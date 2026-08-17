/**
 * \file App/audio_monitor.h
 * \brief AudioMonitor — link-independent RX tap for the Operator Audio Interface.
 *
 * Lets the operator "listen in" on the transceiver's RX audio from the browser
 * regardless of ALE link state or Voice Passthrough arm state — e.g. while
 * scanning or idle. Unlike VoicePathManager it carries no TX/mic/mode machinery;
 * it is a pure RxSink that forwards captured RX PCM to `on_pcm` whenever armed.
 *
 * Suppression: while VoicePathManager::passthrough_active() is true, that
 * manager is already registered as an RxSink and streaming RX to the operator's
 * speaker. AudioMonitor checks this and skips forwarding in that case — both
 * sinks may be registered with AudioTransport simultaneously (armed monitor +
 * an active voice link), and without the guard the same tick's PCM would be
 * sent to the browser twice, doubling the audio.
 *
 * Threading: arm()/on_rx_audio() run on the bridge main loop, same as
 * VoicePathManager and AudioTransport.
 */

#pragma once

#include "App/audio_transport.h"  // RxSink; AudioTransport (pointer only)
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ale {

class VoicePathManager;

class AudioMonitor : public RxSink {
public:
    AudioMonitor()  = default;
    ~AudioMonitor() override;

    AudioMonitor(const AudioMonitor&)            = delete;
    AudioMonitor& operator=(const AudioMonitor&) = delete;

    /// Bind the transport this monitor self-registers with when armed.
    void attach(AudioTransport* transport) { transport_ = transport; }

    /// Bind the voice manager consulted to avoid double-streaming RX audio
    /// while a real voice link is already forwarding it.
    void set_voice_manager(VoicePathManager* voice) { voice_ = voice; }

    /// Enable/disable the RX tap. Self add_rx_sink()/remove_rx_sink() on the
    /// bound transport, mirroring VoicePathManager's passthrough entry/exit.
    void arm(bool on);
    bool armed() const { return armed_; }

    // RxSink implementation — called by the transport each tick when not
    // transmitting and the monitor is registered (i.e. armed).
    void on_rx_audio(const int16_t* buf, size_t samples) override;

    /// RX PCM forwarding callback (set by the bridge) — 8 kHz mono int16.
    std::function<void(const int16_t* buf, size_t samples)> on_pcm;

private:
    AudioTransport*   transport_ = nullptr;
    VoicePathManager* voice_     = nullptr;
    bool              armed_     = false;
};

} // namespace ale
