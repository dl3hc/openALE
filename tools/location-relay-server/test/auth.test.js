'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { verifyEd25519Auth } = require('../auth');
const { openIdentitiesDb } = require('../identities');

function tmpDbPath() {
  return path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'relay-auth-')), 'test.sqlite');
}

// Real cross-implementation interop: uses Node's own keypair generator (not
// the client's vendored ed25519), exercising the exact wire format the
// server expects from an independent implementation.
function makeKeypair() {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
  const rawPublic = publicKey.export({ format: 'der', type: 'spki' }).subarray(-32);
  return { publicKey, privateKey, rawPublicB64: rawPublic.toString('base64') };
}

function sign(privateKey, timestamp, body) {
  const msg = Buffer.from(`${timestamp}\n${body}`, 'utf8');
  return crypto.sign(null, msg, privateKey).toString('base64');
}

function fakeReq(headers) {
  return { headers };
}

test('valid signature from an approved identity is accepted', () => {
  const dbPath = tmpDbPath();
  const identities = openIdentitiesDb(dbPath);
  const { privateKey, rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST1', rawPublicB64);
  identities.approveIdentity('TEST1');

  const body = JSON.stringify({ observer: 'TEST1', source: 'WX1ABC' });
  const timestamp = new Date().toISOString();
  const sig = sign(privateKey, timestamp, body);

  const req = fakeReq({
    authorization: 'Ed25519 TEST1',
    'x-timestamp': timestamp,
    'x-signature': sig,
  });
  const result = verifyEd25519Auth(req, body, identities);
  assert.equal(result.ok, true);
  assert.equal(result.callsign, 'TEST1');
  identities.db.close();
});

test('stale timestamp is rejected', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const { privateKey, rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST2', rawPublicB64);
  identities.approveIdentity('TEST2');

  const body = JSON.stringify({ observer: 'TEST2' });
  const staleTimestamp = new Date(Date.now() - 10 * 60 * 1000).toISOString();
  const sig = sign(privateKey, staleTimestamp, body);
  const req = fakeReq({ authorization: 'Ed25519 TEST2', 'x-timestamp': staleTimestamp, 'x-signature': sig });

  const result = verifyEd25519Auth(req, body, identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'stale_timestamp');
  identities.db.close();
});

test('replayed signature is rejected on second use', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const { privateKey, rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST3', rawPublicB64);
  identities.approveIdentity('TEST3');

  const body = JSON.stringify({ observer: 'TEST3' });
  const timestamp = new Date().toISOString();
  const sig = sign(privateKey, timestamp, body);
  const req = fakeReq({ authorization: 'Ed25519 TEST3', 'x-timestamp': timestamp, 'x-signature': sig });

  const first = verifyEd25519Auth(req, body, identities);
  assert.equal(first.ok, true);
  const second = verifyEd25519Auth(req, body, identities);
  assert.equal(second.ok, false);
  assert.equal(second.reason, 'replay');
  identities.db.close();
});

test('unknown callsign is rejected', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const body = JSON.stringify({ observer: 'NOBODY' });
  const timestamp = new Date().toISOString();
  const req = fakeReq({ authorization: 'Ed25519 NOBODY', 'x-timestamp': timestamp, 'x-signature': 'AAAA' });

  const result = verifyEd25519Auth(req, body, identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'unknown_callsign');
  identities.db.close();
});

test('pending identity is rejected with pending_approval', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const { rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST4', rawPublicB64);

  const body = JSON.stringify({ observer: 'TEST4' });
  const timestamp = new Date().toISOString();
  const req = fakeReq({ authorization: 'Ed25519 TEST4', 'x-timestamp': timestamp, 'x-signature': 'AAAA' });

  const result = verifyEd25519Auth(req, body, identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'pending_approval');
  identities.db.close();
});

test('revoked identity is rejected with revoked', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const { privateKey, rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST5', rawPublicB64);
  identities.approveIdentity('TEST5');
  identities.revokeIdentity('TEST5');

  const body = JSON.stringify({ observer: 'TEST5' });
  const timestamp = new Date().toISOString();
  const sig = sign(privateKey, timestamp, body);
  const req = fakeReq({ authorization: 'Ed25519 TEST5', 'x-timestamp': timestamp, 'x-signature': sig });

  const result = verifyEd25519Auth(req, body, identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'revoked');
  identities.db.close();
});

test('tampered body fails signature verification', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const { privateKey, rawPublicB64 } = makeKeypair();
  identities.insertPendingIdentity('TEST6', rawPublicB64);
  identities.approveIdentity('TEST6');

  const body = JSON.stringify({ observer: 'TEST6', source: 'AAA' });
  const timestamp = new Date().toISOString();
  const sig = sign(privateKey, timestamp, body);
  const req = fakeReq({ authorization: 'Ed25519 TEST6', 'x-timestamp': timestamp, 'x-signature': sig });

  const tamperedBody = JSON.stringify({ observer: 'TEST6', source: 'BBB' });
  const result = verifyEd25519Auth(req, tamperedBody, identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'bad_signature');
  identities.db.close();
});

test('missing auth header is rejected', () => {
  const identities = openIdentitiesDb(tmpDbPath());
  const result = verifyEd25519Auth(fakeReq({}), '{}', identities);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'missing_auth_header');
  identities.db.close();
});
