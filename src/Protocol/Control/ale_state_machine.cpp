/**
 * \file ale_state_machine.cpp
 * \brief Implementation of ALE state machine
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Word/ale_frame.h"
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
    "WAIT_CYCLE_END", "SLOT_WAIT", "CHANNEL_CHECK", "SENDING_RESPONSE", "WAIT_ACK"
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
      group_scan_word_idx_(0),
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
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(ALEState::SCANNING);
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

void ALEStateMachine::update(uint32_t current_time_ms) {
    this->current_time_ms = current_time_ms;

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
        case ALEState::SCANNING:
            scanning_phase_          = ScanningPhase::HOPPING;
            allcall_pause_start_ms_  = 0;
            channel_manager_.start(current_time_ms);
            break;

        case ALEState::CALLING:
            pre_link_state_                = previous_state;  // T-01: IDLE oder SCANNING
            group_scan_word_idx_           = 0;
            link_start_time_ms             = current_time_ms;
            // DD-006: first_call_tx_ms is the Trw-grid anchor; set at end of TUNING
            // so that slot 0 fires exactly when the radio is tuned and ready (AC-LINK-017-2).
            first_call_tx_ms               = 0;
            lbt_start_ms                   = current_time_ms;
            tune_start_ms                  = 0;
            call_cycle_count               = 0;
            call_cycles_in_phase           = 0;
            words_pending                  = 0;
            leading_frame_idx_             = 0;
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
            pre_link_state_       = ALEState::SCANNING;  // T-01: nur von SCANNING erreichbar
            slot_wait_start_ms_   = 0;
            link_start_time_ms    = current_time_ms;
            // ── Handshake sub-state init (Fix 2/3/4) ─────────────────────
            twce_start_ms       = current_time_ms;
            hs_tlww_start_ms    = 0;
            hs_conclusion_rcvd  = false;
            caller_address.clear();
            hs_words_in_phase   = 0;
            hs_ack_start_ms     = 0;
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
            break;

        case ALEState::SOUNDING:
            sounding_phase_ = SoundingPhase::TRANSMITTING;
            if (rx_enabled_callback) rx_enabled_callback(false);
            if (!address_book.get_self_address().empty())
                transmit_words(
                    ALEFrameBuilder::conclusion(address_book.get_self_address()).words());
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
 *  LBT:           listen Twt (784 ms) — AC-LINK-017-1
 *  TUNING:        tune Tt (1045 ms) — AC-LINK-017-2; sets first_call_tx_ms
 *  SCANNING_CALL: TO first-word, 1 Trw/slot, C×2 slots total — AC-LINK-017-5/6
 *  LEADING_CALL:  full TO address × 2 (Tlc = 2×Tc) — AC-LINK-017-7
 *  MESSAGE:       optional AMD orderwire (stub: immediately falls to CONCLUSION)
 *  CONCLUSION:    TIS SAM — frame terminator; opens RX window → LISTENING
 *  LISTENING:     wait Twr (single-ch) / Twrt (multi-ch) for JOE's response
 *                   "TO SAM" → arm response tracking; "TIS JOE" → arm Tlww
 *                   Tlww elapsed → SENDING_ACK
 *  SENDING_ACK:   TO JOE + TIS SAM — third handshake frame (REQ-LINK-008)
 *                   complete → HANDSHAKE_COMPLETE → LINKED
 *
 * Responsibility of this function: decide WHEN to start the next TX.
 * Counter updates and phase transitions are in on_word_complete() (DD-009).
 *
 * Slot schedule (DD-006):
 *   next_tx = first_call_tx_ms + call_cycle_count × Trw_ms
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
        // Blind tune Tt (1045 ms) — AC-LINK-017-2.
        // Sets first_call_tx_ms (Trw-grid anchor) so slot 0 starts at tune-complete.
        case CallingPhase::TUNING: {
            if ((current_time_ms - tune_start_ms) >= ALETimingConstants::Tt_ms) {
                first_call_tx_ms     = current_time_ms;
                call_cycles_in_phase = 0;
                // T-06: Net calls laufen über denselben SAM-Pfad wie Individual Calls
                // T-11: Group calls nutzen GROUP_SCANNING_CALL (THRU/REP statt TO)
                if (active_call_is_group && target_scan_channels > 0) {
                    calling_phase        = CallingPhase::GROUP_SCANNING_CALL;
                    group_scan_word_idx_ = 0;
                } else if (target_scan_channels > 0) {
                    calling_phase = CallingPhase::SCANNING_CALL;
                } else {
                    calling_phase = CallingPhase::LEADING_CALL;
                }
            }
            break;
        }

        // ── SCANNING_CALL ─────────────────────────────────────────────────
        // One TO word per slot (first address chunk only), no DATA/REP.
        // Phase transitions in on_word_complete().
        case CallingPhase::SCANNING_CALL: {
            if (words_pending > 0) break;
            const uint32_t next_tx = first_call_tx_ms
                                   + call_cycle_count * ALETimingConstants::Trw_ms;
            if (current_time_ms >= next_tx)
                build_scanning_word();
            break;
        }

        // ── GROUP_SCANNING_CALL ───────────────────────────────────────────
        // T-11: THRU/REP-Paar pro Slot (A.5.5.4.3), rotiert bis Tsc abgelaufen.
        // Phase transitions in on_word_complete().
        case CallingPhase::GROUP_SCANNING_CALL: {
            if (words_pending > 0) break;
            const uint32_t next_tx = first_call_tx_ms
                                   + call_cycle_count * ALETimingConstants::Trw_ms;
            if (current_time_ms >= next_tx)
                build_group_scanning_word();
            break;
        }

        // ── LEADING_CALL ──────────────────────────────────────────────────
        // Full TO address twice (Tlc = 2 × Tc). Phase transitions in on_word_complete().
        case CallingPhase::LEADING_CALL: {
            if (words_pending > 0) break;
            const uint32_t next_tx = first_call_tx_ms
                                   + call_cycle_count * ALETimingConstants::Trw_ms;
            if (current_time_ms >= next_tx)
                build_leading_call_word();
            break;
        }

        // ── MESSAGE ───────────────────────────────────────────────────────
        // Stub (AC-LINK-009-3 not yet implemented): no words to send.
        // Per DD-013, phase transitions are driven by on_word_complete(), not update().
        // The stub synthesises the callback here so the transition stays in on_word_complete().
        // TODO: when AMD is implemented, replace this with build_message_words() and
        //       let on_word_complete() handle the word-count guard normally.
        case CallingPhase::MESSAGE: {
            on_word_complete();
            break;
        }

        // ── CONCLUSION ────────────────────────────────────────────────────
        // TIS SAM — sent once. RX window opens after last word in on_word_complete().
        // Uses same Trw slot timer as LEADING_CALL so the conclusion starts exactly
        // at call_cycle_count × Trw from first_call_tx_ms, not immediately after
        // the last leading-word's 3rd copy (which finishes 132 ms before the slot).
        case CallingPhase::CONCLUSION: {
            if (words_pending > 0) break;
            if (call_cycles_in_phase == 0) {
                const uint32_t next_tx = first_call_tx_ms
                                       + call_cycle_count * ALETimingConstants::Trw_ms;
                if (current_time_ms >= next_tx)
                    build_conclusion_words();
            }
            break;
        }

        // ── LISTENING ─────────────────────────────────────────────────────
        // Three distinct sub-phases driven by response detection:
        //
        // (a) !response_to_detected:
        //     Waiting for JOE's first "TO SAM" word within Trc_min + Trw.
        //     JOE's minimum response delay = Tlww (392) + LBT (392) + Trw (392) = 3×Trw.
        //     + one extra Trw margin for WinMM audio buffer latency.
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
                // (a) — Trc_min + Trw window (= 4×Trw = 1568 ms).
                // AC-LINK-019-6/4: Twr for single channel, Twrt for multi.
                // For this SW decoder: JOE needs Tlww + LBT + decode = 3×Trw before
                // SAM can detect "TO SAM"; +1×Trw absorbs WinMM round-trip latency.
                const uint32_t wait_ms = ALETimingConstants::Trc_min_ms
                                       + ALETimingConstants::Trw_ms; // 4×Trw = 1568 ms
                if ((current_time_ms - listening_start_ms) >= wait_ms)
                    try_next_calling_channel(); // AC-LINK-019-6
            } else if (tlww_start_ms == 0) {
                // (b) — waiting for TIS JOE conclusion (5×Trw budget)
                if ((current_time_ms - response_rx_start_ms)
                        >= 5u * ALETimingConstants::Trw_ms)
                    try_next_calling_channel(); // AC-LINK-019-8
            } else {
                // (c) — Tlww: let last copy of JOE's conclusion settle
                if ((current_time_ms - tlww_start_ms) >= ALETimingConstants::Tlww_ms) {
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
            // Twce timeout: calling cycle did not end → abort (A.5.5.3.2, AC-LINK-018-5)
            if (!hs_conclusion_rcvd &&
                (current_time_ms - twce_start_ms) >= twce_ms) {
                SM_TRACE("[TRACE] handle_handshake: Twce timeout → LINK_TIMEOUT\n");
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
            // Conclusion received + Tlww elapsed → SLOT_WAIT (T-09) before LBT
            if (hs_conclusion_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tlww_ms) {
                SM_TRACE("[TRACE] handle_handshake: Tlww elapsed → SLOT_WAIT\n");
                handshake_phase     = HandshakePhase::SLOT_WAIT;
                slot_wait_start_ms_ = current_time_ms;
                // RX bleibt offen während Slot-Wait
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
        // Wait Trc_min for calling station's ACK frame (TO JOE × 2 + TIS SAM).
        // SAM needs Tlww (392) + first ACK word (392) = 2×Trw before JOE sees it;
        // +1×Trw absorbs WinMM round-trip latency → 3×Trw = 1176 ms total.
        // Incoming words are processed by process_received_word().
        case HandshakePhase::WAIT_ACK: {
            // Trc_min timeout: no ACK received → abort (A.5.5.3.4 / AC-LINK-020-2)
            if ((current_time_ms - hs_ack_start_ms) >= ALETimingConstants::Trc_min_ms) {
                SM_TRACE("[TRACE] handle_handshake: WAIT_ACK Trc_min timeout → LINK_TIMEOUT\n");
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // TIS [caller] received + Tlww elapsed → LINKED (A.5.5.3.4)
            if (hs_ack_tis_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tlww_ms) {
                SM_TRACE("[TRACE] handle_handshake: ACK Tlww elapsed → LINKED\n");
                if (operator_callback)
                    operator_callback(OperatorEvent::LINK_ESTABLISHED);
                process_event(ALEEvent::HANDSHAKE_COMPLETE);
            }
            break;
        }
    }
}

void ALEStateMachine::handle_linked() {
    // Link health monitoring — extensible with LQA
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

bool ALEStateMachine::initiate_call(const std::string& to_address) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = to_address;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = false;
    calling_channel_index = 0;

    // Pre-compute all TX frames for this call via ALEFrameBuilder.
    // After this point the state machine never re-processes the address string.
    //
    // scanning_frame_   — 1 word (first 3 chars only, A.5.2.5.1)
    // leading_frame_    — full TO address × 2 (Tlc = 2 × Tc, A.5.5.3.1)
    // conclusion_frame_ — own TIS address, sent once (A.5.2.3.2.2)
    scanning_frame_   = ALEFrameBuilder::scanning_individual(to_address);
    leading_frame_    = ALEFrameBuilder::leading_individual(to_address);
    conclusion_frame_ = ALEFrameBuilder::conclusion(address_book.get_self_address());

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

    // Pre-compute TX frames (same structure as individual call; net call
    // protocol is currently stubbed as NET_CALL_STUB).
    scanning_frame_   = ALEFrameBuilder::scanning_individual(net_address);
    leading_frame_    = ALEFrameBuilder::leading_individual(net_address);
    conclusion_frame_ = ALEFrameBuilder::conclusion(address_book.get_self_address());

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_group_call(const std::string& relay, const std::string& dest) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = dest;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    active_call_is_group  = true;
    calling_channel_index = 0;

    group_scan_frame_ = ALEFrameBuilder::scanning_group(relay, dest);
    leading_frame_    = ALEFrameBuilder::leading_group(relay, dest);
    conclusion_frame_ = ALEFrameBuilder::conclusion(address_book.get_self_address());
    // scanning_frame_ nicht benötigt bei Group-Calls (group_scan_frame_ übernimmt)
    scanning_frame_   = group_scan_frame_;

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
    return true;
}

bool ALEStateMachine::send_sounding() {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    return process_event(ALEEvent::SOUNDING_REQUEST);
}

void ALEStateMachine::terminate_link() {
    if (current_state != ALEState::LINKED) return;
    linked_terminating_ = true;
    // T-07: TO [peer] × 2 + TWAS [self] — Gegenseite soll sofort zurück in available state
    transmit_words(ALEFrameBuilder::termination_frame(
        active_call_to, address_book.get_self_address()).words());
    if (rx_enabled_callback) rx_enabled_callback(false);
}

void ALEStateMachine::emergency_manual_control() {
    emergency_active = true;
    if (operator_callback)
        operator_callback(OperatorEvent::EMERGENCY_ACTIVE);
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
                                  ch ? ch->frequency_hz : 0u,
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
            return (current_time_ms - state_entry_time_ms) > ALETimingConstants::Twa_ms;
        case ALEState::LINKED:
            // T-02: Inaktivitäts-Timer — zurücksetzen bei jedem empfangenen Wort
            return (current_time_ms - last_word_time_ms) > ALETimingConstants::LINK_TIMEOUT_MS;
        default:
            return false;
    }
}

uint32_t ALEStateMachine::compute_calling_timeout_ms() const {
    const ale::CallingBudgetParams p{
        calling_channels.empty() ? 1u : static_cast<uint32_t>(calling_channels.size()),
        target_scan_channels,
        static_cast<uint32_t>(leading_frame_.size()),
        calling_channels.size() > 1
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
        leading_frame_idx_   = 0;
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
// Word builders — one per calling phase
// ============================================================================
//
// All build_* functions route through transmit_word() / transmit_words().
// transmit_word() is the single exit point: it stamps the current timestamp
// and fires transmit_callback.  No other code sets timestamp_ms on TX words.
//
// For the calling path (scanning / leading / conclusion) the words are
// pre-computed in initiate_call() via AddressEncoder and stored in
// scanning_word_ / leading_words_ / conclusion_words_.
//
// For the receive path (ACK, response) the remote address is not known at
// initiate_call() time, so AddressEncoder::encode() is called at send time.

void ALEStateMachine::build_group_scanning_word() {
    // T-11: Sendet das nächste Wort aus dem THRU/REP-Paar (group_scan_frame_).
    // Rotiert automatisch: index wraps nach group_scan_frame_.size().
    const auto& words = group_scan_frame_.words();
    if (!words.empty()) {
        transmit_word(words[group_scan_word_idx_ % words.size()]);
        ++group_scan_word_idx_;
    }
}

void ALEStateMachine::build_scanning_word() {
    // Transmit the single word in scanning_frame_ for the current Trw slot.
    // scanning_frame_ holds only the first 3 chars of the destination address
    // (A.5.2.5.1).  DATA/REP extension words are forbidden in the scanning
    // section; the full address is sent only in the leading call.
    // on_word_complete() counts slots and transitions to LEADING_CALL after
    // C × 2 slots.
    transmit_word(scanning_frame_.words()[0]);
}

void ALEStateMachine::build_leading_call_word() {
    // Send exactly one word from the pre-doubled leading_frame_ per Trw slot.
    // leading_frame_idx_ advances through the sequence; on_word_complete() counts
    // each word individually against leading_frame_.size() total slots (Tlc = 2×Tc).
    const auto& words = leading_frame_.words();
    if (leading_frame_idx_ < words.size())
        transmit_word(words[leading_frame_idx_++]);
}

void ALEStateMachine::build_conclusion_words() {
    // Transmit conclusion_frame_ once.
    // conclusion_frame_ encodes the own address with TIS preamble
    // (A.5.2.3.2.2), same DATA/REP scheme as the leading frame.
    transmit_words(conclusion_frame_.words());
}

void ALEStateMachine::build_ack_words() {
    // ACK frame per REQ-LINK-008 / A.5.5.3.4 / Figure A-31:
    //   TO [to_address] × 2 + TIS [self_address]
    // to_address is set during the LISTENING phase (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    transmit_words(ALEFrameBuilder::ack_frame(
        to_address, address_book.get_self_address()).words());
}

void ALEStateMachine::build_response_words() {
    // Accept response per A.5.5.3.3 / Figure A-30: TO [caller] × 2 + TIS [self].
    // Rejection per FEAT-FRAME-005 / AC-FRAME-010-1: TWAS [self].
    // caller_address is set during WAIT_CYCLE_END (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    transmit_words(ALEFrameBuilder::response_frame(
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
// on_word_complete — fired by ALE2GModem after all 3 copies of one word sent
// ============================================================================

void ALEStateMachine::on_word_complete() {
    // ── LINKED termination path (T-07) ────────────────────────────────────
    // terminate_link() sendet TO×2 + TWAS; erst wenn alle Wörter durch sind
    // wird LINK_TERMINATED gefeuert.
    if (current_state == ALEState::LINKED && linked_terminating_) {
        if (words_pending > 0) { --words_pending; return; }
        linked_terminating_ = false;
        process_event(ALEEvent::LINK_TERMINATED);
        return;
    }

    // ── SOUNDING path (T-05 + T-08) ──────────────────────────────────────
    if (current_state == ALEState::SOUNDING) {
        if (words_pending > 0) { --words_pending; return; }
        // Alle Wörter gesendet — RX-Fenster öffnen (A.5.3.4)
        if (sounding_phase_ == SoundingPhase::TRANSMITTING) {
            sounding_phase_     = SoundingPhase::LISTENING;
            state_entry_time_ms = current_time_ms;  // Fenster-Timer neu starten
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
        return;
    }

    // ── MESSAGE stub guard ────────────────────────────────────────────────
    // MESSAGE sends 0 words; transition to CONCLUSION without touching word counters.
    // Triggered synthetically by handle_calling() per DD-013.
    if (current_state == ALEState::CALLING && calling_phase == CallingPhase::MESSAGE) {
        calling_phase        = CallingPhase::CONCLUSION;
        call_cycles_in_phase = 0;
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
                    leading_frame_idx_   = 0;
                }
                break;
            }

            case CallingPhase::GROUP_SCANNING_CALL: {
                // T-11: 2 Wörter (THRU+REP) pro Slot; tsc_slots nach target_scan_channels × 2
                // group_scan_frame_.size() = 2 Wörter → je 2 on_word_complete pro Slot
                const uint32_t words_per_slot = static_cast<uint32_t>(group_scan_frame_.size());
                const uint32_t tsc_total = target_scan_channels * 2u * words_per_slot;
                if (call_cycles_in_phase >= tsc_total) {
                    SM_TRACE("[TRACE] on_word_complete: GROUP_SCANNING_CALL → LEADING_CALL\n");
                    calling_phase        = CallingPhase::LEADING_CALL;
                    call_cycles_in_phase = 0;
                    leading_frame_idx_   = 0;
                }
                break;
            }

            case CallingPhase::LEADING_CALL: {
                // leading_frame_ was pre-doubled by ALEFrameBuilder::leading_individual();
                // its size already equals 2 × wpa (Tlc = 2 × Tc = 2 × wpa × Trw).
                const uint32_t tlc_slots = static_cast<uint32_t>(leading_frame_.size());
                if (call_cycles_in_phase >= tlc_slots) {
                    SM_TRACE("[TRACE] on_word_complete: LEADING_CALL → "
                             + std::string(pending_message.type != PendingMessage::Type::NONE
                                           ? "MESSAGE" : "CONCLUSION")
                             + " (tlc_slots=" + std::to_string(tlc_slots) + ")\n");
                    calling_phase = (pending_message.type != PendingMessage::Type::NONE)
                                    ? CallingPhase::MESSAGE
                                    : CallingPhase::CONCLUSION;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::MESSAGE: {
                // Unreachable: the MESSAGE guard at the top of on_word_complete()
                // intercepts the stub call before words_pending is decremented.
                break;
            }

            case CallingPhase::CONCLUSION: {
                const uint32_t conclusion_slots =
                    static_cast<uint32_t>(conclusion_frame_.size());
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
                 // ACK frame (Figure A-31): TO [to_address] × 2 + TIS [self_address]
                 // to_address is set during the LISTENING phase; encode here to
                // get the exact word count matching build_ack_words().
                 const uint32_t ack_slots =
                     2u * static_cast<uint32_t>(
                              AddressEncoder::encode(to_address, PreambleType::TO).size())
                     + static_cast<uint32_t>(conclusion_frame_.size());
                if (call_cycles_in_phase >= ack_slots) {
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
                SM_TRACE("[TRACE] on_word_complete: SENDING_RESPONSE (TWAS reject) → SCANNING\n");
                pending_reject_ = false;
                process_event(ALEEvent::LINK_TIMEOUT);
            } else {
                SM_TRACE("[TRACE] on_word_complete: SENDING_RESPONSE → WAIT_ACK\n");
                handshake_phase  = HandshakePhase::WAIT_ACK;
                hs_ack_start_ms  = current_time_ms;
                hs_tlww_start_ms = 0;
                hs_ack_tis_rcvd  = false;
                if (rx_enabled_callback) rx_enabled_callback(true);
            }
        }
    }
}

} // namespace ale
