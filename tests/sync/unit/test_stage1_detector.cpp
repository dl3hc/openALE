/**
 * \file tests/sync/unit/test_stage1_detector.cpp
 * \brief Unit test for the §A.5.3.3 stage-1 level-invariant per-symbol-triple +
 *        tone-diversity scanner detector.
 *
 * The stimulus is REAL continuous-phase 8-FSK (a new random tone every 8 ms symbol) at
 * a controlled SINAD and frequency offset — NOT a continuous single tone, which real
 * ALE never produces and which is why the previous same-tone-triple regression passed
 * a green test.  Properties checked:
 *   [1] Real 8-FSK  → ale_energy_cb_ fires within the usable window (dwell − hop-guard)
 *       across SINAD and frequency offset.
 *   [2] White noise → does NOT fire.
 *   [3] Steady carrier (single tone) → does NOT fire (the gr-ale > 2-distinct-tone guard;
 *       this is the case the old detector and a naive ALELite port would false-stop on).
 *   [4] mark_channel_hop() resets the detector — fires again on a fresh visit, and a
 *       noise visit after a hop stays silent.
 *
 * Timing: constants are sim-locked (probe6). With HOP_GUARD_SAMPLES = 64 (8 ms) the
 * detector fires ~30 ms into the signal, so a 200 ms (1600-sample) feed has ample margin.
 */

#include "Modem/ale2g_modem.h"
#include "FSK/ale_waveform.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

using namespace ale;
using namespace ale::ALE2GModem;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
    else       { std::fprintf(stdout, "PASS: %s\n", msg); }
}

constexpr uint32_t FS         = SAMPLE_RATE_HZ;      // 8000
constexpr uint32_t SPS        = SAMPLES_PER_SYMBOL;  // 64
constexpr double   AMPLITUDE  = 8000.0;

// Continuous-phase 8-FSK: a new random tone every symbol, plus Gaussian noise sized for
// the target SINAD, plus a fixed whole-signal frequency offset (models radio mistuning).
std::vector<int16_t> make_8fsk(uint32_t n_symbols, double sinad_db, double offset_hz,
                               std::mt19937& rng)
{
    const double sig_pow = AMPLITUDE * AMPLITUDE / 2.0;
    const double sigma   = std::sqrt(sig_pow / std::pow(10.0, sinad_db / 10.0));
    std::uniform_int_distribution<int> tone(0, static_cast<int>(NUM_TONES) - 1);
    std::normal_distribution<double>   gauss(0.0, sigma);

    std::vector<int16_t> out;
    out.reserve(n_symbols * SPS);
    double phase = 0.0;
    for (uint32_t s = 0; s < n_symbols; ++s) {
        const double f    = static_cast<double>(TONE_FREQS_HZ[tone(rng)]) + offset_hz;
        const double dphi = 2.0 * M_PI * f / FS;
        for (uint32_t k = 0; k < SPS; ++k) {
            const double v = AMPLITUDE * std::sin(phase) + gauss(rng);
            phase += dphi;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            const int iv = static_cast<int>(std::lround(v));
            out.push_back(static_cast<int16_t>(std::max(-32768, std::min(32767, iv))));
        }
    }
    return out;
}

std::vector<int16_t> make_noise(uint32_t n_samples, double sigma, std::mt19937& rng)
{
    std::normal_distribution<double> gauss(0.0, sigma);
    std::vector<int16_t> out(n_samples);
    for (uint32_t i = 0; i < n_samples; ++i) {
        const int iv = static_cast<int>(std::lround(gauss(rng)));
        out[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, iv)));
    }
    return out;
}

// Steady single-tone carrier (a heterodyne) — tonal but only ONE tone.
std::vector<int16_t> make_carrier(uint32_t n_samples, double sigma, std::mt19937& rng)
{
    std::normal_distribution<double> gauss(0.0, sigma);
    std::vector<int16_t> out(n_samples);
    double phase = 0.0;
    const double dphi = 2.0 * M_PI * static_cast<double>(TONE_FREQS_HZ[3]) / FS;
    for (uint32_t i = 0; i < n_samples; ++i) {
        const double v = AMPLITUDE * std::sin(phase) + gauss(rng);
        phase += dphi;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        const int iv = static_cast<int>(std::lround(v));
        out[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, iv)));
    }
    return out;
}

