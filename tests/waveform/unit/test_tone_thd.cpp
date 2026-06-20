/**
 * \file tests/waveform/unit/test_tone_thd.cpp
 * \brief THD and in-band foldback tests for the 8-FSK tone generator.
 *
 * AC-WAVEFORM-004-003:
 *   TX tone THD < 1 % (−40 dBc), and no in-band foldback spur above −55 dBc,
 *   measured both at 8 kHz (raw generator output) and after 8→48 kHz resampling
 *   (the signal path seen by the reference waterfall application).
 *
 * Background: at 8 kHz sample rate (Nyquist 4 kHz), a signal-correlated
 * quantization error folds harmonics of 2500 Hz back in-band:
 *   2nd harmonic 5000 Hz → alias at 3000 Hz
 *   3rd harmonic 7500 Hz → alias at  500 Hz
 * TPDF dither decorrelates the error, dissolving discrete spurs into a flat
 * noise floor well below the −55 dBc threshold.
 */

#ifdef _MSC_VER
#pragma warning(disable: 4127)
#endif

#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include "App/resampler.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace ale {

// ---------------------------------------------------------------------------
// DFT helpers (same incremental algorithm as test_tone_accuracy.cpp)
// ---------------------------------------------------------------------------

static double dft_mag2(const std::vector<int16_t>& x, double f_hz, uint32_t fs) {
    const double omega = 2.0 * M_PI * f_hz / static_cast<double>(fs);
    const double cos_w = std::cos(omega);
    const double sin_w = std::sin(omega);
    double cos_n = 1.0, sin_n = 0.0;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
        re += x[n] * cos_n;
        im -= x[n] * sin_n;
        const double nc = cos_n * cos_w - sin_n * sin_w;
        const double ns = cos_n * sin_w + sin_n * cos_w;
        cos_n = nc;
        sin_n = ns;
    }
    return re * re + im * im;
}

// Return power ratio of spur to fundamental (linear, not dB).
// Returns 0 if fundamental magnitude squared is 0.
static double spur_ratio(double spur_mag2, double fund_mag2) {
    return (fund_mag2 > 0.0) ? std::sqrt(spur_mag2 / fund_mag2) : 0.0;
}

static double to_dBc(double ratio) {
    return (ratio > 0.0) ? 20.0 * std::log10(ratio) : -200.0;
}

