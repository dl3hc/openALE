/**
 * @file test_test_channel.cpp
 * @brief Regression guard for the active Test-Channel sweep (CMD:TEST_CHANNEL).
 *
 * The Test-Channel routine actively links to a peer on each configured channel,
 * records LQA, terminates, and advances. This test exercises the controller-side
 * async driver (tick_test_channel) with a MockRadio and no peer, so every channel
 * attempt fails the handshake and the sweep walks all channels to completion.
 *
 * Asserts:
 *   - start_test_channel() validates the target address and the SM state.
 *   - A "start" event is emitted with the correct channel total.
 *   - The driver visits every channel in order (one "tune" + one "failed" event
 *     per channel) and emits a "done" event; the sweep goes inactive.
 *   - stop_test_channel() mid-sweep emits a "stop" event and clears active.
 *   - The radio is returned to the channel it was on when the sweep started,
 *     on both DONE and STOP.
 *   - process_command() routes CMD:TEST_CHANNEL / CMD:TEST_CHANNEL_STOP.
 *
 * All test output goes through the PAL logger (pal::log_info / pal::log_error),
 * not printf/fprintf. No wall-clock sleeping: the SM is advanced by driving
 * update(now_ms) with a fast-forward clock; the over-the-air timing
 * (LBT/Tune/Listen) resolves in simulated time. MockRadio (header-only, see
 * PAL/radios/mock_radio.h) backs set_vfo_channel() so each channel's tune is real.
 */

#include "App/ale_controller.h"
#include "App/ale_event_data.h"
#include "Protocol/Control/ale_channel_types.h"
#include "PAL/events.h"
#include "PAL/logger.h"
#include "PAL/radios/mock_radio.h"
#include <string>
#include <vector>

using namespace ale;

static const char* kMod = "TestChannel";

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { pal::log_error(kMod, "  FAIL: %s", msg); ++g_failures; }
    else       { pal::log_info(kMod, "  PASS: %s", msg); }
}

struct EvRec {
    std::string phase;
    std::string peer;
    std::string channel_id;
    uint32_t    index;
    uint32_t    total;
    int         score;
    bool        linked;
};
static std::vector<EvRec> g_events;

