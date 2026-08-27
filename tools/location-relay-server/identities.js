'use strict';
const { DatabaseSync } = require('node:sqlite');

// Per-callsign Ed25519 identity store for the Location Relay server.
// Replaces the shared-bearer-token model: each openALE instance registers
// its own keypair, an operator approves it out-of-band via admin-cli.js, and
// every ingest is verified against the stored public key (see auth.js).
//
// Follows the same CREATE TABLE IF NOT EXISTS / prepared-statement idiom as
// db.js. Call attachIdentities(db) with the same DatabaseSync instance db.js
// opened, so both share one connection/transaction domain.

function attachIdentities(db) {
  db.exec(`
    CREATE TABLE IF NOT EXISTS identities (
      callsign    TEXT PRIMARY KEY,
      public_key  TEXT NOT NULL,
      status      TEXT NOT NULL DEFAULT 'pending',
      created_at  INTEGER NOT NULL,
      approved_at INTEGER
    )
  `);

  const stmts = {
    getIdentity: db.prepare(`SELECT * FROM identities WHERE callsign = ?`),
    listIdentities: db.prepare(`SELECT * FROM identities ORDER BY created_at DESC`),
    insertPendingIdentity: db.prepare(`
      INSERT INTO identities (callsign, public_key, status, created_at)
      VALUES (?, ?, 'pending', ?)
    `),
    approveIdentity: db.prepare(`
      UPDATE identities SET status = 'approved', approved_at = ? WHERE callsign = ?
    `),
    revokeIdentity: db.prepare(`
      UPDATE identities SET status = 'revoked' WHERE callsign = ?
    `),
  };

  function getIdentity(callsign) {
    return stmts.getIdentity.get(callsign) || null;
  }

  function insertPendingIdentity(callsign, publicKeyB64) {
    stmts.insertPendingIdentity.run(callsign, publicKeyB64, Date.now());
  }

  function approveIdentity(callsign) {
    return stmts.approveIdentity.run(Date.now(), callsign).changes > 0;
  }

  function revokeIdentity(callsign) {
    return stmts.revokeIdentity.run(callsign).changes > 0;
  }

  function listIdentities() {
    return stmts.listIdentities.all();
  }

  return { getIdentity, insertPendingIdentity, approveIdentity, revokeIdentity, listIdentities };
}

// Opens its own DatabaseSync connection to the same sqlite file db.js/
// db-worker.js use. WAL mode (set by db.js on first open, and persisted in
// the file itself) allows this independent connection to read/write the
// identities table concurrently with the DB worker thread's connection —
// they touch disjoint tables (identities vs reports/stations/
// station_observers), so there is no cross-table contention. Used by
// server.js's main thread (register/auth need synchronous per-request
// lookups without a worker round trip) and by admin-cli.js (a separate
// process entirely).
function openIdentitiesDb(dbPath) {
  const db = new DatabaseSync(dbPath);
  db.exec('PRAGMA journal_mode = WAL');
  return { db, ...attachIdentities(db) };
}

module.exports = { attachIdentities, openIdentitiesDb };
