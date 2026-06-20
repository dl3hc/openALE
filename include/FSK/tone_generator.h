/**
 * \file fsk/tone_generator.h
 * \brief 8-FSK tone generator using NCO
 *
 * Generates 8-FSK tones using a single continuous NCO phase accumulator.
 * The sine value is computed via std::sin() — no lookup table, no
 * interpolation artifacts, no alias products.
 *
 * Quantization: TPDF dither (±1 LSB, triangular PDF) is applied before
 * rounding to int16.  This decorrelates the quantization error from the
 * signal, converting discrete harmonic spurs into a flat, low-level noise
 * floor.  At 8 kHz, the 2nd/3rd harmonics of a 2500 Hz tone would otherwise
 * fold back in-band (5000→3000 Hz, 7500→500 Hz) and appear in the waterfall.
 *
 * REQ-WAVEFORM-005 (slope-zero symbol boundaries) is unaffected: ±1 LSB
 * dither does not alter the integer-cycle NCO phase continuity.
 *
 * The PRNG is seeded deterministically in reset() so test output is
 * reproducible regardless of call order.
 *
 * Specification: MIL-STD-188-141B / ALE2G
 *  - Frequencies: 750, 1000, 1250, 1500, 1750, 2000, 2250, 2500 Hz
 *  - Sample rate: 8000 Hz
 *  - Symbol rate: 125 baud (64 samples per symbol)
 *  - Phase init: pi/2 (0x40000000) — every symbol starts at waveform maximum,
 *    slope zero (REQ-WAVEFORM-005; guaranteed because all ALE frequencies are
 *    exact multiples of 125 Hz, so each symbol spans an integer number of cycles)
 */

#pragma once

#include "FSK/ale_waveform.h"
#include <array>
#include <cstdint>
#include <random>

namespace ale {

class ToneGenerator {
public:
    ToneGenerator();

    /**
     * Generate tone samples for given symbols.
     * \param symbols     Array of symbol values (0-7)
     * \param num_symbols Number of symbols to generate
     * \param output      Pre-allocated buffer [num_symbols × SAMPLES_PER_SYMBOL]
     * \param amplitude   Linear amplitude scalar (use ale::TX_AMPLITUDE for TX)
     * \return Number of samples written
     */
    uint32_t generate_symbols(const uint8_t* symbols, uint32_t num_symbols,
                               int16_t* output, float amplitude = TX_AMPLITUDE);

    /**
     * Generate a continuous tone (no symbol switching).
     * \param symbol_value FSK symbol (0-7)
     * \param num_samples  Number of samples to generate
     * \param output       Output buffer [num_samples]
     * \param amplitude    Linear amplitude scalar
     * \return Number of samples written
     */
    uint32_t generate_tone(uint8_t symbol_value, uint32_t num_samples,
                           int16_t* output, float amplitude = TX_AMPLITUDE);

    /** Reset NCO to initial phase (pi/2) and reseed PRNG to fixed value. */
    void reset();

    /**
     * Return the NCO phase increment stored for a given symbol value.
     * Exposed for unit-testing AC-WAVEFORM-002-001 only.
     */
    uint32_t phase_increment_for(uint8_t symbol) const {
        return (symbol < NUM_TONES) ? phase_increment[symbol] : 0u;
    }

private:
    static constexpr uint32_t SAMPLES_PER_SYMBOL = SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD;

    // 2π / 2^32: converts the 32-bit phase accumulator to radians.
    // Computed in double so that all 32 bits of phase_ are preserved before
    // the final cast to float — gives < 0.01 LSB error at 16-bit resolution.
    static constexpr double TWO_PI_OVER_2_32 =
        2.0 * 3.14159265358979323846 / 4294967296.0;

    // One-stream phase accumulator; the symbol selects the phase increment only.
    uint32_t phase_;

    // Phase increment per sample for each symbol (Q32 fixed-point, indexed by symbol value)
    std::array<uint32_t, NUM_TONES> phase_increment;

    // TPDF dither PRNG — seeded deterministically in reset() for reproducibility.
    std::minstd_rand prng_;
    std::uniform_real_distribution<float> uniform_dist_{-0.5f, 0.5f};

    // Convert a floating-point sample to int16 with TPDF dither + rounding.
    // TPDF: two independent uniform [-0.5, 0.5] draws summed → triangular ±1 LSB.
    inline int16_t quantize(float sample) {
        const float dither = uniform_dist_(prng_) + uniform_dist_(prng_);
        long v = std::lround(static_cast<double>(sample) + static_cast<double>(dither));
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        return static_cast<int16_t>(v);
    }

    void init_phase_increments();
};

} // namespace ale
