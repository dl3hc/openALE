/**
 * \file ale_state_machine.cpp
 * \brief Implementation of ALE state machine
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
#include "LQA/lqa_metrics.h"
#include <algorithm>
#include <cstring>
#include <string>

// Forward protocol-level debug events to the injected trace callback.
// Expands to a no-op (zero overhead) when no callback is set.
// The msg expression is only evaluated when trace_cb_ is non-null.
#define SM_TRACE(msg) do { if (trace_cb_) trace_cb_(msg); } while(0)

namespace ale {

static const char* STATE_NAMES[] = {
    "IDLE", "SCANNING", "CALLING", "HANDSHAKE", "LINKED", "SOUNDING", "ERROR"
};

static const char* EVENT_NAMES[] = {
    "START_SCAN", "STOP_SCAN", "CALL_REQUEST", "CALL_DETECTED",
    "HANDSHAKE_COMPLETE", "LINK_TIMEOUT", "LINK_TERMINATED",
    "SOUNDING_REQUEST", "SOUNDING_COMPLETE", "ERROR_OCCURRED"
};

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
      sounding_phase_(SoundingPhase::TRANSMITTING),
      slot_number_(0),
      tswt_ms_(0),
      slot_wait_start_ms_(0),
      scanning_phase_(ScanningPhase::HOPPING),
      allcall_pause_start_ms_(0)
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
    return i > 9 ? "UNKNOWN" : EVENT_NAMES[i];
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
            break;

        case ALEState::HANDSHAKE:
            if (event == ALEEvent::HANDSHAKE_COMPLETE) return transition_to(ALEState::LINKED);
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(pre_link_state_);  // T-01
            break;

        case ALEState::LINKED:
            if (event == ALEEvent::LINK_TERMINATED ||
                event == ALEEvent::LINK_TIMEOUT)       return transition_to(pre_link_state_);  // T-01
            break;

        case ALEState::SOUNDING:
            if (event == ALEEvent::SOUNDING_COMPLETE)  return transition_to(ALEState::SCANNING);
            if (event == ALEEvent::CALL_DETECTED)      return transition_to(ALEState::HANDSHAKE);  // T-08
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
    this->current_time_ms = now_ms;

    if (check_link_timeout()) {
        process_event(ALEEvent::LINK_TIMEOUT);
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
    switch (new_state) {
        case ALEState::IDLE:
            // Available state: RX on, fixed channel, wait for incoming calls.
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::SCANNING:
            scanning_phase_          = ScanningPhase::HOPPING;
            allcall_pause_start_ms_  = 0;
            channel_manager_.start(current_time_ms);
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::CALLING:
            pre_link_state_                = previous_state;  // T-01: IDLE oder SCANNING
            link_start_time_ms             = current_time_ms;
            // first_call_tx_ms: informational TX-sequence start; set at end of
            // TUNING when the radio is tuned and ready (AC-LINK-017-2).
            first_call_tx_ms               = 0;
            lbt_start_ms                   = current_time_ms;
            tune_start_ms                  = 0;
            call_cycle_count               = 0;
            call_cycles_in_phase           = 0;
            words_pending                  = 0;
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
            pre_link_state_       = previous_state;  // T-01: Rückkehr in Zustand vor HANDSHAKE
            slot_wait_start_ms_   = 0;
            link_start_time_ms    = current_time_ms;
            // ── Handshake sub-state init (Fix 2/3/4) ─────────────────────
            twce_start_ms       = current_time_ms;
            hs_tlww_start_ms    = 0;
            hs_conclusion_rcvd  = false;
            caller_address.clear();
            hs_words_in_phase   = 0;
            hs_ack_start_ms     = 0;
            hs_ack_to_ms        = 0;
            hs_ack_tis_rcvd     = false;
            contiguous_errors   = 0;
            words_pending       = 0;
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
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::SOUNDING:
            sounding_phase_ = SoundingPhase::TRANSMITTING;
            if (rx_enabled_callback) rx_enabled_callback(false);
            if (!address_book.get_self_address().empty())
                transmit_words(
                    ALESequenceBuilder::conclusion(address_book.get_self_address()).words());
            break;

        default:
            break;
    }
}

void ALEStateMachine::exit_state(ALEState old_state) {
    switch (old_state) {
        case ALEState::CALLING:
            if (rx_enabled_callback)
                rx_enabled_callback(false);
            break;

        case ALEState::HANDSHAKE:
            // Ensure RX/TX state is clean regardless of which sub-phase we exit from.
            if (rx_enabled_callback)
                rx_enabled_callback(false);
            break;

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
    if (scanning_phase_ == ScanningPhase::ALLCALL_PAUSE) {
        // T-10: Tcc_max Timeout (A.5.5.4.4) — named constant aus ale_timing.h
        if ((current_time_ms - allcall_pause_start_ms_) > ALETimingConstants::Tcc_max_ms)
            scanning_phase_ = ScanningPhase::HOPPING;
        return;  // kein Hop während AllCall-Pause
    }
    if (channel_manager_.check_dwell_timeout(current_time_ms))
        channel_manager_.hop_next(current_time_ms);
}

/**
 * handle_calling — Individual call protocol per MIL-STD-188-141B A.5.5.3.1
 *
 * Phase sequence (Figure A-29):
 *
 *  LBT:           listen Twt (784 ms) — AC-LINK-017-1            [protocol time]
 *  TUNING:        tune Tt (1045 ms) — AC-LINK-017-2              [protocol time]
 *                   → at tune-complete the COMPLETE deterministic TX sequence
 *                     (scanning + leading + conclusion) is enqueued back-to-back
 *                     via enqueue_call_sequence_().
 *  SCANNING_CALL / GROUP_SCANNING_CALL / LEADING_CALL / CONCLUSION:
 *                 passive — the audio layer consumes the queued symbol frames
 *                 gap-free; each physically consumed frame fires
 *                 on_word_complete(), which advances counters and phases.
 *                 The Trw grid is a property of the sample stream itself
 *                 (one word = 49 symbols × 8 ms = 392 ms), never of wall time.
 *  LISTENING:     wait Twr/Twrt for JOE's response               [protocol time]
 *                   "TO SAM" → arm response tracking; "TIS JOE" → arm Tlww
 *                   Tlww elapsed → SENDING_ACK
 *  SENDING_ACK:   TO JOE × 2 + TIS SAM (REQ-LINK-008), enqueued as one frame
 *                   complete → HANDSHAKE_COMPLETE → LINKED
 *
 * Responsibility of this function: protocol-time windows (LBT, TUNING,
 * LISTENING) and starting RX-dependent TX (SENDING_ACK).  TX progress is
 * never derived from time here — frame-completion events are the only TX
 * clock (DD-009/DD-013; signal time vs. protocol time separation).
 */
