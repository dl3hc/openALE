/**
 * @file test_lbt_occupancy.cpp
 * @brief LBT occupancy detection (MIL-STD-188-141B A.5.4.7) — unit tests.
 *
 * Part 1 — ChannelOccupancyDetector (A.5.4.7.2 / A.4.2.2 energy detection):
 *   silence is not busy; a tone well above the floor is busy within the N-of-M
 *   window; quiet restores the floor; the operator margin gates the decision
 *   (high local noise + raised margin must NOT block — user requirement).
 *
 * Part 2 — ALEStateMachine LBT windows:
 *   busy query blocks calling (all channels exhausted → NO_CHANNELS_LEFT →
 *   IDLE, no TX) and sounding (aborted, no TX); the A.5.4.7.3 override
 *   bypasses busy; the A.5.4.7.1 shared-channel LBT waits >= 2 s while the
 *   ALE-only LBT keeps the short 784 ms Twt.
 */

#include "Modem/channel_occupancy.h"
#include "Protocol/Control/ale_state_machine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace ale;

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++g_failures; }
    else       { std::printf("  PASS: %s\n", msg); }
}

// ── Part 1: detector ─────────────────────────────────────────────────────────

// Feed n_blocks of 100 ms noise blocks with the given peak-ish amplitude.
static void feed_noise(ChannelOccupancyDetector& d, int n_blocks, int16_t amp)
{
    // Deterministic pseudo-noise (LCG) — enough for an energy detector test.
    static uint32_t seed = 0x1234567u;
    std::vector<int16_t> block(ChannelOccupancyDetector::BLOCK_SAMPLES);
    for (int b = 0; b < n_blocks; ++b) {
        for (auto& s : block) {
            seed = seed * 1664525u + 1013904223u;
            s = static_cast<int16_t>((static_cast<int32_t>(seed >> 16) % (2 * amp + 1)) - amp);
        }
        d.push_samples(block.data(), static_cast<uint32_t>(block.size()));
    }
}

static void feed_tone(ChannelOccupancyDetector& d, int n_blocks, int16_t amp)
{
    std::vector<int16_t> block(ChannelOccupancyDetector::BLOCK_SAMPLES);
    for (int b = 0; b < n_blocks; ++b) {
        for (size_t i = 0; i < block.size(); ++i)
            block[i] = static_cast<int16_t>(
                amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 8000.0));
        d.push_samples(block.data(), static_cast<uint32_t>(block.size()));
    }
}

static void test_detector()
{
    std::printf("[LBT-1] ChannelOccupancyDetector\n");

    ChannelOccupancyDetector d;
    feed_noise(d, 10, 50);                       // quiet channel: floor ≈ noise
    check(!d.is_busy(), "steady low noise is not busy (floor adapted)");

    feed_tone(d, 3, 8000);                       // strong signal appears
    check(d.is_busy(), "strong tone over floor is busy within 3 blocks");

    feed_noise(d, 8, 50);                        // signal ends
    check(!d.is_busy(), "quiet after signal clears busy");

    // Operator margin (user requirement): a modest level rise below the raised
    // margin must NOT read busy — high-noise sites raise the margin instead of
    // being blocked forever.
    ChannelOccupancyDetector d2;
    d2.set_margin_db(20.0f);
    feed_noise(d2, 10, 50);
    feed_noise(d2, 4, 300);                      // ~15 dB above the 50-amp floor
    check(!d2.is_busy(), "level rise below the operator margin is not busy");

    ChannelOccupancyDetector d3;                 // same rise, default 6 dB margin
    feed_noise(d3, 10, 50);
    feed_noise(d3, 4, 300);
    check(d3.is_busy(), "same rise above the default margin IS busy");
}

// ── Part 2: SM LBT windows ───────────────────────────────────────────────────

struct SmHarness {
    ALEStateMachine sm;
    std::vector<ALEWord> tx;
    bool no_channels_left = false;

