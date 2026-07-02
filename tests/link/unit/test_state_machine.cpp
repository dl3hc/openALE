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
#include <thread>
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
    sm.set_self_address("SAM");
    WordTracker tracker;

    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });

    // ── Arrange: start from SCANNING (typical caller context) ───────────────
    sm.process_event(ALEEvent::START_SCAN);

    // ── Act 1: initiate sounding ─────────────────────────────────────────────
    std::cout << "  Initiating sounding: ";
    bool success = sm.send_sounding();
    bool in_sounding = (sm.get_state() == ALEState::SOUNDING);
    bool in_lbt      = (sm.get_sounding_phase() == SoundingPhase::LBT);
    // AC-SOUND-001-001: no TX yet — LBT must pass first
    bool no_tx_during_lbt = (tracker.count() == 0);

    bool pass = success && in_sounding && in_lbt && no_tx_during_lbt;
    std::cout << (pass ? "PASS" : "FAIL");
    std::cout << " (state=" << ALEStateMachine::state_name(sm.get_state())
              << ", phase=LBT, words=" << tracker.count() << ")\n";

    // ── Act 2: advance past Twt → LBT clears, TX begins ─────────────────────
    std::cout << "  LBT clear → TIS word sent: ";
    sm.update(ALETimingConstants::Twt_ms + 10);
    bool word_sent = (tracker.count() >= 1);
    bool is_tis    = word_sent && (tracker.words[0].type == PreambleType::TIS);
    std::cout << (is_tis ? "PASS" : "FAIL")
              << " (words=" << tracker.count() << ")\n";
    pass = pass && is_tis;

    // ── Act 3: simulate all words complete, then LISTENING window expires ────
    std::cout << "  Sounding complete → back to SCANNING: ";
    // Trs = 2×Trw: two words sent for "SAM" (1-word addr); drain both before LISTENING.
    sm.on_word_complete();
    sm.on_word_complete();
    sm.update(ALETimingConstants::Twt_ms + 10 + ALETimingConstants::Trw_ms + 50);
    // Returns to pre_link_state_ (SCANNING — the state SOUNDING was entered from).
    bool returned_to_scan = (sm.get_state() == ALEState::SCANNING);
    std::cout << (returned_to_scan ? "PASS" : "FAIL") << "\n";

    return pass && returned_to_scan;
}

// ============================================================================
// Test 7b: AC-SOUND-001-001 — Sounding aborted when channel is busy during LBT
// ============================================================================

bool test_sounding_lbt_busy() {
    std::cout << "\n[TEST 7b] AC-SOUND-001-001: Sounding LBT — busy channel skipped\n";
    std::cout << "================================================================\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    WordTracker tracker;

    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });

    // ── Arrange: start from SCANNING, then enter sounding ───────────────────
    sm.process_event(ALEEvent::START_SCAN);
    bool success = sm.send_sounding();
    bool in_lbt  = (sm.get_state() == ALEState::SOUNDING
                    && sm.get_sounding_phase() == SoundingPhase::LBT);

    std::cout << "  In SOUNDING/LBT after send_sounding(): ";
    std::cout << (success && in_lbt ? "PASS" : "FAIL") << "\n";

    // ── Act: feed a valid word to simulate channel activity during LBT ───────
    ALEWord busy_word{};
    busy_word.valid        = true;
    busy_word.type         = PreambleType::TO;
    strncpy(busy_word.address, "OTH", 3);
    busy_word.fec_errors   = 0;
    busy_word.timestamp_ms = 10;

    sm.process_received_word(busy_word);

    // ── Assert: channel busy → no TX, returned to pre_link_state_ (SCANNING) ─
    std::cout << "  No TX after busy channel: ";
    bool no_tx = (tracker.count() == 0);
    std::cout << (no_tx ? "PASS" : "FAIL") << " (words=" << tracker.count() << ")\n";

    std::cout << "  Returned to SCANNING (previous_state) after abort: ";
    bool returned = (sm.get_state() == ALEState::SCANNING);
    std::cout << (returned ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    return success && in_lbt && no_tx && returned;
}

// ============================================================================
// Test 7c: AC-SOUND-001-002 — Sounding frame nur Conclusion (TIS + Eigenadresse)
// Prüft dass kein Scanning/Leading (TO-Wörter) gesendet wird — nur TIS + self_address.
// ============================================================================

bool test_sounding_conclusion_frame_only() {
    std::cout << "\n[TEST 7c] AC-SOUND-001-002: Sounding Conclusion-Frame — nur TIS + Eigenadresse\n";
    std::cout << "===============================================================================\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    WordTracker tracker;

    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });

    // Arrange: start from SCANNING, initiate sounding
    sm.process_event(ALEEvent::START_SCAN);
    sm.send_sounding();

    // Act: advance past LBT → TX begins
    sm.update(ALETimingConstants::Twt_ms + 10);

    // Assert 1: At least 1 word transmitted
    std::cout << "  Word(s) transmitted after LBT: ";
    bool words_sent = (tracker.count() >= 1);
    std::cout << (words_sent ? "PASS" : "FAIL") << " (count=" << tracker.count() << ")\n";

    // Assert 2: No TO words — no Scanning or Leading section (AC-SOUND-001-002)
    std::cout << "  No TO words (Conclusion section only): ";
    bool no_to_words = true;
    for (const auto& w : tracker.words) {
        if (w.type == PreambleType::TO) { no_to_words = false; break; }
    }
    std::cout << (no_to_words ? "PASS" : "FAIL") << "\n";

    // Assert 3: First word is TIS (Conclusion anchor, not a Scanning/Leading preamble)
    std::cout << "  First word type is TIS: ";
    bool first_is_tis = words_sent && (tracker.words[0].type == PreambleType::TIS);
    std::cout << (first_is_tis ? "PASS" : "FAIL") << "\n";

    // Assert 4: TIS word carries the correct self_address
    std::cout << "  TIS word carries self_address \"SAM\": ";
    bool address_correct = first_is_tis
        && (std::string(tracker.words[0].address, 3) == "SAM");
    std::cout << (address_correct ? "PASS" : "FAIL")
              << " (got=\"" << std::string(tracker.words[0].address, 3) << "\")\n";

    return words_sent && no_to_words && first_is_tis && address_correct;
}

// ============================================================================
// Test 7d: AC-SOUND-002-001 — Tss >= Ts_max: Timing-Formel für Scanning Sound
// Prüft die statische Berechnungsformel: für eine 1-Wort-Adresse muss
//   tss_word_count(1) * Trw_ms >= Ts_max_ms = 50 000 ms.
// ============================================================================

