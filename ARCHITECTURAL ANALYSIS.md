## ARCHITECTURAL ANALYSIS: PC-ALE
### Based on comprehensive reading of the header and implementation files, here is a detailed architecture review:

## 1. ALEController (ale_controller.h + .cpp)
Responsibility Boundary
Single focal point for all operator-facing ALE operations
Bridges the state machine ↔ modem ↔ radio control
Manages LQA database, contacts, nets, channels, sounding
Handles PTT lead/tail timing, manual override, audio device attachment
Processes CLI/GUI commands and exports/imports settings
Public Interface Width
~120 public methods across major categories:

Configuration (27 methods): address, radio, audio device, channels, nets, contacts, self-address
Operators actions (11 methods): start_available, start_scanning, initiate_call, reject/accept, terminate, emergency_stop
Main loop (2): update(now_ms), feed_audio()
Callbacks (10): on_link_established, on_call_received, on_link_terminated, on_amd_received, on_status_changed, on_word_decoded, on_frame_decoded, on_channel_changed, on_ptt_changed
Settings/LQA (15): load/save LQA, load/save channels, export/import settings
Radio/VFO (9): set_frequency, set_mode, step_channel, nudge_frequency, get_ppt_state
Audio/Rig (6): enumerate devices, test connection, audio level
FEC/Sync tuning (5): set_golay_mode, set_min_unanimous_votes, set_adaptive_fec, debug_rx
Auto-relink/EFS (4): set_relink_enabled, set_enhanced_freq_select
Inspection (8): state, self, display_state, get_signal_quality, get_call_duration_seconds, is_link_active
Interface Health: Interface is OVERLOADED. The class owns too many subsystems and provides too many knobs to callers. A GUI layer is forcing configuration to surface here instead of being managed at subsystem boundaries.

Leakage Problems
Lines 202-220: Constructor wires ~8 callback chains for SM, modem, demodulator. SM state changes → controller → GUI. This is proper delegation but coupling is tight.

Lines 273-300: set_rx_enabled_callback in wire_callbacks manages a complex state machine:

manual_ptt_ overrides SM control
ppt_lead/ppt_tail deadlines buffer words
Demodulator enable/disable is coupled to RX enable logic
Problem: The TX buffering (pending_tx_words_) and PTT timing logic should be in modem/audio, not here (lines 765-773).
Lines 533-554: compute_lqa_payload() knows about LQA database encoding (SINAD/BER codes), bilateral scoring, multipath. This is LQA detail that should stay in LQAAnalyzer/LQAMetrics.

Lines 556-624: initiate_call() is complex:

Reorders calling channels by LQA scores (lines 572-596)
Computes first-call frequency for status messages
Encodes and queues CMD LQA with KA1 flag
Problem: channel ordering logic (A.5.4.5 bilateral scoring) belongs in LQAAnalyzer, not here. The SM should receive pre-ordered channels, not reorder them on every call.
Lines 866-944: maybe_emit_call_alert() is a monster:

Detects when caller address is complete (HANDSHAKE → AWAIT_ACCEPT/SLOT_WAIT)
Queues CMD LQA for responder
Decodes bilateral SINAD/BER from incoming CMD 'a'
Generates LQA Report sequence (Block C5)
Problem: This is a 77-line cross-cutting concern mixing SM state gates, LQA encoding, and callback emission. Should be refactored as a separate "call-alert handler" subsystem.
Lines 946-1017: Sounding and RX-BER accumulation:

Accumulates sounding frames with frame-averaged SNR/BER
Commits after Tdrw silence
Resolves peer address from SM
Problem: Multiple responsibilities here. The frame-end detection (Tdrw settle) is duplicated in sounding path and non-sounding path (lines 788-798). Should be a single "frame settle detector".
Lines 1019-1140: Auto-Relink and Enhanced Frequency-Select:

Evaluates relink threshold
Handles EFS proposal/response/collision
Problem: FSM embedded in controller (fs_phase_ = IDLE/PROPOSED/EXECUTING). This should be a separate FreqSelectManager component.
Testability
Low testability through public interface. To test one feature (e.g., call initiation), you must:

