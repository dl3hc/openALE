# Release Notes — openALE 0.0.4-pre-alpha

## Highlights

**Position reports over ALE.** openALE can now send and receive HFLINK ALE-GPR v1.1
position reports as AMD messages — manually, on a timer, or automatically whenever your
GPS fix moves past a configurable threshold. Reports can go to a single station or out as
an ALLCALL broadcast, and carry latitude/longitude/altitude/timestamp plus an optional
comment. See `docs/ALE_GPR_SPEC.md`.

**Location Sharing — forward received positions to a live web map.** A new opt-in relay
service posts every position report your station receives (from ALE-GPR or raw GGA) to an
external HTTP endpoint, so a whole net's positions can show up on a shared map in real
time. Comes with a small, dependency-free reference server (`tools/location-relay-server`,
Node.js + Leaflet) you can run yourself. See `docs/LOCATION_SHARING_CONCEPT.md` and
`docs/LOCATION_SHARING_HOWTO.md` for end-to-end setup on both sides.

**AMD messages no longer force an unwanted link.** Previously, sending any AMD message —
even a one-off "position report" or short note — always drove the full handshake through to
`LINKED`, whether or not that was the intent. AMD text now rides in the calling frame itself
(Ion2G-style: the message arrives before the handshake even concludes), and the third
handshake frame becomes a pure link/no-link decision by the sender: leave it unchecked for
message-only delivery, or tick the new **Link** checkbox next to the AMD send box to keep
the link established afterward, same as before.

**Rig & audio settings survive a restart.** The Hamlib rig model, COM port, DTR/RTS, and
PTT method — and the audio input/output device — are now saved when you connect, and
openALE re-attaches both automatically on the next launch. An explicit Disconnect / Close
disarms auto-reconnect for that item (so a deliberate disconnect stays disconnected next
time) but keeps the saved values pre-filled for a one-click reconnect.

**ALLCALL broadcasts reach scanning stations and can carry free-text AMD.** A broadcast
now sends a standard scanning-call section before the leading call (A.5.2.5.1), so a
station scanning elsewhere can lock on before the leading call starts. A pinned
"ALLCALL" pseudo-contact in the Contacts list (both GUIs) lets you broadcast free-text
AMD to all stations, not just position reports.

## New Features

- **ALE-GPR position reports**: Settings ▸ Location ▸ Position Reports lets you send a
  report immediately, or configure automatic reporting — on GPS-fix change (configurable
  distance threshold, 60 s minimum interval) or on a fixed timer — to a direct address or
  as an ALLCALL broadcast, restricted to a specific net/channel selection if desired. GPS
  fixes now carry altitude (from gpsd TPV or `$GPGGA` field 9) for the report's altitude
  field; a raw-NMEA passthrough format is also available when a live NMEA-serial fix is in
  use. A single **Enable Position Reports** switch now gates both automatic and manual
  "Send Position Now" sends, so turning reporting off is always one toggle.
- **Location Relay**: Settings ▸ Location ▸ Location Relay forwards every position report
  your station receives to an external HTTP endpoint (URL + optional auth token,
  enable/disable), for live mapping outside openALE itself. The relay client now speaks
  HTTPS on Linux too (previously Windows-only via WinHTTP), via a new mbedTLS-based client;
  a **CA Certificate Path** setting pins the relay server's certificate for self-signed
  deployments. The reference server can also terminate HTTPS itself
  (`LOCATION_TLS_CERT_PATH`/`LOCATION_TLS_KEY_PATH`) without needing a reverse proxy.
- **Settings redesigned**: every Settings tab now groups its fields into clearly labeled,
  bordered sections instead of bare unlabeled divider lines, making the available options
  easier to scan at a glance.
- **AMD "Link" checkbox**: next to the AMD send box in the Messages panel — keep the link
  established after this message is delivered, instead of the new message-only default.
- Messages panel redesigned with clearer sent/received direction styling and a delivery
  status pill (pending / delivered / not delivered) on sent messages, driven by real
  handshake outcomes instead of assuming a queued send succeeded.
- **Connection persistence**: the rig model, COM port, baud, DTR/RTS, settle time, and
  PTT method — plus the audio input/output device — are saved on a successful Connect /
  Open and re-attached automatically on the next launch. Explicit Disconnect / Close
  disarms auto-reconnect for that item but keeps the saved values pre-filled.
- **Pinned ALLCALL contact**: a pseudo-contact in the Contacts list (both GUIs) lets you
  broadcast free-text AMD to all stations, not just position reports.
- **Group AMD**: AMD can now be sent to a multi-member group call, riding the group
  calling frame the same way individual AMD rides an individual call.
- **Link Policy defaults**: Settings ▸ Policy ▸ Link Policy sets the default TIS/TWAS
  conclusion per destination type (Individual / Group / ALLCALL). The compose-row **Link**
  checkbox is pre-filled from these and remains overridable per send. ALLCALL broadcasts
  now also support the **Link** option (TIS conclusion) instead of always fire-and-forget.
- **Location Relay — endpoint health pill**: the status indicator under Settings ▸
  Location ▸ Location Relay now shows live endpoint reachability — Running · Connected /
  No connection / Server error — instead of just "Running" (which only proved the worker
  thread existed). The worker re-checks on every send and roughly every 60 s while idle.
- **Location Relay map — HF-band filter + retention**: the reference server's map groups
  stations by HF band (160 m–10 m + Other) rather than per-exact frequency, drops stations
  not heard in the last two days, and uses a select-to-show allowlist in place of the old
  hide-by-toggle filter.
