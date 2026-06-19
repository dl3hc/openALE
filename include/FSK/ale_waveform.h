/**
 * \file fsk/ale_waveform.h
 * \brief ALE-2G waveform parameters and physical-layer constants
 *
 * Physical layer constants (tone frequencies, timing, FFT parameters)
 * and the tone-to-symbol mapping.
 *
 * Specification: MIL-STD-188-141B Appendix A
 *  - 8-FSK modulation: 8 tones, 125 baud, 250 Hz spacing
 *  - Bandwidth: 1.75 kHz (tones 750-2500 Hz)
 */

#pragma once

#include <cstdint>
#include <array>
#include "Protocol/Control/ale_timing.h"

namespace ale {

// Physical layer constants per MIL-STD-188-141B
constexpr uint32_t SAMPLE_RATE_HZ      = 8000;
constexpr uint32_t SYMBOL_RATE_BAUD    = 125;
constexpr uint32_t SAMPLES_PER_SYMBOL  = SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD;  // 64
constexpr uint32_t TONE_SPACING_HZ     = 250;
constexpr uint32_t NUM_TONES           = 8;
constexpr uint32_t BITS_PER_SYMBOL     = 3;
constexpr uint32_t BANDWIDTH_HZ        = 1750;
// Derived from ale::TTONE_MS (ale_timing.h) — single source of truth for symbol duration.
constexpr uint32_t SYMBOL_DURATION_MS  = static_cast<uint32_t>(ale::TTONE_MS);  // 8 ms
// Word timing (number of symbols that form one ALE word on the air)
constexpr uint32_t SYMBOLS_PER_WORD    = 49;
// Triple redundancy (A.5.2.2.4): each of the 49 transmitted-word bits appears
// this many times WITHIN the single 49-symbol word — the three copies of bit b
// sit at on-air bit positions b, b+49 and b+98 of the 147-bit stream the 49
// symbols carry (NOT three separate word transmissions).  Used by the 2/3
// majority voter in ALEDecoder::decode().
constexpr uint32_t SYMBOL_REPETITION   = 3;

// (1) Tone frequencies (Hz) in ASCENDING order, indexed by frequency rank (0 = lowest).
//     A pure physical list — carries NO symbol assignment.
constexpr std::array<uint32_t, NUM_TONES> TONE_FREQS_HZ = {
    750, 1000, 1250, 1500, 1750, 2000, 2250, 2500
};

// All frequencies must be multiples of TONE_SPACING_HZ so that each carrier
// completes an exact integer number of cycles per symbol period (64 samples at
// 8 kHz). This guarantees 64*phase_inc ≡ 0 (mod 2^32) for every tone —
// the precondition for slope-zero symbol boundaries (REQ-WAVEFORM-005).
static_assert([]() constexpr {
    for (auto f : TONE_FREQS_HZ)
        if (f % TONE_SPACING_HZ != 0) return false;
    return true;
}(), "All ALE tone frequencies must be multiples of TONE_SPACING_HZ");

// (2) MIL-STD-188-141B A.5.1.2 symbol assignment: frequency rank -> symbol value.
//     Single source of truth for the Gray-coded tone-to-symbol mapping.
constexpr std::array<uint8_t, NUM_TONES> FREQ_TO_SYMBOL = {
    0, 1, 3, 2, 6, 7, 5, 4
};

constexpr bool freq_to_symbol_is_permutation() {
    std::array<bool, NUM_TONES> seen{};
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        uint8_t s = FREQ_TO_SYMBOL[r];
        if (s >= NUM_TONES || seen[s]) return false;
        seen[s] = true;
    }
    return true;
}
static_assert(freq_to_symbol_is_permutation(),
              "FREQ_TO_SYMBOL must list each symbol value 0..7 exactly once");

// (3) Inverse of FREQ_TO_SYMBOL: symbol value → tone frequency (Hz).
//     Derived from TONE_FREQS_HZ + FREQ_TO_SYMBOL; single source of truth.
constexpr std::array<uint32_t, NUM_TONES> SYMBOL_TO_FREQ = []() constexpr {
    std::array<uint32_t, NUM_TONES> result{};
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank)
        result[FREQ_TO_SYMBOL[rank]] = TONE_FREQS_HZ[rank];
    return result;
}();

static_assert([]() constexpr {
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank)
        if (SYMBOL_TO_FREQ[FREQ_TO_SYMBOL[rank]] != TONE_FREQS_HZ[rank])
            return false;
    return true;
}(), "SYMBOL_TO_FREQ must be the exact inverse of FREQ_TO_SYMBOL");

// FFT parameters
constexpr uint32_t FFT_SIZE        = 64;
constexpr uint32_t FFT_BIN_OFFSET  = 6;
constexpr uint32_t FFT_BIN_STEP    = 2;
constexpr uint32_t FFT_BIN_SPAN    = 15;

/**
 * \struct Symbol
 * Decoded FSK symbol with confidence metrics
 */
struct Symbol {
    uint8_t  bits[BITS_PER_SYMBOL];
    float    magnitude;
    float    signal_to_noise;
    uint32_t sample_index;
};

} // namespace ale