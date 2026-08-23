'use strict';
// Phase D companion service for docs/LOCATION_SHARING_CONCEPT.md — the
// external web API + map frontend the concept doc deliberately keeps out of
// openALE's own C++ core (§3: "Externer Web-Service ... NICHT Teil dieses
// Konzepts, nur spezifiziert"). Implements exactly the endpoints/payload
// shape/response-code table from §9 and the station model from §14.
//
// Zero npm dependencies: Node's built-in http + node:sqlite (Node >= 22.5)
// + node:worker_threads.
//
// Architecture: the DB runs on a worker thread (db-worker.js) so synchronous
// node:sqlite operations never block the HTTP event loop — the map stays
// responsive during a fan-in burst (one broadcast heard by many observers
// whose POSTs all arrive at once). Ingests are micro-batched into one
// transaction per flush (~100x throughput vs per-statement auto-commit).
//
// Usage:
//   LOCATION_API_TOKEN=<bearer-token> node server.js
// Config (env, all optional except the token):
//   PORT                     default 8766
//   LOCATION_DB_PATH         default ./location-relay.sqlite
//   LOCATION_TTL_ONLINE_MIN  default 15   (Konzept §14)
//   LOCATION_TTL_RECENT_MIN  default 60
//   LOCATION_TTL_STALE_MIN   default 1440
//   LOCATION_RETENTION_DAYS  default 2    (stations not seen in N days are decayed)
//   LOCATION_DECAY_INTERVAL_MIN default 60 (how often the decay sweep runs)
//   LOCATION_LOG_PATH        default ./location-relay.log ("" disables the file)
//   LOCATION_TRUST_PROXY     default 0    (set 1 to take client IP from X-Forwarded-For)
//   LOCATION_MAX_CONNECTIONS default 1024 (concurrent socket cap; 0 = unlimited)
//   LOCATION_RATE_LIMIT_PER_MIN default 600 (per-IP ingest cap; 0 disables)
//   LOCATION_MAX_OBSERVERS_RESPONSE default 10 (bound the "who heard" list per station)
//   LOCATION_COLLAPSE_BROADCASTS default off (set 1: one report row per broadcast; fresh DB)
//   LOCATION_FLUSH_MS        default 30   (worker: max batch latency before commit)
//   LOCATION_BATCH_MAX       default 512  (worker: flush at this many pending)
//   LOCATION_TLS_CERT_PATH   default ""   (set with LOCATION_TLS_KEY_PATH for native HTTPS)
//   LOCATION_TLS_KEY_PATH    default ""   (unset = plain HTTP; see README.md)

const http = require('node:http');
const https = require('node:https');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { Worker } = require('node:worker_threads');

const PORT = parseInt(process.env.PORT || '8766', 10);
const DB_PATH = process.env.LOCATION_DB_PATH || path.join(__dirname, 'location-relay.sqlite');
const TOKEN = process.env.LOCATION_API_TOKEN || '';
const TTL_ONLINE_MIN = parseInt(process.env.LOCATION_TTL_ONLINE_MIN || '15', 10);
const TTL_RECENT_MIN = parseInt(process.env.LOCATION_TTL_RECENT_MIN || '60', 10);
const TTL_STALE_MIN  = parseInt(process.env.LOCATION_TTL_STALE_MIN  || '1440', 10);
const RETENTION_DAYS = parseInt(process.env.LOCATION_RETENTION_DAYS || '2', 10);
const DECAY_INTERVAL_MIN = parseInt(process.env.LOCATION_DECAY_INTERVAL_MIN || '60', 10);
const MAX_BODY_BYTES = 16 * 1024;  // reports are small (Konzept: AMD-sized payloads)
const LOG_PATH = process.env.LOCATION_LOG_PATH !== undefined ? process.env.LOCATION_LOG_PATH
                                                             : path.join(__dirname, 'location-relay.log');
