/**
 * \file tone_generator.cpp
 * \brief NCO-based 8-FSK tone generator — pure std::sin(), no lookup table.
 *
 * The 32-bit phase accumulator advances by a tone-specific increment each
 * sample.  Because all ALE tones are exact multiples of 125 Hz (the symbol
 * rate), every symbol spans an integer number of cycles: after 64 samples the
 * accumulator returns to its starting value, so every symbol boundary falls
 * at the waveform maximum (phase = pi/2, slope = 0) — REQ-WAVEFORM-005.
 *
 * Sine is computed via std::sin(double) per sample.  At 8 kHz × ~5 words/s
 * the per-second call count is in the low tens of thousands — completely
 * negligible.  This eliminates all table-quantisation harmonics and alias
 * products that a 256-entry LUT with linear interpolation would introduce.
 */

#include "FSK/tone_generator.h"
#include <cmath>
#include <algorithm>

namespace ale {

static_assert(SAMPLE_RATE_HZ % SYMBOL_RATE_BAUD == 0,
              "ALE2G requires an integer number of samples per symbol");

ToneGenerator::ToneGenerator() {
    init_phase_increments();
    reset();
}

void ToneGenerator::init_phase_increments() {
    // Walk tones in ascending-frequency order (rank). FREQ_TO_SYMBOL[rank] gives
    // the symbol value carried by that tone, so we store the increment in the
    // matching symbol slot → phase_increment indexed by SYMBOL VALUE.
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        uint8_t  symbol  = FREQ_TO_SYMBOL[rank];
        uint32_t freq_hz = TONE_FREQS_HZ[rank];

        // Q32 fixed-point: increment = freq_hz / sample_rate × 2^32.
        // Integer arithmetic avoids rounding error; result fits in uint32_t
        // because all ALE frequencies are well below SAMPLE_RATE_HZ.
        uint64_t increment = (static_cast<uint64_t>(freq_hz) << 32) / SAMPLE_RATE_HZ;
        phase_increment[symbol] = static_cast<uint32_t>(increment);
    }
}

void ToneGenerator::reset() {
    // pi/2 in 32-bit phase: sin(pi/2) = 1 → first sample is the waveform peak,
    // slope = 0.  Guaranteed to be reproduced at the start of every subsequent
    // symbol because all ALE tones complete an integer number of cycles in
    // SAMPLES_PER_SYMBOL (= 64) samples.
    phase_ = 0x40000000u;
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

            int32_t sample = static_cast<int32_t>(sine_val * amplitude * 32767.0f);
            sample = std::max(-32768, std::min(32767, sample));
            output[samples_written++] = static_cast<int16_t>(sample);

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

        int32_t sample = static_cast<int32_t>(sine_val * amplitude * 32767.0f);
        sample = std::max(-32768, std::min(32767, sample));
        output[i] = static_cast<int16_t>(sample);

        phase_ += phase_inc;
    }

    return num_samples;
}

} // namespace ale
