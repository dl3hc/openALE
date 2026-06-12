/**
 * \file Codec/ale_decoder.cpp
 */

#include "Codec/ale_decoder.h"
#include "FEC/ale_fec_codec.h"

namespace ale {

bool ALEDecoder::decode(const uint8_t* symbols_49,
                        ALEWord& out,
                        Golay::DecodeResult& fec,
                        uint8_t* bad_votes_out)
{
    // Step 1: Unpack 3-bit symbols → 147-bit stream (MSB-first per symbol).
    // stream[3k+0] = bit2 (MSB), stream[3k+1] = bit1, stream[3k+2] = bit0.
    uint8_t stream[SYMBOLS_PER_WORD * BITS_PER_SYMBOL];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        stream[3*k + 0] = (symbols_49[k] >> 2) & 1u;
        stream[3*k + 1] = (symbols_49[k] >> 1) & 1u;
        stream[3*k + 2] =  symbols_49[k]        & 1u;
    }

    // Step 2: Stride-49 majority vote → 49-bit tx49.
    // Bit i = majority of stream[i], stream[i+49], stream[i+98].
    // bad_votes counts non-unanimous positions (1 or 2 out of 3 voters).
    // A clean word has bad_votes = 0; high values indicate noise or misalignment.
    // Adapted from LinuxALE modem.c (Brain / Toivanen 2001).
    uint64_t tx49      = 0;
    uint32_t bad_votes = 0;
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i) {
        const uint32_t sum = stream[i]
                           + stream[i + SYMBOLS_PER_WORD]
                           + stream[i + 2u * SYMBOLS_PER_WORD];
        if (sum == 1u || sum == 2u)
            ++bad_votes;
        if (sum >= 2u)
            tx49 |= (1ULL << i);
    }
    if (bad_votes_out)
        *bad_votes_out = static_cast<uint8_t>(bad_votes < 255u ? bad_votes : 255u);

    // Step 3: Deinterleave + Golay FEC → 24-bit word.
    const uint32_t word24 = ALEFECCodec::deinterleave_word(tx49, fec);
    if (fec.flag != Golay::DECODE_OK && fec.flag != Golay::DECODE_CORRECTED)
        return false;

    // Step 4: Parse to ALEWord.
    WordParser parser;
    return parser.parse_from_bits(word24, out);
}

} // namespace ale