const TRUST_PROXY = process.env.LOCATION_TRUST_PROXY === '1';
const MAX_CONNECTIONS = parseInt(process.env.LOCATION_MAX_CONNECTIONS || '1024', 10);
const RATE_LIMIT_PER_MIN = parseInt(process.env.LOCATION_RATE_LIMIT_PER_MIN || '600', 10);
const MAX_OBSERVERS_RESPONSE = parseInt(process.env.LOCATION_MAX_OBSERVERS_RESPONSE || '10', 10);
const TLS_CERT_PATH = process.env.LOCATION_TLS_CERT_PATH || '';
const TLS_KEY_PATH  = process.env.LOCATION_TLS_KEY_PATH  || '';

// ── Logging (terminal + file) ───────────────────────────────────────────────
// Zero-dep: every log line goes to stdout AND an append-only file. Client-
// supplied fields are scrubbed of control chars first, so a callsign/observer
// can't forge log lines or inject terminal escape sequences. The DB worker
// forwards its own log lines here too (single writer to the file).
const LOG_FILE = LOG_PATH
  ? fs.createWriteStream(LOG_PATH, { flags: 'a' })
  : null;
if (LOG_FILE) LOG_FILE.on('error', (e) => console.error(`[ERROR] log file ${LOG_PATH} unusable: ${e.message}`));

function logSafe(s) { return String(s).replace(/[\x00-\x1f\x7f]/g, ' '); }

function log(level, msg) {
  const line = `${new Date().toISOString()} [${level}] ${logSafe(msg)}\n`;
  process.stdout.write(line);
  if (LOG_FILE) LOG_FILE.write(line);
}

function clientIp(req) {
  if (TRUST_PROXY) {
    const xff = req.headers['x-forwarded-for'];
    if (xff) return xff.split(',')[0].trim();
  }
  return (req.socket && req.socket.remoteAddress) || '';
}

// Per-IP ingest rate limit (fixed window). Guards against one peer flooding
// the event loop with POSTs. When LOCATION_TRUST_PROXY=1 all stations may
// share the proxy IP — raise LOCATION_RATE_LIMIT_PER_MIN or set 0 to disable.
const rateBuckets = new Map();  // ip -> { count, windowEnd }
function rateLimitOk(ip) {
  if (RATE_LIMIT_PER_MIN <= 0 || !ip) return true;
  const now = Date.now();
  let b = rateBuckets.get(ip);
  if (!b || now > b.windowEnd) {
    b = { count: 0, windowEnd: now + 60000 };
    rateBuckets.set(ip, b);
  }
  b.count++;
  return b.count <= RATE_LIMIT_PER_MIN;
}

if (!TOKEN) {
  log('ERROR', "LOCATION_API_TOKEN is not set — refusing to start with an open ingest endpoint.");
  log('ERROR', "Set it to the same bearer token configured in openALE's Location Relay settings.");
  process.exit(1);
}

// ── DB worker (owns the DatabaseSync; batches writes; serves reads) ─────────
const worker = new Worker(path.join(__dirname, 'db-worker.js'), { workerData: { dbPath: DB_PATH } });
const pendingReqs = new Map();   // reqId -> { resolve, reject }
let reqSeq = 0;
let workerReadyResolve;
const workerReady = new Promise((resolve) => { workerReadyResolve = resolve; });

function dbSend(msg) {
  const reqId = ++reqSeq;
  return new Promise((resolve, reject) => {
    pendingReqs.set(reqId, { resolve, reject });
    // 10s safety timeout — the HTTP requestTimeout (5s) will already have
    // closed the socket for an unresponsive worker; this reclaims the slot.
    const t = setTimeout(() => {
      if (pendingReqs.has(reqId)) {
        pendingReqs.delete(reqId);
        reject(new Error('db worker timeout'));
      }
    }, 10000);
    t.unref();
    worker.postMessage({ ...msg, reqId });
  });
}

worker.on('message', (m) => {
  if (m.type === 'ready') { workerReadyResolve(); return; }
  if (m.type === 'log') { log(m.level, m.msg); return; }
  if (m.type === 'ingestResult') {
    for (const r of m.results) {
      const p = pendingReqs.get(r.reqId);
      if (p) { pendingReqs.delete(r.reqId); p.resolve(r); }
    }
    return;
  }
  // Single-reply read / decay / error results.
  const p = pendingReqs.get(m.reqId);
  if (p) {
    pendingReqs.delete(m.reqId);
    if (m.type === 'error') p.reject(new Error(m.error));
    else p.resolve(m);
  }
});
worker.on('error', (e) => log('ERROR', `DB worker error: ${e.message}`));
worker.on('exit', (code) => {
  if (code !== 0) log('ERROR', `DB worker exited with code ${code}`);
  if (shuttingDown) process.exit(0);
});

