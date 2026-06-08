/**
 * \file fec/word_interleaver.h
 * \brief Bit-level word interleaver for ALE transmitted-word structure (A.5.2.2.3)
 *
 * Responsible ONLY for interleaving and deinterleaving of pre-encoded bit sequences.
 * Golay encoding and decoding are NOT performed here; see ALEFECCodec.
 *
 * Transmitted-word layout (49 bits):
 *   Positions  0..23 : A/B pairs for data bits   W1..W24
 *   Positions 24..47 : A/B pairs for parity bits G1..G12 (A, normal) / G13..G24 (B, inverted)
 *   Position  48     : Stuff bit S49 = 0
 *
 * interleave()   expects two pre-encoded 24-bit codewords (sequence_a, sequence_b).
 * deinterleave() returns two extracted 24-bit codewords; B-channel parity is uninverted.
 * Both operations are the exact inverse of each other.
 */

#pragma once

#include <cstdint>

namespace ale {

class WordInterleaver {
public:
    static constexpr uint32_t TRANSMITTED_BITS = 49;

    /**
     * Interleave two Golay codewords into a 49-bit transmitted word.
     *
     * sequence_a = [W1..W12  | G1..G12 ] — A-channel; parity placed normal.
     * sequence_b = [W13..W24 | G13..G24] — B-channel; parity placed inverted.
     * Bit 48 of the result (S49) is always 0.
     *
     * \param sequence_a  24-bit Golay codeword for the upper ALE-word half
     * \param sequence_b  24-bit Golay codeword for the lower ALE-word half
     * \return            49-bit transmitted word [A1,B1, ..., A24,B24, S49]
     */
    static uint64_t interleave(uint32_t sequence_a, uint32_t sequence_b);

    /**
     * Deinterleave a 49-bit received word into two raw codewords.
     *
     * Reconstructs sequence_a (A-channel) and sequence_b (B-channel).
     * B-channel parity bits are uninverted before writing to sequence_b.
     * Bit 48 (S49) is ignored per AC-FEC-013-4.
     * No Golay error correction is applied — call ALEFECCodec::decode_word() next.
     *
     * \param transmitted  49-bit received word
     * \param sequence_a   [out] reconstructed A-channel codeword [W1..W12  | G1..G12 ]
     * \param sequence_b   [out] reconstructed B-channel codeword [W13..W24 | G13..G24]
     */
    static void deinterleave(uint64_t transmitted,
                             uint32_t& sequence_a, uint32_t& sequence_b);
};

} // namespace ale