// [1] Real 8-FSK fires across SINAD × offset within the usable window.
bool test_8fsk_fires()
{
    std::printf("\n[Stage1-1] Real 8-FSK fires within the usable window\n");
    std::mt19937 rng(12345);
    const double sinads[]  = {15, 10, 6, 4};
    const double offsets[] = {0, 50};           // realistic HF mistuning
    const uint32_t USABLE  = 1600;              // 200ms feed; fire expected ~30-40ms
    bool all_ok = true;

    for (double off : offsets) {
        for (double sn : sinads) {
            Demodulator dem;
            bool fired = false;
            dem.set_ale_energy_callback([&]() { fired = true; });
            dem.mark_channel_hop();
            const auto sig = make_8fsk(USABLE / SPS, sn, off, rng);

            int fire_at = -1;
            for (uint32_t i = 0; i < sig.size() && !fired; ++i) {
                const int16_t s = sig[i];
                dem.push_samples(&s, 1);
                if (fired) fire_at = static_cast<int>(i + 1);
            }
            const bool ok = fired && fire_at >= 0 && static_cast<uint32_t>(fire_at) <= USABLE;
            std::printf("  SINAD %4.0f dB  offset %3.0f Hz : %s (%.1f ms)\n",
                        sn, off, ok ? "fired" : "MISS",
                        fire_at > 0 ? fire_at / 8.0 : 0.0);
            all_ok = all_ok && ok;
        }
    }
    check(all_ok, "Stage1-1: 8-FSK fires within the usable window across SINAD/offset");
    return all_ok;
}

// [2] White noise does not fire.
bool test_noise_silent()
{
    std::printf("\n[Stage1-2] White noise does not fire\n");
    std::mt19937 rng(777);
    bool fired = false;
    Demodulator dem;
    dem.set_ale_energy_callback([&]() { fired = true; });
    dem.mark_channel_hop();
    const auto noise = make_noise(1600, 2500.0, rng);
    dem.push_samples(noise.data(), static_cast<uint32_t>(noise.size()));
    std::printf("  fired=%s\n", fired ? "yes (UNEXPECTED)" : "no (correct)");
    check(!fired, "Stage1-2: white noise does NOT fire");
    return !fired;
}

// [3] Steady carrier does not fire (tone-diversity guard).
bool test_carrier_silent()
{
    std::printf("\n[Stage1-3] Steady carrier does not fire (> 2 distinct tones guard)\n");
    std::mt19937 rng(555);
    bool fired = false;
    Demodulator dem;
    dem.set_ale_energy_callback([&]() { fired = true; });
    dem.mark_channel_hop();
    const auto carrier = make_carrier(1600, 800.0, rng);
    dem.push_samples(carrier.data(), static_cast<uint32_t>(carrier.size()));
    std::printf("  fired=%s\n", fired ? "yes (UNEXPECTED — camps on a het)" : "no (correct)");
    check(!fired, "Stage1-3: steady carrier does NOT fire");
    return !fired;
}

// [4] mark_channel_hop() resets — fires per visit, silent on a noise visit.
bool test_hop_reset()
{
    std::printf("\n[Stage1-4] mark_channel_hop resets detector state\n");
    std::mt19937 rng(2024);
    int fire_count = 0;
    Demodulator dem;
    dem.set_ale_energy_callback([&]() { ++fire_count; });

    // Visit 1: 8-FSK → fires once.
    dem.mark_channel_hop();
    {
        const auto sig = make_8fsk(1600 / SPS, 10, 0, rng);
        dem.push_samples(sig.data(), static_cast<uint32_t>(sig.size()));
    }
    const int v1 = fire_count;

    // Hop to a noise channel → must stay silent.
    fire_count = 0;
    dem.mark_channel_hop();
    {
        const auto noise = make_noise(1600, 2500.0, rng);
        dem.push_samples(noise.data(), static_cast<uint32_t>(noise.size()));
    }
    const int vn = fire_count;

    // Hop back to 8-FSK → fires again.
    fire_count = 0;
    dem.mark_channel_hop();
    {
        const auto sig = make_8fsk(1600 / SPS, 10, 0, rng);
        dem.push_samples(sig.data(), static_cast<uint32_t>(sig.size()));
    }
    const int v2 = fire_count;

    std::printf("  visit1=%d  noise-visit=%d  visit2=%d\n", v1, vn, v2);
    const bool ok = (v1 == 1) && (vn == 0) && (v2 == 1);
    check(v1 == 1, "Stage1-4: fires exactly once on first 8-FSK visit");
    check(vn == 0, "Stage1-4: noise visit after hop fires zero times");
    check(v2 == 1, "Stage1-4: fires exactly once on second 8-FSK visit after hop");
    return ok;
}