const PUBLIC_DIR = path.join(__dirname, 'public');

// ── Retention decay ─────────────────────────────────────────────────────────
// Stations not heard within RETENTION_DAYS are dropped, and the append-only
// reports + observer tables are age-pruned to the same window so the DB stays
// bounded at scale. ISO timestamps are all UTC 'Z'-suffixed in one fixed
// format, so lexical comparison == chronological.
async function runDecay() {
  const cutoff = new Date(Date.now() - RETENTION_DAYS * 86400000).toISOString();
  let res;
  try {
    const r = await dbSend({ type: 'decay', cutoff });
    res = r.res;
  } catch (e) {
    log('ERROR', `[decay] failed: ${e.message}`);
    return;
  }
  if (res.stations || res.reportsPruned) {
    log('INFO', `[decay] removed ${res.stations} station(s), pruned ${res.reportsPruned} old report(s) (cutoff ${cutoff})`);
  }
}
// Sweep once on boot (after the worker is ready), then on the interval.
workerReady.then(runDecay);
setInterval(() => { workerReady.then(runDecay); }, DECAY_INTERVAL_MIN * 60000);
// Drop expired rate-limit buckets so the Map can't grow unbounded with IPs.
setInterval(() => {
  const now = Date.now();
  for (const [ip, b] of rateBuckets) if (b.windowEnd < now) rateBuckets.delete(ip);
}, 60000).unref();

// ── Helpers ─────────────────────────────────────────────────────────────────

function sendJson(res, status, body) {
  const buf = Buffer.from(JSON.stringify(body));
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': buf.length,
    'Access-Control-Allow-Origin': '*',
  });
  res.end(buf);
}

