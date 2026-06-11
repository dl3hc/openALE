/**
 * \file App/ale_rx_pipeline.h
 * \brief Streaming PCM → ALEWord decoder for ALE 2G (8 kHz, mono, 16-bit).
 *
 * Accepts PCM via push_samples().  Every 64 samples (one symbol period) the
 * pipeline attempts to decode the last 3136 samples (49 symbols × 64 samples/
 * symbol = one ALE word block) using Goertzel detection + stride-49 majority
 * vote + Golay FEC.  On a successful decode word_cb_ is invoked.
 *
 * Word-boundary sync: the sliding-window approach tolerates up to 49 symbol-
 * periods (392 ms) of alignment uncertainty.  False-decode probability is
 * approximately 2^{-24} per attempt (combined Golay check on both half-words),
 * so duplicate or phantom words are negligible in practice.
 */

#pragma once
#include "FSK/ale_waveform.h"
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

    /** Enable / disable decoding (samples pushed while disabled are discarded). */
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const      { return enabled_; }

    /** Feed PCM samples (8 kHz, mono, 16-bit). May invoke word_cb_ zero or more times. */
    void push_samples(const int16_t* samples, uint32_t count);

    void reset();

private:
    static constexpr uint32_t WORD_SAMPLES = SYMBOLS_PER_WORD * FFT_SIZE;  // 3136
    static constexpr uint32_t BUF_CAP      = WORD_SAMPLES + FFT_SIZE;      // 3200

    WordCallback word_cb_;
    bool         enabled_ = true;

    std::vector<int16_t> ring_;   // circular sample buffer, BUF_CAP entries
    uint32_t write_pos_  = 0;    // absolute write position (wraps via % BUF_CAP)
    uint32_t step_accum_ = 0;    // samples in current 64-sample step

    // Compute Goertzel power for one tone on a 64-sample block.
    static float goertzel_power(const int16_t* block, float freq_hz);

    // Detect the dominant ALE tone in a 64-sample block → symbol value 0-7.
    static uint8_t symbol_from_block(const int16_t* block);

    // Read sample at absolute position abs_pos from ring_.
    int16_t ring_at(uint32_t abs_pos) const { return ring_[abs_pos % BUF_CAP]; }

    // Attempt to decode the last WORD_SAMPLES samples as one ALE word.
    // Returns true and fills 'out' on a Golay-clean parse.
    bool try_decode(ALEWord& out) const;
};

} // namespace ale
