# Location Sharing — How-To

Location Sharing (a.k.a. **Location Relay**) forwards ALE-GPR position
reports that your station *receives* from other stations to a web map, so
you (or a whole net) can see who's on the air and roughly where, in a
browser. It is **opt-in and off by default**, and it never forwards your own
transmitted position — only positions you hear from others.

This is the practical setup guide. For the design rationale, the 16
architecture questions it answers, and the file:line citations behind every
decision, see [`LOCATION_SHARING_CONCEPT.md`](LOCATION_SHARING_CONCEPT.md)
(German). This document assumes that design and just tells you how to turn
it on.

## The three pieces

```
 ┌─────────────┐   ALE-GPR over RF    ┌─────────────┐
 │ Any station │ ───────────────────► │   openALE   │
 │ (ALE-GPR TX)│                      │ (this repo) │
 └─────────────┘                      └──────┬──────┘
                                              │ HTTPS POST
                                              │ (opt-in, off by default)
                                              ▼
                                     ┌──────────────────┐
                                     │ Location Relay    │
                                     │ Server             │
                                     │ tools/location-    │
                                     │ relay-server/       │
                                     └────────┬───────────┘
                                              │ HTTP GET (public, read-only)
                                              ▼
                                       Browser map (Leaflet/OSM)
```

1. **A station transmits its position** as an ALE-GPR message (see
   [`ALE_GPR_SPEC.md`](ALE_GPR_SPEC.md)) — either automatically (Position
   Reports settings) or via the "Send Position Now" button.
2. **Your openALE receives it**, and — only if you've enabled Location
   Relay — forwards it to a server you configure.
3. **The Location Relay Server** stores it and serves a live map to anyone
   who opens it in a browser.

You need pieces 2 and 3 running for the map to work; piece 1 (someone
transmitting a position) can be any ALE-GPR-capable station, including your
own or another openALE instance.

## Part 1 — Make sure position reports exist to relay

Location Relay only forwards ALE-GPR messages your station *hears*. If
nobody on the net is sending position reports yet, there's nothing to
relay. Open **Settings → Location**:

- **Position Source** — set how *your own* position is known (Manual
  lat/lon, Maidenhead grid, gpsd, or NMEA serial GPS).
- **Position Reports** — configure automatic sending (on-change or interval)
  or just use **Send Position Now** to transmit one ALE-GPR report on
  demand, to a direct address or ALLCALL.

Full details: [`ALE_GPR_SPEC.md`](ALE_GPR_SPEC.md).

## Part 2 — Run the Location Relay Server

The server lives at [`tools/location-relay-server/`](../tools/location-relay-server/)
in this repo. It's a small, self-contained Node.js service — **no `npm
install` needed**, it only uses Node's built-ins (`http` + `node:sqlite`).
Requires Node.js 22.5 or newer.

```sh
cd tools/location-relay-server
cp .env.example .env
```

Edit `.env` and set a real token:

```
LOCATION_API_TOKEN=<a long random string>
```

This is the shared secret between openALE and the server — anyone who has
it can submit position reports, so treat it like a password. Generate one
with e.g. `openssl rand -hex 32`.

Then run it:

```sh
node server.js
```

```
Location Relay server listening on :8766
  Ingest:  POST http://localhost:8766/api/v1/locations  (Bearer token required)
  Map:     http://localhost:8766/
  DB:      .../location-relay.sqlite
```

That's it for local testing — openALE can already reach
`http://127.0.0.1:8766/api/v1/locations` (openALE's HTTPS-only policy has a
deliberate exception for `127.0.0.1`/`localhost`, exactly so you can test
without setting up TLS first).

**For real use across the network**, you need real HTTPS. Two ways to get
it — either works with openALE's client on both Windows and Linux:

1. **Native TLS**: set `LOCATION_TLS_CERT_PATH`/`LOCATION_TLS_KEY_PATH` in
   the server's `.env` (a one-line `openssl` command generates a self-signed
   cert — see the server's README). Then set openALE's **CA Certificate
   Path** field to that certificate, since a self-signed cert isn't in any
   system trust store and needs to be pinned explicitly.
