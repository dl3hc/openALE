# GUI↔Core Bridge — Known Gaps

`ALEController` (`include/App/ale_controller.h`) is the single entry point the
CLI (`apps/ale_cli.cpp`) and the GUI bridge (`apps/ale_bridge.cpp`) talk to.
A handful of capabilities the browser GUI's settings UI wants do **not**
have a real implementation behind them. This file is the single place that
tracks *why* each one is missing and *where* it should eventually live —
so the absence is a deliberate, documented decision rather than something
to rediscover (or worse, silently fake) the next time the bridge is built.

If you're implementing one of these, update/remove its entry here in the
same change.

## Protocol timing (call/handshake/AMD-wait/turn-around/listen)

**Level 5 programmable defaults are now wired from the GUI.** The GUI Timing
tab pushes the spec-override-safe values to the core via the bridge's
`TIMING_SET` command (handled in `apps/ale_bridge.cpp`):
`sounding_interval_sec` (→ `set_sounding_interval_sec`),
`link_idle_timeout_sec` (→ `set_link_idle_timeout_sec` / `TimingParameters::Twa_ms`),
`max_tune_time_ms` (→ `set_max_tune_time_ms` / `TimingParameters::Tt_ms`).
`apps/gui/app.js::applyTimingToBridge()` fires this on Settings save and on
bridge connect. `ale_timing.h` documents the 5-level derivation hierarchy;
only **Level 5 "Programmable defaults"** are network-manager-overridable.

**Still not exposed (intentionally):** call timeout, handshake timeout
(Twce), AMD wait (Tm_max), turn-around (Tta) and listen time (Twr) are
**Level 3/4 values** — derived from waveform fundamentals and equipment
class, not operator-tunable per the spec itself. Exposing them as free-form
GUI fields would risk MIL-STD-188-141B non-compliance / interop breakage
with real ALE stations. The GUI's Handshake-Timeout / AMD-Wait / Turn-Around
/ Listen-Time fields therefore remain display-only.

**Owner if ever needed:** stays out of scope; if a real equipment-class
selector (fast/slow) is ever wanted, it belongs in `ALEStateMachine` as an
enum choice, not a millisecond text field.

## Manual accept (auto-accept OFF) — RESOLVED (post-link operator gate)

The earlier `HandshakePhase::AWAIT_ACCEPT` gate **paused the ALE handshake**
until the operator pressed Answer/Decline. That broke MIL-STD-188-141B
interoperability: the caller's response-wait window (Twr/Twrt, ~3 s) is far
shorter than practical operator reaction time, so the caller timed out
before the operator could answer.

**Resolution:** the handshake now **always auto-completes** within Twr/Twrt
and links; manual accept is a **post-link** decision
(`ALEController::pending_operator_accept_`, the LINKED_PENDING_OPERATOR
state). The SM's `set_require_explicit_accept()`/`accept_call()`/
`reject_call()` are retained as no-ops for API/CLI + config-persistence
compatibility but no longer gate the protocol. The operator's
`ACCEPT`/`REJECT` (bridge commands) act on the already-established link:
Accept keeps it; Reject calls `terminate_link()` (TWAS → AVAILABLE).

## Policy (AcceptAnyCall / AcceptAllCall / AcceptAMD)

**No policy engine exists.** Nothing in `ALEStateMachine` filters or rejects
an incoming call based on caller identity or call type — every call
auto-completes the handshake (see "Manual accept" above); the operator's
post-link Reject is the only identity-agnostic drop path today.

**Owner:** a policy engine would run *post-link* (after the auto-completed
handshake, before/instead of operator Accept) and call `terminate_link()`
automatically when a call doesn't match policy. The matching/filtering logic
itself (contact list lookup, allcall/individual distinction) is unbuilt.

## Audio device selection — RESOLVED (runtime, from the GUI)

`apps/ale_bridge.cpp` owns the `AudioDevice` and opens/closes it at runtime on
the GUI's `AUDIO_OPEN {in,out}` / `AUDIO_CLOSE` commands (the bridge starts
bare — no startup audio flags). `AUDIO_DEVICES` enumerates via a throwaway
`make_audio_device()->list_devices()` so the list works before any device is
attached. Re-opening is safe because dispatch runs on the main-loop thread,
sequential with `audio->tick()`. The GUI's Audio settings section drives this
via a dedicated "Connect Audio" button + device dropdowns.

**Rest gap:** TX volume / sample-rate fields in the GUI have no Core backing
(8 kHz is fixed); they're display-only.

## Rig backend / port configuration — RESOLVED (runtime, from the GUI)

`apps/ale_bridge.cpp` owns the `pal::IRadio` and (re)constructs it at runtime
on `RIG_CONNECT {backend,host,port,serial,model}` (assembling the
`create_radio("hamlib:<model>:<port>")` spec — model 2 for TCP netrigctl) /
`RIG_DISCONNECT`. `RIG_STATUS` reports `test_rig_connection()`/
`get_rig_connection_status()`. The GUI's Radio section "Test Connection"
button drives this.

**Rest gap:** the GUI baud-rate field is not applied — `HamlibRadio::configure_port`
hardcodes 19200 (would need a Core change to make baud configurable).

## Sync/frequency-lock status (get_sync_lock_status)

**No defined concept.** Nothing in `ALE2GModem::Demodulator` reports a
"locked to frequency" boolean — ALE 2G doesn't carry a continuous pilot tone
to lock onto; the closest real signal is per-word `unanimous_votes`/
`fec_errors` (already surfaced via `get_current_signal_quality()`).

**Owner:** would need a real definition first (e.g. "N consecutive valid
words" or symbol-timing lock from the FSK demodulator) before it can mean
anything — not a wiring task.
