/**
 * \file test_state_machine.cpp
 * \brief Unit tests for ALE state machine (Phase 3)
 * 
 * Tests:
 *  1. State transitions
 *  2. Channel management and scanning
 *  3. Call initiation
 *  4. Incoming call handling
 *  5. LQA (Link Quality Analysis)
 *  6. Timeout handling
 */

#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with fixed-size ALE address fields
#endif

namespace ale {

// Test helper: Track state changes
class StateTracker {
public:
    std::vector<std::pair<ALEState, ALEState>> transitions;
    
    void record(ALEState from, ALEState to) {
        transitions.push_back({from, to});
    }
    
    void clear() {
        transitions.clear();
    }
    
    bool had_transition(ALEState from, ALEState to) const {
        for (const auto& t : transitions) {
            if (t.first == from && t.second == to) {
                return true;
            }
        }
        return false;
    }
};

// Test helper: Track transmitted words
class WordTracker {
public:
    std::vector<ALEWord> words;
    
    void record(const ALEWord& word) {
        words.push_back(word);
    }
    
    void clear() {
        words.clear();
    }
    
    size_t count() const {
        return words.size();
    }
};

// Test helper: Track channel changes
class ChannelTracker {
public:
    std::vector<uint32_t> frequencies;
    
    void record(const Channel& ch) {
        frequencies.push_back(ch.rx_frequency_hz);
    }
    
    void clear() {
        frequencies.clear();
    }
    
    size_t count() const {
        return frequencies.size();
    }
};

// ============================================================================
// Test 1: Basic State Transitions
// ============================================================================

bool test_state_transitions() {
    std::cout << "\n[TEST 1] State Transitions\n";
    std::cout << "==========================\n";
    
    ALEStateMachine sm;
    StateTracker tracker;
    
    sm.set_state_callback([&tracker](ALEState from, ALEState to) {
        tracker.record(from, to);
    });
    
    // Test 1: IDLE -> SCANNING
    std::cout << "  IDLE -> SCANNING: ";
    bool changed = sm.process_event(ALEEvent::START_SCAN);
    bool correct = (changed && sm.get_state() == ALEState::SCANNING);
    std::cout << (correct ? "PASS" : "FAIL") << "\n";
    if (!correct) return false;
    
    // Test 2: SCANNING -> IDLE
    std::cout << "  SCANNING -> IDLE: ";
    changed = sm.process_event(ALEEvent::STOP_SCAN);
    correct = (changed && sm.get_state() == ALEState::IDLE);
    std::cout << (correct ? "PASS" : "FAIL") << "\n";
    if (!correct) return false;
    
    // Test 3: IDLE -> CALLING
    std::cout << "  IDLE -> CALLING: ";
    changed = sm.process_event(ALEEvent::CALL_REQUEST);
    correct = (changed && sm.get_state() == ALEState::CALLING);
    std::cout << (correct ? "PASS" : "FAIL") << "\n";
    if (!correct) return false;
    
    // Test 4: CALLING -> LINKED
    std::cout << "  CALLING -> LINKED: ";
    changed = sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);
    correct = (changed && sm.get_state() == ALEState::LINKED);
    std::cout << (correct ? "PASS" : "FAIL") << "\n";
    if (!correct) return false;
    
    // Test 5: LINKED -> IDLE
    std::cout << "  LINKED -> IDLE: ";
    changed = sm.process_event(ALEEvent::LINK_TERMINATED);
    correct = (changed && sm.get_state() == ALEState::IDLE);
    std::cout << (correct ? "PASS" : "FAIL") << "\n";
    if (!correct) return false;
    
    std::cout << "PASS: All state transitions\n";
    return true;
}

// ============================================================================
// Test 2: Channel Scanning
// ============================================================================

