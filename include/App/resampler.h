/**
 * \file App/resampler.h
 * \brief Streaming rational sample-rate converter (polyphase windowed-sinc FIR).
 *
 * The ALE modem and state machine run at a fixed 8 kHz, but real sound cards
 * (and most virtual audio cables) run at 44.1 / 48 kHz.  This converter sits at
 * the device boundary:
 *
 *   TX:  8 kHz modem PCM  ──Resampler(8000, dev_rate)──▶  device PCM
 *   RX:  device PCM       ──Resampler(dev_rate, 8000)──▶  8 kHz pipeline PCM
 *
 * The ratio is reduced to L/M (interpolate by L, decimate by M) and realised
 * with a single windowed-sinc lowpass prototype designed at the L·in_rate
 * intermediate rate, evaluated polyphase so the per-output cost stays low.
 *
 * Streaming: process() may be called repeatedly with arbitrary block sizes;
 * filter history is carried across calls.  L == M == 1 is a zero-copy
 * passthrough (e.g. when the device already runs at 8 kHz).
 */

#pragma once
#include <cstdint>
#include <vector>
#include <cmath>

namespace ale {

class Resampler {
public:
    Resampler(uint32_t in_rate, uint32_t out_rate) {
        const uint32_t g = gcd(in_rate, out_rate);
        L_ = static_cast<int>(out_rate / g);   // interpolation factor
        M_ = static_cast<int>(in_rate  / g);   // decimation factor

        if (L_ == 1 && M_ == 1) return;        // passthrough — no filter needed

        design_prototype(in_rate, out_rate);

        // History must cover the oldest input sample any pending output needs:
        //   oldest = base - (K-1),  base >= in_count - M  ⇒  span K + M.
        hist_size_ = K_ + M_ + 8;
        hist_.assign(static_cast<size_t>(hist_size_), 0.0f);
    }

    bool is_passthrough() const { return L_ == 1 && M_ == 1; }

    /** Resample one block; results are appended to \p out. */
    void process(const int16_t* in, size_t n, std::vector<int16_t>& out) {
        if (is_passthrough()) {
            out.insert(out.end(), in, in + n);
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            hist_[static_cast<size_t>(in_count_ % hist_size_)] =
                static_cast<float>(in[i]);
            ++in_count_;

            // Emit every output whose newest required input sample (base) has
            // now arrived.  base lags in_count by at most M-1, so history holds it.
            for (;;) {
                const long long m    = out_count_ * static_cast<long long>(M_);
                const long long base = m / L_;
                if (base > in_count_ - 1) break;

                const int phase = static_cast<int>(m % L_);
                // Double-precision accumulation prevents catastrophic cancellation
                // in the FIR sum and avoids float rounding near full-scale.
                double acc = 0.0;
                for (int j = 0; j < K_; ++j) {
                    const long long idx = base - j;
                    if (idx < 0) break;                       // zero-pad startup
                    acc += static_cast<double>(phase_[static_cast<size_t>(phase) * K_ + j])
                         * static_cast<double>(hist_[static_cast<size_t>(idx % hist_size_)]);
                }
                acc *= static_cast<double>(L_);               // zero-stuffing gain

                long v = std::lround(acc);
                if (v >  32767) v =  32767;
                if (v < -32768) v = -32768;
                out.push_back(static_cast<int16_t>(v));
                ++out_count_;
            }
        }
    }

private:
    static uint32_t gcd(uint32_t a, uint32_t b) {
        while (b) { uint32_t t = a % b; a = b; b = t; }
        return a ? a : 1;
    }

    // Design the windowed-sinc lowpass prototype and split it into L polyphase
    // sub-filters of K taps each (phase-major layout: phase_[p*K + j]).
    void design_prototype(uint32_t in_rate, uint32_t out_rate) {
        constexpr int      Q        = 64;     // taps per polyphase phase — 64 gives
                                              // ~41 Hz transition BW at 8↔48 kHz and
                                              // ~74 dB stopband (Blackman window).
        constexpr int      MAX_TAPS = 8192;   // cap for extreme (non-integer) ratios

        const int max_lm = (L_ > M_) ? L_ : M_;
        int n_taps = Q * max_lm;
        if (n_taps > MAX_TAPS) n_taps = MAX_TAPS;
        n_taps -= n_taps % L_;                 // make a whole number of phases
        if (n_taps < L_) n_taps = L_;
        K_ = n_taps / L_;                      // taps per phase

        // Cutoff at the lower Nyquist, normalised to the L·in_rate work rate.
        const double f_low = (in_rate < out_rate ? in_rate : out_rate) / 2.0;
        const double fc    = f_low / (static_cast<double>(L_) * in_rate);

        std::vector<double> h(static_cast<size_t>(n_taps));
        const double center = (n_taps - 1) / 2.0;
        const double PI      = 3.14159265358979323846;
        double sum = 0.0;
        for (int k = 0; k < n_taps; ++k) {
            const double x = k - center;
            // Ideal lowpass impulse response: 2·fc·sinc(2·fc·x)
            const double s = (x == 0.0) ? 1.0
                                        : std::sin(2.0 * PI * fc * x) / (2.0 * PI * fc * x);
            double v = 2.0 * fc * s;
            // Blackman window
            const double w = 0.42
                           - 0.5  * std::cos(2.0 * PI * k / (n_taps - 1))
                           + 0.08 * std::cos(4.0 * PI * k / (n_taps - 1));
            v *= w;
            h[static_cast<size_t>(k)] = v;
            sum += v;
        }
        for (auto& v : h) v /= sum;            // unity DC gain

        // Polyphase split: phase p, tap j  ⇒  prototype index p + j·L.
        phase_.assign(static_cast<size_t>(L_) * K_, 0.0f);
        for (int p = 0; p < L_; ++p)
            for (int j = 0; j < K_; ++j) {
                const int idx = p + j * L_;
                if (idx < n_taps)
                    phase_[static_cast<size_t>(p) * K_ + j] =
                        static_cast<float>(h[static_cast<size_t>(idx)]);
            }
    }

    int L_ = 1, M_ = 1;            // reduced interpolation / decimation factors
    int K_ = 0;                    // taps per polyphase phase
    std::vector<float> phase_;     // L_ × K_ polyphase coefficients (phase-major)

    std::vector<float> hist_;      // ring buffer of recent input samples
    long long hist_size_ = 1;
    long long in_count_   = 0;     // absolute count of input samples consumed
    long long out_count_  = 0;     // absolute count of output samples produced
};

} // namespace ale