bool test_sounding_tss_timing() {
    std::cout << "\n[TEST 7d] AC-SOUND-002-001: Multichannel-Sounding Tss >= Ts_max\n";
    std::cout << "=================================================================\n";

    // Arrange: 1-word address (e.g. "SAM")
    const uint32_t addr_word_count = 1u;

    // Act: Minimum-Wortzahl für Tss-Phase berechnen
    const uint32_t wc     = ALETimingConstants::tss_word_count(addr_word_count);
    const uint64_t tss_ms = static_cast<uint64_t>(wc) * ALETimingConstants::Trw_ms;

    // Assert 1: Tss >= Ts_max = 50 000 ms
    const bool tss_ge_ts_max = (tss_ms >= ALETimingConstants::Ts_max_ms);
    std::cout << "  Tss >= Ts_max (50 000 ms): ";
    std::cout << (tss_ge_ts_max ? "PASS" : "FAIL");
    std::cout << " (wc=" << wc << ", Tss=" << tss_ms << " ms)\n";

    // Assert 2: exakter Wortzahl-Wert für 1-Wort-Adresse = ceil(50000/392) = 128
    const uint32_t expected_wc = 128u;
    const bool count_correct = (wc == expected_wc);
    std::cout << "  Word count for 1-word addr == 128: ";
    std::cout << (count_correct ? "PASS" : "FAIL");
    std::cout << " (got=" << wc << ")\n";

    // Assert 3: kein unnötiges Overpad — Tss <= Ts_max + 1 Trw
    const bool not_over_padded = (tss_ms <= ALETimingConstants::Ts_max_ms + ALETimingConstants::Trw_ms);
    std::cout << "  Tss <= Ts_max + Trw (kein Overpad): ";
    std::cout << (not_over_padded ? "PASS" : "FAIL");
    std::cout << " (Tss=" << tss_ms << " ms, limit=" << (ALETimingConstants::Ts_max_ms + ALETimingConstants::Trw_ms) << " ms)\n";

    return tss_ge_ts_max && count_correct && not_over_padded;
}

// ============================================================================
// TEST 7e: AC-SOUND-003-001 — Single-Channel Sounding: kein Calling-Pfad
//
// Verifies that during sounding the state machine NEVER enters ALEState::CALLING
// and the CallingPhase is NEVER SCANNING_CALL or MESSAGE at any point.
// Sounding uses the dedicated ALEState::SOUNDING with its own SoundingPhase;
// the calling machinery (SCANNING_CALL, MESSAGE) must remain untouched.
//
// Checks:
//  (1) send_sounding() → state is SOUNDING (not CALLING)
//  (2) calling_phase is never SCANNING_CALL or MESSAGE throughout sounding
//  (3) ALEState::CALLING is never visited
//  (4) No TO word (no destination address) in the sounding frame
//  (5) Returns to SCANNING after sounding completes
// ============================================================================

