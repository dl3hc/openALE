/**
 * \file fec/word_interleaver.cpp
 * \brief Bit-level word interleaver implementation (MIL-STD-188-141B A.5.2.2.3)
 *
 * Pure interleaving only — no Golay encoding or decoding.
 */

#include "FEC/word_interleaver.h"

namespace ale {

// Bit layout (spec A.5.2.2.3):
//   sequence_a = [W1..W12  | G1..G12 ]  — 24 bits, A-channel source
//   sequence_b = [W13..W24 | G13..G24]  — 24 bits, B-channel source
//
// Data bits (k = 0..11), sequence_x bit index: 23-k = MSB first
//   out[2*k]   = sequence_a bit (23-k)  → W_(k+1)   A-channel
//   out[2*k+1] = sequence_b bit (23-k)  → W_(k+13)  B-channel
//
// Parity bits (k = 12..23), bit index in 12-bit parity field: 23-k
//   out[2*k]   = G_A bit (23-k)   A-channel parity, placed normal
//   out[2*k+1] = ~G_B bit (23-k)  B-channel parity, placed inverted
//
// Stuff bit:
//   out[48] = 0  (S49)

uint64_t WordInterleaver::interleave(uint32_t sequence_a, uint32_t sequence_b)
{
    uint64_t out = 0;

    // Data bits: k = 0..11
    for (int k = 0; k < 12; ++k) {
        if ((sequence_a >> (23 - k)) & 1u) out |= (1ULL << (2 * k));      // A-channel
        if ((sequence_b >> (23 - k)) & 1u) out |= (1ULL << (2 * k + 1));  // B-channel
    }

    // Parity bits: k = 12..23  (parity field = bits 11..0 of each codeword)
    // sequence_b already carries inverted check bits — place as-is
    for (int k = 12; k < 24; ++k) {
        if ((sequence_a >> (23 - k)) & 1u) out |= (1ULL << (2 * k));      // A-channel
        if ((sequence_b >> (23 - k)) & 1u) out |= (1ULL << (2 * k + 1));  // B-channel
    }

    // bit 48 remains 0 (S49 = 0)
    return out;
}

void WordInterleaver::deinterleave(uint64_t transmitted,
                                   uint32_t& sequence_a, uint32_t& sequence_b)
{
    sequence_a = 0;
    sequence_b = 0;

    // Data bits: k = 0..11
    for (int k = 0; k < 12; ++k) {
        if ((transmitted >> (2 * k)) & 1ULL)
            sequence_a |= (1u << (23 - k));  // A-channel → sequence_a data
        if ((transmitted >> (2 * k + 1)) & 1ULL)
            sequence_b |= (1u << (23 - k));  // B-channel → sequence_b data
    }

    // Parity bits: k = 12..23
    //   sequence_b parity is stored inverted — extract as-is; ALEFECCodec uninverts before decode
    for (int k = 12; k < 24; ++k) {
        if ((transmitted >> (2 * k)) & 1ULL)
            sequence_a |= (1u << (23 - k));  // A-channel parity
        if ((transmitted >> (2 * k + 1)) & 1ULL)
            sequence_b |= (1u << (23 - k));  // B-channel parity (still inverted)
    }

    // Bit 48 (S49) is ignored per AC-FEC-013-4.
}

} // namespace ale
