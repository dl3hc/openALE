'use strict';
// SQLite storage for the Location Relay companion server
// (docs/LOCATION_SHARING_CONCEPT.md §14: "station { callsign, last_position,
// last_position_timestamp, last_seen_at, last_observer, observers[],
// report_count }"). Uses Node's built-in node:sqlite — no npm dependencies.

const { DatabaseSync } = require('node:sqlite');

function openDb(path) {
  const db = new DatabaseSync(path);
  db.exec('PRAGMA journal_mode = WAL');
  db.exec('PRAGMA foreign_keys = ON');

  // Additive column migrations: CREATE TABLE IF NOT EXISTS never adds columns
  // to an existing table, so older relay DBs (created before frequency_hz was
  // captured) get upgraded in place here. Idempotent — checks PRAGMA table_info
  // first so a fresh or already-migrated DB is a no-op.
  function addColumnIfMissing(table, column, decl) {
    const cols = db.prepare(`PRAGMA table_info(${table})`).all();
    if (!cols.some((c) => c.name === column)) {
      db.exec(`ALTER TABLE ${table} ADD COLUMN ${column} ${decl}`);
    }
  }

  db.exec(`
    CREATE TABLE IF NOT EXISTS reports (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      observer        TEXT NOT NULL,
      source          TEXT NOT NULL,
      relay           TEXT NOT NULL DEFAULT '',
      source_type     TEXT NOT NULL DEFAULT 'ale_gpr',
      raw_gpr         TEXT NOT NULL DEFAULT '',
      latitude        REAL,
      longitude       REAL,
      altitude        REAL,
      altitude_unit   TEXT,
      timestamp       TEXT,
      received_at     TEXT NOT NULL,
      call_type       TEXT NOT NULL DEFAULT 'UNKNOWN',
      comment         TEXT NOT NULL DEFAULT '',
      frequency_hz    INTEGER NOT NULL DEFAULT 0,
      created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
    )
  `);
  // Dedup guard. Two modes:
  //  - default (LOCATION_COLLAPSE_BROADCASTS unset): one row per
  //    (observer, source, timestamp) — Konzept §9 idempotency intent (a client
  //    retry after a lost response is one report, not two). A broadcast heard
  //    by N observers stores N rows (distinct observers never collide).
  //  - collapse (LOCATION_COLLAPSE_BROADCASTS=1): one row per
  //    (source, timestamp, latitude, longitude) — all N observers of one
  //    broadcast collapse to a single report row (the other N-1 get 409),
  //    shrinking the append-only reports table hundreds×. The "who heard it"
  //    fan-in is still preserved in station_observers.
  // NULL lat/lon/timestamp never collide (SQLite UNIQUE semantics), so
  // position-less/no-time reports always insert in both modes. Collapse needs
  // a fresh (or already-deduped) DB: if existing rows violate the new key,
  // creating the UNIQUE index throws — fall back to per-observer dedup and
  // log so the operator knows to reset the DB to enable collapse.
  const COLLAPSE = process.env.LOCATION_COLLAPSE_BROADCASTS === '1';
  if (COLLAPSE) {
    try {
      db.exec(`DROP INDEX IF EXISTS ix_reports_dedup`);
      db.exec(`CREATE UNIQUE INDEX IF NOT EXISTS ix_reports_collapse ON reports(source, timestamp, latitude, longitude)`);
    } catch (e) {
      db.exec(`CREATE UNIQUE INDEX IF NOT EXISTS ix_reports_dedup ON reports(observer, source, timestamp)`);
      console.error('[db] LOCATION_COLLAPSE_BROADCASTS=1 but duplicate reports exist under the (source,timestamp,position) key; collapse unavailable until the DB is reset. Falling back to per-observer dedup.');
    }
  } else {
    db.exec(`CREATE UNIQUE INDEX IF NOT EXISTS ix_reports_dedup ON reports(observer, source, timestamp)`);
  }
  db.exec(`CREATE INDEX IF NOT EXISTS ix_reports_source ON reports(source)`);

  db.exec(`
    CREATE TABLE IF NOT EXISTS stations (
      source                  TEXT PRIMARY KEY,
      last_lat                REAL,
      last_lon                REAL,
      last_altitude           REAL,
      last_altitude_unit      TEXT,
      last_position_timestamp TEXT,
      last_comment            TEXT NOT NULL DEFAULT '',
      last_raw_gpr            TEXT NOT NULL DEFAULT '',
      last_call_type          TEXT NOT NULL DEFAULT 'UNKNOWN',
      last_seen_at            TEXT NOT NULL,
      last_observer           TEXT NOT NULL,
      last_frequency_hz       INTEGER NOT NULL DEFAULT 0,
      report_count            INTEGER NOT NULL DEFAULT 0
    )
  `);

  // Upgrade pre-frequency DBs in place (no-op on fresh/already-migrated DBs).
  addColumnIfMissing('reports', 'frequency_hz', 'INTEGER NOT NULL DEFAULT 0');
  addColumnIfMissing('stations', 'last_frequency_hz', 'INTEGER NOT NULL DEFAULT 0');

  // listAllStations / listMappedStations ORDER BY last_seen_at DESC — without
  // this index the query does a filesort, which gets slow at ~10k station rows.
  db.exec(`CREATE INDEX IF NOT EXISTS ix_stations_last_seen ON stations(last_seen_at)`);

  db.exec(`
    CREATE TABLE IF NOT EXISTS station_observers (
      source        TEXT NOT NULL,
      observer      TEXT NOT NULL,
      first_seen_at TEXT NOT NULL,
      last_seen_at  TEXT NOT NULL,
      report_count  INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (source, observer)
    )
  `);

  const stmts = {
    insertReport: db.prepare(`
      INSERT INTO reports
        (observer, source, relay, source_type, raw_gpr, latitude, longitude,
         altitude, altitude_unit, timestamp, received_at, call_type, comment,
         frequency_hz)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `),
    // has_position gate: only overwrite last_lat/lon/etc. when this report
    // actually carries a position (Konzept §17a — a valid_position=false
    // report is still "heard", but must not blank out the last known fix).
    upsertStationWithPosition: db.prepare(`
      INSERT INTO stations
        (source, last_lat, last_lon, last_altitude, last_altitude_unit,
         last_position_timestamp, last_comment, last_raw_gpr, last_call_type,
         last_seen_at, last_observer, last_frequency_hz, report_count)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)
      ON CONFLICT(source) DO UPDATE SET
        last_lat = excluded.last_lat,
        last_lon = excluded.last_lon,
        last_altitude = excluded.last_altitude,
        last_altitude_unit = excluded.last_altitude_unit,
        last_position_timestamp = excluded.last_position_timestamp,
        last_comment = excluded.last_comment,
        last_raw_gpr = excluded.last_raw_gpr,
        last_call_type = excluded.last_call_type,
        last_seen_at = excluded.last_seen_at,
        last_observer = excluded.last_observer,
        last_frequency_hz = excluded.last_frequency_hz,
        report_count = report_count + 1
    `),
    upsertStationHeardOnly: db.prepare(`
      INSERT INTO stations
        (source, last_comment, last_raw_gpr, last_call_type, last_seen_at,
         last_observer, last_frequency_hz, report_count)
      VALUES (?, ?, ?, ?, ?, ?, ?, 1)
      ON CONFLICT(source) DO UPDATE SET
        last_comment = excluded.last_comment,
        last_raw_gpr = excluded.last_raw_gpr,
        last_call_type = excluded.last_call_type,
        last_seen_at = excluded.last_seen_at,
        last_observer = excluded.last_observer,
        last_frequency_hz = excluded.last_frequency_hz,
        report_count = report_count + 1
    `),
    upsertObserver: db.prepare(`
      INSERT INTO station_observers (source, observer, first_seen_at, last_seen_at, report_count)
      VALUES (?, ?, ?, ?, 1)
      ON CONFLICT(source, observer) DO UPDATE SET
        last_seen_at = excluded.last_seen_at,
        report_count = report_count + 1
    `),
    // "Reports" as shown to the client is the count of DISTINCT observers
    // currently reporting this station, not the raw ever-incrementing
    // stations.report_count (which never resets and would eventually read
    // as thousands/millions for a long-lived station). station_observers
    // rows already age out with the 2-day decay sweep (decayOlderThan below
    // prunes on last_seen_at), so this count naturally decays with it too —
    // no separate reset logic needed.
    listMappedStations: db.prepare(`
      SELECT source, last_lat, last_lon, last_altitude, last_altitude_unit,
             last_position_timestamp, last_comment, last_seen_at, last_observer, last_frequency_hz,
             (SELECT COUNT(*) FROM station_observers so WHERE so.source = stations.source) AS observer_count
      FROM stations
      WHERE last_lat IS NOT NULL AND last_lon IS NOT NULL
      ORDER BY last_seen_at DESC
    `),
    listAllStations: db.prepare(`
      SELECT source, last_lat, last_lon, last_seen_at, last_observer, last_frequency_hz,
             (SELECT COUNT(*) FROM station_observers so WHERE so.source = stations.source) AS observer_count
      FROM stations
      ORDER BY last_seen_at DESC
    `),
    getStation: db.prepare(`SELECT * FROM stations WHERE source = ?`),
    // Bounded "who heard this station" — the most-recent N + a total count, so
    // a station heard by thousands of observers doesn't return a thousands-row
    // list to the map / detail API. LIMIT is parameterized (? placeholder).
    getRecentObservers: db.prepare(`
      SELECT observer, first_seen_at, last_seen_at, report_count
      FROM station_observers WHERE source = ?
      ORDER BY last_seen_at DESC LIMIT ?
    `),
    countObservers: db.prepare(`SELECT COUNT(*) AS n FROM station_observers WHERE source = ?`),
    getObservers: db.prepare(`
      SELECT observer, first_seen_at, last_seen_at, report_count
      FROM station_observers WHERE source = ?
      ORDER BY last_seen_at DESC
    `),
  };

  // Retention decay: drop stations whose last sighting is older than the given
  // cutoff ISO timestamp, AND age-prune the append-only reports +
  // station_observers tables to the same window. The append-only tables are
  // write-only history (no endpoint reads them); age-pruning bounds growth at
  // scale (~10k stations) while preserving in-window fresh-resubmit dedup via
  // the reports UNIQUE index. Synchronous statements — Node + node:sqlite are
  // single-threaded so the subquery result is stable across them.
  function decayOlderThan(cutoffIso) {
    const reportsPruned = db.prepare(
      `DELETE FROM reports WHERE received_at < ?`
    ).run(cutoffIso).changes;
    db.prepare(
      `DELETE FROM station_observers WHERE last_seen_at < ?`
    ).run(cutoffIso);
    const stations = db.prepare(
      `SELECT COUNT(*) AS n FROM stations WHERE last_seen_at < ?`
    ).get(cutoffIso).n;
    if (stations) {
      // Observer rollups for a fully-decayed source are gone with it.
      db.prepare(
        `DELETE FROM station_observers WHERE source IN (SELECT source FROM stations WHERE last_seen_at < ?)`
      ).run(cutoffIso);
      db.prepare(`DELETE FROM stations WHERE last_seen_at < ?`).run(cutoffIso);
    }
    return { stations, reportsPruned };
  }

  return { db, stmts, decayOlderThan };
}

module.exports = { openDb };
