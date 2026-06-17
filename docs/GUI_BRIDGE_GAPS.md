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

**Not exposed as settings.** `ale_timing.h` documents a 5-level derivation
hierarchy; only **Level 5 "Programmable defaults"** (sounding interval,
link-idle timeout/Twa, blind-tune delay/Tt — all real, see
`ALEController::set_sounding_interval_sec/set_scan_dwell_ms/
set_link_idle_timeout_sec/set_max_tune_time_ms` and `ale::TimingParameters`)
are meant to be network-manager-overridable. Call timeout, handshake timeout
(Twce), AMD wait (Tm_max), turn-around (Tta) and listen time (Twr) are
**Level 3/4 values** — derived from waveform fundamentals and equipment
class, not operator-tunable per the spec itself. Exposing them as free-form
GUI fields would risk MIL-STD-188-141B non-compliance / interop breakage
with real ALE stations.

**Owner if ever needed:** stays out of scope; if a real equipment-class
selector (fast/slow) is ever wanted, it belongs in `ALEStateMachine` as an
enum choice, not a millisecond text field.

## Policy (AcceptAnyCall / AcceptAllCall / AcceptAMD)

**No policy engine exists.** Nothing in `ALEStateMachine` currently filters
or rejects an incoming call based on caller identity or call type — every
call auto-accepts unless the operator calls `reject_call()` (or, with
`set_manual_accept_mode(true)`, doesn't call `accept_call()` in time).

**Owner:** the `HandshakePhase::AWAIT_ACCEPT` gate (added alongside
`accept_call()`) is the right foundation — a policy engine would run inside
that window and call `reject_call()` automatically when a call doesn't match
policy, instead of waiting on the operator. The matching/filtering logic
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
