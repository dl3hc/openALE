/**
 * @file repro_brief_tx.cpp
 * @brief Repro harness: operator report "call/sounding keys PTT only briefly,
 *        then reverts to RX" (IDLE, single channel, LBT off, both backends).
 *
 * Drives the REAL ALEController with a real-time-paced fake audio device
 * (one 49-symbol word completes 392 ms of simulated time after it is pulled,
 * mirroring WASAPI playout-time completion) and a PTT-logging MockRadio.
 * Prints a timeline of PTT edges, SM display-state changes and word
 * completions so the observed TX duration can be compared against the
 * expected LBT(Twt) + TUNING(Tt=1045) + N x 392 ms + tail(350) budget.
 */

#include "App/ale_controller.h"
#include "Protocol/Control/ale_channel_types.h"
#include "PAL/radios/mock_radio.h"
#include "PAL/audio_driver.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

using namespace ale;

static uint32_t g_now = 0;   // simulated clock (ms)

// ── PTT-logging radio ─────────────────────────────────────────────────────────
class PttLogRadio : public pal::MockRadio {
public:
    struct Edge { uint32_t t; bool on; };
    std::vector<Edge> edges;
    void set_ptt(bool tx) override {
        if (edges.empty() || edges.back().on != tx) {
            edges.push_back({ g_now, tx });
            std::printf("  t=%6u  PTT %s\n", g_now, tx ? "ON " : "OFF");
        }
        pal::MockRadio::set_ptt(tx);
    }
};

// ── Real-time-paced fake audio device ────────────────────────────────────────
// Pulls one symbol frame at a time; the armed completion fires 392 ms of
// simulated time later (playout pacing, like the WASAPI driver).
class PacedAudio : public pal::IAudioDriver {
public:
    bool open(const std::string&, const std::string&) override { open_ = true; return true; }
    void close() override { open_ = false; }
    bool is_open() const override { return open_; }
    std::vector<std::string> list_devices() const override { return {}; }

    void set_symbol_source(std::function<bool(uint8_t*)> fn) override { pull_ = std::move(fn); }
    void arm_frame_complete(std::function<void()> cb) override { fifo_.push_back(std::move(cb)); }

    void tick(std::vector<int16_t>&) override {
        // Start playing when idle and the modem has a frame.
        if (!playing_) try_start_();
        // Word finished playing out -> fire its completion, chain the next.
        while (playing_ && g_now >= play_end_ms_) {
            ++words_played_;
            std::printf("  t=%6u  word played out (#%u)\n", g_now, words_played_);
            if (!fifo_.empty()) { auto cb = std::move(fifo_.front()); fifo_.pop_front(); cb(); }
            playing_ = false;
            try_start_();
        }
    }

    uint32_t words_played_ = 0;

private:
    void try_start_() {
        uint8_t syms[64];
        if (pull_ && pull_(syms)) {
            playing_ = true;
            play_end_ms_ = g_now + 392;   // Trw: 49 symbols x 8 ms
        }
    }
    bool open_ = false;
    bool playing_ = false;
    uint32_t play_end_ms_ = 0;
    std::function<bool(uint8_t*)> pull_;
    std::deque<std::function<void()>> fifo_;
};

// ── Scenario driver ──────────────────────────────────────────────────────────
static void run_scenario(const char* name, bool sounding)
{
    std::printf("\n=== %s ===\n", name);
    g_now = 0;

    PttLogRadio radio;
    PacedAudio  audio;
    audio.open("", "");

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    ctrl.add_channel(Channel(7073000));
    ctrl.set_radio(&radio);
    ctrl.set_audio_device(&audio);

    std::string last_state;
    bool fired = false;
    const uint32_t t_fire = 1000;
    std::vector<int16_t> rx_scratch;

    for (g_now = 0; g_now <= 40000; g_now += 10) {
        rx_scratch.clear();
        audio.tick(rx_scratch);
        ctrl.update(g_now);

        if (!fired && g_now >= t_fire) {
            fired = true;
            const bool ok = sounding ? ctrl.send_sounding()
                                     : ctrl.initiate_call("JOE");
            std::printf("  t=%6u  %s -> %s\n", g_now,
                        sounding ? "send_sounding()" : "initiate_call(JOE)",
                        ok ? "accepted" : "REFUSED");
        }

        const std::string s = ctrl.display_state();
        if (s != last_state) {
            std::printf("  t=%6u  state: %s\n", g_now, s.c_str());
            last_state = s;
        }
    }

    // Summary
    std::printf("  words played: %u\n", audio.words_played_);
    for (size_t i = 0; i + 1 < radio.edges.size(); i += 2)
        if (radio.edges[i].on)
            std::printf("  TX window: %u..%u = %u ms\n",
                        radio.edges[i].t, radio.edges[i+1].t,
                        radio.edges[i+1].t - radio.edges[i].t);
}

int main()
{
    run_scenario("CALL from IDLE, single channel", false);
    run_scenario("SOUNDING from IDLE, single channel", true);
    return 0;
}
