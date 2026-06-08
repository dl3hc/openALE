/**
 * \file fec/ale_fec_codec.cpp
 * \brief ALE FEC codec implementation
 */

#include "FEC/ale_fec_codec.h"

namespace ale {

GolayCoded ALEFECCodec::encode_word(uint32_t ale_word)
{
    const uint16_t w_upper = static_cast<uint16_t>((ale_word >> 12) & 0xFFF);
    const uint16_t w_lower = static_cast<uint16_t>( ale_word        & 0xFFF);

    const uint32_t coder_a = Golay::encode(w_upper);

    // Coder B: Golay encode, then invert check bits per A.5.2.2.3
    const uint32_t cw_b    = Golay::encode(w_lower);
    const uint32_t coder_b = (cw_b & 0xFFF000u) | ((~cw_b) & 0xFFFu);

    return { coder_a, coder_b };
}

uint32_t ALEFECCodec::encode_half(uint16_t info)
{
    return Golay::encode(info);
}

Golay::DecodeResult ALEFECCodec::decode_word(uint32_t codeword, uint16_t& output)
{
    return Golay::decode(codeword, output);
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

uint32_t ALEFECCodec::deinterleave_word(uint64_t transmitted, Golay::DecodeResult& fec_out)
{
    uint32_t seq_a = 0, seq_b = 0;
    WordInterleaver::deinterleave(transmitted, seq_a, seq_b);

    // seq_b carries inverted check bits per A.5.2.2.3 — uninvert before Golay decode
    const uint32_t seq_b_natural = (seq_b & 0xFFF000u) | ((~seq_b) & 0xFFFu);

    uint16_t corrected_upper = 0;
    uint16_t corrected_lower = 0;
    Golay::DecodeResult fec_a = Golay::decode(seq_a,         corrected_upper);
    Golay::DecodeResult fec_b = Golay::decode(seq_b_natural, corrected_lower);

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