bool test_channel_scanning() {
    std::cout << "\n[TEST 2] Channel Scanning\n";
    std::cout << "=========================\n";
    
    ALEStateMachine sm;
    ChannelTracker tracker;
    
    sm.set_channel_callback([&tracker](const Channel& ch) {
        tracker.record(ch);
    });
    
    // Configure scan list with 3 channels
    ScanConfig config;
    config.scan_list.push_back(Channel(7100000, "USB"));  // 7.1 MHz
    config.scan_list.push_back(Channel(14100000, "USB")); // 14.1 MHz
    config.scan_list.push_back(Channel(21100000, "USB")); // 21.1 MHz
    config.dwell_time_ms = 100;  // Fast scanning for test
    
    sm.configure_scan(config);
    
    // Start scanning
    sm.process_event(ALEEvent::START_SCAN);
    
    std::cout << "  Configured 3 channels: ";
    std::cout << (config.scan_list.size() == 3 ? "PASS" : "FAIL") << "\n";
    
    // Simulate time passing and channel hopping
    uint32_t time_ms = 0;
    tracker.clear();
    
    for (int i = 0; i < 10; ++i) {
        time_ms += 50;  // 50ms increments
        sm.update(time_ms);
    }
    
    std::cout << "  Channel hopping count: " << tracker.count();
    bool hopped = (tracker.count() >= 3);  // Should hop multiple times
    std::cout << (hopped ? " PASS" : " FAIL") << "\n";
    
    return hopped;
}

// ============================================================================
// Test 3: Call Initiation
// ============================================================================

bool test_call_initiation() {
    std::cout << "\n[TEST 3] Call Initiation\n";
    std::cout << "========================\n";
    
    ALEStateMachine sm;
    WordTracker tracker;
    
    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });
    
    // Configure self address
    AddressBook book;
    book.set_self_address("W1AW");
    
    // Initiate individual call to K6KB
    std::cout << "  Initiating individual call: ";
    bool success = sm.initiate_call("K6KB");

    // Drive through LBT (Twt_ms=784ms) and Tune (Tt_ms=1045ms) before TX.
    // TX begins after both delays; first_call_tx_ms is set at Tune-expiry.
    const uint32_t tx_start = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    sm.update(ALETimingConstants::Twt_ms); // t=784:   LBT → TUNING
    sm.update(tx_start);                   // t=1829:  TUNING → SCANNING_CALL
    sm.update(tx_start);                   // t=1829:  1st TO word → transmit_callback
    sm.on_word_complete();                 // ack slot 0 → call_cycle_count=1
    sm.update(tx_start + ALETimingConstants::Trw_ms); // t=2221: 2nd TO word
    sm.on_word_complete();                 // ack slot 1

    bool correct_state = (sm.get_state() == ALEState::CALLING);
    bool words_sent = (tracker.count() >= 2);

    bool pass = success && correct_state && words_sent;
    std::cout << (pass ? "PASS" : "FAIL");
    if (!pass) {
        std::cout << " (state=" << ALEStateMachine::state_name(sm.get_state())
                  << ", words=" << tracker.count() << ")";
    }
    std::cout << "\n";

    if (!words_sent) return false;

    std::cout << "  Word 1 (TO): ";
    bool word1_ok = (tracker.words[0].type == PreambleType::TO);
    std::cout << (word1_ok ? "PASS" : "FAIL") << "\n";

    std::cout << "  Word 2 (TO): ";
    bool word2_ok = (tracker.words[1].type == PreambleType::TO);
    std::cout << (word2_ok ? "PASS" : "FAIL") << "\n";

    return pass && word1_ok && word2_ok;
}

// ============================================================================
// Test 4: Incoming Call Detection
// ============================================================================

