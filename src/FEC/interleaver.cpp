/**
 * \file fec/interleaver.cpp
 * \brief Symbol-level block interleaver implementation
 */

#include "FEC/interleaver.h"
#include <array>
#include <cstring>

namespace ale {

// Rectangular block interleaver: write row-by-row, read column-by-column.
// A burst of DEPTH consecutive errors becomes DEPTH errors separated by
// WIDTH positions after deinterleaving, which Golay (t=3) can handle
// provided the separation is sufficient.

void Interleaver::interleave(uint8_t* symbols, uint32_t count)
{
    if (count < BLOCK_SIZE) return;

    std::array<uint8_t, BLOCK_SIZE> buf;
    for (uint32_t col = 0; col < WIDTH; ++col) {
        for (uint32_t row = 0; row < DEPTH; ++row) {
            buf[col * DEPTH + row] = symbols[row * WIDTH + col];
        }
    }
    std::memcpy(symbols, buf.data(), BLOCK_SIZE);
}

void Interleaver::deinterleave(uint8_t* symbols, uint32_t count)
{
    if (count < BLOCK_SIZE) return;

    std::array<uint8_t, BLOCK_SIZE> buf;
    for (uint32_t col = 0; col < WIDTH; ++col) {
        for (uint32_t row = 0; row < DEPTH; ++row) {
            buf[row * WIDTH + col] = symbols[col * DEPTH + row];
        }
    }
    std::memcpy(symbols, buf.data(), BLOCK_SIZE);
}

} // namespace ale