// Noise sigma that yields a given SINAD for the 8-FSK amplitude.
double sigma_for_sinad(double sinad_db)
{
    const double sig_pow = AMPLITUDE * AMPLITUDE / 2.0;
    return std::sqrt(sig_pow / std::pow(10.0, sinad_db / 10.0));
}

// Run one squelch scenario: fresh demod, squelch on at `margin_db`, train the global
// floor on noise, then (after a hop, so the floor persists) feed a fixed 10 dB signal.
// Returns whether the detector fired.  floor_out reports the trained floor.
bool squelch_scenario(float margin_db, double& floor_out, std::mt19937& rng)
{
    const double NOISE_SIGMA = sigma_for_sinad(10.0);
    Demodulator dem;
    bool fired = false;
    dem.set_ale_energy_callback([&]() { fired = true; });
    dem.set_scan_squelch_enabled(true);
    dem.set_scan_detect_margin_db(margin_db);

    dem.mark_channel_hop();
    { const auto n = make_noise(4000, NOISE_SIGMA, rng); dem.push_samples(n.data(), 4000); }
    floor_out = dem.scan_floor_db();

    dem.mark_channel_hop();   // floor persists across the hop; detection state resets
    { const auto s = make_8fsk(1600 / SPS, 10.0, 0, rng); dem.push_samples(s.data(), 1600); }
    return fired;
}

// [5] Operator squelch (PR2): default OFF is a no-op; when ON the operator margin knob
// controls sensitivity on the SAME signal (low margin passes, high margin gates), backed
// by a global floor trained from noise that persists across mark_channel_hop().
bool test_operator_squelch()
{
    std::printf("\n[Stage1-5] Operator squelch margin controls sensitivity\n");
    std::mt19937 rng(31337);

    // Control: squelch OFF → a 10 dB signal fires (unchanged PR1 behaviour).
    bool off_fired = false;
    {
        Demodulator dem;
        dem.set_ale_energy_callback([&]() { off_fired = true; });
        dem.mark_channel_hop();
        const auto sig = make_8fsk(1600 / SPS, 10.0, 0, rng);
        dem.push_samples(sig.data(), static_cast<uint32_t>(sig.size()));
    }
    check(off_fired, "Stage1-5: squelch OFF → signal fires (PR1 unchanged)");

    double floor_lo = 0.0, floor_hi = 0.0;
    const bool low_fires  = squelch_scenario(0.0f,  floor_lo, rng);   // permissive
    const bool high_gates = squelch_scenario(30.0f, floor_hi, rng);   // strict
    std::printf("  floor(dB)=%.1f  margin0→%s  margin30→%s\n",
                floor_lo, low_fires ? "fired" : "gated", high_gates ? "fired" : "gated");

    check(floor_lo > 0.0, "Stage1-5: global floor trained from noise (scan_floor_db > 0)");
    check(low_fires,  "Stage1-5: squelch ON @0 dB margin → signal fires");
    check(!high_gates, "Stage1-5: squelch ON @30 dB margin → same signal is gated");

    return off_fired && floor_lo > 0.0 && low_fires && !high_gates;
}

} // namespace

int main()
{
    std::printf("=== §A.5.3.3 Stage-1 Tone-Diversity Detector ===\n");
    test_8fsk_fires();
    test_noise_silent();
    test_carrier_silent();
    test_hop_reset();
    test_operator_squelch();

    if (g_failures == 0) {
        std::printf("\nAll stage-1 detector tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d test(s) FAILED.\n", g_failures);
    return 1;
}