bool test_incoming_call() {
    std::cout << "\n[TEST 4] Incoming Call Detection (AC-GEN-001-003)\n";
    std::cout << "==================================================\n";

    ALEStateMachine sm;
    sm.set_self_address("W1AW");   // address must live on sm, not a detached AddressBook

    sm.process_event(ALEEvent::START_SCAN);
    std::cout << "  State before word: " << ALEStateMachine::state_name(sm.get_state()) << "\n";

    // --- Arrange ---
    // TO word carrying first 3 chars of our address (per A.5.2.5.1 prefix-match rule).
    ALEWord to_word;
    to_word.type = PreambleType::TO;
    strncpy(to_word.address, "W1A", 3);
    to_word.valid        = true;
    to_word.timestamp_ms = 1000;

    // --- Act ---
    // process_received_word() must detect the address match internally and
    // fire CALL_DETECTED on its own, transitioning SCANNING → HANDSHAKE.
    sm.process_received_word(to_word);

    // --- Assert ---
    // No manual CALL_DETECTED here — that would defeat the test.
    bool in_handshake = (sm.get_state() == ALEState::HANDSHAKE);
    std::cout << "  Auto-transition SCANNING→HANDSHAKE on TO \"W1A\": "
              << (in_handshake ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    return in_handshake;
}

// ============================================================================
// Test 5: Link Quality Analysis (LQA)
// ============================================================================

bool test_lqa() {
    std::cout << "\n[TEST 5] Link Quality Analysis\n";
    std::cout << "===============================\n";
    
    ALEStateMachine sm;
    
    // Configure channels
    ScanConfig config;
    config.scan_list.push_back(Channel(7100000, "USB"));
    config.scan_list.push_back(Channel(14100000, "USB"));
    config.scan_list.push_back(Channel(21100000, "USB"));
    
    sm.configure_scan(config);
    sm.process_event(ALEEvent::START_SCAN);
    
    // Simulate different quality on each channel
    LinkQuality lq1;
    lq1.snr_db = 20.0f;
    lq1.fec_errors = 0;
    lq1.total_words = 10;
    
    sm.update_link_quality(lq1);  // Channel 0: Good
    
    // Move to next channel
    sm.update(200);
    
    LinkQuality lq2;
    lq2.snr_db = 10.0f;
    lq2.fec_errors = 2;
    lq2.total_words = 10;
    
    sm.update_link_quality(lq2);  // Channel 1: Poor
    
    // Select best channel
    const Channel* best = sm.select_best_channel();
    
    std::cout << "  Best channel selection: ";
    bool pass = (best != nullptr && best->rx_frequency_hz == 7100000);
    std::cout << (pass ? "PASS" : "FAIL");
    if (best) {
        std::cout << " (" << best->rx_frequency_hz << " Hz, score=" 
                  << best->lqa_score << ")";
    }
    std::cout << "\n";
    
    return pass;
}

// ============================================================================
// Test 6: Timeout Handling
// ============================================================================

bool test_timeouts() {
    std::cout << "\n[TEST 6] Timeout Handling\n";
    std::cout << "=========================\n";
    
    ALEStateMachine sm;
    StateTracker tracker;
    
    sm.set_state_callback([&tracker](ALEState from, ALEState to) {
        tracker.record(from, to);
    });
    
    // Test call timeout
    std::cout << "  Call timeout: ";
    sm.process_event(ALEEvent::CALL_REQUEST);
    
    // Simulate timeout
    uint32_t timeout = ALETimingConstants::Twa_ms + 1000;
    sm.update(timeout);
    
    bool timed_out = (sm.get_state() == ALEState::IDLE);
    std::cout << (timed_out ? "PASS" : "FAIL");
    std::cout << " (final state: " << ALEStateMachine::state_name(sm.get_state()) << ")\n";
    
    return timed_out;
}

// ============================================================================
// Test 7: Sounding
// ============================================================================

bool test_sounding() {
    std::cout << "\n[TEST 7] Sounding Transmission\n";
    std::cout << "==============================\n";
    
    ALEStateMachine sm;
    WordTracker tracker;
    
    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });
    
    // Send sounding
    std::cout << "  Initiating sounding: ";
    bool success = sm.send_sounding();
    
    bool in_sounding = (sm.get_state() == ALEState::SOUNDING);
    bool word_sent = (tracker.count() >= 1);
    
    bool pass = success && in_sounding;
    std::cout << (pass ? "PASS" : "FAIL");
    std::cout << " (state=" << ALEStateMachine::state_name(sm.get_state())
              << ", words=" << tracker.count() << ")\n";
    
    if (word_sent) {
        std::cout << "  TIS word sent: ";
        bool is_tis = (tracker.words[0].type == PreambleType::TIS);
        std::cout << (is_tis ? "PASS" : "FAIL") << "\n";
        pass = pass && is_tis;
    }
    
    // Simulate sounding complete
    sm.update(ALETimingConstants::Trw_ms + 100);
    
    std::cout << "  Sounding complete: ";
    bool returned_to_scan = (sm.get_state() == ALEState::SCANNING);
    std::cout << (returned_to_scan ? "PASS" : "FAIL") << "\n";
    
    return pass && returned_to_scan;
}

