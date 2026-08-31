/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem — Modulator and Demodulator implementations.
 *
 * Grid-lock state machine (gate, refinement, adaptive FEC) moved to
 * WordGridTracker (word_grid_tracker.{h,cpp}). Demodulator now owns:
 *   - ring buffer + PCM intake
 *   - Goertzel + symbol_from_block() (signal extraction)
 *   - try_decode_() → DecodedCandidate
 *   - push_samples() dispatch to tracker_.process_candidate()
 *   - silence-gap detection → tracker_.on_silence_gap()
 *
 * Decode math (Goertzel, SINAD measurement) unchanged.
 */

#include "Modem/ale2g_modem.h"
#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "FEC/ale_fec_codec.h"
#include "FEC/golay.h"
#include "PAL/logger.h"
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
    // Diagnostic (2026-08-07): confirms exact word content handed to modulator
    // at TX boundary, to compare against a real-radio capture and rule out a
    // TX-side drop of a conclusion's last word. Called from SM/main thread (via
    // ALEController's transmit_callback), not the real-time audio thread — safe
    // to log here. Silent by default (LogLevel::TRACE).
    pal::log_trace("TXWord", "enqueue %s [%s]",
                    WordParser::word_type_name(word.type), word.address);
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
    subblk_accum_  = 0;
    std::fill(subblk_tone_, subblk_tone_ + TRIPLE_LEN, uint8_t(0xFF));
    for (uint32_t t = 0; t < NUM_TONES; ++t)
        last_triple_pos_[t] = -static_cast<int64_t>(DIVERSITY_WINDOW) - 1;
    // Full re-init clears the global squelch floor; a per-channel hop does NOT
    // (floor is the audio-path noise level, learned across channels).
    scan_floor_       = 0.0f;
    scan_floor_valid_ = false;
    tracker_.reset();
}

