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
#include "FEC/word_interleaver.h"
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

    /**
     * Interleave a 24-bit ALE word into a 49-bit transmitted word (A.5.2.2.3).
     *
     * Computes Golay parity from bits 23..12 (W1..W12) internally.
     * Bit 48 of the result is the stuff bit S49 = 0.
     *
     * \param ale_word  24-bit ALE word [W1=bit23 .. W24=bit0]
     * \return          49-bit transmitted word [A1,B1, ..., A24,B24, S49]
     */
    static uint64_t interleave_word(uint32_t ale_word);

    /**
     * Deinterleave a 49-bit transmitted word and apply Golay error correction.
     *
     * W1..W12 are corrected using the extracted Golay parity.
     * W13..W24 are passed through unchanged.
     * Bit 48 (S49) is ignored.
     *
     * \param transmitted  49-bit received word
     * \param fec_out      [out] Golay decode result (flag + errors_corrected)
     * \return             24-bit corrected ALE word
     */
    static uint32_t deinterleave_word(uint64_t transmitted, Golay::DecodeResult& fec_out);
};

} // namespace ale