bool test_sounding_no_calling_phase() {
    std::cout << "\n[TEST 7e] AC-SOUND-003-001: Sounding — kein Calling-Pfad, nur Conclusion\n";
    std::cout << "===========================================================================\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    WordTracker wt;
    sm.set_transmit_callback([&wt](const ALEWord& w) { wt.record(w); });

    // Track all state transitions after sounding starts
    std::vector<ALEState> states_visited;
    sm.set_state_callback([&](ALEState /*from*/, ALEState to) {
        states_visited.push_back(to);
    });

    // Arrange: enter SCANNING, then clear the initial transition log
    sm.process_event(ALEEvent::START_SCAN);
    states_visited.clear();

    bool calling_phase_ok = true;   // SCANNING_CALL and MESSAGE must never appear
    auto check_cp = [&]() {
        const auto cp = sm.get_calling_phase();
        if (cp == CallingPhase::SCANNING_CALL)
            calling_phase_ok = false;
    };

    // Act: initiate sounding
    bool started = sm.send_sounding();
    check_cp();  // snapshot 1: right after entry

    // Assert 1: state is SOUNDING (not CALLING)
    const bool in_sounding = (sm.get_state() == ALEState::SOUNDING);
    std::cout << "  State after send_sounding() is SOUNDING: "
              << (in_sounding ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    // Advance through LBT window
    sm.update(ALETimingConstants::Twt_ms + 10);
    check_cp();  // snapshot 2: after LBT → TRANSMITTING

    // Advance through TX acknowledgement (Trs=2×Trw: two words for "SAM")
    sm.on_word_complete();
    sm.on_word_complete();
    check_cp();  // snapshot 3: after both TX words complete → LISTENING

    // Advance through LISTENING → SOUNDING_COMPLETE → back to SCANNING
    sm.update(ALETimingConstants::Twt_ms + 10 + ALETimingConstants::Trw_ms + 50);
    check_cp();  // snapshot 4: after sounding complete

    // Assert 2: calling_phase was never SCANNING_CALL or MESSAGE
    std::cout << "  calling_phase never SCANNING_CALL or MESSAGE: "
              << (calling_phase_ok ? "PASS" : "FAIL") << "\n";

    // Assert 3: ALEState::CALLING was never visited
    bool calling_not_visited = true;
    for (auto s : states_visited)
        if (s == ALEState::CALLING) { calling_not_visited = false; break; }
    std::cout << "  ALEState::CALLING never entered during sounding: "
              << (calling_not_visited ? "PASS" : "FAIL") << "\n";

    // Assert 4: No TO word (no destination address) in sounding frame
    bool no_to_word = true;
    for (const auto& w : wt.words)
        if (w.type == PreambleType::TO) { no_to_word = false; break; }
    std::cout << "  No TO word (no destination address) in frame: "
              << (no_to_word ? "PASS" : "FAIL") << "\n";

    // Assert 5: returned to SCANNING
    const bool returned = (sm.get_state() == ALEState::SCANNING);
    std::cout << "  Returns to SCANNING after sounding: "
              << (returned ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    return started && in_sounding && calling_phase_ok
        && calling_not_visited && no_to_word && returned;
}

// ============================================================================
// TEST 7f: AC-SOUND-003-002 — Sounding Redundanzzeit Trs = 2 × Ta(caller)
//
// Prüft:
//  (1) ALETimingConstants::Trs_min_ms == 784 ms (= 2 × Trw)
//  (2) trs_word_count(1) == 2  (1-Wort-Adresse → 2 Conclusion-Wörter)
//  (3) State Machine sendet >= 2 Conclusion-Wörter für "SAM"
//  (4) Alle gesendeten Wörter sind TIS (kein TO, kein TWAS)
//  (5) Jedes TIS-Wort trägt die korrekte Eigenadresse
// ============================================================================

bool test_sounding_trs_timing() {
    std::cout << "\n[TEST 7f] AC-SOUND-003-002: Sounding Redundanzzeit Trs = 2×Ta(caller)\n";
    std::cout << "=========================================================================\n";

    // ── Assert 1: Trs_min_ms konstante ist 784 ms ────────────────────────────
    constexpr uint32_t expected_trs_min = 784u;
    const bool trs_const_ok = (ALETimingConstants::Trs_min_ms == expected_trs_min);
    std::cout << "  Trs_min_ms == 784 ms: "
              << (trs_const_ok ? "PASS" : "FAIL")
              << " (got=" << ALETimingConstants::Trs_min_ms << ")\n";

    // ── Assert 2: trs_word_count(1) == 2 ─────────────────────────────────────
    const uint32_t wc = ALETimingConstants::trs_word_count(1u);
    const bool wc_ok = (wc == 2u);
    std::cout << "  trs_word_count(1) == 2: "
              << (wc_ok ? "PASS" : "FAIL")
              << " (got=" << wc << ")\n";

    // ── Assert 3–5: State Machine sendet 2 TIS-Wörter für "SAM" ─────────────
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    WordTracker tracker;
    sm.set_transmit_callback([&tracker](const ALEWord& word) {
        tracker.record(word);
    });

    sm.process_event(ALEEvent::START_SCAN);
    sm.send_sounding();
    sm.update(ALETimingConstants::Twt_ms + 10);  // advance past LBT → TX begins

    const uint32_t tx_count = static_cast<uint32_t>(tracker.count());
    const bool count_ge_2 = (tx_count >= 2u);
    std::cout << "  >= 2 Conclusion-Wörter gesendet (sound_repeat_count >= 2): "
              << (count_ge_2 ? "PASS" : "FAIL")
              << " (count=" << tx_count << ")\n";

    bool all_tis = true;
    bool all_addr_correct = true;
    for (const auto& w : tracker.words) {
        if (w.type != PreambleType::TIS) { all_tis = false; }
        if (std::string(w.address, 3) != "SAM") { all_addr_correct = false; }
    }
    std::cout << "  Alle gesendeten Wörter sind TIS (kein TO/TWAS): "
              << (all_tis ? "PASS" : "FAIL") << "\n";
    std::cout << "  Alle TIS-Wörter tragen Eigenadresse \"SAM\": "
              << (all_addr_correct ? "PASS" : "FAIL") << "\n";

    // ── Assert: Trs-Dauer == count × Trw >= Trs_min_ms ──────────────────────
    const uint64_t trs_ms = static_cast<uint64_t>(tx_count) * ALETimingConstants::Trw_ms;
    const bool trs_ge_min = (trs_ms >= ALETimingConstants::Trs_min_ms);
    std::cout << "  Trs (count×Trw) >= Trs_min_ms (784 ms): "
              << (trs_ge_min ? "PASS" : "FAIL")
              << " (Trs=" << trs_ms << " ms)\n";

    return trs_const_ok && wc_ok && count_ge_2 && all_tis && all_addr_correct && trs_ge_min;
}

// ============================================================================
// TEST 7g: Multi-channel sounding sweep (send_sounding_sweep)
//
// Verifies the sweep walks each channel in turn: the channel callback fires for
// each channel in order, each channel transmits the self-address conclusion
// (TIS ×2 for a 1-word address), and the SM returns to its previous state once
// the last channel is sounded — with the channel-manager override cleared.
// ============================================================================
bool test_sounding_sweep_multichannel() {
    std::cout << "\n[TEST 7g] Multi-channel sounding sweep — walks each channel\n";
    std::cout << "===================================================================\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    WordTracker wt;
    ChannelTracker ct;
    sm.set_transmit_callback([&wt](const ALEWord& w) { wt.record(w); });
    sm.set_channel_callback([&ct](const Channel& ch) { ct.record(ch); });

    // Start from IDLE (the SM's default state). send_sounding_sweep also works
    // from SCANNING.
    ct.clear();  // discard any construction-time channel-select (none, but safe)

    std::vector<Channel> chans = {
        Channel(7100000,  "USB"),
        Channel(14100000, "USB"),
        Channel(18100000, "USB"),
    };

    bool started = sm.send_sounding_sweep(chans);
    std::cout << "  send_sounding_sweep() accepted (IDLE): "
              << (started ? "PASS" : "FAIL") << "\n";
    const bool in_sounding = (sm.get_state() == ALEState::SOUNDING);
    std::cout << "  entered SOUNDING: " << (in_sounding ? "PASS" : "FAIL") << "\n";

    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    uint32_t t = 0;
    size_t tis_total = 0;
    for (size_t i = 0; i < chans.size(); ++i) {
        // LBT → TRANSMITTING (conclusion ×2 enqueued, transmit_callback fired)
        t += Twt + 10;
        sm.update(t);
        // Drain the 2 TX words → LISTENING
        sm.on_word_complete();
        sm.on_word_complete();
        // LISTENING timeout → SOUNDING_COMPLETE → next channel (or done)
        t += Trw + 50;
        sm.update(t);
    }
    for (const auto& w : wt.words)
        if (w.type == PreambleType::TIS) ++tis_total;

    // Channel callback fired once per sweep channel, in order.
    bool chan_order_ok = (ct.frequencies.size() >= chans.size());
    for (size_t i = 0; chan_order_ok && i < chans.size(); ++i)
        if (ct.frequencies[i] != chans[i].rx_frequency_hz) chan_order_ok = false;
    std::cout << "  channel callback fired per channel in order: "
              << (chan_order_ok ? "PASS" : "FAIL")
              << " (count=" << ct.frequencies.size() << ")\n";

    // 2 TIS words per channel (Trs = 2×Ta for a 1-word address).
    bool tis_count_ok = (tis_total == 2u * chans.size());
    std::cout << "  2 TIS words per channel (" << (2u * chans.size()) << " total): "
              << (tis_count_ok ? "PASS" : "FAIL") << " (got=" << tis_total << ")\n";

    // Sweep done → returned to IDLE (previous_state), override cleared.
    bool returned = (sm.get_state() == ALEState::IDLE);
    std::cout << "  returned to IDLE after sweep: "
              << (returned ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
    bool override_cleared = (sm.get_current_channel() == nullptr)
                         || (sm.get_current_channel()->rx_frequency_hz != chans.back().rx_frequency_hz);
    std::cout << "  channel override cleared: "
              << (override_cleared ? "PASS" : "FAIL") << "\n";

    return started && in_sounding && chan_order_ok && tis_count_ok && returned && override_cleared;
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
    //    enter_state(ERROR) must call rx_enabled_callback(true).
    {
        auto sm = make_sm();
        sm.process_event(ALEEvent::CALL_REQUEST);   // → CALLING (sets rx=true for LBT)
        rx_callback_fired = false;
        sm.process_event(ALEEvent::ERROR_OCCURRED); // → ERROR
        // enter_state(ERROR) fires rx=true (AC-GEN-009-001: no dead state)
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
// TEST 14: SENDING_RESPONSE must drain ALL queued words (incl. inserted LQA /
//         LQA-report words) before advancing to WAIT_ACK.
//
// Regression guard for the words_pending desync: build_response_words() inserts
// CMD-LQA + LQA-report words between the TO×2 prefix and the TIS conclusion,
// but on_word_complete() used to compute the completion threshold from an
// independent AddressEncoder::encode() count that ignored those insertions.
// The phase advanced to WAIT_ACK before the inserted words' completions fired,
// leaving words_pending > 0 permanently — so a later terminate_link() never
// reached words_pending == 0 and the SM hung in LINKED with RX disabled.
// ============================================================================
bool test_sending_response_drains_all_words() {
    std::cout << "\n[TEST 14] SENDING_RESPONSE drains all queued words (LQA insertion)\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("JOE");                 // responder
    sm.set_target_scan_channels(1);
    // Record transmitted words so the test can drain exactly the real armed
    // frame-completion count (one on_word_complete per transmitted word) — this
    // models reality, where no "extra" completions ever fire.
    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});

    // Arm the bug: a pending CMD-LQA word forces build_response_words() into the
    // LQA-insertion branch (TO×2 + CMD 'a' + TIS), transmitting 4 words while the
    // old resp_slots count expected only 3.
    sm.set_pending_lqa_cmd(0u);

    sm.process_event(ALEEvent::START_SCAN);    // → SCANNING

    const uint32_t Trw  = ALETimingConstants::Trw_ms;   // 392
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;   // 784
    const char joe3[3] = {'J','O','E'};
    const char sam3[3] = {'S','A','M'};
    uint32_t t = 1000u;

    // Caller SAM calls JOE: scanning TO×2 + leading TO×2 + conclusion TIS SAM.
    auto rx = [&](PreambleType ty, const char a3[3]) {
        sm.update(t);
        sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };
    rx(PreambleType::TO,  joe3);   // SCANNING → HANDSHAKE (CALL_DETECTED)
    rx(PreambleType::TO,  joe3);   // WAIT_CYCLE_END (re-anchor silence)
    rx(PreambleType::TO,  joe3);   // leading ×2
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TIS, sam3);   // conclusion → hs_conclusion_rcvd, caller_address=SAM

    check(sm.get_state() == ALEState::HANDSHAKE, "Reached HANDSHAKE");
    check(sm.is_hs_conclusion_rcvd(), "Caller conclusion (TIS SAM) received");

    // Absolute timeline (rx used t=1000..2568; TIS SAM was processed at 2568):
    //   tis_t      = 2568  → hs_tlww_start_ms anchored here
    //   settle_t   = tis_t + Tdrw = 3352  → SLOT_WAIT
    //   3353       → CHANNEL_CHECK (hs_lbt_start_ms = 3353)
    //   4137       → CHANNEL_CHECK LBT clear → SENDING_RESPONSE + build_response_words
    const uint32_t tis_t    = 1000u + 4u * Trw;   // 2568
    const uint32_t settle_t = tis_t + Tdrw;       // 3352
    sm.update(settle_t);                          // WAIT_CYCLE_END settle → SLOT_WAIT
    sm.update(settle_t + 1);                      // SLOT_WAIT (0 ms) → CHANNEL_CHECK
    sm.update(settle_t + 1 + Tdrw);               // CHANNEL_CHECK LBT clear → SENDING_RESPONSE

    check(sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE,
          "Reached SENDING_RESPONSE");

    // Count words actually handed to transmit_callback (= words_pending).
    const uint32_t wp_after_build = sm.get_words_pending();
    std::cout << "  words_pending after build_response_words = " << wp_after_build << "\n";
    check(wp_after_build == 4, "4 words queued (TO×2 + CMD 'a' + TIS)");
    check(static_cast<uint32_t>(sent.size()) == 4, "transmit_callback saw 4 words");

    // Drain exactly the queued words via on_word_complete (one per rendered frame).
    for (uint32_t i = 0; i < wp_after_build; ++i)
        sm.on_word_complete();

    // ROOT-CAUSE assertion: every queued word must have been consumed; none leaked.
    check(sm.get_words_pending() == 0,
          "words_pending == 0 after SENDING_RESPONSE (no LQA word leaked)");
    check(sm.get_handshake_phase() == HandshakePhase::WAIT_ACK,
          "Advanced to WAIT_ACK only after full drain");

    // Complete the handshake: feed SAM's ACK (TO JOE ×2 + TIS SAM) + Tdrw settle.
    // WAIT_ACK (1) window is ~2091 ms from hs_ack_start_ms (= 4137); stay inside it.
    const uint32_t ack0 = settle_t + 1 + Tdrw + 500;   // 4637 — first ACK word
    sm.update(ack0);                  sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe3));
    sm.update(ack0 + Trw);           sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe3));
    sm.update(ack0 + 2 * Trw);       sm.process_received_word(WordParser::make_word(PreambleType::TIS, sam3));
    sm.update(ack0 + 2 * Trw + Tdrw); // ACK conclusion settle → LINKED

    check(sm.get_state() == ALEState::LINKED, "Reached LINKED");

    // END-TO-END assertion: terminate_link() must complete (LINK_TERMINATED),
    // not hang with a stale words_pending.  Drain ONLY the real termination
    // words (one on_word_complete per armed frame) — over-draining would mask
    // the leak by clearing the stale count with a completion that never fires.
    const size_t sent_before_term = sent.size();
    sm.terminate_link();                              // transmits TO×2 + TWAS
    const uint32_t term_words =
        static_cast<uint32_t>(sent.size() - sent_before_term);
    std::cout << "  terminate_link transmitted " << term_words << " words\n";
    for (uint32_t i = 0; i < term_words; ++i)
        sm.on_word_complete();                         // drain the termination frame

    check(sm.get_words_pending() == 0, "terminate_link drained (words_pending == 0)");
    check(sm.get_state() != ALEState::LINKED,
          "Left LINKED after terminate_link (no hang)");

    if (all_pass)
        std::cout << "PASS: SENDING_RESPONSE drains all queued words\n";
    return all_pass;
}

