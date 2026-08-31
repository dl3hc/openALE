/**
 * \file Protocol/Message/ale_orderwire_protocols.h
 * \brief ALE orderwire message protocols (MIL-STD-188-141B A.5.7)
 *
 * Encoding for Basic Orderwire message types transmitted in the Message
 * section of an ALE calling frame (Ion2G-style AMD, see
 * ALEStateMachine::enqueue_call_sequence_()) or in the ORDERWIRE sub-phase
 * of an established link (LinkedPhase::ORDERWIRE).
 *
 *   AMD — Automatic Message Display (A.5.7.2)
 *   DTM — Data Text Message        (A.5.7.3)
 *   DBM — Data Block Message       (A.5.7.4)
 */

#pragma once
#include "Word/ale_word.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ale {

/**
 * \enum LinkedPhase
 * Sub-states within the LINKED state (A.5.5.3.5).
 *
 *   IDLE       — Link established, no active orderwire session.
 *   ORDERWIRE  — Orderwire message exchange in progress (A.5.7).
 *                AMD/DTM/DBM may only be initiated from this sub-state
 *                (AMD in the calling frame, before any link exists, is an
 *                exception — see ALEStateMachine::enqueue_call_sequence_()).
 */
enum class LinkedPhase {
    IDLE,       ///< No active orderwire session
    ORDERWIRE,  ///< Orderwire exchange active (A.5.7)
};

/**
 * Encode an AMD text message into ALE words (A.5.7.2.2 word layout).
 *
 * Frame layout:
 *   Word 0 : CMD AMD  — preamble=CMD, Expanded-64 payload (first 3 chars)
 *   Word 1 : DATA     — chars 4–6
 *   Word 2 : REP      — chars 7–9
 *   Word 3 : DATA     — chars 10–12  (alternating DATA/REP)
 *   …
 *
 * Rules:
 * - Characters outside Expanded-64 (0x20–0x5F) are replaced with '?'.
 * - The last triplet is padded with SP (0x20) if 1 or 2 chars are missing
 *   (no operator intervention required).
 * - Maximum 30 words / 90 characters (A.5.7.2.3); excess is silently truncated.
 * - Returns an empty vector if \p text is empty.
 *
 * This function is the single authoritative AMD encoder for the stack.
 * ALEStateMachine::enqueue_call_sequence_() delegates to it exclusively (Ion2G-
 * style calling-frame AMD) — never in the scanning or leading-address sections,
 * and never in the ACK/third-handshake frame, which is purely the caller's
 * link/no-link (TIS/TWAS) decision — see ALEStateMachine::build_ack_words().
 *
 * \param text  ASCII text to encode (up to 90 characters).
 * \return      Ordered list of ALEWords for the Message section.
 */
std::vector<ALEWord> encode_amd(const std::string& text);

// ── DTM — Data Text Message (A.5.7.3) ────────────────────────────────────────

/**
 * DTM block: structured input for DTM encoding (A.5.7.3).
 */
struct DtmBlock {
    std::string text;               ///< ASCII text payload (up to 90 characters)
    bool        crc_enabled   = false; ///< Append CRC-16 word (A.5.7.3)
    bool        arq_requested = false; ///< ARQ exchange requested (reserved)
};

/**
 * Encode a DTM message into ALE words (A.5.7.3).
 *
 * Frame layout:
 *   Word 0   : CMD DTM  — CMD preamble, Basic-38 identifier "DTM"
 *   Word 1   : DATA     — first 3 chars of data (Expanded-64)
 *   Word 2   : REP      — next 3 chars (Expanded-64)
 *   …        — alternating DATA/REP
 *   [Last]   : DATA/REP — CRC-16/CCITT encoded in 3 Expanded-64 chars (if crc_enabled)
 *
 * Rules:
 * - Characters outside Expanded-64 (0x20–0x5F) are replaced with '?'.
 * - Partial last data triplet is padded with SP (0x20).
 * - Maximum 30 data words (90 data chars); excess is silently truncated.
 * - Always produces at least one word (CMD DTM) even for empty text.
 * - CRC word uses DATA or REP preamble (whichever avoids a consecutive duplicate).
 *
 * \param text         ASCII text to encode (up to 90 characters).
 * \param crc_enabled  Append CRC-16 word if true.
 * \return             Ordered list of ALEWords.
 */
