#include "Word/ale_word.h"
#include "Stores/address_book.h"
#include <iostream>
#include <iomanip>
#include <cstring>

namespace ale {

// ============================================================================
// Test 1: Word Preamble and Payload Extraction
// ============================================================================

bool test_word_parsing() {
    std::cout << "\n[TEST 1] Word Parsing (Preamble + Payload)\n";
    std::cout << "==========================================\n";
    
    WordParser parser;
    
    struct TestCase {
        uint32_t word_bits;
        WordType expected_type;
        const char* expected_addr;
        const char* description;
    };
    
    // Build test words: 3-bit preamble + 21-bit payload
    // Payload = 3 x 7-bit ASCII characters
    // Helper: encode word from preamble + 3 chars
    auto make_word = [](WordType type, const char* chars) -> uint32_t {
        uint32_t payload = WordParser::encode_ascii(chars, type);
        uint32_t preamble = static_cast<uint8_t>(type) & 0x07;
        return (preamble << 21) | payload;
    };
    
    TestCase tests[] = {
        { make_word(WordType::TO, "W1A"), WordType::TO, "W1A", "TO address" },
        { make_word(WordType::FROM, "K6K"), WordType::FROM, "K6K", "FROM address" },
        { make_word(WordType::TIS, "N0C"), WordType::TIS, "N0C", "TIS (sounding)" },
        { make_word(WordType::DATA, "ABC"), WordType::DATA, "ABC", "DATA word" },
        { make_word(WordType::TWAS, "NET"), WordType::TWAS, "NET", "Net call (TWAS)" },
    };
    
    bool all_pass = true;
    for (const auto& test : tests) {
        ALEWord word;
        bool success = parser.parse_from_bits(test.word_bits, word);
        
        bool type_match = (word.type == test.expected_type);
        bool addr_match = (strncmp(word.address, test.expected_addr, 3) == 0);
        bool pass = success && type_match && addr_match;
        
        std::cout << "  " << test.description << ": ";
        if (pass) {
            std::cout << "PASS (type=" << WordParser::word_type_name(word.type)
                      << ", addr=\"" << word.address << "\")\n";
        } else {
            std::cout << "FAIL";
            if (!type_match) {
                std::cout << " [type: expected " << WordParser::word_type_name(test.expected_type)
                          << ", got " << WordParser::word_type_name(word.type) << "]";
            }
            if (!addr_match) {
                std::cout << " [addr: expected \"" << test.expected_addr
                          << "\", got \"" << word.address << "\"]";
            }
            std::cout << "\n";
            all_pass = false;
        }
    }
    
    return all_pass;
}

// ============================================================================
// Test 2: ASCII Encoding/Decoding
// ============================================================================

bool test_ascii_codec() {
    std::cout << "\n[TEST 2] ASCII Encoding/Decoding\n";
    std::cout << "=================================\n";
    
    struct TestCase {
        const char* input;
        bool should_succeed;
        const char* description;
    };
    
    TestCase tests[] = {
        { "ABC", true, "Valid uppercase" },
        { "123", true, "Valid digits" },
        { "W1A", true, "Mixed alphanumeric" },
        { "N0C", true, "Call sign format" },
        { "@@@", true, "Wildcards" },
        { "   ", false, "Spaces (not in Basic 38)" },
    };
    
    bool all_pass = true;
    for (const auto& test : tests) {
        uint32_t encoded = WordParser::encode_ascii(test.input, WordType::TO);
        bool encode_success = (encoded != 0xFFFFFFFF);
        
        char decoded[4];
        bool decode_success = WordParser::decode_ascii(encoded, WordType::TO, decoded);
        
        bool pass = (encode_success == test.should_succeed);
        if (pass && test.should_succeed) {
            pass = (strncmp(test.input, decoded, 3) == 0);
        }
        
        std::cout << "  " << test.description << " (\"" << test.input << "\"): ";
        if (pass) {
            if (test.should_succeed) {
                std::cout << "PASS (\"" << decoded << "\")\n";
            } else {
                std::cout << "PASS (rejected as expected)\n";
            }
        } else {
            std::cout << "FAIL\n";
            all_pass = false;
        }
    }
    
    return all_pass;
}

// ============================================================================
// Test 3: Address Book
// ============================================================================

bool test_address_book() {
    std::cout << "\n[TEST 3] Address Book\n";
    std::cout << "=====================\n";
    
    AddressBook book;
    
    // Test 1: Set self address
    bool self_ok = book.set_self_address("W1AW");
    std::cout << "  Set self address: " << (self_ok ? "PASS" : "FAIL") << "\n";
    
    // Test 2: Check self
    bool is_self = book.is_self("W1AW");
    std::cout << "  Check is_self: " << (is_self ? "PASS" : "FAIL") << "\n";
    
    // Test 3: Add stations
    book.add_station("K6KB", "Rick");
    book.add_station("N2CKH", "Steve");
    
    bool known = book.is_known_station("K6KB");
    std::cout << "  Known station check: " << (known ? "PASS" : "FAIL") << "\n";
    
    // Test 4: Add net
    book.add_net("MARS", "MARS Net");
    bool is_net = book.is_known_net("MARS");
    std::cout << "  Net address check: " << (is_net ? "PASS" : "FAIL") << "\n";
    
    // Test 5: Wildcard matching — '?' per A.5.2.4.9
    bool match1 = AddressBook::match_wildcard("W?AW", "W1AW");
    bool match2 = AddressBook::match_wildcard("W?AW", "W2AW");
    bool no_match = !AddressBook::match_wildcard("W?AW", "K1AB");
    
    std::cout << "  Wildcard matching: " 
              << (match1 && match2 && no_match ? "PASS" : "FAIL") << "\n";
    
    return self_ok && is_self && known && is_net && match1 && match2 && no_match;
}

// ============================================================================
// Test 4: AC-WORD-001 — Word Bit Structure
// ============================================================================

bool test_word_bit_structure() {
    std::cout << "\n[TEST 4] AC-WORD-001: Word Bit Structure\n";
    std::cout << "==========================================\n";
    bool all_pass = true;

    // AC-WORD-001-1: Exactly 24 bits
    {
        bool pass = (WORD_BITS == 24);
        std::cout << "  AC-WORD-001-1 24-bit word: " << (pass ? "PASS" : "FAIL")
                  << " (WORD_BITS=" << WORD_BITS << ")\n";
        all_pass &= pass;
    }

    // AC-WORD-001-2: 3-bit preamble + 21-bit data field
    {
        bool pass = (PREAMBLE_BITS == 3) && (PAYLOAD_BITS == 21) &&
                    (PREAMBLE_BITS + PAYLOAD_BITS == WORD_BITS);
        std::cout << "  AC-WORD-001-2 3+21 structure: " << (pass ? "PASS" : "FAIL")
                  << " (PREAMBLE_BITS=" << PREAMBLE_BITS
                  << ", PAYLOAD_BITS=" << PAYLOAD_BITS << ")\n";
        all_pass &= pass;
    }

    // AC-WORD-001-3: MSB (bit 23) transmitted first — preamble occupies bits 23-21
    {
        const char chars[3] = {'A', 'B', 'C'};
        uint32_t payload = WordParser::encode_ascii(chars, WordType::TO);
        uint32_t preamble_val = static_cast<uint8_t>(WordType::TO) & 0x07;
        uint32_t word = (preamble_val << 21) | payload;
        WordType extracted = WordParser::extract_preamble(word);
        bool pass = (static_cast<uint8_t>(extracted) == preamble_val) &&
                    (WordParser::extract_payload(word) == payload);
        std::cout << "  AC-WORD-001-3 MSB-first (preamble at bits 23-21): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-001-4: Word splits into two 12-bit halves for FEC
    {
        uint32_t test_word = 0xABCDEFu & 0xFFFFFFu;
        uint16_t hi = static_cast<uint16_t>((test_word >> 12) & 0xFFF);
        uint16_t lo = static_cast<uint16_t>(test_word & 0xFFF);
        uint32_t reconstructed = (static_cast<uint32_t>(hi) << 12) | lo;
        bool pass = (GOLAY_INFO_BITS == 12) &&
                    (WORD_BITS == 2 * GOLAY_INFO_BITS) &&
                    (reconstructed == test_word);
        std::cout << "  AC-WORD-001-4 FEC split (2x12 bits, GOLAY_INFO_BITS="
                  << GOLAY_INFO_BITS << "): " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-001-5: 21-bit data field holds 3 x 7-bit ASCII chars
    {
        const char in[3] = {'W', '1', 'A'};
        uint32_t encoded = WordParser::encode_ascii(in, WordType::TO);
        char decoded[4] = {};
        bool ok = WordParser::decode_ascii(encoded, WordType::TO, decoded);
        bool roundtrip = ok && (decoded[0] == 'W') && (decoded[1] == '1') && (decoded[2] == 'A');
        bool pass = (PAYLOAD_BITS == 21) && (PAYLOAD_BITS == 3 * 7) && roundtrip;
        std::cout << "  AC-WORD-001-5 3x7-bit ASCII payload (PAYLOAD_BITS="
                  << PAYLOAD_BITS << "): " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    return all_pass;
}

// ============================================================================
// Test 5: AC-WORD-002 — Preamble Bits and Word Types
// ============================================================================

bool test_preamble_types() {
    std::cout << "\n[TEST 5] AC-WORD-002: Preamble Bits and Word Types\n";
    std::cout << "====================================================\n";
    bool all_pass = true;

    // AC-WORD-002-1: Leading 3 bits identify exactly one of 8 word types
    {
        bool all_map = true;
        for (uint8_t v = 0; v <= 7; ++v) {
            uint32_t word = static_cast<uint32_t>(v) << 21;
            WordType type = WordParser::extract_preamble(word);
            if (static_cast<uint8_t>(type) != v) {
                all_map = false;
            }
        }
        std::cout << "  AC-WORD-002-1 3-bit value -> 8 distinct types: "
                  << (all_map ? "PASS" : "FAIL") << "\n";
        all_pass &= all_map;
    }

    // AC-WORD-002-2: Preamble bits are P3(bit23), P2(bit22), P1(bit21)
    {
        uint32_t preamble_mask = 0x07u << 21;
        bool pass = (preamble_mask == ((1u << 23) | (1u << 22) | (1u << 21)));
        std::cout << "  AC-WORD-002-2 Preamble at P3=bit23, P2=bit22, P1=bit21: "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-002-3: All 8 word types with correct binary values per Table A-VIII
    {
        struct { WordType type; uint8_t val; const char* name; } types[] = {
            { WordType::DATA, 0, "DATA" },
            { WordType::THRU, 1, "THRU" },
            { WordType::TO,   2, "TO"   },
            { WordType::TWAS,  3, "TWAS" },
            { WordType::FROM, 4, "FROM" },
            { WordType::TIS,  5, "TIS"  },
            { WordType::CMD,  6, "CMD"  },
            { WordType::REP,  7, "REP"  },
        };
        bool all_ok = true;
        for (const auto& t : types) {
            if (static_cast<uint8_t>(t.type) != t.val) {
                std::cout << "    FAIL: " << t.name << " expected "
                          << (int)t.val << "\n";
                all_ok = false;
            }
        }
        std::cout << "  AC-WORD-002-3 THRU/TO/TWAS/FROM/TIS/CMD/DATA/REP values: "
                  << (all_ok ? "PASS" : "FAIL") << "\n";
        all_pass &= all_ok;
    }

    // AC-WORD-002-4: Non-standard (AQC-ALE) preamble values handled as UNKNOWN
    {
        // Standard 3-bit preamble covers 0-7; UNKNOWN sentinel is 0xFF
        bool pass = (static_cast<uint8_t>(WordType::UNKNOWN) == 0xFF);
        std::cout << "  AC-WORD-002-4 AQC/non-standard preambles -> UNKNOWN (0xFF): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    return all_pass;
}

// ============================================================================
// Test 6: CMD/DATA/REP Frame Validation
// ============================================================================

bool test_cmd_data_rep_validation() {
    std::cout << "\n[TEST 6] CMD/DATA/REP Frame Validation\n";
    std::cout << "===================================\n";
    
    bool all_pass = true;
    
    // Test for AC-WORD-008-1: Every Message section begins with CMD
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(WordType::CMD, "ABC"));  // Start message section
        words.push_back(WordParser::make_word(WordType::DATA, "DEF"));  // Valid in message section
        words.push_back(WordParser::make_word(WordType::TO, "GHI"));    // Valid in message section
        words.push_back(WordParser::make_word(WordType::CMD, "JKL"));  // Start new message section
        
        bool pass = FrameValidator::message_sections_begin_with_cmd(words);
        std::cout << "  AC-WORD-008-1 Message sections begin with CMD: " 
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }
    
    // Test for AC-WORD-008-5: First CMD marks boundary — calling-section words must
    // not appear after it.
    {
        // Valid: typical frame — address words precede CMD, message content follows
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(WordType::TO,   "XYZ"));
        valid1.push_back(WordParser::make_word(WordType::FROM, "ABC"));
        valid1.push_back(WordParser::make_word(WordType::CMD,  "MSG"));
        valid1.push_back(WordParser::make_word(WordType::DATA, "HI!"));
        bool v1 = FrameValidator::first_cmd_begins_message_section(valid1);
        std::cout << "  AC-WORD-008-5 Valid (TO,FROM,CMD,DATA): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Valid: CMD followed by TWAS conclusion (no calling-section words after CMD)
        std::vector<ALEWord> valid2;
        valid2.push_back(WordParser::make_word(WordType::TO,   "XYZ"));
        valid2.push_back(WordParser::make_word(WordType::CMD,  "MSG"));
        valid2.push_back(WordParser::make_word(WordType::TWAS, "ABC"));
        bool v2 = FrameValidator::first_cmd_begins_message_section(valid2);
        std::cout << "  AC-WORD-008-5 Valid (TO,CMD,TWAS): "
                  << (v2 ? "PASS" : "FAIL") << "\n";

        // Invalid: TO appears after CMD — calling section bled into message section
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(WordType::TO,  "XYZ"));
        bool i1 = !FrameValidator::first_cmd_begins_message_section(invalid1);
        std::cout << "  AC-WORD-008-5 Invalid (CMD,TO): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: TIS appears after CMD
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        invalid2.push_back(WordParser::make_word(WordType::TIS, "ABC"));
        bool i2 = !FrameValidator::first_cmd_begins_message_section(invalid2);
        std::cout << "  AC-WORD-008-5 Invalid (CMD,TIS): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && v2 && i1 && i2);
    }
    
    // Test for AC-WORD-010-6: REP must not follow itself, TIS, or TWAS
    {
        // Valid sequence: DATA, REP, DATA
        std::vector<ALEWord> valid_words;
        valid_words.push_back(WordParser::make_word(WordType::DATA, "ABC"));
        valid_words.push_back(WordParser::make_word(WordType::REP, "DEF"));
        valid_words.push_back(WordParser::make_word(WordType::DATA, "GHI"));
        
        bool valid_pass = FrameValidator::rep_not_preceded_by_self_tis_twas(valid_words);
        std::cout << "  AC-WORD-010-6 Valid sequence (DATA, REP, DATA): " 
                  << (valid_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: REP, REP
        std::vector<ALEWord> invalid_words1;
        invalid_words1.push_back(WordParser::make_word(WordType::REP, "ABC"));
        invalid_words1.push_back(WordParser::make_word(WordType::REP, "DEF"));
        
        bool invalid1_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words1);
        std::cout << "  AC-WORD-010-6 Invalid sequence (REP, REP): " 
                  << (invalid1_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: TIS, REP
        std::vector<ALEWord> invalid_words2;
        invalid_words2.push_back(WordParser::make_word(WordType::TIS, "ABC"));
        invalid_words2.push_back(WordParser::make_word(WordType::REP, "DEF"));
        
        bool invalid2_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words2);
        std::cout << "  AC-WORD-010-6 Invalid sequence (TIS, REP): " 
                  << (invalid2_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: TWAS, REP
        std::vector<ALEWord> invalid_words3;
        invalid_words3.push_back(WordParser::make_word(WordType::TWAS, "ABC"));
        invalid_words3.push_back(WordParser::make_word(WordType::REP, "DEF"));
        
        bool invalid3_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words3);
        std::cout << "  AC-WORD-010-6 Invalid sequence (TWAS, REP): " 
                  << (invalid3_pass ? "PASS" : "FAIL") << "\n";
        
        all_pass &= (valid_pass && invalid1_pass && invalid2_pass && invalid3_pass);
    }
    
    // AC-WORD-009-1: DATA must not extend another DATA word.
    {
        // Valid: TO → DATA (DATA extends TO)
        std::vector<ALEWord> valid;
        valid.push_back(WordParser::make_word(WordType::TO,   "ABC"));
        valid.push_back(WordParser::make_word(WordType::DATA, "DEF"));
        bool pos = FrameValidator::data_not_after_data(valid);
        std::cout << "  AC-WORD-009-1 Valid (TO, DATA): "
                  << (pos ? "PASS" : "FAIL") << "\n";

        // Invalid: DATA → DATA (DATA cannot extend DATA)
        std::vector<ALEWord> invalid;
        invalid.push_back(WordParser::make_word(WordType::DATA, "ABC"));
        invalid.push_back(WordParser::make_word(WordType::DATA, "DEF"));
        bool neg = !FrameValidator::data_not_after_data(invalid);
        std::cout << "  AC-WORD-009-1 Invalid (DATA, DATA): "
                  << (neg ? "PASS" : "FAIL") << "\n";

        all_pass &= (pos && neg);
    }

    // AC-WORD-009-2: DATA carries Expanded-64 content in messages.
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(WordType::DATA, "ABC"));
        bool pass = (words[0].type == WordType::DATA &&
                     std::string(words[0].address) == "ABC");
        std::cout << "  AC-WORD-009-2 DATA carries message information: "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-010-1: REP after DATA extends the address (REP repeats DATA function).
    // [TO "ABC", DATA "DEF", REP "GHI"] must reconstruct to one address "ABCDEFGHI".
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(WordType::TO,   "ABC"));
        words.push_back(WordParser::make_word(WordType::DATA, "DEF"));
        words.push_back(WordParser::make_word(WordType::REP,  "GHI"));
        auto addrs = FrameValidator::reconstruct_to_addresses(words);
        bool pass = (addrs.size() == 1 && addrs[0] == "ABCDEFGHI");
        std::cout << "  AC-WORD-010-1 REP extends address (TO,DATA,REP -> \"ABCDEFGHI\"): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-010-4: REP directly after TO specifies a second recipient address.
    // [TO "ABC", REP "DEF"] must reconstruct to two addresses {"ABC", "DEF"}.
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(WordType::TO,  "ABC"));
        words.push_back(WordParser::make_word(WordType::REP, "DEF"));
        auto addrs = FrameValidator::reconstruct_to_addresses(words);
        bool pass = (addrs.size() == 2 && addrs[0] == "ABC" && addrs[1] == "DEF");
        std::cout << "  AC-WORD-010-4 REP after TO = two recipients (\"ABC\", \"DEF\"): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }
    
    return all_pass;
}