// ============================================================================
// Test 8: Complete Scanning Call Cycle
// Traces the full SAM-side call sequence per A.5.5.3.1:
//   LBT → TUNING → SCANNING_CALL → LEADING_CALL → CONCLUSION → LISTENING
//   → (no response within Twr) → IDLE
// Addresses: SAM (self, 1 word) calling JOE (target, 1 word),
//            target_scan_channels = 1 (default).
// ============================================================================

bool test_full_call_cycle() {
    std::cout << "\n[TEST 8] Full Scanning Call Cycle\n";
    std::cout << "==================================\n";

    ALEStateMachine sm;

    // ── Collect state transitions ─────────────────────────────────────────
    struct Event { std::string kind; uint32_t t_ms; std::string detail; };
    std::vector<Event> log;

    sm.set_state_callback([&](ALEState from, ALEState to) {
        log.push_back({"STATE", 0,
            std::string(ALEStateMachine::state_name(from)) + " → " +
            ALEStateMachine::state_name(to)});
    });

    // ── Collect transmitted words ─────────────────────────────────────────
    struct SentWord { PreambleType type; std::string addr; };
    std::vector<SentWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w) {
        sent.push_back({w.type, std::string(w.address, 3)});
    });

    // ── Operator events ───────────────────────────────────────────────────
    OperatorEvent last_op_event{};
    bool op_event_fired = false;
    sm.set_operator_callback([&](OperatorEvent e) {
        last_op_event  = e;
        op_event_fired = true;
        const char* names[] = {"CALL_REJECTED","NO_CHANNELS_LEFT",
                               "LINK_ESTABLISHED","EMERGENCY_ACTIVE"};
        log.push_back({"OPER", 0, names[static_cast<int>(e)]});
    });

    sm.set_self_address("SAM");

    // ── Constants for this run ────────────────────────────────────────────
    const uint32_t Twt     = ALETimingConstants::Twt_ms;     // 784 ms
    const uint32_t Tt      = ALETimingConstants::Tt_ms;      // 1045 ms
    const uint32_t Trw     = ALETimingConstants::Trw_ms;     // 392 ms
    // LISTENING(a) timeout = Twrt_slow + Tdrw + (Tdrw − Tlww) = 1960 + 784 + 392 = 3136 ms.
    // Covers the responder's turnaround (conclusion settle Tdrw + CHANNEL_CHECK LBT +
    // first word), the SW-decoder detect window, and round-trip audio latency (A.5.5.3.1).
    const uint32_t Tlisten = static_cast<uint32_t>(0.5 + ale::Twrt_slow_ms)
                           + static_cast<uint32_t>(ale::Tdrw_ms)
                           + (ALETimingConstants::Tdrw_ms - ALETimingConstants::Tlww_ms);  // 3136 ms

    const uint32_t tx0 = Twt + Tt; // first TX slot: 1829 ms

    // send_slot: advance to time t, send one word, ack it
    auto send_slot = [&](uint32_t t) {
        sm.update(t);
        sm.on_word_complete();
    };

    // ── Drive the state machine ───────────────────────────────────────────
    sm.initiate_call("JOE");             // → CALLING/LBT

    sm.update(Twt);                      // LBT (784 ms) → TUNING
    sm.update(tx0);                      // Tune (1045 ms) → SCANNING_CALL
    send_slot(tx0);                      // slot 0: TO "JOE" (scan)
    send_slot(tx0 + 1 * Trw);           // slot 1: TO "JOE" (scan) → LEADING_CALL
    send_slot(tx0 + 2 * Trw);           // slot 2: TO "JOE" (leading pass 1)
    send_slot(tx0 + 3 * Trw);           // slot 3: TO "JOE" (leading pass 2) → CONCLUSION
    send_slot(tx0 + 4 * Trw);           // slot 4: TIS "SAM" (conclusion) → LISTENING
    sm.update(tx0 + 4 * Trw + Tlisten); // LISTENING timeout (1568 ms) → IDLE

    // ── Print phase/word trace ────────────────────────────────────────────
    const char* type_names[] = {"DATA","THRU","TO","TWAS","FROM","TIS","CMD","REP"};

    std::cout << "\n  Transmitted words (" << sent.size() << "):\n";
    for (size_t i = 0; i < sent.size(); ++i) {
        uint8_t ti = static_cast<uint8_t>(sent[i].type);
        std::cout << "    [" << i << "] "
                  << (ti < 8 ? type_names[ti] : "??") << " \""
                  << sent[i].addr << "\"\n";
    }

    std::cout << "\n  Events:\n";
    for (const auto& ev : log)
        std::cout << "    [" << ev.kind << "] " << ev.detail << "\n";

    std::cout << "\n  Final state: "
              << ALEStateMachine::state_name(sm.get_state()) << "\n\n";

    // ── Assertions ────────────────────────────────────────────────────────
    bool ok = true;

    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        ok = ok && cond;
    };

    check(sm.get_state() == ALEState::IDLE,
          "Final state IDLE");
    check(sent.size() == 5,
          "5 words sent (2 scan + 2 leading + 1 conclusion)");
    check(sent.size() >= 2
          && sent[0].type == PreambleType::TO && sent[1].type == PreambleType::TO,
          "Scan slots: TO + TO");
    check(sent.size() >= 4
          && sent[2].type == PreambleType::TO && sent[3].type == PreambleType::TO,
          "Leading slots: TO + TO");
    check(sent.size() >= 5 && sent[4].type == PreambleType::TIS,
          "Conclusion: TIS SAM");
    check(op_event_fired && last_op_event == OperatorEvent::NO_CHANNELS_LEFT,
          "Operator notified: NO_CHANNELS_LEFT");

    return ok;
}

