// test_voice_path_manager.cpp — unit tests for VoicePathManager.
//
// Verifies the ALE_EXCLUSIVE <-> VOICE_PASSTHROUGH state machine, PTT routing,
// and the SPSC mic ring. VoicePathManager no longer owns set_pcm_source — that
// belongs to AudioTransport. Tests verify the observable state flags
// (passthrough_active, media_tx_wanted) and the mic ring, not VAC transitions.
//
// See docs/VOICE_AUDIO_ROUTING.md and test_audio_transport.cpp (TX arbiter).

#include "App/voice_path_manager.h"
#include "PAL/audio_driver.h"
#include "PAL/radios/mock_radio.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

// Minimal IAudioDriver fake that records set_pcm_source transitions and lets
// the test invoke the installed PCM source to distinguish the mic-pull source
// (returns pushed samples) from the silence source (returns 0) from the
// restored symbol path (nullptr).
class FakeAudioDriver : public pal::IAudioDriver {
public:
    bool open(const std::string&, const std::string&) override { open_ = true; return true; }
    void close() override { open_ = false; pcm_pull_ = nullptr; has_pcm_ = false; }
    void set_symbol_source(std::function<bool(uint8_t*)> fn) override { sym_pull_ = std::move(fn); }
    void arm_frame_complete(std::function<void()>) override {}
    void tick(std::vector<int16_t>&) override {}
    bool is_open() const override { return open_; }
    std::vector<std::string> list_devices() const override { return {}; }

    void set_pcm_source(std::function<size_t(int16_t*, size_t)> fn) override {
        pcm_pull_ = std::move(fn);
        has_pcm_ = static_cast<bool>(pcm_pull_);
        ++pcm_set_count_;
    }

    // Invoke the currently-installed PCM source (mimics the audio render thread).
    size_t call_pcm(int16_t* out, size_t want) { return pcm_pull_ ? pcm_pull_(out, want) : 0; }
    bool   has_pcm_source() const { return has_pcm_; }     // false after set_pcm_source(nullptr)
    int    pcm_set_count()  const { return pcm_set_count_; }

private:
    bool        open_ = false;
    std::function<bool(uint8_t*)>              sym_pull_;
    std::function<size_t(int16_t*, size_t)>    pcm_pull_;
    bool        has_pcm_ = false;
    int         pcm_set_count_ = 0;
};

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); ++g_failures; } } while (0)

} // namespace