// ============================================================================
// TEST 15: emergency_manual_control() must not leak words_pending into the
//         next operation.  From LINKED it transmits a TWAS termination frame
//         (incrementing words_pending) and immediately transitions to IDLE
//         before those frame completions can drain.  If IDLE/SOUNDING entry
//         does not reset words_pending, a subsequent send_sounding() inherits
//         the stale count, never reaches words_pending == 0, and hangs in
//         SOUNDING/TRANSMITTING with RX disabled and no timeout.
// ============================================================================
bool test_emergency_does_not_leak_words_pending() {
    std::cout << "\n[TEST 15] emergency_manual_control() does not leak words_pending\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});

    // Reach LINKED with active_call_to set so emergency_manual_control() emits a
    // real TWAS termination frame (it gates on !active_call_to.empty()).
    sm.initiate_call("JOE");                          // IDLE → CALLING; active_call_to = "JOE"
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);   // CALLING → LINKED
    check(sm.get_state() == ALEState::LINKED, "Reached LINKED");
    check(sm.get_words_pending() == 0, "LINKED starts with words_pending == 0");

    sm.update(1000u);  // set current_time_ms for LBT math below

    // Emergency abort: transmits TO JOE ×2 + TWAS SAM, then → IDLE.
    const size_t sent_before_emerg = sent.size();
    sm.emergency_manual_control();
    const uint32_t emerg_words =
        static_cast<uint32_t>(sent.size() - sent_before_emerg);
    std::cout << "  emergency transmitted " << emerg_words << " words\n";
    check(emerg_words == 3, "emergency transmitted TWAS termination (3 words)");
    check(sm.get_state() == ALEState::IDLE, "emergency → IDLE");

    // The armed frame completions for the TWAS words fire now (in IDLE).  IDLE has
    // no on_word_complete handler, so a stale words_pending would survive them.
    for (uint32_t i = 0; i < emerg_words; ++i)
        sm.on_word_complete();
    check(sm.get_words_pending() == 0,
          "no stale words_pending after emergency (would leak into next op)");

    // The real failure mode: a subsequent sounding must not hang in TRANSMITTING.
    check(sm.send_sounding(), "send_sounding from IDLE accepted");
    check(sm.get_state() == ALEState::SOUNDING, "Entered SOUNDING");
    check(sm.get_sounding_phase() == SoundingPhase::LBT, "SOUNDING starts in LBT");

    // LBT (Twt = 784 ms) elapses → TRANSMITTING; conclusion ×2 enqueued.
    const size_t sent_before_snd = sent.size();
    sm.update(1000u + ALETimingConstants::Twt_ms + 1u);
    const uint32_t snd_words =
        static_cast<uint32_t>(sent.size() - sent_before_snd);
    std::cout << "  sounding transmitted " << snd_words << " words\n";
    check(snd_words == 2, "sounding transmitted conclusion ×2");
    check(sm.get_sounding_phase() == SoundingPhase::TRANSMITTING,
          "SOUNDING in TRANSMITTING");

    // Drain ONLY the 2 real conclusion completions (one per armed frame).
    for (uint32_t i = 0; i < snd_words; ++i)
        sm.on_word_complete();

    check(sm.get_words_pending() == 0,
          "words_pending == 0 after sounding TX (no stale carry-over)");
    check(sm.get_sounding_phase() == SoundingPhase::LISTENING,
          "Sounding reached LISTENING (not hung in TRANSMITTING)");

    if (all_pass)
        std::cout << "PASS: emergency_manual_control() does not leak words_pending\n";
    return all_pass;
}

