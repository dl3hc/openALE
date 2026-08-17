/**
 * \file App/audio_monitor.cpp
 * \brief AudioMonitor implementation — see header for the architecture.
 */

#include "App/audio_monitor.h"
#include "App/voice_path_manager.h"

namespace ale {

AudioMonitor::~AudioMonitor()
{
    if (armed_) arm(false);
}

void AudioMonitor::arm(bool on)
{
    if (on == armed_) return;
    armed_ = on;
    if (!transport_) return;
    if (armed_) transport_->add_rx_sink(*this);
    else        transport_->remove_rx_sink(*this);
}

void AudioMonitor::on_rx_audio(const int16_t* buf, size_t samples)
{
    // A real voice link is already streaming this tick's RX audio via
    // VoicePathManager — skip to avoid sending it to the browser twice.
    if (voice_ && voice_->passthrough_active()) return;
    if (on_pcm) on_pcm(buf, samples);
}

} // namespace ale
