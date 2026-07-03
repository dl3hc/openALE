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

namespace {

// In-place Cooley-Tukey radix-2 DIT FFT.
// N must be a power of 2.  re[]/im[] are overwritten with the complex spectrum.
static void fft_inplace(float* re, float* im, size_t N)
{
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    // Butterfly stages
    for (size_t len = 2; len <= N; len <<= 1) {
        const float ang  = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        const float wRe  = std::cos(ang);
        const float wIm  = std::sin(ang);
        for (size_t i = 0; i < N; i += len) {
            float cRe = 1.0f, cIm = 0.0f;
            const size_t half = len >> 1;
            for (size_t k = 0; k < half; ++k) {
                const float uRe = re[i + k];
                const float uIm = im[i + k];
                const float vRe = re[i + k + half] * cRe - im[i + k + half] * cIm;
                const float vIm = re[i + k + half] * cIm + im[i + k + half] * cRe;
                re[i + k]        = uRe + vRe;
                im[i + k]        = uIm + vIm;
                re[i + k + half] = uRe - vRe;
                im[i + k + half] = uIm - vIm;
                const float nRe  = cRe * wRe - cIm * wIm;
                cIm = cRe * wIm + cIm * wRe;
                cRe = nRe;
            }
        }
    }
}

} // namespace

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
    // Blackman-Harris 4-term window — -92 dB sidelobe suppression vs Hann's -31 dB.
    // With ALE tones 40-60 dB above the noise floor, Hann sidelobes (-31 dB) sit
    // 9-29 dB above the noise and make each tone look broad.  Blackman-Harris keeps
    // sidelobes below the noise floor at all realistic HF SNR values.
    constexpr float bh_a0 = 0.35875f, bh_a1 = 0.48829f,
                    bh_a2 = 0.14128f, bh_a3 = 0.01168f;
    for (size_t k = 0; k < SPEC_FFT_N; ++k) {
        const float x = 2.0f * static_cast<float>(M_PI) * k
                                / static_cast<float>(SPEC_FFT_N - 1);
        spec_window_[k] = bh_a0 - bh_a1 * std::cos(x)
                                + bh_a2 * std::cos(2.0f * x)
                                - bh_a3 * std::cos(3.0f * x);
    }
}

void Demodulator::reset()
{
    std::fill(ring_.begin(), ring_.end(), int16_t(0));
    write_pos_     = 0;
    step_accum_    = 0;
    grid_locked_   = false;
    grid_anchor_   = 0;
    uncorr_anchor_ = 0;
    silence_count_ = 0;
    refining_      = false;
    restore_base_operating_point_();   // fresh acquisition uses the base point
}

