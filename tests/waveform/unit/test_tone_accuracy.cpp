/**
 * \file tests/waveform/unit/test_tone_accuracy.cpp
 * \brief AC-WAVEFORM-004-001: FFT-based frequency accuracy ≤ ±1 Hz for all 8 FSK tones
 *
 * Generates 1 second of each tone via ToneGenerator, then measures the peak
 * frequency via DFT to verify deviation ≤ ±1 Hz from the nominal per SYMBOL_TO_FREQ.
 */

#ifdef _MSC_VER
#pragma warning(disable: 4127)
#endif

#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace ale {

// 1 second at 8000 Hz → 1 Hz DFT bin resolution
static constexpr uint32_t ACCURACY_TEST_N   = SAMPLE_RATE_HZ;
static constexpr double   MAX_FREQ_ERROR_HZ = 1.0;

// DFT magnitude squared at frequency f_hz.
// Uses incremental phase to avoid redundant trig calls (O(N) per query).
static double dft_mag2(const std::vector<int16_t>& x, double f_hz) {
    const double omega = 2.0 * M_PI * f_hz / static_cast<double>(SAMPLE_RATE_HZ);
    const double cos_w = std::cos(omega);
    const double sin_w = std::sin(omega);
    double cos_n = 1.0, sin_n = 0.0;  // exp(-j*omega*n) at n=0
    double re = 0.0, im = 0.0;

    for (uint32_t n = 0; n < static_cast<uint32_t>(x.size()); ++n) {
        re += x[n] * cos_n;
        im -= x[n] * sin_n;
        // advance by omega: (cos_n + j*sin_n) * (cos_w + j*sin_w)
        const double new_cos = cos_n * cos_w - sin_n * sin_w;
        const double new_sin = cos_n * sin_w + sin_n * cos_w;
        cos_n = new_cos;
        sin_n = new_sin;
    }
    return re * re + im * im;
}

// Scan [center-span, center+span] at step resolution; return frequency with peak magnitude.
static double scan_peak(const std::vector<int16_t>& x,
                        double center, double span, double step) {
    double best_mag = -1.0, best_f = center;
    for (double f = center - span; f <= center + span + 1e-9; f += step) {
        const double m = dft_mag2(x, f);
        if (m > best_mag) { best_mag = m; best_f = f; }
    }
    return best_f;
}

// ============================================================================
// Test AC-WAVEFORM-004-001
// ============================================================================

bool test_tone_frequency_accuracy_ac_waveform_004_001() {
    std::cout << "\n[TEST AC-WAVEFORM-004-001] Frequency accuracy <= +/-1 Hz (all 8 FSK tones)\n";
    std::cout << "=========================================================================\n";

    bool all_pass = true;

    for (uint8_t sym = 0; sym < static_cast<uint8_t>(NUM_TONES); ++sym) {
        const double nominal_hz = static_cast<double>(SYMBOL_TO_FREQ[sym]);

        // Arrange: 1 second of continuous tone
        ToneGenerator gen;
        std::vector<int16_t> samples(ACCURACY_TEST_N);
        gen.generate_tone(sym, ACCURACY_TEST_N, samples.data(), 0.7f);

        // Act: coarse scan ±5 Hz at 1 Hz, then refine ±1.5 Hz at 0.1 Hz
        const double coarse   = scan_peak(samples, nominal_hz, 5.0, 1.0);
        const double measured = scan_peak(samples, coarse,     1.5, 0.1);

        const double deviation = measured - nominal_hz;
        const bool   pass      = std::abs(deviation) <= MAX_FREQ_ERROR_HZ;

        std::cout << std::fixed << std::setprecision(2)
                  << "  sym=" << static_cast<int>(sym)
                  << "  nominal=" << std::setw(7) << nominal_hz << " Hz"
                  << "  measured=" << std::setw(9) << measured  << " Hz"
                  << "  error=" << std::setw(6) << deviation << " Hz"
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";

        if (!pass) all_pass = false;
    }

    if (all_pass)
        std::cout << "PASS: AC-WAVEFORM-004-001 — all 8 tones within +/-1 Hz\n";
    else
        std::cout << "FAIL: AC-WAVEFORM-004-001 — one or more tones exceed +/-1 Hz\n";

    return all_pass;
}

// ============================================================================
// Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Tone Frequency Accuracy Tests (AC-WAVEFORM-004-001)\n";
    std::cout << "=========================================================\n";

    int pass = 0, fail = 0;
    if (test_tone_frequency_accuracy_ac_waveform_004_001()) ++pass; else ++fail;

    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Results: Passed=" << pass << "  Failed=" << fail << "\n";
    std::cout << "=========================================================\n\n";

    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
