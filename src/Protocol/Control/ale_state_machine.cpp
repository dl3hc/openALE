/**
 * \file ale_state_machine.cpp
 * \brief Implementation of ALE state machine
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_call_processor.h"
#include "Protocol/Message/ale_frame_builder.h"
#include "Protocol/Message/ale_orderwire_protocols.h"
#include "Protocol/Message/frame_validator.h"
#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
#include "LQA/lqa_metrics.h"
#include <algorithm>
#include <cstring>
#include <string>

// Forwards protocol debug events to the injected trace callback; no-op (zero
// overhead) when unset — msg is only evaluated if trace_cb_ is non-null.
#define SM_TRACE(msg) do { if (trace_cb_) trace_cb_(msg); } while(0)

namespace ale {

static const char* STATE_NAMES[] = {
    "IDLE", "SCANNING", "CALLING", "HANDSHAKE", "LINKED", "SOUNDING", "ERROR"
};

static const char* EVENT_NAMES[] = {
    "START_SCAN", "STOP_SCAN", "CALL_REQUEST", "CALL_DETECTED",
    "HANDSHAKE_COMPLETE", "LINK_TIMEOUT", "LINK_TERMINATED", "AMD_DECLINED_LINK",
    "SOUNDING_REQUEST", "SOUNDING_COMPLETE", "ERROR_OCCURRED"
};

// FR-10 frame-level trace line for a reassembled frame: type + completed
// addresses + sender identity — never just the last word. Example:
// "[FRAME] F_RESPONSE to JOE from TIS SAM (7 words)".
static std::string frame_trace_line(const AssembledFrame& f) {
    std::string s = std::string("[FRAME] ") + frame_type_name(f.type);
    for (const auto& a : f.addressed_to) {
        s += " to ";
        s += a;
    }
    if (!f.quick_id.empty()) s += " quick-id " + f.quick_id;
    if (f.complete) {
        s += " from ";
        s += f.conclusion_is_twas ? "TWAS " : "TIS ";
        s += f.conclusion_identity;
    } else {
        s += " (incomplete)";
    }
    if (!f.blocks.empty())
        s += " blocks=" + std::to_string(f.blocks.size());
    s += " words=" + std::to_string(f.word_count);
    return s;
}

// Must match CallingPhase enum order exactly.
static const char* PHASE_NAMES[] = {
    "LBT", "TUNING", "SCANNING_CALL", "GROUP_SCANNING_CALL", "LEADING_CALL", "MESSAGE",
    "CONCLUSION", "LISTENING", "SENDING_ACK"
};

// Must match HandshakePhase enum order exactly.
static const char* HS_PHASE_NAMES[] = {
    "WAIT_CYCLE_END", "AWAIT_ACCEPT", "SLOT_WAIT", "CHANNEL_CHECK", "SENDING_RESPONSE", "WAIT_ACK"
};

// ============================================================================
// Constructor
// ============================================================================

ALEStateMachine::ALEStateMachine()
    : current_state(ALEState::IDLE),
      previous_state(ALEState::IDLE),
      pre_link_state_(ALEState::IDLE),
      link_start_time_ms(0),
      last_word_time_ms(0),
      calling_phase(CallingPhase::LBT),
      active_call_is_net(false),
      active_call_is_group(false),
      first_call_tx_ms(0),
      call_cycle_count(0),
      call_cycles_in_phase(0),
      words_pending(0),
      listening_start_ms(0),
      lbt_start_ms(0),
      tune_start_ms(0),
      calling_channel_index(0),
      response_to_detected(false),
      response_rx_start_ms(0),
      tlww_start_ms(0),
      collecting_remote_conclusion(false),
      emergency_active(false),
      target_scan_channels(1),
      state_entry_time_ms(0),
      current_time_ms(0),
      handshake_phase(HandshakePhase::WAIT_CYCLE_END),
      twce_ms(0),
      twce_start_ms(0),
      hs_tlww_start_ms(0),
      hs_conclusion_rcvd(false),
      hs_words_in_phase(0),
      hs_ack_start_ms(0),
      hs_ack_tis_rcvd(false),
      contiguous_errors(0),
      hs_lbt_start_ms(0),
      hs_message_start_ms(0),
      pending_reject_(false),
      require_explicit_accept_(false),
      accept_decided_(false),
      await_accept_start_ms_(0),
      accept_decision_timeout_ms_(10000),
      linked_terminating_(false),
      sounding_phase_(SoundingPhase::LBT),
      sounding_lbt_start_ms_(0),
      slot_number_(0),
      tswt_ms_(0),
      slot_wait_start_ms_(0),
      scanning_phase_(ScanningPhase::HOPPING)
{}

// ============================================================================
// Static helpers
// ============================================================================

const char* ALEStateMachine::state_name(ALEState state) {
    uint8_t i = static_cast<uint8_t>(state);
    return STATE_NAMES[i > 6 ? 6 : i];
}

const char* ALEStateMachine::event_name(ALEEvent event) {
    uint8_t i = static_cast<uint8_t>(event);
    return i > 10 ? "UNKNOWN" : EVENT_NAMES[i];
}

// ============================================================================
// Event processing
// ============================================================================

bool ALEStateMachine::process_event(ALEEvent event) {
    switch (current_state) {
        case ALEState::IDLE:
            if (event == ALEEvent::START_SCAN)       return transition_to(ALEState::SCANNING);
            if (event == ALEEvent::CALL_REQUEST)     return transition_to(ALEState::CALLING);
            if (event == ALEEvent::CALL_DETECTED)    return transition_to(ALEState::HANDSHAKE);
            if (event == ALEEvent::SOUNDING_REQUEST) return transition_to(ALEState::SOUNDING);
            break;

        case ALEState::SCANNING:
            if (event == ALEEvent::STOP_SCAN)        return transition_to(ALEState::IDLE);
            if (event == ALEEvent::CALL_DETECTED)    return transition_to(ALEState::HANDSHAKE);
            if (event == ALEEvent::CALL_REQUEST)     return transition_to(ALEState::CALLING);
            if (event == ALEEvent::SOUNDING_REQUEST) return transition_to(ALEState::SOUNDING);  // T-04
            break;

        case ALEState::CALLING:
            if (event == ALEEvent::HANDSHAKE_COMPLETE) return transition_to(ALEState::LINKED);
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(pre_link_state_);  // T-01
            if (event == ALEEvent::AMD_DECLINED_LINK)  return transition_to(pre_link_state_);  // graceful, no link wanted
            break;

        case ALEState::HANDSHAKE:
            if (event == ALEEvent::HANDSHAKE_COMPLETE) return transition_to(ALEState::LINKED);
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(pre_link_state_);  // T-01
            if (event == ALEEvent::AMD_DECLINED_LINK)  return transition_to(pre_link_state_);  // graceful, no link wanted
            break;

        case ALEState::LINKED:
            if (event == ALEEvent::LINK_TERMINATED ||
                event == ALEEvent::LINK_TIMEOUT)       return transition_to(pre_link_state_);  // T-01
            break;

        case ALEState::SOUNDING:
            if (event == ALEEvent::SOUNDING_COMPLETE) {
                // Multi-channel sweep: advance to the next channel instead of
                // returning to the previous state, until the list is exhausted.
                if (sounding_sweep_active_) {
                    ++sounding_sweep_idx_;
                    if (sounding_sweep_idx_ < sounding_sweep_chs_.size()) {
                        channel_manager_.set_override(sounding_sweep_chs_[sounding_sweep_idx_]);
                        // Re-arm LBT phase directly: transition_to(SOUNDING) from SOUNDING is a no-op.
                        sounding_phase_        = SoundingPhase::LBT;
                        sounding_lbt_start_ms_ = current_time_ms;
                        if (rx_enabled_callback) rx_enabled_callback(true);
                        return true;
                    }
                    // Sweep exhausted — drop out of sweep mode and return.
                    sounding_sweep_active_ = false;
                    sounding_sweep_chs_.clear();
                    sounding_sweep_idx_    = 0;
                    // The override is cleared by enter_state(IDLE/SCANNING) below.
                }
                return transition_to(previous_state);  // previous_state = IDLE|SCANNING
            }
            if (event == ALEEvent::CALL_DETECTED) {
                // T-08: a return call during sounding LISTENING links on the
                // current (sweep) channel. Stop the sweep (no resume after the
                // link); keep the channel override so handshake + LQA recording
                // use the correct channel — cleared on eventual return to IDLE/SCANNING.
                sounding_sweep_active_ = false;
                sounding_sweep_chs_.clear();
                sounding_sweep_idx_    = 0;
                return transition_to(ALEState::HANDSHAKE);
            }
            break;

        case ALEState::ERROR:
            if (event == ALEEvent::START_SCAN) return transition_to(ALEState::SCANNING);
            else                               return transition_to(ALEState::IDLE);
    }

    if (event == ALEEvent::ERROR_OCCURRED)
        return transition_to(ALEState::ERROR);

    return false;
}

// ============================================================================
// Periodic update
// ============================================================================

void ALEStateMachine::update(uint32_t now_ms) {
    check_thread_();
    // Clock must be monotonically non-decreasing: every timeout is
    // `current_time_ms - some_start_ms >= threshold` on uint32_t, wrap-safe only
    // while the clock moves forward. A backward jump (clock skew, resume-from-
    // sleep, disagreeing callers) would wrap `(current - start)` near 2^32 and
    // fire EVERY threshold at once — a spurious LINK_TIMEOUT mid-handshake/link.
    // Distinguish a real backward jump (unsigned delta >= 2^31) from a
    // legitimate forward wrap (delta near 0); hold the logical clock on a
    // backward step (SM freezes until source catches up) instead of dropping
    // the link. A genuine 49.7-day forward wrap reads as a tiny delta and is
    // handled correctly by the unsigned arithmetic.
    if ((now_ms - current_time_ms) >= 0x80000000u) {
        SM_TRACE("[TRACE] update: non-monotonic clock (backward jump) — holding\n");
        return;
    }
    this->current_time_ms = now_ms;

    // ── OFS FrameReassembler shadow feed (Phase 2, docs/FRAMING_STANDARD.md) ──
    // Observes the word stream in parallel; nothing consumes its Frame events
    // yet (Phase 3 wires them into the §8 context matrix). Completed frames
    // are drained here and traced at frame level — FR-10: RX diagnostics
    // name the frame type and the completed addresses, not just the last word.
    frame_reassembler_.tick(current_time_ms);
    for (const auto& f : frame_reassembler_.take_completed())
        SM_TRACE(frame_trace_line(f));

    if (check_link_timeout()) {
        process_event(ALEEvent::LINK_TIMEOUT);
        return;
    }

    // ── ALLCALL broadcast TX-drain safety net ───────────────────────────────
    // Flag-gated not state-gated: current_state stays IDLE/SCANNING for the
    // whole broadcast (send_allcall_broadcast()/on_word_complete()'s
    // allcall_broadcasting_ branch), and IDLE has no handle_*() at all, so this
    // can't live in a per-state handler like the other TX-drain nets do
    // (handle_linked's linked_terminating_/orderwire_transmitting_,
    // handle_calling's SENDING_ACK/SENDING_RESPONSE). Same purpose: if
    // on_word_complete() never drains words_pending to 0 (audio stall, or a
    // transmit_callback that doesn't arm completion), RX would stay disabled
    // forever with nothing else to recover it. No state transition happens
    // here to trigger enter_state()'s reset, so clear words_pending explicitly.
    if (allcall_broadcasting_ && tx_drain_start_ms_ != 0 &&
        (current_time_ms - tx_drain_start_ms_) >= tx_drain_deadline_ms_) {
        SM_TRACE("[TRACE] update: AllCall broadcast drain timeout — force-recovering\n");
        allcall_broadcasting_ = false;
        tx_drain_start_ms_    = 0;
        words_pending         = 0;
        if (rx_enabled_callback) rx_enabled_callback(true);
        return;
    }

    switch (current_state) {
        case ALEState::SCANNING:  handle_scanning();  break;
        case ALEState::CALLING:   handle_calling();   break;
        case ALEState::HANDSHAKE: handle_handshake(); break;
        case ALEState::LINKED:    handle_linked();    break;
        case ALEState::SOUNDING:  handle_sounding();  break;
        default: break;
    }
}

// ============================================================================
// State transitions
// ============================================================================

bool ALEStateMachine::transition_to(ALEState new_state) {
    if (current_state == new_state) return false;

    exit_state(current_state);
    previous_state      = current_state;
    current_state       = new_state;
    state_entry_time_ms = current_time_ms;
    enter_state(new_state);

    if (state_callback)
        state_callback(previous_state, current_state);

    return true;
}

void ALEStateMachine::enter_state(ALEState new_state) {
    // words_pending is scoped to a single state's TX burst: in a normal
    // transition the queue has already drained to 0 before leaving the old
    // state, so this is normally a no-op. Abnormal exits — e.g.
    // emergency_manual_control() transmits a TWAS frame then transitions to
    // IDLE *before* those completions can drain — would otherwise leak a
    // stale count into the next state (IDLE has no on_word_complete handler
    // to clear it), and a later send_sounding() would inherit it, never
    // reach 0, and hang in TRANSMITTING with no timeout. Resetting on every
    // entry makes "a state starts with no pending words" an invariant.
    words_pending        = 0;
    // Same invariant for the TX-drain deadline (terminate_link/orderwire/
    // SENDING_RESPONSE/SENDING_ACK): no TX burst pending on a fresh state.
    tx_drain_start_ms_    = 0;
    tx_drain_deadline_ms_ = ALETimingConstants::TX_DRAIN_TIMEOUT_MS;

    // Any real state change ends an in-flight LINKED-AMD confirm exchange (it
    // only lives *within* LINKED; leaving LINKED — teardown/timeout — or
    // (re)entering it as a fresh link clears it). See LinkedAmdPhase.
    linked_amd_phase_          = LinkedAmdPhase::NONE;
    linked_amd_listen_start_ms_ = 0;
    linked_amd_resp_detected_  = false;
    linked_amd_resp_tlww_ms_   = 0;
    linked_amd_ack_to_detected_ = false;
    linked_amd_ack_tis_rcvd_   = false;
    linked_amd_ack_tlww_ms_    = 0;
    linked_amd_retry_pending_  = false;
    linked_amd_retry_after_ms_ = 0;

    switch (new_state) {
        case ALEState::IDLE:
            // Available state: RX on, fixed channel, wait for incoming calls.
            channel_manager_.clear_override();  // end any sounding-sweep pin
            allcall_silent_ = false;             // leave any AllCall handshake
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::SCANNING:
            scanning_phase_          = ScanningPhase::HOPPING;
            scan_pause_settle_ms_       = 0;
            prev_hop_ready_          = true;    // first settle edge re-anchors the dwell
            channel_manager_.clear_override();  // end any sounding-sweep pin
            channel_manager_.start(current_time_ms);
            allcall_silent_ = false;             // leave any AllCall handshake
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::CALLING:
            pre_link_state_                = previous_state;  // T-01: IDLE or SCANNING
            link_start_time_ms             = current_time_ms;
            // Informational TX-sequence start; set at end of TUNING when the
            // radio is tuned and ready (AC-LINK-017-2).
            first_call_tx_ms               = 0;
            lbt_start_ms                   = current_time_ms;
            tune_start_ms                  = 0;
            call_cycle_count               = 0;
            call_cycles_in_phase           = 0;
            listening_start_ms             = 0;
            response_to_detected           = false;
            response_rx_start_ms           = 0;
            tlww_start_ms                  = 0;
            collecting_remote_conclusion   = false;
        to_address.clear();

            // Activate first calling channel if a list was set
            if (!calling_channels.empty())
                channel_manager_.hop_calling(calling_channels[calling_channel_index]);

            // AC-LINK-017-1: always start with LBT; TX begins only after Twt + Tt.
            calling_phase = CallingPhase::LBT;
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::HANDSHAKE:
            pre_link_state_       = previous_state;  // T-01: return to state before HANDSHAKE
            slot_wait_start_ms_   = 0;
            link_start_time_ms    = current_time_ms;
            // ── Handshake sub-state init (Fix 2/3/4) ─────────────────────
            twce_start_ms       = current_time_ms;
            hs_tlww_start_ms    = 0;
            hs_conclusion_rcvd  = false;
            hs_conclusion_is_twas_ = false;
            caller_address.clear();
            hs_words_in_phase   = 0;
            hs_ack_start_ms     = 0;
            hs_ack_to_ms        = 0;
            hs_ack_tis_rcvd     = false;
            contiguous_errors   = 0;
            hs_lbt_start_ms     = 0;
            hs_message_start_ms = 0;
            pending_reject_     = false;
            handshake_phase     = HandshakePhase::WAIT_CYCLE_END;
            // Twce = 2 × own Ts (Table A-XV); fall back to 1 channel if not configured.
            {
                const uint32_t C = static_cast<uint32_t>(
                    std::max(size_t(1), channel_manager_.channel_count()));
                twce_ms = ale::calc_twce_ms(C);
            }
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::LINKED:
            link_start_time_ms  = current_time_ms;
            last_word_time_ms   = current_time_ms;
            linked_terminating_ = false;
            linked_twas_addr_.clear();   // fresh link: no TWAS conclusion accumulating
            linked_twas_last_ms_ = 0;
            tx_drain_start_ms_ = 0;   // no TX drain pending on a fresh link
            allcall_silent_ = false;          // AllCall concluded → normal linked state
            // Fresh link: arm the idle warning (fires IDLE_WARNING_LEAD_MS before Twa).
            idle_warning_sent_      = false;
            last_seen_word_time_ms_ = current_time_ms;
            // Bind active_call_to to the actual linked peer so terminate_link()
            // and the Twa-timeout TWAS address the right station.
            //
            // SAM (caller), individual: active_call_to already = target address
            //   set at initiate_call() — no fixup needed.
            //
            // SAM (caller), group (T-11): active_call_to = members.front() but
            //   the actual responder is in to_address (react_calling TIS_CALLER).
            if (active_call_is_group && !to_address.empty())
                active_call_to = to_address;
            //
            // JOE (responder): react_scanning set active_call_to to the first
            //   3 chars of our own TO address (the word that addressed us).
            //   Caller's full address is in caller_address, collected from
            //   their TIS conclusion in react_handshake WAIT_CYCLE_END — bind
            //   it here so termination frames address the actual peer.
            if (!active_call_is_group && !caller_address.empty())
                active_call_to = caller_address;
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::SOUNDING:
            sounding_phase_                = SoundingPhase::LBT;
            sounding_lbt_start_ms_          = current_time_ms;
            sounding_listening_start_ms_    = 0;   // no LISTENING window yet
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::ERROR:
            // AC-GEN-009-001: no dead state — enable RX so the radio is always listening
            // when not transmitting (exit_state of CALLING/HANDSHAKE leaves RX=false).
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        default:
            break;
    }
}

void ALEStateMachine::exit_state(ALEState old_state) {
    switch (old_state) {
        case ALEState::LINKED:
            active_call_to.clear();
            active_call_from.clear();
            break;

        default:
            break;
    }
}

// ============================================================================
// State handlers
// ============================================================================

void ALEStateMachine::handle_scanning() {
    if (scanning_phase_ == ScanningPhase::SCAN_PAUSE) {
        // A.5.3.1: stay on channel until Tdrw (2×Trw = 784 ms) silence after the
        // last word — ensures the full frame (TIS/TWAS conclusion + DATA address
        // words) is received before moving on.
        if ((current_time_ms - scan_pause_settle_ms_) >= ALETimingConstants::Tdrw_ms) {
            channel_manager_.hop_next(current_time_ms);
            scanning_phase_ = ScanningPhase::HOPPING;
        }
        return;
    }
    // HOPPING. §A.5.3.3: anchor the dwell at the SETTLE edge, not tune-issue.
    // With an async radio the tune completes some time after the hop; measuring
    // dwell from tune-issue would leave only (dwell − settle_latency) of
    // on-channel observation — a 200 ms dwell with ~150 ms TCP settle leaves
    // ~50 ms to detect traffic (0 when settle ≥ dwell), so the scanner hops
    // over signals. Re-anchoring when hop_ready_() first goes false→true after
    // a hop gives the full configured dwell of settled listening; per-channel
    // period becomes settle_latency + dwell (slower cadence, deliberately
    // traded for reliable detection). Sync backends: hop_ready_() always true
    // → no edge → dwell anchored at the hop exactly as before (unchanged
    // behaviour for tests/mocks).
    const bool ready = hop_ready_();
    if (ready && !prev_hop_ready_)
        channel_manager_.anchor_dwell(current_time_ms);   // settle edge → restart dwell
    prev_hop_ready_ = ready;

    // Hop once the (settle-anchored) dwell has elapsed AND the radio is hop-ready.
    // At most one tune is ever in flight, so a Stop or scan pause halts the radio
    // within one physical tune.
    if (channel_manager_.check_dwell_timeout(current_time_ms) && ready)
        channel_manager_.hop_next(current_time_ms);
}

/**
 * handle_calling — Individual call protocol per MIL-STD-188-141B A.5.5.3.1
 *
 * Phase sequence (Figure A-29):
 *
 *  LBT:      listen Twt (784 ms) — AC-LINK-017-1                 [protocol time]
 *  TUNING:   tune Tt (1045 ms) — AC-LINK-017-2                   [protocol time]
 *              → at tune-complete the full deterministic TX sequence
 *                (scanning + leading + conclusion) is enqueued back-to-back
 *                via enqueue_call_sequence_().
 *  SCANNING_CALL / GROUP_SCANNING_CALL / LEADING_CALL / CONCLUSION:
 *            passive — audio layer consumes queued symbol frames gap-free;
 *            each consumed frame fires on_word_complete(), advancing
 *            counters/phases. The Trw grid is a property of the sample
 *            stream itself (one word = 49 symbols × 8 ms = 392 ms), never
 *            of wall time.
 *  LISTENING: wait Twr/Twrt for JOE's response                   [protocol time]
 *               "TO SAM" → arm response tracking; "TIS JOE" → arm Tlww;
 *               Tlww elapsed → SENDING_ACK
 *  SENDING_ACK: TO JOE × 2 + TIS SAM (REQ-LINK-008), enqueued as one frame;
 *               complete → HANDSHAKE_COMPLETE → LINKED
 *
 * This function owns protocol-time windows (LBT, TUNING, LISTENING) and
 * starting RX-dependent TX (SENDING_ACK). TX progress is never derived from
 * time here — frame-completion events are the only TX clock (DD-009/DD-013;
 * signal time vs. protocol time separation).
 */
