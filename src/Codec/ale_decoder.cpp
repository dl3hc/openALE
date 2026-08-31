/**
 * \file Codec/ale_decoder.cpp
 */

#include "Codec/ale_decoder.h"
#include "FEC/ale_fec_codec.h"

namespace ale {

bool ALEDecoder::decode(const uint8_t* symbols_49,
                        ALEWord& out,
                        Golay::DecodeResult& fec,
                        uint8_t* unanimous_votes_out,
                        GolayMode golay_mode)
{
    // ── 2/3 majority voter + unanimous-vote count (A.5.2.6.3) ───────────────
    // Triple redundancy (A.5.2.2.4): each of the 49 transmitted-word bits is
    // carried 3× within the single 49-symbol word. The 3 copies of bit `b`
    // occupy on-air bit positions b, b+49, b+98 of the 147-bit stream those
    // 49 symbols carry (BITS_PER_SYMBOL bits/symbol, MSB first): on-air bit
    // position p lives in symbol p/BITS_PER_SYMBOL, sub-bit lane
    // (BITS_PER_SYMBOL - 1 - p % BITS_PER_SYMBOL).
    //
    // For bits 0..47 take the 2/3 majority and tally the UNANIMOUS positions
    // (all 3 copies agree) — the standard's "threshold of unanimous votes in
    // the 2/3 majority voter decoder" quality/triple-redundant-phase metric.
    // Bit 48 (S49, stuff bit) is not voted: stays 0, excluded from the
    // unanimous count per A.5.2.2.4 ("48 possible votes").
    uint64_t tx49            = 0;
    uint8_t  unanimous_votes = 0;
    for (uint32_t bit = 0; bit < SYMBOLS_PER_WORD - 1u; ++bit) {
        uint8_t ones = 0;   // number of '1's among this bit's three copies
        for (uint32_t copy = 0; copy < SYMBOL_REPETITION; ++copy) {
            const uint32_t p    = bit + copy * SYMBOLS_PER_WORD;        // 0..146
            const uint8_t  cbit = (symbols_49[p / BITS_PER_SYMBOL]
                                   >> (BITS_PER_SYMBOL - 1u - p % BITS_PER_SYMBOL)) & 1u;
            ones = static_cast<uint8_t>(ones + cbit);
        }
        if (ones == 0u || ones == SYMBOL_REPETITION)   // all three copies agree
            ++unanimous_votes;
        if (ones * 2u > SYMBOL_REPETITION)             // 2/3 majority → bit is 1
            tx49 |= (1ULL << bit);
    }
    out.unanimous_votes = unanimous_votes;
    if (unanimous_votes_out)
        *unanimous_votes_out = unanimous_votes;

    // ── Successful Golay decode of the "A"/"B" word bits (A.5.2.6.3) ────────
    // Deinterleave the 2 Golay channels (A.5.2.2.3), FEC-decode both halves.
    // DECODE_DETECTED on either half = uncorrectable error → not a successful
    // decode; DECODE_OK / DECODE_CORRECTED = errors within the code's power.
    const uint32_t word24 = ALEFECCodec::deinterleave_word(tx49, fec, golay_mode);
    out.fec_errors = fec.errors_corrected;
    out.golay_uncorrectable = (fec.flag != Golay::DECODE_OK
                               && fec.flag != Golay::DECODE_CORRECTED);
    if (out.golay_uncorrectable)
        return false;

    // ── Acceptable character bits (A.5.2.6.3) ───────────────────────────────
    // parse_from_bits validates each of the 3 characters against the word's
    // ASCII subset (Basic 38 for address words, Expanded 64 for DATA/REP).
    WordParser parser;
    return parser.parse_from_bits(word24, out);
}

} // namespace ale
