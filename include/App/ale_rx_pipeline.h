/**
 * \file App/ale_rx_pipeline.h
 * \brief Streaming PCM → ALEWord decoder for ALE 2G (8 kHz, mono, 16-bit).
 *
 * Accepts PCM via push_samples().  Every DECODE_STEP (16) samples the
 * pipeline attempts to decode the last 3136 samples (49 symbols × 64 samples/
 * symbol = one ALE word block) using Goertzel detection + stride-49 majority
 * vote + Golay FEC.  On an accepted decode word_cb_ is invoked.
 *
 * Word-boundary sync: the incoming word grid has arbitrary sub-symbol phase
 * relative to the local sample counter (sound devices and resamplers are not
 * synchronized).  Attempting a decode every 16 samples bounds the residual
 * misalignment to ±8 samples (1/8 symbol), which Goertzel detection absorbs.
 *
 * Acceptance rule (accept_word_):
 *   - While a word window is still being deduplicated (closer than one word
 *     to the last accepted word), decodes are dropped — neighbouring attempt
 *     offsets re-decode the SAME on-air word.
 *   - Clean decodes (both Golay halves DECODE_OK) are accepted and anchor /
 *     re-anchor the word grid (false-decode probability ≈ 2^{-24} per attempt).
 *   - Corrected decodes (DECODE_CORRECTED) are accepted only on the anchored
 *     grid (multiples of 3136 samples, ±1 symbol).  A misaligned window over
 *     real signal passes Golay with 1–3 "corrected" errors in roughly a third
 *     of all attempts — off-grid corrected words are phantom words and must
 *     be rejected before the state machine acts on them.
 */

#pragma once
#include "Codec/ale_decoder.h"
#include "FSK/ale_waveform.h"
#include "FEC/golay.h"
#include "Word/ale_word.h"
#include <functional>
#include <vector>
#include <cstdint>

namespace ale {

class ALERxPipeline {
public:
    using WordCallback = std::function<void(const ALEWord&)>;

    ALERxPipeline();

    void set_word_callback(WordCallback cb) { word_cb_ = std::move(cb); }

    /**
     * Enable / disable decoding (samples pushed while disabled are discarded).
     * Disabling also resets the ring buffer and the word-grid lock so stale
     * signal cannot leak into decode windows after re-enable.
     */
    void set_enabled(bool en) {
        if (!en && enabled_) reset();
        enabled_ = en;
    }
    bool enabled() const      { return enabled_; }

    /** Feed PCM samples (8 kHz, mono, 16-bit). May invoke word_cb_ zero or more times. */
    void push_samples(const int16_t* samples, uint32_t count);

    void reset();

private:
    static constexpr uint32_t WORD_SAMPLES = SYMBOLS_PER_WORD * FFT_SIZE;  // 3136
    static constexpr uint32_t BUF_CAP      = WORD_SAMPLES + FFT_SIZE;      // 3200
    static constexpr uint32_t DECODE_STEP  = FFT_SIZE / 4;                 // 16: ±8-sample sync residual

    // Silence-gap grid reset: after SILENCE_RESET_SAMPLES consecutive samples
    // below SILENCE_THRESHOLD the word-grid lock is released so the next
    // transmission can re-anchor at its own sub-symbol phase offset.
    static constexpr uint32_t SILENCE_RESET_SAMPLES = 800;  // 100 ms at 8 kHz
    static constexpr int16_t  SILENCE_THRESHOLD     = 200;  // well below ALE tone level

    WordCallback word_cb_;
    bool         enabled_ = true;

    std::vector<int16_t> ring_;   // circular sample buffer, BUF_CAP entries
    uint32_t write_pos_  = 0;    // absolute write position (wraps via % BUF_CAP)
    uint32_t step_accum_ = 0;    // samples in current decode step

    // Word-grid lock: anchor of the last accepted word in the sample stream.
    bool     grid_locked_  = false;
    uint32_t grid_anchor_  = 0;   // write_pos_ at last accepted word

    // Silence-gap detector: released grid lock counter.
    uint32_t silence_count_ = 0;

    // Compute Goertzel power for one tone on a 64-sample block.
    static float goertzel_power(const int16_t* block, float freq_hz);

    // Detect the dominant ALE tone in a 64-sample block → symbol value 0-7.
    static uint8_t symbol_from_block(const int16_t* block);

    // Read sample at absolute position abs_pos from ring_.
    int16_t ring_at(uint32_t abs_pos) const { return ring_[abs_pos % BUF_CAP]; }

    // Attempt to decode the last WORD_SAMPLES samples as one ALE word.
    // Returns true and fills 'out' (and 'fec') on a Golay-decodable parse;
    // grid acceptance is decided separately by accept_word_().
    bool try_decode(ALEWord& out, Golay::DecodeResult& fec) const;

    // Acceptance rule for a decodable word (see file header).
    bool accept_word_(const Golay::DecodeResult& fec);
};

} // namespace ale