void ALEStateMachine::handle_calling() {
    switch (calling_phase) {

        // ── LBT ──────────────────────────────────────────────────────────────────
        // Listen Twt before first TX — AC-LINK-017-1. Duration per A.5.4.7.1:
        // 784 ms only when every channel involved is ALE-only, else ≥2 s
        // (effective_twt_ms_/set_lbt_shared). RX is open; broadband occupancy
        // query (A.5.4.7.2, set_channel_busy_query) is polled every tick —
        // traffic detected → select another channel (A.5.4.7: "for a call
        // another channel shall be selected").
        case CallingPhase::LBT: {
            if (lbt_channel_busy_()) {
                SM_TRACE("[TRACE] handle_calling: channel occupied during LBT → next channel\n");
                try_next_calling_channel();
                break;
            }
            if ((current_time_ms - lbt_start_ms) >= effective_twt_ms_()) {
                calling_phase = CallingPhase::TUNING;
                tune_start_ms = current_time_ms;
                if (rx_enabled_callback) rx_enabled_callback(false);  // blind tune
            }
            break;
        }

        // ── TUNING ───────────────────────────────────────────────────────────────
        // Blind tune Tt (1045 ms default; per-instance override via
        // set_timing_parameters(), see TimingParameters) — AC-LINK-017-2. At
        // tune-complete the full TX sequence is handed to the modem so the
        // audio layer renders it as one contiguous transmission.
        case CallingPhase::TUNING: {
            if ((current_time_ms - tune_start_ms) >= timing_.Tt_ms) {
                first_call_tx_ms     = current_time_ms;
                call_cycles_in_phase = 0;
                // T-06: net calls use the same SAM path as individual calls.
                // T-11: group calls use GROUP_SCANNING_CALL (THRU/REP instead of TO).
                if (active_call_is_group && target_scan_channels > 0) {
                    calling_phase = CallingPhase::GROUP_SCANNING_CALL;
                } else if (target_scan_channels > 0) {
                    calling_phase = CallingPhase::SCANNING_CALL;
                } else {
                    calling_phase = CallingPhase::LEADING_CALL;
                }
                enqueue_call_sequence_();
            }
            break;
        }

        // ── TX phases ─────────────────────────────────────────────────────
        // All words were enqueued at tune-complete; the audio layer renders
        // them without gaps. on_word_complete() drives all phase transitions.
        // RX was disabled at LBT→TUNING.
        case CallingPhase::SCANNING_CALL:
        case CallingPhase::GROUP_SCANNING_CALL:
        case CallingPhase::LEADING_CALL:
        case CallingPhase::CONCLUSION:
            break;

        // ── LISTENING ─────────────────────────────────────────────────────
        // Three sub-phases driven by response detection:
        //
        // (a) !response_to_detected:
        //     Waiting for JOE's first "TO SAM" word. TX→RX window: spans from
        //     SAM's own conclusion (local TX event) to JOE's first received
        //     word, so it accumulates the full round-trip audio latency 2×L
        //     on top of JOE's protocol turnaround.
        //
        //     JOE's turnaround after SAM's conclusion ends (T0):
        //       Tlww (392, post-conclusion wait)
        //       + Tdrw (784, JOE's CHANNEL_CHECK/LBT — AC-LINK-002-002)
        //       + Trw  (392, JOE transmits its first response word)
        //       → SAM's pipeline recognises it at T0 + 1568 + 2×L.
        //     Twrt_slow (1960) covers the 1568 turnaround with margin; +Tdrw
        //     (784) absorbs ~390 ms/direction of WASAPI + virtual-cable
        //     latency. Identical for single- and multi-channel: per-channel
        //     responder turnaround doesn't depend on caller's channel count.
        //     Timeout → AC-LINK-019-6 → try_next_calling_channel().
        //
        // (b) response_to_detected && tlww_start_ms == 0:
        //     Saw "TO SAM", waiting for JOE's "TIS JOE" conclusion.
        //     Timeout (5×Trw) → AC-LINK-019-8 → try_next_calling_channel().
        //
        // (c) tlww_start_ms > 0:
        //     Saw "TIS JOE", waiting Tlww for last copy of JOE's conclusion.
        //     Tlww elapsed → close RX, → SENDING_ACK.
        case CallingPhase::LISTENING: {
            if (!response_to_detected) {
                // (a) — responder turnaround + decode + 2×L latency.
                // AC-LINK-019-6: abort if TO_SELF not decoded within this window.
                // Window factored into listening_response_wait_ms_() so the
                // LINKED-AMD confirm LISTEN reuses the identical timing.
                if ((current_time_ms - listening_start_ms) >= listening_response_wait_ms_())
                    try_next_calling_channel(); // AC-LINK-019-6
            } else if (tlww_start_ms == 0) {
                // (b) — waiting for TIS JOE conclusion. Measured as silence
                // since the last received word, not from the first "TO SAM":
                // a multi-word own address makes JOE repeat the doubled "TO
                // SAM…" block for many Trw before its TIS, which a from-
                // first-word budget would cut off (same defect as the
                // WAIT_CYCLE_END/WAIT_ACK conclusion windows).
                if ((current_time_ms - last_word_time_ms)
                        >= 5u * ALETimingConstants::Trw_ms)
                    try_next_calling_channel(); // AC-LINK-019-8
            } else {
                // (c) — let JOE's conclusion settle. tlww_start_ms is re-armed
                // by every DATA/REP extension word; the Tdrw (2×Trw) window
                // must exceed one on-grid word period so a multi-word JOE
                // address (TIS+DATA+REP…) is fully collected into to_address
                // before the ACK is built (else SAM would ACK a truncated address).
                if ((current_time_ms - tlww_start_ms) >= ALETimingConstants::Tdrw_ms) {
                    calling_phase                = CallingPhase::SENDING_ACK;
                    call_cycles_in_phase         = 0;
                    collecting_remote_conclusion = false;
                    if (rx_enabled_callback)
                        rx_enabled_callback(false); // close RX before TX
                }
            }
            break;
        }

        // ── SENDING_ACK ───────────────────────────────────────────────────
        // Third handshake frame: TO JOE × 2 + TIS/TWAS SAM (Ion2G-style link
        // decision — see build_ack_words()). Transitions to LINKED (TIS) or
        // pre_link_state_ via AMD_DECLINED_LINK (TWAS) in on_word_complete()
        // after all words sent.
        case CallingPhase::SENDING_ACK: {
            if (words_pending > 0) {
                // Waiting for the frame to drain (always short — no message
                // content here, see build_ack_words()). Bound the wait so an
                // audio stall (or a transmit_callback not arming completion)
                // doesn't hang here until the 30 s Twa backstop.
                if (tx_drain_start_ms_ != 0 &&
                    (current_time_ms - tx_drain_start_ms_) >= tx_drain_deadline_ms_) {
                    SM_TRACE("[TRACE] handle_calling: SENDING_ACK drain timeout → LINK_TIMEOUT\n");
                    tx_drain_start_ms_ = 0;
                    process_event(ALEEvent::LINK_TIMEOUT);
                    return;
                }
                break;
            }
            if (call_cycles_in_phase == 0)
                build_ack_words();
            break;
        }

    }
}

