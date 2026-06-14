/**
 * \file symbol_decoder.h
 * \brief FSK symbol detection from FFT magnitude peaks (+ a 3-bit majority helper).
 *
 * Extracts a 3-bit symbol value (0-7) from the peak of an FFT magnitude array
 * via FREQ_TO_SYMBOL (Gray-coded) mapping.
 *
 * NOTE (status): this class backs the FFTDemodulator path, which is currently
 * NOT used by the live receiver — the live demodulator (ALE2GModem::Demodulator)
 * detects tones with the Goertzel algorithm and decodes words with
 * ALEDecoder::decode().  SymbolDecoder + FFTDemodulator are retained as an
 * alternative FFT-based front end, should it ever be preferred over Goertzel.
 *
 * Specification: MIL-STD-188-141B
 *  - Symbol detection: find peak in bins 6-20 (every 2 bins)
 *  - Bin-to-symbol mapping: uses FREQ_TO_SYMBOL lookup table for Gray-coded mapping
 */

#pragma once

#include "FSK/ale_waveform.h"
#include <array>
#include <cstdint>

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