function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(Object.assign(new Error('payload too large'), { code: 'TOO_LARGE' }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

// Constant-time bearer check (Konzept §10 — simple opaque token, but no
// reason to leak comparison timing).
function isAuthorized(req) {
  const hdr = req.headers['authorization'] || '';
  const m = /^Bearer\s+(.+)$/.exec(hdr);
  if (!m) return false;
  const given = Buffer.from(m[1]);
  const want = Buffer.from(TOKEN);
  if (given.length !== want.length) return false;
  return crypto.timingSafeEqual(given, want);
}

function nowIso() { return new Date().toISOString(); }

// Konzept §14's TTL table, applied to last_seen_at.
function stationState(lastSeenAtIso) {
  const ageMin = (Date.now() - Date.parse(lastSeenAtIso)) / 60000;
  if (ageMin <= TTL_ONLINE_MIN) return 'ONLINE';
  if (ageMin <= TTL_RECENT_MIN) return 'RECENT';
  if (ageMin <= TTL_STALE_MIN) return 'STALE';
  return 'OFFLINE';
}

function stationToFeature(row) {
  return {
    type: 'Feature',
    geometry: { type: 'Point', coordinates: [row.last_lon, row.last_lat] },
    properties: {
      callsign: row.source,
      altitude: row.last_altitude,
      altitude_unit: row.last_altitude_unit,
      position_timestamp: row.last_position_timestamp,
      last_seen_at: row.last_seen_at,
      last_observer: row.last_observer,
      frequency_hz: row.last_frequency_hz,
      report_count: row.report_count,
      state: stationState(row.last_seen_at),
    },
  };
}

// ── Route handlers ─────────────────────────────────────────────────────────

async function handleIngest(req, res) {
  const ip = clientIp(req);
  if (!rateLimitOk(ip)) {
    log('WARN', `ingest 429 from ${ip} (rate limit ${RATE_LIMIT_PER_MIN}/min)`);
    sendJson(res, 429, { error: 'rate limit exceeded' });
    return;
  }
  if (!isAuthorized(req)) {
    log('WARN', `ingest 401 from ${ip} (bad/missing token)`);
    sendJson(res, 401, { error: 'unauthorized' });
    return;
  }
  let raw;
  try {
    raw = await readBody(req, MAX_BODY_BYTES);
  } catch (e) {
    if (e && e.code === 'TOO_LARGE') {
      log('WARN', `ingest 413 from ${ip} (payload too large)`);
      sendJson(res, 413, { error: 'payload too large' });
    } else {
      log('WARN', `ingest 400 from ${ip} (read error: ${(e && e.message) || e})`);
      sendJson(res, 400, { error: 'bad request' });
    }
    return;
  }

  let body;
  try { body = JSON.parse(raw); } catch { body = null; }
  if (!body || typeof body !== 'object') {
    log('WARN', `ingest 422 from ${ip} (malformed JSON body)`);
    sendJson(res, 422, { error: 'malformed JSON body' });
    return;
  }

  const observer = typeof body.observer === 'string' ? body.observer.trim() : '';
  const source = typeof body.source === 'string' ? body.source.trim() : '';
  const rawGpr = typeof body.raw_gpr === 'string' ? body.raw_gpr : '';
  if (!observer || !source) {
    log('WARN', `ingest 422 from ${ip} (missing observer/source)`);
    sendJson(res, 422, { error: 'observer and source are required' });
    return;
  }

  const hasLat = typeof body.latitude === 'number' && Number.isFinite(body.latitude);
  const hasLon = typeof body.longitude === 'number' && Number.isFinite(body.longitude);
  if (hasLat !== hasLon) {
    log('WARN', `ingest 422 from ${ip} source=${source} (lat/lon mismatch)`);
    sendJson(res, 422, { error: 'latitude and longitude must both be present or both be null' });
    return;
  }
  if (hasLat && (body.latitude < -90 || body.latitude > 90)) {
    sendJson(res, 422, { error: 'latitude out of range' });
    return;
  }
  if (hasLon && (body.longitude < -180 || body.longitude > 180)) {
    sendJson(res, 422, { error: 'longitude out of range' });
    return;
  }

  const relay = typeof body.relay === 'string' ? body.relay : '';
  const sourceType = typeof body.source_type === 'string' && body.source_type ? body.source_type : 'ale_gpr';
  const altitude = typeof body.altitude === 'number' ? body.altitude : null;
  const altitudeUnit = typeof body.altitude_unit === 'string' ? body.altitude_unit : null;
  const timestamp = typeof body.timestamp === 'string' ? body.timestamp : null;
  const receivedAt = typeof body.received_at === 'string' ? body.received_at : nowIso();
  const callType = typeof body.call_type === 'string' && body.call_type ? body.call_type : 'UNKNOWN';
  const comment = typeof body.comment === 'string' ? body.comment : '';
  // RX frequency the report arrived on (openALE sends ctrl current-channel
  // rx_frequency_hz). Coerce to a non-negative integer; 0 = unknown/none.
  const frequencyHz = (typeof body.frequency_hz === 'number' && Number.isFinite(body.frequency_hz))
    ? Math.max(0, Math.floor(body.frequency_hz))
    : 0;

  // Hand the validated report to the DB worker; it is persisted in the next
  // batch flush (≤ LOCATION_FLUSH_MS). The await resolves once committed, so a
  // 201 here means the report is durable — no loss on a later crash.
  let r;
  try {
    r = await dbSend({
      type: 'ingest',
      fields: {
        observer, source, relay, sourceType, rawGpr,
        latitude:  hasLat ? body.latitude  : null,
        longitude: hasLon ? body.longitude : null,
        altitude, altitudeUnit, timestamp, receivedAt, callType, comment,
        frequencyHz, hasPosition: hasLat && hasLon,
      },
    });
  } catch (e) {
    log('ERROR', `ingest 500 from ${ip} source=${source} (worker: ${e.message})`);
    sendJson(res, 500, { error: 'storage error' });
    return;
  }

  if (r.status === 201) {
    log('INFO', `ingest 201 from ${ip} source=${source} observer=${observer} freq=${frequencyHz} pos=${hasLat ? 'yes' : 'no'} (#${r.id})`);
    sendJson(res, 201, { ok: true, id: r.id });
  } else if (r.status === 409) {
    log('INFO', `ingest 409 from ${ip} source=${source} (duplicate report)`);
    sendJson(res, 409, { error: 'duplicate report' });
  } else {
    log('ERROR', `ingest 500 from ${ip} source=${source} (batch: ${r.error || 'storage error'})`);
    sendJson(res, 500, { error: 'storage error' });
  }
}

async function handleListLocations(_req, res) {
  let r;
  try { r = await dbSend({ type: 'listLocations' }); }
  catch (e) { sendJson(res, 500, { error: 'storage error' }); return; }
  sendJson(res, 200, { type: 'FeatureCollection', features: r.rows.map(stationToFeature) });
}

async function handleListStations(_req, res) {
  let r;
  try { r = await dbSend({ type: 'listStations' }); }
  catch (e) { sendJson(res, 500, { error: 'storage error' }); return; }
  sendJson(res, 200, r.rows.map((row) => ({
    callsign: row.source,
    latitude: row.last_lat,
    longitude: row.last_lon,
    last_seen_at: row.last_seen_at,
    last_observer: row.last_observer,
    frequency_hz: row.last_frequency_hz,
    report_count: row.report_count,
    state: stationState(row.last_seen_at),
  })));
}

async function handleStationDetail(_req, res, id) {
  let r;
  try { r = await dbSend({ type: 'stationDetail', id, limit: MAX_OBSERVERS_RESPONSE }); }
  catch (e) { sendJson(res, 500, { error: 'storage error' }); return; }
  const row = r.row;
  if (!row) { sendJson(res, 404, { error: 'unknown station' }); return; }
  sendJson(res, 200, {
    callsign: row.source,
    latitude: row.last_lat,
    longitude: row.last_lon,
    altitude: row.last_altitude,
    altitude_unit: row.last_altitude_unit,
    position_timestamp: row.last_position_timestamp,
    comment: row.last_comment,
    raw_gpr: row.last_raw_gpr,
    call_type: row.last_call_type,
    last_seen_at: row.last_seen_at,
    last_observer: row.last_observer,
    frequency_hz: row.last_frequency_hz,
    report_count: row.report_count,
    state: stationState(row.last_seen_at),
    // Bounded: the N most-recent observers + the TOTAL count, so the client
    // can render "heard by <count> — showing <N> most recent" instead of a
    // thousands-row list for a popular station.
    observer_count: r.observer_count,
    observers: r.observers,
  });
}

// ── Static file serving (map frontend) ────────────────────────────────────

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json' };

function serveStatic(req, res, urlPath) {
  const rel = urlPath === '/' ? '/index.html' : urlPath;
  // path.join (not path.resolve) against PUBLIC_DIR first: rel carries a
  // leading slash, and path.resolve treats an absolute second argument as
  // overriding everything before it (on Windows this discards PUBLIC_DIR
  // entirely and resolves to the drive root, e.g. "C:\index.html" — a 403
  // for every request). path.join has no such special-case; it always
  // concatenates. The outer path.resolve then normalizes ".." segments so
  // the containment check below still catches traversal attempts (the
  // +sep check rejects a sibling directory whose name merely starts with
  // PUBLIC_DIR, which a bare startsWith would miss).
  const resolved = path.resolve(path.join(PUBLIC_DIR, rel));
  if (resolved !== PUBLIC_DIR && !resolved.startsWith(PUBLIC_DIR + path.sep)) {
    res.writeHead(403);
    res.end();
    return;
  }
  fs.readFile(resolved, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    const ext = path.extname(resolved);
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
}

// ── Router ──────────────────────────────────────────────────────────────────

function requestListener(req, res) {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const start = Date.now();

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Authorization, Content-Type, Idempotency-Key',
    });
    res.end();
    return;
  }

  // Access log for read endpoints (the ingest POST is logged in detail inside
  // handleIngest with source/observer/freq, so it isn't double-logged here).
  if (url.pathname.startsWith('/api/') && req.method === 'GET') {
    res.on('finish', () => {
      log('INFO', `${req.method} ${url.pathname} ${res.statusCode} ${Date.now() - start}ms from ${clientIp(req)}`);
    });
  }

  if (url.pathname === '/healthz') { sendJson(res, 200, { ok: true }); return; }

  if (url.pathname === '/api/v1/locations' && req.method === 'POST') { handleIngest(req, res); return; }
  if (url.pathname === '/api/v1/locations' && req.method === 'GET')  { handleListLocations(req, res); return; }
  if (url.pathname === '/api/v1/stations'  && req.method === 'GET')  { handleListStations(req, res); return; }
  const stationMatch = /^\/api\/v1\/stations\/([^/]+)$/.exec(url.pathname);
  if (stationMatch && req.method === 'GET') { handleStationDetail(req, res, decodeURIComponent(stationMatch[1])); return; }

  if (url.pathname.startsWith('/api/')) { sendJson(res, 404, { error: 'unknown endpoint' }); return; }

  if (req.method === 'GET') { serveStatic(req, res, url.pathname); return; }

  res.writeHead(405);
  res.end();
}

