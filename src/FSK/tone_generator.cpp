/**
 * \file tone_generator.cpp
 * \brief NCO-based 8-FSK tone generator — pure std::sin(), TPDF dither.
 *
 * 32-bit phase accumulator advances by a per-tone increment each sample.
 * All ALE tones are exact multiples of 125 Hz (symbol rate), so each symbol
 * spans integer cycles: after 64 samples the accumulator returns to start,
 * putting every symbol boundary at the waveform max (phase=pi/2, slope=0) —
 * REQ-WAVEFORM-005.
 *
 * Sine via std::sin(double) per sample: at 8 kHz × ~5 words/s that's low
 * tens of thousands of calls/s — negligible. Avoids the table-quantization
 * harmonics and alias products a 256-entry LUT + linear interpolation would add.
 *
 * int16 quantization: TPDF dither + rounding (see tone_generator.h). Without
 * dither, truncating a periodic signal yields signal-correlated quantization
 * error: at 8 kHz, 2500 Hz harmonics fold in-band (5000→3000, 7500→500) as
 * discrete waterfall spurs. TPDF decorrelates it → flat low-level noise floor.
 */

#include "FSK/tone_generator.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ale {

static_assert(SAMPLE_RATE_HZ % SYMBOL_RATE_BAUD == 0,
              "ALE2G requires an integer number of samples per symbol");

ToneGenerator::ToneGenerator() : prng_(0x4A4C4555u) {
    init_phase_increments();
    reset();
}

void ToneGenerator::init_phase_increments() {
    // Walk tones by ascending frequency (rank). FREQ_TO_SYMBOL[rank] = the
    // symbol that tone carries, so store into that slot → phase_increment is
    // indexed by SYMBOL VALUE.
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        uint8_t  symbol  = FREQ_TO_SYMBOL[rank];
        uint32_t freq_hz = TONE_FREQS_HZ[rank];

        // Q32 fixed-point: increment = freq_hz / sample_rate × 2^32. Integer
        // math avoids rounding error; fits uint32_t (all ALE freqs << SAMPLE_RATE_HZ).
        uint64_t increment = (static_cast<uint64_t>(freq_hz) << 32) / SAMPLE_RATE_HZ;
        phase_increment[symbol] = static_cast<uint32_t>(increment);
    }
}

void ToneGenerator::reset() {
    // pi/2 in 32-bit phase: sin(pi/2)=1 → first sample is the peak, slope=0.
    // Reproduced at every subsequent symbol start since all ALE tones complete
    // integer cycles in SAMPLES_PER_SYMBOL (=64) samples.
    phase_ = 0x40000000u;
    // Fixed seed → reproducible dither across test runs.
    prng_.seed(0x4A4C4555u);
}

uint32_t ToneGenerator::generate_symbols(const uint8_t* symbols, uint32_t num_symbols,
                                         int16_t* output, float amplitude) {
    uint32_t samples_written = 0;

    for (uint32_t sym_idx = 0; sym_idx < num_symbols; ++sym_idx) {
        uint8_t symbol = symbols[sym_idx];
        if (symbol >= NUM_TONES)
            symbol = NUM_TONES - 1;

        const uint32_t phase_inc = phase_increment[symbol];

        for (uint32_t s = 0; s < SAMPLES_PER_SYMBOL; ++s) {
            const double angle    = static_cast<double>(phase_) * TWO_PI_OVER_2_32;
            const float  sine_val = static_cast<float>(std::sin(angle));
            output[samples_written++] = quantize(sine_val * amplitude * 32767.0f);
            phase_ += phase_inc;
        }
    }

    return samples_written;
}

uint32_t ToneGenerator::generate_tone(uint8_t symbol_value, uint32_t num_samples,
                                       int16_t* output, float amplitude) {
    if (symbol_value >= NUM_TONES)
        symbol_value = NUM_TONES - 1;

    const uint32_t phase_inc = phase_increment[symbol_value];

    for (uint32_t i = 0; i < num_samples; ++i) {
        const double angle    = static_cast<double>(phase_) * TWO_PI_OVER_2_32;
        const float  sine_val = static_cast<float>(std::sin(angle));
        output[i] = quantize(sine_val * amplitude * 32767.0f);
        phase_ += phase_inc;
    }

    return num_samples;
}

} // namespace ale
