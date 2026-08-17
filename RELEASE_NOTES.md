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

**A dedicated Operator Audio Interface.** The audio on the device you're actually sitting at —
mic, speaker, notifications — is now a first-class concept separate from the transceiver's own
Modem Audio, with a header toggle to listen in on the channel at any time (not just once
linked) and low-latency AudioWorklet-based audio under the hood.

## New Features

- **Operator Audio Interface**: the browser's own mic/speaker path is now a named, distinct
  interface from the transceiver's Modem Audio — configured entirely on whichever device the
  GUI happens to be open on (device selection and notification preferences are saved in that
  browser only, never sent to the controller). Settings ▸ Audio Devices now shows Modem Audio
  and Operator Audio side by side. New capabilities built on it:
  - **Channel Monitor**: a header toggle to "listen in" on the current channel's RX audio
    regardless of ALE link state (previously voice passthrough only worked once a link was
    already up).
  - **Notifications**: an optional ring while a call is incoming and a chime when a message
    arrives, synthesized locally and played through the selected speaker device.
  - The browser's mic/speaker audio path now runs on a dedicated AudioWorklet audio thread
    instead of the main JS thread, for lower and more consistent latency, with improved
    upsampling/downsampling quality; it automatically falls back to the previous behavior when
    accessed over a plain-HTTP remote/LAN connection.
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
  `crash.log` records what happened, including a resolved stack trace (function/file/line where
  available) on both Windows and Linux — both are there to check after the fact instead of
  losing the trail.
- Absolute "Received" timestamp shown on Heard Stations and the LQA table (both GUIs).
- New in-app Help / setup-guide page, linked from both GUIs' Help buttons.
- Word-lock ("Sync") indicator simplified to a single clear pill plus a "Decoding" overlay.
- Settings ▸ Files: the LQA database can now be exported/imported like the Configuration
  and Channel files (previously a non-functional "Browse…" button).

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
- Setting TX power now tells you what happened: the ALE Log reports whether the connected
  rig supports power control and what it was set to, instead of silently doing nothing on
  rigs that don't support it.
- The Settings drawer was too narrow for the LQA table, truncating columns and forcing a
  horizontal scrollbar (desktop). Widened to fit all columns without scrolling.
- Scan dwell time on a real radio could occasionally run longer than configured: a periodic
  background CAT read (used to catch external retunes) could still be in the radio's command
  queue when a hop was due, delaying that hop behind it. Channel hops, frequency/mode changes,
  and PTT now jump ahead of that background read instead of queuing behind it.
- Settings ▸ Files: the Configuration, Channel, and LQA rows now behave identically (same
  button order, confirmation prompts, and refresh behavior), and a failed import/export now
  shows a real reason (e.g. "file not found") in the ALE Log instead of a bare "?".
- AMD messages sent over an established link now send the TO address preamble twice, as
  MIL-STD-188-141B requires (matching leading calls, ACKs, and link termination) — previously
  it was sent only once.

## Other

- Remaining German UI strings in the mobile GUI translated to English.
- Project renamed from ALE-Clean-Room to **openALE**; version line reset and now at
  **0.0.3-pre-alpha**.

---

_This is a pre-alpha build. Expect rough edges; see `CHANGELIST.md` for the full commit-level
changelog._
