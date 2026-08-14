# Release Notes — openALE 0.1.0-pre-alpha

_Covering development work from 2026-08-12 to 2026-08-14._

## Highlights

**Your configuration is now always saved.** Channels, nets, contacts, and every settings
tab are auto-saved to a single `station.state` file as you use the app — no more losing
edits depending on which save path happened to run last.

**The desktop GUI has a new layout: Command Deck.** Radio status, channel, and transmit
controls are now consolidated into one command bar instead of being scattered across the
header. The main panel shows a clear Idle / Calling / Incoming / Linked state with live
link-quality metrics (SINAD, BER, Score, Channel). The waterfall display is smoother and
lighter on CPU/memory.

**The mobile setup wizard is more complete.** First-run setup now walks through Channels
and Nets, not just Callsign and Radio/Audio, and empty screens (Heard Stations, Contacts,
Messages) now tell you what to do next instead of showing a blank list.

## New Features

- Transmitter power control via Hamlib/CAT — set RF power per channel, with a live manual
  override on the Radio Control panel (both GUIs).
- Opt-in CAT/rig traffic view in the ALE Log, for diagnosing radio-control issues.
- Absolute "Received" timestamp shown on Heard Stations and the LQA table (both GUIs).
- New in-app Help / setup-guide page, linked from both GUIs' Help buttons.
- Word-lock ("Sync") indicator simplified to a single clear pill plus a "Decoding" overlay.

## Fixes

- Settings, channels, and nets no longer silently fail to save.
- Net renames now actually take effect in the core, instead of being reverted on the next
  settings sync.
- The Test Channel panel no longer shows stale results from a previous peer when reopened.
- Heard Stations' expanded details no longer collapse every time the list refreshes (mobile).
- The ALE log no longer gets spammed with repeated "sounding on/off" lines.

## Other

- Project renamed from ALE-Clean-Room to **openALE**, version reset to **0.1.0-pre-alpha**.
- Remaining German UI strings in the mobile GUI translated to English.

---

_This is a pre-alpha build. Expect rough edges; see `CHANGELIST.md` for the full commit-level
changelog._