void Demodulator::push_samples(const int16_t* samples, uint32_t count)
{
    if (!enabled_) return;

    // Apply pending channel hop atomically at batch boundary (audio-thread only).
    // mark_channel_hop() (controller thread) only sets hop_pending_; mutable
    // demodulator state is reset here so there's no cross-thread data race.
    if (hop_pending_.exchange(false, std::memory_order_acquire)) {
        hop_offset_    = write_pos_;
        energy_fired_  = false;
        subblk_accum_  = 0;
        std::fill(subblk_tone_, subblk_tone_ + TRIPLE_LEN, uint8_t(0xFF));
        for (uint32_t t = 0; t < NUM_TONES; ++t)
            last_triple_pos_[t] = static_cast<int64_t>(write_pos_)
                                  - static_cast<int64_t>(DIVERSITY_WINDOW) - 1;
        tracker_.reset();
        std::fill(ring_.begin(), ring_.end(), int16_t(0));
    }

    for (uint32_t i = 0; i < count; ++i) {
        const int16_t s = samples[i];
        ring_[write_pos_ % BUF_CAP] = s;
        ++write_pos_;

        // Spectrum analyser (waterfall) — display concern, not decode.
        spectrum_.feed(s);

        check_silence_reset_(s);

        // §A.5.3.3 stage-1: level-invariant per-symbol-triple + tone-diversity detector.
        // Every SUBBLOCK_STEP (16 = 2ms, overlapped), test last ANALYSIS_SAMPLES for a
        // tonal winner; TRIPLE_LEN consecutive same-winner sub-blocks = a per-symbol
        // triple (6ms < 8ms symbol). Fires once ≥ MIN_DISTINCT_TONES tones have a triple
        // within DIVERSITY_WINDOW — the 8-FSK signature rejecting noise, voice, and a
        // steady carrier. See ale2g_modem.h for design + sim-locked constants.
        if (++subblk_accum_ >= SUBBLOCK_STEP) {
            subblk_accum_ = 0;
            if ((write_pos_ - hop_offset_) >= HOP_GUARD_SAMPLES) {
                subblk_tone_[2] = subblk_tone_[1];
                subblk_tone_[1] = subblk_tone_[0];

                uint8_t winner;
                float   tonal_pow = 0.0f;
                const bool tonal = subblock_tonal_(winner, tonal_pow);

                if (!tonal) {
                    // Non-tonal (noise) block → train the GLOBAL in-band floor: the
                    // audio-path noise floor learned across every empty channel a scan
                    // visits, never reset per hop (see mark_channel_hop).
                    if (!scan_floor_valid_) { scan_floor_ = tonal_pow; scan_floor_valid_ = true; }
                    else scan_floor_ += (tonal_pow < scan_floor_ ? SCAN_FLOOR_ALPHA_DOWN
                                                                 : SCAN_FLOOR_ALPHA_UP)
                                        * (tonal_pow - scan_floor_);
                    subblk_tone_[0] = 0xFF;
                } else {
                    // Tonal block → optional operator squelch: tone must stand margin_db
                    // above the calibrated floor. Default OFF ⇒ pass through.
                    const bool pass = !scan_squelch_enabled_ || !scan_floor_valid_
                                      || tonal_pow >= scan_floor_ * scan_detect_margin_lin_;
                    subblk_tone_[0] = pass ? winner : uint8_t(0xFF);
                }

                if (subblk_tone_[0] != 0xFF
                    && subblk_tone_[0] == subblk_tone_[1]
                    && subblk_tone_[1] == subblk_tone_[2])
                {
                    last_triple_pos_[subblk_tone_[0]] = static_cast<int64_t>(write_pos_);

                    if (ale_energy_cb_ && !energy_fired_) {
                        uint32_t distinct = 0;
                        for (uint32_t t = 0; t < NUM_TONES; ++t)
                            if (static_cast<int64_t>(write_pos_) - last_triple_pos_[t]
                                    <= static_cast<int64_t>(DIVERSITY_WINDOW))
                                ++distinct;
                        if (distinct >= MIN_DISTINCT_TONES) {
                            energy_fired_ = true;
                            ale_energy_cb_();
                        }
                    }
                }
            }
        }

        if (++step_accum_ >= (tracker_.use_fine_step() ? DECODE_STEP_FINE
                                                        : DECODE_STEP_COARSE)) {
            step_accum_ = 0;
            if (write_pos_ < WORD_SAMPLES) continue;

            const DecodedCandidate c = try_decode_();
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
    // Full-block winning-tone power: coherent only when block is aligned to the
    // transmitted symbol, so per-word sum peaks at the true boundary — the
    // alignment metric for word-boundary refinement.
    win_power_out = best;

    // ── A.5.4.1.2 true-SINAD on a centered guard-limited sub-window ──────────
    // SINAD measured on central 32 samples (drop 16 at each edge where ringing
    // lives); decision still made on full 64-sample block. M=32 preserves
    // 8-tone orthogonality (250 Hz spacing, integer cycles).
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
    sinad_db_out = (ratio > 1.0f) ? 10.0f * std::log10(ratio) : 0.0f;
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

bool Demodulator::subblock_tonal_(uint8_t& winner, float& tonal_pow) const
{
    int16_t blk[ANALYSIS_SAMPLES];
    for (uint32_t i = 0; i < ANALYSIS_SAMPLES; ++i)
        blk[i] = ring_at(write_pos_ - ANALYSIS_SAMPLES + i);

    // 8-tone Goertzel powers; find the winning bin and its strongest neighbour.
    float   p[NUM_TONES];
    float   best = -1.0f;
    uint8_t best_rank = 0;
    for (uint32_t t = 0; t < NUM_TONES; ++t) {
        p[t] = goertzel_power(blk, static_cast<float>(TONE_FREQS_HZ[t]), ANALYSIS_SAMPLES);
        if (p[t] > best) { best = p[t]; best_rank = static_cast<uint8_t>(t); }
    }
    float neighbour = 0.0f;
    if (best_rank > 0)             neighbour = std::max(neighbour, p[best_rank - 1]);
    if (best_rank < NUM_TONES - 1) neighbour = std::max(neighbour, p[best_rank + 1]);

    // Peakiness = tonal power (winner + strongest neighbour) / total block energy.
    // 2*goertzel/M² recovers tone's mean-square power (== amp²/2 for a pure tone);
    // +neighbour term absorbs 125 Hz scalloping. Level-invariant, so no absolute
    // audio-path calibration needed here (separate concern from operator squelch).
    tonal_pow =
        2.0f * (best + neighbour) / static_cast<float>(ANALYSIS_SAMPLES * ANALYSIS_SAMPLES);
    float energy = 0.0f;
    for (uint32_t i = 0; i < ANALYSIS_SAMPLES; ++i) {
        const float x = static_cast<float>(blk[i]);
        energy += x * x;
    }
    energy /= static_cast<float>(ANALYSIS_SAMPLES);

    if (energy <= 0.0f) return false;
    if (tonal_pow < STAGE1_PEAKINESS * energy) return false;
    winner = best_rank;
    return true;
}

} // namespace ALE2GModem
} // namespace ale
