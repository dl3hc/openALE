/**
 * \file test_channel_selector.cpp
 * \brief Unit tests for ChannelSelector (AC-CHAN-001-001)
 *
 * Verifies that ChannelSelector always selects from a predefined channel_set
 * using LQA scores — never randomly without an LQA basis.
 *
 * MIL-STD-188-141B Appendix A, Section A.5.2.2
 */

#include "Protocol/Control/ale_channel_selector.h"
#include "Stores/ale_data_store.h"
#include <iostream>

namespace ale {

// ── helpers ───────────────────────────────────────────────────────────────────

static ChannelStore make_store(std::initializer_list<uint32_t> freqs,
                               bool all_enabled = true)
{
    ChannelStore cs;
    for (uint32_t f : freqs) {
        Channel ch(f);
        ch.enabled = all_enabled;
        cs.add_channel(ch);
    }
    return cs;
}

// ── test functions ────────────────────────────────────────────────────────────

bool test_best_lqa_selects_highest_scored_channel()
{
    std::cout << "\n[ChannelSelector] BEST_LQA picks highest-scored channel (AC-CHAN-001-001)\n";

    // Arrange
    ChannelStore cs = make_store({7100000, 14250000, 3500000});
    LQAStore     lqa;

    // Give 14.250 the best score (SNR 25 dB, BER 0)
    lqa.record(7100000,  "REMOTE", 10.0f, 0.05f, 0, 10, 1000);
    lqa.record(14250000, "REMOTE", 25.0f, 0.00f, 0, 10, 1000);
    lqa.record(3500000,  "REMOTE",  5.0f, 0.10f, 2, 10, 1000);

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::BEST_LQA);

    // Act
    uint32_t chosen = sel.select_for_call("REMOTE");

    // Assert
    bool ok = (chosen == 14250000);
    std::cout << "  best channel is 14.250 MHz: " << (ok ? "PASS" : "FAIL")
              << " (got " << chosen << " Hz)\n";
    return ok;
}

bool test_best_lqa_no_random_fallback_when_lqa_data_present()
{
    std::cout << "\n[ChannelSelector] BEST_LQA result is deterministic — not random (AC-CHAN-001-001)\n";

    ChannelStore cs = make_store({7100000, 14250000, 3500000});
    LQAStore     lqa;
    lqa.record(7100000,  "R", 20.0f, 0.01f, 0, 5, 1000);
    lqa.record(14250000, "R", 28.0f, 0.00f, 0, 5, 1000);
    lqa.record(3500000,  "R",  3.0f, 0.20f, 5, 5, 1000);

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::BEST_LQA);

    // Call 10 times — must always return the same (best) channel
    uint32_t first = sel.select_for_call("R");
    bool deterministic = true;
    for (int i = 0; i < 9; ++i) {
        if (sel.select_for_call("R") != first) { deterministic = false; break; }
    }

    bool ok = (first == 14250000) && deterministic;
    std::cout << "  BEST_LQA always returns 14.250 MHz: " << (ok ? "PASS" : "FAIL")
              << " (first=" << first << ", deterministic=" << (deterministic ? "yes" : "no") << ")\n";
    return ok;
}

bool test_best_lqa_fallback_to_first_when_no_lqa_data()
{
    std::cout << "\n[ChannelSelector] no LQA data -> falls back to first enabled channel (AC-CHAN-001-001)\n";

    // Three channels, no LQA data recorded at all
    ChannelStore cs = make_store({7100000, 14250000, 3500000});
    LQAStore     lqa;  // empty

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::BEST_LQA);

    uint32_t chosen = sel.select_for_call("NOBODY");

    // Without LQA data all scores are -1; best_channel() returns candidates.front() = 7.1 MHz
    bool ok = (chosen == 7100000);
    std::cout << "  fallback is first channel 7.1 MHz: " << (ok ? "PASS" : "FAIL")
              << " (got " << chosen << " Hz)\n";
    return ok;
}

