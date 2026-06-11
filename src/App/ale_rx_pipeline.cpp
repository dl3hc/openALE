/**
 * \file App/ale_rx_pipeline.cpp
 */

#include "App/ale_rx_pipeline.h"
#include "FEC/ale_fec_codec.h"
#include "FEC/golay.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace ale {

ALERxPipeline::ALERxPipeline()
    : ring_(BUF_CAP, 0)
{}

void ALERxPipeline::reset()
{
    std::fill(ring_.begin(), ring_.end(), int16_t(0));
    write_pos_  = 0;
    step_accum_ = 0;
}

void ALERxPipeline::push_samples(const int16_t* samples, uint32_t count)
{
    if (!enabled_) return;

    for (uint32_t i = 0; i < count; ++i) {
        ring_[write_pos_ % BUF_CAP] = samples[i];
        ++write_pos_;

        if (++step_accum_ >= FFT_SIZE) {
            step_accum_ = 0;

            // Need at least one full word in the buffer before attempting decode.
            if (write_pos_ < WORD_SAMPLES) continue;

            ALEWord word;
            if (try_decode(word) && word_cb_)
                word_cb_(word);
        }
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

float ALERxPipeline::goertzel_power(const int16_t* block, float freq_hz)
{
    const float w     = 2.0f * static_cast<float>(M_PI) * freq_hz / 8000.0f;
    const float coeff = 2.0f * std::cos(w);
    float q1 = 0.0f, q2 = 0.0f;
    for (uint32_t n = 0; n < FFT_SIZE; ++n) {
        const float q0 = coeff * q1 - q2 + static_cast<float>(block[n]);
        q2 = q1; q1 = q0;
    }
    return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

uint8_t ALERxPipeline::symbol_from_block(const int16_t* block)
{
    float   best = -1.0f;
    uint8_t rank = 0;
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        float p = goertzel_power(block, static_cast<float>(TONE_FREQS_HZ[r]));
        if (p > best) { best = p; rank = r; }
    }
    return FREQ_TO_SYMBOL[rank];
}

bool ALERxPipeline::try_decode(ALEWord& out) const
{
    // Base of the last WORD_SAMPLES (3136) samples in the ring.
    const uint32_t base = write_pos_ - WORD_SAMPLES;

    // Step 1: Detect 49 symbols, each from a non-overlapping 64-sample block.
    int16_t blk[FFT_SIZE];
    uint8_t sym[SYMBOLS_PER_WORD];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        const uint32_t bk = base + k * FFT_SIZE;
        for (uint32_t j = 0; j < FFT_SIZE; ++j)
            blk[j] = ring_at(bk + j);
        sym[k] = symbol_from_block(blk);
    }

    // Step 2: Unpack 3-bit symbols → 147-bit stream (MSB-first).
    //   stream[3k+0] = bit2 (MSB), stream[3k+1] = bit1, stream[3k+2] = bit0.
    //   This matches the modem's build_symbols() packing convention.
    uint8_t stream[SYMBOLS_PER_WORD * BITS_PER_SYMBOL];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        stream[3*k + 0] = (sym[k] >> 2) & 1u;
        stream[3*k + 1] = (sym[k] >> 1) & 1u;
        stream[3*k + 2] =  sym[k]       & 1u;
    }

    // Step 3: Stride-49 majority vote → 49-bit tx49.
    //   Bit i is the majority of stream[i], stream[i+49], stream[i+98].
    uint64_t tx49 = 0;
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i) {
        const uint32_t votes = stream[i]
                             + stream[i + SYMBOLS_PER_WORD]
                             + stream[i + 2u * SYMBOLS_PER_WORD];
        if (votes >= 2u)
            tx49 |= (1ULL << i);
    }

    // Step 4: Deinterleave + Golay FEC decode → 24-bit ALE word.
    Golay::DecodeResult fec;
    const uint32_t word24 = ALEFECCodec::deinterleave_word(tx49, fec);
    if (fec.flag == Golay::DECODE_UNCORRECTABLE)
        return false;

    // Step 5: Parse to ALEWord structure.
    WordParser parser;
    return parser.parse_from_bits(word24, out);
}

} // namespace ale