// ============================================================================
// TEST 9: TimingParameters — per-instance isolation
// ============================================================================
// set_timing_parameters() overrides must be scoped to the instance they're
// called on. Two ALEStateMachine objects in the same process must never leak
// an override from one into the other (no global/shared mutable state).
bool test_timing_parameters_isolation() {
    std::cout << "\n[TEST 9] TimingParameters — per-instance isolation\n";
    std::cout << "==================================================\n";

    ALEStateMachine sm_a;  // gets a custom (much shorter) tune delay
    ALEStateMachine sm_b;  // stays on defaults

    sm_a.set_self_address("AAA");
    sm_b.set_self_address("BBB");

    TimingParameters custom;
    custom.Twa_ms = 999u;
    custom.Tt_ms  = 10u;   // far shorter than the 1045 ms default
    sm_a.set_timing_parameters(custom);

    bool a_overridden = sm_a.get_timing_parameters().Twa_ms == 999u
                      && sm_a.get_timing_parameters().Tt_ms  == 10u;
    bool b_untouched  = sm_b.get_timing_parameters().Twa_ms == ALETimingConstants::Twa_ms
                      && sm_b.get_timing_parameters().Tt_ms  == ALETimingConstants::Tt_ms;

    std::cout << "  sm_a reflects override: " << (a_overridden ? "PASS" : "FAIL") << "\n";
    std::cout << "  sm_b stays at defaults (no cross-instance leak): " << (b_untouched ? "PASS" : "FAIL") << "\n";

    // Functional check: drive both through LBT → TUNING and sample shortly
    // after sm_a's short Tt_ms but well before the default Tt_ms — sm_a must
    // have already left TUNING while sm_b is still tuning.
    const uint32_t Twt = ALETimingConstants::Twt_ms;
    sm_a.initiate_call("JOE");
    sm_b.initiate_call("JOE");
    sm_a.update(Twt);
    sm_b.update(Twt);             // both LBT → TUNING

    const uint32_t t_sample = Twt + 50u;  // past sm_a's 10ms Tt, far short of sm_b's 1045ms default
    sm_a.update(t_sample);
    sm_b.update(t_sample);

    bool a_left_tuning  = sm_a.get_calling_phase() != CallingPhase::TUNING;
    bool b_still_tuning = sm_b.get_calling_phase() == CallingPhase::TUNING;

    std::cout << "  sm_a (10ms override) left TUNING early: " << (a_left_tuning ? "PASS" : "FAIL") << "\n";
    std::cout << "  sm_b (1045ms default) still tuning: " << (b_still_tuning ? "PASS" : "FAIL") << "\n";

    return a_overridden && b_untouched && a_left_tuning && b_still_tuning;
}