Attach a fake radio
Set self address
Add channels
Mock LQA database entries
Mock modem callbacks
Mock audio device
A better design would expose sm_ and modulator_ for testing, or inject dependencies.

Complexity Hotspots
Lines 760-863: update() is 103 lines managing:

PTT lead/tail deadlines
SM update dispatch
Call alert emission
Sounding frame commit
RX-BER frame commit
Auto-relink evaluation
EFS timeout/cooldown
Auto-sounding sweep
Offline word-completion
LQA analyzer update
Lines 1332-1569: on_received_word() is 237 lines handling:

Debug RX tracing
Signal quality tracking
Caller address reassembly (HANDSHAKE/WAIT_CYCLE_END)
CMD LQA (Block A5) bilateral RX
CMD NOISE (Block B4) RX
LQA Report (Block C5) RX
EFS CMD 'f' + DATA capture
Sounding frame accumulation
Non-sounding RX-BER accumulation
SM word dispatch
Both are god methods. Break them into smaller, named functions or separate classes.

## 2. ALEStateMachine (ale_state_machine.h + .cpp)
Responsibility Boundary
Implements MIL-STD-188-141B link establishment state machine
Owns calling/handshake/sounding sub-phases
Builds and transmits calling sequences (scanning, leading, conclusion)
Receives and decodes words into structured events
Manages timing windows (Twt, Tt, Twr, Twce, Twa)
Public Interface Width
~45 public methods:

State control (3): process_event, update, get_state
Configuration (10): configure_scan, set_self_address, set_calling_channels, set_target_scan_channels, set_slot_number, set_timing_parameters, set_require_explicit_accept
Operator actions (7): initiate_call, initiate_group_call, send_sounding, terminate_link, respond_to_call, accept_call, reject_call, emergency_manual_control
Word reception (1): process_received_word
Callbacks (6): set_state_callback, set_transmit_callback, set_channel_callback, set_rx_enabled_callback, set_operator_callback, set_lqa_metrics
Inspection (11): get_call_cycle_count, get_calling_phase, get_handshake_phase, get_sounding_phase, thread_violations, etc.
Interface Health: Narrow and clean. State machine doesn't leak.

Complexity Hotspots
Calling sub-state machine (handle_calling(), lines 429-571 in .cpp):

LBT → TUNING → SCANNING_CALL / GROUP_SCANNING_CALL → LEADING_CALL → MESSAGE → CONCLUSION → LISTENING → SENDING_ACK
LISTENING has 3 sub-phases (wait-for-TO, wait-for-TIS, wait-settle) implemented as state flags (response_to_detected, tlww_start_ms)
TX-drain safety net at lines 555-562
Handshake sub-state machine (handle_handshake(), lines 573-766 in .cpp):

WAIT_CYCLE_END has 2 timeout paths (Twce, Tm_max) + caller-address accumulation
SLOT_WAIT, CHANNEL_CHECK (LBT), SENDING_RESPONSE, WAIT_ACK
WAIT_ACK has 3 sub-phases (wait-for-TO, wait-for-TIS, wait-settle)
AllCall (one-way broadcast) short-circuits response TX
Manual-accept gating is obsolete (lines 643-651)
Sounding sub-state (handle_sounding(), lines 865-end in .cpp):

LBT → TRANSMITTING → LISTENING
Multi-channel sweep pin-override via channel_manager_
Calling-phase transitions are driven by on_word_complete() callbacks, NOT time:

Scanner/Leading/Message/Conclusion phases count words, not time
This is correct (signal-time vs. protocol-time) but means the SM cannot drive itself without a modem callback
Problem: The SM has 13+ state enums (ALEState, CallingPhase, HandshakePhase, SoundingPhase, ScanningPhase) with transitions spread across multiple handler functions. A detailed state chart would reveal dead states and unreachable transitions.

Leakage / Tight Coupling
Lines 401-402: process_received_word() → react_scanning(), react_calling(), react_handshake() (private). These are called from controller's on_received_word(), which also manages LQA, caller address, and sounding. Leakage: SM doesn't own the full receive path; controller reconstructs state from SM state queries.