// TLS is optional and native (node:https — no new dependency): set both
// LOCATION_TLS_CERT_PATH and LOCATION_TLS_KEY_PATH to terminate HTTPS here
// directly, e.g. against a self-signed cert for a LAN deployment (see
// README.md for a one-line generation recipe). Leave both unset to keep
// plain HTTP and terminate TLS at a reverse proxy instead — both are valid,
// this just avoids requiring the proxy for the common self-signed-LAN case.
const TLS_ENABLED = !!(TLS_CERT_PATH && TLS_KEY_PATH);
let server;
if (TLS_ENABLED) {
  let cert, key;
  try {
    cert = fs.readFileSync(TLS_CERT_PATH);
    key = fs.readFileSync(TLS_KEY_PATH);
  } catch (e) {
    log('ERROR', `failed to read TLS cert/key (LOCATION_TLS_CERT_PATH=${TLS_CERT_PATH}, ` +
                 `LOCATION_TLS_KEY_PATH=${TLS_KEY_PATH}): ${e.message}`);
    process.exit(1);
  }
  server = https.createServer({ cert, key }, requestListener);
} else {
  server = http.createServer(requestListener);
}

// ── Capacity / DoS hardening ────────────────────────────────────────────────
// Reports are tiny short-burst POSTs (see openALE's LocationRelayService), so
// tight timeouts are safe and stop a slow client from holding a socket open.
// maxConnections caps concurrent sockets (slowloris / socket exhaustion). The
// DB worker keeps the event loop free regardless of write burst size.
if (MAX_CONNECTIONS > 0) server.maxConnections = MAX_CONNECTIONS;
server.requestTimeout = 5000;   // whole request must finish in 5s (≤16KB body)
server.headersTimeout = 60000;  // 60s to receive headers
server.keepAliveTimeout = 5000; // idle keep-alive socket closed after 5s

