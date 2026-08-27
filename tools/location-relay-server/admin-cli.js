#!/usr/bin/env node
'use strict';
// Admin CLI for the Location Relay identities table. Operates directly on
// the sqlite file — no HTTP admin surface, no new secret to manage, matching
// the operator's existing direct filesystem access to the server.
//
// Usage:
//   node admin-cli.js list
//   node admin-cli.js approve <callsign>
//   node admin-cli.js revoke <callsign>

const path = require('node:path');
const { openIdentitiesDb } = require('./identities');

const DB_PATH = process.env.LOCATION_DB_PATH || path.join(__dirname, 'location-relay.sqlite');

function fmtDate(ms) {
  return ms ? new Date(ms).toISOString() : '-';
}

function main() {
  const [, , cmd, arg] = process.argv;
  const identities = openIdentitiesDb(DB_PATH);

  switch (cmd) {
    case 'list': {
      const rows = identities.listIdentities();
      if (!rows.length) {
        console.log('(no identities registered)');
        break;
      }
      console.log('CALLSIGN'.padEnd(16), 'STATUS'.padEnd(10), 'CREATED'.padEnd(24), 'APPROVED');
      for (const r of rows) {
        console.log(
          r.callsign.padEnd(16),
          r.status.padEnd(10),
          fmtDate(r.created_at).padEnd(24),
          fmtDate(r.approved_at),
        );
      }
      break;
    }
    case 'approve': {
      if (!arg) { console.error('usage: node admin-cli.js approve <callsign>'); process.exitCode = 1; break; }
      const callsign = arg.trim().toUpperCase();
      const identity = identities.getIdentity(callsign);
      if (!identity) { console.error(`unknown callsign: ${callsign}`); process.exitCode = 1; break; }
      identities.approveIdentity(callsign);
      console.log(`approved ${callsign}`);
      break;
    }
    case 'revoke': {
      if (!arg) { console.error('usage: node admin-cli.js revoke <callsign>'); process.exitCode = 1; break; }
      const callsign = arg.trim().toUpperCase();
      const identity = identities.getIdentity(callsign);
      if (!identity) { console.error(`unknown callsign: ${callsign}`); process.exitCode = 1; break; }
      identities.revokeIdentity(callsign);
      console.log(`revoked ${callsign}`);
      break;
    }
    default:
      console.log('usage: node admin-cli.js list|approve <callsign>|revoke <callsign>');
      process.exitCode = cmd ? 1 : 0;
  }

  identities.db.close();
}

main();
