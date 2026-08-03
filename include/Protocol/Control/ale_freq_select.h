/**
 * \file ale_freq_select.h
 * \brief Enhanced Frequency-Select (EFS) — CMD 'f' post-link bilateral negotiation.
 *
 * Encodes and decodes the standard 2-word CMD 'f' frequency-select sequence
 * per MIL-STD-188-141B A.5.6.3.2, used post-link for bilateral channel negotiation.
 *
 * Wire format (2 words total, sent as linked orderwire + TIS:SELF):
 *   Word 1  CMD 'f': preamble=110 | 'f'=1100110 | control=000000 | 100Hz BCD=0 | 10Hz BCD=0
 *   Word 2  DATA   : preamble=000 | W4=0 | 10MHz BCD | 1MHz BCD | 100kHz BCD | 10kHz BCD | 1kHz BCD
 *
 * Semantics:
 *   freq_hz > 0  → proposal or accept (echo same frequency)
 *   freq_hz == 0 → reject (DATA word all zeros = impossible real frequency)
 *
 * Spec reference: MIL-STD-188-141B A.5.6.3.2, TABLE A-XVI CMD 'f' = Frequency.
 */

#pragma once
#include "Word/ale_word.h"
#include <cstdint>
#include <vector>

namespace ale {

// ── Encoding ─────────────────────────────────────────────────────────────────

/**
 * Encode a frequency (Hz) into the 21-bit DATA-word payload (A.5.6.3.2).
 * Layout: [20]=0 | [19:16]=10MHz BCD | [15:12]=1MHz BCD |
 *         [11:8]=100kHz BCD | [7:4]=10kHz BCD | [3:0]=1kHz BCD
 * Sub-kHz digits are discarded (ALE resolution = 1 kHz).
 * freq_hz == 0 → all-zero payload (used as reject sentinel).
 */
uint32_t encode_freq_data_word(uint32_t freq_hz);

/**
 * Decode a 21-bit DATA-word payload back to frequency in Hz.
 * Returns 0 for the all-zero payload (reject sentinel).
 */
uint32_t decode_freq_data_word(uint32_t raw21);

/**
 * Return the 21-bit CMD-word payload for CMD 'f' with absolute control field
 * and zero sub-Hz digits (A.5.6.3.2, control=000000).
 * Layout: [20:14]='f'=1100110 | [13:8]=000000 | [7:0]=0
 */
uint32_t encode_freq_select_cmd_word();

/**
 * Build the complete 2-word transmission sequence for a frequency proposal,
 * accept, or reject:
 *   [CMD 'f' word] [DATA word(freq_hz)]
 * freq_hz == 0 → reject sequence (DATA payload all zero).
 *
 * The SM's trigger_linked_orderwire() appends TIS:SELF before transmitting.
 */
std::vector<ALEWord> build_freq_select_sequence(uint32_t freq_hz);

// ── Detection helper ──────────────────────────────────────────────────────────

/**
 * Return the CMD character code from a CMD-type word's raw_payload.
 * CMD character codes (e.g. 'a', 'f', 'n') are in the b7b6="11" ASCII range
 * (0x60-0x7F) and therefore fail Basic38/Expanded64 validation in parse_from_bits().
 * Use this function instead of word.address[0] for correct over-the-air detection.
 */
inline uint8_t cmd_char_code(const ALEWord& w) {
    return static_cast<uint8_t>((w.raw_payload >> 14) & 0x7Fu);
}

} // namespace ale