void Demodulator::push_samples(const int16_t* samples, uint32_t count)
{
    if (!enabled_) return;

    for (uint32_t i = 0; i < count; ++i) {
        const int16_t s = samples[i];
        ring_[write_pos_ % BUF_CAP] = s;
        ++write_pos_;

        // Spectrum analyser: independent FFT pass, throttled to ~10 Hz.
        // Runs regardless of grid state; the ring has enough history once
        // write_pos_ >= SPEC_FFT_N.
        if (spectrum_cb_ && write_pos_ >= SPEC_FFT_N) {
            if (++spec_accum_ >= SPEC_INTERVAL) {
                spec_accum_ = 0;
                compute_spectrum_();
            }
        }

        // Silence-gap grid reset: prolonged absence of signal indicates the
        // previous transmission has ended.  Reset the grid lock so the next
        // transmission can re-anchor at its own sub-symbol phase offset.
        if (s > -SILENCE_THRESHOLD && s < SILENCE_THRESHOLD) {
            if (++silence_count_ >= SILENCE_RESET_SAMPLES) {
                // Transmission ended: flush any pending boundary refinement (the
                // best candidate is a fully gated word — do not lose it), then
                // release the grid and return to the base (acquisition) operating
                // point so the next transmission — which may be weaker —
                // re-acquires with full tolerance.
                if (refining_) commit_refined_word_();
                if (grid_locked_) restore_base_operating_point_();
                grid_locked_   = false;
                silence_count_ = SILENCE_RESET_SAMPLES;
            }
        } else {
            silence_count_ = 0;
        }

        if (++step_accum_ >= ((grid_locked_ || refining_) ? DECODE_STEP_FINE
                                                          : DECODE_STEP_COARSE)) {
            step_accum_ = 0;
            if (write_pos_ < WORD_SAMPLES) continue;

            // Boundary refinement: one full symbol past the first passing
            // candidate the energy peak has necessarily been bracketed —
            // commit the best candidate (anchors grid + fires word_cb_).
            if (refining_ && (write_pos_ - ref_first_pos_) >= SAMPLES_PER_SYMBOL)
                commit_refined_word_();

            ALEWord word;
            Golay::DecodeResult fec;
            uint8_t unanimous_votes = 0;
            float   word_energy     = 0.0f;
            const bool decoded_ok = try_decode(word, fec, unanimous_votes, word_energy);
            if (decoded_ok && gate_word_(fec, word, unanimous_votes)) {
                // Do NOT accept the first passing offset: the decode pipeline is
                // shift-tolerant up to ~half a symbol, so the earliest passing
                // offset lies well before the true word boundary and would poison
                // the SINAD measurement (see file header, "Word-boundary
                // refinement"). Track the candidate whose summed winning-tone
                // energy — a matched-filter statistic peaking exactly at the
                // boundary — is maximal; commit_refined_word_() emits it.
                if (!refining_) {
                    refining_        = true;
                    ref_first_pos_   = write_pos_;
                    ref_best_energy_ = -1.0f;
                }
                if (word_energy > ref_best_energy_) {
                    ref_best_energy_ = word_energy;
                    ref_best_pos_    = write_pos_;
                    ref_best_votes_  = unanimous_votes;
                    ref_best_word_   = word;
                }
            } else if (!decoded_ok && !refining_ && word.golay_uncorrectable
                       && grid_locked_
                       && unanimous_votes >= min_unanimous_votes_) {
                // Grid-locked, quality-thresholded uncorrectable word:
                // report via word_cb_ (word.valid=false, word.golay_uncorrectable=true)
                // so the controller can contribute 48 to the BER sum per A.5.4.1.1.
                // Mirror the on-grid check from accept_word_() but do NOT advance
                // grid_anchor_ — a failed word does not re-anchor the sync grid.
                // uncorr_anchor_ prevents re-firing for the same word slot.
                const uint32_t samples_since = write_pos_ - grid_anchor_;
                const uint32_t min_sp        = WORD_SAMPLES - SAMPLES_PER_SYMBOL;
                if (samples_since >= min_sp) {
                    const uint32_t phase = samples_since % WORD_SAMPLES;
                    // On-grid check: a REAL word slot is always at phase==0 (since
                    // is an exact multiple of WORD_SAMPLES, and the 4-sample decode
                    // grid lands on it exactly because 3136/4 is integer). The
                    // neighbors at phase 4 / phase WORD_SAMPLES-4 are one-step
                    // straddles of the inter-word boundary — Golay-uncorrectable
                    // mashes of two adjacent words, not real words. The previous
                    // ±1-symbol tolerance admitted those straddles, double-counting
                    // each boundary as a spurious "uncorrectable word" (and
                    // inflating BER by 48 per fire per A.5.4.1.1). Fire only at the
                    // exact slot so straddles are dropped but a genuine on-grid
                    // uncorrectable word (still phase 0) is still reported.
                    if ((phase == 0u)
                            && (write_pos_ - uncorr_anchor_ >= min_sp)) {
                        uncorr_anchor_ = write_pos_;
                        if (word_cb_) word_cb_(word);
                    }
                }
            }
        }
    }
}

