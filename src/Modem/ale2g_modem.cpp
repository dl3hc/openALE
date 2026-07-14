/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem — Modulator and Demodulator implementations.
 *
 * The grid-lock state machine (gate, refinement, adaptive FEC) moved to
 * WordGridTracker (word_grid_tracker.{h,cpp}).  Demodulator now owns:
 *   - ring buffer + PCM intake
 *   - Goertzel + symbol_from_block() (signal extraction)
 *   - try_decode_() → DecodedCandidate
 *   - push_samples() dispatch to tracker_.process_candidate()
 *   - silence-gap detection → tracker_.on_silence_gap()
 *
 * All decode math (Goertzel, SINAD measurement) is unchanged.
 */

#include "Modem/ale2g_modem.h"
#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "FEC/ale_fec_codec.h"
#include "FEC/golay.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace ale {
namespace ALE2GModem {

// ════════════════════════════════════════════════════════════════════════════
// Modulator
// ════════════════════════════════════════════════════════════════════════════

Modulator::Modulator()
{
    symbol_buf_.fill(0);
}

void Modulator::enqueue_word(const ALEWord& word)
{
    std::lock_guard<std::mutex> lk(mtx_);
    enqueue_tx49_(word.encode());
}

void Modulator::enqueue_sequence(const ALESequence& seq)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (uint64_t tx49 : seq.encode())
        enqueue_tx49_(tx49);
}

bool Modulator::pull_symbol_frame(uint8_t* out)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!word_enqueued_) return false;
    std::memcpy(out, symbol_buf_.data(), SYMBOLS_PER_WORD);
    word_enqueued_ = false;
    advance_queue_();
    return true;
}

bool Modulator::is_transmitting() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return word_enqueued_ || !tx49_queue_.empty();
}

void Modulator::abort()
{
    std::lock_guard<std::mutex> lk(mtx_);
    tx49_queue_ = {};
    word_enqueued_ = false;
    pending_tx49_  = 0;
    symbol_buf_.fill(0);
}

void Modulator::enqueue_tx49_(uint64_t tx49)
{
    if (word_enqueued_) {
        assert(tx49_queue_.size() < ALETimingConstants::MAX_TX_SEQUENCE_WORDS &&
               "ALE2GModem::Modulator queue overflow — SM enqueued more words than one "
               "full contiguous calling sequence (scanning + leading + conclusion); "
               "likely a loop or double-transmit bug");
        tx49_queue_.push(tx49);
        return;
    }
    pending_tx49_  = tx49;
    word_enqueued_ = true;
    symbol_buf_    = ALEEncoder::encode_tx49(pending_tx49_);
}

void Modulator::advance_queue_()
{
    if (word_enqueued_) return;
    if (tx49_queue_.empty()) return;
    pending_tx49_  = tx49_queue_.front();
    tx49_queue_.pop();
    word_enqueued_ = true;
    symbol_buf_    = ALEEncoder::encode_tx49(pending_tx49_);
}

// ════════════════════════════════════════════════════════════════════════════
// Demodulator
// ════════════════════════════════════════════════════════════════════════════

Demodulator::Demodulator()
    : ring_(BUF_CAP, 0)
{
}

void Demodulator::reset()
{
    std::fill(ring_.begin(), ring_.end(), int16_t(0));
    write_pos_     = 0;
    step_accum_    = 0;
    silence_count_ = 0;
    hop_offset_    = 0;
    energy_fired_  = false;
    tracker_.reset();
}

void Demodulator::push_samples(const int16_t* samples, uint32_t count)
{
    if (!enabled_) return;

    for (uint32_t i = 0; i < count; ++i) {
        const int16_t s = samples[i];
        ring_[write_pos_ % BUF_CAP] = s;
        ++write_pos_;

        // Spectrum analyser (waterfall) — display concern, not decode.
        spectrum_.feed(s);

        // Silence-gap grid reset.
        check_silence_reset_(s);

        if (++step_accum_ >= (tracker_.use_fine_step() ? DECODE_STEP_FINE
                                                        : DECODE_STEP_COARSE)) {
            step_accum_ = 0;
            if (write_pos_ < WORD_SAMPLES) continue;

            const DecodedCandidate c = try_decode_();

            // §A.5.3.3 stage 1: "detects sounds" — A.5.2.6.3 unanimous-vote criterion.
            // Fires once per channel after the decode window is entirely new-channel
            // audio (write_pos_ - hop_offset_ >= WORD_SAMPLES) and the signal shows
            // ALE-characteristic vote agreement.  energy_fired_ gates to one shot;
            // mark_channel_hop() resets it on each hop.
            if (ale_energy_cb_ && !energy_fired_
                && c.unanimous_votes >= ALE_STAGE1_MIN_VOTES
                && (write_pos_ - hop_offset_) >= WORD_SAMPLES)
            {
                energy_fired_ = true;
                ale_energy_cb_();
            }

            tracker_.process_candidate(c, write_pos_);
        }
    }
}

