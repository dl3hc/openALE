/**
 * @file test_lqa_purity.cpp
 * @brief LQA-purity invariant (MIL-STD-188-141B A.5.4.1.1 / A.5.4.1.2).
 *
 * A transmitted sounding produces no *received* words, so entering SOUNDING
 * (the TX side) MUST NOT create an LQA entry. Only stations that *receive* a
 * sounding perform channel measurements (CONTEXT.md "Sounding"). This test
 * drives ALEController into SOUNDING on a fresh channel and asserts the LQA
 * database stays empty — the regression guard for the removed TX-side write
 * that previously fabricated a `ber=0, total_words=1` stub (which
 * from_direction_quality() scored a perfect 30, corrupting channel ranking
 * and the GUI Heard panel).
 */

#include "App/ale_controller.h"
#include "Protocol/Control/ale_channel_types.h"
#include <cassert>
#include <cstdio>

using namespace ale;

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++g_failures; }
    else       { std::printf("  PASS: %s\n", msg); }
}

// Driving SOUNDING from SCANNING keeps a current channel on the SM, so the
// (now-removed) on_state_change write path would have had a non-null channel
// to write under — exactly the condition we are guarding against.
static void test_tx_sounding_creates_no_lqa_entry()
{
    std::printf("\n[TX sounding] transmitting a sounding must not create an LQA entry\n");

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    ctrl.add_channel(Channel(7073000));   // one channel, defaults (rx_only=false)

    // Start scanning so the SM has a current channel, then transmit a sounding.
    ctrl.start_scanning();
    const bool accepted = ctrl.send_sounding();
    check(accepted, "sounding accepted from SCANNING");

    // V1 invariant: the TX-side sounding must not have written an LQA entry.
    auto entry = ctrl.lqa_database().get_entry(7073000, "");
    check(entry == nullptr,
          "no LQA entry created for own TX sounding (A.5.4.1.1/A.5.4.1.2)");
    check(ctrl.lqa_database().get_entry_count() == 0,
          "LQA database empty after TX-only sounding (no received words)");
}

int main()
{
    std::printf("=========================================================\n");
    std::printf("  test_lqa_purity — A.5.4.1.1/A.5.4.1.2 LQA-purity invariant\n");
    std::printf("=========================================================\n");

    test_tx_sounding_creates_no_lqa_entry();

    if (g_failures == 0) { std::printf("\nPASS  all LQA-purity tests\n"); return 0; }
    std::fprintf(stderr, "\n%d failure(s)\n", g_failures);
    return 1;
}