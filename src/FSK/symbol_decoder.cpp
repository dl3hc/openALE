/**
 * \file symbol_decoder.cpp
 * \brief Implementation of FSK symbol detection and decoding
 */

#include "FSK/symbol_decoder.h"
#include "Word/ale_word.h"
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

uint8_t SymbolDecoder::decode_word_with_voting(const WordVoteBuffer& symbols,
                                               uint64_t& output_word) {
    // Each 8-FSK symbol carries BITS_PER_SYMBOL=3 bits.
    // Bit position k is encoded in symbol k/3 at bit-lane k%3.
    // This maps all 8 tones to the full 0-7 symbol range (symmetric with build_symbols()).
    //
    // Bits 0..47: voted + count toward unanimous threshold (A.5.2.6.3 — 48 "possible votes").
    // Bit 48 (S49): voted for correctness, excluded from unanimous count.
    // Bit positions 49-50 (padding zeros in the last symbol): never accessed.

    uint64_t word = 0;
    uint8_t unanimous_count = 0;

    for (uint32_t bit_pos = 0; bit_pos <= 48; ++bit_pos) {
        const uint32_t sym_pos    = bit_pos / BITS_PER_SYMBOL;
        const uint32_t bit_in_sym = bit_pos % BITS_PER_SYMBOL;

        uint8_t bit_copies[SYMBOL_REPETITION];
        for (uint32_t rep = 0; rep < SYMBOL_REPETITION; ++rep) {
            const uint8_t sym = symbols[sym_pos + rep * SYMBOLS_PER_WORD];
            bit_copies[rep] = (sym >> bit_in_sym) & 1u;
        }

        const uint8_t voted = majority_vote(bit_copies);
        if (voted) word |= (1ULL << bit_pos);

        if (bit_pos < VOTE_BUFFER_LENGTH) {  // bits 0..47 count toward sync threshold
            if (bit_copies[0] == bit_copies[1] && bit_copies[1] == bit_copies[2])
                ++unanimous_count;
        }
    }

    output_word = word;
    return unanimous_count;
}

} // namespace ale
