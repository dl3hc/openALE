# Location Relay Server (Phase D)

External companion service for openALE's Location Relay client
(`docs/LOCATION_SHARING_CONCEPT.md`). This is the piece the concept doc
explicitly keeps **out** of openALE's C++ core — a small ingest API plus a
live map — deliberately separate so openALE never depends on it to build,
run, or link normally.

Zero npm dependencies: plain `node:http` + the built-in `node:sqlite` module
(Node 22.5+). `npm install` is not required.

## Run

```sh
cp .env.example .env
node server.js
```

Then point openALE's **Location Relay → API Endpoint** at
`https://your-host:PORT/api/v1/locations` (or `http://127.0.0.1:8766/...` for
local testing — openALE's client only allows plain `http://` for
`127.0.0.1`/`localhost`, everything else must be HTTPS).

### Auth: per-callsign Ed25519 identity

There is no shared secret to configure. Each openALE instance generates its
own Ed25519 keypair on first use and self-registers:

1. openALE auto-POSTs `{callsign, public_key}` to `/api/v1/register` the
   first time Location Relay sends a report with a fresh identity. The
   station lands `pending` — ingest requests 401 with `reason: pending_approval`
   until approved.
2. Approve it from the server host (direct filesystem access to the sqlite
   file is the trust boundary — there is no network-facing admin endpoint):
   ```sh
   node admin-cli.js list
   node admin-cli.js approve WX1ABC
   ```
3. Ingest then succeeds. To revoke a compromised or decommissioned station:
   ```sh
   node admin-cli.js revoke WX1ABC
   ```
   Revocation takes effect immediately (checked before rate limiting).

For real HTTPS you have two options — either works with openALE's client:

1. **Native TLS (no extra infrastructure).** Set `LOCATION_TLS_CERT_PATH` and
   `LOCATION_TLS_KEY_PATH` in `.env` and the server terminates HTTPS itself
   (`node:https`, zero new dependency). Generate a self-signed cert once:
   ```sh
   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
     -keyout key.pem -out cert.pem -days 3650 -nodes -subj "/CN=location-relay" \
     -addext "subjectAltName=DNS:your-host,IP:192.168.2.144"
   ```
   Then set openALE's **CA Certificate Path** field to this `cert.pem` (the
   client pins to exactly that certificate — required for a self-signed cert,
   since it isn't in any system trust store).