int main()
{
    using M = ale::VoicePathManager::Mode;

    // ── 1. Disarmed: link events never engage passthrough ──────────────────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(false);
        vpm.on_link_state(true);
        CHECK(vpm.mode() == M::ALE_EXCLUSIVE);
        CHECK(!vpm.passthrough_active());
        CHECK(!vac.has_pcm_source());          // modem symbol path untouched
        vpm.on_link_state(false);
        CHECK(vpm.mode() == M::ALE_EXCLUSIVE);
    }

    // ── 2. Armed + link → passthrough; AudioTransport owns set_pcm_source ────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        CHECK(vpm.mode() == M::ALE_EXCLUSIVE);     // not linked yet
        vpm.on_link_state(true);
        CHECK(vpm.mode() == M::VOICE_PASSTHROUGH);
        CHECK(vpm.passthrough_active());
        CHECK(!vpm.media_tx_wanted());             // PTT off → media not wanted
        CHECK(!vac.has_pcm_source());              // VPM no longer calls set_pcm_source
        CHECK(!radio.is_transmitting());           // radio stays in RX
    }

    // ── 3. PTT on → radio TX + media_tx_wanted; mic ring round-trips ───────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        int ptt_activity = 0;
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.on_ptt_activity = [&]() { ++ptt_activity; };
        vpm.arm(true);
        vpm.on_link_state(true);

        vpm.set_ptt(true);
        CHECK(radio.is_transmitting());            // CAT PTT asserted
        CHECK(vpm.ptt() == true);
        CHECK(vpm.media_tx_wanted());              // AudioTransport will install mic source
        CHECK(ptt_activity == 1);                  // link-idle reset hook fired

        // Push mic PCM (simulates a browser frame) then pull via pull_mic_pcm
        // directly — AudioTransport installs this as the VAC pcm_source callback.
        int16_t in[64];
        for (int i = 0; i < 64; ++i) in[i] = static_cast<int16_t>(-32000 + i * 1000);
        vpm.push_mic_pcm(in, 64);
        int16_t out[64] = {};
        const size_t got = vpm.pull_mic_pcm(out, 64);
        CHECK(got == 64);
        bool match = true;
        for (int i = 0; i < 64; ++i) if (out[i] != in[i]) match = false;
        CHECK(match);

        // Secondary pull also drains correctly.
        int16_t out2[16] = {};
        vpm.push_mic_pcm(in, 16);
        CHECK(vpm.pull_mic_pcm(out2, 16) == 16);
        match = true;
        for (int i = 0; i < 16; ++i) if (out2[i] != in[i]) match = false;
        CHECK(match);
    }

    // ── 4. PTT off → radio RX; mic ring ignored; media_tx_wanted false ──────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        vpm.on_link_state(true);
        vpm.set_ptt(true);
        vpm.set_ptt(false);
        CHECK(!radio.is_transmitting());
        CHECK(vpm.ptt() == false);
        CHECK(!vpm.media_tx_wanted());             // AudioTransport will restore silence
        // Mic pushed while PTT off is dropped (push_mic_pcm is a no-op then).
        int16_t in[16] = {1, 2, 3};
        vpm.push_mic_pcm(in, 16);
        int16_t out[16] = {};
        CHECK(vpm.pull_mic_pcm(out, 16) == 0);    // ring empty — mic was ignored
    }

    // ── 5. Link terminated → ALE_EXCLUSIVE; PTT released ────────────────────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        vpm.on_link_state(true);
        vpm.set_ptt(true);
        CHECK(radio.is_transmitting());

        vpm.on_link_state(false);
        CHECK(vpm.mode() == M::ALE_EXCLUSIVE);
        CHECK(!vpm.passthrough_active());
        CHECK(!vpm.media_tx_wanted());
        CHECK(!vac.has_pcm_source());  // VPM never set it; AudioTransport will clear on next tick
        CHECK(!radio.is_transmitting()); // PTT released even mid-TX
    }

    // ── 6. Disarm mid-link → returns to ALE_EXCLUSIVE ──────────────────────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        vpm.on_link_state(true);
        CHECK(vpm.passthrough_active());
        vpm.arm(false);
        CHECK(vpm.mode() == M::ALE_EXCLUSIVE);
        CHECK(!vpm.passthrough_active());
        CHECK(!vac.has_pcm_source());  // VPM never calls set_pcm_source
    }

    // ── 7. set_ptt is a no-op outside passthrough (modem owns PTT then) ────
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        // not linked → ALE_EXCLUSIVE → voice PTT must not touch the radio
        vpm.set_ptt(true);
        CHECK(!radio.is_transmitting());
        CHECK(vpm.ptt() == false);
    }

    // ── 8. attach() no-op when pointers unchanged (safe to re-bind per tick)─
    {
        FakeAudioDriver vac;
        pal::MockRadio radio;
        radio.initialize(); radio.start();
        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);
        vpm.arm(true);
        vpm.on_link_state(true);
        CHECK(vpm.passthrough_active());
        const int sets = vac.pcm_set_count();     // VPM never set pcm_source → 0
        vpm.attach(&vac, &radio);                  // same pointers → no-op
        CHECK(vac.pcm_set_count() == sets);        // still 0; no spurious flip
        CHECK(vpm.passthrough_active());           // still in passthrough
    }

    if (g_failures == 0) {
        std::printf("[voice_path_manager] all assertions passed\n");
        return 0;
    }
    std::printf("[voice_path_manager] %d assertion(s) FAILED\n", g_failures);
    return 1;
}