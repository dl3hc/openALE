# Changelist — 2026-08-12 to 2026-08-14

Covers commits `c30f855..b415e16` on `main`.

## Persistence

- **`527b704`** feat(persist): unify auto-save into a single `station.state` file
  Adds `ALEController::load_state()`/`save_state()`, composing the station-file body
  (channels/nets/contacts/rosters/allcall) and the settings body into one auto-managed file
  instead of two independently-triggered saves. Wired into every settings-mutating bridge
  command plus startup load / shutdown save. Adds `resolve_data_path()` so relative paths
  (e.g. `nets/USA.ale`) still resolve when launched from `build/`. Fixes a silent data-loss bug
  where Nets/Channels/settings changes could be dropped depending on which save path last ran.

## GUI — Desktop

- **`508c5e1`** feat(gui): port desktop GUI to Command Deck layout
  Consolidated Radio Command Bar (Status / Channel / Transmit clusters); Idle/Calling/
  Incoming/Linked center-pane state machine wired to real LQA data (SINAD/BER/Score/Chan);
  rewritten waterfall renderer (scrolling GPU-blit, devicePixelRatio-aware, lower CPU/memory);
  Heard Stations card-list redesign; new app icon / brand lettering assets. Also fixes a
  Heard Stations hover-background CSS leak and a Settings drawer close-animation stutter.
- **`28db48f`** feat(diag): simplify word-lock display to a binary "Sync" pill + Decoding overlay
  Replaces the 3-state word-lock chip with a minimal "Sync" pill plus a transient "Decoding"
  overlay on the main ALE-state pill. Frontend-only; no backend change.
- **`4d4fe47`** feat(gui): add Help/setup-guide page and wire it to the Help buttons
  New `help.html` for both GUIs; wires the previously-dead header/More-sheet Help buttons.
- **`bafe966`** feat(lqa): show absolute "Received" timestamp on both GUIs (P1-12)
  New "Received" column/row on Heard Stations and the Settings ▸ LQA table, derived from
  the existing LQA database timestamp — no new tracking.

## GUI — Mobile

- **`b415e16`** feat(gui): mobile UX pass — setup wizard channel/net steps, empty states, i18n cleanup
  First-run wizard restructured 4→5 steps (adds Channels + Nets); richer empty states with
  contextual CTAs; remaining German strings translated to English; Import/Export terminology
  cleanup; new `messageSquare` icon.

## Radio / CAT

- **`de10180`** feat(radio): add transmitter power control via Hamlib/CAT (P2-02)
  `HamlibRadio` now applies `Channel::power` via `RIG_LEVEL_RFPOWER`; live manual override on
  the Radio Control panel (both GUIs); set/get-capability tracked separately after real-hardware
  testing found a rig that accepts writes but doesn't support readback. Unsupported rigs show
  the control disabled rather than silently no-op.
- **`c395004`** feat(diag): opt-in CAT/rig traffic view in the ALE Log (P1-10)
  `IRadio` gains optional `set_cat_trace_enabled()`/`drain_cat_trace()`; only `HamlibRadio`
  implements them. Traces relay through the existing `ALE_STATUS` event as `[CAT] ...` lines.
- **`4198c69`** feat(diag): 3-state modem word-lock display on both GUIs (P1-11)
  Core modem exposes A.5.2.6.3 word-grid lock state (Acquiring lock / Locked / Decoding); later
  simplified by `28db48f`.

## Build

- **`dc02c34`** build: rename project to openALE, reset version to 0.1.0-pre-alpha
  CMake project renamed from ALE-Clean-Room 2.0.0 to openALE 0.1.0-pre-alpha.

## Fixes

- **`07cad68`** fix(ale): stop redundant sounding on/off status from spamming the log
  Idempotency guard in `set_automatic_sounding()` skips redundant status emission when the
  resolved (on/off, net) already matches current state.
- **`a210870`** fix(gui): reset Test Channel modal state on peer/run change
  Both GUIs now clear the previous run's peer/progress/results when reopening for another peer.
- **`33df1f9`** fix(gui): preserve Heard Stations Details disclosure across live refresh
  Mobile's `renderHeard()` no longer collapses an expanded per-station Details panel on rebuild.
- **`c30f855`** fix(gui): persist net renames to core and fix stale contact-editor index
  Net rename now has a real primitive (`NetStore::rename_net` / `CMD:RENAME_NET` /
  `NET_RENAME`) instead of being GUI-local-only and silently wiped on next sync. Also fixes a
  TOCTOU race in the contact editor by keying edits on the stable callsign instead of index.