// ---------------------------------------------------------------------------
// Build alias list: given fundamental f and Nyquist limit, return the
// set of in-band aliases of k·f for k = 2, 3, …, up to kmax.
// Alias(k·f) = fold around Nyquist until in [0, nyquist].
// ---------------------------------------------------------------------------
static std::vector<double> inband_aliases(double fund_hz, uint32_t fs, int kmax = 6) {
    const double nyquist = fs / 2.0;
    std::vector<double> result;
    for (int k = 2; k <= kmax; ++k) {
        double h = k * fund_hz;
        // Fold into [0, nyquist] by reflecting around each Nyquist multiple.
        while (h > nyquist) {
            h = 2.0 * nyquist - h;
            if (h < 0.0) h = -h;
        }
        // Only include if not coincident with the fundamental (within 1 Hz).
        if (std::abs(h - fund_hz) > 1.0 && h > 1.0)
            result.push_back(h);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Part A: raw 8 kHz generator output
// ---------------------------------------------------------------------------

struct ThdResult {
    uint8_t  symbol;
    double   fund_hz;
    double   thd_pct;        // sqrt(sum of harmonic power) / fund, %
    double   worst_spur_dBc; // worst in-band alias (dBc)
    double   worst_spur_hz;
    bool     pass;
};

static constexpr double THD_LIMIT_PCT   = 1.0;   // < 1 % total
static constexpr double SPUR_LIMIT_dBc  = -55.0; // no discrete alias above this
static constexpr uint32_t TEST_SAMPLES_8K = SAMPLE_RATE_HZ; // 1 s at 8 kHz

static ThdResult measure_thd_8k(uint8_t sym) {
    ThdResult r{};
    r.symbol  = sym;
    r.fund_hz = static_cast<double>(SYMBOL_TO_FREQ[sym]);

    ToneGenerator gen;
    gen.reset();
    std::vector<int16_t> buf(TEST_SAMPLES_8K);
    gen.generate_tone(sym, TEST_SAMPLES_8K, buf.data(), TX_AMPLITUDE);

    const double fund_mag2 = dft_mag2(buf, r.fund_hz, SAMPLE_RATE_HZ);

    // THD: sum harmonic power for k=2..6, including their in-band aliases.
    double harm_power = 0.0;
    r.worst_spur_dBc = -200.0;
    r.worst_spur_hz  = 0.0;

    const auto aliases = inband_aliases(r.fund_hz, SAMPLE_RATE_HZ, 6);
    for (double alias_hz : aliases) {
        const double m2 = dft_mag2(buf, alias_hz, SAMPLE_RATE_HZ);
        harm_power += m2;
        const double dbc = to_dBc(spur_ratio(m2, fund_mag2));
        if (dbc > r.worst_spur_dBc) {
            r.worst_spur_dBc = dbc;
            r.worst_spur_hz  = alias_hz;
        }
    }
    r.thd_pct = 100.0 * std::sqrt(harm_power / fund_mag2);
    r.pass = (r.thd_pct < THD_LIMIT_PCT) && (r.worst_spur_dBc < SPUR_LIMIT_dBc);
    return r;
}

bool test_thd_8k_ac_waveform_004_003a() {
    std::cout << "\n[TEST AC-WAVEFORM-004-003-A] THD + foldback @ 8 kHz\n";
    std::cout << "  Limits: THD < " << THD_LIMIT_PCT << " %,  worst alias < "
              << SPUR_LIMIT_dBc << " dBc\n";
    std::cout << "============================================================\n";

    bool all_pass = true;
    for (uint8_t sym = 0; sym < static_cast<uint8_t>(NUM_TONES); ++sym) {
        const ThdResult r = measure_thd_8k(sym);
        std::cout << std::fixed << std::setprecision(2)
                  << "  sym=" << static_cast<int>(r.symbol)
                  << "  f=" << std::setw(6) << r.fund_hz << " Hz"
                  << "  THD=" << std::setw(6) << r.thd_pct << " %"
                  << "  worstAlias=" << std::setw(7) << r.worst_spur_dBc << " dBc"
                  << " @" << std::setw(6) << r.worst_spur_hz << " Hz"
                  << "  " << (r.pass ? "PASS" : "FAIL") << "\n";
        if (!r.pass) all_pass = false;
    }

    if (all_pass)
        std::cout << "PASS: AC-WAVEFORM-004-003-A\n";
    else
        std::cout << "FAIL: AC-WAVEFORM-004-003-A\n";
    return all_pass;
}

// ---------------------------------------------------------------------------
// Part B: full TX path — 8 kHz → Resampler → 48 kHz, then DFT @ 48 kHz
// ---------------------------------------------------------------------------

static constexpr uint32_t TX_DEVICE_RATE = 48000;

static ThdResult measure_thd_48k(uint8_t sym) {
    ThdResult r{};
    r.symbol  = sym;
    r.fund_hz = static_cast<double>(SYMBOL_TO_FREQ[sym]);

    // Generate 1 s at 8 kHz
    ToneGenerator gen;
    gen.reset();
    std::vector<int16_t> buf8k(TEST_SAMPLES_8K);
    gen.generate_tone(sym, TEST_SAMPLES_8K, buf8k.data(), TX_AMPLITUDE);

    // Resample to 48 kHz
    Resampler rs(SAMPLE_RATE_HZ, TX_DEVICE_RATE);
    std::vector<int16_t> buf48k;
    buf48k.reserve(TX_DEVICE_RATE);
    rs.process(buf8k.data(), buf8k.size(), buf48k);

    const double fund_mag2 = dft_mag2(buf48k, r.fund_hz, TX_DEVICE_RATE);

    // At 48 kHz Nyquist = 24 kHz; harmonics of max 2500 Hz well outside band,
    // but may appear as images near resampler passband edge or as residual
    // in-band aliases if the resampler stopband doesn't fully reject them.
    // We check the same in-band set (fold relative to 8 kHz Nyquist = 4 kHz)
    // since these are the frequencies that would reach the radio's passband.
    double harm_power = 0.0;
    r.worst_spur_dBc = -200.0;
    r.worst_spur_hz  = 0.0;

    // Check at the same alias frequencies that were relevant at 8 kHz.
    const auto aliases = inband_aliases(r.fund_hz, SAMPLE_RATE_HZ, 6);
    for (double alias_hz : aliases) {
        // Evaluate DFT of the 48-kHz signal at the alias frequency.
        const double m2  = dft_mag2(buf48k, alias_hz, TX_DEVICE_RATE);
        harm_power += m2;
        const double dbc = to_dBc(spur_ratio(m2, fund_mag2));
        if (dbc > r.worst_spur_dBc) {
            r.worst_spur_dBc = dbc;
            r.worst_spur_hz  = alias_hz;
        }
    }
    r.thd_pct = 100.0 * std::sqrt(harm_power / fund_mag2);
    r.pass = (r.thd_pct < THD_LIMIT_PCT) && (r.worst_spur_dBc < SPUR_LIMIT_dBc);
    return r;
}

bool test_thd_48k_ac_waveform_004_003b() {
    std::cout << "\n[TEST AC-WAVEFORM-004-003-B] THD + foldback @ 48 kHz (full TX path)\n";
    std::cout << "  Limits: THD < " << THD_LIMIT_PCT << " %,  worst alias < "
              << SPUR_LIMIT_dBc << " dBc\n";
    std::cout << "============================================================\n";

    bool all_pass = true;
    for (uint8_t sym = 0; sym < static_cast<uint8_t>(NUM_TONES); ++sym) {
        const ThdResult r = measure_thd_48k(sym);
        std::cout << std::fixed << std::setprecision(2)
                  << "  sym=" << static_cast<int>(r.symbol)
                  << "  f=" << std::setw(6) << r.fund_hz << " Hz"
                  << "  THD=" << std::setw(6) << r.thd_pct << " %"
                  << "  worstAlias=" << std::setw(7) << r.worst_spur_dBc << " dBc"
                  << " @" << std::setw(6) << r.worst_spur_hz << " Hz"
                  << "  " << (r.pass ? "PASS" : "FAIL") << "\n";
        if (!r.pass) all_pass = false;
    }

    if (all_pass)
        std::cout << "PASS: AC-WAVEFORM-004-003-B\n";
    else
        std::cout << "FAIL: AC-WAVEFORM-004-003-B\n";
    return all_pass;
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int run_all_tests() {
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Tone THD / Foldback Tests (AC-WAVEFORM-004-003)\n";
    std::cout << "=========================================================\n";

    int pass = 0, fail = 0;
    if (test_thd_8k_ac_waveform_004_003a())  ++pass; else ++fail;
    if (test_thd_48k_ac_waveform_004_003b()) ++pass; else ++fail;

    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  Results: Passed=" << pass << "  Failed=" << fail << "\n";
    std::cout << "=========================================================\n\n";
    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
