# Release Notes — openALE 0.0.3-pre-alpha

## Highlights

**Your configuration is now always saved.** Channels, nets, contacts, and every settings
tab are auto-saved to a single `station.state` file as you use the app — no more losing
edits depending on which save path happened to run last.

**The desktop GUI has a new layout: Command Deck.** Radio status, channel, and transmit
controls are now consolidated into one command bar instead of being scattered across the
header. The main panel shows a clear Idle / Calling / Incoming / Linked state with live
link-quality metrics (SINAD, BER, Score, Channel). The waterfall display is smoother and
lighter on CPU/memory.

**The setup wizard gets you further, faster.** First-run setup now walks through Channels
and Nets (not just Callsign and Radio/Audio), lets you set each net's scan dwell time while
you're already there, and — once you close it — automatically arms the first configured net
so scanning, sounding, and calling are ready to go without an extra manual step.

**A new opt-in Tuner server for auto-tuners.** openALE can now serve its current RX
frequency read-only over the Hamlib `rigctld`/`netrigctl` protocol, so external tools like
auto-tuners can follow along without opening a second, competing connection to the radio.

## New Features

- **Tuner**: opt-in read-only rigctld/netrigctl-compatible TCP server for external
  auto-tuner tools (Settings ▸ Tuner, both GUIs; disabled by default).
- Setup wizard's Nets step gained an inline Dwell (ms) input, and now auto-selects the
  first configured net when the wizard closes.
- Transmitter power control via Hamlib/CAT — TX power is now a real per-channel setting,
  editable in the channel editor or live from the Radio Control panel; either one updates
  the same saved value for that channel (both GUIs).
- Opt-in CAT/rig traffic view in the ALE Log, for diagnosing radio-control issues.
- All log output is now also written to `openALE.log` (auto-rotated once it grows past ~5 MB),
  so it survives after the console window is gone. If openALE ever exits unexpectedly, a new
  `crash.log` records what happened — both are there to check after the fact instead of losing
  the trail.
- Absolute "Received" timestamp shown on Heard Stations and the LQA table (both GUIs).
- New in-app Help / setup-guide page, linked from both GUIs' Help buttons.
- Word-lock ("Sync") indicator simplified to a single clear pill plus a "Decoding" overlay.

## Fixes

- The "Sync" pill no longer stays lit indefinitely after a transmission ends — it now
  clears on a timeout instead of relying on true silence, which real receive noise rarely
  produces.
- Settings, channels, and nets no longer silently fail to save.
- Net renames now actually take effect in the core, instead of being reverted on the next
  settings sync.
- The Test Channel panel no longer shows stale results from a previous peer when reopened.
- Heard Stations' expanded details no longer collapse every time the list refreshes (mobile).
- The ALE log no longer gets spammed with repeated "sounding on/off" lines.
- The Settings drawer no longer visibly paints over the icon rail while sliding open/closed
  (desktop).
- RF transmit power no longer resets to 100% on a channel hop, scan, or sounding cycle — the
  radio now applies each channel's own configured power instead.
- A damaged or truncated `lqa.bin` could crash openALE on startup; it's now rejected cleanly
  (with a clear reason logged) instead, and openALE just starts with an empty LQA table.

## Other

- Remaining German UI strings in the mobile GUI translated to English.
- Project renamed from ALE-Clean-Room to **openALE**; version line reset and now at
  **0.0.3-pre-alpha**.

---

_This is a pre-alpha build. Expect rough edges; see `CHANGELIST.md` for the full commit-level
changelog._
