/**
 * \file test_sync.cpp
 * \brief Unit tests for FEAT-SYNC-001 — Trw-Grid (Sendeseitiger Wortphasen-Anker)
 *
 * AC-SYNC-001-001: first_call_tx_ms is set exactly once (at TUNING-complete)
 *   and never reset or overwritten during the TX call cycle.
 *
 * Timing model (DD-006, DD-013):
 *   first_call_tx_ms = 0 at enter_state(CALLING)           (sentinel "not yet set")
 *   first_call_tx_ms = current_time_ms at TUNING-complete  (set once)
 *   Immutable thereafter for the entire TX sequence:
 *     SCANNING_CALL → LEADING_CALL → CONCLUSION → LISTENING
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include <iostream>
#include <iomanip>
#include <cstdint>

namespace ale {

// ============================================================================
// AC-SYNC-001-001 — first_call_tx_ms: Einmalig gesetzt, nie zurückgesetzt
//
// Arrange: SAM calls BOB with scan_ch=1.
//   TX sequence = 2 SCANNING_CALL + 2 LEADING_CALL + 1 CONCLUSION = 5 words.
//
// Assert:
//   (1) After enter_state(CALLING), before TUNING completes: first_call_tx_ms == 0
//   (2) At TUNING-complete (current_time_ms == T_TX):        first_call_tx_ms == T_TX
//   (3) After each of the 5 on_word_complete() calls:        first_call_tx_ms unchanged
// ============================================================================

bool test_ac_sync_001_001_first_call_tx_ms_set_once()
{
    std::cout << "\n[AC-SYNC-001-001] first_call_tx_ms: set once at TUNING-complete, never reset\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(1);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_transmit_callback([](const ALEWord&){});

    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;
    const uint32_t T_TX = Twt + Tt;

    sm.initiate_call("BOB");

    // (1) Immediately after entering CALLING — before TUNING completes: must be 0
    sm.update(0);
    bool before_zero = (sm.get_first_call_tx_ms() == 0);
    std::cout << "  before TUNING: first_call_tx_ms == 0: "
              << (before_zero ? "PASS" : "FAIL")
              << " (got " << sm.get_first_call_tx_ms() << ")\n";

    // LBT phase passes
    sm.update(Twt);
    bool during_lbt_zero = (sm.get_first_call_tx_ms() == 0);
    std::cout << "  after LBT:     first_call_tx_ms == 0: "
              << (during_lbt_zero ? "PASS" : "FAIL")
              << " (got " << sm.get_first_call_tx_ms() << ")\n";

    // TUNING completes — first_call_tx_ms is set exactly once here
    sm.update(T_TX);
    const uint32_t anchor = sm.get_first_call_tx_ms();
    bool anchor_set  = (anchor == T_TX);
    bool anchor_nonzero = (anchor != 0);
    std::cout << "  after TUNING:  first_call_tx_ms == T_TX (" << T_TX << "): "
              << (anchor_set ? "PASS" : "FAIL")
              << " (got " << anchor << ")\n";
    std::cout << "  after TUNING:  first_call_tx_ms != 0: "
              << (anchor_nonzero ? "PASS" : "FAIL") << "\n";

    // (3) Drive through all 5 TX words; first_call_tx_ms must not change
    const uint32_t total_words = 5;
    bool immutable_ok = true;
    for (uint32_t n = 0; n < total_words; ++n) {
        sm.on_word_complete();
        uint32_t current_anchor = sm.get_first_call_tx_ms();
        bool unchanged = (current_anchor == anchor);
        immutable_ok &= unchanged;
        std::cout << "  word " << n << " on_word_complete(): first_call_tx_ms == "
                  << anchor << ": "
                  << (unchanged ? "PASS" : "FAIL")
                  << (unchanged ? "" : " (got " + std::to_string(current_anchor) + ")")
                  << "\n";
    }

    bool all_ok = before_zero && during_lbt_zero && anchor_set && anchor_nonzero && immutable_ok;
    std::cout << "  => AC-SYNC-001-001: " << (all_ok ? "PASS" : "FAIL") << "\n";
    return all_ok;
}

// ============================================================================
// AC-SYNC-001-002 — Alle Slot-Zeiten relativ zu first_call_tx_ms
//
// Verifies: next_slot_ms = first_call_tx_ms + call_cycle_count × Trw_ms
//           is the sole timing basis for every TX slot.
//
// Arrange: SAM calls BOB with scan_ch=2.
//   TX sequence = 4 SCANNING_CALL + 2 LEADING_CALL + 1 CONCLUSION = 7 words.
//
// Assert:
//   (1) first_call_tx_ms == T_TX (anchor set at TUNING-complete)
//   (2) For each word N (0..6):
//         slot_formula = first_call_tx_ms + call_cycle_count × Trw_ms
//         == T_TX + N × Trw_ms   (expected slot time)
//   (3) call_cycle_count == N before word N; increments to N+1 after
//       on_word_complete()
//   (4) No other time basis: calling update() with the same wall-clock
//       time throughout TX phases does not affect slot computation — the
//       SM's TX phases (SCANNING_CALL, LEADING_CALL, CONCLUSION) perform
//       no wall-clock check and are driven solely by on_word_complete().
// ============================================================================

bool test_ac_sync_001_002_slot_times_relative_to_first_call_tx_ms()
{
    std::cout << "\n[AC-SYNC-001-002] All TX slot times relative to first_call_tx_ms\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(2);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_transmit_callback([](const ALEWord&){});

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Twt  = ALETimingConstants::Twt_ms;
    const uint32_t Tt   = ALETimingConstants::Tt_ms;
    const uint32_t T_TX = Twt + Tt;

    sm.initiate_call("BOB");

    sm.update(Twt);   // LBT → TUNING
    sm.update(T_TX);  // TUNING complete → SCANNING_CALL, first_call_tx_ms set

    // (1) Grid anchor == TUNING-complete timestamp
    const uint32_t anchor = sm.get_first_call_tx_ms();
    bool anchor_ok = (anchor == T_TX);
    std::cout << "  first_call_tx_ms == T_TX (" << T_TX << "): "
              << (anchor_ok ? "PASS" : "FAIL")
              << " (got " << anchor << ")\n";

    // scan_ch=2: 4 scan + 2 leading + 1 conclusion = 7 words
    const uint32_t total_words = 7;
    bool formula_ok = true;

    for (uint32_t n = 0; n < total_words; ++n) {
        // (2) + (3): formula check before consuming word N
        uint32_t expected_slot = T_TX + n * Trw;
        uint32_t cycle_before  = sm.get_call_cycle_count();
        uint32_t formula_slot  = anchor + cycle_before * Trw;

        bool count_ok  = (cycle_before == n);
        bool slot_ok   = (formula_slot == expected_slot);
        formula_ok    &= (count_ok && slot_ok);

        std::cout << "  word " << n
                  << ": call_cycle_count==" << n << ": " << (count_ok ? "PASS" : "FAIL")
                  << "  slot=" << formula_slot << " (exp " << expected_slot << "): "
                  << (slot_ok ? "PASS" : "FAIL") << "\n";

        // (4) Passing the same T_TX to update() during a TX phase has no
        //     effect — the SM just breaks; only on_word_complete() advances.
        sm.update(T_TX);
        bool count_unchanged = (sm.get_call_cycle_count() == n);
        if (!count_unchanged) {
            std::cout << "  FAIL: update() during TX phase advanced call_cycle_count!\n";
            formula_ok = false;
        }

        sm.on_word_complete();
    }

    // After all 7 words call_cycle_count == 7
    bool final_count_ok = (sm.get_call_cycle_count() == total_words);
    std::cout << "  call_cycle_count == " << total_words << " after all words: "
              << (final_count_ok ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";

    bool all_ok = anchor_ok && formula_ok && final_count_ok;
    std::cout << "  => AC-SYNC-001-002: " << (all_ok ? "PASS" : "FAIL") << "\n";
    return all_ok;
}

// ============================================================================
// Main
// ============================================================================

static int pass_count = 0;
static int fail_count = 0;

static void run(const char* name, bool result)
{
    if (result) {
        ++pass_count;
        std::cout << "[PASS] " << name << "\n";
    } else {
        ++fail_count;
        std::cout << "[FAIL] " << name << "\n";
    }
}

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  test_sync — FEAT-SYNC-001 Trw-Grid Anker                 ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    run("AC-SYNC-001-001  first_call_tx_ms: set once at TUNING-complete, never reset",
        test_ac_sync_001_001_first_call_tx_ms_set_once());

    run("AC-SYNC-001-002  All TX slot times = first_call_tx_ms + call_cycle_count × Trw_ms",
        test_ac_sync_001_002_slot_times_relative_to_first_call_tx_ms());

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
