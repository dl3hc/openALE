'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { openIdentitiesDb } = require('../identities');

function tmpDbPath() {
  return path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'relay-identities-')), 'test.sqlite');
}

test('a new callsign registers as pending', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  identities.insertPendingIdentity('NEW1', Buffer.alloc(32, 1).toString('base64'));
  const row = identities.getIdentity('NEW1');
  assert.ok(row);
  assert.equal(row.status, 'pending');
  identities.db.close();
});

test('registering the same callsign twice is a caller-level duplicate concern', () => {
  // identities.js itself has a PRIMARY KEY on callsign; a second insert for
  // the same callsign throws (server.js's handleRegister checks getIdentity
  // first and returns 200/409 instead of calling insert again).
  const identities = openIdentitiesDb(tmpDbPath());
  identities.insertPendingIdentity('NEW2', Buffer.alloc(32, 2).toString('base64'));
  assert.throws(() => identities.insertPendingIdentity('NEW2', Buffer.alloc(32, 3).toString('base64')));
  identities.db.close();
});

test('approve transitions pending to approved with a timestamp', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  identities.insertPendingIdentity('NEW3', Buffer.alloc(32, 4).toString('base64'));
  const changed = identities.approveIdentity('NEW3');
  assert.equal(changed, true);
  const row = identities.getIdentity('NEW3');
  assert.equal(row.status, 'approved');
  assert.ok(row.approved_at);
});

test('approve on an unknown callsign is a no-op (returns false)', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  assert.equal(identities.approveIdentity('GHOST'), false);
});

test('revoke transitions approved to revoked', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  identities.insertPendingIdentity('NEW4', Buffer.alloc(32, 5).toString('base64'));
  identities.approveIdentity('NEW4');
  const changed = identities.revokeIdentity('NEW4');
  assert.equal(changed, true);
  assert.equal(identities.getIdentity('NEW4').status, 'revoked');
});

test('listIdentities returns all registered identities', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  identities.insertPendingIdentity('NEW5', Buffer.alloc(32, 6).toString('base64'));
  identities.insertPendingIdentity('NEW6', Buffer.alloc(32, 7).toString('base64'));
  const rows = identities.listIdentities();
  assert.equal(rows.length, 2);
});
