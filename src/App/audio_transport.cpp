/**
 * \file App/audio_transport.cpp
 * \brief AudioTransport implementation — see header for the architecture.
 */

#include "App/audio_transport.h"
#include "App/voice_path_manager.h"
#include <algorithm>

namespace ale {

// Silence source for the VAC while a voice link is active but not TX:
// returns 0 ⇒ driver fills silence on render, nothing transmitted. Radio
// stays RX; captured audio flows to decoder (always) and speaker (when not
// TX). Non-capturing, so it can be stored once.
static size_t silence_source_(int16_t* /*out*/, size_t /*want*/) { return 0; }

AudioTransport::AudioTransport() = default;

AudioTransport::~AudioTransport()
{
    // Restore modem symbol path on teardown so device is left in default
    // (non-passthrough) state — matches VoicePathManager's destructor.
    if (vac_ && last_source_ != Source::Symbol) vac_->set_pcm_source(nullptr);
}

void AudioTransport::attach(AudioDevice* vac)
{
    if (vac == vac_) return;  // no-op when unchanged — bridge re-binds every tick
    vac_ = vac;
    last_source_ = Source::Symbol;  // new device has no pcm_source; force re-install
}

void AudioTransport::set_decoder_sink(PcmSink feed)   { decoder_sink_     = std::move(feed); }
void AudioTransport::set_media_producer(VoicePathManager* media) { media_ = media; }
void AudioTransport::set_protocol_tx_query(TxQuery q)  { protocol_tx_query_ = std::move(q); }

void AudioTransport::add_rx_sink(RxSink& sink)
{
    rx_sinks_.push_back(&sink);
}

void AudioTransport::remove_rx_sink(RxSink& sink)
{
    rx_sinks_.erase(std::remove(rx_sinks_.begin(), rx_sinks_.end(), &sink), rx_sinks_.end());
}

void AudioTransport::tick()
{
    if (!vac_) return;

    // ── Capture RX (every tick, every state) ──────────────────────────────
    rx_buf_.clear();
    vac_->tick(rx_buf_);

    // ── RX fan-out ────────────────────────────────────────────────────────
    // Decoder is a PERMANENT sink: fed in all states incl. voice passthrough
    // — lets a remote TWAS termination be decoded during an active phone-patch link.
    if (!rx_buf_.empty() && decoder_sink_)
        decoder_sink_(rx_buf_.data(), rx_buf_.size());

    // Arbitrate TX first so the speaker-suppression gate below reflects the
    // current TX decision (protocol or media burst ⇒ no speaker).
    arbitrate_tx_();

    // Dynamic sinks (voice speaker, …): called only when not TX. Half-duplex:
    // both protocol and media TX suppress RX forwarding so operator hears no
    // echo. VoicePathManager adds itself via add_rx_sink() on passthrough
    // entry, removes on exit — no explicit gate here.
    if (!rx_buf_.empty() && !protocol_tx_active_ && !media_tx_active_) {
        for (RxSink* s : rx_sinks_)
            s->on_rx_audio(rx_buf_.data(), rx_buf_.size());
    }
}

void AudioTransport::arbitrate_tx_()
{
    if (!vac_) return;

    const bool protocol    = protocol_tx_query_ && protocol_tx_query_();
    const bool media_want   = media_ && media_->media_tx_wanted();
    const bool passthrough  = media_ && media_->passthrough_active();

    Source want;
    if (protocol) {
        // Modem has a burst to send (TERM, ack, sounding, …) — highest
        // priority; restore symbol path so pull_symbol_frame() runs.
        want = Source::Symbol;
        protocol_tx_active_ = true;
        media_tx_active_    = false;
    } else if (media_want) {
        // Operator voice PTT — mic ring → radio.
        want = Source::Media;
        protocol_tx_active_ = false;
        media_tx_active_    = true;
    } else if (passthrough) {
        // Voice link active, not TX — keep radio quiet so RX flows to the
        // speaker (and the decoder, always).
        want = Source::Silence;
        protocol_tx_active_ = false;
        media_tx_active_    = false;
    } else {
        // No voice link: normal ALE. Leave modem symbol path active so
        // calling/sounding TX works as before (set_pcm_source(nullptr) ⇒
        // driver falls through to symbol source); idle modem renders silence
        // via the symbol path itself.
        want = Source::Symbol;
        protocol_tx_active_ = false;
        media_tx_active_    = false;
    }

    if (want == last_source_) return;

    switch (want) {
        case Source::Symbol:
            vac_->set_pcm_source(nullptr);                 // modem symbol path
            break;
        case Source::Media:
            vac_->set_pcm_source(
                [this](int16_t* out, size_t want_n) {
                    return media_ ? media_->pull_mic_pcm(out, want_n) : 0;
                });
            break;
        case Source::Silence:
            vac_->set_pcm_source(silence_source_);
            break;
    }
    last_source_ = want;
}

bool AudioTransport::receiving_voice() const
{
    return media_ && media_->passthrough_active() && !protocol_tx_active_ && !media_tx_active_;
}

bool AudioTransport::protocol_pending() const
{
    return protocol_tx_active_ && media_ && media_->passthrough_active();
}

} // namespace ale