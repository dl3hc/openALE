/**
 * \file App/ale_rx_pipeline.cpp
 */

#include "App/ale_rx_pipeline.h"
#include "Codec/ale_decoder.h"
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
    write_pos_    = 0;
    step_accum_   = 0;
    grid_locked_  = false;
    grid_anchor_  = 0;
    silence_count_ = 0;
}

void ALERxPipeline::push_samples(const int16_t* samples, uint32_t count)
{
    if (!enabled_) return;

    for (uint32_t i = 0; i < count; ++i) {
        const int16_t s = samples[i];
        ring_[write_pos_ % BUF_CAP] = s;
        ++write_pos_;

        // Silence-gap grid reset: prolonged absence of signal indicates the
        // previous transmission has ended.  Reset the grid lock so the next
        // transmission can re-anchor at its own sub-symbol phase offset.
        if (s > -SILENCE_THRESHOLD && s < SILENCE_THRESHOLD) {
            if (++silence_count_ >= SILENCE_RESET_SAMPLES) {
                grid_locked_   = false;
                silence_count_ = SILENCE_RESET_SAMPLES; // clamp, avoid overflow
            }
        } else {
            silence_count_ = 0;
        }

        if (++step_accum_ >= DECODE_STEP) {
            step_accum_ = 0;

            // Need at least one full word in the buffer before attempting decode.
            if (write_pos_ < WORD_SAMPLES) continue;

            ALEWord word;
            Golay::DecodeResult fec;
            if (try_decode(word, fec) && accept_word_(fec) && word_cb_)
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

bool ALERxPipeline::accept_word_(const Golay::DecodeResult& fec)
{
    if (grid_locked_) {
        const uint32_t dist = write_pos_ - grid_anchor_;

        // Deduplication: attempt offsets closer than one word to the last
        // accepted word re-decode the SAME on-air word — drop them.
        if (dist < WORD_SAMPLES - FFT_SIZE)
            return false;

        // Clean decodes always re-anchor the grid (handles peer clock drift
        // and the start of a new transmission at arbitrary phase).
        if (fec.flag == Golay::DECODE_OK) {
            grid_anchor_ = write_pos_;
            return true;
        }

        // Corrected decodes are trusted only on the anchored grid (±1 symbol):
        // a misaligned window over real signal passes Golay with "corrections"
        // in roughly a third of all attempts and would yield phantom words.
        const uint32_t mod = dist % WORD_SAMPLES;
        if (mod <= FFT_SIZE || mod >= WORD_SAMPLES - FFT_SIZE) {
            grid_anchor_ = write_pos_;
            return true;
        }
        return false;
    }

    // Unlocked: only a clean decode may establish the word grid.
    if (fec.flag == Golay::DECODE_OK) {
        grid_locked_ = true;
        grid_anchor_ = write_pos_;
        return true;
    }
    return false;
}

bool ALERxPipeline::try_decode(ALEWord& out, Golay::DecodeResult& fec) const
{
    // Step 1: Goertzel symbol detection — extract 49 FSK symbols from the
    // last WORD_SAMPLES (3136) samples in the ring buffer.
    // Each symbol is detected from a non-overlapping 64-sample block.
    const uint32_t base = write_pos_ - WORD_SAMPLES;
    int16_t blk[FFT_SIZE];
    uint8_t sym[SYMBOLS_PER_WORD];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        const uint32_t bk = base + k * FFT_SIZE;
        for (uint32_t j = 0; j < FFT_SIZE; ++j)
            blk[j] = ring_at(bk + j);
        sym[k] = symbol_from_block(blk);
    }

    // Steps 2–4: majority vote → Golay FEC → parse  (ALEDecoder).
    return ALEDecoder::decode(sym, out, fec);
}

} // namespace ale