bool test_fixed_policy_ignores_lqa()
{
    std::cout << "\n[ChannelSelector] FIXED policy always returns configured channel\n";

    ChannelStore cs = make_store({7100000, 14250000, 3500000});
    LQAStore     lqa;
    lqa.record(14250000, "X", 30.0f, 0.0f, 0, 10, 1000);  // best LQA — must be ignored

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::FIXED);
    sel.set_fixed_channel(3500000);

    uint32_t chosen = sel.select_for_call("X");
    bool ok = (chosen == 3500000);
    std::cout << "  FIXED returns 3.5 MHz regardless of LQA: " << (ok ? "PASS" : "FAIL")
              << " (got " << chosen << " Hz)\n";
    return ok;
}

bool test_round_robin_cycles_all_channels()
{
    std::cout << "\n[ChannelSelector] ROUND_ROBIN cycles through all enabled channels\n";

    ChannelStore cs = make_store({7100000, 14250000, 3500000});
    LQAStore     lqa;

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::ROUND_ROBIN);

    uint32_t r0 = sel.select_for_call("X");
    uint32_t r1 = sel.select_for_call("X");
    uint32_t r2 = sel.select_for_call("X");
    uint32_t r3 = sel.select_for_call("X");  // wraps back to r0

    bool all_three = (r0 != r1 && r1 != r2 && r0 != r2);
    bool wrapped   = (r3 == r0);
    bool ok = all_three && wrapped;

    std::cout << "  all three channels visited: " << (all_three ? "PASS" : "FAIL")
              << " [" << r0 << " " << r1 << " " << r2 << "]\n";
    std::cout << "  wraps back to first on 4th call: " << (wrapped ? "PASS" : "FAIL")
              << " (got " << r3 << ")\n";
    return ok;
}

bool test_best_lqa_only_considers_enabled_channels()
{
    std::cout << "\n[ChannelSelector] BEST_LQA skips disabled channels (AC-CHAN-001-001)\n";

    ChannelStore cs;
    Channel c1(14250000); c1.enabled = true;  cs.add_channel(c1);
    Channel c2(7100000);  c2.enabled = false; cs.add_channel(c2);  // disabled
    Channel c3(3500000);  c3.enabled = true;  cs.add_channel(c3);

    LQAStore lqa;
    // 7.1 MHz has the best LQA but is disabled — must not be selected
    lqa.record(7100000,  "R", 30.0f, 0.0f, 0, 10, 1000);
    lqa.record(14250000, "R", 20.0f, 0.0f, 0, 10, 1000);
    lqa.record(3500000,  "R",  5.0f, 0.1f, 0, 10, 1000);

    ChannelSelector sel(cs, lqa);
    sel.set_policy(SelectionPolicy::BEST_LQA);

    uint32_t chosen = sel.select_for_call("R");

    // 14.250 MHz has the best score among enabled channels
    bool ok = (chosen == 14250000);
    std::cout << "  selected 14.250 MHz (not disabled 7.1 MHz): " << (ok ? "PASS" : "FAIL")
              << " (got " << chosen << " Hz)\n";
    return ok;
}

bool test_no_channels_returns_zero()
{
    std::cout << "\n[ChannelSelector] empty channel list returns 0\n";

    ChannelStore cs;  // empty
    LQAStore     lqa;

    ChannelSelector sel(cs, lqa);
    bool ok = (sel.select_for_call("X") == 0);
    std::cout << "  returns 0 for empty channel list: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// ── runner ────────────────────────────────────────────────────────────────────

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  ChannelSelector unit tests (AC-CHAN-001-001)\n";
    std::cout << "================================================================\n";

    int pass = 0, fail = 0;
    auto run = [&](const char* name, bool result) {
        if (result) ++pass; else { ++fail; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    run("BEST_LQA selects highest-scored channel",
        test_best_lqa_selects_highest_scored_channel());
    run("BEST_LQA is deterministic (not random)",
        test_best_lqa_no_random_fallback_when_lqa_data_present());
    run("no LQA data -> first enabled channel (no random fallback)",
        test_best_lqa_fallback_to_first_when_no_lqa_data());
    run("FIXED policy ignores LQA",
        test_fixed_policy_ignores_lqa());
    run("ROUND_ROBIN cycles all channels",
        test_round_robin_cycles_all_channels());
    run("BEST_LQA skips disabled channels",
        test_best_lqa_only_considers_enabled_channels());
    run("empty channel list returns 0",
        test_no_channels_returns_zero());

    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  Results — Passed: " << pass << "  Failed: " << fail << "\n";
    std::cout << "================================================================\n\n";

    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
