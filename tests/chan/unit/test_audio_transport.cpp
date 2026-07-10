// test_audio_transport.cpp — unit tests for AudioTransport (Phases 1–3).
//
// AT-1  RX fan-out: decoder sink is called every tick regardless of voice
//       passthrough state; the dynamic speaker sink (VPM's on_speaker_pcm) is
//       called only when passthrough is active and the station is not transmitting.
// AT-2  TX arbiter: the correct pcm_source is installed on the VAC based on
//       protocol-TX / media-TX / passthrough-idle / ALE-exclusive priority.
//
// Both tests use a FakeAudioDriver whose tick() produces configurable samples,
// and the real VoicePathManager (to exercise the RxSink self-registration).
//
// See docs/VOICE_AUDIO_ROUTING.md for the architecture.

#include "App/audio_transport.h"
#include "App/voice_path_manager.h"
#include "PAL/audio_driver.h"
#include "PAL/radios/mock_radio.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

// ── Fake audio driver ────────────────────────────────────────────────────────
// Produces `capture_samples` int16 samples (value 1) on every tick() call
// when non-zero. Records set_pcm_source transitions for arbiter assertions.

class FakeAudioDriver : public pal::IAudioDriver {
public:
    bool open(const std::string&, const std::string&) override { return true; }
    void close() override { pcm_pull_ = nullptr; has_pcm_ = false; }
    void set_symbol_source(std::function<bool(uint8_t*)>) override {}
    void arm_frame_complete(std::function<void()>) override {}
    bool is_open() const override { return true; }
    std::vector<std::string> list_devices() const override { return {}; }

    void tick(std::vector<int16_t>& out) override {
        out.assign(capture_samples, static_cast<int16_t>(1));
    }

    void set_pcm_source(std::function<size_t(int16_t*, size_t)> fn) override {
        pcm_pull_ = std::move(fn);
        has_pcm_  = static_cast<bool>(pcm_pull_);
        ++pcm_set_count_;
    }

    size_t call_pcm(int16_t* out, size_t want) {
        return pcm_pull_ ? pcm_pull_(out, want) : 0;
    }

    bool has_pcm_source() const  { return has_pcm_; }
    int  pcm_set_count()  const  { return pcm_set_count_; }

    size_t capture_samples = 0;

private:
    std::function<size_t(int16_t*, size_t)> pcm_pull_;
    bool has_pcm_       = false;
    int  pcm_set_count_ = 0;
};

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
        ++g_failures; \
    } \
} while (0)

} // namespace