void Demodulator::compute_spectrum_()
{
    // Fill real/imag member arrays from the last SPEC_FFT_N samples of the ring,
    // scaled to [-1, +1] and weighted by the pre-computed Hann window.
    // Member arrays (not stack) avoid placing ~80 KB on the audio-thread stack.
    constexpr float kScale = 1.0f / 32768.0f;
    const uint32_t base = write_pos_ - static_cast<uint32_t>(SPEC_FFT_N);
    for (size_t k = 0; k < SPEC_FFT_N; ++k) {
        spec_re_[k] = ring_at(base + static_cast<uint32_t>(k)) * kScale * spec_window_[k];
        spec_im_[k] = 0.0f;
    }

    fft_inplace(spec_re_.data(), spec_im_.data(), SPEC_FFT_N);

    // Compute one-sided magnitude spectrum (bins 0 … SPEC_FFT_N/2).
    // Normalise by N so magnitude is independent of FFT size.
    constexpr size_t kBins = SPEC_FFT_N / 2 + 1;
    const float kNorm = 1.0f / static_cast<float>(SPEC_FFT_N);
    spec_bins_[0] = std::sqrt(spec_re_[0] * spec_re_[0] + spec_im_[0] * spec_im_[0]) * kNorm;
    for (size_t k = 1; k < SPEC_FFT_N / 2; ++k)
        spec_bins_[k] = std::sqrt(spec_re_[k] * spec_re_[k] + spec_im_[k] * spec_im_[k]) * kNorm * 2.0f;
    spec_bins_[SPEC_FFT_N / 2] = std::sqrt(spec_re_[SPEC_FFT_N/2] * spec_re_[SPEC_FFT_N/2]
                                           + spec_im_[SPEC_FFT_N/2] * spec_im_[SPEC_FFT_N/2]) * kNorm;

    // Convert to power dBFS (ref = full-scale Hann-windowed sine ≈ −6 dBFS).
    // Spreads HF dynamic range (~80 dB) across the display instead of compressing
    // everything into the top few percent of a linear scale.
    for (size_t k = 0; k < kBins; ++k)
        spec_bins_[k] = 10.0f * std::log10(spec_bins_[k] * spec_bins_[k] + 1e-12f);

    constexpr float kHzPerBin = 8000.0f / static_cast<float>(SPEC_FFT_N);
    spectrum_cb_(spec_bins_.data(), kBins, kHzPerBin);
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
    // Symbol decision (A.5.2.6.3): the winning tone is the one with the largest
    // Goertzel power over the full 64-sample symbol block. This is unchanged.
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

    // ── A.5.4.1.2 true-SINAD on a centered guard-limited sub-window ───────────
    // The previous SINAD used the sum of the 7 non-winning tone bins as
    // "noise+distortion". On a clean loopback ~25 % of the winning-tone energy
    // leaks off-fundamental at the 64-sample window edges (sub-symbol straddle +
    // resampling ISI), so that ratio read only ~6–7 dB even for a flawless,
    // error-free decode — purely a receiver artifact, not link quality.
    //
    // Measure SINAD instead on the central 32 samples (drop 16 at each edge where
    // the ringing lives), keeping the decision on the full 64-sample block.
    // M = 32 preserves 8-tone orthogonality: 250 Hz spacing over 32 samples at
    // 8 kHz is exactly one DFT bin, and every tone is an integer number of cycles
    // (750 Hz = 3 cyc … 2500 Hz = 10 cyc) over the window — so a clean tone
    // still leaks ~0 into the others and a clean decode reads a high SINAD.
    // Real channel noise is present throughout the window (not just the edges),
    // so it is captured and lowers SINAD as it should (true (S+N+D)/(N+D)).
    constexpr uint32_t SINAD_M      = SAMPLES_PER_SYMBOL / 2;   // 32 samples
    constexpr uint32_t SINAD_OFFSET = SAMPLES_PER_SYMBOL / 4;  // 16 → block[16..47]
    const int16_t* win = block + SINAD_OFFSET;

    // Signal (winning tone) average power. Goertzel returns |X(k)|^2; for a real
    // sinusoid on-bin over M samples, average power = 2·|X(k)|^2 / M^2.
    const float f_win       = static_cast<float>(TONE_FREQS_HZ[rank]);
    const float goertzel_sq = goertzel_power(win, f_win, SINAD_M);
    const float s_avg       = 2.0f * goertzel_sq
                              / static_cast<float>(SINAD_M * SINAD_M);

    // Total (S+N+D) average power from the time-domain samples, DC-removed so a
    // static offset is not counted as noise+distortion.
    float sum_x = 0.0f, sum_sq = 0.0f;
    for (uint32_t n = 0; n < SINAD_M; ++n) {
        const float x = static_cast<float>(win[n]);
        sum_x  += x;
        sum_sq += x * x;
    }
    const float mean      = sum_x / static_cast<float>(SINAD_M);
    const float total_avg = (sum_sq / static_cast<float>(SINAD_M)) - mean * mean;

    // SINAD = total / (total − S), in dB, clamped to the [0, 30] LQA scale.
    // Guard against numerical total ≤ S on a near-pure tone (clean decode →
    // ratio diverges → clamps to 30, the "best" code).
    constexpr float kEps = 1e-3f;
    const float nd     = std::max(total_avg - s_avg, kEps);
    const float ratio  = (total_avg > 0.0f) ? (total_avg / nd) : 0.0f;
    sinad_db_out = (ratio > 1.0f) ? 10.0f * std::log10f(ratio) : 0.0f;
    if (sinad_db_out > 30.0f) sinad_db_out = 30.0f;   // LQA_QUALITY_MAX
    if (sinad_db_out < 0.0f)  sinad_db_out = 0.0f;    // LQA_QUALITY_MIN
    return FREQ_TO_SYMBOL[rank];
}

