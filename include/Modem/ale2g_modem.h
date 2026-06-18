/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — Modulator and Demodulator.
 *
 * namespace ale::ALE2GModem contains two classes:
 *
 *   Modulator   — TX side: word-queue + 49-symbol pull API (no PCM, no timing)
 *   Demodulator — RX side: streaming PCM → ALEWord via Goertzel + Grid-Lock
 *
 * ── Modulator ────────────────────────────────────────────────────────────────
 *
 * Encodes ALEWords into 49 8-FSK symbol values (0–7) for audio-layer pull.
 *
 *   ALEWord → word.encode() → tx49 (49-bit) → ALEEncoder → symbol_buf_[49]
 *
 * On-air word format (AC-WAVEFORM-008-2):
 *   symbol[k] = word_bit(3k%49) as bit2, word_bit((3k+1)%49) as bit1,
 *               word_bit((3k+2)%49) as bit0
 *   49 symbols × 8 ms/symbol = 392 ms = Trw (REQ-WAVEFORM-010).
 *
 * Pull API (thread-safe):
 *   Main thread  — enqueue_word() / enqueue_sequence() / is_transmitting()
 *   Audio thread — pull_symbol_frame(out_49)
 *
 * ── Demodulator ──────────────────────────────────────────────────────────────
 *
 * Accepts 8 kHz / mono / int16 PCM via push_samples().  Every DECODE_STEP (16)
 * samples it attempts to decode the last 3136 samples (one ALE word block) via
 * Goertzel detection + ALEDecoder (majority vote + Golay FEC).  Accepted words
 * are delivered via word_cb_.
 *
 * Word acquisition / tracking (MIL-STD-188-141B A.5.2.6.3):
 *   Every word — during both initial and continuing sync — must satisfy all
 *   applicable criteria, NOT a zero-error decode.  The 3× symbol redundancy and
 *   Golay(24,12) exist so a receiver can acquire through residual symbol errors
 *   (channel noise, resampling smear, sub-sample phase) that any real or
 *   foreign-encoder signal carries — such words decode as DECODE_CORRECTED, not
 *   DECODE_OK.  The criteria applied here:
 *     - unanimous 2/3-vote count ≥ threshold  (primary BER / phase discriminator)
 *     - successful Golay decode of both A and B halves (DECODE_OK or _CORRECTED)
 *     - valid ASCII for the word's character set      (enforced in ALEDecoder)
 *     - acceptable leading preamble (TO / TWAS / TIS) for INITIAL acquisition
 *     - correct triple-redundant word phase (on-grid once locked)
 *   Silence-gap reset: 100 ms below threshold releases the grid lock so the
 *   next transmission re-acquires at its own sub-symbol phase.
 */

#pragma once

#include "Codec/ale_decoder.h"
#include "Codec/ale_encoder.h"
#include "FSK/ale_waveform.h"
#include "FEC/golay.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include "Word/ale_sequence.h"
#include <array>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstring>

namespace ale {
namespace ALE2GModem {

// ── Modulator ─────────────────────────────────────────────────────────────────

class Modulator {
public:
    Modulator();

    /**
     * Enqueue one logical word for transmission (AC-WAVEFORM-008-2).
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Enqueue all words of an ALESequence for sequential transmission.
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_sequence(const ALESequence& seq);

    /**
     * Pull the next pending symbol frame (49 symbol values, 0–7) into out_49.
     * Returns false when idle. Thread-safe: may be called concurrently with enqueue_word().
     */
    bool pull_symbol_frame(uint8_t* out_49);

    /** True if at least one symbol frame is pending. Thread-safe. */
    bool is_transmitting() const;

private:
    mutable std::mutex   mtx_;
    uint64_t             pending_tx49_  = 0;
    bool                 word_enqueued_ = false;
    std::queue<uint64_t> tx49_queue_;
    SymbolFrame          symbol_buf_;

