/**
 * \file Modem/spectrum_analyser.cpp
 * \brief Waterfall / spectrum analyser — moved out of ALE2GModem::Demodulator.
 */

#include "Modem/spectrum_analyser.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace {
// In-place Cooley-Tukey radix-2 DIT FFT.  N must be a power of 2.
// re[]/im[] are overwritten with the complex spectrum.
void fft_inplace(float* re, float* im, size_t N)
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

SpectrumAnalyser::SpectrumAnalyser()
{
    std::fill(ring_.begin(), ring_.end(), int16_t(0));

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

void SpectrumAnalyser::feed(int16_t sample)
{
    // Always advance the ring so it holds the last SPEC_FFT_N samples regardless
    // of whether a callback is registered (matches the pre-extraction invariant
    // where the demodulator's ring was always full; a callback set later then has
    // history immediately instead of waiting one window to fill).
    ring_[count_ % SPEC_FFT_N] = sample;
    ++count_;

    if (!cb_) return;             // no consumer — skip the FFT work entirely
    if (count_ < SPEC_FFT_N) return;   // need a full window of history first
    if (++spec_accum_ < SPEC_INTERVAL) return;
    spec_accum_ = 0;
    compute_();
}

void SpectrumAnalyser::compute_()
{
    // Fill real/imag working arrays from the last SPEC_FFT_N samples of the ring
    // (chronological order: oldest first), scaled to [-1, +1] and weighted by the
    // precomputed Blackman-Harris window.  Member arrays (not stack) avoid placing
    // ~80 KB on the audio-thread stack.
    constexpr float kScale = 1.0f / 32768.0f;
    // The next write position (= oldest sample, since the buffer is full).
    const size_t start = count_ % SPEC_FFT_N;
    for (size_t k = 0; k < SPEC_FFT_N; ++k) {
        spec_re_[k] = ring_[(start + k) % SPEC_FFT_N] * kScale * spec_window_[k];
        spec_im_[k] = 0.0f;
    }

    fft_inplace(spec_re_.data(), spec_im_.data(), SPEC_FFT_N);

    // One-sided magnitude spectrum (bins 0 … SPEC_FFT_N/2), normalised by N so
    // magnitude is independent of FFT size.
    constexpr size_t kBins = SPEC_FFT_N / 2 + 1;
    const float kNorm = 1.0f / static_cast<float>(SPEC_FFT_N);
    spec_bins_[0] = std::sqrt(spec_re_[0] * spec_re_[0] + spec_im_[0] * spec_im_[0]) * kNorm;
    for (size_t k = 1; k < SPEC_FFT_N / 2; ++k)
        spec_bins_[k] = std::sqrt(spec_re_[k] * spec_re_[k] + spec_im_[k] * spec_im_[k]) * kNorm * 2.0f;
    spec_bins_[SPEC_FFT_N / 2] = std::sqrt(spec_re_[SPEC_FFT_N/2] * spec_re_[SPEC_FFT_N/2]
                                           + spec_im_[SPEC_FFT_N/2] * spec_im_[SPEC_FFT_N/2]) * kNorm;

    // Power dBFS (ref = full-scale Hann-windowed sine ≈ −6 dBFS).  Spreads HF
    // dynamic range (~80 dB) across the display instead of compressing everything
    // into the top few percent of a linear scale.
    for (size_t k = 0; k < kBins; ++k)
        spec_bins_[k] = 10.0f * std::log10(spec_bins_[k] * spec_bins_[k] + 1e-12f);

    constexpr float kHzPerBin = 8000.0f / static_cast<float>(SPEC_FFT_N);
    cb_(spec_bins_.data(), kBins, kHzPerBin);
}

} // namespace ALE2GModem
} // namespace ale