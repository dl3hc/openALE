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
      created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
    )
  `);
  // Exact-duplicate guard (Konzept §9's Idempotency-Key intent, enforced
  // server-side too): the same observer reporting the same source at the
  // same GPR timestamp twice (e.g. a client retry after a lost response) is
  // one report, not two. NULL timestamps never collide in a UNIQUE index
  // (SQLite/ANSI semantics), which is correct here — position-less/no-time
  // reports (§20.5's manual_or_invalid_position) always insert.
  db.exec(`
    CREATE UNIQUE INDEX IF NOT EXISTS ix_reports_dedup
      ON reports(observer, source, timestamp)
  `);
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
      report_count            INTEGER NOT NULL DEFAULT 0
    )
  `);

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
         altitude, altitude_unit, timestamp, received_at, call_type, comment)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `),
    // has_position gate: only overwrite last_lat/lon/etc. when this report
    // actually carries a position (Konzept §17a — a valid_position=false
    // report is still "heard", but must not blank out the last known fix).
    upsertStationWithPosition: db.prepare(`
      INSERT INTO stations
        (source, last_lat, last_lon, last_altitude, last_altitude_unit,
         last_position_timestamp, last_comment, last_raw_gpr, last_call_type,
         last_seen_at, last_observer, report_count)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)
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
        report_count = report_count + 1
    `),
    upsertStationHeardOnly: db.prepare(`
      INSERT INTO stations
        (source, last_comment, last_raw_gpr, last_call_type, last_seen_at,
         last_observer, report_count)
      VALUES (?, ?, ?, ?, ?, ?, 1)
      ON CONFLICT(source) DO UPDATE SET
        last_comment = excluded.last_comment,
        last_raw_gpr = excluded.last_raw_gpr,
        last_call_type = excluded.last_call_type,
        last_seen_at = excluded.last_seen_at,
        last_observer = excluded.last_observer,
        report_count = report_count + 1
    `),
    upsertObserver: db.prepare(`
      INSERT INTO station_observers (source, observer, first_seen_at, last_seen_at, report_count)
      VALUES (?, ?, ?, ?, 1)
      ON CONFLICT(source, observer) DO UPDATE SET
        last_seen_at = excluded.last_seen_at,
        report_count = report_count + 1
    `),
    listMappedStations: db.prepare(`
      SELECT source, last_lat, last_lon, last_altitude, last_altitude_unit,
             last_position_timestamp, last_seen_at, last_observer, report_count
      FROM stations
      WHERE last_lat IS NOT NULL AND last_lon IS NOT NULL
      ORDER BY last_seen_at DESC
    `),
    listAllStations: db.prepare(`
      SELECT source, last_lat, last_lon, last_seen_at, last_observer, report_count
      FROM stations
      ORDER BY last_seen_at DESC
    `),
    getStation: db.prepare(`SELECT * FROM stations WHERE source = ?`),
    getObservers: db.prepare(`
      SELECT observer, first_seen_at, last_seen_at, report_count
      FROM station_observers WHERE source = ?
      ORDER BY last_seen_at DESC
    `),
  };

  return { db, stmts };
}

module.exports = { openDb };
