/**
 * \file fec/ale_fec_codec.cpp
 * \brief ALE FEC codec implementation
 */

#include "FEC/ale_fec_codec.h"

namespace ale {

// MIL-STD-188-141B A.5.2.2.3: Coder B inverts the 12 Golay check bits (the low
// 12 bits of the 24-bit codeword) and leaves the upper 12 bits unchanged.  This
// lets a receiver tell a Coder-A word from a Coder-B word.  The operation is its
// own inverse, so the same helper both applies the inversion (on encode) and
// removes it (on decode).
static uint32_t flip_coder_b_check_bits(uint32_t golay_codeword)
{
    const uint32_t unchanged_high_12 = golay_codeword & 0xFFF000u;
    const uint32_t inverted_check_12 = (~golay_codeword) & 0xFFFu;
    return unchanged_high_12 | inverted_check_12;
}

GolayCoded ALEFECCodec::encode_word(uint32_t ale_word)
{
    const uint16_t w_upper = static_cast<uint16_t>((ale_word >> 12) & 0xFFF);
    const uint16_t w_lower = static_cast<uint16_t>( ale_word        & 0xFFF);

    const uint32_t coder_a = Golay::encode(w_upper);

    // Coder B: Golay encode, then invert the check bits per A.5.2.2.3.
    const uint32_t coder_b = flip_coder_b_check_bits(Golay::encode(w_lower));

    return { coder_a, coder_b };
}

uint32_t ALEFECCodec::encode_half(uint16_t info)
{
    return Golay::encode(info);
}

Golay::DecodeResult ALEFECCodec::decode_word(uint32_t codeword, uint16_t& output, GolayMode mode)
{
    return Golay::decode(codeword, output, mode);
}

void ALEFECCodec::interleave_symbols(uint8_t* symbols, uint32_t count)
{
    for (uint32_t offset = 0; offset + Interleaver::BLOCK_SIZE <= count;
         offset += Interleaver::BLOCK_SIZE) {
        Interleaver::interleave(symbols + offset, Interleaver::BLOCK_SIZE);
    }
}

void ALEFECCodec::deinterleave_symbols(uint8_t* symbols, uint32_t count)
{
    for (uint32_t offset = 0; offset + Interleaver::BLOCK_SIZE <= count;
         offset += Interleaver::BLOCK_SIZE) {
        Interleaver::deinterleave(symbols + offset, Interleaver::BLOCK_SIZE);
    }
}

uint64_t ALEFECCodec::interleave_word(const GolayCoded& coded)
{
    return WordInterleaver::interleave(coded.coder_a, coded.coder_b);
}

uint32_t ALEFECCodec::deinterleave_word(uint64_t transmitted, Golay::DecodeResult& fec_out,
                                        GolayMode mode)
{
    uint32_t seq_a = 0, seq_b = 0;
    WordInterleaver::deinterleave(transmitted, seq_a, seq_b);

    // seq_b carries inverted check bits per A.5.2.2.3 — undo the inversion
    // (same self-inverse operation as on encode) before Golay decode.
    const uint32_t seq_b_natural = flip_coder_b_check_bits(seq_b);

    uint16_t corrected_upper = 0;
    uint16_t corrected_lower = 0;
    Golay::DecodeResult fec_a = Golay::decode(seq_a,         corrected_upper, mode);
    Golay::DecodeResult fec_b = Golay::decode(seq_b_natural, corrected_lower, mode);

    if (fec_a.flag == Golay::DECODE_DETECTED || fec_b.flag == Golay::DECODE_DETECTED) {
        fec_out = { Golay::DECODE_DETECTED, 0 };
    } else if (fec_a.flag == Golay::DECODE_CORRECTED || fec_b.flag == Golay::DECODE_CORRECTED) {
        fec_out = { Golay::DECODE_CORRECTED,
                    static_cast<uint8_t>(fec_a.errors_corrected + fec_b.errors_corrected) };
    } else {
        fec_out = { Golay::DECODE_OK, 0 };
    }

    return ((uint32_t)corrected_upper << 12) | corrected_lower;
}

} // namespace ale
