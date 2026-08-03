# Propagation-Aware LQA Scoring

HF ionospheric conditions follow a ~24 h solar cycle. A channel measurement taken at 14:00
local solar time is a much better predictor of quality at 14:00 tomorrow than the same
measurement taken at 02:00. Standard LQA (spec A.5.4.1) only considers recency — not *when*
during the day the measurement was made. This extension adds a **time-of-day and solar-cycle
similarity factor** that down-weights historical measurements taken under different propagation
conditions without discarding them.

This is an openALE-specific extension layered on top of the spec-defined LQA ranking — see
[LQA_BILATERAL_RELINK.md](LQA_BILATERAL_RELINK.md) for the bilateral exchange and auto-relink
mechanisms this factor feeds into.

## Station Position Configuration

Three ways to supply the station's geographic position (GUI → Settings → Location):

| Source | Description |
|--------|-------------|
| **Manual** | Operator enters decimal latitude/longitude directly |
| **Maidenhead Grid** | 4- or 6-character Maidenhead locator (e.g. `IO91wm`) — converted to the centre of the cell |
| **gpsd** | Live fix from a running [gpsd](https://gpsd.gitlab.io/gpsd/) daemon over TCP (default `127.0.0.1:2947`) |
| **NMEA Serial** | Live fix from a GPS receiver on a serial/USB port (e.g. `COM3`, `/dev/ttyUSB0`) at configurable baud rate |

If no position source is configured, propagation scoring is fully bypassed — the LQA system
behaves exactly as before.

## Solar Elevation Algorithm

Solar position is computed with the **Spencer/Michalsky series** (< 0.5° error, 1950–2050).
No external library is required; the implementation lives in
[src/LQA/solar_position.cpp](../src/LQA/solar_position.cpp).

At sounding time, the current solar elevation is stamped onto the LQA entry alongside the
measurement (`solar_elevation_deg_at_measurement`). At ranking time, the same calculation is
run for *now* and compared against each stored entry.

## Solar Flux Index (SFI)

Optionally, a background thread ([src/App/sfi_service.cpp](../src/App/sfi_service.cpp)) fetches
the **10.7 cm Solar Flux Index** from NOAA SWPC once per hour:

```
GET https://services.swpc.noaa.gov/products/summary/10cm-flux.json
→ [{"flux": 165, "time_tag": "..."}]
```

The SFI at measurement time is also stamped onto each LQA entry (`sfi_at_measurement`). When
both values are present, the SFI difference between *now* and the stored measurement adds a
second similarity dimension — useful for distinguishing solar-maximum from solar-minimum
conditions on the same channel at the same time of day.

If the fetch fails, or SFI is disabled, only solar elevation is used. Graceful degradation is
enforced at every level.

## Propagation Factor Calculation

For each candidate channel during `rank_channels_for_station()`
([lqa_analyzer.cpp](../src/LQA/lqa_analyzer.cpp)):

```
elev_now  = compute_solar_elevation(lat, lon, now)
elev_diff = |elev_now − entry.solar_elevation_deg_at_measurement|
solar_sim = max(0, 1 − elev_diff / 45°)      ← 0° diff → 1.0 ; ≥45° diff → 0.0

sfi_sim   = 1.0   (default when either SFI value is unknown)
if sfi_current > 0 and entry.sfi_at_measurement > 0:
    sfi_diff = |sfi_current − entry.sfi_at_measurement|
    sfi_sim  = max(0, 1 − sfi_diff / 100 sfu) ← 0 sfu diff → 1.0 ; ≥100 sfu → 0.0

factor = 0.5 + 0.5 × (solar_sim × sfi_sim)   ← floor at 0.5
score  *= factor                               ← applied before handshake-fail penalty
```

The **floor at 0.5** ensures a channel with real historical LQA data is never ranked below
half its measured quality due to a temporal mismatch alone — it competes on less equal terms
but is never suppressed entirely.

| Scenario | Factor |
|----------|--------|
| Same time of day, same solar cycle | **1.0** — measurement fully relevant |
| 22.5° solar elevation difference | **0.75** |
| ≥ 45° elevation difference (opposite half of day) | **0.5** — floor |
| 100+ sfu SFI difference (different solar cycle) | **0.5** — floor |
| No position configured | **1.0** — scoring unchanged |
| Entry predates this feature (no stamps) | **1.0** — scoring unchanged |

## LQA v3 Persistent Format

Two `float32` fields are appended to each LQA database entry on disk (+8 bytes per entry), see
[include/LQA/lqa_database.h](../include/LQA/lqa_database.h):

```
[4]  solar_elevation_deg_at_measurement   (−90..+90 °; 0.0 = not recorded)
[4]  sfi_at_measurement                   (1–999 sfu;  0.0 = not recorded)
```

Files written by older versions (v2) load cleanly — the new fields default to `0.0`, which the
factor code treats as "not recorded → no adjustment". The `max_age_ms` window is extended from
1 h to 25 h to retain the full diurnal cycle.

## Thread Safety

`GpsService` (gpsd + NMEA) and `SfiService` run in independent daemon threads. They are
**never** allowed to call `ALEController` directly. Instead, callbacks write to a
mutex-protected `PendingUpdate` mailbox; the main bridge loop drains it each tick and calls the
controller setters on the main thread:

```
GPS/SFI thread → PendingUpdate (mutex) → main loop → ctrl.set_gps_fix() / set_current_sfi()
                                                    → update_propagation_context()
                                                    → lqa_analyzer_.set_propagation_context()
```