int main()
{
    // ── AT-1: RX fan-out ─────────────────────────────────────────────────────
    // Invariant: decoder_sink fires every tick; on_speaker_pcm (via VPM's
    // RxSink) fires only when passthrough_active() && !transmitting.
    // VPM self-registers / self-removes with the transport — no set_speaker_sink.
    {
        FakeAudioDriver   vac;
        pal::MockRadio    radio;
        radio.initialize(); radio.start();

        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);

        int decoder_calls = 0;
        int speaker_calls = 0;
        bool protocol_tx  = false;

        ale::AudioTransport transport;
        transport.set_decoder_sink([&](const int16_t* /*buf*/, size_t n) {
            if (n > 0) ++decoder_calls;
        });
        transport.set_media_producer(&vpm);
        transport.set_protocol_tx_query([&]() { return protocol_tx; });
        transport.attach(&vac);

        // Wire speaker via VPM's callback — VPM self-registers as RxSink
        // when entering passthrough, so this fires only at the right time.
        vpm.on_speaker_pcm = [&](const int16_t* /*buf*/, size_t n) {
            if (n > 0) ++speaker_calls;
        };
        vpm.set_transport(&transport);

        vac.capture_samples = 8;

        // ALE_EXCLUSIVE: decoder called, speaker NOT called.
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 1);
        CHECK(speaker_calls == 0);

        // VOICE_PASSTHROUGH + PTT off: BOTH decoder and speaker called.
        // on_link_state(true) → enter_passthrough_() → transport.add_rx_sink(vpm)
        vpm.arm(true);
        vpm.on_link_state(true);
        CHECK(vpm.passthrough_active());
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 1);   // decoder still fed — the critical fix
        CHECK(speaker_calls == 1);   // VPM's on_rx_audio forwarded to on_speaker_pcm

        // VOICE_PASSTHROUGH + PTT on (media TX): decoder called, speaker muted.
        vpm.set_ptt(true);
        CHECK(vpm.media_tx_wanted());
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 1);   // decoder always fed
        CHECK(speaker_calls == 0);   // half-duplex: dynamic sinks suppressed while TX
        vpm.set_ptt(false);

        // Protocol TX active: decoder called, speaker muted.
        protocol_tx = true;
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 1);   // decoder always fed
        CHECK(speaker_calls == 0);   // protocol burst → dynamic sinks suppressed
        protocol_tx = false;

        // Link terminated → VPM removes itself from sinks → speaker silent again.
        vpm.on_link_state(false);
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 1);
        CHECK(speaker_calls == 0);   // VPM unregistered in exit_passthrough_

        // Phase 4: session sub-state accessors
        // (ALE_EXCLUSIVE state — nothing is active)
        CHECK(!transport.receiving_voice());
        CHECK(!transport.transmitting_voice());
        CHECK(!transport.protocol_pending());

        // Re-enter passthrough and verify sub-states
        vpm.on_link_state(true);
        transport.tick();   // update arbitration flags
        CHECK(transport.receiving_voice());     // passthrough + PTT off + no protocol
        CHECK(!transport.transmitting_voice());
        CHECK(!transport.protocol_pending());

        vpm.set_ptt(true);
        transport.tick();
        CHECK(!transport.receiving_voice());
        CHECK(transport.transmitting_voice());  // media TX won
        CHECK(!transport.protocol_pending());
        vpm.set_ptt(false);

        protocol_tx = true;
        transport.tick();
        CHECK(!transport.receiving_voice());
        CHECK(!transport.transmitting_voice());
        CHECK(transport.protocol_pending());    // protocol preempting voice session
        protocol_tx = false;

        // Exit passthrough before end of block: VPM destructor would otherwise
        // call remove_rx_sink on the already-destroyed transport (transport is
        // declared after vpm in this block and thus destroyed first).
        vpm.on_link_state(false);

        // Empty capture: no sink fires.
        vac.capture_samples = 0;
        decoder_calls = speaker_calls = 0;
        transport.tick();
        CHECK(decoder_calls == 0);
        CHECK(speaker_calls == 0);
    }

    // ── AT-2: TX arbiter ─────────────────────────────────────────────────────
    // Invariant: protocol > media > passthrough-idle > ALE-exclusive.
    // Each state transition installs the correct pcm_source exactly once.
    {
        FakeAudioDriver   vac;
        pal::MockRadio    radio;
        radio.initialize(); radio.start();

        ale::VoicePathManager vpm;
        vpm.attach(&vac, &radio);

        bool protocol_tx = false;

        ale::AudioTransport transport;
        transport.set_decoder_sink([](const int16_t*, size_t) {});
        transport.set_media_producer(&vpm);
        transport.set_protocol_tx_query([&]() { return protocol_tx; });
        transport.attach(&vac);

        vpm.on_speaker_pcm = [](const int16_t*, size_t) {};  // no-op for AT-2
        vpm.set_transport(&transport);

        // ALE_EXCLUSIVE: symbol path (nullptr) installed on first tick.
        transport.tick();
        CHECK(!vac.has_pcm_source());   // nullptr = symbol path
        const int sets_after_init = vac.pcm_set_count();

        // Same state again: no redundant set_pcm_source call.
        transport.tick();
        CHECK(vac.pcm_set_count() == sets_after_init);

        // Enter VOICE_PASSTHROUGH (PTT off) → silence installed.
        vpm.arm(true);
        vpm.on_link_state(true);
        CHECK(vpm.passthrough_active());
        transport.tick();
        CHECK(vac.has_pcm_source());    // silence source installed
        int16_t buf[8] = {};
        CHECK(vac.call_pcm(buf, 8) == 0);  // silence returns 0

        // No state change on repeated tick → no redundant flip.
        const int sets_silence = vac.pcm_set_count();
        transport.tick();
        CHECK(vac.pcm_set_count() == sets_silence);

        // PTT on → mic-pull source installed.
        vpm.set_ptt(true);
        CHECK(vpm.media_tx_wanted());
        transport.tick();
        CHECK(vac.has_pcm_source());    // mic source installed

        // Mic ring round-trip via the installed pcm_source.
        int16_t in[4] = {10, 20, 30, 40};
        vpm.push_mic_pcm(in, 4);
        int16_t out[4] = {};
        CHECK(vac.call_pcm(out, 4) == 4);
        CHECK(out[0] == 10 && out[3] == 40);

        // Protocol TX preempts media: symbol path restored even while PTT is on.
        protocol_tx = true;
        transport.tick();
        CHECK(!vac.has_pcm_source());   // symbol path (nullptr) wins

        // Protocol ends → media resumes (PTT still asserted).
        protocol_tx = false;
        transport.tick();
        CHECK(vac.has_pcm_source());    // back to mic source

        // PTT off → silence.
        vpm.set_ptt(false);
        transport.tick();
        CHECK(vac.has_pcm_source());
        CHECK(vac.call_pcm(buf, 8) == 0);  // silence again

        // Link terminated → ALE_EXCLUSIVE → symbol path.
        vpm.on_link_state(false);
        transport.tick();
        CHECK(!vac.has_pcm_source());   // symbol path restored
    }

    if (g_failures == 0) {
        std::printf("[audio_transport] all assertions passed\n");
        return 0;
    }
    std::printf("[audio_transport] %d assertion(s) FAILED\n", g_failures);
    return 1;
}
