/**
 * \file fec/word_interleaver.cpp
 * \brief Bit-level word interleaver implementation (MIL-STD-188-141B A.5.2.2.3)
 */

#include "FEC/word_interleaver.h"

namespace ale {

// Notation (spec A.5.2.2.3):
//   f.W[i]   = bit i of the 24-bit ALE word  (i = 0..23, W1 = bit 23, W24 = bit 0)
//   f.G[i]   = Golay parity bit i            (i = 0..11, G1 = index 11, G12 = index 0)
//   f.G[i]   for i = 12..23 = inverted complement of f.G[i-12]
//
// Data bits (k = 0..11):
//   out[2*k]   = f.W[23-k]   → W_(k+1)   (A channel, bits 23..12 of word)
//   out[2*k+1] = f.W[11-k]   → W_(13+k)  (B channel, bits 11..0 of word)
//
// Parity bits (k = 12..23):
//   out[2*k]   = f.G[23-k]   → G_(k-11)  (A channel, normal parity, bit 11..0 order)
//   out[2*k+1] = ~f.G[23-k]  → ~G_(k+1)  (B channel, inverted parity)
//
// Stuff bit:
//   out[48] = 0  (S49)

uint64_t WordInterleaver::interleave(uint32_t ale_word)
{
    const uint16_t w_info = (ale_word >> 12) & 0xFFF;
    const uint16_t parity = Golay::encode(w_info) & 0xFFF;  // G1..G12

    uint64_t out = 0;

    // Data bits: k = 0..11
    for (int k = 0; k < 12; ++k) {
        if ((ale_word >> (23 - k)) & 1u) out |= (1ULL << (2 * k));      // W[23-k] → A
        if ((ale_word >> (11 - k)) & 1u) out |= (1ULL << (2 * k + 1));  // W[11-k] → B
    }

    // Parity bits: k = 12..23
    // f.G[23-k] for k=12..23 accesses parity bits 11..0 (MSB..LSB)
    for (int k = 12; k < 24; ++k) {
        const uint8_t g = (parity >> (23 - k)) & 1u;
        if (g)  out |= (1ULL << (2 * k));      // G normal → A
        if (!g) out |= (1ULL << (2 * k + 1));  // G inverted → B
    }

    // bit 48 remains 0 (S49 = 0)
    return out;
}

uint32_t WordInterleaver::deinterleave(uint64_t transmitted, Golay::DecodeResult& fec_out)
{
    uint32_t ale_word = 0;
    uint16_t parity   = 0;

    // Reconstruct data bits from A/B channels
    for (int k = 0; k < 12; ++k) {
        if ((transmitted >> (2 * k)) & 1ULL)     ale_word |= (1u << (23 - k));  // A → W[23-k]
        if ((transmitted >> (2 * k + 1)) & 1ULL) ale_word |= (1u << (11 - k));  // B → W[11-k]
    }

    // Reconstruct parity from A channel (even positions 24..46)
    // Position 2*k (k=12..23) carries G[23-k] = parity bit (23-k), i.e. bits 11..0
    for (int k = 12; k < 24; ++k) {
        if ((transmitted >> (2 * k)) & 1ULL) parity |= (1u << (23 - k));
    }

    // Bit 48 (S49) is ignored per AC-FEC-013-4.

    // Apply Golay error correction to W1..W12 (bits 23..12 of ale_word)
    const uint16_t w_info   = (ale_word >> 12) & 0xFFF;
    const uint32_t codeword = ((uint32_t)w_info << 12) | parity;
    uint16_t corrected_info = 0;
    fec_out = Golay::decode(codeword, corrected_info);

    // Rebuild ALE word with corrected upper half; lower half unchanged
    return (ale_word & 0x000FFF) | ((uint32_t)corrected_info << 12);
}

} // namespace ale
