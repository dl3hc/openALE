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
    d.set_active(true);                          // controller syncs this each tick
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
    d2.set_active(true);
    d2.set_margin_db(20.0f);
    feed_noise(d2, 10, 50);
    feed_noise(d2, 4, 300);                      // ~15 dB above the 50-amp floor
    check(!d2.is_busy(), "level rise below the operator margin is not busy");

    ChannelOccupancyDetector d3;                 // same rise, default 6 dB margin
    d3.set_active(true);
    feed_noise(d3, 10, 50);
    feed_noise(d3, 4, 300);
    check(d3.is_busy(), "same rise above the default margin IS busy");
}

// Active gating (regression for the "FREQ BUSY never clears" pill bug):
// the detector's stored busy flag is the single LBT-busy truth, read by the
// SM decision and the GUI pill.  A.5.4.7.2 detection cannot run without an RX
// stream, so going inactive (RX closed during TX, or LBT disabled) must clear
// the flag and starve push_samples — no sticky busy latch outliving its window.
static void test_active_gating()
{
    std::printf("[LBT-1b] Active gating — no sticky busy without an RX stream\n");

    ChannelOccupancyDetector d;
    d.set_active(true);
    feed_noise(d, 10, 50);
    feed_tone(d, 3, 8000);
    check(d.is_busy(), "detector goes busy while active");

    d.set_active(false);                         // RX closes (TX) / LBT disabled
    check(!d.is_busy(), "busy flag clears immediately on going inactive");

    feed_tone(d, 5, 8000);                       // would be busy, but no RX stream
    check(!d.is_busy(), "push_samples is a no-op while inactive (no stale busy)");

    d.set_active(true);                          // RX reopens — fresh start: the
                                                 // floor is re-learned (a TX is an
                                                 // RX-off period), not retained.
    feed_noise(d, 4, 50);                        // ambient noise re-establishes the floor
    check(!d.is_busy(), "quiet after reactivation not busy (floor re-learned)");
    feed_tone(d, 1, 8000);                       // one hot block: 2-of-4 not met
    check(!d.is_busy(), "vote reset on reactivation — single hot block not busy");
    feed_tone(d, 1, 8000);                       // second hot block: 2-of-4 → busy
    check(d.is_busy(), "detection resumes after reactivation (2-of-4 hot)");
}

// reset() is the channel-change contract: ALEController::notify_channel_changed_
// calls it whenever the RX frequency changes, so the EWMA floor (channel-specific)
// and any busy/vote from the previous channel cannot bleed into the new one.
static void test_reset_clears_state()
{
    std::printf("[LBT-1c] reset() drops floor + busy (channel-change contract)\n");

    ChannelOccupancyDetector d;
    d.set_active(true);
    feed_noise(d, 10, 50);
    feed_tone(d, 3, 8000);
    check(d.is_busy(), "busy before reset");

    d.reset();                                   // controller: RX frequency changed
    check(!d.is_busy(), "busy cleared by reset");

    // Floor is relearned from the first post-reset block; a quiet channel must
    // read not-busy immediately — no inherited high floor from the tone before.
    feed_noise(d, 4, 50);
    check(!d.is_busy(), "quiet channel not busy after reset (floor relearned)");
}

// Post-TX AGC pump (regression for "stays FREQ BUSY all the time after
// transmitting"): after RX reopens the radio's RX gain is pumped, so the noise
// level is far above the pre-TX floor. The detector must re-learn the floor on
// RX reopen (set_active(false) drops floor_valid_) — otherwise every post-TX
// block is "hot" against the stale floor and, hot blocks being excluded from
// ALPHA_UP, the floor only creeps up via the 0.01 dB/block drift (~60 s per
// 6 dB), keeping the pill BUSY for minutes. Steady elevated noise after TX is
// NOT a signal and must read CLEAR.
static void test_floor_relearned_after_inactive()
{
    std::printf("[LBT-1d] floor re-learned on RX reopen (post-TX AGC pump)\n");

    ChannelOccupancyDetector d;
    d.set_active(true);
    feed_noise(d, 10, 50);                       // quiet channel, floor ≈ 50-amp noise
    check(!d.is_busy(), "quiet channel not busy before TX");

    d.set_active(false);                         // TX: RX closes — forget floor
    check(!d.is_busy(), "clear while inactive");

    // RX reopens to a ~18 dB higher STEADY noise level (AGC pumped after TX).
    d.set_active(true);
    feed_noise(d, 6, 400);
    check(!d.is_busy(), "steady elevated noise after TX is not busy (floor re-learned)");

    // A real signal on top of the new floor still drives busy.
    feed_tone(d, 3, 8000);
    check(d.is_busy(), "signal above the re-learned floor is still busy");
}

// clear_busy_latch() (regression for "LBT immediately fires on menu Call /
// Sounding"): the SM's LBT phase polls is_busy() from the very first tick, and
// RX is still open during LBT, so a busy latch left over from a transient hot
// block in the last ~400 ms of idle dwelling would abort the call/sounding on
// tick 0 — before the Twt window can re-evaluate.  The controller calls
// clear_busy_latch() when the operator requests a TX so the LBT window measures
// fresh.  Unlike reset(), it KEEPS the tracked floor: a genuine continuous
// signal during the LBT window must still re-latch busy (clear_busy_latch must
// not become a way to talk over an occupied channel).
static void test_clear_busy_latch_keeps_floor()
{
    std::printf("[LBT-1e] clear_busy_latch: drops verdict, keeps floor\n");

    ChannelOccupancyDetector d;
    d.set_active(true);
    feed_noise(d, 10, 50);                          // quiet dwell: floor ≈ 50-amp
    feed_tone(d, 3, 8000);                          // a passing signal latches busy
    check(d.is_busy(), "busy from a transient signal during dwelling");

    d.clear_busy_latch();                           // operator hits "Call"
    check(!d.is_busy(), "stale busy latch cleared at TX request (no tick-0 abort)");

    // Quiet LBT window: floor retained → blocks not hot → stays clear → TX proceeds.
    feed_noise(d, 4, 50);
    check(!d.is_busy(), "quiet LBT window stays clear (floor retained, not re-learned)");

    // A genuine signal during the LBT window must re-latch busy within the
    // N-of-M vote — clear_busy_latch must NOT let a real signal through.
    feed_tone(d, 3, 8000);
    check(d.is_busy(), "genuine signal during LBT re-latches busy (floor kept)");
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
    test_active_gating();
    test_reset_clears_state();
    test_floor_relearned_after_inactive();
    test_clear_busy_latch_keeps_floor();
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
