/**
 * \file symbol_decoder.cpp
 * \brief Implementation of FSK symbol detection and decoding
 */

#include "FSK/symbol_decoder.h"
#include <algorithm>
#include <cmath>

namespace ale {

uint8_t SymbolDecoder::detect_symbol(const std::array<float, FFT_SIZE>& magnitudes) {
    // Find peak magnitude in ALE tone region (every FFT_BIN_STEP bins)
    // Uses FREQ_TO_SYMBOL lookup table to convert frequency-ascending index to Gray-coded symbol
    
    float peak_mag = -1e6f;
    uint32_t peak_freq_idx = 0;
    
    // Check each ALE tone bin (every FFT_BIN_STEP bins starting from FFT_BIN_OFFSET)
    for (uint32_t fi = 0; fi < NUM_TONES; ++fi) {
        uint32_t bin = FFT_BIN_OFFSET + fi * FFT_BIN_STEP;
        if (bin < FFT_SIZE && magnitudes[bin] > peak_mag) {
            peak_mag = magnitudes[bin];
            peak_freq_idx = fi;
        }
    }
    
    if (peak_freq_idx == 0) {
        // Check if we actually found a peak (this check is redundant but for safety)
        bool found_peak = false;
        for (uint32_t fi = 0; fi < NUM_TONES; ++fi) {
            uint32_t bin = FFT_BIN_OFFSET + fi * FFT_BIN_STEP;
            if (bin < FFT_SIZE && magnitudes[bin] > 0.0f) {
                found_peak = true;
                break;
            }
        }
        if (!found_peak) {
            return 0xFF;  // No valid tone detected
        }
    }
    
    // Convert frequency-ascending index to Gray-coded symbol value
    return FREQ_TO_SYMBOL[peak_freq_idx];
}

uint8_t SymbolDecoder::bin_to_symbol(uint32_t bin_index) {
    // ALE tones occupy bins 6-13 (consecutive) -> symbols 0-7
    // Mapping: bin 6->symbol 0, bin 7->symbol 1, ..., bin 13->symbol 7
    
    if (bin_index < 6 || bin_index > 13) {
        return 0xFF;  // Invalid bin
    }
    
    uint8_t symbol = static_cast<uint8_t>(bin_index - 6);  // 6->0, 7->1, ..., 13->7
    return symbol;
}

uint8_t SymbolDecoder::majority_vote(const uint8_t bits[3]) {
    // Sum the three bits: 0-3 is the count of 1s
    uint8_t sum = bits[0] + bits[1] + bits[2];
    // Majority voting: if 2 or more are 1, output 1, else 0
    return (sum >= 2) ? 1 : 0;
}

uint64_t SymbolDecoder::decode_word_with_voting(const std::array<uint8_t, SYMBOLS_PER_WORD>& symbols) {
    // Recover the 147-bit stream from 49 symbols (MSB-first per symbol).
    // stream[i] = (symbols[i/3] >> (2 - i%3)) & 1   for i = 0..146
    uint8_t stream[3 * SYMBOLS_PER_WORD] = {};
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        for (uint32_t b = 0; b < BITS_PER_SYMBOL; ++b)
            stream[k * BITS_PER_SYMBOL + b] = (symbols[k] >> (BITS_PER_SYMBOL - 1u - b)) & 1u;
    }

    // Stride-49 majority vote: bits 0..47; bit 48 (S49) stays 0.
    // voted_bit[i] = majority(stream[i], stream[i+49], stream[i+98])  i = 0..47
    uint64_t tx49 = 0;
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD - 1u; ++i) {
        const uint8_t trio[3] = { stream[i], stream[i + 49], stream[i + 98] };
        if (majority_vote(trio))
            tx49 |= (1ULL << i);
    }
    return tx49;
}

} // namespace ale
