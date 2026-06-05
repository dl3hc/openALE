/**
 * \file symbol_decoder.h
 * \brief FSK symbol detection and decoding
 * 
 * Extracts 3-bit symbol values from FFT magnitude peaks.
 * Uses majority voting for triple-redundancy error correction.
 * 
 * Specification: MIL-STD-188-141B
 *  - Each data bit transmitted 3 times (symbols at positions k, k+49, k+98)
 *  - Symbol detection: find peak in bins 6-20 (every 2 bins)
 *  - Bin-to-symbol mapping: uses FREQ_TO_SYMBOL lookup table for Gray-coded mapping
 */

#pragma once

#include "FSK/ale_waveform.h"
#include <array>
#include <cstdint>

// FEC (Forward Error Correction) parameters per MIL-STD-188-141B
constexpr uint32_t SYMBOL_REPETITION  = 3;   // Triple redundant word transmission
constexpr uint32_t VOTE_BUFFER_LENGTH = 48;  // Symbol voting window size
constexpr uint32_t VOTE_THRESHOLD_BAD = 25;  // Min votes for valid symbol detection

namespace ale {

class SymbolDecoder {
public:
    SymbolDecoder();
    
    /**
     * Detect FSK symbol from FFT magnitudes
     * Finds peak in bins 6-22 and maps to symbol value
     * 
     * \param magnitudes FFT magnitude array [FFT_SIZE]
     * \return Symbol value 0-7, or 0xFF if detection failed
     */
    static uint8_t detect_symbol(const std::array<float, FFT_SIZE>& magnitudes);
    
/**
 * \brief Extract 3-bit symbol value from peak position
 * Maps FFT bin position to 0-7 symbol (using FREQ_TO_SYMBOL lookup)
 * 
 * \param bin_index FFT bin (6-22, every 2 bins for ALE)
 * \return Symbol value 0-7
 */
static uint8_t bin_to_symbol(uint32_t bin_index);
    
    /**
     * Majority voting for triple-redundant bit
     * Combines 3 copies of same bit for error correction
     * 
     * \param bits Array of 3 bit values
     * \return Final bit value (0 or 1)
     */
    static uint8_t majority_vote(const uint8_t bits[3]);
    
    /**
     * Decode word using triple redundancy voting.
     *
     * Votes across SYMBOL_REPETITION copies of the 49-bit transmitted word.
     * The caller must supply SYMBOLS_PER_WORD * SYMBOL_REPETITION symbols.
     * Each symbol value (0-7) contributes its LSB as the transmitted bit.
     *
     * \param symbols      Buffer of SYMBOLS_PER_WORD * SYMBOL_REPETITION symbols
     * \param output_word  [out] 49-bit transmitted word after majority voting (bit 48 = 0)
     * \return             Number of unanimous votes among the 48 voted bit positions
     *                     (A.5.2.6.3); range 0..48.  Threshold: VOTE_THRESHOLD_BAD.
     */
    static uint32_t decode_word_with_voting(const uint8_t symbols[],
                                            uint64_t& output_word);
    
private:
    // Lookup table: FFT bin -> symbol value (not used anymore, kept for API compatibility)
    // Bins 6-22 (every 2): 6->0, 8->1, 10->2, 12->3, 14->4, 16->5, 18->6, 20->7, 22->0xFF
    // Note: This is now superseded by FREQ_TO_SYMBOL lookup in fsk/ale_waveform.h
    static constexpr std::array<uint8_t, 32> BIN_TO_SYMBOL_TABLE = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0xFF, 1, 0xFF,
        2, 0xFF, 3, 0xFF, 4, 0xFF, 5, 0xFF, 6, 0xFF,
        7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF
    };
};

} // namespace ale
