/**
 * @file test_sounding_timing.cpp
 * @brief Auto-sounding cadence: the next sounding fires `interval` after the
 *        previous sounding cycle COMPLETES, not after it STARTED.
 *
 * Regression guard for the auto-sounding timer re-arm bug: tick_sounding_sweep()
 * previously stamped auto_sounding_last_ms_ at sweep fire (entry), so with a
 * 25 s cycle + 30 s interval only ~5 s elapsed after the cycle before the next
 * sounding.  The fix defers the re-arm to cycle END (on_sm_state_change leaving
 * SOUNDING).  This test drives ALEController with a MockRadio and no audio
 * (tick_offline_completion drains the TX), a short sounding interval, and a
 * 2-channel net, and asserts the gap between the first sweep's completion and
 * the second sweep's fire equals the configured interval (within tick
 * granularity) — not interval-minus-cycle.
 *
 * All output via the PAL logger.  No wall-clock sleeping: the SM is advanced by
 * driving update(now_ms) with a fast-forward clock.
 */

#include "App/ale_controller.h"
#include "Protocol/Control/ale_channel_types.h"
#include "Protocol/Control/ale_state_machine.h"
#include "PAL/logger.h"
#include "PAL/events.h"
#include "PAL/radios/mock_radio.h"
#include "Stores/ale_data_store.h"

using namespace ale;

static const char* kMod = "SoundingTiming";

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { pal::log_error(kMod, "  FAIL: %s", msg); ++g_failures; }
    else       { pal::log_info(kMod, "  PASS: %s", msg); }
}

// Drive update() until the SM enters (enter==true) or leaves (enter==false)
// SOUNDING, recording the simulated `now` at the transition.  Returns the
// transition time, or 0 if the cap was hit without a transition.
static uint32_t wait_for_sounding_edge(ALEController& ctrl, uint32_t& now,
                                       uint32_t step_ms, int max_ticks, bool enter)
{
    for (int i = 0; i < max_ticks; ++i) {
        now += step_ms;
        ctrl.update(now);
        const bool in_snd = (ctrl.state() == ALEState::SOUNDING);
        if (enter == in_snd) return now;
    }
    return 0;  // not reached within cap
}

static void test_auto_sounding_interval_from_cycle_end()
{
    pal::log_info(kMod, "[cadence] next sounding fires interval after cycle END");

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    pal::MockRadio radio;
    ctrl.set_radio(&radio);

    // 2 ALE-only channels (short 784 ms LBT keeps the sweep fast).
    const uint32_t freqs[2] = { 7073000u, 14109000u };
    for (uint32_t f : freqs) {
        Channel ch(f);
        ch.enabled  = true;
        ch.ale_only = true;
        ctrl.add_channel(ch);
    }
    // Net "N1" owns both channels; short 8 s sounding interval.
    ctrl.add_net("N1");
    ctrl.assign_channel_to_net("N1", "C-1");
    ctrl.assign_channel_to_net("N1", "C-2");
    {
        // update_net takes a Net by value and matches by name; copy the stored
        // net, set a short interval + enable sounding, push it back.
        for (const Net& x : ctrl.nets()) {
            if (x.name == "N1") {
                Net upd = x;
                upd.sounding_interval_sec = 8u;   // 8 s — shorter than the sweep
                upd.sounding_enabled      = true;
                ctrl.update_net(upd);
                break;
            }
        }
    }
    // No pre-sounding warning (keeps the test deterministic; ALE_SOUNDING is
    // otherwise only dispatched from the warning path).
    ctrl.set_sounding_warning_lead_sec(0);

    // set_automatic_sounding() reads the net's sounding_interval_sec (8 s here)
    // into auto_sounding_interval_ms_, so query the effective interval AFTER
    // enabling (before enable it is still 0).
    ctrl.set_automatic_sounding(true, "N1");
    check(ctrl.is_automatic_sounding(), "automatic sounding enabled");

    const uint32_t interval_ms = ctrl.get_auto_sounding_interval_sec() * 1000u;
    pal::log_info(kMod, "  configured auto-sounding interval: %u ms", interval_ms);
    check(interval_ms == 8000u, "effective auto-sounding interval is 8 s");

    const uint32_t STEP_MS  = 50u;
    const int      MAX_TICK = 6000;   // 300 s ceiling — plenty for 2 sweeps

    // First sweep: fire (enter SOUNDING) then complete (leave SOUNDING).
    uint32_t now = 0;
    const uint32_t fire1 = wait_for_sounding_edge(ctrl, now, STEP_MS, MAX_TICK, true);
    check(fire1 != 0, "first auto-sounding sweep fired (entered SOUNDING)");
    const uint32_t complete1 = wait_for_sounding_edge(ctrl, now, STEP_MS, MAX_TICK, false);
    check(complete1 != 0, "first sweep completed (left SOUNDING)");
    pal::log_info(kMod, "  fire1=%u ms  complete1=%u ms  (sweep dur=%u ms)",
                  fire1, complete1, complete1 - fire1);

    // Second sweep: fire again.  With the fix, auto_sounding_last_ms_ was
    // stamped at complete1, so fire2 ≈ complete1 + interval.  The old (buggy)
    // code stamped at fire1, giving fire2 ≈ fire1 + interval (only
    // interval - sweep_duration after completion).
    const uint32_t fire2 = wait_for_sounding_edge(ctrl, now, STEP_MS, MAX_TICK, true);
    check(fire2 != 0, "second auto-sounding sweep fired");
    pal::log_info(kMod, "  fire2=%u ms  (gap after completion=%u ms)",
                  fire2, fire2 - complete1);

    // Primary invariant: next sounding fires `interval` after the previous
    // cycle COMPLETED (within tick granularity).
    const uint32_t gap_after_completion = fire2 - complete1;
    const bool gap_ok = (gap_after_completion >= interval_ms - STEP_MS)
                     && (gap_after_completion <= interval_ms + 2 * STEP_MS);
    check(gap_ok, "gap after completion == interval (not interval minus cycle)");

    // Negative guard: the buggy behaviour (re-arm at fire) would give
    // fire2 - fire1 == interval exactly; the fix makes fire2 - fire1 exceed
    // interval by the sweep duration.
    const bool not_entry_rearmed = (fire2 - fire1) > interval_ms;
    check(not_entry_rearmed, "timer was NOT re-armed at sweep entry (fire2-fire1 > interval)");
}

int main()
{
    pal::set_logger(pal::create_logger());

    pal::log_info(kMod, "=========================================================");
    pal::log_info(kMod, "  test_sounding_timing — auto-sounding cadence from END");
    pal::log_info(kMod, "=========================================================");

    test_auto_sounding_interval_from_cycle_end();

    if (g_failures == 0) { pal::log_info(kMod, "PASS  all sounding-timing tests"); return 0; }
    pal::log_error(kMod, "%d failure(s)", g_failures);
    return 1;
}