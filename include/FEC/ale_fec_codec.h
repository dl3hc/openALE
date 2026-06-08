/**
 * \file fec/ale_fec_codec.h
 * \brief ALE FEC codec: Golay (24,12) with optional symbol-level interleaving
 *
 * Provides the complete FEC pipeline for ALE 2G per MIL-STD-188-141B.
 *
 * TX pipeline (A.5.2.2.3):
 *   encode_word(raw24)     → GolayCoded   — Golay (24,12) on both halves
 *   interleave_word(coded) → uint64_t     — A/B-interleaving + S49
 *
 * RX pipeline (A.5.2.2.3):
 *   deinterleave_word(tx49, fec) → uint32_t  — deinterleave + Golay decode both halves
 *
 * The Protocol / Word layer uses this class exclusively and does not call
 * Golay, WordInterleaver, or Interleaver directly.
 */

#pragma once

#include "FEC/golay.h"
#include "FEC/interleaver.h"
#include "FEC/word_interleaver.h"
#include <cstdint>

namespace ale {

/**
 * Output of encode_word() and input to interleave_word().
 *
 * Holds two Golay (24,12) codewords for the upper and lower halves of an ALE word.
 * G13..G24 in coder_b are stored in their natural form;
 * interleave_word() inverts them when writing to the B-channel.
 *
 * coder_a: [W1..W12  | G1..G12 ]   — upper half, check bits normal
 * coder_b: [W13..W24 | G13..G24]   — lower half, check bits natural (inverted on TX)
 */
struct GolayCoded {
    uint32_t coder_a;
    uint32_t coder_b;
};

class ALEFECCodec {
public:
    /**
     * Golay-encode a 24-bit ALE word — step 1 of TX pipeline (A.5.2.2.3).
     *
     * Coder A: W1..W12  (bits 23..12) → [W1..W12  | G1..G12 ]  check bits normal
     * Coder B: W13..W24 (bits 11..0 ) → [W13..W24 | G13..G24]  check bits stored natural
     *
     * \param ale_word  24-bit ALE word [W1=bit23 .. W24=bit0]
     * \return          GolayCoded with coder_a and coder_b
     */
    static GolayCoded encode_word(uint32_t ale_word);

    /**
     * Encode a single 12-bit half-word to a 24-bit Golay codeword.
     * Used for direct Golay testing; prefer encode_word() for full ALE words.
     *
     * \param info  12-bit information word (bits 11-0)
     * \return      24-bit codeword [info(12) | parity(12)]
     */
    static uint32_t encode_half(uint16_t info);

    /**
     * Decode and error-correct a single 24-bit Golay codeword.
     *
     * \param codeword  24-bit received codeword (after majority voting)
     * \param output    [out] 12-bit corrected information word
     * \return          DecodeResult (DECODE_OK / DECODE_CORRECTED / DECODE_DETECTED)
     */
    static Golay::DecodeResult decode_word(uint32_t codeword, uint16_t& output);

    /**
     * Interleave symbol bursts for data channel protection.
     * Operates in-place on successive Interleaver::BLOCK_SIZE-symbol blocks.
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
     * Interleave a GolayCoded word into a 49-bit transmitted word — step 2 of TX pipeline.
     *
     * coder_a check bits (G1..G12)  → A-channel parity positions (normal).
     * coder_b check bits (G13..G24) → B-channel parity positions (inverted).
     * Bit 48 of the result is the stuff bit S49 = 0.
     *
     * \param coded  GolayCoded word from encode_word()
     * \return       49-bit transmitted word [A1,B1, ..., A24,B24, S49]
     */
    static uint64_t interleave_word(const GolayCoded& coded);

    /**
     * Deinterleave and Golay-decode a 49-bit received word — full RX pipeline.
     *
     * W1..W12  are corrected using Coder A's parity (G1..G12,  A-channel).
     * W13..W24 are corrected using Coder B's parity (G13..G24, B-channel, uninverted).
     * Bit 48 (S49) is ignored per AC-FEC-013-4.
     * fec_out reflects the worst outcome of both decoders.
     *
     * \param transmitted  49-bit received word
     * \param fec_out      [out] combined Golay decode result
     * \return             24-bit corrected ALE word
     */
    static uint32_t deinterleave_word(uint64_t transmitted, Golay::DecodeResult& fec_out);
};

} // namespace ale