Lines 347-349: Group-call binding (T-11):

On LINKED entry, if active_call_is_group && to_address not empty, active_call_to = to_address
Problem: Overwriting active_call_to after the responder identity changes. This breaks the initial group-call contract. Should track caller identity separately.
Lines 557-569 (LISTENING turnaround window):

Hard-coded latency estimate (Twrt_slow_ms + Tdrw_ms + Tdrw_ms - Tlww_ms = 3136 ms)
The comment admits this is a tuple of protocol constants + margin. Should be a named constant or computed from timing parameters.
Testability
Moderately testable. The SM has no global state (except thread-violation counter). To test:

Instantiate SM
Set callbacks for state/transmit/channel/rx-enable/operator events
Call process_event() and update()
Inject received words via process_received_word()
Assert callbacks fired
Tests in /tests/link/unit/test_state_machine.cpp (lines 89-137) show basic transitions. More complex scenarios (multi-channel calling, multi-word addresses) are tested in /tests but files not fully read.
---> fixed
## 3. LQAMetrics (lqa_metrics.h)
Responsibility Boundary
Collects real-time metrics from demodulator/decoder
Computes derived metrics (SINAD, multipath score, BER)
Manages rolling 60-min noise-floor window (AC-CHAN-004-001)
Feeds LQA database with averaged samples
Public Interface Width
~20 public methods:

Configuration (3): constructor, set_config, set_database
Data collection (2): add_sample
Metric computation (4): calculate_sinad, sinad_to_lqa_code, estimate_ber, detect_multipath
Window management (4): reset, get_sample_count, get_averaged_sample, get_noise_floor_stats
Standalone utilities (7, global functions): encode_lqa_cmd, decode_lqa_cmd, ber_score_to_lqa_code, multipath_delay_to_lqa_code, lqa_age_code
Interface Health: Narrow. Utilities are global because they're spec constants (Table A-XIII/A-XIV encoding).

Leakage / Design Issues
Lines 290-308: Private AccumulatedMetrics struct holds per-frequency, per-station metrics. Only used internally but exposed in structure (not exposed in public API, so not a leakage, but shows the class does windowing and database update internally.)

Line 137: Constructor takes optional LQADatabase* database = nullptr. Allows standalone noise-floor tracking without updating DB. Good design (composition over inheritance).

Lines 222-223: multipath_delay_to_lqa_code() is both a member method AND a global function (407). Duplication. Should be one or the other.

Testability
Good. Metrics are pure functions over sample windows. Testing:

Create LQAMetrics instance
add_sample() repeatedly
Assert get_averaged_sample() matches expected average
No callbacks or state machines
---> fixed
## 4. LQADatabase (lqa_database.h)
Responsibility Boundary
Persistent storage of channel/station quality pairs (freq, remote_station) → LQAEntry
Time-weighted averaging (old_value, new_value, old_samples)
Bilateral (TO-direction) SINAD/BER/MP recording
Handshake failure tracking (A.5.4.5.1 penalty window)
File I/O: binary save/load, CSV export
Public Interface Width
~25 public methods:

Configuration (2): set_config, get_config
Data update (4): update_entry, update_entry_extended, update_bilateral, mark_bilateral_attempted, record_handshake_fail
Data retrieval (4): get_entry, get_entries_for_channel, get_entries_for_station, get_all_entries
Maintenance (3): prune_stale_entries, clear, get_entry_count
File I/O (3): save_to_file, load_from_file, export_to_csv
Utilities (2): compute_score, bilateral_quality_score, get_current_time_ms
Interface Health: Good, but compute_score() and bilateral_quality_score() should arguably be in LQAAnalyzer (higher-level policy), not here.

Leakage
Lines 419-423: Internal EntryKey struct with operator<. Correct for std::map keying, but EntryKey should be named FrequencyStationKey or similar for clarity.

Lines 434-435: time_weighted_average() is private. Should perhaps be a utility function so other components can use the same averaging logic.