std::vector<ALEWord> encode_dtm(const std::string& text, bool crc_enabled = false);

// ── DBM — Data Block Message (A.5.7.4) ────────────────────────────────────────

/**
 * DBM block: structured input for DBM encoding (A.5.7.4).
 *
 * DBM supports transparent binary data — any byte value 0x00–0xFF is valid,
 * unlike AMD/DTM which are restricted to Expanded-64 (0x20–0x5F).
 * CRC-16/CCITT over the payload is appended when crc_enabled is true.
 */
struct DbmBlock {
    std::vector<uint8_t> payload;        ///< Raw binary payload (transparent, any byte value)
    uint16_t             crc16    = 0;   ///< CRC-16/CCITT result (filled by encode_dbm)
    bool                 buffered = true; ///< Block is fully buffered before transmission (A.5.7.4)
};

/**
 * Encode a DBM message into ALE words (A.5.7.4).
 *
 * Frame layout:
 *   Word 0   : CMD DBM  — CMD preamble, Basic-38 identifier "DBM"
 *   Word 1   : DATA     — first 3 bytes of binary payload
 *   Word 2   : REP      — next 3 bytes
 *   …        — alternating DATA/REP
 *   [Last]   : DATA/REP — CRC-16 high byte, low byte, 0x00 padding (if crc_enabled)
 *
 * Binary transparency: each payload byte is stored in a 7-bit ALE character slot.
 * The MSB of bytes > 0x7F is masked off (& 0x7F); partial last triplets are
 * zero-padded.  Returns exactly one word (CMD DBM) for an empty payload.
 *
 * \param payload      Raw binary data (any byte values 0x00–0xFF).
 * \param crc_enabled  Append CRC-16/CCITT word after data (default true; mandatory per A.5.7.4).
 * \return             Ordered list of ALEWords for the Message section.
 */
std::vector<ALEWord> encode_dbm(const std::vector<uint8_t>& payload, bool crc_enabled = true);

// ── CMD word function decode (MIL-STD-188-141B A.5.6 / TABLE A-XVI) ──────────

/**
 * Decoded function of a received CMD word, per TABLE A-XVI (Summary of CMD
 * functions).
 *
 * Word layout (24-bit ALE word, W1-MSB .. W24-LSB):
 *   W1-W3   CMD preamble (110)
 *   W4-W10  first character, 7-bit ASCII (CMD function range 0x60-0x7F)
 *   W11-W17 second character, 7-bit ASCII (two-character functions only)
 *   W18-W24 / W11-W24  function payload bits
 *
 * One-character functions cover the whole 0x60-0x7F range ('`' through '~');
 * the groups 'm' (mode selection), 't' (scheduling) and 'v' (capabilities/
 * version) carry a second character. 'x'/'y'/'z'/'{' are CRC words whose
 * remaining bits are FCS bits (A.5.6.1). Below 0x60 lies the message-word
 * range: AMD may use any Expanded-64 first character, and DTM/DBM are
 * identified by their Basic-38 text.
 */
struct CmdFunctionInfo {
    const char* name   = nullptr; ///< Function name, or nullptr when the first
                                  ///< character has no TABLE A-XVI entry
    char        first  = 0;       ///< First character code (W4-W10)
    char        second = 0;       ///< Second character code (W11-W17) for the
                                  ///< two-character groups; 0 otherwise
};

/**
 * Decode a CMD word's raw 21-bit payload into its TABLE A-XVI function.
 * Works purely from the payload bits (same rationale as cmd_char_code():
 * the 0x60-0x7F character codes fail Basic-38/Expanded-64 validation in
 * parse_from_bits(), so word.address[] is not usable for them).
 */
CmdFunctionInfo decode_cmd_function(uint32_t raw_payload);

} // namespace ale
