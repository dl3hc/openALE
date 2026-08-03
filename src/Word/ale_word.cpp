/**
 * \file ale_word.cpp
 * \brief Implementation of ALE word parser
 *
 * Key changes vs. previous revision
 * -----------------------------------
 * AC-WORD-001-5  parse_from_bits accepts and stores timestamp_ms.
 *
 * AC-WORD-002-1  ALE_ASCII_64[] table removed (was dead code).
 *                Two separate predicates replace the single is_valid_ale_char():
 *                  is_valid_basic38_char()   — A.5.2.4.2
 *                  is_valid_expanded64_char() — A.5.7.2.1
 *
 * AC-WORD-002-2  decode_ascii() / encode_ascii() now receive the PreambleType and
 *                select the correct predicate automatically via uses_basic38().
 *
 * AC-WORD-002-3  set_self_address() calls is_valid_basic38_char() (addresses
 *                are always Basic 38).
 *
 * AC-WORD-002-4  match_wildcard() documents and enforces the single-character
 *                '@' wildcard per A.5.2.4.2.  Length must still match because
 *                the spec defines '@' as a positional single-character wildcard,
 *                not a Kleene-star.
 */

#include "Word/ale_word.h"
#include "FEC/ale_fec_codec.h"
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ale {

// Word type names per MIL-STD-188-141B Table A-II
static const char* WORD_TYPE_NAMES[] = {
    "DATA", "THRU", "TO", "TWAS", "FROM", "TIS", "CMD", "REP", "UNKNOWN"
};

// ── ASCII character-set classification table ─────────────────────────────────
// Shared verbatim by both reference decoders (LinuxALE modem.c, ALELite
// ASCII_Set.h): a 128-entry table indexed by ASCII codepoint giving the most
// restrictive ALE character set the byte belongs to.  Adopted here in place of
// two explicit-range predicates — a single data table is the canonical, more
// compact reference-decoder idiom and reads at a glance.
//
//   ASCII_INVALID (0) — not a valid ALE character (control bytes, lowercase, 0x60+)
//   ASCII_64     (1) — member of the Expanded-64 set (0x20-0x5F, minus Basic-38)
//   ASCII_38     (2) — member of the Basic-38 set (A-Z, 0-9, '@', '?')
//
// Derived predicates (see is_valid_basic38_char / is_valid_expanded64_char):
//   basic38    ⇔ ASCII_SET[u] == 2          (A-Z, 0-9, @, ?)
//   expanded64 ⇔ ASCII_SET[u] != 0          (full 0x20-0x5F)
// These are bit-equivalent to the previous explicit-range checks.
static constexpr uint8_t ASCII_INVALID = 0;
static constexpr uint8_t ASCII_64     = 1;
static constexpr uint8_t ASCII_38     = 2;

static constexpr uint8_t ASCII_SET[128] = {
    // 0x00-0x0F: control — invalid
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x10-0x1F: control — invalid
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x20-0x2F: space ! " # $ % & ' ( ) * + , - . /  → Expanded-64 only
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // 0x30-0x3F: 0-9 Basic-38; : ; < = > Expanded-64; ? Basic-38
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 2,
    // 0x40-0x4F: @ Basic-38; A-O Basic-38
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    // 0x50-0x5F: P-Z Basic-38; [ \ ] ^ _ Expanded-64
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
    // 0x60-0x6F: lowercase etc. — invalid (CMD function codes handled separately)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x70-0x7F: invalid
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// ============================================================================
// WordParser
// ============================================================================

WordParser::WordParser() : last_timestamp_ms(0) {}

bool WordParser::parse_from_bits(uint32_t word_bits,
                                  ALEWord& output,
                                  uint32_t timestamp_ms)
{
    output.type         = extract_preamble(word_bits);
    output.raw_payload  = extract_payload(word_bits);
    output.timestamp_ms = timestamp_ms;
    last_timestamp_ms   = timestamp_ms;

    bool ascii_valid = decode_ascii(output.raw_payload, output.type, output.address);

    output.valid = ascii_valid;
    return ascii_valid;
}

PreambleType WordParser::extract_preamble(uint32_t word_bits)
{
    uint8_t preamble = (word_bits >> 21) & 0x07;   // bits 23-21
    // All 3-bit values 0-7 map directly to the enum
    return static_cast<PreambleType>(preamble);
}

uint32_t WordParser::extract_payload(uint32_t word_bits)
{
    return word_bits & 0x1FFFFF;   // bits 20-0
}

bool WordParser::decode_ascii(uint32_t payload,
                               PreambleType  word_type,
                               char      output[4])
{
    // 21 bits = 3 × 7-bit characters
    char c0 = static_cast<char>((payload >> 14) & 0x7F);  // bits 20-14
    char c1 = static_cast<char>((payload >>  7) & 0x7F);  // bits 13-7
    char c2 = static_cast<char>((payload >>  0) & 0x7F);  // bits 6-0

    // CMD words carry function codes defined in TABLE A-XVI (A.5.6), not addresses.
    // A.5.2.6.3 NOTE: CMD validity is determined by "history, status, expectations,
    // and protocol" — not by character-set membership.  CMD payloads combine a
    // function-code char (0x60-0x7E) with BINARY argument fields (LQA KA1/SINAD/
    // BER/MP, noise bytes) and AMD/DTM/DBM text, so no single char set applies; the
    // payload is accepted as-is on Golay success and interpreted by protocol context.
    // A CMD is never transmitted in isolation — it only appears mid-frame after TO,
    // by which time the demodulator grid is already locked — so a CMD seen during
    // cold acquisition is garbage, rejected by the unanimous-vote gate (criterion 1)
    // rather than a char-set filter.  The modem therefore applies NO preamble-type
    // filter for acquisition (see gate_word_); CMD validity is protocol-layer work.
    if (word_type == PreambleType::CMD) {
        output[0] = c0;
        output[1] = c1;
        output[2] = c2;
        output[3] = '\0';
        return true;
    }

    // All other word types: select Basic38 (addresses) or Expanded64 (orderwire data).
    bool (*valid_char)(char) = uses_basic38(word_type)
                               ? is_valid_basic38_char
                               : is_valid_expanded64_char;

    if (!valid_char(c0) || !valid_char(c1) || !valid_char(c2)) {
        output[0] = output[1] = output[2] = '?';
        output[3] = '\0';
        return false;
    }

    output[0] = c0;
    output[1] = c1;
    output[2] = c2;
    output[3] = '\0';
    return true;
}

uint32_t WordParser::encode_ascii(const char chars[3], PreambleType word_type)
{
    bool (*valid_char)(char) = uses_basic38(word_type)
                               ? is_valid_basic38_char
                               : is_valid_expanded64_char;

    if (!valid_char(chars[0]) || !valid_char(chars[1]) || !valid_char(chars[2])) {
        return 0xFFFFFFFF;
    }

    uint32_t payload = 0;
    payload |= (static_cast<uint32_t>(chars[0]) & 0x7F) << 14;
    payload |= (static_cast<uint32_t>(chars[1]) & 0x7F) <<  7;
    payload |= (static_cast<uint32_t>(chars[2]) & 0x7F) <<  0;
    return payload & 0x1FFFFF;
}

// ----------------------------------------------------------------------------
// Character-set predicates
// ----------------------------------------------------------------------------

bool WordParser::is_valid_basic38_char(char ch)
{
    // A.5.2.4.2: A-Z, 0-9, '@', '?'.  The spec warns against checking only the
    // three MSBs (48 combinations would be accepted, 10 of them invalid); the
    // ASCII_SET table encodes the full codepoint membership directly.
    // ALE characters are 7-bit (bits 6-0); a char with bit 7 set (signed char
    // ≥ 0x80) is not an ALE character — guard before indexing the 128-entry table.
    const unsigned int u = static_cast<unsigned char>(ch);
    if (u >= 128) return false;
    return ASCII_SET[u] == ASCII_38;
}

bool WordParser::is_valid_expanded64_char(char ch)
{
    // A.5.7.2.1: digital discrimination by two MSBs b7 and b6 — all members with
    // b7b6 = "01" (0x20-0x3F) or "10" (0x40-0x5F), i.e. the half-open range
    // [0x20, 0x60).  ASCII_SET marks exactly those codepoints non-zero.
    // 7-bit guard as in is_valid_basic38_char (bit-7-set chars are non-ALE).
    const unsigned int u = static_cast<unsigned char>(ch);
    if (u >= 128) return false;
    return ASCII_SET[u] != ASCII_INVALID;
}

bool WordParser::uses_basic38(PreambleType type)
{
    // DATA (0) and REP (7) carry orderwire content → Expanded 64.
    // CMD carries A.5.6 command payloads (function code + binary args) → no char
    //   set, accepted unconditionally in decode_ascii() and interpreted by the
    //   protocol layer.  All other routing/addressing types → Basic 38.
    return (type != PreambleType::DATA) && (type != PreambleType::REP);
}

ALEWord WordParser::make_word(PreambleType type, const char chars[3])
{
    uint32_t payload = encode_ascii(chars, type);
    ALEWord  w;
    if (payload == 0xFFFFFFFF) return w;  // invalid characters
    WordParser p;
    p.parse_from_bits((static_cast<uint32_t>(type) << 21) | payload, w);
    return w;
}

const char* WordParser::word_type_name(PreambleType type)
{
    uint8_t index = static_cast<uint8_t>(type);
    if (index > 7) index = 8;   // maps UNKNOWN (0xFF) to last entry
    return WORD_TYPE_NAMES[index];
}

// ============================================================================
// ALEWord
// ============================================================================

uint64_t ALEWord::encode() const
{
    const uint32_t raw24   = (static_cast<uint32_t>(type) << 21)
                           | (raw_payload & 0x1F'FFFFu);
    const GolayCoded coded = ALEFECCodec::encode_word(raw24);
    return ALEFECCodec::interleave_word(coded);
}

} // namespace ale