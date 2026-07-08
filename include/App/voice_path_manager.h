/**
 * \file App/voice_path_manager.h
 * \brief VoicePathManager — dynamic owner of the radio (VAC) audio path.
 *
 * Switches the radio-side audio path (the Virtual Audio Cable / loopback that
 * connects openALE to the radio) between two owners based **solely** on ALE
 * link state:
 *
 *   ALE_EXCLUSIVE     — the ALE modem owns the VAC (today's behaviour). The
 *                       bridge feeds VAC capture → ALEController::feed_audio()
 *                       and the modem's symbol source drives VAC render.
 *   VOICE_PASSTHROUGH — entered on link_established (when armed). The modem
 *                       releases the VAC; it becomes a transparent bidirectional
 *                       pipe between the radio and the operator's voice device
 *                       (the browser, via WebSocket PCM — see
 *                       docs/VOICE_AUDIO_ROUTING.md). PTT selects the half-duplex
 *                       direction: on  ⇒ mic → radio TX (speaker muted);
 *                       off ⇒ radio RX → speaker (mic ignored).
 *
 * On link_terminated (any reason) ownership returns to the modem automatically.
 *
 * The modem `AudioDevice` (VAC) is **never closed/reopened** across a
 * transition — entering passthrough overrides the render path via
 * `IAudioDriver::set_pcm_source()`; leaving clears it, restoring the symbol
 * source that was registered by `ALEController::set_audio_device()`. This keeps
 * the exclusive-ownership invariant: exactly one consumer owns the VAC at any
 * instant, and the modem/decoder signal-processing code is untouched (the
 * decoder is simply not fed while passthrough is active — the bridge gates
 * `feed_audio()` on `passthrough_active()`).
 *
 * Threading: all public methods are called from the bridge main loop. The only
 * cross-thread touch is the mic ring: `push_mic_pcm()` produces on the main
 * loop; `pull_mic_pcm()` consumes on the modem audio render thread (via the
 * `set_pcm_source` callback). The ring is single-producer/single-consumer and
 * lock-free.
 *
 * Bridge-owned (audio/radio lifecycle is owned by the bridge caller, not the
 * controller — see docs/GUI_BRIDGE_GAPS.md). The controller is not modified.
 */

#pragma once

#include "App/audio_device.h"      // AudioDevice = pal::IAudioDriver
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace pal { class IRadio; }

namespace ale {

class VoicePathManager {
public:
    enum class Mode { ALE_EXCLUSIVE, VOICE_PASSTHROUGH };

    VoicePathManager();
    ~VoicePathManager();

    VoicePathManager(const VoicePathManager&)            = delete;
    VoicePathManager& operator=(const VoicePathManager&) = delete;

    /// Bind the modem (VAC) audio device and the radio used for PTT. Both must
    /// outlive this instance. Pass nullptr for either to detach.
    void attach(AudioDevice* vac, pal::IRadio* radio);

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

    /// True while the voice path owns the VAC (LINKED + armed).
    bool passthrough_active() const { return mode_ == Mode::VOICE_PASSTHROUGH; }
    Mode  mode() const { return mode_; }

    /// Fired on each voice-PTT activation so the bridge can reset the link idle
    /// timer (voice counts as link activity, preventing Twa auto-termination).
    std::function<void()> on_ptt_activity;

private:
    void enter_passthrough_();
    void exit_passthrough_();

    AudioDevice*        vac_   = nullptr;
    pal::IRadio*        radio_ = nullptr;

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