// A MIL-STD-188-141B transmission always begins with a TO, TWAS, or TIS word;
// these are the only word types a receiver acquires INITIAL word-sync on (cf.
// A.5.2.6.3 "acceptable preamble", and the reference WordSync()/PreambleOK()).
// A.5.2.6.3 criterion 4: only TO/TWAS/TIS words trigger initial grid-lock
// acquisition.  DATA / REP / CMD / FROM / THRU occur only mid-frame and are
// read freely once the grid is locked — not a decoder limitation, spec intent.
static bool is_acquisition_anchor(PreambleType t)
{
    return t == PreambleType::TO || t == PreambleType::TWAS || t == PreambleType::TIS;
}

bool Demodulator::gate_word_(const Golay::DecodeResult& fec, const ALEWord& word,
                             uint8_t unanimous_votes) const
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
    if (!grid_locked_)
        return is_acquisition_anchor(word.type);

    // ── Continuing sync (grid locked) ───────────────────────────────────────
    // grid_anchor_ holds the sample position of the last committed word boundary;
    // words are one WORD_SAMPLES apart on the grid.
    const uint32_t samples_since_last_word = write_pos_ - grid_anchor_;

    // The Goertzel window slides every DECODE_STEP_COARSE/FINE samples, so the
    // same word would otherwise be decoded many times in a row.  Ignore any
    // candidate closer than (almost) one full word to the previous committed word.
    const uint32_t min_spacing = WORD_SAMPLES - SAMPLES_PER_SYMBOL;  // one word, minus one symbol of slack
    if (samples_since_last_word < min_spacing)
        return false;

    // A clean decode is always trusted as a candidate.
    if (clean_decode)
        return true;

    // A FEC-corrected decode is trusted only when it lands on the word grid,
    // i.e. within one symbol (SAMPLES_PER_SYMBOL samples) of an expected word boundary
    // (A.5.2.6.3 criterion 9: correct triple-redundant word phase).
    const uint32_t phase_in_word    = samples_since_last_word % WORD_SAMPLES;
    return (phase_in_word <= SAMPLES_PER_SYMBOL) ||
           (phase_in_word >= WORD_SAMPLES - SAMPLES_PER_SYMBOL);
}

void Demodulator::commit_refined_word_()
{
    // Anchor the grid at the best-aligned candidate — not the first passing one —
    // so the next word's decode window and the per-symbol SINAD line up with the
    // true symbol boundaries (see "Word-boundary refinement" in the file header).
    grid_locked_ = true;
    grid_anchor_ = ref_best_pos_;
    refining_    = false;

    // A.5.2.6.3 "DO": fold the committed word's quality into the adaptive
    // estimate and update the operating point.
    if (adaptive_) {
        adaptive_fec_.observe(ref_best_votes_);
        golay_mode_          = adaptive_fec_.mode();
        min_unanimous_votes_ = adaptive_fec_.threshold();
    }
    if (word_cb_) word_cb_(ref_best_word_);
}

bool Demodulator::try_decode(ALEWord& out, Golay::DecodeResult& fec, uint8_t& unanimous_votes,
                             float& word_energy) const
{
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
    // A.5.4.1.2: SINAD = time-averaged value over the signal duration (49 symbols).
    out.sinad_db = sinad_sum / static_cast<float>(SYMBOLS_PER_WORD);
    // Alignment metric for the boundary refinement (peaks at the true boundary).
    word_energy = energy_sum;
    return ALEDecoder::decode(sym, out, fec, &unanimous_votes, golay_mode_);
}

} // namespace ALE2GModem
} // namespace ale
