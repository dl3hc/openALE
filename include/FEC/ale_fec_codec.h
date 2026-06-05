/**
 * \file fec/ale_fec_codec.h
 * \brief ALE FEC codec: Golay (24,12) with optional symbol-level interleaving
 *
 * Provides the complete FEC pipeline for ALE 2G per MIL-STD-188-141B:
 *  - Word-level:  Extended Golay (24,12) encode/decode (corrects up to 3 errors)
 *  - Burst-level: Optional symbol interleaving for data channel protection
 *
 * The Protocol / Word layer uses this class exclusively and does not call
 * Golay or Interleaver directly.
 */

#pragma once

#include "FEC/golay.h"
#include "FEC/interleaver.h"
#include <cstdint>

namespace ale {

class ALEFECCodec {
public:
    /**
     * Encode a 12-bit information word to a 24-bit Golay codeword.
     *
     * \param info  12-bit information word (bits 11-0)
     * \return      24-bit codeword [info(12) | parity(12)]
     */
    static uint32_t encode_word(uint16_t info);

    /**
     * Decode and error-correct a 24-bit Golay codeword.
     *
     * \param codeword  24-bit received codeword (after majority voting)
     * \param output    [out] 12-bit corrected information word
     * \return          DecodeResult with flag (DECODE_OK / DECODE_CORRECTED / DECODE_DETECTED)
     *                  and errors_corrected count
     */
    static Golay::DecodeResult decode_word(uint32_t codeword, uint16_t& output);

    /**
     * Interleave symbol bursts for data channel protection.
     * Operates in-place on successive Interleaver::BLOCK_SIZE-symbol blocks.
     * Any trailing symbols beyond the last full block are left unchanged.
     *
     * \param symbols  Symbol buffer
     * \param count    Total number of symbols
     */
    static void interleave_symbols(uint8_t* symbols, uint32_t count);

    /**
     * Deinterleave received symbol bursts (inverse of interleave_symbols).
     *
     * \param symbols  Symbol buffer
     * \param count    Total number of symbols
     */
    static void deinterleave_symbols(uint8_t* symbols, uint32_t count);
};

} // namespace ale