void Demodulator::check_silence_reset_(int16_t s)
{
    if (s > -SILENCE_THRESHOLD && s < SILENCE_THRESHOLD) {
        if (++silence_count_ < SILENCE_RESET_SAMPLES) return;
        tracker_.on_silence_gap();
        silence_count_ = SILENCE_RESET_SAMPLES;
    } else {
        silence_count_ = 0;
    }
}

float Demodulator::goertzel_power(const int16_t* block, float freq_hz, uint32_t M)
{
    const float w     = 2.0f * static_cast<float>(M_PI) * freq_hz / 8000.0f;
    const float coeff = 2.0f * std::cos(w);
    float q1 = 0.0f, q2 = 0.0f;
    for (uint32_t n = 0; n < M; ++n) {
        const float q0 = coeff * q1 - q2 + static_cast<float>(block[n]);
        q2 = q1; q1 = q0;
    }
    return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

uint8_t Demodulator::symbol_from_block(const int16_t* block, float& sinad_db_out,
                                       float& win_power_out)
{
    // Symbol decision (A.5.2.6.3): winning tone = largest Goertzel power.
    float best = -1.0f;
    uint8_t rank = 0;
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        const float p = goertzel_power(block, static_cast<float>(TONE_FREQS_HZ[r]));
        if (p > best) { best = p; rank = r; }
    }
    // Full-block winning-tone power: coherent only when the block is aligned to
    // the transmitted symbol, so its per-word sum peaks at the true boundary —
    // the alignment metric for the word-boundary refinement.
    win_power_out = best;

    // ── A.5.4.1.2 true-SINAD on a centered guard-limited sub-window ──────────
    // Measure SINAD on the central 32 samples (drop 16 at each edge where
    // ringing lives), keeping the decision on the full 64-sample block.
    // M = 32 preserves 8-tone orthogonality (250 Hz spacing, integer cycles).
    constexpr uint32_t SINAD_M      = SAMPLES_PER_SYMBOL / 2;   // 32 samples
    constexpr uint32_t SINAD_OFFSET = SAMPLES_PER_SYMBOL / 4;  // 16 → block[16..47]
    const int16_t* win = block + SINAD_OFFSET;

    const float f_win       = static_cast<float>(TONE_FREQS_HZ[rank]);
    const float goertzel_sq = goertzel_power(win, f_win, SINAD_M);
    const float s_avg       = 2.0f * goertzel_sq
                              / static_cast<float>(SINAD_M * SINAD_M);

    float sum_x = 0.0f, sum_sq = 0.0f;
    for (uint32_t n = 0; n < SINAD_M; ++n) {
        const float x = static_cast<float>(win[n]);
        sum_x  += x;
        sum_sq += x * x;
    }
    const float mean      = sum_x / static_cast<float>(SINAD_M);
    const float total_avg = (sum_sq / static_cast<float>(SINAD_M)) - mean * mean;

    constexpr float kEps = 1e-3f;
    const float nd     = std::max(total_avg - s_avg, kEps);
    const float ratio  = (total_avg > 0.0f) ? (total_avg / nd) : 0.0f;
    sinad_db_out = (ratio > 1.0f) ? 10.0f * std::log10f(ratio) : 0.0f;
    if (sinad_db_out > 30.0f) sinad_db_out = 30.0f;
    if (sinad_db_out < 0.0f)  sinad_db_out = 0.0f;
    return FREQ_TO_SYMBOL[rank];
}

DecodedCandidate Demodulator::try_decode_() const
{
    DecodedCandidate c;
    const uint32_t base = write_pos_ - WORD_SAMPLES;
    int16_t blk[SAMPLES_PER_SYMBOL];
    uint8_t sym[SYMBOLS_PER_WORD];
    float sinad_sum  = 0.0f;
    float energy_sum = 0.0f;
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        const uint32_t bk = base + k * SAMPLES_PER_SYMBOL;
        for (uint32_t j = 0; j < SAMPLES_PER_SYMBOL; ++j)
            blk[j] = ring_at(bk + j);
        float sym_sinad, sym_power;
        sym[k] = symbol_from_block(blk, sym_sinad, sym_power);
        sinad_sum  += sym_sinad;
        energy_sum += sym_power;
    }
    c.word.sinad_db = sinad_sum / static_cast<float>(SYMBOLS_PER_WORD);
    c.word_energy   = energy_sum;
    c.decoded_ok    = ALEDecoder::decode(sym, c.word, c.fec, &c.unanimous_votes,
                                         tracker_.golay_mode());
    return c;
}

} // namespace ALE2GModem
} // namespace ale