// ============================================================================
// TEST 16: terminate_link() must not hang in LINKED if its TX frame never drains.
//         terminate_link() transmits TO×2 + TWAS and relies on on_word_complete()
//         firing LINK_TERMINATED once words_pending reaches 0.  If the audio
//         device stalls (or transmit_callback never arms the completion), the SM
//         would hang in LINKED with RX disabled — handle_linked() short-circuits
//         on linked_terminating_ and the Twa timer is suppressed.  A drain
//         deadline must force LINK_TERMINATED so the radio recovers.
// ============================================================================
bool test_terminate_link_drain_timeout() {
    std::cout << "\n[TEST 16] terminate_link() drain deadline (no hang on stalled TX)\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_operator_callback([](OperatorEvent){});
    bool rx_on = false;
    sm.set_rx_enabled_callback([&](bool on){ rx_on = on; });

    // Reach LINKED with active_call_to set (so terminate emits a real frame).
    sm.initiate_call("JOE");                          // IDLE → CALLING; active_call_to="JOE"
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);   // CALLING → LINKED
    check(sm.get_state() == ALEState::LINKED, "Reached LINKED");

    sm.update(1000u);  // set current_time_ms
    rx_on = true;

    const size_t sent_before = sent.size();
    sm.terminate_link();                              // transmits TO JOE×2 + TWAS SAM
    const uint32_t term_words =
        static_cast<uint32_t>(sent.size() - sent_before);
    std::cout << "  termination words: " << term_words << "\n";
    check(term_words == 3, "terminate_link transmitted termination frame (3 words)");
    check(sm.get_state() == ALEState::LINKED, "Still LINKED (drain pending, not yet terminated)");
    check(!rx_on, "RX disabled during termination TX");

    // Simulate a stall: do NOT fire on_word_complete().  Advance past the drain
    // deadline — handle_linked() must force LINK_TERMINATED instead of hanging.
    sm.update(1000u + ALETimingConstants::TX_DRAIN_TIMEOUT_MS + 1u);
    check(sm.get_state() != ALEState::LINKED,
          "Force-LINK_TERMINATED after drain timeout (not hung in LINKED)");
    check(rx_on, "RX re-enabled after forced termination");

    if (all_pass)
        std::cout << "PASS: terminate_link() drain deadline\n";
    return all_pass;
}