    void enqueue_tx49_(uint64_t tx49);
    void advance_queue_();
};

// ── Adaptive FEC policy (MIL-STD-188-141B A.5.2.6.3 "DO") ───────────────────────
//
// Tracks an EWMA of per-word unanimous-vote counts and maps the running
// signal-quality estimate to a Golay correction power and acceptance threshold:
// clean channel → less correction, higher threshold; marginal → full correction,
// lower threshold.  Threshold is bounded so re-acquisition is never blocked.
class AdaptiveFec {
public:
    void reset() { quality_ = INIT_QUALITY; }

    /// Fold one accepted word's unanimous-vote count into the running estimate.
    void observe(uint8_t unanimous_votes) {
        quality_ += ALPHA * (static_cast<float>(unanimous_votes) - quality_);
    }

    float quality() const { return quality_; }

    GolayMode mode() const {
        if (quality_ >= CLEAN_HI) return GolayMode::Mode1_6;  // very clean link
        if (quality_ >= CLEAN_LO) return GolayMode::Mode2_5;  // clean link
        return GolayMode::Mode3_4;                            // marginal / unknown
    }

    uint8_t threshold() const {
        float t = quality_ - MARGIN;
        if (t < MIN_THRESHOLD) t = MIN_THRESHOLD;
        if (t > MAX_THRESHOLD) t = MAX_THRESHOLD;
        return static_cast<uint8_t>(t + 0.5f);
    }

private:
    static constexpr float   ALPHA         = 0.125f;
    static constexpr float   MARGIN        = 8.0f;
    static constexpr float   CLEAN_LO      = 45.0f;
    static constexpr float   CLEAN_HI      = 48.0f;
    static constexpr float   INIT_QUALITY  = 40.0f;  // neutral start: Mode3_4, threshold 32
    static constexpr uint8_t MIN_THRESHOLD = 30;
    static constexpr uint8_t MAX_THRESHOLD = 42;
    float quality_ = INIT_QUALITY;
};

// ── Demodulator ───────────────────────────────────────────────────────────────

class Demodulator {
public:
    using WordCallback = std::function<void(const ALEWord&)>;

    /**
     * Spectrum callback — fired ~10 times/second from the audio thread.
     * bins[k] = linear FFT magnitude at k * hz_per_bin Hz (257 bins, ≈15.6 Hz each).
     * ALE channel (750–2500 Hz) maps to bins ≈ 48–160.
     * Called from the audio capture thread — keep it short or hand off to a queue.
     */
    using SpectrumCallback =
        std::function<void(const float* bins, size_t count, float hz_per_bin)>;

    Demodulator();

    void set_word_callback(WordCallback cb) { word_cb_ = std::move(cb); }

    /** Register a callback for spectrum/waterfall data.  Pass nullptr to disable. */
    void set_spectrum_callback(SpectrumCallback cb) { spectrum_cb_ = std::move(cb); }

    /**
     * Enable / disable decoding.  Disabling resets the ring buffer and
     * word-grid lock so stale signal cannot leak after re-enable.
     */
    void set_enabled(bool en) {
        if (!en && enabled_) reset();
        enabled_ = en;
    }
    bool enabled() const { return enabled_; }

    /** Feed PCM samples (8 kHz, mono, 16-bit). May invoke word_cb_. */
    void push_samples(const int16_t* samples, uint32_t count);

    void reset();

    // ── FEC / sync configuration (A.5.2.6.3, manufacturer-tunable) ──────────
    // These set the BASE operating point used for word acquisition.  Default is
    // the most tolerant point (full Golay correction, MIN_UNANIMOUS_VOTES) so any
    // spec-compliant signal can be acquired.

    /// Select the Golay correction power applied during decode (A.5.2.6.3).
    void      set_golay_mode(GolayMode m) { base_golay_mode_ = m; golay_mode_ = m; }
    GolayMode golay_mode() const          { return golay_mode_; }

    /// Set the minimum unanimous 2/3-vote count required to accept a word (0..49).
    void    set_min_unanimous_votes(uint8_t v) { base_min_unanimous_ = v; min_unanimous_votes_ = v; }
    uint8_t min_unanimous_votes() const        { return min_unanimous_votes_; }

