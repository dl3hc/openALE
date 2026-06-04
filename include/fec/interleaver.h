/**
 * \file fec/interleaver.h
 * \brief Symbol-level block interleaver for ALE data channel protection
 *
 * Implements a rectangular block interleaver to spread burst errors across
 * multiple Golay codewords.  Write order: row-by-row; read order: column-by-column.
 *
 * Fixed parameters for ALE 2G data channel:
 *  - Depth (rows):   8 symbols
 *  - Width (cols):  24 symbols
 *  - Block size:   192 symbols
 *
 * Specification: MIL-STD-188-141B optional data channel FEC
 */

#pragma once

#include <cstdint>

namespace ale {

class Interleaver {
public:
    static constexpr uint32_t DEPTH      = 8;            ///< Interleaver depth (rows)
    static constexpr uint32_t WIDTH      = 24;           ///< Interleaver width (columns)
    static constexpr uint32_t BLOCK_SIZE = DEPTH * WIDTH; ///< Symbols per interleaver block

    /**
     * Interleave a block of symbols in-place.
     * Written row-by-row, read column-by-column.
     *
     * \param symbols Buffer of at least BLOCK_SIZE symbols; only the first
     *                BLOCK_SIZE symbols are processed.
     * \param count   Total number of symbols in the buffer (ignored beyond BLOCK_SIZE).
     */
    static void interleave(uint8_t* symbols, uint32_t count);

    /**
     * Deinterleave a block of symbols in-place (inverse of interleave).
     *
     * \param symbols Buffer of at least BLOCK_SIZE symbols.
     * \param count   Total number of symbols in the buffer.
     */
    static void deinterleave(uint8_t* symbols, uint32_t count);
};

} // namespace ale