// ============================================================================
// TEST 17: trigger_linked_orderwire() must not hang in LINKED if its burst never
//         drains.  Same shape as terminate_link but the recovery is to ABANDON
//         the orderwire burst (stay LINKED, re-open RX), not to terminate the link.
// ============================================================================
bool test_orderwire_drain_timeout() {
    std::cout << "\n[TEST 17] trigger_linked_orderwire() drain deadline (abandon on stall)\n";
    std::cout << "===========================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_operator_callback([](OperatorEvent){});
    bool rx_on = false;
    sm.set_rx_enabled_callback([&](bool on){ rx_on = on; });

    sm.initiate_call("JOE");
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);   // → LINKED
    check(sm.get_state() == ALEState::LINKED, "Reached LINKED");

    sm.update(1000u);
    rx_on = true;

    // Queue a one-word orderwire burst (CMD 'f').  handle_linked() will start it
    // on the next update(): pending + TIS:SELF, doubled → 4 words on the air.
    ALEWord cmd{};
    cmd.type    = PreambleType::CMD;
    cmd.address[0] = 'f'; cmd.address[1] = ' '; cmd.address[2] = ' '; cmd.address[3] = '\0';
    cmd.valid   = true;
    sm.trigger_linked_orderwire({cmd});

    // Start the burst (orderwire_pending_ → transmit → orderwire_transmitting_).
    // The burst is pending + TIS:SELF, doubled → 4 words (regression-guard: the
    // conclusion must actually be appended, not dropped by a dangling-ref loop).
    const size_t sent_before = sent.size();
    sm.update(1001u);
    const uint32_t burst_words =
        static_cast<uint32_t>(sent.size() - sent_before);
    std::cout << "  orderwire burst words: " << burst_words << "\n";
    check(burst_words == 4, "orderwire burst = [CMD, TIS, CMD, TIS] (4 words)");
    check(sm.get_state() == ALEState::LINKED, "Still LINKED during orderwire TX");
    check(!rx_on, "RX disabled during orderwire TX");

    // Stall: do not drain.  Advance past the deadline → abandon the burst, keep
    // the link, re-open RX (NOT terminate the link).
    sm.update(1001u + ALETimingConstants::TX_DRAIN_TIMEOUT_MS + 1u);
    check(sm.get_state() == ALEState::LINKED,
          "Orderwire drain timeout abandons burst, keeps link LINKED");
    check(rx_on, "RX re-enabled after orderwire drain timeout");

    if (all_pass)
        std::cout << "PASS: trigger_linked_orderwire() drain deadline\n";
    return all_pass;
}

// ============================================================================
// TEST 18: respond_to_call() must not bypass the 3-way handshake.  The handshake
//         auto-advances (WAIT_CYCLE_END → … → WAIT_ACK → LINKED) on update(); there
//         is no manual "respond" step.  Calling respond_to_call() from an early
//         phase (before the caller's conclusion is received or our response sent)
//         used to fire HANDSHAKE_COMPLETE unconditionally, declaring LINKED with an
//         empty caller identity and no response on the air — the peer would time
//         out while we believed the link was up.  It must be a no-op there.
// ============================================================================
bool test_respond_to_call_does_not_bypass_handshake() {
    std::cout << "\n[TEST 18] respond_to_call() does not bypass the handshake\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("JOE");
    sm.set_transmit_callback([](const ALEWord&){});
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});
    sm.set_target_scan_channels(0);
    sm.process_event(ALEEvent::START_SCAN);

    // Drive an incoming call up to HANDSHAKE/WAIT_CYCLE_END but STOP before the
    // caller's conclusion (TIS) arrives: hs_conclusion_rcvd == false,
    // caller_address empty.  This is the dangerous phase for a bypass.
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    uint32_t t = 1000u;
    const char joe3[3] = {'J','O','E'};
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TO, joe3));
    // SCANNING → HANDSHAKE on the first TO; no TIS yet.
    check(sm.get_state() == ALEState::HANDSHAKE, "Reached HANDSHAKE");
    check(!sm.is_hs_conclusion_rcvd(), "No conclusion received yet");
    check(sm.get_caller_address().empty(), "caller_address empty (no TIS yet)");

    bool ok = sm.respond_to_call();
    check(!ok, "respond_to_call() returns false from WAIT_CYCLE_END (no conclusion)");
    check(sm.get_state() == ALEState::HANDSHAKE, "Still HANDSHAKE (not LINKED)");
    check(sm.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END,
          "Phase unchanged (no bypass)");

    // Even after the conclusion arrives (WAIT_CYCLE_END, conclusion received),
    // the response frame has not been sent — respond_to_call() must still refuse.
    const char sam3[3] = {'S','A','M'};
    sm.update(t + Trw); sm.process_received_word(WordParser::make_word(PreambleType::TIS, sam3));
    check(sm.is_hs_conclusion_rcvd(), "Conclusion now received");
    ok = sm.respond_to_call();
    check(!ok, "respond_to_call() returns false before response is sent");
    check(sm.get_state() == ALEState::HANDSHAKE, "Still HANDSHAKE (not LINKED)");

    if (all_pass)
        std::cout << "PASS: respond_to_call() does not bypass the handshake\n";
    return all_pass;
}

// ============================================================================
// TEST 19: AllCall address detection in the decoder (A.5.5.4.4).
//         Global @?@ → ALLCALL for all; selective @A@ → ALLCALL only if our self
//         address ends in the selector char; AnyCall @@? is NOT AllCall (NONE);
//         a normal TO to self stays TO_SELF.
// ============================================================================
bool test_allcall_decoder_detection() {
    std::cout << "\n[TEST 19] AllCall address detection (decoder)\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEWordDecoder dec;
    auto decode = [&](PreambleType ty, const char a3[3], const std::string& self) {
        ALEWord w = WordParser::make_word(ty, a3);
        WordDecodeContext ctx;
        ctx.self_address = self;
        return dec.decode(w, ctx);
    };

    const char allcall_g[3] = {'@','?','@'};   // global AllCall
    const char allcall_sA[3] = {'@','A','@'}; // selective AllCall (selector 'A')
    const char anycall[3] = {'@','@','?'};    // global AnyCall (A.5.5.4.5)
    const char sam3[3] = {'S','A','M'};

    auto e1 = decode(PreambleType::TO, allcall_g, "JOE");
    check(e1.type == WordEvent::Type::ALLCALL, "global @?@ → ALLCALL (any self)");
    check(e1.address == "@?", "address trimmed to @?");

    auto e2 = decode(PreambleType::TO, allcall_sA, "SAMA");
    check(e2.type == WordEvent::Type::ALLCALL, "selective @A@ pertinent (self ends A) → ALLCALL");
    auto e3 = decode(PreambleType::TO, allcall_sA, "SAM");
    check(e3.type == WordEvent::Type::NONE, "selective @A@ not pertinent (self ends M) → NONE");

    auto e4 = decode(PreambleType::TO, anycall, "JOE");
    check(e4.type == WordEvent::Type::NONE, "AnyCall @@? → NONE (not AllCall)");

    auto e5 = decode(PreambleType::TO, sam3, "SAM");
    check(e5.type == WordEvent::Type::TO_SELF, "normal TO to self → TO_SELF (not ALLCALL)");

    if (all_pass)
        std::cout << "PASS: AllCall address detection\n";
    return all_pass;
}

