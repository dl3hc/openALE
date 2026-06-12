/**
 * \file Codec/ale_decoder.h
 * \brief ALE 2G word decoder: 49 FSK symbol values → ALEWord.
 *
 * Decoding pipeline (A.5.2.2.4 / A.5.2.6.3 / MIL-STD-188-141B):
 *
 *   symbols[49]  →  unpack 3-bit symbols      →  stream[147] (MSB-first)
 *   stream[147]  →  stride-49 majority vote   →  tx49 (49-bit)
 *   tx49         →  deinterleave + Golay FEC  →  word24
 *   word24       →  WordParser::parse_from_bits →  ALEWord
 *
 * The majority vote recovers the 49-bit tx49 from three end-to-end copies
 * embedded in the 147-bit stream (physical 3× redundancy, A.5.2.2.4).
 *
 * bad_votes (adapted from LinuxALE modem.c, Charles Brain / Ilkka Toivanen):
 *   Count of non-unanimous vote positions (sum == 1 or sum == 2 out of 3).
 *   A clean, well-aligned word has bad_votes = 0.  High values indicate
 *   noise, a misaligned decode window, or a false FEC decode attempt.
 *   The metric is exposed as an optional output to the caller; it does not
 *   gate the decode internally (the grid-lock in ALERxPipeline handles that).
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
     * \param symbols_49    Pointer to 49 symbol values from Goertzel detection.
     * \param out           [out] Decoded ALE word on success.
     * \param fec           [out] Combined Golay decode result (worst of A/B halves).
     * \param bad_votes_out [out, optional] Non-unanimous vote count (0..49).
     *
     * \return true if Golay produced DECODE_OK or DECODE_CORRECTED AND
     *         WordParser accepted the resulting 24-bit word.
     */
    static bool decode(const uint8_t* symbols_49,
                       ALEWord& out,
                       Golay::DecodeResult& fec,
                       uint8_t* bad_votes_out = nullptr);
};

} // namespace ale
