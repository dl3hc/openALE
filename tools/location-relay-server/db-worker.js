'use strict';
// DB owner thread for the Location Relay server. Runs on a worker_threads
// worker so synchronous node:sqlite operations NEVER block the HTTP event
// loop — that is the responsiveness lever for the fan-in burst (one broadcast
// heard by many observers whose POSTs all arrive at once).
//
// Ingest reports are micro-batched and flushed in ONE transaction (many
// statements, single commit). That is the throughput lever: per-statement
// auto-commit does an fsync per row (~230 rows/s); one commit for N rows is
// ~100x faster. A caught UNIQUE violation rolls back only the offending
// statement (SQLite default ON CONFLICT ABORT), so per-item 409s are detected
// inside the same BEGIN…COMMIT without aborting the batch.
//
// Reads drain pending ingests first so a GET reflects the latest POSTs, then
// run synchronously. The worker is single-threaded, so a read arriving during
// a flush waits for that flush to finish — but flushes are ~ms now, not
// seconds, so reads stay fast.
//
// Env (inherited from the parent process):
//   LOCATION_FLUSH_MS   default 30  (max latency before a batch is committed)
//   LOCATION_BATCH_MAX  default 512 (flush immediately when this many pending)

const { parentPort, workerData } = require('node:worker_threads');
const { openDb } = require('./db');

const FLUSH_MS = parseInt(process.env.LOCATION_FLUSH_MS || '30', 10);
const BATCH_MAX = parseInt(process.env.LOCATION_BATCH_MAX || '512', 10);

const { db, stmts, decayOlderThan } = openDb(workerData.dbPath);

const pending = [];     // ingest items awaiting the next flush
let flushTimer = null;

function logToParent(level, msg) { parentPort.postMessage({ type: 'log', level, msg }); }

// Flush the pending batch in a single transaction. Per-item UNIQUE failures
// become 409s; any other error rolls the whole batch back and 500s its items.
function flush() {
  if (flushTimer) { clearTimeout(flushTimer); flushTimer = null; }
  if (!pending.length) return;
  const batch = pending.splice(0);
  const results = [];
  db.exec('BEGIN');
  try {
    for (const it of batch) {
      let status = 201, id = null;
      try {
        id = stmts.insertReport.run(
          it.observer, it.source, it.relay, it.sourceType, it.rawGpr,
          it.latitude, it.longitude, it.altitude, it.altitudeUnit,
          it.timestamp, it.receivedAt, it.callType, it.comment, it.frequencyHz,
        ).lastInsertRowid;
      } catch (e) {
        if (String(e && e.message).includes('UNIQUE constraint failed')) {
          status = 409;  // broadcast already recorded — but this observer still heard it
        } else {
          throw e;  // non-constraint error: abort + rollback the whole batch
        }
      }
      // Station + observer upserts ALWAYS run, whether this item won the
      // report-row dedup (201) or collided with an existing broadcast (409).
      // A 409 still means this observer/relay just heard the source at
      // receivedAt — the map's last_seen_at/last_lat/last_lon must advance
      // for every hearing, not just the first one recorded in the append-only
      // reports table (this previously left stations stale under
      // LOCATION_COLLAPSE_BROADCASTS=1, where all but the first observer of a
      // broadcast get 409). Re-writing the same position on a dedup hit is
      // harmless (idempotent — it's the same broadcast).
      if (it.hasPosition) {
        stmts.upsertStationWithPosition.run(
          it.source, it.latitude, it.longitude, it.altitude, it.altitudeUnit,
          it.timestamp, it.comment, it.rawGpr, it.callType, it.receivedAt,
          it.observer, it.frequencyHz,
        );
      } else {
        stmts.upsertStationHeardOnly.run(
          it.source, it.comment, it.rawGpr, it.callType, it.receivedAt,
          it.observer, it.frequencyHz,
        );
      }
      stmts.upsertObserver.run(it.source, it.observer, it.receivedAt, it.receivedAt);
      results.push(id !== null
        ? { reqId: it.reqId, status: 201, id }
        : { reqId: it.reqId, status });
    }
    db.exec('COMMIT');
  } catch (e) {
    try { db.exec('ROLLBACK'); } catch (_) {}
    logToParent('ERROR', `batch flush rolled back ${batch.length} item(s): ${e.message}`);
    for (const it of batch) {
      if (!results.some((r) => r.reqId === it.reqId))
        results.push({ reqId: it.reqId, status: 500, error: 'storage error' });
    }
  }
  if (results.length) parentPort.postMessage({ type: 'ingestResult', results });
}

function scheduleFlush() {
  if (flushTimer) return;
  flushTimer = setTimeout(flush, FLUSH_MS);
}

// Drain pending ingests, then run a read — so reads see the latest writes.
function drainThen(fn) {
  flush();
  return fn();
}

parentPort.on('message', (m) => {
  try {
    switch (m.type) {
      case 'ingest':
        // reqId rides alongside the fields so flush can tag each result; the
        // main thread matches the reply to its waiting request by reqId.
        pending.push({ reqId: m.reqId, ...m.fields });
        if (pending.length >= BATCH_MAX) flush(); else scheduleFlush();
        break;
      case 'listLocations': {
        const rows = drainThen(() => stmts.listMappedStations.all());
        parentPort.postMessage({ type: 'listLocationsResult', reqId: m.reqId, rows });
        break;
      }
      case 'listStations': {
        const rows = drainThen(() => stmts.listAllStations.all());
        parentPort.postMessage({ type: 'listStationsResult', reqId: m.reqId, rows });
        break;
      }
      case 'stationDetail': {
        const row = drainThen(() => stmts.getStation.get(m.id));
        // Bounded observer list (most-recent N) + total count, so a station
        // heard by thousands doesn't ship a thousands-row list to the client.
        const observers = row ? stmts.getRecentObservers.all(m.id, m.limit || 10) : [];
        const observer_count = row ? stmts.countObservers.get(m.id).n : 0;
        parentPort.postMessage({ type: 'stationDetailResult', reqId: m.reqId, row, observers, observer_count });
        break;
      }
      case 'decay': {
        flush();
        const res = decayOlderThan(m.cutoff);
        parentPort.postMessage({ type: 'decayResult', reqId: m.reqId, res });
        break;
      }
      case 'shutdown':
        flush();
        db.close();
        parentPort.close();
        process.exit(0);
        break;
    }
  } catch (e) {
    logToParent('ERROR', `worker handler failed (${m && m.type}): ${e.message}`);
    if (m && m.reqId) parentPort.postMessage({ type: 'error', reqId: m.reqId, error: e.message });
  }
});

// Signal readiness AFTER openDb + the message handler are in place, so the
// parent never posts before the DB is open and the listener is attached.
parentPort.postMessage({ type: 'ready' });
logToParent('INFO', `DB worker started (flush=${FLUSH_MS}ms, batchMax=${BATCH_MAX})`);