void ALEStateMachine::handle_calling() {
    switch (calling_phase) {

        // ── LBT ──────────────────────────────────────────────────────────────────
        // Listen Twt (784 ms ALE-only) before first TX — AC-LINK-017-1.
        // RX is open; timer runs regardless of channel activity.
        case CallingPhase::LBT: {
            if ((current_time_ms - lbt_start_ms) >= ALETimingConstants::Twt_ms) {
                calling_phase = CallingPhase::TUNING;
                tune_start_ms = current_time_ms;
                if (rx_enabled_callback) rx_enabled_callback(false);  // blind tune
            }
            break;
        }

        // ── TUNING ───────────────────────────────────────────────────────────────
        // Blind tune Tt (1045 ms default; per-instance override via
        // set_timing_parameters(), see TimingParameters) — AC-LINK-017-2.
        // At tune-complete the full TX sequence is handed to the modem so the
        // audio layer can render it as one contiguous transmission.
        case CallingPhase::TUNING: {
            if ((current_time_ms - tune_start_ms) >= timing_.Tt_ms) {
                first_call_tx_ms     = current_time_ms;
                call_cycles_in_phase = 0;
                // T-06: Net calls laufen über denselben SAM-Pfad wie Individual Calls
                // T-11: Group calls nutzen GROUP_SCANNING_CALL (THRU/REP statt TO)
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
        // them without gaps.  Phase transitions happen in on_word_complete().
        // MESSAGE phase: AMD orderwire words built from active_message_ in
        // enqueue_call_sequence_() at tune-complete (AC-LINK-009-3 / A.5.7.2).
        case CallingPhase::SCANNING_CALL:
        case CallingPhase::GROUP_SCANNING_CALL:
        case CallingPhase::LEADING_CALL:
        case CallingPhase::MESSAGE:
        case CallingPhase::CONCLUSION:
            break;

        // ── LISTENING ─────────────────────────────────────────────────────
        // Three distinct sub-phases driven by response detection:
        //
        // (a) !response_to_detected:
        //     Waiting for JOE's first "TO SAM" word.  This is a TX→RX window:
        //     it spans from SAM's own conclusion (a local TX event) to JOE's
        //     first received word, so it accumulates the full round-trip audio
        //     latency 2×L on top of JOE's protocol turnaround.
        //
        //     JOE's turnaround after SAM's conclusion ends (T0):
        //       Tlww (392, post-conclusion wait)
        //       + Trw  (392, JOE's CHANNEL_CHECK / LBT)
        //       + Trw  (392, JOE transmits its first response word)
        //       → SAM's pipeline recognises it at  T0 + 1176 + 2×L.
        //     Twrt_slow (1960) covers the 1176 turnaround with margin; +Tdrw
        //     (784) absorbs ~390 ms/direction of WASAPI + virtual-cable latency.
        //     Identical for single- and multi-channel: the per-channel responder
        //     turnaround does not depend on the caller's channel count.
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
                const uint32_t wait_ms =
                      static_cast<uint32_t>(0.5 + ale::Twrt_slow_ms)  // 1960 ms turnaround+tune
                    + static_cast<uint32_t>(ale::Tdrw_ms)            // + 784 ms ≈ 2×L latency
                    + (ALETimingConstants::Tdrw_ms
                       - ALETimingConstants::Tlww_ms);               // + 392 ms: JOE settles SAM's
                // conclusion over Tdrw (not Tlww) before responding, delaying its response start by
                // one settle-delta (mirrors the WAIT_CYCLE_END settle change).  Total = 3136 ms.
                if ((current_time_ms - listening_start_ms) >= wait_ms)
                    try_next_calling_channel(); // AC-LINK-019-6
            } else if (tlww_start_ms == 0) {
                // (b) — waiting for TIS JOE conclusion.  Measured as silence
                // since the last received word, not from the first "TO SAM":
                // a multi-word own address makes JOE repeat the doubled
                // "TO SAM…" block for many Trw before its TIS, which a
                // from-first-word budget would cut off (same defect as the
                // WAIT_CYCLE_END / WAIT_ACK conclusion windows).
                if ((current_time_ms - last_word_time_ms)
                        >= 5u * ALETimingConstants::Trw_ms)
                    try_next_calling_channel(); // AC-LINK-019-8
            } else {
                // (c) — let JOE's conclusion settle.  tlww_start_ms is re-armed by
                // every DATA/REP extension word; the Tdrw (2×Trw) window must
                // exceed one on-grid word period so a multi-word JOE address
                // (TIS+DATA+REP…) is fully collected into to_address before the
                // ACK is built (else SAM would ACK a truncated address).
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
        // Third handshake frame: TO JOE [DATA]* TIS SAM [DATA]*.
        // Transition to LINKED via on_word_complete() after all words sent.
        case CallingPhase::SENDING_ACK: {
            if (words_pending > 0) break;
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
            // Measured as SILENCE since the last received word, not as wall time
            // since HANDSHAKE entry.  The calling cycle's length is set by the
            // CALLER (scanning section length + leading call = full address sent
            // twice), so a multi-word address (TO+DATA+REP+…, up to 5 words → 10
            // words doubled) legitimately pushes the conclusion (TIS) several Trw
            // past entry.  Anchoring Twce at entry aborted such calls before the
            // conclusion arrived — 3-char calls linked, longer ones did not.
            // Each received word re-anchors last_word_time_ms; an undecodable
            // sequence is still caught by contiguous_errors (handle_invalid_word).
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
            // It uses Tdrw (2×Trw), not Tlww (1×Trw): a multi-word caller address
            // (TIS+DATA+REP…) sends extension words one Trw apart, and a 1×Trw
            // settle races with the next on-grid word — the phase advanced before
            // the extension was appended, truncating the caller address.
            if (hs_conclusion_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tdrw_ms) {
                if (require_explicit_accept_) {
                    SM_TRACE("[TRACE] handle_handshake: conclusion settle → AWAIT_ACCEPT\n");
                    handshake_phase        = HandshakePhase::AWAIT_ACCEPT;
                    accept_decided_        = false;
                    await_accept_start_ms_ = current_time_ms;
                } else {
                    SM_TRACE("[TRACE] handle_handshake: conclusion settle → SLOT_WAIT\n");
                    handshake_phase     = HandshakePhase::SLOT_WAIT;
                    slot_wait_start_ms_ = current_time_ms;
                }
                // RX bleibt offen während Slot-Wait / AWAIT_ACCEPT
            }
            break;
        }

        // ── AWAIT_ACCEPT ──────────────────────────────────────────────────
        // Operator decision gate (only entered when manual-accept mode is on).
        // Holds here until accept_call()/reject_call() resolves accept_decided_,
        // or the decision timeout elapses (auto-accept fallback).
        case HandshakePhase::AWAIT_ACCEPT: {
            const bool timed_out =
                (current_time_ms - await_accept_start_ms_) >= accept_decision_timeout_ms_;
            if (accept_decided_ || timed_out) {
                if (timed_out && !accept_decided_)
                    SM_TRACE("[TRACE] handle_handshake: AWAIT_ACCEPT timeout → auto-accept\n");
                else
                    SM_TRACE("[TRACE] handle_handshake: AWAIT_ACCEPT resolved → SLOT_WAIT\n");
                handshake_phase     = HandshakePhase::SLOT_WAIT;
                slot_wait_start_ms_ = current_time_ms;
            }
            break;
        }

        // ── SLOT_WAIT ─────────────────────────────────────────────────────
        // T-09: warte tswt_ms_ (0 bei Individual Call) bevor LBT beginnt.
        case HandshakePhase::SLOT_WAIT: {
            if ((current_time_ms - slot_wait_start_ms_) >= tswt_ms_) {
                SM_TRACE("[TRACE] handle_handshake: SLOT_WAIT elapsed → CHANNEL_CHECK\n");
                handshake_phase = HandshakePhase::CHANNEL_CHECK;
                hs_lbt_start_ms = current_time_ms;
            }
            break;
        }

        // ── CHANNEL_CHECK ─────────────────────────────────────────────────
        // Listen-Before-Transmit: 1×Trw per A.5.5.3.3 / AC-LINK-019-1.
        // The calling station just finished TX; channel is clear by protocol.
        // One Trw window detects any collision from a third station.
        // Any word received here signals channel busy → abort (AC-LINK-019-3).
        // process_received_word() handles the busy-detection path.
        case HandshakePhase::CHANNEL_CHECK: {
            if ((current_time_ms - hs_lbt_start_ms) >= 1u * ALETimingConstants::Trw_ms) {
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
        // transitions to WAIT_ACK when all words have been sent.
        case HandshakePhase::SENDING_RESPONSE:
            break;

        // ── WAIT_ACK ─────────────────────────────────────────────────────
        // Wait for SAM's ACK frame (TO JOE × 2 + TIS SAM — Figure A-31).
        // Per A.5.5.3.4 + NOTE 1 this has two sub-phases, mirroring SAM's
        // LISTENING(a)/(b)/(c).  All windows are measured from JOE's response
        // end (Rb = hs_ack_start_ms), a TX→RX boundary that carries the full
        // round-trip audio latency 2×L on top of the protocol path.
        //
        // (1) hs_ack_to_ms == 0 — "TO JOE" must *start* within Twr (the narrow
        //     window of NOTE 1 that prevents the ACK being mistaken for a new
        //     call → ping-pong).  SAM's ACK turnaround, measured from Rb:
        //       SAM detects JOE conclusion ≈ Rb + L
        //       + Tlww (392) → SAM starts ACK
        //       + Trw  (392) first "TO JOE" word on air
        //       + L + Trw (SW-pipeline decode) → JOE recognises ≈ Rb + 784 + 2L
        //     SAM's ACK path has no CHANNEL_CHECK (unlike JOE's response), so
        //     Twr_slow + Tdrw = 1699 ms suffices (covers ~450 ms/dir latency).
        //     Timeout → abort; JOE returns to its pre-link state and a genuinely
        //     late "TO JOE" is then handled as a fresh call (NOTE 1).
        //
        // (2) hs_ack_to_ms > 0 — "TO JOE" seen; wait for the "TIS SAM"
        //     conclusion, now governed by the frame limit (5×Trw), not Twr.
        //
        // (3) hs_ack_tis_rcvd — Tlww settle → LINKED.
        case HandshakePhase::WAIT_ACK: {
            if (hs_ack_to_ms == 0) {
                // (1) — ACK start must arrive within this turnaround window.
                const uint32_t wait_ms =
                      ale::Twr_slow_int                              //  915 ms protocol turnaround
                    + static_cast<uint32_t>(ale::Tdrw_ms)           // + 784 ms ≈ 2×L round-trip latency
                    + (ALETimingConstants::Tdrw_ms
                       - ALETimingConstants::Tlww_ms);              // + 392 ms: SAM settles its
                // conclusion over Tdrw (not Tlww) before starting the ACK, so a multi-word JOE
                // address pushes the ACK start one settle-delta later (mirrors the LISTENING(c)
                // / WAIT_CYCLE_END settle change).  Total = 2091 ms.
                if ((current_time_ms - hs_ack_start_ms) >= wait_ms) {
                    SM_TRACE("[TRACE] handle_handshake: WAIT_ACK Twr timeout (no ACK start) → LINK_TIMEOUT\n");
                    process_event(ALEEvent::LINK_TIMEOUT);
                    return;
                }
            } else if (!hs_ack_tis_rcvd) {
                // (2) — waiting for "TIS SAM" conclusion.  Measured as silence
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
            // (A.5.5.3.4).  hs_tlww_start_ms is re-armed by each DATA/REP
            // extension word; the Tdrw (2×Trw) settle exceeds one on-grid word
            // period so a multi-word caller conclusion is fully collected before
            // the link is declared up (same fix as the WAIT_CYCLE_END settle).
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
    if (linked_terminating_) return;
    // AC-LINK-023: auto-terminate after Twa=30s (default; per-instance override
    // via set_timing_parameters(), see TimingParameters) of link inactivity.
    // AC-LINK-023-6: send TWAS before transitioning so the peer can return
    // to available state immediately (T-07).
    if ((current_time_ms - last_word_time_ms) >= timing_.Twa_ms) {
        linked_terminating_ = true;
        if (!active_call_to.empty() && !address_book.get_self_address().empty())
            transmit_words(ALESequenceBuilder::termination(
                active_call_to, address_book.get_self_address()).words());
        process_event(ALEEvent::LINK_TIMEOUT);
    }
}

void ALEStateMachine::on_link_activity() {
    // §A.5.5.3.5.2: any user-layer traffic (voice, data, AMD) resets Twa.
    // ALE words already reset last_word_time_ms via process_received_word();
    // this covers activity the ALE layer cannot observe directly.
    if (current_state == ALEState::LINKED)
        last_word_time_ms = current_time_ms;
}

void ALEStateMachine::handle_sounding() {
    // Fallback: wenn alle TX-Wörter durch sind aber on_word_complete() nicht gefeuert
    // hat (z.B. keine Adresse → kein Wort gesendet), Übergang direkt hier auslösen.
    // state_entry_time_ms wird NICHT überschrieben — der SOUNDING-Eintritts-Zeitpunkt
    // dient als Fensterbeginn.
    if (sounding_phase_ == SoundingPhase::TRANSMITTING && words_pending == 0) {
        sounding_phase_ = SoundingPhase::LISTENING;
        if (rx_enabled_callback) rx_enabled_callback(true);
        // Kein return: LISTENING-Timeout-Check folgt direkt unten
    }
    if (sounding_phase_ == SoundingPhase::TRANSMITTING) return;  // Wörter noch ausstehend

    // LISTENING: Trw-Fenster auf eingehenden Ruf warten (A.5.3.4)
    if ((current_time_ms - state_entry_time_ms) > ALETimingConstants::Trw_ms) {
        if (rx_enabled_callback) rx_enabled_callback(false);
        process_event(ALEEvent::SOUNDING_COMPLETE);
    }
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
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = target;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = false;
    calling_channel_index = 0;

    // Pre-compute all TX sequences via ALESequenceBuilder.
    // After this point the state machine never re-processes the address string.
    // scanning_seq_: scan_channels×2 words (§A.5.2.5.1, first 3 chars only)
    // leading_seq_:  full TO address × 2 (Tlc = 2×Tc, §A.5.5.3.1)
    // conclusion_seq_: own TIS address, sent once (§A.5.2.3.2.2)
    scanning_seq_   = ALESequenceBuilder::scanning_call(target, target_scan_channels);
    leading_seq_    = ALESequenceBuilder::leading_call(target);
    conclusion_seq_ = ALESequenceBuilder::conclusion(address_book.get_self_address());

    // Snapshot AMD orderwire and release the pending slot so the user can queue
    // the next message immediately.  active_message_ persists across channel retries.
    active_message_  = pending_message;
    pending_message  = PendingMessage{};

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

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_group_call(const std::vector<std::string>& members) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    if (members.empty())
        return false;

    // active_call_to is only used for post-link termination/emergency frames;
    // it is not refreshed once a specific member's response is identified
    // (to_address / active_call_from track the actual responding peer during
    // the handshake, same as for individual/net calls — see react_calling()).
    active_call_to        = members.front();
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = true;
    calling_channel_index = 0;

    group_scan_seq_ = ALESequenceBuilder::scanning_call_group(members, target_scan_channels);
    leading_seq_    = ALESequenceBuilder::leading_call_group(members);
    conclusion_seq_ = ALESequenceBuilder::conclusion(address_book.get_self_address());
    scanning_seq_   = group_scan_seq_;   // unified entry point for enqueue_call_sequence_()

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::respond_to_call() {
    if (current_state != ALEState::HANDSHAKE) return false;
    process_event(ALEEvent::HANDSHAKE_COMPLETE);
    return true;
}

bool ALEStateMachine::reject_call() {
    if (current_state != ALEState::HANDSHAKE) return false;
    pending_reject_ = true;
    accept_decided_ = true;  // resolves AWAIT_ACCEPT, if active (decline is final)
    return true;
}

bool ALEStateMachine::accept_call() {
    if (current_state != ALEState::HANDSHAKE || handshake_phase != HandshakePhase::AWAIT_ACCEPT)
        return false;
    accept_decided_ = true;
    return true;
}

void ALEStateMachine::set_require_explicit_accept(bool on, uint32_t decision_timeout_ms) {
    require_explicit_accept_    = on;
    accept_decision_timeout_ms_ = decision_timeout_ms;
}

bool ALEStateMachine::send_sounding() {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    return process_event(ALEEvent::SOUNDING_REQUEST);
}

void ALEStateMachine::terminate_link() {
    if (current_state != ALEState::LINKED) return;
    linked_terminating_ = true;
    // T-07: TO [peer] × 2 + TWAS [self] — peer returns to available state immediately
    transmit_words(ALESequenceBuilder::termination(
        active_call_to, address_book.get_self_address()).words());
    if (rx_enabled_callback) rx_enabled_callback(false);
}

void ALEStateMachine::emergency_manual_control() {
    emergency_active = true;
    if (operator_callback)
        operator_callback(OperatorEvent::EMERGENCY_ACTIVE);
    // AC-LINK-022-4: if linked, send TWAS so the peer returns to available state.
    if (current_state == ALEState::LINKED && !linked_terminating_
        && !active_call_to.empty() && !address_book.get_self_address().empty()) {
        linked_terminating_ = true;
        transmit_words(ALESequenceBuilder::termination(
            active_call_to, address_book.get_self_address()).words());
    }
    // Abort any ongoing operation; transition_to(IDLE) is a no-op if already IDLE.
    transition_to(ALEState::IDLE);
}

// ============================================================================
// process_received_word
// ============================================================================

// ── Fix 6: 3-error tolerance in HANDSHAKE/WAIT_CYCLE_END (A.5.5.3.2) ──
void ALEStateMachine::handle_invalid_word() {
    if (current_state != ALEState::HANDSHAKE) return;
    if (handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
        if (++contiguous_errors > ALETimingConstants::MAX_SCANNING_CALL_ERRORS) {
            SM_TRACE("[TRACE] handle_invalid_word: "
                     + std::to_string(+contiguous_errors)
                     + " contiguous errors → LINK_TIMEOUT\n");
            process_event(ALEEvent::LINK_TIMEOUT);
        }
    } else if (handshake_phase == HandshakePhase::CHANNEL_CHECK) {
        // Invalid signal on channel during LBT → busy → abort (AC-LINK-019-3)
        SM_TRACE("[TRACE] handle_invalid_word: invalid word during LBT → LINK_TIMEOUT\n");
        process_event(ALEEvent::LINK_TIMEOUT);
    }
}

void ALEStateMachine::react_scanning(const WordEvent& ev) {
    if (ev.type == WordEvent::Type::TO_SELF) {
        active_call_to = ev.address;
        process_event(ALEEvent::CALL_DETECTED);
        return;
    }
    // T-10: AllCall/AnyCall-Erkennung — Adresse beginnt mit '@' (A.5.2.4.7)
    if (current_state == ALEState::SCANNING
        && scanning_phase_ == ScanningPhase::HOPPING
        && ev.type == WordEvent::Type::NONE) {
        // WordEvent::NONE mit TO-Preamble und '@'-Adresse = AllCall
        // (Erkennung über das rohe Adressfeld im WordEvent ist hier nicht direkt
        // verfügbar; die Logik nutzt ev.address das vom Decoder gesetzt wird)
        if (!ev.address.empty() && ev.address[0] == '@') {
            scanning_phase_         = ScanningPhase::ALLCALL_PAUSE;
            allcall_pause_start_ms_ = current_time_ms;
        }
    }
    // TWAS während AllCall-Pause → Pause beenden (A.5.5.4.4)
    if (scanning_phase_ == ScanningPhase::ALLCALL_PAUSE
        && ev.type == WordEvent::Type::TWAS_REJECTION) {
        scanning_phase_ = ScanningPhase::HOPPING;
    }
}

// JOE's response frame per A.5.5.3.2: TO SAM [DATA]* TIS JOE [DATA]*
//   TO_SELF      → "TO SAM": JOE has begun his response; arm AC-LINK-019-8 timer.
//   TIS_CALLER   → "TIS JOE" (first conclusion word); arm Tlww.
//   DATA_EXTENSION → extended JOE address; Tlww reset each time.
//   TWAS_REJECTION → call rejected (AC-LINK-019-10).
//
// AC-LINK-019-7 (Ungültige Preamble-Sequenz → nächster Kanal) is NOT enforced
// here: WordEvent::NONE conflates unrelated QRM (TO:OTHER) with genuinely
// broken sequences.  Triggering a channel hop on QRM would cause false aborts.
// The existing AC-LINK-019-6/8 timeouts provide sufficient protection.
void ALEStateMachine::react_calling(const WordEvent& ev) {
    if (calling_phase != CallingPhase::LISTENING) return;

    switch (ev.type) {
    case WordEvent::Type::TO_SELF:
        if (!response_to_detected) {
            response_to_detected = true;
            response_rx_start_ms = current_time_ms;
        }
        break;
    case WordEvent::Type::TIS_CALLER:
        if (response_to_detected && tlww_start_ms == 0) {
            to_address                   = ev.address;
            active_call_from             = ev.address;
            tlww_start_ms                = current_time_ms;
            collecting_remote_conclusion = true;
        }
        break;
    case WordEvent::Type::DATA_EXTENSION:
        to_address      += ev.address;
        active_call_from = to_address;
        tlww_start_ms    = current_time_ms;
        break;
    case WordEvent::Type::TWAS_REJECTION:
        if (operator_callback)
            operator_callback(OperatorEvent::CALL_REJECTED);
        process_event(ALEEvent::LINK_TIMEOUT);
        break;
    default:
        break;
    }
}

// WAIT_CYCLE_END: read SAM's conclusion (TIS SAM [DATA]*).
// CHANNEL_CHECK:  any valid word → channel busy → abort.
// WAIT_ACK:       read SAM's ACK frame (TO JOE × 2 + TIS SAM [DATA]*).
void ALEStateMachine::react_handshake(const WordEvent& ev, const ALEWord& word) {
    if (ev.type == WordEvent::Type::CHANNEL_BUSY) {
        SM_TRACE("[TRACE] react_handshake: channel busy during LBT → LINK_TIMEOUT\n");
        process_event(ALEEvent::LINK_TIMEOUT);
        return;
    }

    if (handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
        switch (ev.type) {
        case WordEvent::Type::TIS_CALLER:
            if (!hs_conclusion_rcvd) {
                caller_address     = ev.address;
                active_call_from   = ev.address;
                hs_conclusion_rcvd = true;
                hs_tlww_start_ms   = current_time_ms;
            }
            break;
        case WordEvent::Type::DATA_EXTENSION:
            caller_address  += ev.address;
            active_call_from = caller_address;
            hs_tlww_start_ms = current_time_ms;
            break;
        case WordEvent::Type::TWAS_REJECTION:
            process_event(ALEEvent::LINK_TIMEOUT);
            break;
        case WordEvent::Type::NONE:
            // DATA/REP before TIS → message section has begun; arm Tmmax (AC-LINK-018-5).
            if (!hs_conclusion_rcvd
                && (word.type == PreambleType::DATA || word.type == PreambleType::REP)
                && hs_message_start_ms == 0) {
                hs_message_start_ms = current_time_ms;
            }
            break;
        default:
            break;
        }
    } else if (handshake_phase == HandshakePhase::WAIT_ACK) {
        switch (ev.type) {
        case WordEvent::Type::TO_SELF:
            // "TO JOE" — start of SAM's ACK frame (A.5.5.3.4).  Records the
            // arrival so handle_handshake() can switch from the narrow Twr
            // start-window to the frame-limited conclusion wait.
            if (hs_ack_to_ms == 0)
                hs_ack_to_ms = current_time_ms;
            break;
        case WordEvent::Type::TIS_CALLER:
            if (!hs_ack_tis_rcvd) {
                hs_ack_tis_rcvd  = true;
                hs_tlww_start_ms = current_time_ms;
            }
            break;
        case WordEvent::Type::DATA_EXTENSION:
            hs_tlww_start_ms = current_time_ms;
            break;
        case WordEvent::Type::TWAS_REJECTION:
            process_event(ALEEvent::LINK_TIMEOUT);
            break;
        default:
            break;
        }
    }
}

void ALEStateMachine::process_received_word(const ALEWord& word) {
    if (!word.valid) { handle_invalid_word(); return; }
    contiguous_errors = 0;
    last_word_time_ms = current_time_ms;

    LinkQuality lq;
    lq.fec_errors   = word.fec_errors;
    lq.total_words  = 1;
    lq.timestamp_ms = current_time_ms;
    update_link_quality(lq);

    const bool lbt_active =
        current_state == ALEState::HANDSHAKE
        && handshake_phase == HandshakePhase::CHANNEL_CHECK;

    const bool collecting =
        collecting_remote_conclusion   // CALLING/LISTENING
        || hs_conclusion_rcvd          // HANDSHAKE/WAIT_CYCLE_END
        || hs_ack_tis_rcvd;            // HANDSHAKE/WAIT_ACK

    WordDecodeContext ctx;
    ctx.self_address        = address_book.get_self_address();
    ctx.expected_caller     = caller_address.substr(0, 3);
    ctx.lbt_active          = lbt_active;
    ctx.collecting_conclusion = collecting;

    const WordEvent ev = decoder_.decode(word, ctx);

    switch (current_state) {
        case ALEState::IDLE:      react_scanning(ev);        break;
        case ALEState::SCANNING:  react_scanning(ev);        break;
        case ALEState::CALLING:   react_calling(ev);         break;
        case ALEState::HANDSHAKE: react_handshake(ev, word); break;
        case ALEState::LINKED:
            // T-03: Gegenseite sendet TWAS → Link sofort beenden (A.5.5.3.5)
            if (ev.type == WordEvent::Type::TWAS_REJECTION)
                process_event(ALEEvent::LINK_TERMINATED);
            break;
        case ALEState::SOUNDING:
            // T-08: im LISTENING-Fenster kann eine Station sofort zurückrufen (A.5.3.4)
            if (sounding_phase_ == SoundingPhase::LISTENING)
                react_scanning(ev);  // TO_SELF → CALL_DETECTED → HANDSHAKE
            break;
        default: break;
    }

    message_assembler.add_word(word);
}

void ALEStateMachine::update_link_quality(const LinkQuality& lq) {
    // Forward to LQAMetrics subsystem when attached.
    if (lqa_metrics_) {
        MetricsSample sample;
        sample.snr_db               = lq.snr_db;
        sample.fec_errors_corrected = static_cast<int>(lq.fec_errors);
        sample.decode_success       = (lq.fec_errors <= MAX_GOLAY_ERRORS);
        sample.timestamp_ms         = lq.timestamp_ms;
        const Channel* ch = channel_manager_.current();
        lqa_metrics_->add_sample(sample,
                                  ch ? ch->rx_frequency_hz : 0u,
                                  active_call_from);
    }

    // Route heuristic LQA score update through channel manager (Schritt 6).
    float score = 100.0f - (static_cast<float>(lq.fec_errors) * 10.0f);
    score = std::max(0.0f, std::min(100.0f, score));
    channel_manager_.update_lqa_score(channel_manager_.current_index(), score);
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
            // handle_linked() already handles Twa=30s (AC-LINK-023) with TWAS.
            // LINK_TIMEOUT_MS is a defensive safety net — it fires only if
            // handle_linked() is somehow not reached within its window.
            return (current_time_ms - last_word_time_ms) > ALETimingConstants::LINK_TIMEOUT_MS;
        default:
            return false;
    }
}

uint32_t ALEStateMachine::compute_calling_timeout_ms() const {
    const ale::CallingBudgetParams p{
        calling_channels.empty() ? 1u : static_cast<uint32_t>(calling_channels.size()),
        target_scan_channels,
        static_cast<uint32_t>(leading_seq_.size()),
        static_cast<uint32_t>(conclusion_seq_.size())
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
// All builders route through transmit_word() / transmit_words().
// transmit_word() is the single exit point: it stamps the current timestamp
// and fires transmit_callback.  No other code sets timestamp_ms on TX words.
//
// For the calling path the full sequence (scanning / leading / conclusion)
// is pre-computed in initiate_call() via ALESequenceBuilder and enqueued in
// one piece by enqueue_call_sequence_() at tune-complete.
//
// For the receive path (ACK, response) the remote address is not known at
// initiate_call() time, so ALESequenceBuilder is called at send time.

// Build a CMD AMD word: CMD preamble (110) with Expanded-64 payload (A.5.7.2.2).
// encode_ascii(DATA) uses is_valid_expanded64_char — same 21-bit encoding as DATA/REP
// but transmitted with the CMD preamble so the receiver recognises it as AMD.
static ALEWord make_cmd_amd_word(const char chars[3])
{
    const uint32_t payload = WordParser::encode_ascii(chars, PreambleType::DATA);
    if (payload == 0xFFFFFFFF) return ALEWord{};  // should not happen after sanitising
    ALEWord w{};
    w.type        = PreambleType::CMD;
    w.raw_payload = payload;
    w.address[0]  = chars[0];
    w.address[1]  = chars[1];
    w.address[2]  = chars[2];
    w.address[3]  = '\0';
    w.valid       = true;
    return w;
}

// Build the AMD orderwire word sequence for the MESSAGE section (A.5.7.2.2).
//
// Frame layout: CMD(chars 1-3)  DATA(4-6)  REP(7-9)  DATA(10-12) … max 30 words / 90 chars.
// All words carry only Expanded-64 characters (0x20-0x5F); out-of-range chars become '?'.
// Last triplet is padded with SP (0x20) as required by A.5.7.2.2.
static std::vector<ALEWord> build_amd_words(const std::string& text)
{
    const size_t n = std::min(text.size(), size_t{90});   // 30 words × 3 chars (A.5.7.2.3)
    std::vector<ALEWord> words;
    words.reserve((n + 2) / 3);
    for (size_t i = 0; i < n; i += 3) {
        char c[3] = {' ', ' ', ' '};
        for (size_t j = 0; j < 3 && i + j < n; ++j) {
            const char ch = text[i + j];
            c[j] = (static_cast<unsigned char>(ch) >= 0x20
                    && static_cast<unsigned char>(ch) <= 0x5F) ? ch : '?';
        }
        if (words.empty()) {
            // First word: CMD AMD preamble + Expanded-64 payload (A.5.7.2.2)
            words.push_back(make_cmd_amd_word(c));
        } else {
            // Subsequent: alternating DATA / REP (words[1]=DATA, [2]=REP, [3]=DATA, …)
            const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                                  :                            PreambleType::REP;
            words.push_back(WordParser::make_word(pt, c));
        }
    }
    return words;
}

void ALEStateMachine::enqueue_call_sequence_() {
    // scanning_seq_ and leading_seq_ are pre-built by initiate_call*():
    //   scanning_seq_  — scan_channels × 2 words (§A.5.2.5.1 / §A.5.5.4.3)
    //   leading_seq_   — full address × 2 (Tlc = 2×Tc, §A.5.5.3.1)
    //   message_seq_   — AMD orderwire DATA/REP words (AC-LINK-009-3), empty if none
    //   conclusion_seq_ — TIS self address (§A.5.2.3.2.2)
    // All words enqueued back-to-back; Trw grid emerges from the audio stream
    // (49 symbols × 8 ms per word).  on_word_complete() advances phase counters
    // against exactly these word counts.
    if (active_message_.type == PendingMessage::Type::AMD
            && !active_message_.content.empty()) {
        message_seq_ = ALESequence(build_amd_words(active_message_.content));
    } else {
        message_seq_ = ALESequence{};
    }
    // active_message_ is NOT cleared here — it survives across channel retries
    // so that AMD text is re-transmitted on each channel attempt.

    transmit_words(scanning_seq_.words());
    transmit_words(leading_seq_.words());
    transmit_words(message_seq_.words());
    transmit_words(conclusion_seq_.words());
}

void ALEStateMachine::build_ack_words() {
    // ACK frame per REQ-LINK-008 / §A.5.5.3.4 / Figure A-31:
    //   TO [to_address] × 2 + TIS [self_address]
    // to_address is set during the LISTENING phase (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    transmit_words(ALESequenceBuilder::ack(
        to_address, address_book.get_self_address()).words());
}

void ALEStateMachine::build_response_words() {
    // Accept: TO [caller] × 2 + TIS [self] (§A.5.5.3.3 / Figure A-30).
    // Reject:  TWAS [self] (FEAT-FRAME-005 / AC-FRAME-010-1).
    // caller_address is set during WAIT_CYCLE_END (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    transmit_words(ALESequenceBuilder::response(
        caller_address, address_book.get_self_address(), pending_reject_).words());
}

void ALEStateMachine::transmit_word(const ALEWord& word) {
    // Single exit point for all transmitted words.
    // Stamps the transmission timestamp so pre-computed words carry the
    // correct time regardless of when they were built.
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
    // ── LINKED termination path (T-07) ────────────────────────────────────
    // terminate_link() sendet TO×2 + TWAS; erst wenn alle Wörter durch sind
    // wird LINK_TERMINATED gefeuert.
    if (current_state == ALEState::LINKED && linked_terminating_) {
        // Decrement-first, dann auf 0 prüfen (wie SENDING_ACK): das LETZTE
        // gesendete Wort feuert LINK_TERMINATED. Das frühere "decrement-and-
        // return"-Muster wartete auf eine (N+1)-te Frame-Completion, die nie
        // kommt — SAM blieb dadurch in LINKED hängen.
        if (words_pending > 0) --words_pending;
        if (words_pending == 0) {
            linked_terminating_ = false;
            process_event(ALEEvent::LINK_TERMINATED);
        }
        return;
    }

    // ── SOUNDING path (T-05 + T-08) ──────────────────────────────────────
    // Decrement-first, dann auf 0 prüfen (wie LINKED-Terminierung/SENDING_ACK):
    // das LETZTE gesendete Wort öffnet das RX-Fenster. Der Zero-Word-Fall
    // (leere self_address → kein Wort, kein on_word_complete) wird weiterhin
    // vom Fallback in handle_sounding() abgedeckt.
    if (current_state == ALEState::SOUNDING) {
        if (words_pending > 0) --words_pending;
        // Alle Wörter gesendet — RX-Fenster öffnen (A.5.3.4)
        if (words_pending == 0 && sounding_phase_ == SoundingPhase::TRANSMITTING) {
            sounding_phase_     = SoundingPhase::LISTENING;
            state_entry_time_ms = current_time_ms;  // Fenster-Timer neu starten
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
        return;
    }

    // ── CALLING path (SAM side) ───────────────────────────────────────────
    if (current_state == ALEState::CALLING) {
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
                // equals 2×wpa.  After leading, enter MESSAGE if AMD words were queued.
                const uint32_t tlc_slots = static_cast<uint32_t>(leading_seq_.size());
                if (call_cycles_in_phase >= tlc_slots) {
                    if (!message_seq_.empty()) {
                        SM_TRACE("[TRACE] on_word_complete: LEADING_CALL → MESSAGE (msg_words="
                                 + std::to_string(message_seq_.size()) + ")\n");
                        calling_phase = CallingPhase::MESSAGE;
                    } else {
                        SM_TRACE("[TRACE] on_word_complete: LEADING_CALL → CONCLUSION (tlc_slots="
                                 + std::to_string(tlc_slots) + ")\n");
                        calling_phase = CallingPhase::CONCLUSION;
                    }
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::MESSAGE: {
                const uint32_t msg_slots = static_cast<uint32_t>(message_seq_.size());
                if (call_cycles_in_phase >= msg_slots) {
                    SM_TRACE("[TRACE] on_word_complete: MESSAGE → CONCLUSION (msg_slots="
                             + std::to_string(msg_slots) + ")\n");
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
                // ACK frame (Figure A-31): TO [to_address] × 2 + TIS [self_address].
                // Completion tied to the actual TX queue draining (words_pending == 0)
                // rather than a re-derived slot count, so any address length works.
                if (words_pending == 0) {
                    SM_TRACE("[TRACE] on_word_complete: SENDING_ACK → LINKED\n");
                    if (operator_callback)
                        operator_callback(OperatorEvent::LINK_ESTABLISHED);
                    process_event(ALEEvent::HANDSHAKE_COMPLETE);
                }
                break;
            }

            default:
                break;
        }
        return;
    }

    // ── HANDSHAKE / SENDING_RESPONSE path (JOE side) ─────────────────────
    if (current_state == ALEState::HANDSHAKE &&
        handshake_phase == HandshakePhase::SENDING_RESPONSE) {
        --words_pending;
        ++hs_words_in_phase;

        // Slot count depends on whether this is an accept or a reject frame.
        // Rejection (FEAT-FRAME-005): TWAS [self_address] only — no TO prefix.
        // Accept (Figure A-30):       TO [caller] × 2 + TIS [self_address].
        const uint32_t resp_slots = pending_reject_
            ? static_cast<uint32_t>(
                  AddressEncoder::encode(address_book.get_self_address(),
                                         PreambleType::TWAS).size())
            : 2u * static_cast<uint32_t>(
                       AddressEncoder::encode(caller_address, PreambleType::TO).size())
              + static_cast<uint32_t>(
                       AddressEncoder::encode(address_book.get_self_address(),
                                              PreambleType::TIS).size());

        if (hs_words_in_phase >= resp_slots) {
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
