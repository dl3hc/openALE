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

## New Features

- **ALE-GPR position reports**: Settings ▸ Location ▸ Position Reports lets you send a
  report immediately, or configure automatic reporting — on GPS-fix change (configurable
  distance threshold, 60 s minimum interval) or on a fixed timer — to a direct address or
  as an ALLCALL broadcast, restricted to a specific net/channel selection if desired. GPS
  fixes now carry altitude (from gpsd TPV or `$GPGGA` field 9) for the report's altitude
  field; a raw-NMEA passthrough format is also available when a live NMEA-serial fix is in
  use.
- **Location Relay**: Settings ▸ Location ▸ Location Relay forwards every position report
  your station receives to an external HTTP endpoint (URL + optional auth token,
  enable/disable), for live mapping outside openALE itself.
- **AMD "Link" checkbox**: next to the AMD send box in the Messages panel — keep the link
  established after this message is delivered, instead of the new message-only default.
- Messages panel redesigned with clearer sent/received direction styling and a delivery
  status pill (pending / delivered / not delivered) on sent messages, driven by real
  handshake outcomes instead of assuming a queued send succeeded.

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

## Other

- README.md now links to `docs/ALE_GPR_SPEC.md` and the two Location Sharing docs.

---

_This is a pre-alpha build. Expect rough edges; see `CHANGELIST.md` for the full commit-level
changelog._
