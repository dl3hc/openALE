'use strict';
// Phase D companion service for docs/LOCATION_SHARING_CONCEPT.md — the
// external web API + map frontend the concept doc deliberately keeps out of
// openALE's own C++ core (§3: "Externer Web-Service ... NICHT Teil dieses
// Konzepts, nur spezifiziert"). Implements exactly the endpoints/payload
// shape/response-code table from §9 and the station model from §14.
//
// Zero npm dependencies: Node's built-in http + node:sqlite (Node >= 22.5).
//
// Usage:
//   LOCATION_API_TOKEN=<bearer-token> node server.js
// Config (env, all optional except the token):
//   PORT                     default 8766
//   LOCATION_DB_PATH         default ./location-relay.sqlite
//   LOCATION_TTL_ONLINE_MIN  default 15   (Konzept §14)
//   LOCATION_TTL_RECENT_MIN  default 60
//   LOCATION_TTL_STALE_MIN   default 1440

const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { openDb } = require('./db');

const PORT = parseInt(process.env.PORT || '8766', 10);
const DB_PATH = process.env.LOCATION_DB_PATH || path.join(__dirname, 'location-relay.sqlite');
const TOKEN = process.env.LOCATION_API_TOKEN || '';
const TTL_ONLINE_MIN = parseInt(process.env.LOCATION_TTL_ONLINE_MIN || '15', 10);
const TTL_RECENT_MIN = parseInt(process.env.LOCATION_TTL_RECENT_MIN || '60', 10);
const TTL_STALE_MIN  = parseInt(process.env.LOCATION_TTL_STALE_MIN  || '1440', 10);
const MAX_BODY_BYTES = 16 * 1024;  // reports are small (Konzept: AMD-sized payloads)

if (!TOKEN) {
  console.error('LOCATION_API_TOKEN is not set — refusing to start with an open ingest endpoint.');
  console.error('Set it to the same bearer token configured in openALE\'s Location Relay settings.');
  process.exit(1);
}

const { stmts } = openDb(DB_PATH);
const PUBLIC_DIR = path.join(__dirname, 'public');

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
      report_count: row.report_count,
      state: stationState(row.last_seen_at),
    },
  };
}

// ── Route handlers ─────────────────────────────────────────────────────────