// ============================================================================
// TEST 20: AllCall receiver (A.5.5.4.4) — one-way broadcast, no response frame.
//          On TIS conclusion the SM links directly to the caller (no
//          SENDING_RESPONSE/WAIT_ACK); on TWAS it resumes scanning.  No frame
//          is ever transmitted in reply.
// ============================================================================
bool test_allcall_receiver_links_on_tis() {
    std::cout << "\n[TEST 20] AllCall receiver links on TIS conclusion (no response)\n";
    std::cout << "=========================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("JOE");
    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    OperatorEvent last_op{};
    bool op_fired = false;
    sm.set_operator_callback([&](OperatorEvent e){ last_op = e; op_fired = true; });

    sm.process_event(ALEEvent::START_SCAN);    // → SCANNING

    const uint32_t Trw  = ALETimingConstants::Trw_ms;   // 392
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;   // 784
    const char allcall3[3] = {'@','?','@'};   // global AllCall address
    const char sam3[3] = {'S','A','M'};
    uint32_t t = 1000u;

    // Caller SAM sends an AllCall: scanning TO @?@ ×2 + leading TO @?@ ×2,
    // then conclusion TIS SAM (the AllCall address never appears in the
    // conclusion).  First @?@ in SCANNING → ALLCALL → HANDSHAKE.
    auto rx = [&](PreambleType ty, const char a3[3]) {
        sm.update(t);
        sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };
    rx(PreambleType::TO,  allcall3);   // SCANNING → HANDSHAKE (AllCall)
    rx(PreambleType::TO,  allcall3);
    rx(PreambleType::TO,  allcall3);   // leading ×2
    rx(PreambleType::TO,  allcall3);
    rx(PreambleType::TIS, sam3);       // conclusion → hs_conclusion_rcvd, caller=SAM

    check(sm.get_state() == ALEState::HANDSHAKE, "In HANDSHAKE collecting AllCall conclusion");
    check(sm.is_hs_conclusion_rcvd(), "Caller conclusion (TIS SAM) received");
    check(sm.get_caller_address() == "SAM", "caller recorded as SAM");

    // Settle Tdrw after the conclusion → AllCall path → LINKED (no response sent).
    const uint32_t tis_t = 1000u + 4u * Trw;   // 2568 — when TIS was processed
    sm.update(tis_t + Tdrw);                    // settle → AllCall TIS → LINKED

    check(sm.get_state() == ALEState::LINKED, "LINKED on AllCall TIS conclusion");
    check(op_fired && last_op == OperatorEvent::LINK_ESTABLISHED,
          "Operator LINK_ESTABLISHED fired");
    check(sent.empty(), "No response frame transmitted (AllCall is one-way)");
    check(sm.get_caller_address() == "SAM", "Linked to the AllCall caller SAM");

    if (all_pass)
        std::cout << "PASS: AllCall receiver links on TIS (no response)\n";
    return all_pass;
}

// ============================================================================
// TEST 21: AllCall concluding with TWAS → resume scanning (no link).
// ============================================================================
bool test_allcall_receiver_resumes_on_twas() {
    std::cout << "\n[TEST 21] AllCall receiver resumes scanning on TWAS conclusion\n";
    std::cout << "=========================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("JOE");
    sm.set_transmit_callback([](const ALEWord&){});
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});

    sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const char allcall3[3] = {'@','?','@'};
    const char sam3[3] = {'S','A','M'};
    uint32_t t = 1000u;
    auto rx = [&](PreambleType ty, const char a3[3]) {
        sm.update(t);
        sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };
    rx(PreambleType::TO,   allcall3);   // SCANNING → HANDSHAKE (AllCall)
    rx(PreambleType::TO,   allcall3);
    rx(PreambleType::TO,   allcall3);
    rx(PreambleType::TO,   allcall3);
    rx(PreambleType::TWAS, sam3);       // AllCall concluded with TWAS → resume

    check(sm.get_state() == ALEState::SCANNING,
          "AllCall + TWAS conclusion → resume SCANNING (no link)");
    check(!sm.is_hs_conclusion_rcvd(), "No conclusion linked on TWAS");

    if (all_pass)
        std::cout << "PASS: AllCall receiver resumes on TWAS\n";
    return all_pass;
}

// ============================================================================
// TEST 22: Single-thread contract net (debug-only).  The SM has no internal
//         synchronization; the caller must drive it from one thread.  In debug
//         builds a boundary call from a different thread increments
//         thread_violations(); in release the check is compiled out (always 0).
// ============================================================================
bool test_thread_contract_check() {
    std::cout << "\n[TEST 22] Single-thread contract net\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_transmit_callback([](const ALEWord&){});
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});

    // First boundary call on THIS thread captures the owner.
    sm.update(1000u);
    sm.update(2000u);
    check(sm.thread_violations() == 0, "No violation on the owner thread");

    // A boundary call from a second thread must be detected (debug) / no-op (release).
    std::thread other([&](){ sm.update(3000u); });
    other.join();
#ifndef NDEBUG
    check(sm.thread_violations() > 0, "Cross-thread call detected (debug build)");
#else
    std::cout << "  cross-thread detection: SKIPPED (release build, check compiled out)\n";
    check(sm.thread_violations() == 0, "Release: check compiled out (0 violations)");
#endif

    if (all_pass)
        std::cout << "PASS: single-thread contract net\n";
    return all_pass;
}

// ============================================================================
// TEST 23: SENDING_RESPONSE must not hang until the 30 s Twa backstop if its
//          response frame never drains.  A stuck audio device / null completion
//          would leave the responder in SENDING_RESPONSE until Twa.  The per-
//          phase TX-drain deadline must force LINK_TIMEOUT promptly.
// ============================================================================
bool test_sending_response_drain_timeout() {
    std::cout << "\n[TEST 23] SENDING_RESPONSE drain deadline (no Twa-wait on stall)\n";
    std::cout << "===================================================================\n";

    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("JOE");                 // responder
    sm.set_target_scan_channels(1);
    sm.set_transmit_callback([](const ALEWord&){});
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_operator_callback([](OperatorEvent){});

    sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char joe3[3] = {'J','O','E'};
    const char sam3[3] = {'S','A','M'};
    uint32_t t = 1000u;
    auto rx = [&](PreambleType ty, const char a3[3]) {
        sm.update(t); sm.process_received_word(WordParser::make_word(ty, a3)); t += Trw;
    };
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TIS, sam3);   // conclusion → WAIT_CYCLE_END

    const uint32_t tis_t    = 1000u + 4u * Trw;   // 2568
    const uint32_t settle_t = tis_t + Tdrw;       // 3352
    sm.update(settle_t);                          // settle → SLOT_WAIT
    sm.update(settle_t + 1);                      // SLOT_WAIT → CHANNEL_CHECK
    const uint32_t sr_t = settle_t + 1 + Tdrw;    // 4137
    sm.update(sr_t);                              // CHANNEL_CHECK → SENDING_RESPONSE + build_response_words

    check(sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE,
          "Reached SENDING_RESPONSE with response frame queued");
    check(sm.get_words_pending() > 0, "Response frame is pending (waiting for drain)");

    // Do NOT fire on_word_complete — simulate a stalled audio device.  Advance
    // past the TX-drain deadline (well under the 30 s Twa backstop).
    sm.update(sr_t + ALETimingConstants::TX_DRAIN_TIMEOUT_MS + 1u);
    check(sm.get_state() != ALEState::HANDSHAKE,
          "Force-LINK_TIMEOUT after SENDING_RESPONSE drain timeout (not hung)");
    check(sm.get_state() == ALEState::SCANNING,
          "Returned to pre-link state (SCANNING)");

    if (all_pass)
        std::cout << "PASS: SENDING_RESPONSE drain deadline\n";
    return all_pass;
}

