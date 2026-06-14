/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem — Modulator and Demodulator implementations.
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

void Modulator::enqueue_frame(const Frame& frame)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (uint64_t tx49 : frame.encode())
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
{}

void Demodulator::reset()
{
    std::fill(ring_.begin(), ring_.end(), int16_t(0));
    write_pos_     = 0;
    step_accum_    = 0;
    grid_locked_   = false;
    grid_anchor_   = 0;
    silence_count_ = 0;
    restore_base_operating_point_();   // fresh acquisition uses the base point
}

void Demodulator::push_samples(const int16_t* samples, uint32_t count)
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
                // Transmission ended: release the grid and return to the base
                // (acquisition) operating point so the next transmission — which
                // may be weaker — re-acquires with full tolerance.
                if (grid_locked_) restore_base_operating_point_();
                grid_locked_   = false;
                silence_count_ = SILENCE_RESET_SAMPLES;
            }
        } else {
            silence_count_ = 0;
        }

        if (++step_accum_ >= DECODE_STEP) {
            step_accum_ = 0;
            if (write_pos_ < WORD_SAMPLES) continue;

            ALEWord word;
            Golay::DecodeResult fec;
            uint8_t unanimous_votes = 0;
            if (try_decode(word, fec, unanimous_votes) &&
                accept_word_(fec, word, unanimous_votes)) {
                // A.5.2.6.3 "DO": once tracking, fold the accepted word's quality
                // into the adaptive estimate and update the operating point.
                if (adaptive_) {
                    adaptive_fec_.observe(unanimous_votes);
                    golay_mode_          = adaptive_fec_.mode();
                    min_unanimous_votes_ = adaptive_fec_.threshold();
                }
                if (word_cb_) word_cb_(word);
            }
        }
    }
}

float Demodulator::goertzel_power(const int16_t* block, float freq_hz)
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

uint8_t Demodulator::symbol_from_block(const int16_t* block)
{
    float   best = -1.0f;
    uint8_t rank = 0;
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        float p = goertzel_power(block, static_cast<float>(TONE_FREQS_HZ[r]));
        if (p > best) { best = p; rank = r; }
    }
    return FREQ_TO_SYMBOL[rank];
}

// A MIL-STD-188-141B transmission always begins with a TO, TWAS, or TIS word;
// these are the only word types a receiver acquires INITIAL word-sync on (cf.
// A.5.2.6.3 "acceptable preamble", and the reference WordSync()/PreambleOK()).
// DATA / REP / CMD / FROM / THRU occur only mid-frame and are read once the grid
// is locked.
static bool is_acquisition_anchor(PreambleType t)
{
    return t == PreambleType::TO || t == PreambleType::TWAS || t == PreambleType::TIS;
}

bool Demodulator::accept_word_(const Golay::DecodeResult& fec, const ALEWord& word,
                               uint8_t unanimous_votes)
{
    // ── A.5.2.6.3 criterion 1: unanimous-vote (signal-quality) threshold ────
    // Applied to EVERY word, initial and continuing.  This is the standard's
    // primary BER discriminator and simultaneously enforces "correct triple
    // redundant word phase": a misaligned decode window makes the three symbol
    // copies disagree, collapsing the unanimous count below the threshold.  It
    // is what keeps the FEC-tolerant acquisition below from locking onto noise.
    if (unanimous_votes < min_unanimous_votes_)
        return false;

    // "Successful Golay decode" per A.5.2.6.3 means all detected errors were
    // within the correction power of the code — i.e. DECODE_OK *or*
    // DECODE_CORRECTED.  (try_decode only returns true for those two, and only
    // after ASCII validation, so criteria 2/3/5/6/7 are already met here.)
    const bool clean_decode = (fec.flag == Golay::DECODE_OK);

    // ── Initial acquisition ─────────────────────────────────────────────────
    // Acquire USING the FEC, not by demanding a zero-error decode.  Requiring
    // DECODE_OK would only ever lock onto a bit-exact peer (two ale_cli
    // instances); a real or foreign-encoder transmission (e.g. PCALE) presents
    // residual symbol errors at every alignment and so decodes as
    // DECODE_CORRECTED.  Criterion 4 (acceptable leading preamble) plus the
    // unanimous-vote gate above guard against false locks.
    if (!grid_locked_) {
        if (!is_acquisition_anchor(word.type))
            return false;
        grid_locked_ = true;
        grid_anchor_ = write_pos_;
        return true;
    }

    // ── Continuing sync (grid locked) ───────────────────────────────────────
    // grid_anchor_ holds the sample position of the last accepted word boundary;
    // words are one WORD_SAMPLES apart on the grid.
    const uint32_t samples_since_last_word = write_pos_ - grid_anchor_;

    // The FFT window slides forward every DECODE_STEP samples, so the same word
    // would otherwise be decoded many times in a row.  Ignore any candidate
    // closer than (almost) one full word to the previous accepted word.
    const uint32_t min_spacing = WORD_SAMPLES - FFT_SIZE;  // one word, minus one symbol of slack
    if (samples_since_last_word < min_spacing)
        return false;

    // A clean decode is always trusted and re-anchors the grid.
    if (clean_decode) {
        grid_anchor_ = write_pos_;
        return true;
    }

    // A FEC-corrected decode is trusted only when it lands on the word grid,
    // i.e. within one symbol (FFT_SIZE samples) of an expected word boundary
    // (A.5.2.6.3 criterion 9: correct triple-redundant word phase).
    const uint32_t phase_in_word    = samples_since_last_word % WORD_SAMPLES;
    const bool     on_word_boundary = (phase_in_word <= FFT_SIZE) ||
                                      (phase_in_word >= WORD_SAMPLES - FFT_SIZE);
    if (on_word_boundary) {
        grid_anchor_ = write_pos_;
        return true;
    }
    return false;
}

bool Demodulator::try_decode(ALEWord& out, Golay::DecodeResult& fec, uint8_t& unanimous_votes) const
{
    const uint32_t base = write_pos_ - WORD_SAMPLES;
    int16_t blk[FFT_SIZE];
    uint8_t sym[SYMBOLS_PER_WORD];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        const uint32_t bk = base + k * FFT_SIZE;
        for (uint32_t j = 0; j < FFT_SIZE; ++j)
            blk[j] = ring_at(bk + j);
        sym[k] = symbol_from_block(blk);
    }
    return ALEDecoder::decode(sym, out, fec, &unanimous_votes, golay_mode_);
}

} // namespace ALE2GModem
} // namespace ale