int main()
{
    // Install the PAL console logger so pal::log_info/log_error produce output
    // (get_logger() returns null until set_logger() is called → silent no-op).
    pal::set_logger(pal::create_logger());

    pal::log_info(kMod, "=========================================================");
    pal::log_info(kMod, "  test_test_channel — active Test-Channel sweep driver");
    pal::log_info(kMod, "=========================================================");

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    pal::MockRadio radio;
    ctrl.set_radio(&radio);

    // Capture ALE_TEST_CHANNEL events on a dedicated bus.
    auto bus = pal::create_event_handler();
    bus->on(pal::EventType::ALE_TEST_CHANNEL, [](const pal::Event& ev) {
        const auto* d = static_cast<const TestChannelData*>(ev.data);
        g_events.push_back(EvRec{
            d->phase ? d->phase : "",
            d->peer ? d->peer : "",
            d->channel_id ? d->channel_id : "",
            d->index, d->total, d->score, d->linked
        });
    });
    pal::set_event_handler(std::move(bus));

    // 3 callable ALE-only channels (short 784 ms LBT keeps the test fast).
    const uint32_t freqs[3] = { 7073000u, 14109000u, 21096000u };
    for (uint32_t f : freqs) {
        Channel ch(f);
        ch.enabled  = true;
        ch.ale_only = true;
        ctrl.add_channel(ch);
    }

    // ── Validation ──────────────────────────────────────────────────────────
    pal::log_info(kMod, "[validation] bad address / state rejected");
    check(!ctrl.start_test_channel("X"), "invalid address (too short) rejected");
    // Park the radio on a "home" channel distinct from the test set so we can
    // verify the sweep returns the radio there when it finishes (not leave it
    // on the last tested channel).
    ctrl.set_vfo_channel(3600000u, "USB");
    check(ctrl.start_test_channel("JOE"), "start_test_channel accepted from IDLE");

    // ── Start ───────────────────────────────────────────────────────────────
    pal::log_info(kMod, "[start] sweep begins and emits a start event");
    check(ctrl.test_channel_active(), "sweep is active after start");
    check(!g_events.empty() && g_events.front().phase == "start",
          "start event emitted first");
    check(g_events.front().total == 3, "start event reports 3 channels total");
    check(g_events.front().peer == "JOE", "start event carries peer JOE");

    // ── Drive to completion ──────────────────────────────────────────────────
    pal::log_info(kMod, "[drive] walk all channels with no peer -> all fail -> done");
    uint32_t now = 0;
    const uint32_t STEP_MS = 50u;
    const int MAX_TICKS = 20000;
    int ticks = 0;
    while (ctrl.test_channel_active() && ticks < MAX_TICKS) {
        now += STEP_MS;
        ctrl.update(now);
        ++ticks;
    }
    check(ticks < MAX_TICKS, "sweep completed before tick cap (no hang)");
    check(!ctrl.test_channel_active(), "sweep inactive after driving to completion");

    int n_tune = 0, n_failed = 0, n_done = 0;
    for (const auto& e : g_events) {
        if (e.phase == "tune")   { ++n_tune; }
        if (e.phase == "failed") { ++n_failed; }
        if (e.phase == "done")   { ++n_done; }
    }
    check(n_tune == 3, "one tune event per channel (3)");
    check(n_failed == 3, "one failed event per channel (3 — no peer in test)");
    check(n_done == 1, "exactly one done event emitted");
    check(ctrl.get_current_frequency() == 3600000u,
          "radio returned to original (home) channel after sweep completed");

    // ── Stop mid-sweep ───────────────────────────────────────────────────────
    pal::log_info(kMod, "[stop] mid-sweep abort emits stop and clears active");
    g_events.clear();
    check(ctrl.start_test_channel("JOE"), "second sweep started");
    for (int i = 0; i < 6 && ctrl.test_channel_active(); ++i) {
        now += STEP_MS;
        ctrl.update(now);
    }
    check(ctrl.test_channel_active(), "sweep active mid-flight before stop");
    check(ctrl.get_current_frequency() != 3600000u,
          "radio tuned away from home during the sweep");
    ctrl.stop_test_channel();
    check(!ctrl.test_channel_active(), "sweep inactive after stop_test_channel");
    bool got_stop = false;
    for (const auto& e : g_events) if (e.phase == "stop") got_stop = true;
    check(got_stop, "stop event emitted on abort");
    check(ctrl.get_current_frequency() == 3600000u,
          "radio returned to original (home) channel after STOP");

    // ── process_command routing ─────────────────────────────────────────────
    pal::log_info(kMod, "[cmd] process_command routes TEST_CHANNEL / STOP");
    std::string r_stop = ctrl.process_command("CMD:TEST_CHANNEL_STOP");
    check(r_stop.rfind("ERROR:", 0) == 0,
          "TEST_CHANNEL_STOP with no sweep in progress -> ERROR");

    std::string r_start = ctrl.process_command("CMD:TEST_CHANNEL JOE");
    check(r_start.rfind("OK:", 0) == 0,
          "CMD:TEST_CHANNEL JOE accepted via process_command");
    check(ctrl.test_channel_active(), "sweep active after CMD:TEST_CHANNEL");
    // Leave the process clean: stop the sweep we just started.
    std::string r_stop2 = ctrl.process_command("CMD:TEST_CHANNEL_STOP");
    check(r_stop2.rfind("OK:", 0) == 0,
          "CMD:TEST_CHANNEL_STOP on active sweep -> OK");
    check(!ctrl.test_channel_active(), "sweep inactive after CMD:TEST_CHANNEL_STOP");

    if (g_failures == 0) { pal::log_info(kMod, "PASS  all test_test_channel tests"); return 0; }
    pal::log_error(kMod, "%d failure(s)", g_failures);
    return 1;
}