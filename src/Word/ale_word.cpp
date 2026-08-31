/**
 * \file ale_word.cpp
 * \brief ALE word parser implementation
 *
 * AC-WORD-001-5: parse_from_bits takes/stores timestamp_ms.
 * AC-WORD-002-1: removed dead ALE_ASCII_64[]; is_valid_ale_char() split into
 *   is_valid_basic38_char() (A.5.2.4.2) and is_valid_expanded64_char() (A.5.7.2.1).
 * AC-WORD-002-2: decode_ascii()/encode_ascii() take PreambleType, pick predicate via uses_basic38().
 * AC-WORD-002-3: set_self_address() uses is_valid_basic38_char() (addresses always Basic 38).
 * AC-WORD-002-4: match_wildcard(): '@' is a single-char wildcard per A.5.2.4.2,
 *   not a Kleene-star — length must still match.
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

// 128-entry table indexed by codepoint -> most restrictive ALE set the byte
// belongs to. Matches reference decoders (LinuxALE modem.c, ALELite
// ASCII_Set.h); replaces two range predicates with one table, bit-equivalent.
//   0=ASCII_INVALID: control/lowercase/0x60+, not ALE
//   1=ASCII_64: Expanded-64 (0x20-0x5F minus Basic-38)
//   2=ASCII_38: Basic-38 (A-Z, 0-9, '@', '?')
// basic38 ⇔ ASCII_SET[u]==2; expanded64 ⇔ ASCII_SET[u]!=0
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
    // 0x40-0x4F: @, A-O → Basic-38
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
    // values 0-7 map directly to enum
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

    // CMD = function code (TABLE A-XVI/A.5.6), not address. A.5.2.6.3: validity
    // judged by history/status/expectations/protocol, not char-set — mixes
    // func-code char (0x60-0x7E), BINARY args (LQA KA1/SINAD/BER/MP, noise
    // bytes), AMD/DTM/DBM text; no single set fits. Accepted as-is on Golay
    // success, interpreted by protocol layer. Only appears mid-frame after TO
    // (grid locked), so CMD during cold acquisition is garbage — rejected by
    // unanimous-vote gate (criterion 1), not char-set. Hence no preamble-type
    // filter at acquisition (see gate_word_); CMD validity is protocol work.
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
    // A.5.2.4.2: A-Z,0-9,'@','?'. Spec warns 3-MSB-only check accepts 48
    // combos (10 invalid); ASCII_SET encodes full codepoint membership instead.
    // ALE chars are 7-bit; a bit-7-set char (≥0x80) is non-ALE — guard before indexing.
    const unsigned int u = static_cast<unsigned char>(ch);
    if (u >= 128) return false;
    return ASCII_SET[u] == ASCII_38;
}

bool WordParser::is_valid_expanded64_char(char ch)
{
    // A.5.7.2.1: b7b6="01"(0x20-0x3F) or "10"(0x40-0x5F) → range [0x20,0x60);
    // ASCII_SET marks these non-zero. Same 7-bit guard as is_valid_basic38_char.
    const unsigned int u = static_cast<unsigned char>(ch);
    if (u >= 128) return false;
    return ASCII_SET[u] != ASCII_INVALID;
}

bool WordParser::uses_basic38(PreambleType type)
{
    // DATA/REP → orderwire content → Expanded64. CMD → A.5.6 payload
    // (func code+binary args), no char set, accepted unconditionally in
    // decode_ascii(), interpreted by protocol layer. All other types → Basic38.
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