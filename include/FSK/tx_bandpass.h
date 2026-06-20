/**
 * \file FSK/tx_bandpass.h
 * \brief Streaming linear-phase FIR band-pass for the TX audio output.
 *
 * Models the audio pass-band of a real SSB transceiver: it lets the 8 ALE tones
 * (750–2500 Hz) through flat and attenuates everything outside the voice band —
 * the sub-300 Hz keying-sideband skirt and any out-of-band content above
 * ~2.8 kHz. This is purely an OUTPUT filter; it does NOT change the 8-FSK keying
 * (frequencies, timing, phase continuity), so a standard ALE receiver decodes
 * the result unchanged (MIL-STD-188-141B compatibility preserved).
 *
 * Design: difference of two Blackman-windowed sinc low-passes
 *   h_bp = LP(f_hi) − LP(f_lo)
 * (each low-pass normalised to unity DC gain), giving a band-pass with ~unity
 * pass-band gain and a DC null. Same window/sinc math as
 * Resampler::design_prototype() in include/App/resampler.h.
 *
 * Streaming: process() may be called repeatedly with arbitrary block sizes;
 * filter history is carried across calls (constant group delay (N−1)/2 samples).
 */

#pragma once

#include "FSK/ale_waveform.h"
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ale {

class TxBandpass {
public:
    // Defaults: flat across 750–2500 Hz; −6 dB corners ~450 / ~2800 Hz at 8 kHz.
    explicit TxBandpass(uint32_t fs = SAMPLE_RATE_HZ,
                        double f_lo = 450.0, double f_hi = 2800.0,
                        int taps = 161) {
        design(fs, f_lo, f_hi, taps);
        hist_.assign(h_.size(), 0.0f);
        pos_ = 0;
    }

    /** Filter one block; results are appended to \p out (one output per input). */
    void process(const int16_t* in, size_t n, std::vector<int16_t>& out) {
        const size_t N = h_.size();
        out.reserve(out.size() + n);
        for (size_t i = 0; i < n; ++i) {
            hist_[pos_] = static_cast<float>(in[i]);
            // Convolve: h_[0] multiplies the newest sample (hist_[pos_]),
            // walking backwards through the ring buffer.
            double acc = 0.0;
            size_t idx = pos_;
            for (size_t j = 0; j < N; ++j) {
                acc += static_cast<double>(h_[j]) * static_cast<double>(hist_[idx]);
                idx = (idx == 0) ? (N - 1) : (idx - 1);
            }
            pos_ = (pos_ + 1) % N;

            long v = std::lround(acc);
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            out.push_back(static_cast<int16_t>(v));
        }
    }

    /** Clear filter history. */
    void reset() {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        pos_ = 0;
    }

    /** Group delay in samples ((N−1)/2 for a symmetric type-I FIR). */
    size_t group_delay() const { return (h_.size() - 1) / 2; }

private:
    void design(uint32_t fs, double f_lo, double f_hi, int taps) {
        if (taps < 3)        taps = 3;
        if ((taps & 1) == 0) ++taps;                 // odd → symmetric, integer delay

        const double PI  = 3.14159265358979323846;
        const double c   = (taps - 1) / 2.0;
        const double flo = f_lo / static_cast<double>(fs);
        const double fhi = f_hi / static_cast<double>(fs);

        std::vector<double> lo(static_cast<size_t>(taps));
        std::vector<double> hi(static_cast<size_t>(taps));
        double sum_lo = 0.0, sum_hi = 0.0;

        for (int k = 0; k < taps; ++k) {
            const double x = k - c;
            // Ideal low-pass impulse: 2·fc·sinc(2·fc·x), sinc(y)=sin(πy)/(πy).
            auto lp = [&](double fc) {
                return (x == 0.0) ? (2.0 * fc)
                                  : std::sin(2.0 * PI * fc * x) / (PI * x);
            };
            const double w = 0.42
                           - 0.5  * std::cos(2.0 * PI * k / (taps - 1))
                           + 0.08 * std::cos(4.0 * PI * k / (taps - 1));   // Blackman
            const double vlo = lp(flo) * w;
            const double vhi = lp(fhi) * w;
            lo[static_cast<size_t>(k)] = vlo; sum_lo += vlo;
            hi[static_cast<size_t>(k)] = vhi; sum_hi += vhi;
        }

        h_.resize(static_cast<size_t>(taps));
        for (int k = 0; k < taps; ++k) {
            const double l = lo[static_cast<size_t>(k)] / sum_lo;   // unity DC gain
            const double h = hi[static_cast<size_t>(k)] / sum_hi;
            h_[static_cast<size_t>(k)] = static_cast<float>(h - l); // band-pass
        }
    }

    std::vector<float> h_;       // FIR coefficients (h_[0] = newest tap)
    std::vector<float> hist_;    // circular history, size == h_.size()
    size_t             pos_ = 0; // index of newest sample in hist_
};

} // namespace ale
