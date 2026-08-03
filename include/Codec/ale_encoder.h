/**
 * \file Codec/ale_encoder.h
 * \brief ALE 2G word encoder: ALEWord / tx49 → 49 FSK symbol values.
 *
 * Encoding pipeline (A.5.2.2.2 / A.5.2.2.3 / A.5.2.2.4):
 *
 *   ALEWord  →  word.encode()          →  tx49 (49-bit, Golay + interleave)
 *   tx49     →  encode_tx49()          →  SymbolFrame (49 × uint8_t, 0-7)
 *
 * Wire layout (A.5.2.2.4):
 *   The 49-bit tx49 word is mapped to 49 FSK symbols, each carrying 3 bits.
 *   The 147-bit stream = three end-to-end copies of tx49 (physical 3× redundancy).
 *   Symbol k covers stream bits [3k, 3k+1, 3k+2] MSB-first:
 *     sym[k] = { tx49[3k%49], tx49[(3k+1)%49], tx49[(3k+2)%49] }
 *
 * SymbolFrame output feeds ToneGenerator::generate_symbols() or can be decoded
 * directly by ALEDecoder::decode() for offline/test use.
 *
 * Both methods are stateless and thread-safe.
 */

#pragma once

#include "FSK/ale_waveform.h"
#include "Word/ale_word.h"
#include <array>
#include <cstdint>

namespace ale {

/// One transmitted ALE word: 49 8-FSK symbol values (0–7).
using SymbolFrame = std::array<uint8_t, SYMBOLS_PER_WORD>;

class ALEEncoder {
public:
    /**
     * Encode an ALEWord to a SymbolFrame.
     * Calls word.encode() internally to obtain the tx49 representation.
     */
    static SymbolFrame encode(const ALEWord& word);

    /**
     * Encode a raw 49-bit transmitted word (output of ALEFECCodec::interleave_word
     * or ALEWord::encode()) directly to a SymbolFrame.
     * Use when tx49 is already available to avoid redundant FEC computation.
     */
    static SymbolFrame encode_tx49(uint64_t tx49);
};

} // namespace ale
