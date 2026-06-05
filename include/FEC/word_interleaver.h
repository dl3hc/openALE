/**
 * \file fec/word_interleaver.h
 * \brief Bit-level word interleaver for ALE transmitted-word structure
 *
 * Implements the bit interleaving / deinterleaving per MIL-STD-188-141B A.5.2.2.3.
 *
 * Transmitted-word layout (49 bits):
 *   Positions  0..23 : A/B pairs for W1..W24 (data bits, interleaved)
 *   Positions 24..47 : A/B pairs for G1..G12 (Golay parity, normal / inverted)
 *   Position  48     : Stuff bit S49 = 0
 *
 * Parity bits G13..G24 are the bitwise inverses of G1..G12 (AC-FEC-012-2).
 * Golay (24,12) is applied to the upper 12 bits of the ALE word (W1..W12).
 */

#pragma once

#include "FEC/golay.h"
#include <cstdint>

namespace ale {

class WordInterleaver {
public:
    static constexpr uint32_t TRANSMITTED_BITS = 49;

    /**
     * Interleave a 24-bit ALE word into a 49-bit transmitted word.
     *
     * Golay parity G1..G12 is computed from bits 23..12 (W1..W12).
     * Bit 48 of the result (S49) is always 0.
     *
     * \param ale_word  24-bit ALE word  [W1=bit23 .. W24=bit0]
     * \return          49-bit transmitted word [A1,B1, ..., A24,B24, S49]
     */
    static uint64_t interleave(uint32_t ale_word);

    /**
     * Deinterleave a 49-bit transmitted word and apply Golay error correction.
     *
     * W1..W12 are corrected using the extracted G1..G12 parity.
     * W13..W24 are passed through unchanged.
     * Bit 48 (S49) is ignored per AC-FEC-013-4.
     *
     * \param transmitted  49-bit received word
     * \param fec_out      [out] Golay decode result (flag + errors_corrected)
     * \return             24-bit corrected ALE word
     */
    static uint32_t deinterleave(uint64_t transmitted, Golay::DecodeResult& fec_out);
};

} // namespace ale
