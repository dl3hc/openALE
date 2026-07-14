/**
 * \file tests/sync/unit/test_stage1_detector.cpp
 * \brief Unit test for the ALELite-style triple-agreement §A.5.3.3 stage-1 detector.
 *
 * Tests two properties:
 *   [1] ALE signal  → ale_energy_cb_ fires within 320 samples (40ms) after mark_channel_hop()
 *   [2] White noise → ale_energy_cb_ does NOT fire within 1000 samples after mark_channel_hop()
 *
 * The 320-sample budget gives 10 half-blocks; the detector needs 9 (= 3 fill + 5 consecutive
 * triples) at a minimum = 288 samples. The 32-sample margin is intentional.
 */

#include "Modem/ale2g_modem.h"
#include "FSK/ale_waveform.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

using namespace ale;
using namespace ale::ALE2GModem;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    } else {
        std::fprintf(stdout, "PASS: %s\n", msg);
    }
}

// Generate N samples of a pure sine at freq_hz, 8 kHz sample rate, amplitude A.
std::vector<int16_t> make_sine(float freq_hz, uint32_t n_samples, int16_t amplitude = 8000)
{
    std::vector<int16_t> out(n_samples);
    for (uint32_t i = 0; i < n_samples; ++i) {
        const float phase = 2.0f * static_cast<float>(M_PI) * freq_hz
                            * static_cast<float>(i) / 8000.0f;
        out[i] = static_cast<int16_t>(static_cast<float>(amplitude) * std::sin(phase));
    }
    return out;
}

// Generate N samples of white noise in [-amplitude, +amplitude].
std::vector<int16_t> make_noise(uint32_t n_samples, int16_t amplitude = 8000)
{
    std::vector<int16_t> out(n_samples);
    uint32_t state = 0xDEADBEEFu;
    for (uint32_t i = 0; i < n_samples; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5; // xorshift32
        const int32_t v = static_cast<int32_t>(state & 0x7FFF)
                          - (static_cast<int32_t>(amplitude) / 2);
        out[i] = static_cast<int16_t>(
            std::min(std::max(v, static_cast<int32_t>(-amplitude)),
                     static_cast<int32_t>( amplitude)));
    }
    return out;
}

// [1] Pure ALE FSK tone → stage-1 fires within 320 samples after channel hop.
bool test_ale_tone_fires()
{
    std::printf("\n[Stage1-1] Pure ALE tone fires stage-1 within 40ms\n");

    Demodulator dem;
    bool fired       = false;
    uint32_t fire_at = 0;

    dem.set_ale_energy_callback([&]() {
        fired   = true;
    });

    // Use tone index 0 (lowest ALE frequency — 750 Hz per ale_waveform.h).
    const float freq = static_cast<float>(TONE_FREQS_HZ[0]);

    // Arm the channel-hop guard.
    dem.mark_channel_hop();

    // Feed up to 320 samples, stop as soon as callback fires.
    const auto tone = make_sine(freq, 320);
    for (uint32_t i = 0; i < 320 && !fired; ++i) {
        const int16_t s = tone[i];
        dem.push_samples(&s, 1);
        if (fired) fire_at = i + 1;
    }
    if (fired && fire_at == 0) fire_at = 320; // fired on last sample

    std::printf("  fired=%s  fire_at=%u samples (%.1f ms)\n",
                fired ? "yes" : "no", fire_at,
                static_cast<float>(fire_at) / 8.0f);

    check(fired, "Stage1-1: callback fires on pure ALE tone");
    check(fire_at <= 320, "Stage1-1: fires within 320 samples (40ms)");
    return fired;
}

// [2] White noise → stage-1 does NOT fire within 1000 samples after channel hop.
bool test_noise_does_not_fire()
{
    std::printf("\n[Stage1-2] White noise does not fire stage-1 within 1000 samples\n");

    Demodulator dem;
    bool fired = false;

    dem.set_ale_energy_callback([&]() { fired = true; });
    dem.mark_channel_hop();

    const auto noise = make_noise(1000);
    dem.push_samples(noise.data(), static_cast<uint32_t>(noise.size()));

    std::printf("  fired=%s\n", fired ? "yes (UNEXPECTED)" : "no (correct)");

    check(!fired, "Stage1-2: callback does NOT fire on white noise");
    return !fired;
}

// [3] mark_channel_hop() resets the detector — no stale fire after hop.
bool test_hop_resets_detector()
{
    std::printf("\n[Stage1-3] mark_channel_hop resets detector state\n");

    Demodulator dem;
    int fire_count = 0;

    dem.set_ale_energy_callback([&]() { ++fire_count; });

    // First visit: fire once on tone.
    const float freq = static_cast<float>(TONE_FREQS_HZ[3]);
    dem.mark_channel_hop();
    const auto tone1 = make_sine(freq, 400);
    dem.push_samples(tone1.data(), 400);
    const int fires_visit1 = fire_count;

    // Hop away then back — energy_fired_ resets. Feed noise: must NOT fire.
    fire_count = 0;
    dem.mark_channel_hop();
    const auto noise = make_noise(400);
    dem.push_samples(noise.data(), 400);
    const int fires_noise = fire_count;

    // Hop again — new tone visit should fire again.
    fire_count = 0;
    dem.mark_channel_hop();
    const auto tone2 = make_sine(freq, 400);
    dem.push_samples(tone2.data(), 400);
    const int fires_visit2 = fire_count;

    std::printf("  fires visit1=%d  fires noise=%d  fires visit2=%d\n",
                fires_visit1, fires_noise, fires_visit2);

    check(fires_visit1 == 1, "Stage1-3: fires exactly once on first visit");
    check(fires_noise  == 0, "Stage1-3: noise visit after hop fires zero times");
    check(fires_visit2 == 1, "Stage1-3: fires exactly once on second visit after hop");
    return (fires_visit1 == 1) && (fires_noise == 0) && (fires_visit2 == 1);
}

} // namespace

int main()
{
    std::printf("=== §A.5.3.3 Stage-1 Triple-Agreement Detector ===\n");

    test_ale_tone_fires();
    test_noise_does_not_fire();
    test_hop_resets_detector();

    if (g_failures == 0) {
        std::printf("\nAll stage-1 detector tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d test(s) FAILED.\n", g_failures);
    return 1;
}
