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
#include "FSK/symbol_decoder.h"
#include "FEC/ale_fec_codec.h"
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ale {

// Word type names per MIL-STD-188-141B Table A-II
static const char* WORD_TYPE_NAMES[] = {
    "DATA", "THRU", "TO", "TWAS", "FROM", "TIS", "CMD", "REP", "UNKNOWN"
};

// ============================================================================
// WordParser
// ============================================================================

WordParser::WordParser() : last_timestamp_ms(0) {}

bool WordParser::parse_word(const WordVoteBuffer& symbols,
                             ALEWord& output,
                             uint32_t timestamp_ms)
{
    // Step 1: majority voting across 3 word repetitions → 49-bit transmitted word
    uint64_t transmitted = 0;
    output.unanimous_votes = SymbolDecoder::decode_word_with_voting(symbols, transmitted);

    // A.5.2.6.3: reject word if vote quality is below threshold (too many disagreements)
    if (output.unanimous_votes < VOTE_THRESHOLD_BAD) {
        output.valid = false;
        return false;
    }

    // Step 2: deinterleave per A.5.2.2.3 + Golay error correction → 24-bit ALE word
    Golay::DecodeResult fec;
    const uint32_t ale_word = ALEFECCodec::deinterleave_word(transmitted, fec);

    if (fec.flag == Golay::DECODE_DETECTED) {
        output.valid = false;
        return false;
    }

    output.fec_errors = fec.errors_corrected;

    // Step 3: parse preamble + payload from the corrected ALE word
    return parse_from_bits(ale_word, output, timestamp_ms);
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

bool WordParser::uses_basic38(PreambleType type)
{
    // DATA (0) and REP (7) carry orderwire content → Expanded 64
    // All other preamble types carry addresses → Basic 38
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
// FrameValidator
// ============================================================================

bool FrameValidator::from_count_valid(const std::vector<ALEWord>& words)
{
    int count = 0;
    for (const auto& w : words) {
        if (w.type == PreambleType::FROM) ++count;
    }
    return count <= 1;
}

bool FrameValidator::from_precedes_cmd_only(const std::vector<ALEWord>& words)
{
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].type != PreambleType::FROM) continue;
        // Skip optional DATA/REP address extension words
        size_t j = i + 1;
        while (j < words.size() &&
               (words[j].type == PreambleType::DATA || words[j].type == PreambleType::REP)) {
            ++j;
        }
        // FROM (+ optional DATA/REP) must be immediately followed by CMD
        if (j >= words.size() || words[j].type != PreambleType::CMD) {
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
            case PreambleType::TO:
            case PreambleType::TIS:
            case PreambleType::TWAS:
            case PreambleType::FROM:
            case PreambleType::CMD:
                past_scanning = true;
                break;
            case PreambleType::THRU:
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
        if (w.type != PreambleType::THRU && w.type != PreambleType::REP) continue;
        if (expect_thru  && w.type != PreambleType::THRU) return false;
        if (!expect_thru && w.type != PreambleType::REP)  return false;
        expect_thru = !expect_thru;
    }
    return expect_thru; // true only after an even number of THRU/REP words
}

bool FrameValidator::group_call_target_count_valid(const std::vector<ALEWord>& scanning_words)
{
    // Accumulate distinct THRU target addresses; maximum is 5 per spec.
    std::vector<std::string> seen;
    for (const auto& w : scanning_words) {
        if (w.type != PreambleType::THRU) continue;
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

bool FrameValidator::cmd_not_before_address_section(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-3: CMD must only appear after the address section has started.
    // Any CMD that precedes the first TO/FROM/TIS/TWAS word is a violation.
    bool address_seen = false;
    for (const auto& word : words) {
        switch (word.type) {
            case PreambleType::TO:
            case PreambleType::FROM:
            case PreambleType::TIS:
            case PreambleType::TWAS:
                address_seen = true;
                break;
            case PreambleType::CMD:
                if (!address_seen) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

bool FrameValidator::cmd_has_call_and_conclusion(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-4: a frame with CMD must have a preceding call and a following
    // conclusion.  Find the first CMD and check both sides.
    size_t cmd_idx = words.size();
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].type == PreambleType::CMD) { cmd_idx = i; break; }
    }
    if (cmd_idx == words.size()) return true;  // no CMD — no constraint

    bool has_call = false;
    for (size_t i = 0; i < cmd_idx; ++i) {
        if (words[i].type == PreambleType::TO   ||
            words[i].type == PreambleType::FROM  ||
            words[i].type == PreambleType::TIS   ||
            words[i].type == PreambleType::TWAS) {
            has_call = true;
            break;
        }
    }
    if (!has_call) return false;

    for (size_t i = cmd_idx + 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::TIS || words[i].type == PreambleType::TWAS)
            return true;
    }
    return false;
}

bool FrameValidator::message_sections_begin_with_cmd(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-1: THRU must not appear inside the message section (after CMD).
    bool in_message_section = false;
    for (const auto& word : words) {
        if (word.type == PreambleType::CMD) {
            in_message_section = true;
        } else if (in_message_section &&
                   (word.type == PreambleType::THRU || word.type == PreambleType::UNKNOWN)) {
            return false;
        }
    }
    return true;
}

bool FrameValidator::first_cmd_begins_message_section(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-5: the first CMD marks the boundary between the Calling Cycle
    // and the Message section.  Calling-section word types (TO, FROM, TIS, THRU)
    // must not appear after the first CMD.
    bool past_first_cmd = false;
    for (const auto& word : words) {
        if (word.type == PreambleType::CMD) {
            past_first_cmd = true;
        } else if (past_first_cmd) {
            if (word.type == PreambleType::TO   ||
                word.type == PreambleType::FROM  ||
                word.type == PreambleType::TIS   ||
                word.type == PreambleType::THRU) {
                return false;
            }
        }
    }
    return true;
}

bool FrameValidator::rep_not_preceded_by_self_tis_twas(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-6: REP must not be directly preceded by REP, TIS, or TWAS.
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::REP) {
            if (words[i-1].type == PreambleType::REP ||
                words[i-1].type == PreambleType::TIS  ||
                words[i-1].type == PreambleType::TWAS) {
                return false;
            }
        }
    }
    return true;
}