2. **Reverse proxy.** Leave the server on plain HTTP and put Caddy/nginx/
   Traefik in front for TLS termination — the usual approach for a
   CA-signed cert (e.g. via Let's Encrypt). Leave openALE's CA Certificate
   Path empty in this case (system trust store validates a real cert).

Open `http://your-host:PORT/` (or `https://` if native TLS is enabled) in a
browser for the live map.

## API

Matches `docs/LOCATION_SHARING_CONCEPT.md` §9 exactly.

- `POST /api/v1/register` — public, unauthenticated by design (the
  `admin-cli.js` approval step is the actual trust gate). Body:
  `{callsign, public_key}` (`public_key` = base64 of the raw 32-byte Ed25519
  public key). `201` new pending registration, `200` already pending
  (idempotent), `409` already approved or revoked, `422` invalid callsign/key.
- `POST /api/v1/locations` — ingest. Requires
  `Authorization: Ed25519 <callsign>`, `X-Timestamp: <ISO8601>`,
  `X-Signature: <base64>` — the signature covers `<timestamp>\n<raw JSON
  body>`. Body is the JSON payload openALE's `LocationRelayService` already
  sends (`observer`, `source`, `relay`, `source_type`, `raw_gpr`, `latitude`/
  `longitude` (nullable), `altitude`/`altitude_unit` (nullable), `timestamp`
  (nullable), `received_at`, `call_type`); `observer` must equal the signing
  callsign or the request is rejected as `403 observer_mismatch`. Response
  codes follow §9's table plus the new auth cases: `201` accepted, `409` exact
  duplicate (same observer+source+timestamp seen before — not an error, just
  already stored), `422` malformed/missing required fields, `401` bad
  signature/stale timestamp/replay/unknown or pending callsign (see `reason`
  field), `403` revoked identity or `observer_mismatch`.
- `GET /api/v1/locations` — public, no auth. Returns a GeoJSON
  `FeatureCollection` of every station with a known position — this is what
  the map frontend polls every 15s. Stations that have only ever sent
  position-less/manual GPRs (§17a: "heard, position unknown") are tracked
  server-side but never appear here, since GeoJSON requires coordinates —
  see `/api/v1/stations` for those.
- `GET /api/v1/stations` — public. Flat JSON list of every known station
  (mapped or not), with computed `state`.
- `GET /api/v1/stations/:callsign` — public. Full detail: position, altitude,
  comment, raw GPR text, and the list of observers who have heard this
  station (§14 — "mehrere Observer erzeugen keinen Mehrfachmarker").
- `GET /healthz` — liveness probe.

## Station state / TTL (§14)

Computed from `last_seen_at`, thresholds configurable via env:

| State   | Age                          |
|---------|-------------------------------|
| ONLINE  | ≤ `LOCATION_TTL_ONLINE_MIN` (default 15 min) |
| RECENT  | ≤ `LOCATION_TTL_RECENT_MIN` (default 60 min) |
| STALE   | ≤ `LOCATION_TTL_STALE_MIN` (default 1440 min / 24 h) |
| OFFLINE | older than that |

## Storage

A single SQLite file (`LOCATION_DB_PATH`, default `./location-relay.sqlite`),
created automatically on first run. Three tables: `reports` (full append-only
log, one row per accepted POST — this is what the dedup UNIQUE index sits on),
`stations` (materialized latest-state-per-callsign, what the map/list
endpoints actually query), `station_observers` (per-station "heard by"
roster). No server-side dedup window job is needed — the UNIQUE
`(observer, source, timestamp)` index on `reports` rejects an exact
resubmission inline, at insert time (§9/§11).

## Multiple observers, one marker (§14)

A station heard by several openALE instances is **one** row in `stations`,
not one per observer — every accepted report just updates that row's
`last_seen_at`/`last_observer`/`report_count` and upserts a
`station_observers` entry. `GET /api/v1/stations/:callsign` lists all of them.

## Map frontend

`public/index.html` — a single self-contained page (Leaflet + OpenStreetMap
tiles, both loaded from a CDN, no build step). Polls `GET /api/v1/locations`
every 15s, colors markers by state, click for details. Swappable for a
different map provider (Google Maps, etc.) — the concept doc is explicit that
openALE-side and this server's API are provider-agnostic (§15/16); nothing
else here depends on Leaflet specifically.

## Security notes

- Every ingest request is signed per-callsign with Ed25519
  (`crypto.verify('Ed25519', ...)`, native since Node 12 — no new
  dependency); a station can only ever claim its own callsign as `observer`.
  Registration lands `pending` and requires manual `admin-cli.js approve` —
  there is no way for a client to self-approve. Stale timestamps (>300s skew)
  and replayed `(callsign, signature)` pairs are rejected.
- Read endpoints (`GET /api/v1/*`) are intentionally public/unauthenticated —
  the map is meant to be viewable without a credential. If that's not wanted,
  put the whole thing behind a reverse-proxy auth layer.
- TLS: either terminate it here natively (`LOCATION_TLS_CERT_PATH`/
  `LOCATION_TLS_KEY_PATH`, see Run above) or at a reverse proxy — openALE's
  client itself refuses plaintext `http://` except to `127.0.0.1`/`localhost`
  (see `include/App/http_poster.h`). For a self-signed cert, openALE's
  **CA Certificate Path** setting must point at that exact certificate — the
  client pins to it rather than trusting the system CA store.
