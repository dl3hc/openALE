/**
 * \file Codec/ale_decoder.h
 * \brief ALE 2G word decoder: 49 FSK symbol values → ALEWord.
 *
 * Decoding pipeline (A.5.2.2.4 / A.5.2.6.3 / MIL-STD-188-141B).  The step names
 * mirror the standard's word-synchronisation criteria (A.5.2.6.3):
 *
 *   symbols[49] → 2/3 majority voter      → tx49 (49-bit) + unanimous-vote count
 *   tx49        → deinterleave + Golay FEC → word24   (successful "A"/"B" decode)
 *   word24      → parse_from_bits          → ALEWord  (acceptable character bits)
 *
 * Triple redundancy (A.5.2.2.4): each of the 49 transmitted-word bits is carried
 * three times WITHIN the single 49-symbol word — the three copies of bit i sit at
 * on-air bit positions i, i+49 and i+98 of the 147-bit stream those 49 symbols
 * carry (3 bits/symbol, MSB first).  The 2/3 majority voter recovers tx49 and
 * counts the UNANIMOUS positions (all three copies agree).
 *
 * unanimous_votes (A.5.2.6.3 "threshold of unanimous votes in the 2/3 majority
 * voter decoder"): 0..48 (bits 0..47 only; bit 48 / S49 excluded per A.5.2.2.4);
 * a clean, correctly-phased word scores 48.  It is the
 * standard's primary signal-quality / BER and triple-redundant-phase
 * discriminator.  decode() only computes and reports it (and stores it in the
 * ALEWord); the acceptance threshold is applied by the caller (the modem's
 * word-acquisition / grid-lock logic).
 *
 * Both methods are stateless and thread-safe.
 */

#pragma once

#include "FSK/ale_waveform.h"
#include "FEC/golay.h"
#include "Word/ale_word.h"
#include <cstdint>

namespace ale {

class ALEDecoder {
public:
    /**
     * Decode 49 received 8-FSK symbol values (0–7) to an ALE word.
     *
     * \param symbols_49          Pointer to 49 symbol values from Goertzel detection.
     * \param out                 [out] Decoded ALE word on success; its
     *                            unanimous_votes and fec_errors fields are always set.
     * \param fec                 [out] Combined Golay decode result (worst of A/B halves).
     * \param unanimous_votes_out [out, optional] Unanimous 2/3-vote count (0..48,
     *                            bits 0..47; S49 excluded); 48 = clean word (A.5.2.6.3).
     * \param golay_mode          Golay correction power (A.5.2.6.3); default Mode3_4.
     *
     * \return true if Golay produced DECODE_OK or DECODE_CORRECTED AND
     *         WordParser accepted the resulting 24-bit word.
     */
    static bool decode(const uint8_t* symbols_49,
                       ALEWord& out,
                       Golay::DecodeResult& fec,
                       uint8_t* unanimous_votes_out = nullptr,
                       GolayMode golay_mode = GolayMode::Mode3_4);
};

} // namespace ale
