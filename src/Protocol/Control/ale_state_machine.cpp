/**
 * \file ale_state_machine.cpp
 * \brief Implementation of ALE state machine
 */

#include "Protocol/Control/ale_state_machine.h"
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

// ============================================================================
// Constructor
// ============================================================================

ALEStateMachine::ALEStateMachine()
    : current_state(ALEState::IDLE),
      previous_state(ALEState::IDLE),
      link_start_time_ms(0),
      last_word_time_ms(0),
      calling_phase(CallingPhase::SCANNING_CALL),
      active_call_is_net(false),
      call_phase_start_ms(0),
      first_call_tx_ms(0),
      call_cycle_count(0),
      call_cycles_in_phase(0),
      target_scan_channels(1),
      state_entry_time_ms(0),
      last_scan_hop_time_ms(0),
      current_time_ms(0)
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
            link_start_time_ms   = current_time_ms;
            first_call_tx_ms     = current_time_ms;  // Phasenreferenz — bleibt konstant
            call_cycle_count     = 0;
            call_cycles_in_phase = 0;
            call_phase_start_ms  = current_time_ms;

            // Net calls: go directly to net call stub.
            // Individual calls: start with scanning call if target may be scanning,
            // or skip directly to leading call if target is known on fixed channel.
            if (active_call_is_net) {
                calling_phase = CallingPhase::NET_CALL_STUB;
            } else if (target_scan_channels > 0) {
                calling_phase = CallingPhase::SCANNING_CALL;
            } else {
                calling_phase = CallingPhase::LEADING_CALL;
            }
            break;

        case ALEState::HANDSHAKE:
            link_start_time_ms = current_time_ms;
            break;

        case ALEState::LINKED:
            link_start_time_ms = current_time_ms;
            last_word_time_ms  = current_time_ms;
            break;

        case ALEState::SOUNDING:
            if (!address_book.get_self_address().empty()) {
                // Full own address including DATA/REP extension if > 3 chars
                transmit_address_words(WordType::TIS,
                                       address_book.get_self_address());
            }
            break;

        default:
            break;
    }
}