Testability
Excellent. Database is a container with time-based operations:

Create, add entries
Query by freq/station
Assert aging and time-weighted values
Save/load round-trip No callbacks, no state machines.

## 5. LQAAnalyzer (lqa_analyzer.h)
Responsibility Boundary
High-level LQA analysis for channel selection
Ranks channels by quality (A.5.4.5 bilateral, A.5.4.5.2 broadcast, A.5.4.5.3 listening modes)
Detects sounding-due conditions
Provides aggregate channel scores
Optionally drives auto-sounding callback
Public Interface Width
~15 public methods:

Configuration (3): constructor, set_config, set_database
Sounding processing (2): process_sounding, process_sounding_extended
Channel selection (3): get_best_channel, get_best_channel_for_station, rank_all_channels, rank_channels_for_station
Sounding scheduling (3): is_sounding_due, get_channels_needing_sounding, set_sounding_callback
Utilities (2): get_channel_quality_summary, get_station_quality_summary, compute_channel_aggregate_score, update()
Interface Health: Clean. Analyzer doesn't leak.

Leakage
Lines 260-274: Private bilateral_channel_score() method has two overloads (one returns score only, one with out-parameters). Duplication. Should have one implementation with optional output params or use a struct.
Testability
Good. Analyzer reads from database and returns rankings:

Populate database with entries
Call rank_all_channels()
Assert order by score No callbacks needed.

## 6. Cross-Cutting Architectural Issues
A. Frame-End Detection (Tdrw Silence Window)
Lines 788-798 (sounding) and 796-799 (non-sounding) in ale_controller.cpp duplicate the same logic:


if (settled_acc_.empty() && settle_ms_ > 0
    && (now_ms - settle_ms_) >= ALETimingConstants::Tdrw_ms) {
    commit_sample();
}
Should be a reusable "FrameSettleDetector" utility.

B. LQA Bilateral Scoring
Encoded in ALEController (lines 533-554, 574-596, 1249-1274)
Implemented in LQADatabase (bilateral_quality_score(), line 409)
Implemented in LQAAnalyzer (bilateral_channel_score(), lines 260-274)
Three implementations of the same spec (A.5.4.5.1 bilateral = (FROM+TO)/2). Consolidate into LQAAnalyzer.

C. AMD / Orderwire Reception
Lines 1332-1408 (on_received_word) manually:

Skip leading-call DATA/REP words (amd_skip_count_)
Collect CMD AMD + DATA words
Decode Expanded-64
Should be encapsulated in an "AmdDecoder" utility that returns (is_amd_start, text_chunk).

D. CMD LQA Handling (Block A)
Encoding: ALEController::compute_lqa_payload() + encode_lqa_cmd()
Decoding: ALEController::on_received_word() lines 1411-1433
Validation: No validation that KA1, SINAD, BER, MP fields are within spec ranges
Should have a dedicated "LqaCmdHandler" or "BilateralExchange" subsystem.

E. SM's Dead Code
HandshakePhase::AWAIT_ACCEPT (lines 643-651 in .cpp) — documented as "legacy, no longer entered" but still in enum. Should be removed or gated behind a feature flag.
set_require_explicit_accept() (line 261 in .h) — called by ALEController but does nothing per comment.

F. TX-Drain Safety Net
Appears 3 times: handle_calling (lines 555-562), handle_handshake (lines 689-696), handle_linked (lines 778-795)
Timeout is shared: ALETimingConstants::TX_DRAIN_TIMEOUT_MS = likely 10s or 30s (not specified in header)
Should be a helper: bool check_tx_drain_timeout(uint32_t start_ms, uint32_t now_ms)

G. Manual-Accept Post-Link Gate
ALEController implements a post-link operator decision via pending_operator_accept_ (lines 286-292, 810):

Handshake auto-completes → link established
SM fires LINK_ESTABLISHED callback
Controller sets pending_operator_accept_ = true (manual mode)
accept_call() / reject_call() clear it or call terminate_link()
This is not in MIL-STD-188-141B. Per the code, the spec's response-window timeout (Twrt ≈ 3s) is too short for operator reaction. This is a bridge-specific feature that leaks into the core SM architecture.

