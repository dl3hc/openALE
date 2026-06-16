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

## Audio device selection (set_audio_input_device / set_audio_output_device)

**Wrong layer, now resolved architecturally — but only at startup.**
`ALEController` doesn't own the `AudioDevice` instance — the caller
constructs it and passes it in via `set_audio_device()`. `apps/ale_bridge.cpp`
is that caller: it opens the device named by `--in-device`/`--out-device` at
startup and calls `ctrl.set_audio_device(audio.get())` before the main loop.
`ALEController::enumerate_audio_inputs/outputs()` (real) backs the bridge's
`AUDIO_DEVICES` command so the GUI can show device names.

**Still missing:** a runtime "switch device while running" WS command. The
bridge has no `AUDIO_SET_DEVICE` handler — re-opening a device while the
audio thread is mid-callback needs the same careful stop/reopen/restart
sequencing called out above, just not built yet. Today, changing devices
means restarting `ale_bridge` with different `--in-device`/`--out-device`.

## Rig backend / port configuration (set_rig_backend / set_rig_tcp_config / set_rig_serial_config)

**Wrong layer, now resolved architecturally — but only at startup.**
`pal::IRadio` is constructed once via the `create_radio("hamlib:<model>:<port>")`
factory string; `apps/ale_bridge.cpp` does this from `--radio SPEC` at startup
and calls `ctrl.set_radio(radio.get())`. `test_rig_connection()`/
`get_rig_connection_status()` back the bridge's `RIG_STATUS` command.

**Still missing:** a runtime "change backend/port while running" WS command,
same reasoning as audio devices above — today this means restarting
`ale_bridge` with a different `--radio` value.

## Sync/frequency-lock status (get_sync_lock_status)

**No defined concept.** Nothing in `ALE2GModem::Demodulator` reports a
"locked to frequency" boolean — ALE 2G doesn't carry a continuous pilot tone
to lock onto; the closest real signal is per-word `unanimous_votes`/
`fec_errors` (already surfaced via `get_current_signal_quality()`).

**Owner:** would need a real definition first (e.g. "N consecutive valid
words" or symbol-timing lock from the FSK demodulator) before it can mean
anything — not a wiring task.