bool FrameValidator::no_consecutive_same_preamble(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-2/3: consecutive words must have different preamble types so
    // receivers can distinguish data changes from repeated transmissions.
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == words[i-1].type) return false;
    }
    return true;
}

bool FrameValidator::rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-7: not enforceable at single-frame level.
    // "Multiple sender situation" is a network-state concept (net call, simultaneous
    // TX from several stations) that requires cross-station context unavailable here.
    // Enforcement deferred to ALEStateMachine net-call handling (NET_CALL_STUB).
    (void)words;
    return true;
}

bool FrameValidator::data_not_after_data(const std::vector<ALEWord>& words)
{
    // AC-WORD-009-1: DATA must not extend a DATA word (only non-DATA words may be extended).
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::DATA && words[i-1].type == PreambleType::DATA)
            return false;
    }
    return true;
}

// Strip trailing '@' padding characters (A.5.2.4.3) from a 3-char address field.
static std::string strip_padding(const char* addr)
{
    std::string s(addr);
    while (!s.empty() && s.back() == '@')
        s.pop_back();
    return s;
}

std::vector<std::string> FrameValidator::reconstruct_to_addresses(const std::vector<ALEWord>& words)
{
    // REP repeats the function of the previous non-REP word:
    //   last_non_rep == TO   → REP acts as TO   → new recipient
    //   last_non_rep == DATA → REP acts as DATA → extend current address
    std::vector<std::string> addresses;
    std::string current;
    PreambleType last_non_rep = PreambleType::UNKNOWN;

    for (const auto& word : words) {
        switch (word.type) {
            case PreambleType::TO:
                if (!current.empty()) addresses.push_back(current);
                current = strip_padding(word.address);
                last_non_rep = PreambleType::TO;
                break;
            case PreambleType::DATA:
                current += strip_padding(word.address);
                last_non_rep = PreambleType::DATA;
                break;
            case PreambleType::REP:
                if (last_non_rep == PreambleType::TO) {
                    addresses.push_back(current);
                    current = strip_padding(word.address);
                    // last_non_rep stays TO: next REP would also be a new recipient
                } else {
                    current += strip_padding(word.address);
                    // last_non_rep stays DATA: REP after DATA acts as DATA
                }
                break;
            default:
                break;
        }
    }

    if (!current.empty()) addresses.push_back(current);
    return addresses;
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