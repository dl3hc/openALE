/**
 * \file Modem/spectrum_analyser.h
 * \brief Waterfall / spectrum analyser for the ALE 2G demodulator.
 *
 * Extracted from ALE2GModem::Demodulator so the demodulator proper does only
 * decode (matching LinuxALE's "modem = decode only" separation).  This class is
 * a pure display-side concern: it owns its own SPEC_FFT_N-sample ring, a
 * precomputed Blackman-Harris window, and an in-place FFT; it emits a
 * power-spectrum callback throttled to ~10 Hz.
 *
 * Tunable parameters — both must stay consistent with apps/gui/app.js:
 *
 *   SPEC_FFT_N   — FFT size (power of 2).  Hz/bin = 8000 / SPEC_FFT_N.
 *                  2048 → 3.9 Hz/bin, 64 bins/tone-gap — current default.
 *   SPEC_INTERVAL — Samples between consecutive FFT triggers.
 *                   Update rate = SAMPLE_RATE_HZ / SPEC_INTERVAL.
 *                   Must match the JS row interval (default 100 ms):
 *                     SPEC_INTERVAL = 8000 × 0.100 = 800 (10 Hz).
 *
 * The names and values are GUI-coupled — do not rename or revalue without
 * updating apps/gui/app.js.
 */

#pragma once

#include <array>
#include <functional>
#include <cstdint>

namespace ale {
namespace ALE2GModem {

class SpectrumAnalyser {
public:
    /// Spectrum callback — fired ~10 times/second from the audio thread.
    /// bins[k] = power spectral density in dBFS at k * hz_per_bin Hz.
    /// Called from the audio capture thread — keep it short or hand off to a queue.
    using SpectrumCallback =
        std::function<void(const float* bins, size_t count, float hz_per_bin)>;

    SpectrumAnalyser();

    /// Register a callback for spectrum/waterfall data.  Pass nullptr to disable.
    void set_callback(SpectrumCallback cb) { cb_ = std::move(cb); }

    /// Feed one PCM sample (8 kHz, mono, 16-bit).  Throttled internally; the
    /// callback (if any) fires only after SPEC_FFT_N samples have arrived and
    /// SPEC_INTERVAL samples have elapsed since the last fire.
    void feed(int16_t sample);

private:
    static constexpr size_t SPEC_FFT_N    = 2048;  // bins: 0–4000 Hz, ≈3.9 Hz/bin
    static constexpr uint32_t SPEC_INTERVAL = 800;  // samples between updates (~10 Hz)

    SpectrumCallback              cb_;
    std::array<int16_t, SPEC_FFT_N>          ring_;       // own history buffer
    uint32_t                       count_      = 0;  // total samples fed (monotonic)
    uint32_t                       spec_accum_ = 0;
    std::array<float, SPEC_FFT_N>           spec_window_;  // precomputed Blackman-Harris
    std::array<float, SPEC_FFT_N>           spec_re_;      // FFT working buffer (real)
    std::array<float, SPEC_FFT_N>           spec_im_;      // FFT working buffer (imag)
    std::array<float, SPEC_FFT_N / 2 + 1>  spec_bins_;    // output magnitude/dBFS bins

    void compute_();
};

} // namespace ALE2GModem
} // namespace ale