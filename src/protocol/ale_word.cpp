/**
 * \file ale_word.cpp
 * \brief Implementation of ALE word parser
 *
 * Key changes vs. previous revision
 * -----------------------------------
 * AC-WORD-001-5  parse_from_bits / parse_word now accept and store timestamp_ms.
 *
 * AC-WORD-002-1  ALE_ASCII_64[] table removed (was dead code).
 *                Two separate predicates replace the single is_valid_ale_char():
 *                  is_valid_basic38_char()   — A.5.2.4.2
 *                  is_valid_expanded64_char() — A.5.7.2.1
 *
 * AC-WORD-002-2  decode_ascii() / encode_ascii() now receive the WordType and
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

#include "ale_word.h"
#include "symbol_decoder.h"
#include "golay.h"
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ale {

// Word type names per MIL-STD-188-141B Table A-II
static const char* WORD_TYPE_NAMES[] = {
    "DATA", "THRU", "TO", "TWS", "FROM", "TIS", "CMD", "REP", "UNKNOWN"
};

// ============================================================================
// WordParser
// ============================================================================

WordParser::WordParser() : last_timestamp_ms(0) {}

bool WordParser::parse_word(const uint8_t symbols[SYMBOLS_PER_WORD],
                             ALEWord& output,
                             uint32_t timestamp_ms)
{
    // Step 1: Decode 49 symbols with majority voting → 24-bit word
    uint32_t raw_word = 0;
    SymbolDecoder::decode_word_with_voting(symbols, raw_word);

    // Step 2: Apply Golay FEC (symbol-level error correction)
    uint16_t decoded_info = 0;
    uint8_t  fec_errors   = Golay::decode(raw_word, decoded_info);

    if (fec_errors == 0xFF) {
        output.valid = false;
        return false;
    }

    output.fec_errors = fec_errors;

    // Step 3: The 24 voted bits ARE the ALE word; parse preamble + payload
    return parse_from_bits(raw_word & 0x00FFFFFF, output, timestamp_ms);
}

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

WordType WordParser::extract_preamble(uint32_t word_bits)
{
    uint8_t preamble = (word_bits >> 21) & 0x07;   // bits 23-21
    // All 3-bit values 0-7 map directly to the enum
    return static_cast<WordType>(preamble);
}

uint32_t WordParser::extract_payload(uint32_t word_bits)
{
    return word_bits & 0x1FFFFF;   // bits 20-0
}

bool WordParser::decode_ascii(uint32_t payload,
                               WordType  word_type,
                               char      output[4])
{
    // 21 bits = 3 × 7-bit characters
    char c0 = static_cast<char>((payload >> 14) & 0x7F);  // bits 20-14
    char c1 = static_cast<char>((payload >>  7) & 0x7F);  // bits 13-7
    char c2 = static_cast<char>((payload >>  0) & 0x7F);  // bits 6-0

    // Select the correct character-set validator for this word type
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

uint32_t WordParser::encode_ascii(const char chars[3], WordType word_type)
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
    // A.5.2.4.2: A-Z, 0-9, '@', '?'
    // Note: the spec warns against checking only the three MSBs because
    // 48 combinations would be accepted (10 invalid symbols included).
    // We therefore check the full codepoint explicitly.
    if (ch >= 'A' && ch <= 'Z') return true;
    if (ch >= '0' && ch <= '9') return true;
    if (ch == '@' || ch == '?') return true;
    return false;
}

bool WordParser::is_valid_expanded64_char(char ch)
{
    // A.5.7.2.1: digital discrimination by two MSBs b7 and b6.
    // All members with b7b6 = "01" (0x20-0x3F) or "10" (0x40-0x5F) are valid.
    // Equivalently: bit 7 must be 0 AND (bit 6 OR bit 5) must be 1,
    // which resolves to the half-open range [0x20, 0x60).
    uint8_t u = static_cast<uint8_t>(ch);
    return (u >= 0x20) && (u < 0x60);
}

bool WordParser::uses_basic38(WordType type)
{
    // DATA (0) and REP (7) carry orderwire content → Expanded 64
    // All other preamble types carry addresses → Basic 38
    return (type != WordType::DATA) && (type != WordType::REP);
}

const char* WordParser::word_type_name(WordType type)
{
    uint8_t index = static_cast<uint8_t>(type);
    if (index > 7) index = 8;   // maps UNKNOWN (0xFF) to last entry
    return WORD_TYPE_NAMES[index];
}

// ============================================================================
// AddressBook
// ============================================================================

AddressBook::AddressBook() {}

bool AddressBook::set_self_address(const std::string& address)
{
    // Addresses are always Basic 38 per A.5.2.4.2
    if (address.length() < 3 || address.length() > 15) {
        return false;
    }
    for (char ch : address) {
        if (!WordParser::is_valid_basic38_char(ch)) {
            return false;
        }
    }
    self_address = address;
    return true;
}

void AddressBook::add_station(const std::string& address, const std::string& name)
{
    for (const auto& s : stations) {
        if (s.first == address) return;
    }
    stations.push_back({address, name});
}

void AddressBook::add_net(const std::string& net_address,
                           const std::string& description)
{
    for (const auto& n : nets) {
        if (n.first == net_address) return;
    }
    nets.push_back({net_address, description});
}

bool AddressBook::is_self(const std::string& address) const
{
    return address == self_address;
}

bool AddressBook::is_known_station(const std::string& address) const
{
    for (const auto& s : stations) {
        if (s.first == address) return true;
    }
    return false;
}

bool AddressBook::is_known_net(const std::string& address) const
{
    for (const auto& n : nets) {
        if (n.first == net_address) return true;
    }
    return false;
}

bool AddressBook::match_wildcard(const std::string& pattern,
                                  const std::string& address)
{
    // Per A.5.2.4.2 '@' is a single-character positional wildcard within
    // the Basic 38 set.  Pattern and address must therefore have equal length.
    if (pattern.length() != address.length()) {
        return false;
    }

    for (size_t i = 0; i < pattern.length(); ++i) {
        if (pattern[i] == '@') {
            // '@' matches any single Basic 38 character
            if (!WordParser::is_valid_basic38_char(address[i])) {
                return false;
            }
        } else if (pattern[i] != address[i]) {
            return false;
        }
    }
    return true;
}

} // namespace ale
