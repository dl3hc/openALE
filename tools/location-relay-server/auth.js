'use strict';
// Ed25519 per-callsign auth for the Location Relay ingest endpoint.
// Replaces the shared LOCATION_API_TOKEN bearer check.
//
// Wire format (client side: include/App/relay_identity.h, http_poster.cpp):
//   Authorization: Ed25519 <callsign>
//   X-Timestamp:   <ISO8601 UTC, e.g. 2026-08-27T12:34:56.789Z>
//   X-Signature:   <base64 signature over "<timestamp>\n<raw JSON body bytes>">
//
// crypto.verify('Ed25519', ...) is Node's built-in Ed25519 verification
// (native since Node 12; this project already requires Node >=22.5) — no new
// dependency. The public key is stored as raw 32-byte base64 and wrapped into
// a SPKI DER buffer for crypto.createPublicKey (Node has no raw-Ed25519-key
// constructor).

const crypto = require('node:crypto');

const MAX_SKEW_MS = 300 * 1000;

// Static SPKI prefix for an Ed25519 public key (RFC 8410): a fixed 12-byte
// ASN.1 header followed by the raw 32-byte key. This is the standard trick
// for feeding a raw Ed25519 public key to Node's KeyObject APIs.
const SPKI_PREFIX = Buffer.from('302a300506032b6570032100', 'hex');

function spkiFromRawPublicKey(rawKey32) {
  return Buffer.concat([SPKI_PREFIX, rawKey32]);
}

// Replay guard: in-memory Map of "callsign:signature" -> expiry ms. Swept
// lazily on each check (no separate timer) — fine at relay traffic volumes
// (small stations, seconds-scale report cadence, MAX_SKEW_MS-bounded window).
const seenSignatures = new Map();

function sweepSeen(now) {
  for (const [key, expiry] of seenSignatures) {
    if (expiry < now) seenSignatures.delete(key);
  }
}

/**
 * Verify an Ed25519-signed ingest request.
 * @param {import('node:http').IncomingMessage} req
 * @param {string} rawBody exact bytes read off the wire (not re-serialized)
 * @param {{getIdentity(callsign): object|null}} identities
 * @returns {{ok:boolean, callsign?:string, reason?:string}}
 */
function verifyEd25519Auth(req, rawBody, identities) {
  const authHeader = req.headers['authorization'] || '';
  const m = /^Ed25519\s+(\S+)$/.exec(authHeader);
  if (!m) return { ok: false, reason: 'missing_auth_header' };
  const callsign = m[1].toUpperCase();

  const timestamp = req.headers['x-timestamp'];
  const signatureB64 = req.headers['x-signature'];
  if (!timestamp || !signatureB64) return { ok: false, reason: 'missing_signature_headers' };

  const tsMs = Date.parse(timestamp);
  if (!Number.isFinite(tsMs)) return { ok: false, reason: 'invalid_timestamp' };
  const now = Date.now();
  if (Math.abs(now - tsMs) > MAX_SKEW_MS) return { ok: false, reason: 'stale_timestamp' };

  const replayKey = `${callsign}:${signatureB64}`;
  sweepSeen(now);
  if (seenSignatures.has(replayKey)) return { ok: false, reason: 'replay' };

  const identity = identities.getIdentity(callsign);
  if (!identity) return { ok: false, reason: 'unknown_callsign' };
  if (identity.status === 'pending') return { ok: false, reason: 'pending_approval' };
  if (identity.status === 'revoked') return { ok: false, reason: 'revoked' };
  if (identity.status !== 'approved') return { ok: false, reason: 'not_approved' };

  let signature, publicKeyRaw;
  try {
    signature = Buffer.from(signatureB64, 'base64');
    publicKeyRaw = Buffer.from(identity.public_key, 'base64');
  } catch {
    return { ok: false, reason: 'malformed_signature' };
  }
  if (publicKeyRaw.length !== 32) return { ok: false, reason: 'malformed_public_key' };

  let publicKey;
  try {
    publicKey = crypto.createPublicKey({
      key: spkiFromRawPublicKey(publicKeyRaw),
      format: 'der',
      type: 'spki',
    });
  } catch {
    return { ok: false, reason: 'malformed_public_key' };
  }

  const signedMessage = Buffer.from(`${timestamp}\n${rawBody}`, 'utf8');
  let valid = false;
  try {
    valid = crypto.verify(null, signedMessage, publicKey, signature);
  } catch {
    valid = false;
  }
  if (!valid) return { ok: false, reason: 'bad_signature' };

  // Only remember the signature as "seen" once it has verified — an attacker
  // replaying a garbage signature shouldn't be able to poison the replay map.
  seenSignatures.set(replayKey, now + MAX_SKEW_MS);

  return { ok: true, callsign };
}

module.exports = { verifyEd25519Auth, spkiFromRawPublicKey, MAX_SKEW_MS };
