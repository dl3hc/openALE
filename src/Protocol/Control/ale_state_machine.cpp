/**
 * \file ale_state_machine.cpp
 * \brief Implementation of ALE state machine
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Word/address_encoder.h"
#include <algorithm>
#include <cstring>
#include <iostream>

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
    "LBT", "TUNING", "SCANNING_CALL", "LEADING_CALL", "MESSAGE",
    "CONCLUSION", "LISTENING", "SENDING_ACK", "NET_CALL_STUB"
};

// Must match HandshakePhase enum order exactly.
static const char* HS_PHASE_NAMES[] = {
    "WAIT_CYCLE_END", "CHANNEL_CHECK", "SENDING_RESPONSE", "WAIT_ACK"
};

// ============================================================================
// Constructor
// ============================================================================

ALEStateMachine::ALEStateMachine()
    : current_state(ALEState::IDLE),
      previous_state(ALEState::IDLE),
      link_start_time_ms(0),
      last_word_time_ms(0),
      calling_phase(CallingPhase::LBT),
      active_call_is_net(false),
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
      last_scan_hop_time_ms(0),
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
      hs_message_start_ms(0)
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
            break;

        case ALEState::CALLING:
            if (event == ALEEvent::HANDSHAKE_COMPLETE) return transition_to(ALEState::LINKED);
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(ALEState::IDLE);
            break;

        case ALEState::HANDSHAKE:
            if (event == ALEEvent::HANDSHAKE_COMPLETE) return transition_to(ALEState::LINKED);
            if (event == ALEEvent::LINK_TIMEOUT)       return transition_to(ALEState::SCANNING);
            break;

        case ALEState::LINKED:
            if (event == ALEEvent::LINK_TERMINATED ||
                event == ALEEvent::LINK_TIMEOUT)       return transition_to(ALEState::IDLE);
            break;

        case ALEState::SOUNDING:
            if (event == ALEEvent::SOUNDING_COMPLETE)  return transition_to(ALEState::SCANNING);
            break;

        case ALEState::ERROR:
            if (event == ALEEvent::START_SCAN) return transition_to(ALEState::SCANNING);
            else                               return transition_to(ALEState::IDLE);
            break;
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
            scan_config.channel_index = 0;
            last_scan_hop_time_ms     = current_time_ms;
            if (!scan_config.scan_list.empty())
                set_channel(0);
            break;

        case ALEState::CALLING:
            link_start_time_ms             = current_time_ms;
            // DD-013/DD-006: slot-grid anchor set at state entry; first word fires
            // when current_time_ms >= first_call_tx_ms + call_cycle_count × Trw_ms.
            first_call_tx_ms               = current_time_ms;
            call_cycle_count               = 0;
            call_cycles_in_phase           = 0;
            words_pending                  = 0;
            listening_start_ms             = 0;
            response_to_detected           = false;
            response_rx_start_ms           = 0;
            tlww_start_ms                  = 0;
            collecting_remote_conclusion   = false;
            joe_address.clear();

            // Activate first calling channel if a list was set
            if (!calling_channels.empty() && channel_callback)
                channel_callback(calling_channels[calling_channel_index]);

            // Enter TX phase directly (DD-013: phase transitions only via on_word_complete).
            // target_scan_channels == 0: target on fixed channel → LEADING_CALL directly.
            // target_scan_channels  > 0: scan target first → SCANNING_CALL.
            if (active_call_is_net) {
                calling_phase = CallingPhase::NET_CALL_STUB;
            } else if (target_scan_channels > 0) {
                calling_phase = CallingPhase::SCANNING_CALL;
            } else {
                calling_phase = CallingPhase::LEADING_CALL;
            }
            break;

        case ALEState::HANDSHAKE:
            link_start_time_ms  = current_time_ms;
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
            handshake_phase     = HandshakePhase::WAIT_CYCLE_END;
            // Twce = 2 × own Ts (Table A-XV); fall back to 1 channel if not configured.
            {
                const uint32_t C = static_cast<uint32_t>(
                    std::max(size_t(1), scan_config.scan_list.size()));
                twce_ms = ale::calc_twce_ms(C);
            }
            if (rx_enabled_callback) rx_enabled_callback(true);
            break;

        case ALEState::LINKED:
            link_start_time_ms = current_time_ms;
            last_word_time_ms  = current_time_ms;
            break;

        case ALEState::SOUNDING:
            if (!address_book.get_self_address().empty())
                transmit_words(AddressEncoder::encode(address_book.get_self_address(),
                                                      WordType::TIS));
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

void ALEStateMachine::handle_idle() {}

void ALEStateMachine::handle_scanning() {
    if (check_scan_dwell_timeout())
        hop_to_next_channel();
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
    std::cout << "[TRACE] t=" << current_time_ms
              << " phase=" << PHASE_NAMES[static_cast<int>(calling_phase)]
              << " pending=" << words_pending
              << " cycles=" << call_cycles_in_phase
              << " total=" << call_cycle_count
              << " addr=" << active_call_to
              << "\n";

    switch (calling_phase) {

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
        // Optional AMD/DTM/DBM orderwire — stub, falls through to CONCLUSION.
        // TODO: implement AMD per AC-LINK-009-3 (earliest start: word 30 = 11.368 s)
        case CallingPhase::MESSAGE: {
            calling_phase        = CallingPhase::CONCLUSION;
            call_cycles_in_phase = 0;
            break;
        }

        // ── CONCLUSION ────────────────────────────────────────────────────
        // TIS SAM — sent once. RX window opens after last word in on_word_complete().
        case CallingPhase::CONCLUSION: {
            if (words_pending > 0) break;
            if (call_cycles_in_phase == 0)
                build_conclusion_words();
            break;
        }

        // ── LISTENING ─────────────────────────────────────────────────────
        // Three distinct sub-phases driven by response detection:
        //
        // (a) !response_to_detected:
        //     Waiting for JOE's first "TO SAM" word within Twr/Twrt.
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
                // (a) — Twr / Twrt window
                const uint32_t wait_ms = (calling_channels.size() > 1)
                                         ? ALETimingConstants::Twrt_ms
                                         : ALETimingConstants::Twr_ms;
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

        // ── NET_CALL_STUB ──────────────────────────────────────────────────
        case CallingPhase::NET_CALL_STUB:
            break;
    }
}

void ALEStateMachine::handle_handshake() {
    std::cout << "[TRACE] t=" << current_time_ms
              << " hs_phase=" << HS_PHASE_NAMES[static_cast<int>(handshake_phase)]
              << " pending=" << words_pending
              << " caller=" << caller_address
              << " conclusion=" << hs_conclusion_rcvd
              << "\n";

    switch (handshake_phase) {

        // ── WAIT_CYCLE_END ────────────────────────────────────────────────
        // Listen for calling station's conclusion (TIS SAM) within Twce.
        // Incoming words are processed by process_received_word().
        case HandshakePhase::WAIT_CYCLE_END: {
            // Twce timeout: calling cycle did not end → abort (A.5.5.3.2, AC-LINK-018-5)
            if (!hs_conclusion_rcvd &&
                (current_time_ms - twce_start_ms) >= twce_ms) {
                std::cout << "[TRACE] handle_handshake: Twce timeout → LINK_TIMEOUT\n";
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // Tmmax: message section began but conclusion not yet received → abort
            // (A.5.5.3.2, AC-LINK-018-5 second condition)
            if (!hs_conclusion_rcvd && hs_message_start_ms > 0 &&
                (current_time_ms - hs_message_start_ms) >= ALETimingConstants::Tm_max_ms) {
                std::cout << "[TRACE] handle_handshake: Tmmax elapsed without conclusion → LINK_TIMEOUT\n";
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // Conclusion received + Tlww elapsed → LBT before response (A.5.5.3.3, AC-LINK-019-1)
            if (hs_conclusion_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tlww_ms) {
                std::cout << "[TRACE] handle_handshake: Tlww elapsed → CHANNEL_CHECK\n";
                handshake_phase = HandshakePhase::CHANNEL_CHECK;
                hs_lbt_start_ms = current_time_ms;
                // RX stays open during LBT to detect channel activity
            }
            break;
        }

        // ── CHANNEL_CHECK ─────────────────────────────────────────────────
        // Listen-Before-Transmit: 2×Trw per A.5.5.3.3 / AC-LINK-019-1.
        // Any word received here signals channel busy → abort (AC-LINK-019-3).
        // process_received_word() handles the busy-detection path.
        case HandshakePhase::CHANNEL_CHECK: {
            if ((current_time_ms - hs_lbt_start_ms) >= 2u * ALETimingConstants::Trw_ms) {
                std::cout << "[TRACE] handle_handshake: LBT clear → SENDING_RESPONSE\n";
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
        // Wait Twr for calling station's ACK frame (TO JOE × 2 + TIS SAM).
        // Incoming words are processed by process_received_word().
        case HandshakePhase::WAIT_ACK: {
            // Twr timeout: no ACK received → abort (A.5.5.3.4)
            if ((current_time_ms - hs_ack_start_ms) >= ALETimingConstants::Twr_ms) {
                std::cout << "[TRACE] handle_handshake: WAIT_ACK Twr timeout → LINK_TIMEOUT\n";
                process_event(ALEEvent::LINK_TIMEOUT);
                return;
            }
            // TIS [caller] received + Tlww elapsed → LINKED (A.5.5.3.4)
            if (hs_ack_tis_rcvd && hs_tlww_start_ms > 0 &&
                (current_time_ms - hs_tlww_start_ms) >= ALETimingConstants::Tlww_ms) {
                std::cout << "[TRACE] handle_handshake: ACK Tlww elapsed → LINKED\n";
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
    uint32_t elapsed = current_time_ms - state_entry_time_ms;
    if (elapsed > ALETimingConstants::Trw_ms)
        process_event(ALEEvent::SOUNDING_COMPLETE);
}

// ============================================================================
// Public API
// ============================================================================

void ALEStateMachine::configure_scan(const ScanConfig& config) {
    scan_config = config;
}

void ALEStateMachine::add_scan_channel(const Channel& channel) {
    scan_config.scan_list.push_back(channel);
}

void ALEStateMachine::set_self_address(const std::string& address) {
    address_book.set_self_address(address);
}

const Channel* ALEStateMachine::get_current_channel() const {
    if (scan_config.scan_list.empty()) return nullptr;
    if (scan_config.channel_index >= scan_config.scan_list.size()) return nullptr;
    return &scan_config.scan_list[scan_config.channel_index];
}

bool ALEStateMachine::initiate_call(const std::string& to_address) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = to_address;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = false;
    calling_channel_index = 0;

    // Pre-compute all TX word sequences for this call.
    // After this point the state machine never re-processes the address string
    // for transmission; it only iterates the vectors below.
    //
    // scanning_word_   — first 3 chars only (A.5.2.5.1); sent once per Trw slot
    // leading_words_   — full address (A.5.5.3.1); sent twice (Tlc = 2 × Tc)
    // conclusion_words_ — own address with TIS (A.5.2.3.2.2); sent once
    scanning_word_    = AddressEncoder::encode_first(to_address, WordType::TO);
    leading_words_    = AddressEncoder::encode(to_address, WordType::TO);
    conclusion_words_ = AddressEncoder::encode(address_book.get_self_address(), WordType::TIS);

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_net_call(const std::string& net_address) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to        = net_address;
    active_call_from      = address_book.get_self_address();
    active_call_is_net    = true;
    calling_channel_index = 0;

    // Pre-compute TX sequences (same encoding as individual call; net call
    // protocol is currently stubbed as NET_CALL_STUB).
    scanning_word_    = AddressEncoder::encode_first(net_address, WordType::TO);
    leading_words_    = AddressEncoder::encode(net_address, WordType::TO);
    conclusion_words_ = AddressEncoder::encode(address_book.get_self_address(), WordType::TIS);

    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::respond_to_call() {
    if (current_state != ALEState::HANDSHAKE) return false;
    process_event(ALEEvent::HANDSHAKE_COMPLETE);
    return true;
}

bool ALEStateMachine::send_sounding() {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;
    return process_event(ALEEvent::SOUNDING_REQUEST);
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

void ALEStateMachine::process_received_word(const ALEWord& word) {
    // ── Fix 6: 3-error tolerance in HANDSHAKE/WAIT_CYCLE_END (A.5.5.3.2) ──
    // During the scanning call section, up to MAX_SCANNING_CALL_ERRORS contiguous
    // FEC-uncorrectable words are tolerated without rejecting the frame.
    if (!word.valid) {
        if (current_state == ALEState::HANDSHAKE) {
            if (handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
                if (++contiguous_errors > ALETimingConstants::MAX_SCANNING_CALL_ERRORS) {
                    std::cout << "[TRACE] process_received_word: "
                              << contiguous_errors << " contiguous errors → LINK_TIMEOUT\n";
                    process_event(ALEEvent::LINK_TIMEOUT);
                }
            } else if (handshake_phase == HandshakePhase::CHANNEL_CHECK) {
                // Signal on channel during LBT → busy → abort (AC-LINK-019-3)
                std::cout << "[TRACE] process_received_word: channel busy during LBT → LINK_TIMEOUT\n";
                process_event(ALEEvent::LINK_TIMEOUT);
            }
        }
        return;
    }
    contiguous_errors = 0;   // Any valid word resets the error run.

    last_word_time_ms = current_time_ms;

    LinkQuality lq;
    lq.fec_errors   = word.fec_errors;
    lq.total_words  = 1;
    lq.timestamp_ms = current_time_ms;
    update_link_quality(lq);

    // Extract 3-char address field; trim trailing spaces.
    std::string addr(word.address, 3);
    auto trim = addr.find_last_not_of(' ');
    if (trim != std::string::npos) addr.erase(trim + 1);

    switch (current_state) {

        // ── SCANNING ─────────────────────────────────────────────────────
        case ALEState::SCANNING:
            if (word.type == WordType::TO || word.type == WordType::TWAS) {
                if (address_book.is_self(addr)) {
                    active_call_to = addr;
                    process_event(ALEEvent::CALL_DETECTED);
                }
            }
            break;

        // ── CALLING / LISTENING (SAM side) ───────────────────────────────
        // JOE's response frame per A.5.5.3.2: TO SAM [DATA]* TIS JOE [DATA]*
        //
        // Detection sequence:
        //   1. TO + SAM's address → "TO SAM": JOE has begun his response.
        //      Sets response_to_detected; starts AC-LINK-019-8 timer.
        //   2. TIS + any address  → "TIS JOE" (first word of conclusion).
        //      Captures JOE's identity; starts Tlww.
        //   3. DATA/REP after TIS → extended JOE address; Tlww is reset each time
        //      (Fix 5: multi-word conclusion).
        //   4. TWAS              → call rejection (AC-LINK-019-10).
        case ALEState::CALLING:
            if (calling_phase == CallingPhase::LISTENING) {
                if (word.type == WordType::TO && address_book.is_self(addr)) {
                    if (!response_to_detected) {
                        response_to_detected = true;
                        response_rx_start_ms = current_time_ms;
                    }
                } else if (word.type == WordType::TIS
                           && response_to_detected
                           && tlww_start_ms == 0) {
                    // First conclusion word — arm Tlww, start collecting JOE's address.
                    joe_address                  = addr;
                    active_call_from             = addr;
                    tlww_start_ms                = current_time_ms;
                    collecting_remote_conclusion = true;
                } else if (collecting_remote_conclusion
                           && (word.type == WordType::DATA
                               || word.type == WordType::REP)) {
                    // Fix 5: extended address chunk after TIS — append and reset Tlww.
                    std::string chunk(word.address, 3);
                    auto p = chunk.find_last_not_of('@');
                    if (p != std::string::npos) chunk.erase(p + 1);
                    else chunk.clear();
                    joe_address      += chunk;
                    active_call_from  = joe_address;
                    tlww_start_ms     = current_time_ms;   // Tlww reset: wait for next word
                } else if (word.type == WordType::TWAS) {
                    // Call rejected — AC-LINK-019-10
                    if (operator_callback)
                        operator_callback(OperatorEvent::CALL_REJECTED);
                    process_event(ALEEvent::LINK_TIMEOUT);
                }
            }
            break;

        // ── HANDSHAKE (JOE side) ─────────────────────────────────────────
        // WAIT_CYCLE_END: read SAM's conclusion (TIS SAM [DATA]*).
        // WAIT_ACK:       read SAM's ACK frame (TO JOE × 2 + TIS SAM [DATA]*).
        case ALEState::HANDSHAKE:
            if (handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
                if (word.type == WordType::TIS && !hs_conclusion_rcvd) {
                    // First word of SAM's conclusion.
                    caller_address     = addr;
                    active_call_from   = addr;
                    hs_conclusion_rcvd = true;
                    hs_tlww_start_ms   = current_time_ms;
                } else if (hs_conclusion_rcvd
                           && (word.type == WordType::DATA
                               || word.type == WordType::REP)) {
                    // Fix 5: extended caller address — append chunk, reset Tlww.
                    std::string chunk(word.address, 3);
                    auto p = chunk.find_last_not_of('@');
                    if (p != std::string::npos) chunk.erase(p + 1);
                    else chunk.clear();
                    caller_address   += chunk;
                    active_call_from  = caller_address;
                    hs_tlww_start_ms  = current_time_ms;
                } else if (!hs_conclusion_rcvd
                           && (word.type == WordType::DATA
                               || word.type == WordType::REP)
                           && hs_message_start_ms == 0) {
                    // Message section has begun — arm Tmmax (AC-LINK-018-5)
                    hs_message_start_ms = current_time_ms;
                } else if (word.type == WordType::TWAS) {
                    // Calling station is busy / rejected — abort.
                    process_event(ALEEvent::LINK_TIMEOUT);
                }
            } else if (handshake_phase == HandshakePhase::CHANNEL_CHECK) {
                // Any valid word during LBT → channel busy → abort (AC-LINK-019-3)
                std::cout << "[TRACE] process_received_word: channel busy during LBT → LINK_TIMEOUT\n";
                process_event(ALEEvent::LINK_TIMEOUT);
            } else if (handshake_phase == HandshakePhase::WAIT_ACK) {
                // SAM's ACK frame: TO JOE × 2 + TIS SAM [DATA]*
                if (word.type == WordType::TIS
                    && !hs_ack_tis_rcvd
                    && !caller_address.empty()
                    && addr == caller_address.substr(0, 3)) {
                    // SAM's conclusion word — arm Tlww.
                    hs_ack_tis_rcvd  = true;
                    hs_tlww_start_ms = current_time_ms;
                } else if (hs_ack_tis_rcvd
                           && (word.type == WordType::DATA
                               || word.type == WordType::REP)) {
                    // Fix 5: extended SAM address continuation — reset Tlww only.
                    hs_tlww_start_ms = current_time_ms;
                } else if (word.type == WordType::TWAS) {
                    // SAM rejected (e.g. TWAS instead of TIS) — abort.
                    process_event(ALEEvent::LINK_TIMEOUT);
                }
            }
            break;

        default:
            break;
    }

    message_assembler.add_word(word);
}

void ALEStateMachine::update_link_quality(const LinkQuality& lq) {
    uint32_t idx = scan_config.channel_index;
    while (channel_quality.size() <= idx)
        channel_quality.push_back(LinkQuality());
    channel_quality[idx] = lq;

    if (idx < scan_config.scan_list.size()) {
        float score = 100.0f - (lq.fec_errors * 10.0f);
        score = std::max(0.0f, std::min(100.0f, score));
        scan_config.scan_list[idx].lqa_score = score;
    }
}

const Channel* ALEStateMachine::select_best_channel() const {
    if (scan_config.scan_list.empty()) return nullptr;
    const Channel* best = &scan_config.scan_list[0];
    for (const auto& ch : scan_config.scan_list)
        if (ch.lqa_score > best->lqa_score)
            best = &ch;
    return best;
}

void ALEStateMachine::hop_to_next_channel() {
    if (scan_config.scan_list.empty()) return;
    scan_config.channel_index =
        (scan_config.channel_index + 1) % scan_config.scan_list.size();
    set_channel(scan_config.channel_index);
    last_scan_hop_time_ms = current_time_ms;
}

void ALEStateMachine::set_channel(uint32_t index) {
    if (index >= scan_config.scan_list.size()) return;
    scan_config.channel_index = index;
    scan_config.scan_list[index].last_scan_time_ms = current_time_ms;
    if (channel_callback)
        channel_callback(scan_config.scan_list[index]);
}

// ============================================================================
// Timeout helpers
// ============================================================================

bool ALEStateMachine::check_link_timeout() {
    uint32_t timeout_ms = 0;
    switch (current_state) {
        case ALEState::CALLING:   timeout_ms = compute_calling_timeout_ms(); break;
        case ALEState::HANDSHAKE: timeout_ms = ALETimingConstants::Twa_ms;   break;
        case ALEState::LINKED:    timeout_ms = ALETimingConstants::LINK_TIMEOUT_MS; break;
        default: return false;
    }
    return (current_time_ms - state_entry_time_ms) > timeout_ms;
}

uint32_t ALEStateMachine::compute_calling_timeout_ms() const {
    // Tsc = C × 2 × Trw;  Tlc = 2 × wpa × Trw
    const uint32_t tsc = target_scan_channels * 2u * ALETimingConstants::Trw_ms;
    // leading_words_ was pre-computed in initiate_call(); its size equals the
    // number of words needed to transmit active_call_to once (1–5 words).
    const uint32_t tlc = 2u * static_cast<uint32_t>(leading_words_.size())
                            * ALETimingConstants::Trw_ms;
    // Per-channel budget: LBT + Tune + Tsc + Tlc + Twr/Twrt
    const uint32_t twr = (calling_channels.size() > 1)
                         ? ALETimingConstants::Twrt_ms
                         : ALETimingConstants::Twr_ms;
    const uint32_t per_ch = ALETimingConstants::Twt_ms
                          + ALETimingConstants::Tt_ms
                          + tsc + tlc + twr;
    const uint32_t n = calling_channels.empty()
                     ? 1u
                     : static_cast<uint32_t>(calling_channels.size());
    return per_ch * n + 2000u; // 2 s safety margin
}

bool ALEStateMachine::check_scan_dwell_timeout() {
    if (current_state != ALEState::SCANNING) return false;
    return (current_time_ms - last_scan_hop_time_ms) >= scan_config.dwell_time_ms;
}

// ============================================================================
// Multi-channel retry — AC-LINK-017-8
// ============================================================================

void ALEStateMachine::try_next_calling_channel() {
    ++calling_channel_index;

    if (!calling_channels.empty()
        && calling_channel_index < calling_channels.size()) {
        // Hop to next channel and restart from LBT
        if (channel_callback)
            channel_callback(calling_channels[calling_channel_index]);

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
        joe_address.clear();
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

void ALEStateMachine::build_scanning_word() {
    // Transmit scanning_word_ once for the current Trw slot.
    // scanning_word_ holds only the first 3 chars of the destination address
    // (A.5.2.5.1).  DATA/REP extension words are forbidden in the scanning
    // section; the full address is sent only in the leading call.
    // on_word_complete() counts slots and transitions to LEADING_CALL after
    // C × 2 slots.
    transmit_word(scanning_word_);
}

void ALEStateMachine::build_leading_call_word() {
    // Transmit the full leading_words_ sequence once.
    // This function is called twice by the on_word_complete() slot counter
    // (Tlc = 2 × Tc per A.5.5.3.1).  leading_words_ contains 1–5 words
    // depending on the address length: TO + DATA/REP alternation.
    transmit_words(leading_words_);
}

void ALEStateMachine::build_conclusion_words() {
    // Transmit conclusion_words_ once.
    // conclusion_words_ encodes the own address with TIS preamble
    // (A.5.2.3.2.2), same DATA/REP scheme as leading_words_.
    transmit_words(conclusion_words_);
}

void ALEStateMachine::build_ack_words() {
    // ACK frame per REQ-LINK-008 / A.5.5.3.4 / Figure A-31:
    //   TO [joe_address] × 2 + TIS [own addr]
    // joe_address is set during the LISTENING phase (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    const auto joe_words = AddressEncoder::encode(joe_address, WordType::TO);
    transmit_words(joe_words);   // pass 1
    transmit_words(joe_words);   // pass 2
    transmit_words(AddressEncoder::encode(address_book.get_self_address(), WordType::TIS));
}

void ALEStateMachine::build_response_words() {
    // Response frame per A.5.5.3.3 / Figure A-30 — mirrors ACK (roles inverted):
    //   TO [caller_address] × 2 + TIS [own addr]
    // caller_address is set during WAIT_CYCLE_END (process_received_word),
    // so it is encoded here at send time, not pre-computed.
    const auto caller_words = AddressEncoder::encode(caller_address, WordType::TO);
    transmit_words(caller_words);   // pass 1
    transmit_words(caller_words);   // pass 2
    transmit_words(AddressEncoder::encode(address_book.get_self_address(), WordType::TIS));
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
    // ── CALLING path (SAM side) ───────────────────────────────────────────
    if (current_state == ALEState::CALLING) {
        --words_pending;
        ++call_cycle_count;
        ++call_cycles_in_phase;

        switch (calling_phase) {

            case CallingPhase::SCANNING_CALL: {
                const uint32_t tsc_slots = target_scan_channels * 2u;
                if (call_cycles_in_phase >= tsc_slots) {
                    std::cout << "[TRACE] on_word_complete: SCANNING_CALL → LEADING_CALL"
                              << " (tsc_slots=" << tsc_slots << ")\n";
                    calling_phase        = CallingPhase::LEADING_CALL;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::LEADING_CALL: {
                // leading_words_ pre-computed in initiate_call(); its size equals
                // the word count for active_call_to.  Tlc = 2 × Tc = 2 × wpa × Trw.
                const uint32_t tlc_slots = 2u * static_cast<uint32_t>(leading_words_.size());
                if (call_cycles_in_phase >= tlc_slots) {
                    std::cout << "[TRACE] on_word_complete: LEADING_CALL → "
                              << (pending_message.type != PendingMessage::Type::NONE
                                  ? "MESSAGE" : "CONCLUSION")
                              << " (tlc_slots=" << tlc_slots << ")\n";
                    calling_phase = (pending_message.type != PendingMessage::Type::NONE)
                                    ? CallingPhase::MESSAGE
                                    : CallingPhase::CONCLUSION;
                    call_cycles_in_phase = 0;
                }
                break;
            }

            case CallingPhase::MESSAGE: {
                // Stub: MESSAGE phase completes immediately (no words sent).
                // handle_calling() transitions MESSAGE → CONCLUSION synchronously.
                break;
            }

            case CallingPhase::CONCLUSION: {
                // conclusion_words_ pre-computed in initiate_call().
                const uint32_t conclusion_slots =
                    static_cast<uint32_t>(conclusion_words_.size());
                if (call_cycles_in_phase >= conclusion_slots) {
                    std::cout << "[TRACE] on_word_complete: CONCLUSION → LISTENING\n";
                    calling_phase        = CallingPhase::LISTENING;
                    listening_start_ms   = current_time_ms;
                    call_cycles_in_phase = 0;
                    if (rx_enabled_callback)
                        rx_enabled_callback(true);
                }
                break;
            }

            case CallingPhase::SENDING_ACK: {
                // ACK frame (Figure A-31): TO [joe_address] × 2 + TIS [self]
                // joe_address is set during the LISTENING phase; encode here to
                // get the exact word count matching build_ack_words().
                const uint32_t ack_slots =
                    2u * static_cast<uint32_t>(
                             AddressEncoder::encode(joe_address, WordType::TO).size())
                    + static_cast<uint32_t>(conclusion_words_.size());
                if (call_cycles_in_phase >= ack_slots) {
                    std::cout << "[TRACE] on_word_complete: SENDING_ACK → LINKED\n";
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

        // Response frame (Figure A-30): TO [caller_address] × 2 + TIS [self]
        // JOE never calls initiate_call(), so no pre-computed vectors exist;
        // encode here to get the exact word counts matching build_response_words().
        const uint32_t resp_slots =
            2u * static_cast<uint32_t>(
                     AddressEncoder::encode(caller_address, WordType::TO).size())
            + static_cast<uint32_t>(
                     AddressEncoder::encode(address_book.get_self_address(),
                                            WordType::TIS).size());
        if (hs_words_in_phase >= resp_slots) {
            std::cout << "[TRACE] on_word_complete: SENDING_RESPONSE → WAIT_ACK\n";
            handshake_phase  = HandshakePhase::WAIT_ACK;
            hs_ack_start_ms  = current_time_ms;
            hs_tlww_start_ms = 0;
            hs_ack_tis_rcvd  = false;
            if (rx_enabled_callback) rx_enabled_callback(true);
        }
    }
}

} // namespace ale