function handleIngest(req, res) {
  if (!isAuthorized(req)) {
    sendJson(res, 401, { error: 'unauthorized' });
    return;
  }
  readBody(req, MAX_BODY_BYTES).then((raw) => {
    let body;
    try { body = JSON.parse(raw); } catch { body = null; }
    if (!body || typeof body !== 'object') {
      sendJson(res, 422, { error: 'malformed JSON body' });
      return;
    }

    const observer = typeof body.observer === 'string' ? body.observer.trim() : '';
    const source = typeof body.source === 'string' ? body.source.trim() : '';
    const rawGpr = typeof body.raw_gpr === 'string' ? body.raw_gpr : '';
    if (!observer || !source) {
      sendJson(res, 422, { error: 'observer and source are required' });
      return;
    }

    const hasLat = typeof body.latitude === 'number' && Number.isFinite(body.latitude);
    const hasLon = typeof body.longitude === 'number' && Number.isFinite(body.longitude);
    if (hasLat !== hasLon) {
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

    let info;
    try {
      info = stmts.insertReport.run(
        observer, source, relay, sourceType, rawGpr,
        hasLat ? body.latitude : null, hasLon ? body.longitude : null,
        altitude, altitudeUnit, timestamp, receivedAt, callType, comment,
      );
    } catch (e) {
      // UNIQUE(observer, source, timestamp) violation — Konzept §9's 409
      // "Server-Duplikat" (same observer resubmitting the same report, e.g.
      // after a lost response). Different observer / different timestamp
      // always inserts — that is a new, real report, not a duplicate.
      if (String(e && e.message).includes('UNIQUE constraint failed')) {
        sendJson(res, 409, { error: 'duplicate report' });
        return;
      }
      console.error('insertReport failed:', e);
      sendJson(res, 500, { error: 'storage error' });
      return;
    }

    if (hasLat && hasLon) {
      stmts.upsertStationWithPosition.run(
        source, body.latitude, body.longitude, altitude, altitudeUnit,
        timestamp, comment, rawGpr, callType, receivedAt, observer,
      );
    } else {
      // Konzept §17a: "Station gehört, Position unbekannt" — still tracked
      // (heard), just never placed on the map (see stationToFeature()).
      stmts.upsertStationHeardOnly.run(source, comment, rawGpr, callType, receivedAt, observer);
    }
    stmts.upsertObserver.run(source, observer, receivedAt, receivedAt);

    sendJson(res, 201, { ok: true, id: info.lastInsertRowid });
  }).catch((e) => {
    if (e && e.code === 'TOO_LARGE') sendJson(res, 413, { error: 'payload too large' });
    else sendJson(res, 400, { error: 'bad request' });
  });
}

function handleListLocations(_req, res) {
  const rows = stmts.listMappedStations.all();
  sendJson(res, 200, { type: 'FeatureCollection', features: rows.map(stationToFeature) });
}

function handleListStations(_req, res) {
  const rows = stmts.listAllStations.all();
  sendJson(res, 200, rows.map((r) => ({
    callsign: r.source,
    latitude: r.last_lat,
    longitude: r.last_lon,
    last_seen_at: r.last_seen_at,
    last_observer: r.last_observer,
    report_count: r.report_count,
    state: stationState(r.last_seen_at),
  })));
}

function handleStationDetail(res, id) {
  const row = stmts.getStation.get(id);
  if (!row) { sendJson(res, 404, { error: 'unknown station' }); return; }
  const observers = stmts.getObservers.all(id);
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
    report_count: row.report_count,
    state: stationState(row.last_seen_at),
    observers,
  });
}

// ── Static file serving (map frontend) ────────────────────────────────────

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json' };

function serveStatic(req, res, urlPath) {
  let rel = urlPath === '/' ? '/index.html' : urlPath;
  const filePath = path.join(PUBLIC_DIR, path.normalize(rel).replace(/^(\.\.[/\\])+/, ''));
  if (!filePath.startsWith(PUBLIC_DIR)) { res.writeHead(403); res.end(); return; }
  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    const ext = path.extname(filePath);
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
}

// ── Router ──────────────────────────────────────────────────────────────────

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Authorization, Content-Type, Idempotency-Key',
    });
    res.end();
    return;
  }

  if (url.pathname === '/healthz') { sendJson(res, 200, { ok: true }); return; }

  if (url.pathname === '/api/v1/locations' && req.method === 'POST') { handleIngest(req, res); return; }
  if (url.pathname === '/api/v1/locations' && req.method === 'GET')  { handleListLocations(req, res); return; }
  if (url.pathname === '/api/v1/stations'  && req.method === 'GET')  { handleListStations(req, res); return; }
  const stationMatch = /^\/api\/v1\/stations\/([^/]+)$/.exec(url.pathname);
  if (stationMatch && req.method === 'GET') { handleStationDetail(res, decodeURIComponent(stationMatch[1])); return; }

  if (url.pathname.startsWith('/api/')) { sendJson(res, 404, { error: 'unknown endpoint' }); return; }

  if (req.method === 'GET') { serveStatic(req, res, url.pathname); return; }

  res.writeHead(405);
  res.end();
});

server.listen(PORT, () => {
  console.log(`Location Relay server listening on :${PORT}`);
  console.log(`  Ingest:  POST http://localhost:${PORT}/api/v1/locations  (Bearer token required)`);
  console.log(`  Map:     http://localhost:${PORT}/`);
  console.log(`  DB:      ${DB_PATH}`);
});