// ── Graceful shutdown: stop accepting, flush the worker, exit ───────────────
let shuttingDown = false;
function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  log('INFO', 'shutdown signal — closing server, flushing DB worker');
  // Tell the worker to flush + close immediately — don't wait for server.close
  // (lingering keep-alive sockets can delay its callback, and the worker's
  // pending-batch flush must happen before we exit either way).
  worker.postMessage({ type: 'shutdown' });
  server.close();
  // Hard exit if the worker doesn't exit on its own within 5s.
  setTimeout(() => process.exit(0), 5000).unref();
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

server.listen(PORT, () => {
  const scheme = TLS_ENABLED ? 'https' : 'http';
  log('INFO', `Location Relay server listening on :${PORT} (bound 0.0.0.0, TLS ${TLS_ENABLED ? 'on' : 'off'})`);
  log('INFO', `  Ingest:  POST ${scheme}://<host>:${PORT}/api/v1/locations  (Bearer token required)`);
  log('INFO', `  Map:     ${scheme}://<host>:${PORT}/`);
  log('INFO', `  DB:      ${DB_PATH}  (owned by db-worker.js)`);
  log('INFO', `  Log:     ${LOG_PATH || '(file disabled, stdout only)'}`);
  log('INFO', `  Decay:   stations not seen in ${RETENTION_DAYS}d removed every ${DECAY_INTERVAL_MIN}min`);
  log('INFO', `  Limits:  maxConnections=${MAX_CONNECTIONS || 'unlimited'}, rateLimit=${RATE_LIMIT_PER_MIN || 'off'}/min/IP, trustProxy=${TRUST_PROXY}`);
});