// ============================================================================
// TEST 10: Standard Scan-Rate 2 ch/s — Td = 500 ms (AC-GEN-002-001)
// Verifies:
//   1. TD2_MS constant == 500 ms
//   2. A scan configured with dwell_time_ms = TD2_MS does NOT hop before
//      500 ms have elapsed (t=499 → no hop), but DOES hop at t=500 ms.
// ============================================================================
bool test_standard_scan_rate_td2() {
    std::cout << "\n[TEST 10] Standard Scan-Rate 2 ch/s — Td = 500 ms (AC-GEN-002-001)\n";
    std::cout << "=====================================================================\n";

    // --- 1. Constant check ---
    constexpr uint32_t TD2_expected = 500u;
    const bool constant_ok = (static_cast<uint32_t>(ale::TD2_MS) == TD2_expected);
    std::cout << "  TD2_MS == 500 ms: " << (constant_ok ? "PASS" : "FAIL") << "\n";

    // --- 2. Dwell-time enforcement ---
    ALEStateMachine sm;
    ChannelTracker tracker;
    sm.set_channel_callback([&tracker](const Channel& ch) { tracker.record(ch); });

    ScanConfig cfg;
    cfg.scan_list.push_back(Channel(7100000,  "USB"));
    cfg.scan_list.push_back(Channel(14100000, "USB"));
    cfg.dwell_time_ms = static_cast<uint32_t>(ale::TD2_MS);  // 500 ms

    sm.configure_scan(cfg);
    sm.process_event(ALEEvent::START_SCAN);
    tracker.clear();  // discard initial channel-select callback

    // t = 499 ms: must still be on first channel (dwell not yet expired)
    sm.update(499);
    const bool no_hop_at_499 = (tracker.count() == 0);
    std::cout << "  No hop at t=499 ms: " << (no_hop_at_499 ? "PASS" : "FAIL") << "\n";

    // t = 500 ms: dwell expired — one hop must have occurred
    sm.update(500);
    const bool hop_at_500 = (tracker.count() == 1);
    std::cout << "  Hop at t=500 ms:   " << (hop_at_500 ? "PASS" : "FAIL") << "\n";

    return constant_ok && no_hop_at_499 && hop_at_500;
}

// ============================================================================
// TEST 11: Schnelle Scan-Rate 5 ch/s — Td = 200 ms (AC-GEN-002-002)
// Verifies:
//   1. TD5_MS constant == 200 ms
//   2. A scan configured with dwell_time_ms = TD5_MS does NOT hop before
//      200 ms have elapsed (t=199 → no hop), but DOES hop at t=200 ms.
// ============================================================================
bool test_fast_scan_rate_td5() {
    std::cout << "\n[TEST 11] Schnelle Scan-Rate 5 ch/s — Td = 200 ms (AC-GEN-002-002)\n";
    std::cout << "======================================================================\n";

    // --- 1. Constant check ---
    constexpr uint32_t TD5_expected = 200u;
    const bool constant_ok = (static_cast<uint32_t>(ale::TD5_MS) == TD5_expected);
    std::cout << "  TD5_MS == 200 ms: " << (constant_ok ? "PASS" : "FAIL") << "\n";

    // --- 2. Dwell-time enforcement ---
    ALEStateMachine sm;
    ChannelTracker tracker;
    sm.set_channel_callback([&tracker](const Channel& ch) { tracker.record(ch); });

    ScanConfig cfg;
    cfg.scan_list.push_back(Channel(7100000,  "USB"));
    cfg.scan_list.push_back(Channel(14100000, "USB"));
    cfg.dwell_time_ms = static_cast<uint32_t>(ale::TD5_MS);  // 200 ms

    sm.configure_scan(cfg);
    sm.process_event(ALEEvent::START_SCAN);
    tracker.clear();  // discard initial channel-select callback

    // t = 199 ms: must still be on first channel (dwell not yet expired)
    sm.update(199);
    const bool no_hop_at_199 = (tracker.count() == 0);
    std::cout << "  No hop at t=199 ms: " << (no_hop_at_199 ? "PASS" : "FAIL") << "\n";

    // t = 200 ms: dwell expired — one hop must have occurred
    sm.update(200);
    const bool hop_at_200 = (tracker.count() == 1);
    std::cout << "  Hop at t=200 ms:   " << (hop_at_200 ? "PASS" : "FAIL") << "\n";

    return constant_ok && no_hop_at_199 && hop_at_200;
}

