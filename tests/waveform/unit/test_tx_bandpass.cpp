/**
 * \file tests/waveform/unit/test_tx_bandpass.cpp
 * \brief Tests for the TX-audio band-pass (include/FSK/tx_bandpass.h).
 *
 * AC-WAVEFORM-004-004:
 *   (A) The 8 ALE tones (750–2500 Hz) pass with ≤ 0.5 dB ripple (decode-safe).
 *   (B) Out-of-band content (≤150 Hz, ≥3300 Hz) is attenuated ≥ 40 dB.
 *   (C) A keyed 8-FSK symbol stream still decodes bit-exact after filtering
 *       (group-delay compensated) — proves MIL-STD-188-141B keying is preserved.
 */

#ifdef _MSC_VER
#pragma warning(disable: 4127)
#endif

#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include "FSK/tx_bandpass.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>

namespace ale {

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

// Arbitrary-frequency sine at 8 kHz (the tone generator only emits the 8 tones).
static std::vector<int16_t> gen_sine(double freq, uint32_t n, double amp) {
    std::vector<int16_t> out(n);
    const double dw = 2.0 * M_PI * freq / SAMPLE_RATE_HZ;
    for (uint32_t i = 0; i < n; ++i)
        out[i] = static_cast<int16_t>(std::lround(std::sin(dw * i) * amp * 32767.0));
    return out;
}

static double rms(const std::vector<int16_t>& x, size_t from, size_t to) {
    double s = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i)
        s += static_cast<double>(x[i]) * x[i];
    const size_t n = (to > from) ? (to - from) : 1;
    return std::sqrt(s / n);
}

// |H(freq)| as a linear gain, measured on the settled part of the output.
static double filter_gain(double freq) {
    const uint32_t N = 8000;                       // 1 s
    auto in = gen_sine(freq, N, 0.5);
    TxBandpass bp;
    std::vector<int16_t> out;
    bp.process(in.data(), in.size(), out);
    const size_t settle = 400;                     // skip FIR startup transient
    return rms(out, settle, out.size()) / rms(in, settle, in.size());
}

bool test_passband_flatness_004_004a() {
    std::cout << "\n[TEST AC-WAVEFORM-004-004-A] Pass-band flatness (8 tones <= 0.5 dB)\n";
    std::cout << "====================================================================\n";
    const double ref = filter_gain(1500.0);
    bool ok = true;
    for (uint32_t f : TONE_FREQS_HZ) {
        const double g_db = 20.0 * std::log10(filter_gain(f) / ref);
        const bool pass = std::abs(g_db) <= 0.5;
        std::cout << std::fixed << std::setprecision(3)
                  << "  " << std::setw(4) << f << " Hz  rel=" << std::setw(7) << g_db
                  << " dB  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) ok = false;
    }
    std::cout << (ok ? "PASS" : "FAIL") << ": AC-WAVEFORM-004-004-A\n";
    return ok;
}

bool test_stopband_004_004b() {
    std::cout << "\n[TEST AC-WAVEFORM-004-004-B] Out-of-band attenuation (>= 40 dB)\n";
    std::cout << "====================================================================\n";
    const double ref = filter_gain(1500.0);
    struct { double f; bool hard; } probes[] = {
        { 100.0, true }, { 150.0, true }, { 300.0, false }, { 500.0, false },
        { 2900.0, false }, { 3000.0, false }, { 3300.0, true }, { 3500.0, true },
    };
    bool ok = true;
    for (auto& p : probes) {
        const double a_db = 20.0 * std::log10(filter_gain(p.f) / ref);
        const bool pass = !p.hard || (a_db <= -40.0);
        std::cout << std::fixed << std::setprecision(2)
                  << "  " << std::setw(5) << p.f << " Hz  att=" << std::setw(8) << a_db << " dB"
                  << (p.hard ? "  [>=40dB]" : "  [info]  ")
                  << "  " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) ok = false;
    }
    std::cout << (ok ? "PASS" : "FAIL") << ": AC-WAVEFORM-004-004-B\n";
    return ok;
}

// ── Goertzel per-symbol detector (same approach as test_roundtrip.cpp) ────────
static float goertzel(const int16_t* s, uint32_t N, float f, float sr) {
    const float w = 2.0f * static_cast<float>(M_PI) * f / sr;
    const float coeff = 2.0f * std::cos(w);
    float q1 = 0.0f, q2 = 0.0f;
    for (uint32_t n = 0; n < N; ++n) { float q0 = coeff*q1 - q2 + s[n]; q2 = q1; q1 = q0; }
    return q1*q1 + q2*q2 - coeff*q1*q2;
}
static uint8_t detect_symbol(const int16_t* chunk) {
    float best = -1.0f; uint8_t rank = 0;
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        float m = goertzel(chunk, SAMPLES_PER_SYMBOL, static_cast<float>(TONE_FREQS_HZ[r]), 8000.0f);
        if (m > best) { best = m; rank = r; }
    }
    return FREQ_TO_SYMBOL[rank];
}

bool test_decode_survives_filter_004_004c() {
    std::cout << "\n[TEST AC-WAVEFORM-004-004-C] Keyed 8-FSK decodes bit-exact after filter\n";
    std::cout << "====================================================================\n";

    constexpr uint32_t NSYM = 300;
    std::vector<uint8_t> tx(NSYM);
    std::mt19937 rng(12345);
    for (auto& s : tx) s = static_cast<uint8_t>(rng() % NUM_TONES);

    // Tone-generate, then append group-delay zeros so the filtered window is complete.
    ToneGenerator gen;
    std::vector<int16_t> pcm(NSYM * SAMPLES_PER_SYMBOL);
    gen.generate_symbols(tx.data(), NSYM, pcm.data(), TX_AMPLITUDE);

    TxBandpass bp;
    const size_t gd = bp.group_delay();
    pcm.insert(pcm.end(), gd, 0);                  // flush tail through the delay
    std::vector<int16_t> filt;
    bp.process(pcm.data(), pcm.size(), filt);

    // Linear-phase FIR ⇒ pure delay: filt[gd + k] aligns with original sample k.
    int errors = 0, checked = 0;
    const uint32_t skip = 2;                        // ignore first symbols (startup)
    for (uint32_t k = skip; k < NSYM; ++k) {
        const size_t off = gd + static_cast<size_t>(k) * SAMPLES_PER_SYMBOL;
        if (off + SAMPLES_PER_SYMBOL > filt.size()) break;
        if (detect_symbol(filt.data() + off) != tx[k]) ++errors;
        ++checked;
    }
    std::cout << "  symbols checked=" << checked << "  errors=" << errors << "\n";
    const bool ok = (errors == 0);
    std::cout << (ok ? "PASS" : "FAIL") << ": AC-WAVEFORM-004-004-C\n";
    return ok;
}

int run_all_tests() {
    std::cout << "\n=========================================================\n";
    std::cout << "  TX Band-pass Tests (AC-WAVEFORM-004-004)\n";
    std::cout << "=========================================================\n";
    int pass = 0, fail = 0;
    if (test_passband_flatness_004_004a())      ++pass; else ++fail;
    if (test_stopband_004_004b())               ++pass; else ++fail;
    if (test_decode_survives_filter_004_004c()) ++pass; else ++fail;
    std::cout << "\n  Results: Passed=" << pass << "  Failed=" << fail << "\n";
    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
