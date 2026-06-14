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

} // namespace ale