void ALEStateMachine::exit_state(ALEState old_state) {
    switch (old_state) {
        case ALEState::CALLING:
            // Ensure RX window is closed when leaving CALLING for any reason
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
 *  SCANNING_CALL:
 *    Send TO first word only, every Trw.
 *    Duration = target_scan_channels × 2 × Trw = Tsc.
 *    Per spec: "scanning call section shall contain only the first word of
 *    the called station address" — no DATA/REP extension in this phase.
 *
 *  LEADING_CALL:
 *    Send full TO address sequence exactly twice (Tlc = 2 × Tc).
 *    Tc = words_for_address(addr) × Trw — grows with address length.
 *    Per spec: "entire called station address ... sent twice."
 *
 *  CONCLUSION:
 *    Send TIS with full own address (including DATA/REP if > 3 chars).
 *    Per A.5.2.3.2.2: terminates frame, invites response.
 *    Then open RX window (LISTENING).
 *
 *  LISTENING:
 *    Wait Twr for called station's TIS response.
 *    Response arrives via process_received_word() → HANDSHAKE_COMPLETE.
 *    No response within Twr → channel attempt failed.
 *    Single channel: LINK_TIMEOUT → IDLE.
 *    Multi-channel: TODO — hop to next channel and retry.
 *
 *  NET_CALL_STUB:
 *    TODO: implement per A.5.5.x
 */
void ALEStateMachine::handle_calling() {
    uint32_t phase_elapsed = current_time_ms - call_phase_start_ms;

    // ── Diagnostic trace ─────────────────────────────────────────────────
    static const char* PHASE_NAMES[] = {
        "SCANNING_CALL", "LEADING_CALL", "CONCLUSION", "LISTENING", "NET_CALL_STUB"
    };
    std::cout << "[TRACE] t=" << current_time_ms
              << " phase=" << PHASE_NAMES[static_cast<int>(calling_phase)]
              << " elapsed=" << phase_elapsed
              << " cycles=" << call_cycles_in_phase
              << " total_words=" << call_cycle_count
              << " addr=" << active_call_to
              << "\n";

    switch (calling_phase) {

        // ── SCANNING_CALL ─────────────────────────────────────────────────
        // Per A.5.2.5.1: scanning call contains only the FIRST WORD of each
        // address — one TO per slot, no DATA/REP extension (A.5.2.4.3).
        // Slot width = 1 × Trw.  Duration: Tsc = C × 2 × Trw.
        case CallingPhase::SCANNING_CALL: {
            const uint32_t tsc = target_scan_channels
                               * 2 * ALETimingConstants::Trw_ms;

            // Transition first — prevents extra word at phase boundary
            if (phase_elapsed >= tsc) {
                std::cout << "[TRACE] SCANNING_CALL → LEADING_CALL"
                          << " (tsc=" << tsc << "ms)\n";
                calling_phase        = CallingPhase::LEADING_CALL;
                call_phase_start_ms  = current_time_ms;
                call_cycles_in_phase = 0;
                break;
            }

            const uint32_t next_tx = call_phase_start_ms
                                   + call_cycles_in_phase
                                   * ALETimingConstants::Trw_ms;

            if (current_time_ms >= next_tx) {
                build_scanning_word(active_call_to);
                ++call_cycles_in_phase;
                ++call_cycle_count;
            }
            break;
        }

        // ── LEADING_CALL ──────────────────────────────────────────────────
        // Per A.5.5.3.1 + Figure A-29: full TO address, sent exactly twice.
        // Tlc = 2 × Tc.  Tc = words_for_address × Trw.
        // call_cycles_in_phase counts complete address sequences (0 → 1 → 2).
        //
        // Transition guard: phase_elapsed >= 2 × tc_ms, NOT call_cycles >= 2.
        // Seq 1 fires at tc_ms; its last word finishes at 2 × tc_ms = Tlc.
        // Transitioning on call_cycles >= 2 would move to CONCLUSION the moment
        // seq 1 is *triggered*, before it has been on air — timing short by tc_ms.
        case CallingPhase::LEADING_CALL: {
            const uint32_t wpa   = words_for_address(active_call_to);
            const uint32_t tc_ms = wpa * ALETimingConstants::Trw_ms;

            // Guard: should never exceed 2 sequences in leading call
            if (call_cycles_in_phase > 2) {
                std::cout << "[ERROR] LEADING_CALL OVER-EMISSION:"
                          << " call_cycles_in_phase=" << call_cycles_in_phase
                          << " (expected 0..2)\n";
            }

            // Trigger each sequence at its scheduled slot
            const uint32_t next_tx = call_phase_start_ms
                                   + call_cycles_in_phase * tc_ms;
            if (current_time_ms >= next_tx && call_cycles_in_phase < 2) {
                std::cout << "[TRACE] LEADING_CALL seq " << call_cycles_in_phase
                          << " tx (next_tx=" << next_tx << "ms)\n";
                build_leading_call_word(active_call_to, active_call_is_net);
                ++call_cycles_in_phase;
                call_cycle_count += wpa;
            }

            // Wait until the last sequence has fully been on air (Tlc = 2 × Tc)
            if (phase_elapsed >= 2 * tc_ms) {
                std::cout << "[TRACE] LEADING_CALL → CONCLUSION"
                          << " (tlc=" << 2 * tc_ms << "ms)\n";
                calling_phase        = CallingPhase::CONCLUSION;
                call_phase_start_ms  = current_time_ms;
                call_cycles_in_phase = 0;
            }
            break;
        }

        // ── CONCLUSION ────────────────────────────────────────────────────
        // Per A.5.2.3.2.2: TIS SAM — identifies caller, invites response.
        // Full address including DATA/REP if own address > 3 chars.
        // Sent once, then open RX window after last word is on air.
        case CallingPhase::CONCLUSION: {
            if (call_cycles_in_phase == 0) {
                std::cout << "[TRACE] CONCLUSION tx\n";
                build_conclusion_words();
                const uint32_t wpa = words_for_address(
                                         address_book.get_self_address());
                call_cycles_in_phase = 1;
                call_cycle_count    += wpa;
            }

            // Open RX window after full conclusion sequence has been on air.
            // Use actual word count × Trw, not a fixed single-word constant.
            const uint32_t conclusion_air_ms =
                words_for_address(address_book.get_self_address())
                * ALETimingConstants::Trw_ms;

            if (phase_elapsed >= conclusion_air_ms) {
                std::cout << "[TRACE] CONCLUSION → LISTENING"
                          << " (air=" << conclusion_air_ms << "ms)\n";
                calling_phase       = CallingPhase::LISTENING;
                call_phase_start_ms = current_time_ms;
                if (rx_enabled_callback)
                    rx_enabled_callback(true);
            }
            break;
        }

        // ── LISTENING ─────────────────────────────────────────────────────
        // Twr: wait for called station's TIS (accept) or TWAS (reject).
        // Response arrives via process_received_word() → HANDSHAKE_COMPLETE.
        // Timeout: channel attempt failed.
        // NOTE: do NOT call rx_enabled_callback(false) here explicitly —
        // process_event(LINK_TIMEOUT) → transition_to(IDLE) → exit_state(CALLING)
        // will fire it exactly once.
        // TODO: multi-channel → hop to next channel and restart from SCANNING_CALL.
        case CallingPhase::LISTENING: {
            if (phase_elapsed >= ALETimingConstants::Twr_ms) {
                std::cout << "[TRACE] LISTENING timeout → LINK_TIMEOUT"
                          << " (twr=" << ALETimingConstants::Twr_ms
                          << "ms)\n";
                process_event(ALEEvent::LINK_TIMEOUT);
            }
            break;
        }

        // ── NET_CALL_STUB ──────────────────────────────────────────────────
        // TODO: implement net call protocol per MIL-STD-188-141B A.5.5.x
        case CallingPhase::NET_CALL_STUB:
            break;
    }
}

void ALEStateMachine::handle_handshake() {
    // Timeout handled in update()
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

    active_call_to     = to_address;
    active_call_from   = address_book.get_self_address();
    active_call_is_net = false;

    // active_call_is_net must be set before process_event() so
    // enter_state(CALLING) can select the correct starting CallingPhase.
    return process_event(ALEEvent::CALL_REQUEST);
}

bool ALEStateMachine::initiate_net_call(const std::string& net_address) {
    if (current_state != ALEState::IDLE && current_state != ALEState::SCANNING)
        return false;

    active_call_to     = net_address;
    active_call_from   = address_book.get_self_address();
    active_call_is_net = true;

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

void ALEStateMachine::process_received_word(const ALEWord& word) {
    if (!word.valid) return;

    last_word_time_ms = current_time_ms;

    LinkQuality lq;
    lq.fec_errors   = word.fec_errors;
    lq.total_words  = 1;
    lq.timestamp_ms = current_time_ms;
    update_link_quality(lq);

    std::string addr(word.address, 3);
    auto trim = addr.find_last_not_of(' ');
    if (trim != std::string::npos) addr.erase(trim + 1);

    switch (current_state) {

        case ALEState::SCANNING:
            // Incoming call: TO or TWAS addressed to us
            if (word.type == WordType::TO || word.type == WordType::TWAS) {
                if (address_book.is_self(addr)) {
                    active_call_to = addr;
                    process_event(ALEEvent::CALL_DETECTED);
                }
            }
            break;

        case ALEState::CALLING:
            // Only accept responses during the LISTENING window.
            // Per A.5.5.3.1: called station replies with TIS (accept)
            // or TWAS (reject) containing the calling station's address.
            // NOTE: rx_enabled_callback(false) is NOT called explicitly here —
            // the subsequent process_event() triggers exit_state(CALLING) which
            // fires the callback exactly once.
            if (calling_phase == CallingPhase::LISTENING) {
                if (word.type == WordType::TIS) {
                    if (address_book.is_self(addr)) {
                        active_call_from = addr;
                        process_event(ALEEvent::HANDSHAKE_COMPLETE);
                    }
                } else if (word.type == WordType::TWAS) {
                    // Called station explicitly rejected the call
                    if (address_book.is_self(addr))
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

bool ALEStateMachine::check_link_timeout() {
    uint32_t timeout_ms = 0;
    switch (current_state) {
        case ALEState::CALLING:
        case ALEState::HANDSHAKE: timeout_ms = ALETimingConstants::Twa_ms; break;
        case ALEState::LINKED:    timeout_ms = ALETimingConstants::LINK_TIMEOUT_MS; break;
        default: return false;
    }
    return (current_time_ms - state_entry_time_ms) > timeout_ms;
}

bool ALEStateMachine::check_scan_dwell_timeout() {
    if (current_state != ALEState::SCANNING) return false;
    return (current_time_ms - last_scan_hop_time_ms) >= scan_config.dwell_time_ms;
}

// ============================================================================
// Multi-word address helpers
// ============================================================================

// static
std::vector<std::string> ALEStateMachine::chunk_address(const std::string& addr) {
    std::vector<std::string> chunks;

    // Clamp to 15 chars (5 words × 3 chars) per A.5.2.3.2.1
    const size_t len = std::min(addr.size(), size_t(15));

    for (size_t i = 0; i < len; i += 3) {
        std::string chunk = addr.substr(i, std::min(size_t(3), len - i));
        // A.5.2.4.3: pad trailing positions with '@' (utility symbol, 0x40)
        while (chunk.size() < 3)
            chunk += '@';
        chunks.push_back(chunk);
    }

    // Empty address fallback
    if (chunks.empty())
        chunks.push_back("@@@");

    return chunks;
}

// static
uint32_t ALEStateMachine::words_for_address(const std::string& addr) {
    const size_t len = std::min(addr.size(), size_t(15));
    // ceil(len / 3), minimum 1
    return static_cast<uint32_t>(std::max(size_t(1), (len + 2) / 3));
}

void ALEStateMachine::transmit_address_words(WordType first_type,
                                             const std::string& addr) {
    const auto chunks = chunk_address(addr);

    // Word type sequence per A.5.2.3.2.1 / A.5.2.3.2.2:
    //   chunk[0] → first_type       (TO / TIS / TWAS)
    //   chunk[1] → DATA             (A.5.2.3.4.1: extends previous word type)
    //   chunk[2] → REP              (A.5.2.3.4.2: duplicates preamble, new data)
    //   chunk[3] → DATA
    //   chunk[4] → REP              (maximum — 15 chars total)
    //
    // REP must not follow TIS/TWAS directly — DATA is always the first extension.
    // AC-WORD-010-2/3: preamble must differ between consecutive words — enforced
    // here by the alternating DATA/REP pattern.
    static constexpr WordType extension_types[] = {
        WordType::DATA, WordType::REP,
        WordType::DATA, WordType::REP
    };
    static_assert(extension_types[0] != extension_types[1] &&
                  extension_types[1] != extension_types[2] &&
                  extension_types[2] != extension_types[3],
                  "Extension word preambles must alternate (AC-WORD-010-2/3)");

    for (size_t i = 0; i < chunks.size(); ++i) {
        ALEWord word = ALEWord();
        word.type        = (i == 0) ? first_type : extension_types[i - 1];
        word.address[0]  = chunks[i][0];
        word.address[1]  = chunks[i][1];
        word.address[2]  = chunks[i][2];
        word.address[3]  = '\0';
        word.valid        = true;
        word.timestamp_ms = current_time_ms;
        transmit_word(word);
    }
}

// ============================================================================
// Word builders — one per calling phase
// ============================================================================

void ALEStateMachine::build_scanning_word(const std::string& to_addr) {
    // Per A.5.2.5.1: "The scanning call shall be composed of TO words...
    // which contain only the first word(s) of the called station(s) address."
    // Per A.5.2.4.3: Stuff-1 and Stuff-2 words "should appear only in the
    // leading call (Tlc)" — stuffed DATA words are therefore excluded from Tsc.
    // Conclusion: one TO word per scanning slot, no DATA/REP extension,
    // regardless of address length.
    ALEWord word = ALEWord();
    word.type        = WordType::TO;
    const auto chunks = chunk_address(to_addr);
    word.address[0]  = chunks[0][0];
    word.address[1]  = chunks[0][1];
    word.address[2]  = chunks[0][2];
    word.address[3]  = '\0';
    word.valid        = true;
    word.timestamp_ms = current_time_ms;
    transmit_word(word);
}

void ALEStateMachine::build_leading_call_word(const std::string& to_addr,
                                              bool is_net) {
    // Per A.5.5.3.1: "entire called station address shall be used in
    // leading call section" — full DATA/REP chain for addresses > 3 chars.
    const WordType first = is_net ? WordType::TWAS : WordType::TO;
    transmit_address_words(first, to_addr);
}

void ALEStateMachine::build_conclusion_words() {
    // Per A.5.2.3.2.2: TIS with full own address — full DATA/REP chain
    // for own addresses > 3 chars.
    transmit_address_words(WordType::TIS, address_book.get_self_address());
}

void ALEStateMachine::transmit_word(const ALEWord& word) {
    // A.5.2.2.4: each logical word must be sent three times (3× redundancy).
    // The physical layer (ALE2GModem) is responsible for the repetition:
    // it calls on_tw_tick() once per Tw ≈ 130.7 ms and sends one copy per
    // tick, so three ticks fill exactly one Trw = 392 ms.
    if (transmit_callback)
        transmit_callback(word);
}

} // namespace ale