Friction: The SM should never know about this. The operator gate should be in ALEController only, post-event. Currently, set_require_explicit_accept() is a no-op, but the field still exists and is checked (lines 262-263 in .h).

## 7. Architectural Friction Summary
Area	Problem	Lines	Severity
ALEController size	1900+ lines, god method update() (103 L) and on_received_word() (237 L)	760-863, 1332-1569	HIGH
LQA bilateral logic duplication	Spec rule encoded 3 times (controller, DB, analyzer)	533-554, 409, 260-274	MEDIUM
Frame settle detection	Same Tdrw window check duplicated for sounding and non-sounding	788-798	LOW
AMD decoding ad-hoc	Manual address reassembly in on_received_word, no decoder utility	1380-1409	MEDIUM
CMD LQA encoding scattered	compute_lqa_payload(), maybe_emit_call_alert(), on_received_word() all handle LQA	533, 887-932, 1411	MEDIUM
Manual-accept post-link	Feature not in spec, leaks post-link operator gate into SM	286-292, 810, 632-635	MEDIUM
SM dead code	AWAIT_ACCEPT enum still exists but never entered	643-651	LOW
EFS phase machine in controller	FreqSelectPhase IDLE/PROPOSED/EXECUTING embedded in controller	915-922, 1054-1140	MEDIUM
Channel reordering in controller	A.5.4.5 bilateral ranking should happen in LQAAnalyzer, not initiate_call()	556-603	HIGH
No frame assembler separation	Words come from modem, controller does caller-address reassembly, SM processes semantically	1376-1409	MEDIUM

## 8. Testability Assessment
Component	Testability	Why	Recommendation
ALEController	Low	Too many responsibilities; need fake radio, audio device, LQA DB	Extract subsystems; inject dependencies
ALEStateMachine	Moderate	Pure event/timing logic, but called from multiple paths	Good; add more edge-case tests (multi-word addresses, AllCall)
LQAMetrics	High	Pure functions, windowing; no callbacks	Good; add SINAD/BER conversion tests
LQADatabase	High	Container with time-based operations	Good; test time-weighted average edge cases
LQAAnalyzer	Moderate	Reads DB, returns rankings; but depends on DB state	Good; add bilateral collision tests

## 9. Pure-Function Extraction Opportunities (Low-Value)
These are technically pure but so lightweight that extracting them doesn't improve modularity:

is_valid_ale_address() (lines 26-31, ale_controller.cpp) — 5 lines, only called once
is_ale_mode() (lines 33-41) — 8 lines
mode_from_string() / mode_to_string() (lines 166-196) — RadeMode enum marshalling
parse_channel_spec() (lines 49-108) — 59 lines, complex but only called from load_channels()
ber_score_to_lqa_code() (lqa_metrics.h) — 12 lines, spec constant mapping
Verdict: These are fine as-is. Extracting them to a separate utility file adds compilation overhead with minimal cohesion gain. Keep them local.

## Summary
PC-ALE is a well-structured implementation of MIL-STD-188-141B, but ALEController is becoming a god class (1900 lines, ~8 subsystems wired together). Key friction:

High-level policy (LQA ranking, manual-accept post-link) should not be in core SM.
LQA bilateral logic is duplicated across 3 modules.
Frame settle detection (Tdrw silence window) is duplicated.
AMD/CMD LQA encoding is scattered; needs a dedicated handler.
EFS (Enhanced Frequency-Select) embeds a mini-FSM in controller instead of a separate component.
Channel reordering (A.5.4.5) happens in initiate_call() instead of LQAAnalyzer.
Recommendation: Extract a LqaExchangeManager (bilateral + report + noise decode), a FrameSettleDetector utility, and a FreqSelectManager FSM. Move channel ranking to LQAAnalyzer. This would reduce ALEController to ~1200 lines (single responsibility = audio ↔ SM ↔ radio bridge, not LQA policy).