// ============================================================================
// TEST 12: Always-Listen-Regel — AC-GEN-009-001
//
// Verifies:
//   1. SCANNING state: rx_enabled_callback fires with true
//   2. IDLE state: rx_enabled_callback fires with true
//   3. ERROR state after CALLING: rx_enabled_callback fires with true
//      (no dead state — exit_state(CALLING) sets RX=false, so ERROR must restore it)
// ============================================================================
bool test_always_listen_ac_gen_009_001() {
    std::cout << "\n[TEST 12] Always-Listen-Regel — AC-GEN-009-001\n";
    std::cout << "===============================================\n";

    bool rx_current = false;
    bool rx_callback_fired = false;

    auto make_sm = [&]() {
        ALEStateMachine sm;
        rx_current = false;
        rx_callback_fired = false;
        sm.set_rx_enabled_callback([&](bool enabled) {
            rx_current = enabled;
            rx_callback_fired = true;
        });
        return sm;
    };

    // 1. SCANNING: RX must be enabled on entry
    {
        auto sm = make_sm();
        sm.process_event(ALEEvent::START_SCAN);
        const bool ok = rx_callback_fired && rx_current;
        std::cout << "  SCANNING: rx_enabled_callback(true): " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return false;
    }

    // 2. IDLE: RX must be enabled on entry (explicit STOP_SCAN)
    {
        auto sm = make_sm();
        sm.process_event(ALEEvent::START_SCAN);
        rx_callback_fired = false;
        sm.process_event(ALEEvent::STOP_SCAN);
        const bool ok = rx_callback_fired && rx_current;
        std::cout << "  IDLE:     rx_enabled_callback(true): " << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return false;
    }

    // 3. ERROR state after CALLING must restore RX (no dead state)
    //    exit_state(CALLING) calls rx_enabled_callback(false);
    //    enter_state(ERROR) must call rx_enabled_callback(true).
    {
        auto sm = make_sm();
        sm.process_event(ALEEvent::CALL_REQUEST);   // → CALLING (sets rx=true for LBT)
        rx_callback_fired = false;
        sm.process_event(ALEEvent::ERROR_OCCURRED); // → ERROR
        // exit_state(CALLING) fires rx=false, then enter_state(ERROR) must fire rx=true
        const bool ok = rx_callback_fired && rx_current;
        std::cout << "  ERROR (after CALLING): rx_enabled=true (no dead state): "
                  << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return false;
    }

    // 4. ERROR state after HANDSHAKE must also restore RX
    {
        auto sm = make_sm();
        sm.process_event(ALEEvent::START_SCAN);
        sm.process_event(ALEEvent::CALL_DETECTED);  // → HANDSHAKE
        rx_callback_fired = false;
        sm.process_event(ALEEvent::ERROR_OCCURRED); // → ERROR
        const bool ok = rx_callback_fired && rx_current;
        std::cout << "  ERROR (after HANDSHAKE): rx_enabled=true (no dead state): "
                  << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) return false;
    }

    std::cout << "PASS: Always-Listen-Regel — AC-GEN-009-001\n";
    return true;
}