2. **Reverse proxy**: put Caddy/nginx/Traefik in front for TLS termination
   (typically with a real CA-signed cert, e.g. Let's Encrypt) and point
   openALE at the proxy's `https://` URL — leave CA Certificate Path empty
   in this case, the system trust store already validates a real cert.

See [`tools/location-relay-server/README.md`](../tools/location-relay-server/README.md)
for the full API reference, storage model, and env var list.

Keep the server running (systemd unit, pm2, a screen/tmux session — your
choice) so it survives reboots and picks back up on its own; it has no
external dependencies to babysit.

## Part 3 — Configure openALE's Location Relay

Open the openALE GUI → **Settings → Location**, scroll past *Position
Reports* and *Known Positions* to **Privacy / Network — Location Relay**.

| Field | What it does |
|---|---|
| **Enable Location Relay** | Master switch. Off by default — nothing is ever sent until you flip this. |
| **API Endpoint** | The server's ingest URL, e.g. `https://relay.example.com/api/v1/locations` or `http://127.0.0.1:8766/api/v1/locations` for local testing. |
| **Bearer Token** | Must match the server's `LOCATION_API_TOKEN`. Write-only in the GUI — once saved, it's never displayed or sent back to the browser again; the field just shows a hint that a token is stored. |
| **CA Certificate Path** | Optional. If the server uses a self-signed certificate, point this at that certificate (PEM) — the client trusts exactly that cert. Leave empty for a real CA-signed cert (system trust store) or for `http://127.0.0.1`/`localhost` local testing. |
| **Forward positions received via** | Per-call-type gates: ALLCALL, Individual Call, Net Call, Group Call, Linked (over an established connection). Only checked types get forwarded — see the privacy note below. |
| **Min. interval per source (s)** | Throttle: don't forward more than one report from the same station more often than this, even if it keeps re-sending. Default 30s. |
| **Position rounding (digits)** | Reduces position precision before it's sent — 2 digits ≈ 1 km resolution. Also stabilizes deduplication. |
| **Forward the GPR comment field** | Off by default — the free-text comment field in a GPR report can contain anything the sender typed; leave this off unless you trust the content. |

Click **Apply**. The status line next to the button will read *Running* once
the background worker thread has started — if it stays on *Enabled, not
running*, see Troubleshooting below.

Your own transmitted position is **never** forwarded by this feature — only
positions you receive from other stations. This is a deliberate,
non-configurable design choice (see the concept doc §6) that prevents a
"forward everything I transmit" misuse case and avoids duplicate map
entries (your own position shows up on the map via *other* stations that
hear you, if they also run Location Relay).

## Part 4 — View the map

Open the server's own URL in a browser, e.g. `http://localhost:8766/` or
your reverse-proxied `https://relay.example.com/`. No login needed — the
map/read API is intentionally public so anyone with the link can view it
(the write/ingest side is the one that needs the token).

- Markers are colored by how recently the station was last heard:
  **green = Online** (≤15 min), **yellow = Recent** (≤60 min),
  **orange = Stale** (≤24 h), **grey = Offline** (older).
- Click a marker for details: last-seen time, which station relayed it,
  report count, altitude, position timestamp.
- The map refreshes itself every 15 seconds — no need to reload the page.
- A station heard by multiple observers appears as **one** marker, not one
  per observer; click it to see who's heard it (the `observers` list in the
  detail popup/API).
- Stations that have only ever sent a position-less/manual GPR ("heard, but
  no position") don't get a map marker — see `GET /api/v1/stations` in the
  server's own README for the raw list including those.

## Troubleshooting

**Status stays "Enabled, not running" after Apply.**
Check the ALE Log — the bridge logs `Location Relay: starting (<url>)` and
any startup problem right after. Most common cause: the URL was rejected by
the HTTPS-only policy (must be `https://`, or `http://127.0.0.1` /
`http://localhost` for local testing — anything else `http://` is refused
client-side before any network call happens).

**Reports are queued but never show up on the map.**
- Check the server's console output — `401` means the tokens don't match;
  `422` means a required field was missing/malformed (shouldn't happen with
  an unmodified openALE client — file a bug if you see this).
- Check that the call-type toggle for how the position was actually
  received is checked (e.g. if a station sent its GPR via a Net Call and
  you only enabled "ALLCALL", it won't be forwarded).
- Remember the per-source throttle: if the same station reported twice
  within your configured minimum interval, the second one is silently
  dropped — that's by design, not a bug.

**openALE logs a TLS/certificate warning and nothing sends.**
Both platforms speak real TLS now (Windows via WinHTTP, Linux via mbedTLS),
so this almost always means a certificate trust problem rather than a
missing TLS backend:
- `no ca_cert_path configured and no system CA bundle found` / `does not
  match the configured ca_cert_path pin` (Linux) or `server certificate
  does not match the configured ca_cert_path pin` (Windows) — the server is
  using a self-signed certificate and either **CA Certificate Path** is
  empty or points at the wrong file. Set it to the exact `cert.pem` the
  server was started with.
- `failed to load ca_cert_path ... (unreadable/invalid PEM)` — the path is
  wrong, or the file isn't a valid PEM certificate.
- No TLS-specific warning at all, just "endpoint unreachable" — a plain
  connectivity problem (host/port/firewall), not TLS; see the reachability
  troubleshooting above.

**"Auth failed" / 401 shown in the ALE Log.**
The tokens don't match. Re-enter the token in openALE's Location Relay
settings (it's write-only, so if you're not sure it's right, just retype it
and Apply) and confirm it's identical to the server's `LOCATION_API_TOKEN`.

**I want to stop sharing entirely.**
Uncheck **Enable Location Relay** and Apply. This stops the worker thread
immediately; nothing further is sent. Your data already on the server isn't
deleted automatically — that's the server operator's call (it's just SQLite
rows; deleting `location-relay.sqlite` or editing it directly both work for
a full reset).

## Reference

- [`LOCATION_SHARING_CONCEPT.md`](LOCATION_SHARING_CONCEPT.md) — full
  architecture/design doc (German), all 16 design questions answered, every
  decision cited against file:line.
- [`ALE_GPR_SPEC.md`](ALE_GPR_SPEC.md) — the wire format being relayed.
- [`tools/location-relay-server/README.md`](../tools/location-relay-server/README.md) —
  server API reference, storage model, security notes.
- Core source: `include/App/location_relay_service.h` /
  `src/App/location_relay_service.cpp` (the gate, dedup, and worker thread),
  `include/App/http_poster.h` / `src/App/http_poster.cpp` (the HTTP client).
- GUI source: the "Privacy / Network — Location Relay" card in
  `apps/gui/index.html` / `apps/gui/mobile/index.html`, wired in
  `apps/gui/app.js` / `apps/gui/mobile/app.js`
  (`syncLocationSharingFromBridge()` / `applyLocationSharingToBridge()`).
- Bridge commands: `LOCATION_SHARING_GET` / `LOCATION_SHARING_SET` in
  `apps/ale_bridge.cpp`.
