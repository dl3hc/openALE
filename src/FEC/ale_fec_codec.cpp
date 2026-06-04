/**
 * \file fec/ale_fec_codec.cpp
 * \brief ALE FEC codec implementation
 */

#include "fec/ale_fec_codec.h"

namespace ale {

uint32_t ALEFECCodec::encode_word(uint16_t info)
{
    return Golay::encode(info);
}

uint8_t ALEFECCodec::decode_word(uint32_t codeword, uint16_t& output)
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

} // namespace ale