// ============================================================================
// TEST 13: Automatische Rückkehr in den Ausgangszustand — AC-GEN-009-003
//
// After a call/link ends the SM must return to the state it came from
// (IDLE or SCANNING), not always to IDLE.
//
// Sub-cases:
//  a) SCANNING → CALLING       → LINK_TIMEOUT     → SCANNING
//  b) SCANNING → HANDSHAKE     → LINK_TIMEOUT     → SCANNING
//  c) SCANNING → HANDSHAKE     → LINKED
//                              → LINK_TERMINATED  → SCANNING
//  d) IDLE     → CALLING       → LINK_TIMEOUT     → IDLE (regression guard)
// ============================================================================
bool test_return_to_origin_ac_gen_009_003() {
    std::cout << "\n[TEST 13] Automatische Rückkehr in den Ausgangszustand — AC-GEN-009-003\n";
    std::cout << "=========================================================================\n";

    bool all_pass = true;

    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    // a) SCANNING → CALLING → LINK_TIMEOUT → SCANNING
    {
        ALEStateMachine sm;
        ScanConfig cfg;
        cfg.scan_list.push_back(Channel(7100000, "USB"));
        sm.configure_scan(cfg);
        sm.process_event(ALEEvent::START_SCAN);    // → SCANNING
        sm.initiate_call("JOE");                   // → CALLING; pre_link_state_ = SCANNING
        sm.process_event(ALEEvent::LINK_TIMEOUT);  // → pre_link_state_
        check(sm.get_state() == ALEState::SCANNING,
              "SCANNING → CALLING → LINK_TIMEOUT → SCANNING");
    }

    // b) SCANNING → HANDSHAKE → LINK_TIMEOUT → SCANNING
    {
        ALEStateMachine sm;
        ScanConfig cfg;
        cfg.scan_list.push_back(Channel(7100000, "USB"));
        sm.configure_scan(cfg);
        sm.process_event(ALEEvent::START_SCAN);      // → SCANNING
        sm.process_event(ALEEvent::CALL_DETECTED);   // → HANDSHAKE; pre_link_state_ = SCANNING
        sm.process_event(ALEEvent::LINK_TIMEOUT);    // → pre_link_state_
        check(sm.get_state() == ALEState::SCANNING,
              "SCANNING → HANDSHAKE → LINK_TIMEOUT → SCANNING");
    }

    // c) SCANNING → HANDSHAKE → LINKED → LINK_TERMINATED → SCANNING
    {
        ALEStateMachine sm;
        ScanConfig cfg;
        cfg.scan_list.push_back(Channel(7100000, "USB"));
        sm.configure_scan(cfg);
        sm.process_event(ALEEvent::START_SCAN);         // → SCANNING
        sm.process_event(ALEEvent::CALL_DETECTED);      // → HANDSHAKE; pre_link_state_ = SCANNING
        sm.process_event(ALEEvent::HANDSHAKE_COMPLETE); // → LINKED
        sm.process_event(ALEEvent::LINK_TERMINATED);    // → pre_link_state_
        check(sm.get_state() == ALEState::SCANNING,
              "SCANNING → HANDSHAKE → LINKED → LINK_TERMINATED → SCANNING");
    }

    // d) IDLE → CALLING → LINK_TIMEOUT → IDLE (regression guard)
    {
        ALEStateMachine sm;
        sm.process_event(ALEEvent::CALL_REQUEST);  // → CALLING; pre_link_state_ = IDLE
        sm.process_event(ALEEvent::LINK_TIMEOUT);  // → IDLE
        check(sm.get_state() == ALEState::IDLE,
              "IDLE → CALLING → LINK_TIMEOUT → IDLE (regression)");
    }

    if (all_pass)
        std::cout << "PASS: Automatische Rückkehr in den Ausgangszustand — AC-GEN-009-003\n";
    return all_pass;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PC-ALE 2.0 Phase 3 - Link State Machine Tests           ║\n";
    std::cout << "║  MIL-STD-188-141B Link Establishment                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    int pass_count = 0;
    int fail_count = 0;
    
    if (test_state_transitions()) { pass_count++; } else { fail_count++; }
    if (test_channel_scanning()) { pass_count++; } else { fail_count++; }
    if (test_call_initiation()) { pass_count++; } else { fail_count++; }
    if (test_incoming_call()) { pass_count++; } else { fail_count++; }
    if (test_lqa()) { pass_count++; } else { fail_count++; }
    if (test_timeouts()) { pass_count++; } else { fail_count++; }
    if (test_sounding()) { pass_count++; } else { fail_count++; }
    if (test_full_call_cycle()) { pass_count++; } else { fail_count++; }
    if (test_timing_parameters_isolation()) { pass_count++; } else { fail_count++; }
    if (test_standard_scan_rate_td2()) { pass_count++; } else { fail_count++; }
    if (test_fast_scan_rate_td5()) { pass_count++; } else { fail_count++; }
    if (test_always_listen_ac_gen_009_001()) { pass_count++; } else { fail_count++; }
    if (test_return_to_origin_ac_gen_009_003()) { pass_count++; } else { fail_count++; }

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count << "  Failed: " << std::setw(2) << fail_count
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main() {
    return ale::run_all_tests();
}
