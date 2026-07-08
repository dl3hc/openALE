/**
 * \file App/voice_path_manager.cpp
 * \brief VoicePathManager implementation — see header for the architecture.
 */

#include "App/voice_path_manager.h"
#include "PAL/radio.h"
#include <algorithm>
#include <cstring>

namespace ale {

// Silence source handed to the VAC while passthrough is active but PTT is off:
// returns 0 samples ⇒ the driver fills silence on the render side, so nothing
// is transmitted to the radio (the modem's symbol source is overridden). The
// radio stays in RX and its captured audio flows to the browser speaker via
// the bridge main loop.
static size_t silence_pcm_source_(int16_t* /*out*/, size_t /*want*/) { return 0; }

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
    // manager tracks AUDIO_OPEN/RIG_CONNECT device swaps. Acting on every call
    // would clobber the pcm_source override mid-passthrough.
    if (vac == vac_ && radio == radio_) return;
    // If we are currently overriding the VAC, restore it before swapping.
    if (mode_ == Mode::VOICE_PASSTHROUGH && vac_) vac_->set_pcm_source(nullptr);
    vac_   = vac;
    radio_ = radio;
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
    if (!vac_) return;  // nothing to override — behave as ALE_EXCLUSIVE
    mode_ = Mode::VOICE_PASSTHROUGH;
    // PTT off on entry: VAC render is silence (override the modem symbol
    // source), radio stays in RX, captured audio flows to the browser.
    ptt_.store(false, std::memory_order_relaxed);
    vac_->set_pcm_source(silence_pcm_source_);
}

void VoicePathManager::exit_passthrough_()
{
    // Release PTT if the operator was mid-transmit when the link dropped.
    if (ptt_.load(std::memory_order_relaxed)) {
        if (radio_) radio_->set_ptt(false);
        ptt_.store(false, std::memory_order_relaxed);
    }
    if (vac_) vac_->set_pcm_source(nullptr);  // restore modem symbol-source path
    mode_ = Mode::ALE_EXCLUSIVE;
}

void VoicePathManager::set_ptt(bool on)
{
    if (mode_ != Mode::VOICE_PASSTHROUGH) return;  // PTT belongs to the modem otherwise
    if (on == ptt_.load(std::memory_order_relaxed)) return;

    ptt_.store(on, std::memory_order_relaxed);

    if (on) {
        if (radio_) radio_->set_ptt(true);          // key the transmitter
        if (vac_) vac_->set_pcm_source(
            [this](int16_t* out, size_t want) {
                return this->pull_mic_pcm(out, want);
            });
        if (on_ptt_activity) on_ptt_activity();     // reset link idle timer
    } else {
        if (radio_) radio_->set_ptt(false);         // back to receive
        if (vac_) vac_->set_pcm_source(silence_pcm_source_);
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