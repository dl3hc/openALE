/**
 * \file App/audio_transport.h
 * \brief AudioTransport — permanent owner of the radio (VAC) audio path.
 *
 * Replaces VoicePathManager's "exclusive ownership" model, which was the root
 * cause of the phone-patch termination bug: during an active voice link the modem
 * fully released the VAC, so (a) the decoder was never fed and could not decode a
 * remote TWAS termination, and (b) the modem could not transmit its own
 * termination. Only the local "End Link" button worked, via a bridge hack that
 * forced the path back to the modem before sending CMD:TERMINATE.
 *
 * The transport fixes this with two principles:
 *
 *   - **RX fan-out.** Captured audio is delivered to every registered sink every
 *     tick. The ALE decoder (`ALEController::feed_audio`, set via
 *     `set_decoder_sink()`) is a **permanent** sink — it is fed in every state,
 *     including voice passthrough. Feeding the decoder during voice is identical
 *     to normal LISTENING (the demodulator only fires on Golay-corrected
 *     unanimous words, which voice never produces) and is what lets a remote
 *     termination be decoded. A separate speaker sink (set via
 *     `set_speaker_sink()`, enabled only while a voice link is active and the
 *     station is not transmitting) forwards RX to the operator's browser.
 *
 *   - **TX arbitration by priority.** The transport owns `IAudioDriver::
 *     set_pcm_source()` and selects the TX source each tick in priority order:
 *       1. protocol (modem symbols) — `ctrl.is_tx_active()` ⇒
 *          `set_pcm_source(nullptr)`, which lets the render thread fall through
 *          to the symbol source registered by `ALEController::set_audio_device()`
 *          (the modem's `pull_symbol_frame`). The SM's existing ptt_lead/tail and
 *          frame-completion logic send the burst unchanged.
 *       2. media (operator voice mic) — `VoicePathManager::media_tx_wanted()`
 *          (LINKED + armed + PTT) ⇒ `set_pcm_source(mic ring pull)`.
 *       3. idle — while a voice link is active, `set_pcm_source(silence)` keeps
 *          the radio quiet and RX flowing to the speaker; with no voice link,
 *          `set_pcm_source(nullptr)` leaves the modem symbol path active so
 *          normal ALE TX (calling/sounding) works byte-for-byte as before.
 *
 * The modem symbol source stays registered for the life of the device; only the
 * active render branch flips. The transport never closes/reopens the VAC across
 * state changes (preserves the existing no-reacquisition property).
 *
 * Threading: all public methods run on the bridge main loop. The only
 * cross-thread touch is the mic ring inside VoicePathManager, pulled by the
 * audio render thread through the `set_pcm_source` callback installed here.
 *
 * See docs/VOICE_AUDIO_ROUTING.md and the plan
 * C:\Users\chris\.claude\plans\feature-proposal-unified-dreamy-brooks.md.
 */

#pragma once

#include "App/audio_device.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ale {

class VoicePathManager;

/// Receiving-side sink for 8 kHz mono int16 PCM delivered by the transport.
/// Permanent sinks (ALE decoder) are registered via AudioTransport::set_decoder_sink().
/// Dynamic sinks (voice speaker) self-register via add_rx_sink() / remove_rx_sink()
/// so the transport need not know about their lifecycle.
class RxSink {
public:
    virtual ~RxSink() = default;
    virtual void on_rx_audio(const int16_t* buf, size_t samples) = 0;
};

class AudioTransport {
public:
    /// RX delivery callback for the permanent decoder sink.
    using PcmSink = std::function<void(const int16_t* samples, size_t count)>;

    /// TX source-selection query: returns true when the modem has frames to
    /// transmit (priority over media). Bound to ALEController::is_tx_active().
    using TxQuery = std::function<bool()>;

    AudioTransport();
    ~AudioTransport();

    AudioTransport(const AudioTransport&)            = delete;
    AudioTransport& operator=(const AudioTransport&) = delete;

    /// Bind the modem (VAC) audio device. Must outlive this instance.
    /// nullptr detaches (tick() becomes a no-op).
    void attach(AudioDevice* vac);

    /// Permanent RX sink: the ALE decoder. Called every tick with the captured
    /// buffer (empty buffers are skipped). Fed in ALL states — this is the fix
    /// that lets a remote termination be decoded during voice.
    void set_decoder_sink(PcmSink feed);

    /// Register a dynamic RX sink (e.g. VoicePathManager). Called from the bridge
    /// main loop only. The sink is called each tick while not transmitting (both
    /// protocol and media TX suppress it — half-duplex). VoicePathManager calls
    /// this from enter_passthrough_() / remove from exit_passthrough_().
    void add_rx_sink(RxSink& sink);
    void remove_rx_sink(RxSink& sink);

    /// Register the voice media producer. The transport queries it each tick
    /// for `passthrough_active()` (voice link engaged) and `media_tx_wanted()`
    /// (PTT on), and pulls mic PCM via `pull_mic_pcm()` when media wins
    /// arbitration. nullptr detaches (media never selected).
    void set_media_producer(VoicePathManager* media);

    /// Protocol-TX-active query (modem has frames ⇒ priority over media).
    void set_protocol_tx_query(TxQuery q);

    /// Drive one main-loop tick: capture RX → fan out to decoder (+ speaker when
    /// eligible) → arbitrate the TX source. Idempotent and cheap.
    void tick();

    /// True on a tick where the protocol (modem) source won TX arbitration —
    /// a control burst is being transmitted right now.
    bool protocol_tx_active() const { return protocol_tx_active_; }
    /// True on a tick where the media (voice mic) source won TX arbitration.
    bool media_tx_active()    const { return media_tx_active_; }

    // ── Phase 4: observable voice session sub-states ─────────────────────
    /// Voice link active + not transmitting (radio in RX, speaker forwarded).
    bool receiving_voice()    const;
    /// Media (voice mic) won TX arbitration this tick.
    bool transmitting_voice() const { return media_tx_active_; }
    /// Protocol (modem burst) is pre-empting an active voice session.
    bool protocol_pending()   const;

private:
    enum class Source { Symbol, Media, Silence };

    void arbitrate_tx_();

    AudioDevice*        vac_   = nullptr;
    VoicePathManager*   media_ = nullptr;
    PcmSink             decoder_sink_;
    PcmSink             speaker_sink_;
    TxQuery             protocol_tx_query_;

    std::vector<int16_t> rx_buf_;
    std::vector<RxSink*> rx_sinks_;           // dynamic sinks (voice speaker, …)
    Source               last_source_       = Source::Symbol;
    bool                 protocol_tx_active_ = false;
    bool                 media_tx_active_    = false;
};

} // namespace ale