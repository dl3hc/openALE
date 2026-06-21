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
