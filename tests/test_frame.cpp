/**
 * \file test_frame.cpp
 * \brief Acceptance tests for FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis
 *
 * Covers REQ-FRAME-001 (AC-FRAME-001-2, -3, -4) per MIL-STD-188-141B A.5.2.5.
 *
 * Timing model (DD-013, DD-006):
 *   ALEStateMachine is callback-driven: on_word_complete() is called once per
 *   Trw-slot (after ALE2GModem finishes all 3 copies).  update() only triggers
 *   the next word if words_pending == 0 and current_time_ms >= next_tx.
 *
 *   Slot schedule (DD-006):
 *     next_tx = first_call_tx_ms + call_cycle_count × Trw_ms
 *
 *   Test drive pattern:
 *     sm.update(first_call_tx_ms + N * Trw_ms)   → word N enqueued
 *     sm.on_word_complete()                        → call_cycle_count becomes N+1
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "FSK/ale_waveform.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <cstring>
#include <algorithm>

namespace ale {

// ============================================================================
// Test harness
// ============================================================================

struct FrameHarness {
    ALEStateMachine sm;
    ALE2GModem      modem;

    std::vector<ALEWord>   tx_words;         // logical words sent by SM
    std::vector<std::vector<int16_t>> tx_bufs; // PCM buffers from modem
    std::vector<uint32_t>  word_complete_times; // times of on_word_complete() calls
    std::vector<CallingPhase> phase_at_complete; // calling_phase at each on_word_complete

    FrameHarness(const std::string& self, const std::string& to_addr,
                 uint32_t scan_ch)
    {
        sm.set_self_address(self);
        sm.set_target_scan_channels(scan_ch);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_channel_callback([](const Channel&){});
        sm.set_rx_enabled_callback([](bool){});

        sm.set_transmit_callback([this](const ALEWord& w) {
            tx_words.push_back(w);
            modem.enqueue_word(w);
        });

        modem.set_tx_callback([this](const int16_t* s, uint32_t n) {
            tx_bufs.emplace_back(s, s + n);
        });

        modem.set_word_done_callback([this]() {
            word_complete_times.push_back(current_ms_);
            phase_at_complete.push_back(sm.get_calling_phase());
            sm.on_word_complete();
        });

        sm.initiate_call(to_addr);
    }

    // Advance both SM and modem to timestamp t.
    // The modem fires done_cb_ → on_word_complete() as copies complete.
    void tick(uint32_t t) {
        current_ms_ = t;
        sm.update(t);
        modem.update(t);
    }

    // Drive N full Trw slots starting at t0.  Each slot:
    //   tick(t0 + i*Trw)          – trigger word transmission
    //   tick(t0 + i*Trw + Trw-1)  – complete all 3 copies
    void drive_slots(uint32_t t0, uint32_t n_slots) {
        const uint32_t Trw = ALETimingConstants::Trw_ms;
        for (uint32_t i = 0; i < n_slots; ++i) {
            tick(t0 + i * Trw);
            // Advance modem time so all 3 copies of this word complete.
            // TW_INT_MS = Trw/3 (≈130 ms); 3 copies finish by t + 3×TW_INT_MS.
            uint32_t tw = Trw / SYMBOL_REPETITION;
            tick(t0 + i * Trw + tw);
            tick(t0 + i * Trw + 2 * tw);
        }
    }

private:
    uint32_t current_ms_ = 0;
};

// ============================================================================
// AC-FRAME-001-2 (row 1) — ALE2GModem produces exactly 3 identical PCM buffers
// per logical word; no other module generates repetitions.
// ============================================================================

bool test_ac_001_2_modem_3x_identical_copies()
{
    std::cout << "\n[AC-FRAME-001-2] ALE2GModem: 3 identical PCM copies per word\n";

    ALE2GModem modem;
    std::vector<std::vector<int16_t>> bufs;

    modem.set_tx_callback([&](const int16_t* s, uint32_t n) {
        bufs.emplace_back(s, s + n);
    });

    ALEWord word = ALEWord();
    word.type       = WordType::TO;
    word.address[0] = 'S'; word.address[1] = 'A'; word.address[2] = 'M';
    word.address[3] = '\0';
    word.valid       = true;
    modem.enqueue_word(word);

    // Drive through 3 copies: each copy fires at TW_INT_MS intervals.
    // TW_INT_MS = Trw_ms / SYMBOL_REPETITION = 392 / 3 = 130 ms (integer).
    constexpr uint32_t Tw = ALETimingConstants::Trw_ms / SYMBOL_REPETITION;
    modem.update(0);          // first copy (word_enqueued_ branch)
    modem.update(Tw);         // second copy
    modem.update(2 * Tw);     // third copy

    bool count_ok = (bufs.size() == 3);
    std::cout << "  tx_callback fired 3 times: "
              << (count_ok ? "PASS" : "FAIL")
              << " (got " << bufs.size() << ")\n";

    bool size_ok = count_ok;
    bool identical_01 = count_ok;
    bool identical_12 = count_ok;

    if (count_ok) {
        size_ok = (bufs[0].size() == bufs[1].size()) &&
                  (bufs[1].size() == bufs[2].size());
        std::cout << "  all 3 buffers same size: " << (size_ok ? "PASS" : "FAIL")
                  << " (" << bufs[0].size() << " samples each)\n";

        if (size_ok) {
            identical_01 = (bufs[0] == bufs[1]);
            identical_12 = (bufs[1] == bufs[2]);
            std::cout << "  copy 0 == copy 1: " << (identical_01 ? "PASS" : "FAIL") << "\n";
            std::cout << "  copy 1 == copy 2: " << (identical_12 ? "PASS" : "FAIL") << "\n";
        }
    }

    return count_ok && size_ok && identical_01 && identical_12;
}

// ============================================================================
// AC-FRAME-001-2 (row 2) — SM calls transmit_word() exactly once per logical
// word; call_cycle_count increments per on_word_complete(), not per transmit.
// ============================================================================

bool test_ac_001_2_cycle_count_increments_in_callback()
{
    std::cout << "\n[AC-FRAME-001-2] call_cycle_count increments only in on_word_complete()\n";

    // scan_ch=0: skip SCANNING_CALL, enter LEADING_CALL directly.
    // TO address "BOB" (1 word), self "SAM" (1 word).
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(0);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});

    uint32_t transmit_count = 0;
    sm.set_transmit_callback([&](const ALEWord&) { ++transmit_count; });

    sm.initiate_call("BOB");

    // Before any update: counters at zero
    bool pre_ok = (sm.get_call_cycle_count() == 0 && sm.get_words_pending() == 0);
    std::cout << "  before update: call_cycle_count=0, words_pending=0: "
              << (pre_ok ? "PASS" : "FAIL") << "\n";

    // update(0) → transmit_word() called once (1 word, LEADING seq 1)
    sm.update(0);
    bool tx_fired  = (transmit_count == 1);
    bool count_unchanged = (sm.get_call_cycle_count() == 0);  // NOT yet incremented
    bool pending_up      = (sm.get_words_pending() == 1);

    std::cout << "  after update(0): transmit_word called 1×: "
              << (tx_fired ? "PASS" : "FAIL") << " (got " << transmit_count << ")\n";
    std::cout << "  after update(0): call_cycle_count still 0: "
              << (count_unchanged ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";
    std::cout << "  after update(0): words_pending=1: "
              << (pending_up ? "PASS" : "FAIL")
              << " (got " << sm.get_words_pending() << ")\n";

    // Fire on_word_complete → call_cycle_count must now be 1
    sm.on_word_complete();
    bool count_incremented = (sm.get_call_cycle_count() == 1);
    bool pending_down      = (sm.get_words_pending() == 0);

    std::cout << "  after on_word_complete(): call_cycle_count=1: "
              << (count_incremented ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";
    std::cout << "  after on_word_complete(): words_pending=0: "
              << (pending_down ? "PASS" : "FAIL")
              << " (got " << sm.get_words_pending() << ")\n";

    return pre_ok && tx_fired && count_unchanged && pending_up
        && count_incremented && pending_down;
}

// ============================================================================
// AC-FRAME-001-3 — Inner-state sequence is exclusively
//   SCANNING_CALL → LEADING_CALL → CONCLUSION
// and every transition happens only inside on_word_complete(), never between.
// ============================================================================

bool test_ac_001_3_phase_sequence_via_callback_only()
{
    std::cout << "\n[AC-FRAME-001-3] Phase sequence and callback-only transitions\n";

    // scan_ch=2: Tsc = C×2 = 4 slots; TO "BOB" (1 word); self "SAM" (1 word).
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(2);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_transmit_callback([](const ALEWord&){});

    sm.initiate_call("BOB");

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    std::vector<CallingPhase> phase_log;
    // Record phase BEFORE update() and BEFORE on_word_complete().
    // A transition must not appear between the two — only inside the callback.

    bool transition_outside_callback = false;

    // Helper: record current phase, call update(), check phase unchanged,
    // call on_word_complete(), record new phase.
    auto tick = [&](uint32_t t) {
        CallingPhase before_update = sm.get_calling_phase();
        sm.update(t);
        CallingPhase after_update  = sm.get_calling_phase();
        // Phase must not change inside update() (only on_word_complete() may change it)
        if (before_update != after_update)
            transition_outside_callback = true;

        if (sm.get_words_pending() > 0) {
            sm.on_word_complete();
            phase_log.push_back(sm.get_calling_phase()); // phase after callback
        }
    };

    // Drive 4 SCANNING slots (Tsc = 4 Trw-slots)
    for (uint32_t i = 0; i < 4; ++i)
        tick(i * Trw);

    // Drive 2 LEADING slots (Tlc = 2 × 1 × Trw = 2 Trw-slots for 1-word address)
    for (uint32_t i = 4; i < 6; ++i)
        tick(i * Trw);

    // Drive 1 CONCLUSION slot
    tick(6 * Trw);

    // ── Check no transition happened outside on_word_complete() ──────────
    bool no_outside_transition = !transition_outside_callback;
    std::cout << "  no phase change inside update(): "
              << (no_outside_transition ? "PASS" : "FAIL") << "\n";

    // ── Check phase sequence ──────────────────────────────────────────────
    // Expected: after slot 0–3 → SCANNING; after slot 3 → LEADING; after slots 4–5 → LEADING/CONCLUSION; ...
    // Concretely:
    //   after slot 0: SCANNING (call_cycles_in_phase=1 < 4)
    //   after slot 1: SCANNING (2 < 4)
    //   after slot 2: SCANNING (3 < 4)
    //   after slot 3: LEADING  (4 >= 4 → transition)
    //   after slot 4: LEADING  (1 < 2)
    //   after slot 5: CONCLUSION (2 >= 2 → transition)
    //   after slot 6: LISTENING  (1 >= 1 → transition)

    const CallingPhase expected[] = {
        CallingPhase::SCANNING_CALL,  // after slot 0
        CallingPhase::SCANNING_CALL,  // after slot 1
        CallingPhase::SCANNING_CALL,  // after slot 2
        CallingPhase::LEADING_CALL,   // after slot 3 (transition in callback)
        CallingPhase::LEADING_CALL,   // after slot 4
        CallingPhase::CONCLUSION,     // after slot 5 (transition in callback)
        CallingPhase::LISTENING,      // after slot 6 (transition in callback)
    };

    bool seq_ok = (phase_log.size() == 7);
    std::cout << "  7 on_word_complete() callbacks fired: "
              << (seq_ok ? "PASS" : "FAIL")
              << " (got " << phase_log.size() << ")\n";

    static const char* PNAMES[] = {
        "SCANNING_CALL", "LEADING_CALL", "CONCLUSION", "LISTENING", "NET_CALL_STUB"
    };

    bool all_phases_ok = seq_ok;
    for (size_t i = 0; seq_ok && i < 7; ++i) {
        bool ok = (phase_log[i] == expected[i]);
        all_phases_ok &= ok;
        std::cout << "  slot " << i << ": expected "
                  << PNAMES[static_cast<int>(expected[i])]
                  << ", got "
                  << PNAMES[static_cast<int>(phase_log[i])]
                  << ": " << (ok ? "PASS" : "FAIL") << "\n";
    }

    return no_outside_transition && all_phases_ok;
}

// ============================================================================
// AC-FRAME-001-4 (row 1) — Slot times match first_call_tx_ms + N × Trw_ms.
// Deviation must be < 1 ms.
// ============================================================================

bool test_ac_001_4_slot_timing_formula()
{
    std::cout << "\n[AC-FRAME-001-4] Slot times = first_call_tx_ms + N × Trw_ms\n";

    // scan_ch=1 (Tsc=2), addr="BOB" (1 word), self="SAM" (1 word).
    // Total logical words until end of CONCLUSION: 2 + 2 + 1 = 5.
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(1);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});

    std::vector<uint32_t> tx_times;  // timestamp of each transmit_word() call
    sm.set_transmit_callback([&](const ALEWord&) {
        tx_times.push_back(0);  // placeholder; will be set below
    });

    sm.initiate_call("BOB");

    // first_call_tx_ms = 0 (initiate_call at t=0, first update at t=0)
    const uint32_t t0 = 0;

    // Drive 5 slots: tick at t = N × Trw, ack after each
    for (uint32_t slot = 0; slot < 5; ++slot) {
        uint32_t t = t0 + slot * Trw;
        // Record time just before triggering so we can match to transmit
        size_t before = tx_times.size();
        // Patch transmit_callback to record actual time
        sm.set_transmit_callback([&, t](const ALEWord&) {
            tx_times.push_back(t);
        });
        sm.update(t);
        sm.on_word_complete();

        (void)before;
    }

    bool all_ok = (tx_times.size() == 5);
    std::cout << "  5 words transmitted: "
              << (all_ok ? "PASS" : "FAIL")
              << " (got " << tx_times.size() << ")\n";

    for (size_t i = 0; i < tx_times.size(); ++i) {
        uint32_t expected = t0 + static_cast<uint32_t>(i) * Trw;
        int32_t  deviation = static_cast<int32_t>(tx_times[i]) - static_cast<int32_t>(expected);
        bool ok = (deviation >= -1 && deviation <= 1);
        all_ok &= ok;
        std::cout << "  slot " << i << ": expected t=" << expected
                  << " ms, got t=" << tx_times[i]
                  << " ms, dev=" << deviation
                  << " ms: " << (ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-001-4 (row 2) — Tsc = C×2×Trw, Tlc = 2×wpa×Trw, Tcc = Tsc+Tlc,
// all verified in Trw-slot counts.
// ============================================================================

bool test_ac_001_4_timing_formulas()
{
    std::cout << "\n[AC-FRAME-001-4] Tsc / Tlc / Tcc formulas (Trw-slot counts)\n";

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    struct Case {
        const char* to_addr;
        const char* self;
        uint32_t    scan_ch;
        uint32_t    expected_tsc_slots;   // C × 2
        uint32_t    expected_tlc_slots;   // 2 × wpa
        uint32_t    expected_tcc_slots;   // Tsc + Tlc
    };

    const Case cases[] = {
        // C=1, addr="SAM" (1 word), self="BOB" (1 word)
        { "SAM", "BOB", 1, 2,  2, 4  },
        // C=3, addr="SAM" (1 word), self="BOB"
        { "SAM", "BOB", 3, 6,  2, 8  },
        // C=2, addr="K6KB" (2 words: K6K + B@@), self="BOB"
        { "K6KB", "BOB", 2, 4, 4, 8  },
        // C=1, addr="MIAMI" (2 words: MIA + MI@), self="BOB"
        { "MIAMI", "BOB", 1, 2, 4, 6 },
    };

    bool all_ok = true;

    for (const auto& c : cases) {
        ALEStateMachine sm;
        sm.set_self_address(c.self);
        sm.set_target_scan_channels(c.scan_ch);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([](const ALEWord&){});

        sm.initiate_call(c.to_addr);

        // Count slots in each phase by driving until CONCLUSION phase begins.
        uint32_t scanning_slots = 0;
        uint32_t leading_slots  = 0;

        for (uint32_t slot = 0; slot < 100; ++slot) {
            CallingPhase ph = sm.get_calling_phase();
            if (ph == CallingPhase::CONCLUSION || ph == CallingPhase::LISTENING)
                break;

            sm.update(slot * Trw);
            sm.on_word_complete();

            if (ph == CallingPhase::SCANNING_CALL) ++scanning_slots;
            if (ph == CallingPhase::LEADING_CALL)  ++leading_slots;
        }

        bool tsc_ok  = (scanning_slots == c.expected_tsc_slots);
        bool tlc_ok  = (leading_slots  == c.expected_tlc_slots);
        bool tcc_ok  = ((scanning_slots + leading_slots) == c.expected_tcc_slots);

        all_ok &= tsc_ok && tlc_ok && tcc_ok;

        std::cout << "  to=\"" << c.to_addr << "\" C=" << c.scan_ch
                  << ": Tsc=" << scanning_slots << " (exp " << c.expected_tsc_slots << ")"
                  << " Tlc=" << leading_slots   << " (exp " << c.expected_tlc_slots << ")"
                  << " Tcc=" << (scanning_slots + leading_slots) << " (exp " << c.expected_tcc_slots << ")"
                  << " → " << (tsc_ok && tlc_ok && tcc_ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// Main
// ============================================================================

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis         ║\n";
    std::cout << "║  REQ-FRAME-001 Acceptance Tests                           ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) { ++pass_count; }
        else        { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    run("AC-FRAME-001-2 (modem) ALE2GModem sends 3 identical PCM copies",
        test_ac_001_2_modem_3x_identical_copies());

    run("AC-FRAME-001-2 (SM)    call_cycle_count increments in on_word_complete()",
        test_ac_001_2_cycle_count_increments_in_callback());

    run("AC-FRAME-001-3         phase sequence via callback only",
        test_ac_001_3_phase_sequence_via_callback_only());

    run("AC-FRAME-001-4 (timing) slot times = first_call_tx_ms + N×Trw_ms",
        test_ac_001_4_slot_timing_formula());

    run("AC-FRAME-001-4 (formulas) Tsc/Tlc/Tcc in Trw-slot counts",
        test_ac_001_4_timing_formulas());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count
              << "  Failed: " << std::setw(2) << fail_count
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main()
{
    return ale::run_all_tests();
}
