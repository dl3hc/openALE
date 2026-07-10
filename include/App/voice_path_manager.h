/**
 * \file App/voice_path_manager.h
 * \brief VoicePathManager — voice media producer/consumer for the AudioTransport.
 *
 * Tracks ALE link state and the operator's PTT to expose two observable flags
 * that the AudioTransport TX arbiter queries each tick:
 *
 *   passthrough_active() — a voice link is established (LINKED + armed).
 *     The transport routes captured radio RX to the operator's browser speaker.
 *   media_tx_wanted()    — passthrough is active AND the operator has asserted
 *     voice PTT.  The transport installs `pull_mic_pcm` on the VAC render path
 *     so the mic ring drives the radio transmitter.
 *
 * VoicePathManager does **not** own `IAudioDriver::set_pcm_source()` — that
 * responsibility now belongs to AudioTransport (TX arbitration). VPM is a pure
 * media producer/consumer: it owns the mic SPSC ring and CAT PTT assertion;
 * the transport decides when each source wins the VAC render path.
 *
 * Mode semantics (internal):
 *   ALE_EXCLUSIVE     — no voice link (or disarmed). Transport uses the modem
 *                       symbol source for TX; RX goes to the ALE decoder only.
 *   VOICE_PASSTHROUGH — voice link active. Transport fans RX to both decoder
 *                       and browser speaker; selects media or silence for TX.
 *
 * Threading: all public methods called from the bridge main loop. The mic ring
 * is SPSC lock-free: push_mic_pcm() produces on the main loop; pull_mic_pcm()
 * consumes on the audio render thread (via the set_pcm_source callback that the
 * transport installs when media wins the arbiter).
 */

#pragma once

#include "App/audio_device.h"      // AudioDevice = pal::IAudioDriver
#include "App/audio_transport.h"   // RxSink; AudioTransport (pointer only)
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace pal { class IRadio; }

namespace ale {

class VoicePathManager : public RxSink {
public:
    enum class Mode { ALE_EXCLUSIVE, VOICE_PASSTHROUGH };

    VoicePathManager();
    ~VoicePathManager();

    VoicePathManager(const VoicePathManager&)            = delete;
    VoicePathManager& operator=(const VoicePathManager&) = delete;

    /// Bind the modem (VAC) audio device and the radio used for PTT. Both must
    /// outlive this instance. Pass nullptr for either to detach.
    void attach(AudioDevice* vac, pal::IRadio* radio);

    /// Register the AudioTransport that this VPM self-registers with as an
    /// RxSink on passthrough entry/exit. Must be set before on_link_state(true).
    void set_transport(AudioTransport* t) { transport_ = t; }

    // RxSink implementation — called by the transport each tick when not
    // transmitting and VPM is registered (i.e. passthrough is active).
    void on_rx_audio(const int16_t* buf, size_t samples) override;

    /// Speaker output callback (set by the bridge). Called by on_rx_audio with
    /// the radio RX PCM to be forwarded to the browser (WebSocket 0x01 frame).
    std::function<void(const int16_t* buf, size_t samples)> on_speaker_pcm;

    /// Enable/disable voice capability. When disarmed, the manager never enters
    /// VOICE_PASSTHROUGH — the modem keeps the VAC (legacy ALE-only behaviour).
    void arm(bool on);
    bool armed() const { return armed_.load(std::memory_order_relaxed); }

    /// ALE link-state notification. `linked && armed` ⇒ VOICE_PASSTHROUGH;
    /// `!linked` ⇒ ALE_EXCLUSIVE (releases the VAC back to the modem).
    void on_link_state(bool linked);

    /// Voice PTT (half-duplex direction). No-op unless passthrough is active.
    ///   on  → radio TX (CAT PTT asserted), mic→VAC, speaker muted
    ///   off → radio RX (CAT PTT released), VAC→speaker, mic ignored
    /// Voice PTT does NOT invoke ALE ptt_lead/ptt_tail (those are ALE-modem
    /// word-framing concerns). Each activation fires `on_ptt_activity`.
    void set_ptt(bool on);
    bool ptt() const { return ptt_.load(std::memory_order_relaxed); }

    /// Push mic PCM (8 kHz mono int16) received from the browser via WebSocket.
    /// Main-loop thread. Drops samples that don't fit the ring. No-op unless
    /// passthrough + PTT (the ring is only drained while PTT is on).
    void push_mic_pcm(const int16_t* samples, size_t count);

    /// Pull mic PCM for the modem device's `set_pcm_source` callback. Audio
    /// render thread. Returns samples filled; 0 ⇒ silence/underrun.
    size_t pull_mic_pcm(int16_t* out, size_t want);

    /// True while a voice link is active (LINKED + armed). Queried by
    /// AudioTransport to gate the browser speaker sink.
    bool passthrough_active() const { return mode_ == Mode::VOICE_PASSTHROUGH; }

    /// True while passthrough is active AND the operator has voice PTT asserted.
    /// Queried by AudioTransport to install the mic-pull source on the VAC.
    bool media_tx_wanted() const {
        return ptt_.load(std::memory_order_relaxed) && mode_ == Mode::VOICE_PASSTHROUGH;
    }

    Mode mode() const { return mode_; }

    /// Fired on each voice-PTT activation so the bridge can reset the link idle
    /// timer (voice counts as link activity, preventing Twa auto-termination).
    std::function<void()> on_ptt_activity;

private:
    void enter_passthrough_();
    void exit_passthrough_();

    AudioDevice*        vac_       = nullptr;
    pal::IRadio*        radio_     = nullptr;
    AudioTransport*     transport_ = nullptr;

    std::atomic<bool> armed_{false};
    std::atomic<bool> linked_{false};
    std::atomic<bool> ptt_{false};
    Mode              mode_ = Mode::ALE_EXCLUSIVE;

    // ── SPSC lock-free mic ring ───────────────────────────────────────────
    // Power-of-two capacity; head_/tail_ are monotonic counters, index = c & mask.
    // Producer: main loop (push_mic_pcm). Consumer: audio render thread
    // (pull_mic_pcm). Capacity ~256 ms @ 8 kHz.
    static constexpr size_t RING_CAP = 2048;
    static constexpr size_t RING_MASK = RING_CAP - 1;
    std::vector<int16_t> ring_buf_;
    std::atomic<size_t>  ring_head_{0};  // write counter (main loop)
    std::atomic<size_t>  ring_tail_{0};  // read counter  (audio thread)
};

} // namespace ale