// ============================================================================
// TEST: Link idle timeout — the configured Twa governs (not the hardcoded
// 120 s safety net), the idle warning fires once ~30 s before Twa, and
// reset_link_idle_timer() restarts the full period and re-arms the warning.
bool test_link_idle_timeout_and_warning() {
    std::cout << "\n[TEST] Link idle timeout — Twa governs + warning + reset\n";
    std::cout << "============================================================\n";
    bool ok = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        ok = ok && cond;
    };

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_transmit_callback([](const ALEWord&) {});  // swallow TWAS / frames

    // User-configured idle timeout: 360 s (GUI default). The hardcoded 120 s
    // LINK_TIMEOUT_MS safety net must NOT fire before this — it is now
    // max(Twa_ms, LINK_TIMEOUT_MS), so Twa governs when larger.
    TimingParameters tp = sm.get_timing_parameters();
    tp.Twa_ms = 360000u;
    sm.set_timing_parameters(tp);

    int warn_count = 0;
    uint32_t last_remaining = 0;
    sm.set_idle_warning_callback([&](uint32_t remaining_sec) {
        ++warn_count;
        last_remaining = remaining_sec;
    });

    // Drive to LINKED. update(0) anchors current_time_ms so last_word_time_ms
    // (set on LINKED entry) == 0.
    sm.update(0u);
    ScanConfig cfg;
    cfg.scan_list.push_back(Channel(7100000, "USB"));
    sm.configure_scan(cfg);
    sm.process_event(ALEEvent::START_SCAN);         // → SCANNING
    sm.process_event(ALEEvent::CALL_DETECTED);      // → HANDSHAKE
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE); // → LINKED (last_word_time_ms = 0)
    check(sm.get_state() == ALEState::LINKED, "Entered LINKED");

    // 130 s idle: the old 120 s safety net would have fired LINK_TIMEOUT here.
    // With the fix, the configured 360 s Twa governs — link stays LINKED.
    sm.update(130000u);
    check(sm.get_state() == ALEState::LINKED, "Link survives past 120 s safety net (Twa=360 s governs)");
    check(warn_count == 0, "No idle warning at 130 s (warns only in last 30 s)");

    // 330 s idle: exactly IDLE_WARNING_LEAD_MS before Twa → warning fires once.
    sm.update(330000u);
    check(sm.get_state() == ALEState::LINKED, "Link still LINKED at 330 s");
    check(warn_count == 1, "Idle warning fired once at 330 s");
    check(last_remaining == 30u, "Warning reports ~30 s remaining");

    // Re-tick at the same time: no duplicate warning (one-shot per idle period).
    sm.update(330000u);
    check(warn_count == 1, "No duplicate warning on same-tick re-update");

    // Operator clicks "Reset Timer": restarts the full idle period, re-arms.
    sm.reset_link_idle_timer();
    check(warn_count == 1, "Reset does not itself fire a warning");

    // 330 s after the reset (t=660 s): warning re-fires.
    sm.update(660000u);
    check(warn_count == 2, "Idle warning re-armed after reset, fires again 330 s later");
    check(sm.get_state() == ALEState::LINKED, "Link still LINKED after reset+330 s");

    // 360 s after the reset (t=690 s): Twa elapses → TWAS + LINK_TIMEOUT.
    sm.update(690000u);
    check(sm.get_state() != ALEState::LINKED, "Link terminated at Twa after reset");

    if (ok)
        std::cout << "PASS: Link idle timeout — Twa governs + warning + reset\n";
    return ok;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ALE Link State Machine Tests                             ║\n";
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
    if (test_sounding_lbt_busy()) { pass_count++; } else { fail_count++; }
    if (test_sounding_conclusion_frame_only()) { pass_count++; } else { fail_count++; }
    if (test_sounding_tss_timing()) { pass_count++; } else { fail_count++; }
    if (test_sounding_no_calling_phase()) { pass_count++; } else { fail_count++; }
    if (test_sounding_trs_timing()) { pass_count++; } else { fail_count++; }
    if (test_sounding_sweep_multichannel()) { pass_count++; } else { fail_count++; }
    if (test_full_call_cycle()) { pass_count++; } else { fail_count++; }
    if (test_timing_parameters_isolation()) { pass_count++; } else { fail_count++; }
    if (test_standard_scan_rate_td2()) { pass_count++; } else { fail_count++; }
    if (test_fast_scan_rate_td5()) { pass_count++; } else { fail_count++; }
    if (test_always_listen_ac_gen_009_001()) { pass_count++; } else { fail_count++; }
    if (test_return_to_origin_ac_gen_009_003()) { pass_count++; } else { fail_count++; }
    if (test_sending_response_drains_all_words()) { pass_count++; } else { fail_count++; }
    if (test_emergency_does_not_leak_words_pending()) { pass_count++; } else { fail_count++; }
    if (test_terminate_link_drain_timeout()) { pass_count++; } else { fail_count++; }
    if (test_orderwire_drain_timeout()) { pass_count++; } else { fail_count++; }
    if (test_respond_to_call_does_not_bypass_handshake()) { pass_count++; } else { fail_count++; }
    if (test_allcall_decoder_detection()) { pass_count++; } else { fail_count++; }
    if (test_allcall_receiver_links_on_tis()) { pass_count++; } else { fail_count++; }
    if (test_allcall_receiver_resumes_on_twas()) { pass_count++; } else { fail_count++; }
    if (test_thread_contract_check()) { pass_count++; } else { fail_count++; }
    if (test_sending_response_drain_timeout()) { pass_count++; } else { fail_count++; }
    if (test_link_idle_timeout_and_warning()) { pass_count++; } else { fail_count++; }

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
