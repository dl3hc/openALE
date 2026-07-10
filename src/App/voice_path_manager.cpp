/**
 * \file App/voice_path_manager.cpp
 * \brief VoicePathManager implementation — see header for the architecture.
 */

#include "App/voice_path_manager.h"
#include "PAL/radio.h"
#include <algorithm>
#include <cstring>

namespace ale {

VoicePathManager::VoicePathManager()
    : ring_buf_(RING_CAP, 0)
{
}

VoicePathManager::~VoicePathManager()
{
    if (mode_ == Mode::VOICE_PASSTHROUGH) exit_passthrough_();
}

void VoicePathManager::attach(AudioDevice* vac, pal::IRadio* radio)
{
    // No-op when unchanged — the bridge re-binds every main-loop tick so the
    // manager tracks AUDIO_OPEN/RIG_CONNECT device swaps.
    if (vac == vac_ && radio == radio_) return;
    vac_   = vac;
    radio_ = radio;
    // VAC pcm_source arbitration is owned by AudioTransport; it resets its
    // source-tracking when its own attach() detects the pointer change.
}

void VoicePathManager::arm(bool on)
{
    armed_.store(on, std::memory_order_relaxed);
    // Re-evaluate mode: arming while already linked engages passthrough;
    // disarming forces the modem back to exclusive ownership.
    on_link_state(linked_.load(std::memory_order_relaxed));
}

void VoicePathManager::on_link_state(bool linked)
{
    linked_.store(linked, std::memory_order_relaxed);
    const bool want_passthrough = linked && armed_.load(std::memory_order_relaxed);

    if (want_passthrough && mode_ == Mode::ALE_EXCLUSIVE) {
        enter_passthrough_();
    } else if (!want_passthrough && mode_ == Mode::VOICE_PASSTHROUGH) {
        exit_passthrough_();
    }
}

void VoicePathManager::enter_passthrough_()
{
    mode_ = Mode::VOICE_PASSTHROUGH;
    ptt_.store(false, std::memory_order_relaxed);
    // Self-register as an RxSink so the transport fan-outs speaker audio here
    // each tick while not transmitting.
    if (transport_) transport_->add_rx_sink(*this);
    // TX arbitration (set_pcm_source) is handled by AudioTransport on the
    // next tick: it will see passthrough_active()=true and install silence.
}

void VoicePathManager::exit_passthrough_()
{
    // Unregister from the transport before clearing mode so on_rx_audio is
    // never called on a half-exited state.
    if (transport_) transport_->remove_rx_sink(*this);
    // Release PTT if the operator was mid-transmit when the link dropped.
    if (ptt_.load(std::memory_order_relaxed)) {
        if (radio_) radio_->set_ptt(false);
        ptt_.store(false, std::memory_order_relaxed);
    }
    mode_ = Mode::ALE_EXCLUSIVE;
    // AudioTransport will see passthrough_active()=false on the next tick and
    // restore the symbol path (set_pcm_source(nullptr)).
}

void VoicePathManager::on_rx_audio(const int16_t* buf, size_t samples)
{
    if (on_speaker_pcm) on_speaker_pcm(buf, samples);
}

void VoicePathManager::set_ptt(bool on)
{
    if (mode_ != Mode::VOICE_PASSTHROUGH) return;  // PTT belongs to the modem otherwise
    if (on == ptt_.load(std::memory_order_relaxed)) return;

    ptt_.store(on, std::memory_order_relaxed);

    if (on) {
        if (radio_) radio_->set_ptt(true);     // key the transmitter
        if (on_ptt_activity) on_ptt_activity(); // reset link idle timer
        // AudioTransport sees media_tx_wanted()=true on the next tick and
        // installs the mic-pull source on the VAC.
    } else {
        if (radio_) radio_->set_ptt(false);    // back to receive
        // AudioTransport sees media_tx_wanted()=false and restores silence.
    }
}

void VoicePathManager::push_mic_pcm(const int16_t* samples, size_t count)
{
    if (mode_ != Mode::VOICE_PASSTHROUGH) return;
    if (!ptt_.load(std::memory_order_relaxed)) return;  // mic only routed while TX
    if (!samples || !count) return;

    const size_t head = ring_head_.load(std::memory_order_relaxed);
    const size_t tail = ring_tail_.load(std::memory_order_acquire);
    const size_t free_slots = RING_CAP - (head - tail);
    const size_t n = std::min(count, free_slots);
    for (size_t k = 0; k < n; ++k)
        ring_buf_[(head + k) & RING_MASK] = samples[k];
    ring_head_.store(head + n, std::memory_order_release);
}

size_t VoicePathManager::pull_mic_pcm(int16_t* out, size_t want)
{
    const size_t tail = ring_tail_.load(std::memory_order_relaxed);
    const size_t head = ring_head_.load(std::memory_order_acquire);
    const size_t avail = head - tail;  // unsigned: correct modulo 2^N
    const size_t n = std::min(want, avail);
    for (size_t k = 0; k < n; ++k)
        out[k] = ring_buf_[(tail + k) & RING_MASK];
    if (n) ring_tail_.store(tail + n, std::memory_order_release);
    // A short return (n < want) is fine: the driver's PCM render branch
    // resamples whatever was returned and refills on the next drain cycle.
    // A zero return (ring empty / underrun) makes the driver fill silence for
    // the rest of that batch and retry on the next service_render() call.
    return n;
}

} // namespace ale