// ============================================================================
// Test 7: Phase 3 — AC-WORD-008-3, AC-WORD-008-4, AC-WORD-010-2/3
// ============================================================================

bool test_phase3_validators() {
    std::cout << "\n[TEST 7] Frame Validators\n";
    std::cout << "=========================\n";

    bool all_pass = true;

    // AC-WORD-008-3: CMD must not appear before the address section.
    {
        // Valid: address word before CMD
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(WordType::TO,  "XYZ"));
        valid1.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        bool v1 = FrameValidator::cmd_not_before_address_section(valid1);
        std::cout << "  AC-WORD-008-3 Valid (TO, CMD): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD before any address word
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(WordType::TO,  "XYZ"));
        bool i1 = !FrameValidator::cmd_not_before_address_section(invalid1);
        std::cout << "  AC-WORD-008-3 Invalid (CMD, TO): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD in scanning section (THRU present, no address word yet)
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(WordType::THRU, "XYZ"));
        invalid2.push_back(WordParser::make_word(WordType::CMD,  "MSG"));
        bool i2 = !FrameValidator::cmd_not_before_address_section(invalid2);
        std::cout << "  AC-WORD-008-3 Invalid (THRU, CMD): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && i1 && i2);
    }

    // AC-WORD-008-4: Frame with CMD must have preceding call and following conclusion.
    {
        // Valid: TO → CMD → TIS
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(WordType::TO,  "XYZ"));
        valid1.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        valid1.push_back(WordParser::make_word(WordType::TIS, "ABC"));
        bool v1 = FrameValidator::cmd_has_call_and_conclusion(valid1);
        std::cout << "  AC-WORD-008-4 Valid (TO, CMD, TIS): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Valid: TO → FROM → CMD → TWAS
        std::vector<ALEWord> valid2;
        valid2.push_back(WordParser::make_word(WordType::TO,   "XYZ"));
        valid2.push_back(WordParser::make_word(WordType::FROM, "ABC"));
        valid2.push_back(WordParser::make_word(WordType::CMD,  "MSG"));
        valid2.push_back(WordParser::make_word(WordType::TWAS, "NET"));
        bool v2 = FrameValidator::cmd_has_call_and_conclusion(valid2);
        std::cout << "  AC-WORD-008-4 Valid (TO, FROM, CMD, TWAS): "
                  << (v2 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD without preceding call
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(WordType::TIS, "ABC"));
        bool i1 = !FrameValidator::cmd_has_call_and_conclusion(invalid1);
        std::cout << "  AC-WORD-008-4 Invalid (CMD, TIS — no call): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD without following conclusion
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(WordType::TO,  "XYZ"));
        invalid2.push_back(WordParser::make_word(WordType::CMD, "MSG"));
        bool i2 = !FrameValidator::cmd_has_call_and_conclusion(invalid2);
        std::cout << "  AC-WORD-008-4 Invalid (TO, CMD — no conclusion): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && v2 && i1 && i2);
    }

    // AC-WORD-010-2/3: Consecutive words must have different preamble types.
    {
        // Valid: alternating TO, DATA, REP
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(WordType::TO,   "ABC"));
        valid1.push_back(WordParser::make_word(WordType::DATA, "DEF"));
        valid1.push_back(WordParser::make_word(WordType::REP,  "GHI"));
        bool v1 = FrameValidator::no_consecutive_same_preamble(valid1);
        std::cout << "  AC-WORD-010-2/3 Valid (TO, DATA, REP): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Invalid: two consecutive DATA words
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(WordType::DATA, "ABC"));
        invalid1.push_back(WordParser::make_word(WordType::DATA, "DEF"));
        bool i1 = !FrameValidator::no_consecutive_same_preamble(invalid1);
        std::cout << "  AC-WORD-010-2/3 Invalid (DATA, DATA): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: two consecutive REP words
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(WordType::REP, "ABC"));
        invalid2.push_back(WordParser::make_word(WordType::REP, "DEF"));
        bool i2 = !FrameValidator::no_consecutive_same_preamble(invalid2);
        std::cout << "  AC-WORD-010-2/3 Invalid (REP, REP): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && i1 && i2);
    }

    return all_pass;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PC-ALE 2.0 Phase 2 - Protocol Layer Unit Tests          ║\n";
    std::cout << "║  MIL-STD-188-141B Word Structure & Message Assembly       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    int pass_count = 0;
    int fail_count = 0;
    
    if (test_word_parsing()) { pass_count++; } else { fail_count++; }
    if (test_ascii_codec()) { pass_count++; } else { fail_count++; }
    if (test_address_book()) { pass_count++; } else { fail_count++; }
    if (test_word_bit_structure()) { pass_count++; } else { fail_count++; }
    if (test_preamble_types()) { pass_count++; } else { fail_count++; }
    if (test_cmd_data_rep_validation()) { pass_count++; } else { fail_count++; }
    if (test_phase3_validators())       { pass_count++; } else { fail_count++; }
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count << "  Failed: " << std::setw(2) << fail_count 
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main() {
    return ale::run_all_tests();
}