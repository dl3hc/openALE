/**
 * \file Codec/ale_encoder.cpp
 */

#include "Codec/ale_encoder.h"

namespace ale {

SymbolFrame ALEEncoder::encode_tx49(uint64_t tx49)
{
    // Pack the 49-bit wire word into 49 8-FSK symbol values (MSB-first/symbol).
    //
    // On-air stream = 3 identical copies of tx49 concatenated:
    //   stream[i] = tx49[i % 49]  for i in [0, 147)
    //
    // Symbol k covers stream bits [3k, 3k+1, 3k+2]:
    //   sym[k] = stream[3k]<<2 | stream[3k+1]<<1 | stream[3k+2]
    //
    // Since stream[i] = tx49[i % 49], modulo wraps automatically at k=17
    // (3*17=51 ≡ 2 mod 49) — confirmed against reference modem.c.
    SymbolFrame frame{};
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        uint8_t sym = 0;
        for (uint32_t b = 0; b < BITS_PER_SYMBOL; ++b) {
            const uint32_t bit_pos = (k * BITS_PER_SYMBOL + b) % SYMBOLS_PER_WORD;
            if ((tx49 >> bit_pos) & 1u)
                sym |= static_cast<uint8_t>(1u << (BITS_PER_SYMBOL - 1u - b));
        }
        frame[k] = sym;
    }
    return frame;
}

SymbolFrame ALEEncoder::encode(const ALEWord& word)
{
    return encode_tx49(word.encode());
}

} // namespace ale