    explicit SmHarness(bool busy, bool override_on = false, bool shared = false) {
        sm.set_transmit_callback([this](const ALEWord& w){ tx.push_back(w); });
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_channel_callback([](const Channel&){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_operator_callback([this](OperatorEvent e){
            if (e == OperatorEvent::NO_CHANNELS_LEFT) no_channels_left = true;
        });
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(0);
        sm.set_channel_busy_query([busy]{ return busy; });
        sm.set_lbt_override(override_on);
        sm.set_lbt_shared(shared);
    }

    // Drive absolute time in 49 ms ticks up to t_end.
    void run_to(uint32_t t_end) {
        for (uint32_t t = 49; t <= t_end; t += 49) sm.update(t);
        sm.update(t_end);
    }
};

static void test_sm_calling_busy_blocks()
{
    std::printf("[LBT-2] Calling LBT: busy channel → no TX, channels exhausted\n");
    SmHarness h(/*busy=*/true);
    h.sm.set_calling_channels({ Channel(7102000), Channel(10145500) });
    h.sm.initiate_call("JOE");
    h.run_to(8000);
    check(h.tx.empty(),          "no words transmitted on a busy channel");
    check(h.no_channels_left,    "NO_CHANNELS_LEFT reported (all channels busy)");
    check(h.sm.get_state() == ALEState::IDLE, "SM back to IDLE after busy abort");
}

static void test_sm_calling_clear_transmits()
{
    std::printf("[LBT-3] Calling LBT: clear channel → TX proceeds after Twt\n");
    SmHarness h(/*busy=*/false);
    h.sm.set_calling_channels({ Channel(7102000) });
    h.sm.initiate_call("JOE");
    h.run_to(ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms + 200);
    check(!h.tx.empty(), "clear channel: call sequence transmitted");
}

static void test_sm_override_bypasses_busy()
{
    std::printf("[LBT-4] A.5.4.7.3 override: busy ignored, TX proceeds\n");
    SmHarness h(/*busy=*/true, /*override=*/true);
    h.sm.set_calling_channels({ Channel(7102000) });
    h.sm.initiate_call("JOE");
    h.run_to(ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms + 200);
    check(!h.tx.empty(),      "override: call transmitted despite busy query");
    check(!h.no_channels_left, "override: no busy abort");
}

static void test_sm_shared_lbt_duration()
{
    std::printf("[LBT-5] A.5.4.7.1: shared-channel LBT waits >= 2 s\n");
    SmHarness h(/*busy=*/false, /*override=*/false, /*shared=*/true);
    h.sm.set_calling_channels({ Channel(7102000) });
    h.sm.initiate_call("JOE");
    // At the old ALE-only budget (784 + Tt) nothing may be transmitted yet:
    // the shared LBT still listens (2000 ms > 784 + 1045).
    h.run_to(ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms);
    check(h.tx.empty(), "shared LBT: still listening at Twt(784)+Tt");
    // After 2000 ms LBT + Tt the sequence must flow.
    h.run_to(ALETimingConstants::Twt_shared_ms + ALETimingConstants::Tt_ms + 200);
    check(!h.tx.empty(), "shared LBT: transmitted after 2 s + Tt");
}

static void test_sm_sounding_busy_aborts()
{
    std::printf("[LBT-6] Sounding LBT: busy channel → sounding aborted, no TX\n");
    SmHarness h(/*busy=*/true);
    const bool accepted = h.sm.send_sounding();
    check(accepted, "sounding accepted from IDLE");
    h.run_to(4000);
    check(h.tx.empty(), "no sounding words transmitted on a busy channel");
    check(h.sm.get_state() == ALEState::IDLE, "SM back to IDLE after busy sounding abort");
}

static void test_sm_sounding_clear_transmits()
{
    std::printf("[LBT-7] Sounding LBT: clear channel → sounding transmits\n");
    SmHarness h(/*busy=*/false);
    const bool accepted = h.sm.send_sounding();
    check(accepted, "sounding accepted from IDLE");
    h.run_to(ALETimingConstants::Twt_ms + 500);
    check(!h.tx.empty(), "clear channel: sounding transmitted after Twt");
}

int main()
{
    std::printf("=========================================================\n");
    std::printf("  test_lbt_occupancy — A.5.4.7 listen-before-transmit\n");
    std::printf("=========================================================\n");

    test_detector();
    test_sm_calling_busy_blocks();
    test_sm_calling_clear_transmits();
    test_sm_override_bypasses_busy();
    test_sm_shared_lbt_duration();
    test_sm_sounding_busy_aborts();
    test_sm_sounding_clear_transmits();

    if (g_failures == 0) { std::printf("\nPASS  all LBT occupancy tests\n"); return 0; }
    std::fprintf(stderr, "\n%d failure(s)\n", g_failures);
    return 1;
}
