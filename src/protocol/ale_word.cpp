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

ALEWord WordParser::make_word(WordType type, const char chars[3])
{
    uint32_t payload = encode_ascii(chars, type);
    ALEWord  w;
    if (payload == 0xFFFFFFFF) return w;  // invalid characters
    WordParser p;
    p.parse_from_bits((static_cast<uint32_t>(type) << 21) | payload, w);
    return w;
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
        if (n.first == address) return true;
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

// ============================================================================
// FrameValidator
// ============================================================================

bool FrameValidator::from_count_valid(const std::vector<ALEWord>& words)
{
    int count = 0;
    for (const auto& w : words) {
        if (w.type == WordType::FROM) ++count;
    }
    return count <= 1;
}

bool FrameValidator::from_precedes_cmd_only(const std::vector<ALEWord>& words)
{
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].type != WordType::FROM) continue;
        // Skip optional DATA/REP address extension words
        size_t j = i + 1;
        while (j < words.size() &&
               (words[j].type == WordType::DATA || words[j].type == WordType::REP)) {
            ++j;
        }
        // FROM (+ optional DATA/REP) must be immediately followed by CMD
        if (j >= words.size() || words[j].type != WordType::CMD) {
            return false;
        }
    }
    return true;
}

bool FrameValidator::thru_in_scanning_section_only(const std::vector<ALEWord>& words)
{
    bool past_scanning = false;
    for (const auto& w : words) {
        switch (w.type) {
            case WordType::TO:
            case WordType::TIS:
            case WordType::TWS:
            case WordType::FROM:
            case WordType::CMD:
                past_scanning = true;
                break;
            case WordType::THRU:
                if (past_scanning) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

bool FrameValidator::thru_rep_alternates(const std::vector<ALEWord>& scanning_words)
{
    // Scanning section must consist of complete THRU, REP pairs.
    // expect_thru starts true; after each complete pair it returns to true.
    // Returning true requires expect_thru == true (all pairs complete).
    bool expect_thru = true;
    for (const auto& w : scanning_words) {
        if (w.type != WordType::THRU && w.type != WordType::REP) continue;
        if (expect_thru  && w.type != WordType::THRU) return false;
        if (!expect_thru && w.type != WordType::REP)  return false;
        expect_thru = !expect_thru;
    }
    return expect_thru; // true only after an even number of THRU/REP words
}

bool FrameValidator::group_call_target_count_valid(const std::vector<ALEWord>& scanning_words)
{
    // Accumulate distinct THRU target addresses; maximum is 5 per spec.
    std::vector<std::string> seen;
    for (const auto& w : scanning_words) {
        if (w.type != WordType::THRU) continue;
        std::string addr(w.address, 3);
        bool found = false;
        for (const auto& s : seen) {
            if (s == addr) { found = true; break; }
        }
        if (!found) {
            if (seen.size() >= 5) return false;
            seen.push_back(addr);
        }
    }
    return true;
}

// ============================================================================
// CMD/DATA/REP Frame Validation
// ============================================================================

bool FrameValidator::message_sections_begin_with_cmd(const std::vector<ALEWord>& words)
{
    // Check that every message section (after a CMD) starts with CMD
    // A message section is a sequence that starts with CMD and continues until
    // another CMD or end of frame
    bool in_message_section = false;
    
    for (const auto& word : words) {
        if (word.type == WordType::CMD) {
            // CMD can start a new message section
            in_message_section = true;
        } else if (in_message_section && 
                   (word.type == WordType::TO || word.type == WordType::TWS || 
                    word.type == WordType::FROM || word.type == WordType::TIS ||
                    word.type == WordType::DATA || word.type == WordType::REP)) {
            // Continue in message section - valid word types
            continue;
        } else if (in_message_section && 
                   (word.type == WordType::THRU || word.type == WordType::UNKNOWN)) {
            // Invalid word type in message section
            return false;
        } else if (in_message_section && word.type == WordType::CMD) {
            // CMD can start a new message section
            continue;
        } else if (in_message_section) {
            // Any other word type ends the message section
            in_message_section = false;
        }
    }
    
    return true;
}

bool FrameValidator::first_cmd_begins_message_section(const std::vector<ALEWord>& words)
{
    // The first CMD word in a frame should be at the beginning of the frame
    // (i.e., it should not be preceded by any non-CMD words)
    if (words.empty()) return true;
    
    // Find first CMD word
    for (const auto& word : words) {
        if (word.type == WordType::CMD) {
            // If this is the first word in the frame, it begins the message section
            return true;
        } else if (word.type != WordType::UNKNOWN) {
            // If we encounter any non-CMD word before the first CMD, 
            // then the first CMD doesn't begin a message section
            return false;
        }
    }
    
    // No CMD found, so it's not a problem
    return true;
}

bool FrameValidator::rep_not_followed_by_self_tis_twas(const std::vector<ALEWord>& words)
{
    // REP must not follow itself, TIS, or TWAS
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == WordType::REP) {
            // Check if previous word is REP, TIS, or TWAS
            if (words[i-1].type == WordType::REP ||
                words[i-1].type == WordType::TIS ||
                words[i-1].type == WordType::TWS) {
                return false;
            }
        }
    }
    return true;
}

bool FrameValidator::rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words)
{
    // This is a complex validation that requires detailed frame analysis
    // For now, we'll return true to indicate it's a placeholder
    // In a full implementation, this would analyze the frame structure to 
    // detect multiple sender situations where REP is not appropriate
    return true;
}

} // namespace ale