- **Channel-file overlay persistence**: `station.state` no longer duplicates every channel
  frequency row from a loaded preset. It stores a reference to the preset file plus only the
  channels you added/modified and a deleted-ID list, so a loaded preset survives a restart
  without bloating the state file — and your manual channel edits are preserved.
- **Automatic Sounding toggle** moved to the top of Settings ▸ Nets, where the per-net
  sounding interval it controls lives. (Was under Misc / Sounding.)

## Fixes

- Incoming AMD messages sent in the calling frame or response frame were previously
  deliverable, but sending one from openALE always forced a full link — even for a message
  with no follow-up conversation intended. Sending is now message/link decoupled as
  described above.
- A LINKED-state AMD's delivery confirmation (Response → ACK) could be silently starved by
  a drain-transition race, so the message showed no delivery outcome even though it had
  gone out.
- A LINKED-state AMD retry resent instantly instead of waiting the normal retry backoff
  like every other retry path in the protocol.
- The incoming-call ring tone could keep playing indefinitely on auto-accept or a
  pre-clicked manual accept — it was only ever stopped from the link-teardown path, not
  either accept path.
- `STATION_LOC_SET` always reset the position source back to Manual on every call, even
  when only an unrelated field (e.g. the SFI toggle) was being updated through the same
  bridge command — this could silently clobber a configured GPS/NMEA position source back
  to Manual.
- ALLCALL broadcasts were sent as a leading call only — no preceding scanning-call
  section (A.5.2.5.1) — so a station scanning another channel could not lock on before
  the leading call started. A scanning call sized to the same call width C now precedes
  the broadcast.
- Received ALLCALL/wildcard position reports (and any AMD in a TWAS-concluded calling
  frame) were silently dropped: the TWAS conclusion never captured the caller's address,
  so the report had no attributed sender. TWAS conclusions now settle the caller identity
  the same way TIS does (multi-word extensions included), then abort without linking.
- An ALLCALL broadcast whose audio stalled could leave RX disabled permanently — the
  broadcast stays in IDLE/SCANNING the whole time with no state transition to recover it.
  A TX-drain safety net now force-recovers the path.
- `apply_config()` did not propagate the rig- and audio-connection fields into the live
  config, so the new persistence would have silently no-op'd (saved values never reached
  the auto-reconnect path).
- Every Settings / navbar click made Hamlib iterate and reload every radio backend — the
  rig model dropdown was refetched on each Settings open, and the first such call in a
  session is the one that trips `rig_load_all_backends`. The list is now fetched only when
  you open the Radio / CAT Control tab, cached for the session, and re-fetched after a
  bridge restart; navigating Settings no longer triggers a full backend reload.
- GPS altitude was almost always reported as unavailable (`#M`) in ALE-GPR position
  reports even with a good fix — a `$GPRMC` sentence (which never carries altitude) was
  unconditionally clearing the altitude a preceding `$GPGGA` sentence from the same
  receiver had just reported, and most receivers emit GGA+RMC back to back. Altitude is
  now preserved across altitude-less updates and only cleared when the fix itself is lost.
- ALE-GPR coordinates were silently truncated to ~11 m precision in two places: the
  relay server rounded every incoming position to 4 decimals before storage (SQLite
  already stores full double precision, so this was a pure loss), and the client's own
  privacy-rounding setting defaulted to 2 decimals (~1.1 km) — far more blurring than
  intended for a default. The server no longer rounds at all; the client default is now
  6 decimals, with the setting still available as an explicit operator privacy control.
  Station Location settings persistence had the same silent-truncation bug (~11 m) on
  every save — widened to sub-millimeter precision.
- A free-text ALE-GPR OBJECT or COMMENT field containing the `*` field delimiter would
  split a report into more fields than the format allows, causing a receiving station to
  reject the whole message as malformed. Delimiter characters typed into free text are
  now replaced with `#` before the report is built.
- Location Relay reference server: a station's map position stopped updating for every
  observer but the first once `LOCATION_COLLAPSE_BROADCASTS` was enabled — the
  position/observer upsert only ran on the winning report row (201) instead of on every
  hearing (409s included). All observers now update the map correctly on every hearing.
- Location Relay map's frequency display rounded to 3 decimals, silently dropping
  channels that sit on 100 Hz boundaries (e.g. 10.1455 MHz showed as "10.145 MHz") — now
  shows 6 decimals with trailing zeros trimmed.

## Other

- README.md now links to `docs/ALE_GPR_SPEC.md` and the two Location Sharing docs.
- Hamlib dependency bumped from 4.5.2 to 4.7.2. `scripts/build_hamlib.sh` now clones
  the `4.7.2` tag (Hamlib's GitHub tags are bare version numbers — the previous
  `Hamlib-<version>` tag form did not exist and would have failed the clone) and
  rebuilds `libhamlib-4.dll` against the latest Hamlib release. The soname stays
  `libhamlib-4.dll` (major-ABI-versioned, unchanged across the 4.x series).
- `ale_app_core` now links against the project's vendored mbedTLS libraries on
  Linux/POSIX builds (already used elsewhere in the project), to provide the Location
  Relay HTTPS client above.
- Location Relay reference server (`tools/location-relay-server`): a DB worker thread with
  batched transactions removes a write fan-in bottleneck (~10× ingest throughput), with
  logging, capacity/security bounds, rounding/observer-cap safety nets, and an opt-in
  collapse mode.

---

_This is a pre-alpha build. Expect rough edges; see `CHANGELIST.md` for the full commit-level
changelog._
