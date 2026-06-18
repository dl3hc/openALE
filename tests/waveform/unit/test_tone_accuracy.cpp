/**
 * \file tests/waveform/unit/test_tone_accuracy.cpp
 * \brief Accuracy tests for the 8-FSK tone generator
 *
 * AC-WAVEFORM-004-001: FFT-based frequency accuracy ≤ ±1 Hz for all 8 tones.
 * AC-WAVEFORM-004-002: Symbol/bit/word timing accuracy ≤ 10 ppm.
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
// Test AC-WAVEFORM-004-002: Timing accuracy ≤ 10 ppm (symbol / bit / word rate)
// ============================================================================

bool test_timing_accuracy_ac_waveform_004_002() {
    std::cout << "\n[TEST AC-WAVEFORM-004-002] Timing accuracy <= 10 ppm (symbol/bit/word rate)\n";
    std::cout << "===========================================================================\n";

    bool all_pass = true;
    constexpr double MAX_PPM = 10.0;

    // 1. SAMPLES_PER_SYMBOL is an exact integer (SAMPLE_RATE_HZ % SYMBOL_RATE_BAUD == 0)
    {
        const uint32_t remainder = SAMPLE_RATE_HZ % SYMBOL_RATE_BAUD;
        const double   ppm       = (remainder == 0) ? 0.0
            : static_cast<double>(remainder) / SYMBOL_RATE_BAUD * 1e6;
        const bool pass = ppm <= MAX_PPM;
        std::cout << "  Symbol period: " << SAMPLE_RATE_HZ << "/" << SYMBOL_RATE_BAUD
                  << " = " << SAMPLES_PER_SYMBOL << " samples/symbol  remainder=" << remainder
                  << "  error=" << std::fixed << std::setprecision(3) << ppm << " ppm"
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) all_pass = false;
    }

    // 2. NCO phase-increment exactness for all 8 tones
    //    All ALE frequencies are multiples of 250 Hz → (freq<<32)/8000 is always exact.
    {
        bool inc_pass = true;
        for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
            const uint32_t freq_hz      = TONE_FREQS_HZ[rank];
            const uint64_t numerator    = static_cast<uint64_t>(freq_hz) << 32;
            const uint64_t remainder_q32 = numerator % SAMPLE_RATE_HZ;
            const uint64_t exact_inc    = numerator / SAMPLE_RATE_HZ;
            const double   ppm          = (exact_inc > 0)
                ? static_cast<double>(remainder_q32) / static_cast<double>(exact_inc) * 1e6
                : 0.0;
            const bool tone_pass = ppm <= MAX_PPM;
            std::cout << "  NCO sym=" << static_cast<int>(FREQ_TO_SYMBOL[rank])
                      << "  freq=" << std::setw(4) << freq_hz << " Hz"
                      << "  inc=" << exact_inc
                      << "  Q32_rem=" << remainder_q32
                      << "  error=" << std::fixed << std::setprecision(3) << ppm << " ppm"
                      << "  " << (tone_pass ? "PASS" : "FAIL") << "\n";
            if (!tone_pass) inc_pass = false;
        }
        if (!inc_pass) all_pass = false;
    }

    // 3. Symbol timing: generate_symbols() returns exactly expected sample count
    {
        constexpr uint32_t TEST_WORDS   = 10;
        constexpr uint32_t TEST_SYMS    = TEST_WORDS * SYMBOLS_PER_WORD;  // 490
        constexpr uint64_t EXPECTED     = static_cast<uint64_t>(TEST_SYMS) * SAMPLES_PER_SYMBOL;

        std::vector<uint8_t> syms(TEST_SYMS);
        for (uint32_t i = 0; i < TEST_SYMS; ++i)
            syms[i] = static_cast<uint8_t>(i % NUM_TONES);

        std::vector<int16_t> buf(EXPECTED);
        ToneGenerator gen;
        const uint64_t actual = gen.generate_symbols(syms.data(), TEST_SYMS, buf.data(), 0.7f);
        const double   ppm    = std::abs(static_cast<double>(actual) - static_cast<double>(EXPECTED))
                                / static_cast<double>(EXPECTED) * 1e6;
        const bool pass = ppm <= MAX_PPM;
        std::cout << "  Symbol timing: " << TEST_SYMS << " symbols"
                  << "  expected=" << EXPECTED << "  actual=" << actual
                  << "  error=" << std::fixed << std::setprecision(3) << ppm << " ppm"
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) all_pass = false;
    }

    // 4. Word timing: one ALE word = SYMBOLS_PER_WORD × SAMPLES_PER_SYMBOL samples
    {
        constexpr uint64_t EXPECTED = static_cast<uint64_t>(SYMBOLS_PER_WORD) * SAMPLES_PER_SYMBOL;

        std::vector<uint8_t> syms(SYMBOLS_PER_WORD);
        for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i)
            syms[i] = static_cast<uint8_t>(i % NUM_TONES);

        std::vector<int16_t> buf(EXPECTED);
        ToneGenerator gen;
        const uint64_t actual = gen.generate_symbols(syms.data(), SYMBOLS_PER_WORD, buf.data(), 0.7f);
        const double   ppm    = std::abs(static_cast<double>(actual) - static_cast<double>(EXPECTED))
                                / static_cast<double>(EXPECTED) * 1e6;
        const bool pass = ppm <= MAX_PPM;
        std::cout << "  Word timing:   " << SYMBOLS_PER_WORD << " symbols/word"
                  << "  expected=" << EXPECTED << "  actual=" << actual
                  << "  error=" << std::fixed << std::setprecision(3) << ppm << " ppm"
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) all_pass = false;
    }

    // 5. Bit rate: SYMBOL_RATE_BAUD × BITS_PER_SYMBOL must equal exactly 375 bps
    {
        constexpr uint32_t computed   = SYMBOL_RATE_BAUD * BITS_PER_SYMBOL;  // 125 × 3 = 375
        constexpr uint32_t nominal    = 375u;
        const double ppm = (nominal > 0)
            ? std::abs(static_cast<double>(computed) - static_cast<double>(nominal))
              / static_cast<double>(nominal) * 1e6
            : 0.0;
        const bool pass = ppm <= MAX_PPM;
        std::cout << "  Bit rate:      " << SYMBOL_RATE_BAUD << " baud × " << BITS_PER_SYMBOL
                  << " bits/sym = " << computed << " bps  (nominal " << nominal << " bps)"
                  << "  error=" << std::fixed << std::setprecision(3) << ppm << " ppm"
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) all_pass = false;
    }

    if (all_pass)
        std::cout << "PASS: AC-WAVEFORM-004-002 — symbol/bit/word timing all within 10 ppm\n";
    else
        std::cout << "FAIL: AC-WAVEFORM-004-002 — timing accuracy exceeds 10 ppm\n";

    return all_pass;
}

// ============================================================================
// Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Tone Accuracy Tests (AC-WAVEFORM-004-001 / -004-002)\n";
    std::cout << "=========================================================\n";

    int pass = 0, fail = 0;
    if (test_tone_frequency_accuracy_ac_waveform_004_001()) ++pass; else ++fail;
    if (test_timing_accuracy_ac_waveform_004_002())         ++pass; else ++fail;

    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Results: Passed=" << pass << "  Failed=" << fail << "\n";
    std::cout << "=========================================================\n\n";

    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