    /// A.5.2.6.3 "DO": enable automatic adjustment of Golay mode + unanimous-vote
    /// threshold from observed signal quality.  Off by default → fixed operating
    /// point.  Adaptation runs only while the word grid is locked (tracking);
    /// acquisition always uses the configured base point so a degraded signal can
    /// still be acquired.
    void set_adaptive_fec(bool on) { adaptive_ = on; if (!on) restore_base_operating_point_(); }
    bool adaptive_fec() const      { return adaptive_; }

private:
    static constexpr uint32_t WORD_SAMPLES         = SYMBOLS_PER_WORD * FFT_SIZE; // 3136
    static constexpr uint32_t BUF_CAP              = WORD_SAMPLES + FFT_SIZE;     // 3200
    static constexpr uint32_t DECODE_STEP          = FFT_SIZE / 4;                // 16
    static constexpr uint32_t SILENCE_RESET_SAMPLES = 800;  // 100 ms at 8 kHz
    static constexpr int16_t  SILENCE_THRESHOLD    = 200;

    // A.5.2.6.3 criterion 1: minimum unanimous 2/3-votes (out of 49) required to
    // accept a word.  This is the standard's primary, manufacturer-tunable BER /
    // signal-quality discriminator; it also enforces "correct triple-redundant
    // word phase" — a misaligned window makes the three copies disagree, dropping
    // the unanimous count far below threshold.  A clean (bit-exact) peer scores 49.
    static constexpr uint8_t  MIN_UNANIMOUS_VOTES  = 33;   // ~67 % of 49

    WordCallback         word_cb_;
    bool                 enabled_       = true;
    std::vector<int16_t> ring_;
    uint32_t             write_pos_     = 0;
    uint32_t             step_accum_    = 0;
    bool                 grid_locked_   = false;
    uint32_t             grid_anchor_   = 0;
    uint32_t             silence_count_ = 0;

    // FEC / sync operating point (A.5.2.6.3).  base_* = configured acquisition
    // point; the active golay_mode_ / min_unanimous_votes_ equal base_* except
    // while adaptive tracking is in effect.
    GolayMode   base_golay_mode_     = GolayMode::Mode3_4;
    uint8_t     base_min_unanimous_  = MIN_UNANIMOUS_VOTES;
    GolayMode   golay_mode_          = GolayMode::Mode3_4;
    uint8_t     min_unanimous_votes_ = MIN_UNANIMOUS_VOTES;
    bool        adaptive_            = false;
    AdaptiveFec adaptive_fec_;

    void restore_base_operating_point_() {
        golay_mode_          = base_golay_mode_;
        min_unanimous_votes_ = base_min_unanimous_;
        adaptive_fec_.reset();
    }

    // ── Spectrum analyser (waterfall data source) ─────────────────────────
    // Independent 512-point FFT pass on the same PCM stream.
    // Fired ~10 times/second; does NOT affect word decoding.
    static constexpr size_t   SPEC_FFT_N    = 512;   // bins: 0–4000 Hz, 15.625 Hz/bin
    static constexpr uint32_t SPEC_INTERVAL = 800;   // samples between updates (~10 Hz)

    SpectrumCallback              spectrum_cb_;
    uint32_t                      spec_accum_  = 0;
    std::array<float, SPEC_FFT_N> spec_window_;  // pre-computed Hann window

    void compute_spectrum_();

    static float   goertzel_power(const int16_t* block, float freq_hz);
    static uint8_t symbol_from_block(const int16_t* block);
    int16_t        ring_at(uint32_t abs_pos) const { return ring_[abs_pos % BUF_CAP]; }
    bool           try_decode(ALEWord& out, Golay::DecodeResult& fec, uint8_t& unanimous_votes) const;
    bool           accept_word_(const Golay::DecodeResult& fec, const ALEWord& word,
                                uint8_t unanimous_votes);
};

} // namespace ALE2GModem
} // namespace ale