void ALEStateMachine::handle_handshake() {
    switch (handshake_phase) {

        // ── WAIT_CYCLE_END ────────────────────────────────────────────────
        // Listen for calling station's conclusion (TIS SAM) within Twce.
        // Incoming words are processed by process_received_word().
        case HandshakePhase::WAIT_CYCLE_END: {
            // Twce timeout: calling cycle did not end → abort (A.5.5.3.2, AC-LINK-018-5).
            //
            // Measured as SILENCE since the last received word, not wall time
            // since HANDSHAKE entry. The calling cycle's length is set by the
            // CALLER (scanning section length + leading call = full address
            // sent twice), so a multi-word address (TO+DATA+REP+…, up to 5
            // words → 10 words doubled) legitimately pushes the conclusion
            // (TIS) several Trw past entry. Anchoring Twce at entry aborted
            // such calls before the conclusion arrived — 3-char calls linked,
            // longer ones did not. Each received word re-anchors
            // last_word_time_ms; an undecodable sequence is still caught by
            // contiguous_errors (handle_invalid_word).
            if (!hs_conclusion_rcvd &&
                (current_time_ms - last_word_time_ms) >= twce_ms) {
                SM_TRACE("[TRACE] handle_handshake: Twce silence → LINK_TIMEOUT\n");
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // Tmmax: message section began but conclusion not yet received → abort
            // (A.5.5.3.2, AC-LINK-018-5 second condition)
            if (!hs_conclusion_rcvd && hs_message_start_ms > 0 &&
                (current_time_ms - hs_message_start_ms) >= ALETimingConstants::Tm_max_ms) {
                SM_TRACE("[TRACE] handle_handshake: Tmmax elapsed without conclusion → LINK_TIMEOUT\n");
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // Conclusion received + last-word settle elapsed → SLOT_WAIT (T-09).
            // hs_tlww_start_ms is re-armed by every DATA/REP extension word, so
            // the settle measures silence after the conclusion's *last* word.
            // Uses Tdrw (2×Trw), not Tlww (1×Trw): a multi-word caller address
            // (TIS+DATA+REP…) sends extension words one Trw apart, and a 1×Trw
            // settle races the next on-grid word — the phase would advance
            // before the extension was appended, truncating the caller address.
            if (hs_conclusion_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tdrw_ms) {
                if (hs_conclusion_is_twas_) {
                    // TWAS conclusion (A.5.5.3.2 individual-call rejection, or
                    // A.5.5.4.4 AllCall/wildcard "resume scanning, don't
                    // respond") — caller_address is now fully settled
                    // (multi-word extensions included, via the same
                    // DATA_EXTENSION path TIS uses), so on_sm_state_change()
                    // correctly attributes any AMD already reassembled by
                    // rx_accumulate_call_amd() before this abort discards it.
                    SM_TRACE("[TRACE] handle_handshake: TWAS conclusion settle → LINK_TIMEOUT\n");
                    process_event(ALEEvent::LINK_TIMEOUT);
                    return;
                }
                // AllCall (A.5.5.4.4): one-way broadcast — no response frame.
                // On TIS conclusion, link directly to the caller (the
                // conclusion carries the caller's real address; the AllCall
                // address never appears in it). Skip SLOT_WAIT/CHANNEL_CHECK/
                // SENDING_RESPONSE/WAIT_ACK entirely — caller expects no ACK.
                if (allcall_silent_) {
                    SM_TRACE("[TRACE] handle_handshake: AllCall TIS conclusion → LINKED (no response)\n");
                    active_call_to = caller_address;  // terminate/TWAS would address the AllCall caller
                    if (operator_callback)
                        operator_callback(OperatorEvent::LINK_ESTABLISHED);
                    process_event(ALEEvent::HANDSHAKE_COMPLETE);
                    return;
                }
                // Manual accept no longer gates the handshake: the responder
                // always auto-advances to SLOT_WAIT and completes the 3-way
                // handshake within Twr/Twrt (MIL-STD-188-141B interoperability
                // — caller's response-wait window is only ~3 s, far shorter
                // than practical operator reaction time). Operator approval
                // is applied POST-link by ALEController (LINKED_PENDING_OPERATOR),
                // not here. require_explicit_accept_ is retained as a stored
                // flag but no longer pauses the protocol; see docs/GUI_BRIDGE_GAPS.md.
                SM_TRACE("[TRACE] handle_handshake: conclusion settle → SLOT_WAIT\n");
                handshake_phase     = HandshakePhase::SLOT_WAIT;
                slot_wait_start_ms_ = current_time_ms;
                // RX stays open during slot wait.
            }
            break;
        }

        // ── AWAIT_ACCEPT (legacy, no longer entered) ──────────────────────
        // Retained as a dead phase so HandshakePhase stays stable for
        // tests/serialization; the settle above never transitions here now.
        // accept_call()/reject_call() on the SM are no-ops; operator decision
        // is handled post-link by ALEController.
        case HandshakePhase::AWAIT_ACCEPT:
            handshake_phase     = HandshakePhase::SLOT_WAIT;
            slot_wait_start_ms_ = current_time_ms;
            break;

        // ── SLOT_WAIT ─────────────────────────────────────────────────────
        // T-09: wait tswt_ms_ (0 for individual call) before LBT begins.
        case HandshakePhase::SLOT_WAIT: {
            if ((current_time_ms - slot_wait_start_ms_) >= tswt_ms_) {
                SM_TRACE("[TRACE] handle_handshake: SLOT_WAIT elapsed → CHANNEL_CHECK\n");
                handshake_phase = HandshakePhase::CHANNEL_CHECK;
                hs_lbt_start_ms = current_time_ms;
            }
            break;
        }

        // ── CHANNEL_CHECK ─────────────────────────────────────────────────
        // Listen-Before-Transmit: Tdrw = 2×Trw = 784 ms (AC-LINK-002-002 /
        // A.5.5.3.3 / Table A-XV). The LBT must span the spec's "detect
        // redundant word period" Tdrw so a competing station's in-progress
        // redundant word is reliably caught — a single Trw window can miss it.
        // Any word received here signals channel busy → abort (AC-LINK-019-3);
        // process_received_word() handles the busy-detection path.
        case HandshakePhase::CHANNEL_CHECK: {
            // Broadband occupancy (A.5.4.7.2) — same abort as the valid-word
            // busy path (AC-LINK-019-3); non-ALE traffic never reaches
            // process_received_word(), must be caught here.
            if (lbt_channel_busy_()) {
                SM_TRACE("[TRACE] handle_handshake: channel occupied during LBT → LINK_TIMEOUT\n");
                process_event(ALEEvent::LINK_TIMEOUT);
                break;
            }
            if ((current_time_ms - hs_lbt_start_ms) >= ALETimingConstants::Tdrw_ms) {
                SM_TRACE("[TRACE] handle_handshake: LBT clear → SENDING_RESPONSE\n");
                if (rx_enabled_callback) rx_enabled_callback(false);
                handshake_phase   = HandshakePhase::SENDING_RESPONSE;
                hs_words_in_phase = 0;
                build_response_words();
            }
            break;
        }

        // ── SENDING_RESPONSE ──────────────────────────────────────────────
        // Response TX in progress; on_word_complete() drives the counter and
        // transitions to WAIT_ACK once all words are sent. If audio stalls
        // (or the frame completion is never armed) the phase would hang until
        // the 30 s Twa backstop — bound it with the TX-drain deadline so the
        // SM aborts to pre_link_state promptly. tx_drain_deadline_ms_ is
        // scaled to the actual burst length at the arm site
        // (build_response_words()) — a response frame carrying a full
        // bilateral LQA report isn't cut off by a budget sized for the bare response.
        case HandshakePhase::SENDING_RESPONSE: {
            if (tx_drain_start_ms_ != 0 &&
                (current_time_ms - tx_drain_start_ms_) >= tx_drain_deadline_ms_) {
                SM_TRACE("[TRACE] handle_handshake: SENDING_RESPONSE drain timeout → LINK_TIMEOUT\n");
                tx_drain_start_ms_ = 0;
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            break;
        }

        // ── WAIT_ACK ─────────────────────────────────────────────────────
        // Wait for SAM's ACK frame (TO JOE × 2 + TIS SAM — Figure A-31).
        // Per A.5.5.3.4 + NOTE 1 this has two sub-phases, mirroring SAM's
        // LISTENING(a)/(b)/(c). All windows are measured from JOE's response
        // end (Rb = hs_ack_start_ms), a TX→RX boundary that carries the full
        // round-trip audio latency 2×L on top of the protocol path.
        //
        // (1) hs_ack_to_ms == 0 — "TO JOE" must *start* within Twr (the narrow
        //     window of NOTE 1 that prevents the ACK being mistaken for a new
        //     call → ping-pong). SAM's ACK turnaround, measured from Rb:
        //       SAM detects JOE conclusion ≈ Rb + L
        //       + Tlww (392) → SAM starts ACK
        //       + Trw  (392) first "TO JOE" word on air
        //       + L + Trw (SW-pipeline decode) → JOE recognises ≈ Rb + 784 + 2L
        //     SAM's ACK path has no CHANNEL_CHECK (unlike JOE's response), so
        //     Twr_slow + Tdrw = 1699 ms suffices (covers ~450 ms/dir latency).
        //     Timeout → abort; JOE returns to pre-link state and a genuinely
        //     late "TO JOE" is then handled as a fresh call (NOTE 1).
        //
        // (2) hs_ack_to_ms > 0 — "TO JOE" seen; wait for the "TIS SAM"
        //     conclusion, now governed by the frame limit (5×Trw), not Twr.
        //
        // (3) hs_ack_tis_rcvd — Tlww settle → LINKED.
        case HandshakePhase::WAIT_ACK: {
            if (hs_ack_to_ms == 0) {
                // (1) — ACK start must arrive within this turnaround window.
                // Window factored into wait_ack_start_wait_ms_() so the LINKED-AMD
                // confirm receiver's WAIT_ACK reuses the identical timing.
                if ((current_time_ms - hs_ack_start_ms) >= wait_ack_start_wait_ms_()) {
                    SM_TRACE("[TRACE] handle_handshake: WAIT_ACK Twr timeout (no ACK start) → LINK_TIMEOUT\n");
                    process_event(ALEEvent::LINK_TIMEOUT);
                    return;
                }
            } else if (!hs_ack_tis_rcvd) {
                // (2) — waiting for "TIS SAM" conclusion. Measured as silence
                // since the last received ACK word, not from the first "TO JOE":
                // a multi-word peer address makes the doubled "TO JOE…" block
                // span many Trw before its TIS, which a from-first-word budget
                // would cut off (same defect as WAIT_CYCLE_END Twce above).
                if ((current_time_ms - last_word_time_ms) >= 5u * ALETimingConstants::Trw_ms) {
                    SM_TRACE("[TRACE] handle_handshake: WAIT_ACK no conclusion → LINK_TIMEOUT\n");
                    process_event(ALEEvent::LINK_TIMEOUT);
                    return;
                }
            }
            // (3) TIS [caller] received + last-word settle elapsed → LINKED
            // (A.5.5.3.4). hs_tlww_start_ms is re-armed by each DATA/REP
            // extension word; the Tdrw (2×Trw) settle exceeds one on-grid word
            // period so a multi-word caller conclusion is fully collected
            // before the link is declared up (same fix as WAIT_CYCLE_END settle).
            if (hs_ack_tis_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tdrw_ms) {
                SM_TRACE("[TRACE] handle_handshake: ACK conclusion settle → LINKED\n");
                if (operator_callback)
                    operator_callback(OperatorEvent::LINK_ESTABLISHED);
                process_event(ALEEvent::HANDSHAKE_COMPLETE);
            }
            break;
        }
    }
}

void ALEStateMachine::handle_linked() {
    // ── TX-drain safety net (AC-LINK-023-adjacent) ──────────────────────────
    // terminate_link() and the orderwire burst both rely on on_word_complete()
    // draining words_pending to 0. If that never happens (audio stall or a
    // transmit_callback that doesn't arm the frame completion), the SM would
    // hang in LINKED with RX disabled — the linked_terminating_ short-circuit
    // below and the orderwire_transmitting_ return suppress the Twa timer, so
    // nothing else recovers it. Bound the wait: once tx_drain_deadline_ms_ has
    // elapsed since the drain was armed, force the transition (termination) or
    // abandon the burst (orderwire) and re-open RX. The orderwire arm site
    // below scales this deadline to the burst's own word count — AMD text can
    // legitimately run up to Tm_max = 59×Trw ≈ 23.1 s (A.5.7.2.3), far past the
    // flat TX_DRAIN_TIMEOUT_MS this safety net used to apply unconditionally.
    if ((linked_terminating_ || orderwire_transmitting_)
        && tx_drain_start_ms_ != 0
        && (current_time_ms - tx_drain_start_ms_) >= tx_drain_deadline_ms_) {
        if (linked_terminating_) {
            SM_TRACE("[TRACE] handle_linked: terminate drain timeout → LINK_TERMINATED (forced)\n");
            linked_terminating_       = false;
            tx_drain_start_ms_ = 0;
            if (rx_enabled_callback) rx_enabled_callback(true);
            process_event(ALEEvent::LINK_TERMINATED);
        } else {
            SM_TRACE("[TRACE] handle_linked: orderwire drain timeout → abandon burst\n");
            orderwire_transmitting_   = false;
            tx_drain_start_ms_ = 0;
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
        return;
    }

    if (linked_terminating_) return;

    // ── Peer TWAS termination-frame settle (T-03, A.5.5.3.5) ──────────────────
    // A TWAS conclusion prefix-matching active_call_to has been accumulating
    // (anchor + DATA/REP extensions, see ALECallProcessor LINKED branch). After
    // Tdrw of silence the sender's FULL address is settled — same window the
    // handshake uses for multi-word conclusions. Terminate ONLY on an exact
    // full-address match vs active_call_to; anything else (a foreign station
    // sharing the peer's first word, e.g. DC7XY vs peer DC7SU) is discarded.
    // The Tdrw delay on genuine termination is harmless — the peer is gone
    // either way. Same Trw-settle reasoning as handle_handshake(): a 1×Trw
    // settle would race the next on-grid extension word and truncate the
    // address, so the decision waits the full 2×Trw.
    if (!linked_twas_addr_.empty()
        && (current_time_ms - linked_twas_last_ms_) >= ALETimingConstants::Tdrw_ms) {
        if (linked_twas_addr_ == active_call_to) {
            SM_TRACE("[TRACE] handle_linked: peer TWAS termination frame (full address match) → LINK_TERMINATED\n");
            linked_twas_addr_.clear();
            process_event(ALEEvent::LINK_TERMINATED);
            return;
        }
        SM_TRACE("[TRACE] handle_linked: foreign TWAS (address mismatch) — discarded\n");
        linked_twas_addr_.clear();
    }

    // ── Linked Orderwire (Enhanced Frequency-Select, A.5.6.3.2) ─────────────
    // Sends pending_orderwire_words_ + TIS:SELF (×2) while LINKED so EFS
    // proposals/responses can be exchanged without terminating the link. The
    // words_pending==0 drain transition (RX re-enable + LINKED-AMD confirm
    // phase hand-off) happens synchronously in on_word_complete() the instant
    // the last word completes — the only place that ever observes
    // orderwire_transmitting_==true together with words_pending==0. By the
    // time this function next runs (the following update() tick),
    // on_word_complete() has already cleared orderwire_transmitting_, so a
    // duplicate check here could never fire; this early-return only
    // suppresses the idle-warning/Twa logic below while a burst is in flight.
    if (orderwire_transmitting_) return;
    if (orderwire_pending_) {
        orderwire_pending_      = false;
        orderwire_transmitting_ = true;
        if (rx_enabled_callback) rx_enabled_callback(false);
        // F-08 orderwire burst (OFS FR-09): content words + TIS:SELF conclusion,
        // grammar-validated and catalog-tagged by the builder. An empty return
        // (validation refused) transmits nothing — fail-safe, and the drain
        // deadline below stays disarmed since words_pending stays 0.
        const ALESequence burst = ALEFrameBuilder::orderwire_burst(
            std::move(pending_orderwire_words_),
            address_book.get_self_address(),
            orderwire_double_burst_);
        transmit_words(burst.words());
        // Arm the drain deadline (skipped harmlessly if nothing was enqueued —
        // the words_pending == 0 branch above clears it next tick). Scale the
        // deadline to the burst just queued (tx.size() == words_pending here,
        // since it was 0 before transmit_words()) instead of the flat
        // TX_DRAIN_TIMEOUT_MS — a full-length AMD/EFS burst can need up to
        // 59×Trw ≈ 23.1 s (A.5.7.2.3 Tm_max incl. AMD) to legitimately drain.
        if (words_pending > 0) {
            tx_drain_start_ms_    = current_time_ms;
            tx_drain_deadline_ms_ = std::max(
                ALETimingConstants::TX_DRAIN_TIMEOUT_MS,
                words_pending * ALETimingConstants::Trw_ms + 2u * ALETimingConstants::Trw_ms);
        }
        return;
    }

    // ── LINKED-state AMD retry pacing (Tt = TT_NEXT_TRY_MS) ──────────────────
    // linked_amd_retry_or_fail_() queues the resend here instead of firing
    // immediately — matches the not-linked path's inter-attempt gap rather
    // than hammering the channel with zero turnaround between bursts.
    if (linked_amd_retry_pending_) {
        if (current_time_ms >= linked_amd_retry_after_ms_) {
            linked_amd_retry_pending_ = false;
            pending_orderwire_words_  = linked_amd_burst_;
            orderwire_double_burst_   = false;
            orderwire_pending_        = true;
            last_word_time_ms         = current_time_ms;
        }
        return;
    }

    // ── LINKED-state AMD delivery confirmation (Call→Response→ACK) ───────────
    // Runs only between bursts (returns above while orderwire_transmitting_).
    // While a confirm is in flight, suppress the idle-warning/Twa logic below
    // — the exchange itself is link activity and drives its own bounded timeouts.
    if (linked_amd_phase_ == LinkedAmdPhase::LISTENING && linked_amd_listen_start_ms_ != 0) {
        handle_linked_amd_listening_();
        return;
    }
    if (linked_amd_phase_ == LinkedAmdPhase::WAIT_ACK) {
        handle_linked_amd_wait_ack_();
        return;
    }

    // ── Idle warning (Twa lead) ─────────────────────────────────────────────
    // Any link activity moves last_word_time_ms (ALE word RX, TX orderwire,
    // on_link_activity(), reset_link_idle_timer()). Detect that here and
    // re-arm the one-shot warning without touching every activity site.
    // Fires once, IDLE_WARNING_LEAD_MS before Twa elapses, so the GUI can offer a reset.
    if (last_word_time_ms != last_seen_word_time_ms_) {
        last_seen_word_time_ms_ = last_word_time_ms;
        idle_warning_sent_      = false;
    }
    if (!linked_terminating_ && !idle_warning_sent_ && idle_warning_cb_) {
        const uint32_t idle_ms = current_time_ms - last_word_time_ms;
        // Scale the lead time down for a short Twa instead of a flat
        // IDLE_WARNING_LEAD_MS: if the two are equal (e.g. an unconfigured/
        // degenerate Twa_ms == the 30s lead constant), warn_at would collapse
        // to 0 and the warning would fire the instant the link came up —
        // jarring, and indistinguishable from a real "about to time out"
        // state. Capping the lead at half of Twa_ms keeps the normal case
        // (Twa_ms=360s) unchanged (min(30s, 180s) = 30s) while still giving a
        // short-but-real grace window for any deliberately short Twa_ms.
        const uint32_t lead_ms = std::min(ALETimingConstants::IDLE_WARNING_LEAD_MS,
                                           timing_.Twa_ms / 2u);
        const uint32_t warn_at = (timing_.Twa_ms > lead_ms) ? (timing_.Twa_ms - lead_ms) : 0u;
        if (idle_ms >= warn_at) {
            idle_warning_sent_ = true;
            const uint32_t remaining_sec =
                (timing_.Twa_ms > idle_ms) ? ((timing_.Twa_ms - idle_ms) / 1000u) : 0u;
            idle_warning_cb_(remaining_sec);
        }
    }

    // ── Twa inactivity timeout (AC-LINK-023) ─────────────────────────────────
    // AC-LINK-023-6: send TWAS before transitioning so the peer can return
    // to available state immediately (T-07).
    if ((current_time_ms - last_word_time_ms) >= timing_.Twa_ms) {
        linked_terminating_ = true;
        if (!active_call_to.empty() && !address_book.get_self_address().empty())
            transmit_words(ALEFrameBuilder::termination(
                active_call_to, address_book.get_self_address()).words());
        // Transmits TO×2+TWAS, then transitions out immediately (process_event
        // leaves LINKED; the frame still goes out via the modulator). No
        // drain deadline needed here — handle_linked() won't run again once LINKED is left.
        process_event(ALEEvent::LINK_TIMEOUT);
    }
}

void ALEStateMachine::trigger_linked_orderwire(std::vector<ALEWord> words,
                                               bool double_burst) {
    check_thread_();
    if (current_state != ALEState::LINKED || linked_terminating_) return;
    pending_orderwire_words_ = std::move(words);
    orderwire_double_burst_  = double_burst;
    orderwire_pending_       = true;
    last_word_time_ms        = current_time_ms;  // reset Twa to prevent timeout during TX
}

// ── LINKED-state AMD delivery confirmation (Call→Response→ACK) ───────────────
// See LinkedAmdPhase. TX rides the existing orderwire path (queue content-
// only; TIS:SELF appended by handle_linked's orderwire block). Detection
// reuses the WordRole classifier (ALECallProcessor::react_linked_amd_confirm_);
// reply windows below are shared with CALLING/LISTENING and HANDSHAKE/WAIT_ACK.

uint32_t ALEStateMachine::listening_response_wait_ms_() {
    // handle_calling LISTENING(a): responder turnaround (Twrt_slow) + ~2×L latency
    // (Tdrw) + one settle-delta (Tdrw − Tlww). Total ≈ 3136 ms.
    return static_cast<uint32_t>(0.5 + ale::Twrt_slow_ms)
         + static_cast<uint32_t>(ale::Tdrw_ms)
         + (ALETimingConstants::Tdrw_ms - ALETimingConstants::Tlww_ms);
}

uint32_t ALEStateMachine::wait_ack_start_wait_ms_() {
    // handle_handshake WAIT_ACK(1): ACK turnaround (Twr_slow) + ~2×L latency (Tdrw)
    // + one settle-delta (Tdrw − Tlww). Total ≈ 2091 ms.
    return ale::Twr_slow_int
         + static_cast<uint32_t>(ale::Tdrw_ms)
         + (ALETimingConstants::Tdrw_ms - ALETimingConstants::Tlww_ms);
}

void ALEStateMachine::send_linked_amd(std::vector<ALEWord> content_words,
                                       const std::string& peer, uint32_t max_attempts) {
    check_thread_();
    if (current_state != ALEState::LINKED || linked_terminating_) return;
    linked_amd_burst_         = content_words;           // verbatim resend on retry
    linked_amd_peer_          = peer;
    linked_amd_attempts_left_ = std::max(1u, max_attempts);
    linked_amd_phase_         = LinkedAmdPhase::LISTENING;
    linked_amd_listen_start_ms_ = 0;                     // armed once the burst drains
    linked_amd_resp_detected_ = false;
    linked_amd_resp_tlww_ms_  = 0;
    // Ship the burst via the orderwire path (single, not doubled — AMD text).
    pending_orderwire_words_ = std::move(content_words);
    orderwire_double_burst_  = false;
    orderwire_pending_       = true;
    last_word_time_ms        = current_time_ms;
}

void ALEStateMachine::respond_to_linked_amd(const std::string& sender,
                                             std::vector<ALEWord> extra_cmd_words) {
    check_thread_();
    if (current_state != ALEState::LINKED || linked_terminating_) return;
    if (sender.empty()) return;
    // Response frame content: TO[sender] ×2 (leading_call) + optional CMD 'a';
    // TIS:SELF is appended by the orderwire path.
    std::vector<ALEWord> resp = ALESequenceBuilder::leading_call(sender).words();
    resp.insert(resp.end(), extra_cmd_words.begin(), extra_cmd_words.end());
    linked_amd_peer_          = sender;
    linked_amd_phase_         = LinkedAmdPhase::RESPONDING;
    pending_orderwire_words_  = std::move(resp);
    orderwire_double_burst_   = false;
    orderwire_pending_        = true;
    last_word_time_ms         = current_time_ms;
}

void ALEStateMachine::handle_linked_amd_listening_() {
    // Sender side. Mirrors handle_calling LISTENING(a)/(b)/(c), but a window
    // timeout retries the burst (or gives up) instead of hopping channels, and
    // a detected Response leads to the ACK frame instead of link establishment.
    if (!linked_amd_resp_detected_) {
        // (a) waiting for the peer's Response to *start* (TO-self).
        if ((current_time_ms - linked_amd_listen_start_ms_) >= listening_response_wait_ms_())
            linked_amd_retry_or_fail_();
    } else if (linked_amd_resp_tlww_ms_ == 0) {
        // (b) saw TO-self, waiting for the peer's TIS conclusion — 5×Trw silence.
        if ((current_time_ms - last_word_time_ms) >= 5u * ALETimingConstants::Trw_ms)
            linked_amd_retry_or_fail_();
    } else {
        // (c) saw TIS, let it settle (Tdrw), then send the ACK (frame 3):
        // TO[peer] ×2 (content-only, TIS:SELF appended by the orderwire path).
        if ((current_time_ms - linked_amd_resp_tlww_ms_) >= ALETimingConstants::Tdrw_ms) {
            linked_amd_phase_           = LinkedAmdPhase::SENDING_ACK;
            linked_amd_listen_start_ms_ = 0;
            pending_orderwire_words_    = ALESequenceBuilder::leading_call(linked_amd_peer_).words();
            orderwire_double_burst_     = false;
            orderwire_pending_          = true;
            last_word_time_ms           = current_time_ms;
        }
    }
}

void ALEStateMachine::linked_amd_retry_or_fail_() {
    if (linked_amd_attempts_left_ > 1) {
        --linked_amd_attempts_left_;
        // Defer the resend by Tt (TT_NEXT_TRY_MS) — same inter-attempt pacing
        // the not-linked path's retry already uses (amd_retry_recall_after_ms_
        // in ale_controller.cpp). Without this the burst went straight back
        // out the instant the listening window expired: correct attempt
        // COUNT, wrong protocol TIMING (hammering the channel with zero
        // turnaround, unlike every other retry path in this codebase).
        // handle_linked() fires the actual resend once linked_amd_retry_after_ms_ elapses.
        linked_amd_listen_start_ms_ = 0;
        linked_amd_resp_detected_   = false;
        linked_amd_resp_tlww_ms_    = 0;
        linked_amd_retry_pending_   = true;
        linked_amd_retry_after_ms_  = current_time_ms + ale::TT_NEXT_TRY_MS;
        last_word_time_ms           = current_time_ms;
        if (operator_callback) operator_callback(OperatorEvent::AMD_RETRY);
    } else {
        linked_amd_phase_           = LinkedAmdPhase::NONE;
        linked_amd_listen_start_ms_ = 0;
        if (operator_callback) operator_callback(OperatorEvent::AMD_NOT_DELIVERED);
    }
}

void ALEStateMachine::handle_linked_amd_wait_ack_() {
    // Receiver side. Mirrors handle_handshake WAIT_ACK(1)/(2)/(3); a timeout here
    // just ends the exchange silently — the AMD itself was already displayed, only
    // the wire-level acknowledgement of our Response went unseen (no retransmit).
    if (!linked_amd_ack_to_detected_) {
        // (1) ACK start (TO-self) must arrive within the Twr turnaround window.
        if ((current_time_ms - linked_amd_ack_start_ms_) >= wait_ack_start_wait_ms_())
            linked_amd_phase_ = LinkedAmdPhase::NONE;
    } else if (!linked_amd_ack_tis_rcvd_) {
        // (2) saw TO-self, waiting for the sender's TIS — 5×Trw silence.
        if ((current_time_ms - last_word_time_ms) >= 5u * ALETimingConstants::Trw_ms)
            linked_amd_phase_ = LinkedAmdPhase::NONE;
    } else if (linked_amd_ack_tlww_ms_ != 0
               && (current_time_ms - linked_amd_ack_tlww_ms_) >= ALETimingConstants::Tdrw_ms) {
        // (3) ACK conclusion settled → handshake closed on the receiver side.
        linked_amd_phase_ = LinkedAmdPhase::NONE;
    }
}

void ALEStateMachine::on_link_activity() {
    // §A.5.5.3.5.2: any user-layer traffic (voice, data, AMD) resets Twa.
    // ALE words already reset last_word_time_ms via process_received_word();
    // this covers activity the ALE layer cannot observe directly.
    if (current_state == ALEState::LINKED)
        last_word_time_ms = current_time_ms;
}

void ALEStateMachine::reset_link_idle_timer() {
    // Named GUI-facing operation: restart the full Twa countdown and re-arm the
    // idle warning.  No-op outside LINKED (the timer is only meaningful while
    // linked).  Equivalent to on_link_activity() plus flag cleanup; done here so
    // handle_linked()'s re-arm detection picks it up next tick.
    if (current_state != ALEState::LINKED) return;
    last_word_time_ms        = current_time_ms;
    idle_warning_sent_       = false;
    last_seen_word_time_ms_  = current_time_ms;
}

void ALEStateMachine::handle_sounding() {
    // ── LBT: listen Twt before any TX (AC-SOUND-001-001 / REQ-CHAN-031) ──
    // Duration per A.5.4.7.1 (784 ms ALE-only / ≥2 s shared, see
    // set_lbt_shared). Broadband occupancy busy (A.5.4.7.2) → abort, same as
    // the invalid-word path: per A.5.4.7 an aborted sound is rescheduled, not moved.
    if (sounding_phase_ == SoundingPhase::LBT) {
        if (lbt_channel_busy_()) {
            SM_TRACE("[TRACE] handle_sounding: channel occupied during LBT → SOUNDING_COMPLETE\n");
            process_event(ALEEvent::SOUNDING_COMPLETE);
            return;
        }
        if ((current_time_ms - sounding_lbt_start_ms_) >= effective_twt_ms_()) {
            sounding_phase_ = SoundingPhase::TRANSMITTING;
            if (!address_book.get_self_address().empty()) {
                if (rx_enabled_callback) rx_enabled_callback(false);
                // A.5.3.1/A.5.3.3: the (scanning) sound is the whole-address
                // conclusion repeated for Tsrs = Tss + Trs = (n + 2)·Ta, where
                // n is the number of scan channels the sound must cover
                // (Tss = n·Ta ≥ the receivers' scan period) and the trailing
                // +2 is the redundant sound Trs = 2·Ta.
                //
                // n is taken from target_scan_channels — the SAME "call width"
                // C the controller configures for calling (Tsc = C·2·Trw),
                // resolved per net from the active sounding/scan net's
                // calling_length_c (see ALEController::resolve_sounding_C).
                // This makes sounding use the same configurable scan-channel
                // count as calling, instead of the own scan-channel count
                // (arbitrary, not configurable). Burst length is therefore
                // (C+2) conclusions = (C+2)·Ta on air; for the default C=10
                // that is ~4.7 s — long enough to get through the radio's
                // PTT/TX ramp, unlike the 784 ms single-channel Trs=2 burst
                // that keyed PTT but transmitted no words on the live rig.
                // C=0 (no scan channels assumed) falls back to reps=2 (the
                // bare redundant sound Trs).
                const size_t n    = target_scan_channels;
                const size_t reps = (n > 0) ? (n + 2) : 2;
                // F-06 sound burst (OFS FR-09): catalog-built, grammar-validated
                // and tagged by the builder; the pending CMD NOISE word rides
                // along as its trailing fragment (AC-CHAN-004-002 / Block B3).
                uint32_t noise_raw = 0;
                if (pending_noise_cmd_set_) {
                    noise_raw              = pending_noise_cmd_raw_;
                    pending_noise_cmd_set_ = false;
                }
                const ALESequence burst = ALEFrameBuilder::sound(
                    address_book.get_self_address(), sounding_use_twas_,
                    static_cast<uint32_t>(reps), noise_raw);
                transmit_words(burst.words());
            } else {
                // No callsign → nothing to transmit → don't key PTT, skip channel.
                SM_TRACE("[TRACE] handle_sounding: no self-address — SOUNDING_COMPLETE\n");
                process_event(ALEEvent::SOUNDING_COMPLETE);
            }
        }
        return;
    }

    // Fallback: if all TX words are done but on_word_complete() never fired
    // (e.g. no address → no word sent), trigger the transition directly here.
    if (sounding_phase_ == SoundingPhase::TRANSMITTING && words_pending == 0) {
        sounding_phase_                = SoundingPhase::LISTENING;
        sounding_listening_start_ms_   = current_time_ms;  // anchor LISTENING window
        if (rx_enabled_callback) rx_enabled_callback(true);
        // No return: LISTENING timeout check follows directly below.
    }
    if (sounding_phase_ == SoundingPhase::TRANSMITTING) return;  // words still pending

    // LISTENING: wait Trw window for incoming call (A.5.3.4)
    // RX state is set by the enter_state of whatever comes next (LBT re-arm or
    // previous_state) — no explicit rx=false needed here.
    if ((current_time_ms - sounding_listening_start_ms_) > ALETimingConstants::Trw_ms)
        process_event(ALEEvent::SOUNDING_COMPLETE);
}

// ============================================================================
// Public API
// ============================================================================

void ALEStateMachine::configure_scan(const ScanConfig& config) {
    channel_manager_.configure(config);
}

void ALEStateMachine::add_scan_channel(const Channel& channel) {
    channel_manager_.add_channel(channel);
}

void ALEStateMachine::set_self_address(const std::string& address) {
    address_book.set_self_address(address);
}

const Channel* ALEStateMachine::get_current_channel() const {
    return channel_manager_.current();
}

bool ALEStateMachine::initiate_call(const std::string& target) {
    check_thread_();
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = target;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = false;
    calling_channel_index = 0;

    // Pre-compute all TX sequences via ALESequenceBuilder; the SM never
    // re-processes the address string after this point.
    // scanning_seq_: scan_channels×2 words (§A.5.2.5.1, first 3 chars only)
    // leading_seq_:  full TO address × 2 (Tlc = 2×Tc, §A.5.5.3.1)
    // conclusion_seq_: own TIS address, sent once (§A.5.2.3.2.2)
    scanning_seq_   = ALESequenceBuilder::scanning_call(target, target_scan_channels);
    leading_seq_    = ALESequenceBuilder::leading_call(target);
    conclusion_seq_ = ALESequenceBuilder::conclusion(address_book.get_self_address());

    // Snapshot AMD orderwire and release the pending slot so the user can
    // queue the next message immediately. active_message_ persists across
    // channel retries.
    active_message_  = pending_message;
    pending_message  = PendingMessage{};

    // Calling-frame AMD words (Ion2G-style, replaces the old ACK-frame
    // embedding): FROM[self] self-ID + AMD CMD/DATA/REP payload. Both empty
    // ALESequence{} unless active_message_.type==AMD — the ONLY gate that puts
    // extra words on the air, so a plain call (type==NONE) is byte-identical
    // to before this feature: enqueue_call_sequence_() and the LEADING_CALL
    // slot-count both simply append/count zero words below.
    const bool has_amd = (active_message_.type == PendingMessage::Type::AMD)
                       && !active_message_.content.empty();
    active_amd_from_seq_ = has_amd
        ? ALESequenceBuilder::from_id(address_book.get_self_address()) : ALESequence{};
    active_amd_words_ = has_amd
        ? ALESequence(encode_amd(active_message_.content)) : ALESequence{};

    // Snapshot CMD LQA and LQA report; both survive channel retries like active_message_.
    active_lqa_cmd_seq_ = pending_lqa_cmd_set_
        ? ALESequenceBuilder::lqa_cmd(pending_lqa_cmd_raw_) : ALESequence{};
    pending_lqa_cmd_set_ = false;

    active_lqa_report_seq_ = pending_lqa_report_set_
        ? pending_lqa_report_seq_ : ALESequence{};
    pending_lqa_report_set_ = false;

    if (!call_frame_is_legal_())
        return false;   // FR-09 grammar gate: refuse, nothing transmitted

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_net_call(const std::string& net_address) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = net_address;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = true;
    active_call_is_group  = false;
    calling_channel_index = 0;

    // Same sequence structure as individual call; net call protocol is
    // currently stubbed as NET_CALL_STUB — address classification only differs.
    scanning_seq_   = ALESequenceBuilder::scanning_call(net_address, target_scan_channels);
    leading_seq_    = ALESequenceBuilder::leading_call(net_address);
    conclusion_seq_ = ALESequenceBuilder::conclusion(address_book.get_self_address());

    if (!call_frame_is_legal_())
        return false;   // FR-09 grammar gate: refuse, nothing transmitted

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_group_call(const std::vector<std::string>& members) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    if (members.empty())
        return false;

    // active_call_to is only used for post-link termination/emergency frames;
    // not refreshed once a specific member's response is identified
    // (to_address/active_call_from track the actual responding peer during
    // the handshake, same as individual/net calls — see react_calling()).
    active_call_to        = members.front();
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = true;
    calling_channel_index = 0;

    group_scan_seq_ = ALESequenceBuilder::scanning_call_group(members, target_scan_channels);
    leading_seq_    = ALESequenceBuilder::leading_call_group(members);
    conclusion_seq_ = ALESequenceBuilder::conclusion(address_book.get_self_address());
    scanning_seq_   = group_scan_seq_;   // unified entry point for enqueue_call_sequence_()

    // Snapshot AMD orderwire and release the pending slot so the user can
    // queue the next message immediately. active_message_ persists across
    // channel retries.
    active_message_  = pending_message;
    pending_message  = PendingMessage{};

    // Calling-frame AMD words (Ion2G-style, replaces the old ACK-frame
    // embedding): FROM[self] self-ID + AMD CMD/DATA/REP payload. Both empty
    // ALESequence{} unless active_message_.type==AMD — the ONLY gate that puts
    // extra words on the air, so a plain group call (type==NONE) is
    // byte-identical to before this feature: enqueue_call_sequence_() and the
    // LEADING_CALL slot-count both simply append/count zero words below.
    const bool has_amd = (active_message_.type == PendingMessage::Type::AMD)
                       && !active_message_.content.empty();
    active_amd_from_seq_ = has_amd
        ? ALESequenceBuilder::from_id(address_book.get_self_address()) : ALESequence{};
    active_amd_words_ = has_amd
        ? ALESequence(encode_amd(active_message_.content)) : ALESequence{};

    // Snapshot CMD LQA and LQA report; both survive channel retries like active_message_.
    active_lqa_cmd_seq_ = pending_lqa_cmd_set_
        ? ALESequenceBuilder::lqa_cmd(pending_lqa_cmd_raw_) : ALESequence{};
    pending_lqa_cmd_set_ = false;

    active_lqa_report_seq_ = pending_lqa_report_set_
        ? pending_lqa_report_seq_ : ALESequence{};
    pending_lqa_report_set_ = false;

    if (!call_frame_is_legal_())
        return false;   // FR-09 grammar gate: refuse, nothing transmitted

    return process_event(ALEEvent::CALL_REQUEST);
}

// ── OFS FR-09: F_CALL build-time grammar validation ──────────────────────────
// The calling frame is rendered section-by-section (enqueue_call_sequence_)
// because the phase advance is word-count driven, so the complete frame is
// validated here as one concatenated list — exactly the words that will go
// on air — before CALL_REQUEST commits the SM to CALLING.
bool ALEStateMachine::call_frame_is_legal_() const {
    std::vector<ALEWord> full;
    full.reserve(scanning_seq_.size() + leading_seq_.size()
                 + active_amd_from_seq_.size() + active_lqa_cmd_seq_.size()
                 + active_lqa_report_seq_.size() + active_amd_words_.size()
                 + conclusion_seq_.size());
    auto append = [&full](const ALESequence& s) {
        full.insert(full.end(), s.words().begin(), s.words().end()); };
    append(scanning_seq_);   // group calls: unified entry point (== group_scan_seq_)
    append(leading_seq_);
    append(active_amd_from_seq_);
    append(active_lqa_cmd_seq_);
    append(active_lqa_report_seq_);
    append(active_amd_words_);
    append(conclusion_seq_);
    return !FrameValidator::validate_frame(FrameType::F_CALL, full).has_value();
}

bool ALEStateMachine::respond_to_call() {
    check_thread_();
    // The 3-way handshake auto-advances (WAIT_CYCLE_END → SLOT_WAIT →
    // CHANNEL_CHECK → SENDING_RESPONSE → WAIT_ACK → LINKED) on update(); there
    // is no manual "respond" step, and the manual-accept gate is a no-op (see
    // set_require_explicit_accept()). This method is retained only as a
    // bounded force-complete, and ONLY from a state where it's safe: WAIT_ACK,
    // i.e. our response frame was already transmitted and we're merely
    // waiting for the caller's ACK. From any earlier phase it's a no-op —
    // firing HANDSHAKE_COMPLETE there would declare LINKED with an
    // empty/partial caller identity and without sending any response, leaving
    // the peer to time out while we believed the link was up. See
    // accept_call()/reject_call() (also no-ops) — operator decision is
    // applied post-link by ALEController.
    if (current_state != ALEState::HANDSHAKE) return false;
    if (handshake_phase != HandshakePhase::WAIT_ACK) return false;
    if (caller_address.empty()) return false;
    process_event(ALEEvent::HANDSHAKE_COMPLETE);
    return true;
}

bool ALEStateMachine::reject_call() {
    // Manual accept no longer gates the handshake (the responder auto-completes
    // the 3-way handshake within Twr/Twrt). Operator reject is a POST-link
    // action handled by ALEController::reject_call() → terminate_link() (sends
    // TWAS and returns to AVAILABLE). This SM method is now a no-op.
    return false;
}

bool ALEStateMachine::accept_call() {
    // See reject_call(): the operator accept/reject decision is applied post-link
    // by ALEController. This SM method is now a no-op.
    return false;
}

void ALEStateMachine::set_require_explicit_accept(bool /*on*/, uint32_t /*decision_timeout_ms*/) {
    // Retained as a no-op for API/CLI + config-persistence compatibility. Manual
    // accept no longer pauses the protocol; the flag is intentionally ignored
    // here (see docs/GUI_BRIDGE_GAPS.md and the handle_handshake settle comment).
}

bool ALEStateMachine::send_sounding() {
    check_thread_();
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    return process_event(ALEEvent::SOUNDING_REQUEST);
}

bool ALEStateMachine::send_sounding_sweep(const std::vector<Channel>& channels) {
    check_thread_();
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    if (allcall_broadcasting_) return false;  // don't interleave with a draining broadcast
    if (channels.empty()) return false;
    sounding_sweep_chs_    = channels;
    sounding_sweep_idx_    = 0;
    sounding_sweep_active_ = true;
    // Pin current() to the first sweep channel and tune the radio to it.
    channel_manager_.set_override(channels[0]);
    return process_event(ALEEvent::SOUNDING_REQUEST);
}

bool ALEStateMachine::send_allcall_broadcast(const std::string& self_addr, const std::string& text,
                                              bool link_after_send) {
    check_thread_();
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING) return false;
    if (allcall_broadcasting_) return false;  // one in flight — see is_allcall_broadcasting()
    if (self_addr.empty()) return false;
    const auto amd = encode_amd(text);
    if (amd.empty()) return false;

    // A.5.2.4.7: the global AllCall address is the literal 3-char "@?@" —
    // already valid Basic-38 (A-Z, 0-9, @, ?), no length/charset special-case
    // needed anywhere else in the address path.
    // (The literal "@?@" AllCall address itself lives in the builder —
    // ALESequenceBuilder::allcall_broadcast(), OFS FR-09 catalog entry.)
    // Full standard calling routine (A.5.2.5.1 scanning call + A.5.5.3.1
    // leading call) — a scanning call section MUST precede the leading call,
    // sized to the same "call width" C the normal calling path uses
    // (Tsc = C×2×Trw, see target_scan_channels/initiate_call()/
    // ALEController::resolve_sounding_C()). Without it, a listening station
    // only catches the broadcast if its scan dwell already happens to be on
    // this channel at this instant — the scanning call lets a station mid-hop
    // elsewhere still lock on before the leading call starts.
    // F-07 AllCall broadcast frame (OFS FR-09): assembled, grammar-validated
    // and catalog-tagged by the builder (scanning @?@ + leading @?@ ×2 +
    // FROM[self] + AMD payload + TWAS/TIS conclusion). An empty return refuses
    // the broadcast (nothing on air).
    const ALESequence frame = ALEFrameBuilder::allcall_broadcast(
        self_addr, amd, link_after_send, target_scan_channels);
    if (frame.empty()) return false;   // grammar gate refused (FR-09) — nothing sent

    allcall_broadcasting_ = true;
    if (rx_enabled_callback) rx_enabled_callback(false);
    transmit_words(frame.words());
    // Arm the TX-drain safety net (see the ALLCALL branch in update(), right
    // after check_link_timeout()) — without it, an audio stall or a
    // transmit_callback that never arms on_word_complete() would leave RX
    // disabled forever, since this path stays in IDLE/SCANNING the whole
    // time (no state transition to fall back on). Scaled to the words just
    // queued, same formula the orderwire burst uses (words.size() ==
    // words_pending here, since transmit_words() just set it from 0) — the
    // scanning call alone can be C×2 words, easily past the flat
    // TX_DRAIN_TIMEOUT_MS for a large net.
    if (words_pending > 0) {
        tx_drain_start_ms_    = current_time_ms;
        tx_drain_deadline_ms_ = std::max(
            ALETimingConstants::TX_DRAIN_TIMEOUT_MS,
            words_pending * ALETimingConstants::Trw_ms + 2u * ALETimingConstants::Trw_ms);
    }
    return true;
}

void ALEStateMachine::terminate_link() {
    check_thread_();
    if (current_state != ALEState::LINKED) return;
    linked_terminating_ = true;
    // T-07: TO [peer] × 2 + TWAS [self] — peer returns to available state immediately
    transmit_words(ALEFrameBuilder::termination(
        active_call_to, address_book.get_self_address()).words());
    if (rx_enabled_callback) rx_enabled_callback(false);
    if (words_pending == 0) {
        // Empty termination frame (degenerate addresses) — nothing is on the
        // air, no on_word_complete will ever fire. Complete the transition
        // now rather than wait for the drain deadline.
        linked_terminating_ = false;
        process_event(ALEEvent::LINK_TERMINATED);
        return;
    }
    // Arm the TX-drain safety net: on_word_complete() normally fires
    // LINK_TERMINATED once the frame drains, but if the audio device stalls
    // or transmit_callback never armed the completion, handle_linked()
    // force-fires it after tx_drain_deadline_ms_ so the SM never hangs in
    // LINKED. The termination frame (TO×2+TWAS) is always short, so reset
    // the deadline to its default in case a prior orderwire burst left it scaled up.
    tx_drain_start_ms_    = current_time_ms;
    tx_drain_deadline_ms_ = ALETimingConstants::TX_DRAIN_TIMEOUT_MS;
}

void ALEStateMachine::emergency_manual_control() {
    check_thread_();
    emergency_active = true;
    if (operator_callback)
        operator_callback(OperatorEvent::EMERGENCY_ACTIVE);
    // AC-LINK-022-4: if linked, send TO×2+TWAS so the peer returns to available state.
    if (current_state == ALEState::LINKED && !linked_terminating_
        && !active_call_to.empty() && !address_book.get_self_address().empty()) {
        linked_terminating_ = true;
        transmit_words(ALEFrameBuilder::termination(
            active_call_to, address_book.get_self_address()).words());
    }
    // Abort any ongoing operation; transition_to(IDLE) is a no-op if already IDLE.
    transition_to(ALEState::IDLE);
}

// ============================================================================
// Received-word processing — delegate shims to ALECallProcessor
// ============================================================================
//
// All classification, per-state reactions, LQA update, and frame assembly
// live in ALECallProcessor (friend of this SM). These public methods stay as
// one-line forwards so the SM's existing API (tests, examples, controller) is
// unchanged while the SM itself contains no word-processing logic — only
// states + transitions (+ time evolution / TX).

void ALEStateMachine::process_received_word(const ALEWord& word) {
    check_thread_();
    ALECallProcessor::process_received_word(*this, word);
}

void ALEStateMachine::update_link_quality(const LinkQuality& lq) {
    ALECallProcessor::update_lqa(*this, lq);
}

void ALEStateMachine::set_lqa_metrics(LQAMetrics* m) {
    lqa_metrics_ = m;
}

void ALEStateMachine::set_frame_assembled_callback(std::function<void(const ALEMessage&)> cb) {
    frame_assembled_cb_ = std::move(cb);
}

const Channel* ALEStateMachine::select_best_channel() const {
    return channel_manager_.select_best();
}

// ============================================================================
// Timeout helpers
// ============================================================================

bool ALEStateMachine::check_link_timeout() {
    switch (current_state) {
        case ALEState::CALLING:
            return (current_time_ms - state_entry_time_ms) > compute_calling_timeout_ms();
        case ALEState::HANDSHAKE:
            return (current_time_ms - state_entry_time_ms) > timing_.Twa_ms;
        case ALEState::LINKED:
            // handle_linked() owns the spec-compliant Twa timeout (AC-LINK-023):
            // sends TWAS, then transitions out. Twa is user-configurable
            // (set_link_idle_timeout_sec → timing_.Twa_ms, default 360 s in
            // the GUI). This defensive net only matters if handle_linked() is
            // somehow not reached within its window — so it must never fire
            // *before* Twa. max(Twa, LINK_TIMEOUT_MS) guarantees that: when
            // Twa > 120 s the net is inert and handle_linked()'s TWAS
            // termination governs; when Twa < 120 s the net still backstops a stalled SM.
            return (current_time_ms - last_word_time_ms)
                   > std::max(timing_.Twa_ms, ALETimingConstants::LINK_TIMEOUT_MS);
        default:
            return false;
    }
}

uint32_t ALEStateMachine::compute_calling_timeout_ms() const {
    const ale::CallingBudgetParams p{
        calling_channels.empty() ? 1u : static_cast<uint32_t>(calling_channels.size()),
        target_scan_channels,
        static_cast<uint32_t>(leading_seq_.size()),
        static_cast<uint32_t>(conclusion_seq_.size()),
        // Calling-frame MESSAGE section: FROM self-ID + LQA CMD/report + AMD —
        // all re-sent on every channel retry, so budgeted like leading_seq_.
        // 0 for any call with nothing queued there (unaffected: same budget
        // as before this field existed).
        static_cast<uint32_t>(active_amd_from_seq_.size() + active_lqa_cmd_seq_.size()
                             + active_lqa_report_seq_.size() + active_amd_words_.size())
    };
    return ale::calc_calling_timeout_ms(p);
}

// ============================================================================
// Multi-channel retry — AC-LINK-017-8
// ============================================================================

void ALEStateMachine::try_next_calling_channel() {
    ++calling_channel_index;

    if (!calling_channels.empty()
        && calling_channel_index < calling_channels.size()) {
        // Hop to next channel and restart from LBT
        channel_manager_.hop_calling(calling_channels[calling_channel_index]);

        calling_phase        = CallingPhase::LBT;
        lbt_start_ms         = current_time_ms;
        tune_start_ms        = 0;
        first_call_tx_ms     = 0;
        call_cycle_count     = 0;
        call_cycles_in_phase = 0;
        words_pending        = 0;
        listening_start_ms   = 0;
        response_to_detected = false;
        response_rx_start_ms = 0;
        tlww_start_ms        = 0;
        to_address.clear();
        state_entry_time_ms  = current_time_ms; // reset global timeout per channel
    } else {
        // All channels exhausted — AC-LINK-017-8: notify operator, go IDLE
        if (operator_callback)
            operator_callback(OperatorEvent::NO_CHANNELS_LEFT);
        process_event(ALEEvent::LINK_TIMEOUT);
    }
}

// ============================================================================
// TX sequence builders
// ============================================================================
//
// All builders route through transmit_word()/transmit_words().
// transmit_word() is the single exit point: stamps the current timestamp and
// fires transmit_callback. No other code sets timestamp_ms on TX words.
//
// Calling path: the full sequence (scanning/leading/conclusion) is
// pre-computed in initiate_call() via ALESequenceBuilder and enqueued in one
// piece by enqueue_call_sequence_() at tune-complete.
//
// Receive path (ACK, response): remote address is not known at
// initiate_call() time, so ALESequenceBuilder is called at send time.

// AMD word building is delegated to encode_amd() in ale_orderwire_protocols.cpp
// (AC-GEN-014-002). Ion2G-style: AMD now rides the CALLING frame (frame 1),
// not the ACK frame — TO×2 + FROM[self] + [CMD 'a'] + [CMD 'r'+DATA...] +
// CMD-AMD + TIS[self]. The called station can identify the caller and read
// the message without waiting for a full 3-way handshake; frame 3
// (build_ack_words()) then becomes purely the caller's link/no-link decision (TIS vs TWAS).

void ALEStateMachine::enqueue_call_sequence_() {
    // scanning_seq_ and leading_seq_ are pre-built by initiate_call*():
    //   scanning_seq_  — scan_channels × 2 words (§A.5.2.5.1/§A.5.5.4.3)
    //   leading_seq_   — full address × 2 (Tlc = 2×Tc, §A.5.5.3.1)
    //   conclusion_seq_ — TIS self address (§A.5.2.3.2.2)
    // MESSAGE section (A.5.2.5.5/Table A-XIV): FROM[self] + CMD 'a' [+ LQA
    // report] + CMD-AMD between leading address words and the TIS conclusion.
    // active_amd_from_seq_/active_amd_words_/active_lqa_cmd_seq_/
    // active_lqa_report_seq_ are all snapshot at initiate_call() and survive
    // channel retries unchanged (re-emitted on every retry channel).
    // active_amd_from_seq_/active_amd_words_ are empty ALESequence{} for any
    // non-AMD call (see initiate_call()'s has_amd gate) — a plain call
    // therefore transmits exactly the same words as before this feature.

    transmit_words(scanning_seq_.words());
    transmit_words(leading_seq_.words());
    transmit_words(active_amd_from_seq_.words());
    transmit_words(active_lqa_cmd_seq_.words());
    transmit_words(active_lqa_report_seq_.words());
    transmit_words(active_amd_words_.words());
    transmit_words(conclusion_seq_.words());

    // Note: unlike SENDING_ACK/SENDING_RESPONSE/LINKED-termination, the
    // CALLING TX phases (SCANNING_CALL/LEADING_CALL/CONCLUSION) have no
    // per-frame tx_drain_start_ms_ stall watchdog — purely word-count driven
    // by on_word_complete() (see LEADING_CALL/CONCLUSION cases below). The
    // overall CALLING-state backstop is compute_calling_timeout_ms() (checked
    // every update() via check_link_timeout()), which includes the
    // MESSAGE-section word count (FROM + LQA + AMD) via
    // CallingBudgetParams::message_words — see initiate_call()/
    // compute_calling_timeout_ms() — so a long AMD calling frame (up to
    // Tm_max = 59×Trw ≈ 23.1 s, A.5.7.2.3) is budgeted for and won't
    // self-abort via a bogus LINK_TIMEOUT mid-transmission.
}

void ALEStateMachine::build_ack_words() {
    // Third handshake frame per §A.5.5.3.4/Figure A-31, Ion2G-style:
    //   TO [to_address] × 2 + TIS [self]  — link established (normal call, or
    //                                        AMD send with link_after_send=true)
    //   TO [to_address] × 2 + TWAS [self] — AMD sent with link_after_send=false:
    //                                        handshake concludes, no link persists
    // No message content here — LQA CMD/report and AMD (if any) were already
    // sent in the calling frame MESSAGE section (enqueue_call_sequence_()).
    // to_address is set during the LISTENING phase (process_received_word),
    // so it's encoded here at send time, not pre-computed.
    const bool no_link = (active_message_.type == PendingMessage::Type::AMD)
                       && !active_message_.link_after_send;
    transmit_words(ALEFrameBuilder::ack(
        to_address, address_book.get_self_address(), no_link).words());

    // Arm the TX-drain deadline: on_word_complete() normally fires
    // LINK_ESTABLISHED/AMD_DECLINED_LINK once the frame drains; if audio
    // stalls or the completion is never armed, handle_calling SENDING_ACK
    // force-aborts after tx_drain_deadline_ms_ instead of waiting the full
    // Twa backstop. This frame no longer carries AMD content (moved to the
    // calling frame, see enqueue_call_sequence_()), so the flat default always suffices.
    if (words_pending > 0) {
        tx_drain_start_ms_    = current_time_ms;
        tx_drain_deadline_ms_ = ALETimingConstants::TX_DRAIN_TIMEOUT_MS;
    }
}

void ALEStateMachine::build_response_words() {
    // Accept: TO [caller] × 2 + TIS [self] (§A.5.5.3.3/Figure A-30).
    // Reject: TWAS [self] (FEAT-FRAME-005/AC-FRAME-010-1).
    // caller_address is set during WAIT_CYCLE_END (process_received_word),
    // so it's encoded here at send time, not pre-computed.
    //
    // When a CMD LQA word (char 'a') is pending, insert it between the
    // TO×2 prefix and the TIS conclusion (plan §Block A2).
    ALESequence lqa_seq{};
    if (pending_lqa_cmd_set_) {
        lqa_seq = ALESequenceBuilder::lqa_cmd(pending_lqa_cmd_raw_);
        pending_lqa_cmd_set_ = false;
    }

    // Consume LQA report sequence if pending (set via set_pending_lqa_report_seq()).
    ALESequence report_seq{};
    if (pending_lqa_report_set_) {
        report_seq = pending_lqa_report_seq_;
        pending_lqa_report_set_ = false;
    }

    if (pending_reject_ || (lqa_seq.empty() && report_seq.empty())) {
        transmit_words(ALEFrameBuilder::response(
            caller_address, address_book.get_self_address(), pending_reject_).words());
    } else {
        // A.5.5.3.3/Fig A-30 base frame: TO caller×2 + TIS self [+ DATA/REP ext].
        // Optional message section (A.5.2.5.5/Fig A-14 + A.5.3.4 Tmmax):
        //   [CMD 'a'] [CMD 'r' + DATA...] inserted BEFORE the conclusion (TIS/TWAS).
        // → Full: TO caller×2 + [CMD 'a'] + [CMD 'r' + DATA...] + TIS self [+ DATA/REP ext]
        const auto base = ALEFrameBuilder::response(
            caller_address, address_book.get_self_address(), false);
        const auto& bw = base.words();
        // Find where the conclusion section begins (first TIS or TWAS word).
        size_t conc_start = bw.size();
        for (size_t i = 0; i < bw.size(); ++i) {
            if (bw[i].type == PreambleType::TIS || bw[i].type == PreambleType::TWAS) {
                conc_start = i;
                break;
            }
        }
        std::vector<ALEWord> words;
        for (size_t i = 0; i < conc_start; ++i)         words.push_back(bw[i]);
        for (const auto& w : lqa_seq.words())            words.push_back(w);
        for (const auto& w : report_seq.words())         words.push_back(w);
        for (size_t i = conc_start; i < bw.size(); ++i) words.push_back(bw[i]);
        transmit_words(words);
    }
    // Arm the TX-drain deadline: on_word_complete() normally fires WAIT_ACK/
    // the reject abort once the frame drains; if audio stalls or the
    // completion is never armed, handle_handshake SENDING_RESPONSE
    // force-aborts after tx_drain_deadline_ms_ instead of waiting the full
    // Twa backstop. This response frame never carries AMD, but can carry a
    // bilateral LQA CMD 'a'/'r' report (lqa_seq/report_seq above) — scale the
    // deadline to the words actually queued instead of the flat
    // TX_DRAIN_TIMEOUT_MS sized for the bare response, same as build_ack_words() does for AMD.
    if (words_pending > 0) {
        tx_drain_start_ms_    = current_time_ms;
        tx_drain_deadline_ms_ = std::max(
            ALETimingConstants::TX_DRAIN_TIMEOUT_MS,
            words_pending * ALETimingConstants::Trw_ms + 2u * ALETimingConstants::Trw_ms);
    }
}

void ALEStateMachine::transmit_word(const ALEWord& word) {
    // Single exit point for all transmitted words; stamps the transmission
    // timestamp so pre-computed words carry the correct time regardless of
    // when they were built.
    ALEWord w      = word;
    w.timestamp_ms = current_time_ms;
    ++words_pending;
    if (transmit_callback)
        transmit_callback(w);
}

void ALEStateMachine::transmit_words(const std::vector<ALEWord>& words) {
    for (const auto& w : words)
        transmit_word(w);
}

// ============================================================================
// on_word_complete — called by ALEController once per symbol frame rendered by the audio thread
// ============================================================================

void ALEStateMachine::on_word_complete() {
    check_thread_();
    // ── LINKED termination path (T-07) ────────────────────────────────────
    // terminate_link() sends TO×2 + TWAS; LINK_TERMINATED fires only once
    // all words are through.
    if (current_state == ALEState::LINKED && linked_terminating_) {
        // Decrement-first, then check for 0 (like SENDING_ACK): the LAST word
        // sent fires LINK_TERMINATED. The earlier "decrement-and-return"
        // pattern waited for an (N+1)th frame completion that never comes —
        // SAM stayed hung in LINKED as a result.
        if (words_pending > 0) --words_pending;
        if (words_pending == 0) {
            linked_terminating_       = false;
            tx_drain_start_ms_ = 0;   // disarm the drain safety net
            process_event(ALEEvent::LINK_TERMINATED);
        }
        return;
    }

    // ── LINKED orderwire path (AMD/EFS over an established link) ──────────
    // trigger_linked_orderwire() sends TO[peer] (+CMD…) + TIS:SELF while
    // LINKED (handle_linked()'s orderwire_transmitting_ branch). Without this
    // branch no case below matched LINKED, so words_pending never reached 0
    // and every orderwire burst fell through to handle_linked()'s
    // TX_DRAIN_TIMEOUT_MS (10 s) safety net before PTT was released and RX
    // re-enabled — a fixed 10 s TRX hang after every AMD/EFS send. Mirrors
    // the linked_terminating_ branch above: decrement-first, then check 0 so
    // the last word's completion reopens RX.
    if (current_state == ALEState::LINKED && orderwire_transmitting_) {
        if (words_pending > 0) --words_pending;
        if (words_pending == 0) {
            orderwire_transmitting_ = false;
            tx_drain_start_ms_      = 0;   // disarm the drain safety net
            if (rx_enabled_callback) rx_enabled_callback(true);
            // LINKED-AMD confirm hand-off: a burst just drained. Instead of
            // going idle, advance the confirm phase (the reply window only
            // starts NOW — after our own burst is off the air — never
            // mid-transmission). This MUST live here, not in handle_linked():
            // this decrement-and-check is the only place that ever sees
            // orderwire_transmitting_==true and words_pending==0 at once —
            // handle_linked() only runs on the next update() tick, by which
            // point orderwire_transmitting_ is already false, so a duplicate
            // hand-off there is unreachable dead code (the bug this replaces:
            // the LISTEN window never opened/WAIT_ACK never started/the ACK
            // was never queued, because nothing ever ran this switch).
            switch (linked_amd_phase_) {
                case LinkedAmdPhase::LISTENING:
                    // sender: AMD burst (or a retry) drained → open the LISTEN window
                    linked_amd_listen_start_ms_ = current_time_ms;
                    linked_amd_resp_detected_   = false;
                    linked_amd_resp_tlww_ms_    = 0;
                    last_word_time_ms           = current_time_ms;  // hold off Twa
                    break;
                case LinkedAmdPhase::RESPONDING:
                    // receiver: Response drained → wait for the sender's ACK
                    linked_amd_phase_           = LinkedAmdPhase::WAIT_ACK;
                    linked_amd_ack_start_ms_    = current_time_ms;
                    linked_amd_ack_to_detected_ = false;
                    linked_amd_ack_tis_rcvd_    = false;
                    linked_amd_ack_tlww_ms_     = 0;
                    last_word_time_ms           = current_time_ms;
                    break;
                case LinkedAmdPhase::SENDING_ACK:
                    // sender: ACK drained → the 3-frame handshake is complete
                    linked_amd_phase_ = LinkedAmdPhase::NONE;
                    if (operator_callback) operator_callback(OperatorEvent::AMD_DELIVERED);
                    break;
                default: break;
            }
        }
        return;
    }

    // ── SOUNDING path (T-05 + T-08) ──────────────────────────────────────
    // Decrement-first, then check for 0 (like LINKED termination/SENDING_ACK):
    // the LAST word sent opens the RX window. The zero-word case (empty
    // self_address → no word, no on_word_complete) is still covered by the
    // fallback in handle_sounding().
    if (current_state == ALEState::SOUNDING) {
        if (words_pending > 0) --words_pending;
        // All words sent — open RX window (A.5.3.4)
        if (words_pending == 0 && sounding_phase_ == SoundingPhase::TRANSMITTING) {
            sounding_phase_                = SoundingPhase::LISTENING;
            sounding_listening_start_ms_    = current_time_ms;  // restart window timer
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
        return;
    }

    // ── ALLCALL broadcast path (send_allcall_broadcast) ──────────────────
    // Flag-gated not state-gated: the broadcast deliberately stays in
    // IDLE/SCANNING rather than a dedicated state (see
    // is_allcall_broadcasting()), so this must be checked ahead of the
    // CALLING/HANDSHAKE state branches.
    if (allcall_broadcasting_) {
        if (words_pending > 0) --words_pending;
        if (words_pending == 0) {
            allcall_broadcasting_ = false;
            tx_drain_start_ms_    = 0;  // disarm the drain safety net (update())
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
        return;
    }

    // ── CALLING path (SAM side) ───────────────────────────────────────────
    if (current_state == ALEState::CALLING) {
        // Every frame completion must correspond to a word we actually queued
        // (transmit_word incremented words_pending). A completion with no
        // pending word is a spurious/double-fired frame-complete — ignore it
        // rather than underflowing words_pending to UINT32_MAX (which would
        // make every ==0 completion check below permanently false and stall
        // the phase machine) or advancing call_cycles_in_phase on nothing
        // (premature phase transition). In normal operation words_pending > 0
        // for every CALLING completion; the last word transitions to
        // LISTENING/LINKED and leaves CALLING before any further completion.
        if (words_pending == 0) {
            SM_TRACE("[TRACE] on_word_complete: spurious CALLING completion (no pending word) — ignored\n");
            return;
        }
        --words_pending;
        ++call_cycle_count;
        ++call_cycles_in_phase;

        switch (calling_phase) {

            case CallingPhase::SCANNING_CALL: {
                const uint32_t tsc_slots = target_scan_channels * 2u;
                if (call_cycles_in_phase >= tsc_slots) {
                    SM_TRACE("[TRACE] on_word_complete: SCANNING_CALL → LEADING_CALL (tsc_slots="
                             + std::to_string(tsc_slots) + ")\n");
                    calling_phase        = CallingPhase::LEADING_CALL;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::GROUP_SCANNING_CALL: {
                // group_scan_seq_ is pre-built with all words (scan_channels×2 THRU+REP pairs).
                const uint32_t tsc_total = static_cast<uint32_t>(group_scan_seq_.size());
                if (call_cycles_in_phase >= tsc_total) {
                    SM_TRACE("[TRACE] on_word_complete: GROUP_SCANNING_CALL → LEADING_CALL\n");
                    calling_phase        = CallingPhase::LEADING_CALL;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::LEADING_CALL: {
                // leading_seq_ is pre-doubled (Tlc = 2×Tc, §A.5.5.3.1); its size
                // equals 2×wpa. The MESSAGE section (FROM self-ID + CMD 'a' +
                // LQA report + AMD) is emitted immediately after leading_seq_
                // in enqueue_call_sequence_(), so those words are counted here
                // before transitioning to CONCLUSION. active_amd_from_seq_/
                // active_amd_words_ are empty for any non-AMD call, so
                // tlc_slots is unchanged for a plain call — see the invariant
                // callout in enqueue_call_sequence_().
                const uint32_t tlc_slots = static_cast<uint32_t>(leading_seq_.size())
                                         + static_cast<uint32_t>(active_amd_from_seq_.size())
                                         + static_cast<uint32_t>(active_lqa_cmd_seq_.size())
                                         + static_cast<uint32_t>(active_lqa_report_seq_.size())
                                         + static_cast<uint32_t>(active_amd_words_.size());
                if (call_cycles_in_phase >= tlc_slots) {
                    SM_TRACE("[TRACE] on_word_complete: LEADING_CALL → CONCLUSION (tlc_slots="
                             + std::to_string(tlc_slots) + ")\n");
                    calling_phase        = CallingPhase::CONCLUSION;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::CONCLUSION: {
                const uint32_t conclusion_slots =
                    static_cast<uint32_t>(conclusion_seq_.size());
                if (call_cycles_in_phase >= conclusion_slots) {
                    SM_TRACE("[TRACE] on_word_complete: CONCLUSION → LISTENING\n");
                    calling_phase        = CallingPhase::LISTENING;
                    listening_start_ms   = current_time_ms;
                    call_cycles_in_phase = 0;
                    if (rx_enabled_callback)
                        rx_enabled_callback(true);
                }
                break;
            }

            case CallingPhase::SENDING_ACK: {
                // ACK frame (Figure A-31): TO [to_address] × 2 + TIS/TWAS [self_address].
                // Completion tied to the actual TX queue draining (words_pending
                // == 0) rather than a re-derived slot count, so any address
                // length works. no_link mirrors build_ack_words()'s derivation:
                // an AMD send with link_after_send=false concluded frame 3
                // with TWAS instead of TIS — graceful "handshake done, no
                // link wanted", not a real link.
                if (words_pending == 0) {
                    const bool no_link = (active_message_.type == PendingMessage::Type::AMD)
                                       && !active_message_.link_after_send;
                    if (no_link) {
                        SM_TRACE("[TRACE] on_word_complete: SENDING_ACK → AMD_DECLINED_LINK\n");
                        if (operator_callback)
                            operator_callback(OperatorEvent::AMD_SENT_NO_LINK);
                        process_event(ALEEvent::AMD_DECLINED_LINK);
                    } else {
                        SM_TRACE("[TRACE] on_word_complete: SENDING_ACK → LINKED\n");
                        if (operator_callback)
                            operator_callback(OperatorEvent::LINK_ESTABLISHED);
                        process_event(ALEEvent::HANDSHAKE_COMPLETE);
                    }
                }
                break;
            }

            default:
                break;
        }
        return;
    }

    // ── HANDSHAKE / SENDING_RESPONSE path (JOE side) ─────────────────────
    // Completion is driven by the TX queue draining (words_pending == 0), NOT
    // by a re-derived slot count. build_response_words() may insert CMD-LQA +
    // LQA-report words between the TO×2 prefix and the TIS conclusion; any
    // slot-count computed independently of the actually-transmitted sequence
    // (the old AddressEncoder::encode() formula) diverged from the real word
    // count, so the phase advanced to WAIT_ACK before the inserted words'
    // completions fired and words_pending leaked — a later terminate_link()
    // then never reached 0 and the SM hung in LINKED with RX disabled.
    // words_pending is the single source of truth for "all response words
    // have been rendered" (same pattern as SENDING_ACK above).
    // hs_words_in_phase is kept only as a diagnostic counter.
    if (current_state == ALEState::HANDSHAKE &&
        handshake_phase == HandshakePhase::SENDING_RESPONSE) {
        // A completion with no queued word is spurious — ignore it (same
        // guard as the CALLING path above) so it cannot underflow
        // words_pending or falsely fire the WAIT_ACK/reject transition on nothing.
        if (words_pending == 0) {
            SM_TRACE("[TRACE] on_word_complete: spurious SENDING_RESPONSE completion — ignored\n");
            return;
        }
        --words_pending;
        ++hs_words_in_phase;

        if (words_pending == 0) {
            if (pending_reject_) {
                // Rejection sent — SAM expects no ACK; return to SCANNING.
                SM_TRACE("[TRACE] on_word_complete: SENDING_RESPONSE (TWAS reject) → pre_link_state\n");
                pending_reject_ = false;
                process_event(ALEEvent::LINK_TIMEOUT);
            } else {
                SM_TRACE("[TRACE] on_word_complete: SENDING_RESPONSE → WAIT_ACK\n");
                handshake_phase  = HandshakePhase::WAIT_ACK;
                hs_ack_start_ms  = current_time_ms;
                hs_ack_to_ms     = 0;
                hs_tlww_start_ms = 0;
                hs_ack_tis_rcvd  = false;
                if (rx_enabled_callback) rx_enabled_callback(true);
            }
        }
    }
}

} // namespace ale
