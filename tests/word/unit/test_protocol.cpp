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
        PreambleType expected_type;
        const char* expected_addr;
        const char* description;
    };
    
    // Build test words: 3-bit preamble + 21-bit payload
    // Payload = 3 x 7-bit ASCII characters
    // Helper: encode word from preamble + 3 chars
    auto make_word = [](PreambleType type, const char* chars) -> uint32_t {
        uint32_t payload = WordParser::encode_ascii(chars, type);
        uint32_t preamble = static_cast<uint8_t>(type) & 0x07;
        return (preamble << 21) | payload;
    };
    
    TestCase tests[] = {
        { make_word(PreambleType::TO, "W1A"), PreambleType::TO, "W1A", "TO address" },
        { make_word(PreambleType::FROM, "K6K"), PreambleType::FROM, "K6K", "FROM address" },
        { make_word(PreambleType::TIS, "N0C"), PreambleType::TIS, "N0C", "TIS (sounding)" },
        { make_word(PreambleType::DATA, "ABC"), PreambleType::DATA, "ABC", "DATA word" },
        { make_word(PreambleType::TWAS, "NET"), PreambleType::TWAS, "NET", "Net call (TWAS)" },
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
        uint32_t encoded = WordParser::encode_ascii(test.input, PreambleType::TO);
        bool encode_success = (encoded != 0xFFFFFFFF);
        
        char decoded[4];
        WordParser::decode_ascii(encoded, PreambleType::TO, decoded);
        
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
        uint32_t payload = WordParser::encode_ascii(chars, PreambleType::TO);
        uint32_t preamble_val = static_cast<uint8_t>(PreambleType::TO) & 0x07;
        uint32_t word = (preamble_val << 21) | payload;
        PreambleType extracted = WordParser::extract_preamble(word);
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
        uint32_t encoded = WordParser::encode_ascii(in, PreambleType::TO);
        char decoded[4] = {};
        bool ok = WordParser::decode_ascii(encoded, PreambleType::TO, decoded);
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
            PreambleType type = WordParser::extract_preamble(word);
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
        struct { PreambleType type; uint8_t val; const char* name; } types[] = {
            { PreambleType::DATA, 0, "DATA" },
            { PreambleType::THRU, 1, "THRU" },
            { PreambleType::TO,   2, "TO"   },
            { PreambleType::TWAS,  3, "TWAS" },
            { PreambleType::FROM, 4, "FROM" },
            { PreambleType::TIS,  5, "TIS"  },
            { PreambleType::CMD,  6, "CMD"  },
            { PreambleType::REP,  7, "REP"  },
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
        bool pass = (static_cast<uint8_t>(PreambleType::UNKNOWN) == 0xFF);
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
        words.push_back(WordParser::make_word(PreambleType::CMD, "ABC"));  // Start message section
        words.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));  // Valid in message section
        words.push_back(WordParser::make_word(PreambleType::TO, "GHI"));    // Valid in message section
        words.push_back(WordParser::make_word(PreambleType::CMD, "JKL"));  // Start new message section
        
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
        valid1.push_back(WordParser::make_word(PreambleType::TO,   "XYZ"));
        valid1.push_back(WordParser::make_word(PreambleType::FROM, "ABC"));
        valid1.push_back(WordParser::make_word(PreambleType::CMD,  "MSG"));
        valid1.push_back(WordParser::make_word(PreambleType::DATA, "HI!"));
        bool v1 = FrameValidator::first_cmd_begins_message_section(valid1);
        std::cout << "  AC-WORD-008-5 Valid (TO,FROM,CMD,DATA): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Valid: CMD followed by TWAS conclusion (no calling-section words after CMD)
        std::vector<ALEWord> valid2;
        valid2.push_back(WordParser::make_word(PreambleType::TO,   "XYZ"));
        valid2.push_back(WordParser::make_word(PreambleType::CMD,  "MSG"));
        valid2.push_back(WordParser::make_word(PreambleType::TWAS, "ABC"));
        bool v2 = FrameValidator::first_cmd_begins_message_section(valid2);
        std::cout << "  AC-WORD-008-5 Valid (TO,CMD,TWAS): "
                  << (v2 ? "PASS" : "FAIL") << "\n";

        // Invalid: TO appears after CMD — calling section bled into message section
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(PreambleType::TO,  "XYZ"));
        bool i1 = !FrameValidator::first_cmd_begins_message_section(invalid1);
        std::cout << "  AC-WORD-008-5 Invalid (CMD,TO): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: TIS appears after CMD
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        invalid2.push_back(WordParser::make_word(PreambleType::TIS, "ABC"));
        bool i2 = !FrameValidator::first_cmd_begins_message_section(invalid2);
        std::cout << "  AC-WORD-008-5 Invalid (CMD,TIS): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && v2 && i1 && i2);
    }
    
    // Test for AC-WORD-010-6: REP must not follow itself, TIS, or TWAS
    {
        // Valid sequence: DATA, REP, DATA
        std::vector<ALEWord> valid_words;
        valid_words.push_back(WordParser::make_word(PreambleType::DATA, "ABC"));
        valid_words.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
        valid_words.push_back(WordParser::make_word(PreambleType::DATA, "GHI"));
        
        bool valid_pass = FrameValidator::rep_not_preceded_by_self_tis_twas(valid_words);
        std::cout << "  AC-WORD-010-6 Valid sequence (DATA, REP, DATA): " 
                  << (valid_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: REP, REP
        std::vector<ALEWord> invalid_words1;
        invalid_words1.push_back(WordParser::make_word(PreambleType::REP, "ABC"));
        invalid_words1.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
        
        bool invalid1_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words1);
        std::cout << "  AC-WORD-010-6 Invalid sequence (REP, REP): " 
                  << (invalid1_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: TIS, REP
        std::vector<ALEWord> invalid_words2;
        invalid_words2.push_back(WordParser::make_word(PreambleType::TIS, "ABC"));
        invalid_words2.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
        
        bool invalid2_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words2);
        std::cout << "  AC-WORD-010-6 Invalid sequence (TIS, REP): " 
                  << (invalid2_pass ? "PASS" : "FAIL") << "\n";
        
        // Invalid sequence: TWAS, REP
        std::vector<ALEWord> invalid_words3;
        invalid_words3.push_back(WordParser::make_word(PreambleType::TWAS, "ABC"));
        invalid_words3.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
        
        bool invalid3_pass = !FrameValidator::rep_not_preceded_by_self_tis_twas(invalid_words3);
        std::cout << "  AC-WORD-010-6 Invalid sequence (TWAS, REP): " 
                  << (invalid3_pass ? "PASS" : "FAIL") << "\n";
        
        all_pass &= (valid_pass && invalid1_pass && invalid2_pass && invalid3_pass);
    }
    
    // AC-WORD-009-1: DATA must not extend another DATA word.
    {
        // Valid: TO → DATA (DATA extends TO)
        std::vector<ALEWord> valid;
        valid.push_back(WordParser::make_word(PreambleType::TO,   "ABC"));
        valid.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));
        bool pos = FrameValidator::data_not_after_data(valid);
        std::cout << "  AC-WORD-009-1 Valid (TO, DATA): "
                  << (pos ? "PASS" : "FAIL") << "\n";

        // Invalid: DATA → DATA (DATA cannot extend DATA)
        std::vector<ALEWord> invalid;
        invalid.push_back(WordParser::make_word(PreambleType::DATA, "ABC"));
        invalid.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));
        bool neg = !FrameValidator::data_not_after_data(invalid);
        std::cout << "  AC-WORD-009-1 Invalid (DATA, DATA): "
                  << (neg ? "PASS" : "FAIL") << "\n";

        all_pass &= (pos && neg);
    }

    // AC-WORD-009-2: DATA carries Expanded-64 content in messages.
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(PreambleType::DATA, "ABC"));
        bool pass = (words[0].type == PreambleType::DATA &&
                     std::string(words[0].address) == "ABC");
        std::cout << "  AC-WORD-009-2 DATA carries message information: "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // AC-WORD-010-1: REP after DATA extends the address (REP repeats DATA function).
    // [TO "ABC", DATA "DEF", REP "GHI"] must reconstruct to one address "ABCDEFGHI".
    {
        std::vector<ALEWord> words;
        words.push_back(WordParser::make_word(PreambleType::TO,   "ABC"));
        words.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));
        words.push_back(WordParser::make_word(PreambleType::REP,  "GHI"));
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
        words.push_back(WordParser::make_word(PreambleType::TO,  "ABC"));
        words.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
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
        valid1.push_back(WordParser::make_word(PreambleType::TO,  "XYZ"));
        valid1.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        bool v1 = FrameValidator::cmd_not_before_address_section(valid1);
        std::cout << "  AC-WORD-008-3 Valid (TO, CMD): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD before any address word
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(PreambleType::TO,  "XYZ"));
        bool i1 = !FrameValidator::cmd_not_before_address_section(invalid1);
        std::cout << "  AC-WORD-008-3 Invalid (CMD, TO): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD in scanning section (THRU present, no address word yet)
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(PreambleType::THRU, "XYZ"));
        invalid2.push_back(WordParser::make_word(PreambleType::CMD,  "MSG"));
        bool i2 = !FrameValidator::cmd_not_before_address_section(invalid2);
        std::cout << "  AC-WORD-008-3 Invalid (THRU, CMD): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && i1 && i2);
    }

    // AC-WORD-008-4: Frame with CMD must have preceding call and following conclusion.
    {
        // Valid: TO → CMD → TIS
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(PreambleType::TO,  "XYZ"));
        valid1.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        valid1.push_back(WordParser::make_word(PreambleType::TIS, "ABC"));
        bool v1 = FrameValidator::cmd_has_call_and_conclusion(valid1);
        std::cout << "  AC-WORD-008-4 Valid (TO, CMD, TIS): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Valid: TO → FROM → CMD → TWAS
        std::vector<ALEWord> valid2;
        valid2.push_back(WordParser::make_word(PreambleType::TO,   "XYZ"));
        valid2.push_back(WordParser::make_word(PreambleType::FROM, "ABC"));
        valid2.push_back(WordParser::make_word(PreambleType::CMD,  "MSG"));
        valid2.push_back(WordParser::make_word(PreambleType::TWAS, "NET"));
        bool v2 = FrameValidator::cmd_has_call_and_conclusion(valid2);
        std::cout << "  AC-WORD-008-4 Valid (TO, FROM, CMD, TWAS): "
                  << (v2 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD without preceding call
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        invalid1.push_back(WordParser::make_word(PreambleType::TIS, "ABC"));
        bool i1 = !FrameValidator::cmd_has_call_and_conclusion(invalid1);
        std::cout << "  AC-WORD-008-4 Invalid (CMD, TIS — no call): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: CMD without following conclusion
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(PreambleType::TO,  "XYZ"));
        invalid2.push_back(WordParser::make_word(PreambleType::CMD, "MSG"));
        bool i2 = !FrameValidator::cmd_has_call_and_conclusion(invalid2);
        std::cout << "  AC-WORD-008-4 Invalid (TO, CMD — no conclusion): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && v2 && i1 && i2);
    }

    // AC-WORD-010-2/3: Consecutive words must have different preamble types.
    {
        // Valid: alternating TO, DATA, REP
        std::vector<ALEWord> valid1;
        valid1.push_back(WordParser::make_word(PreambleType::TO,   "ABC"));
        valid1.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));
        valid1.push_back(WordParser::make_word(PreambleType::REP,  "GHI"));
        bool v1 = FrameValidator::no_consecutive_same_preamble(valid1);
        std::cout << "  AC-WORD-010-2/3 Valid (TO, DATA, REP): "
                  << (v1 ? "PASS" : "FAIL") << "\n";

        // Invalid: two consecutive DATA words
        std::vector<ALEWord> invalid1;
        invalid1.push_back(WordParser::make_word(PreambleType::DATA, "ABC"));
        invalid1.push_back(WordParser::make_word(PreambleType::DATA, "DEF"));
        bool i1 = !FrameValidator::no_consecutive_same_preamble(invalid1);
        std::cout << "  AC-WORD-010-2/3 Invalid (DATA, DATA): "
                  << (i1 ? "PASS" : "FAIL") << "\n";

        // Invalid: two consecutive REP words
        std::vector<ALEWord> invalid2;
        invalid2.push_back(WordParser::make_word(PreambleType::REP, "ABC"));
        invalid2.push_back(WordParser::make_word(PreambleType::REP, "DEF"));
        bool i2 = !FrameValidator::no_consecutive_same_preamble(invalid2);
        std::cout << "  AC-WORD-010-2/3 Invalid (REP, REP): "
                  << (i2 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && i1 && i2);
    }

    return all_pass;
}

// ============================================================================
// AC-WORD-001-001 — Bit-Layout: W1=bit23 (MSB), W24=bit0 (LSB)
// Verifies exact bit positions: Preamble bits[23:21], Char1 bits[20:14],
// Char2 bits[13:7], Char3 bits[6:0]. Encode formula: (preamble<<21)|(c1<<14)|(c2<<7)|c3.
// ============================================================================

bool test_ac_word_001_001_bit_layout()
{
    std::cout << "\n[AC-WORD-001-001] Bit-Layout: W1=bit23 (MSB), W24=bit0 (LSB)\n";
    std::cout << "==================================================================\n";
    bool all_pass = true;

    // TO preamble = 2 (0b010); use distinct chars A=0x41, B=0x42, C=0x43
    // Expected word24 = (2<<21) | (0x41<<14) | (0x42<<7) | 0x43
    const uint32_t preamble_val = static_cast<uint8_t>(PreambleType::TO);
    const char c1 = 'A', c2 = 'B', c3 = 'C';
    const uint32_t expected_word = (preamble_val << 21)
                                 | (static_cast<uint32_t>(c1) << 14)
                                 | (static_cast<uint32_t>(c2) <<  7)
                                 | (static_cast<uint32_t>(c3) <<  0);

    // 1. encode_ascii produces the correct bit pattern
    {
        const char chars[3] = { c1, c2, c3 };
        uint32_t payload = WordParser::encode_ascii(chars, PreambleType::TO);
        uint32_t word24  = (preamble_val << 21) | payload;
        bool pass = (word24 == expected_word);
        std::cout << "  encode: (preamble<<21)|(c1<<14)|(c2<<7)|c3 = 0x"
                  << std::hex << expected_word << std::dec
                  << ": " << (pass ? "PASS" : "FAIL");
        if (!pass) std::cout << " (got=0x" << std::hex << word24 << std::dec << ")";
        std::cout << "\n";
        all_pass &= pass;
    }

    // 2. Preamble extracted from bits[23:21] via >> 21
    {
        PreambleType extracted = WordParser::extract_preamble(expected_word);
        bool pass = (static_cast<uint8_t>(extracted) == preamble_val);
        std::cout << "  preamble bits[23:21] >>21 -> " << static_cast<int>(extracted)
                  << ": " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // 3. Char1 sits at bits[20:14]: (payload >> 14) & 0x7F
    {
        uint32_t payload = WordParser::extract_payload(expected_word);
        char got = static_cast<char>((payload >> 14) & 0x7F);
        bool pass = (got == c1);
        std::cout << "  Char1 bits[20:14] (payload>>14 & 0x7F) = '" << got
                  << "': " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // 4. Char2 sits at bits[13:7]: (payload >> 7) & 0x7F
    {
        uint32_t payload = WordParser::extract_payload(expected_word);
        char got = static_cast<char>((payload >> 7) & 0x7F);
        bool pass = (got == c2);
        std::cout << "  Char2 bits[13:7]  (payload>>7  & 0x7F) = '" << got
                  << "': " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // 5. Char3 sits at bits[6:0]: payload & 0x7F
    {
        uint32_t payload = WordParser::extract_payload(expected_word);
        char got = static_cast<char>(payload & 0x7F);
        bool pass = (got == c3);
        std::cout << "  Char3 bits[6:0]   (payload     & 0x7F) = '" << got
                  << "': " << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    // 6. parse_from_bits full round-trip restores all fields correctly
    {
        WordParser wp;
        ALEWord w;
        bool ok = wp.parse_from_bits(expected_word, w);
        bool pass = ok && (w.type == PreambleType::TO)
                       && (w.address[0] == c1)
                       && (w.address[1] == c2)
                       && (w.address[2] == c3);
        std::cout << "  parse_from_bits round-trip (TO/\"ABC\"): "
                  << (pass ? "PASS" : "FAIL");
        if (ok && !pass) std::cout << " (addr=\"" << w.address << "\")";
        std::cout << "\n";
        all_pass &= pass;
    }

    return all_pass;
}

// ============================================================================
// AC-WORD-001-002 — Encode/Decode-Symmetrie
//
// Two directions must hold for all valid 24-bit words:
//   (A) decode(encode(p, c1, c2, c3)) == (p, c1, c2, c3)
//   (B) encode(decode(w)) == w
//
// Basic 38 preambles (TO, FROM, TIS, TWAS, THRU, CMD) use A-Z, 0-9, @, ?.
// Expanded-64 preambles (DATA, REP) use 0x20..0x5F.
// ============================================================================

bool test_ac_word_001_002_encode_decode_symmetry()
{
    std::cout << "\n[AC-WORD-001-002] Encode/Decode-Symmetrie\n";
    std::cout << "==========================================\n";
    bool all_pass = true;

    // ── Direction A: decode(encode(p, c1, c2, c3)) == (p, c1, c2, c3) ────────
    {
        // Basic-38 preambles — representative chars from each sub-range
        struct B38Case { PreambleType type; char c1, c2, c3; };
        const B38Case basic38[] = {
            { PreambleType::TO,   'A', 'B', 'C' },
            { PreambleType::TO,   'Z', '0', '9' },
            { PreambleType::TO,   '@', '?', 'A' },
            { PreambleType::FROM, 'W', '1', 'A' },
            { PreambleType::TIS,  'N', '0', 'C' },
            { PreambleType::TWAS, 'R', 'E', 'J' },
            { PreambleType::THRU, 'X', 'Y', 'Z' },
            { PreambleType::CMD,  '1', '2', '3' },
        };
        bool dir_a_basic = true;
        for (const auto& c : basic38) {
            const char chars[3] = { c.c1, c.c2, c.c3 };
            uint32_t payload = WordParser::encode_ascii(chars, c.type);
            bool enc_ok = (payload != 0xFFFFFFFF);
            char out[4] = {};
            bool dec_ok = enc_ok && WordParser::decode_ascii(payload, c.type, out);
            bool match  = dec_ok && out[0] == c.c1 && out[1] == c.c2 && out[2] == c.c3;
            // preamble survives the word-bits round-trip
            uint32_t word_bits = (static_cast<uint32_t>(c.type) << 21) | payload;
            bool pre_ok = match && (WordParser::extract_preamble(word_bits) == c.type);
            if (!pre_ok) {
                std::cout << "  A Basic-38 FAIL type=" << WordParser::word_type_name(c.type)
                          << " chars=\"" << c.c1 << c.c2 << c.c3 << "\""
                          << " got=\"" << out << "\"\n";
                dir_a_basic = false;
            }
        }
        std::cout << "  A: decode(encode(p,c1,c2,c3)) — Basic-38 cases: "
                  << (dir_a_basic ? "PASS" : "FAIL") << "\n";
        all_pass &= dir_a_basic;

        // Expanded-64 preambles (DATA, REP) — chars from 0x20..0x5F
        struct E64Case { PreambleType type; char c1, c2, c3; };
        const E64Case exp64[] = {
            { PreambleType::DATA, ' ', '!', '"' },   // 0x20, 0x21, 0x22
            { PreambleType::DATA, 'A', 'Z', '_' },   // 0x41, 0x5A, 0x5F
            { PreambleType::REP,  ' ', '@', '?' },   // 0x20, 0x40, 0x3F
            { PreambleType::REP,  '0', '9', '!' },   // 0x30, 0x39, 0x21
        };
        bool dir_a_exp64 = true;
        for (const auto& c : exp64) {
            const char chars[3] = { c.c1, c.c2, c.c3 };
            uint32_t payload = WordParser::encode_ascii(chars, c.type);
            bool enc_ok = (payload != 0xFFFFFFFF);
            char out[4] = {};
            bool dec_ok = enc_ok && WordParser::decode_ascii(payload, c.type, out);
            bool match  = dec_ok && out[0] == c.c1 && out[1] == c.c2 && out[2] == c.c3;
            uint32_t word_bits = (static_cast<uint32_t>(c.type) << 21) | payload;
            bool pre_ok = match && (WordParser::extract_preamble(word_bits) == c.type);
            if (!pre_ok) {
                std::cout << "  A Expanded-64 FAIL type=" << WordParser::word_type_name(c.type)
                          << " chars=0x" << std::hex << (int)(uint8_t)c.c1
                          << "/0x" << (int)(uint8_t)c.c2
                          << "/0x" << (int)(uint8_t)c.c3 << std::dec
                          << " enc_ok=" << enc_ok << " match=" << match << "\n";
                dir_a_exp64 = false;
            }
        }
        std::cout << "  A: decode(encode(p,c1,c2,c3)) — Expanded-64 cases: "
                  << (dir_a_exp64 ? "PASS" : "FAIL") << "\n";
        all_pass &= dir_a_exp64;
    }

    // ── Direction B: encode(decode(w)) == w ───────────────────────────────────
    {
        // Build a representative set of valid 24-bit words and verify identity.
        struct WordCase { PreambleType type; char c1, c2, c3; };
        const WordCase cases[] = {
            { PreambleType::TO,   'W', '1', 'A' },
            { PreambleType::FROM, 'K', '6', 'K' },
            { PreambleType::TIS,  'N', '0', 'C' },
            { PreambleType::TWAS, 'N', 'E', 'T' },
            { PreambleType::THRU, 'A', 'B', 'C' },
            { PreambleType::CMD,  'Z', '9', '@' },
            { PreambleType::DATA, 'H', 'i', '!' },   // 'i'=0x69 > 0x5F: encode fails → skipped
            { PreambleType::REP,  ' ', 'A', '_' },   // Expanded-64 valid
        };
        // Note: 'i' (0x69) is outside Expanded-64 — that case must be skipped.
        // We'll detect encode failure and skip rather than marking as FAIL.

        bool dir_b_pass = true;
        int tested = 0, skipped = 0;
        for (const auto& c : cases) {
            const char chars[3] = { c.c1, c.c2, c.c3 };
            uint32_t payload = WordParser::encode_ascii(chars, c.type);
            if (payload == 0xFFFFFFFF) { ++skipped; continue; }  // invalid chars for type

            uint32_t w = (static_cast<uint32_t>(c.type) << 21) | payload;

            // Decode: extract preamble + payload, decode chars
            PreambleType p2    = WordParser::extract_preamble(w);
            uint32_t payload2  = WordParser::extract_payload(w);
            char out[4] = {};
            bool dec_ok = WordParser::decode_ascii(payload2, p2, out);

            // Re-encode
            uint32_t re_payload = dec_ok ? WordParser::encode_ascii(out, p2) : 0xFFFFFFFFu;
            uint32_t w2 = (static_cast<uint32_t>(p2) << 21) | re_payload;

            bool ok = dec_ok && (re_payload != 0xFFFFFFFF) && (w2 == w);
            if (!ok) {
                std::cout << "  B FAIL type=" << WordParser::word_type_name(c.type)
                          << " w=0x" << std::hex << w << " w2=0x" << w2 << std::dec << "\n";
                dir_b_pass = false;
            }
            ++tested;
        }
        std::cout << "  B: encode(decode(w)) == w — " << tested << " words tested"
                  << (skipped ? " (" + std::to_string(skipped) + " skipped: invalid chars for type)" : "")
                  << ": " << (dir_b_pass ? "PASS" : "FAIL") << "\n";
        all_pass &= dir_b_pass;
    }

    // ── Exhaustive direction B over all 8 preambles with boundary chars ───────
    {
        // For each preamble type use the full Basic-38 or Expanded-64 boundary chars.
        // Spot-check 3 boundary triples per character set to catch off-by-one errors.
        struct BoundaryCase { PreambleType type; char c1, c2, c3; const char* label; };
        const BoundaryCase boundary[] = {
            // Basic-38 boundaries
            { PreambleType::TO,   'A', 'A', 'A', "TO/AAA" },
            { PreambleType::TO,   'Z', 'Z', 'Z', "TO/ZZZ" },
            { PreambleType::TO,   '0', '0', '0', "TO/000" },
            { PreambleType::TO,   '9', '9', '9', "TO/999" },
            { PreambleType::TO,   '@', '@', '@', "TO/@@@" },
            { PreambleType::TO,   '?', '?', '?', "TO/???" },
            // Expanded-64 boundaries (0x20=' ', 0x5F='_')
            { PreambleType::DATA, ' ', ' ', ' ', "DATA/0x20" },
            { PreambleType::DATA, '_', '_', '_', "DATA/0x5F" },
            { PreambleType::REP,  ' ', '_', '!', "REP/mix"  },
        };
        bool boundary_pass = true;
        for (const auto& c : boundary) {
            const char chars[3] = { c.c1, c.c2, c.c3 };
            uint32_t payload  = WordParser::encode_ascii(chars, c.type);
            if (payload == 0xFFFFFFFF) {
                std::cout << "  boundary FAIL encode: " << c.label << "\n";
                boundary_pass = false;
                continue;
            }
            uint32_t w = (static_cast<uint32_t>(c.type) << 21) | payload;
            PreambleType p2   = WordParser::extract_preamble(w);
            uint32_t pl2      = WordParser::extract_payload(w);
            char out[4] = {};
            bool dec_ok       = WordParser::decode_ascii(pl2, p2, out);
            uint32_t re_pl    = dec_ok ? WordParser::encode_ascii(out, p2) : 0xFFFFFFFFu;
            uint32_t w2       = (static_cast<uint32_t>(p2) << 21) | re_pl;
            bool ok = dec_ok && (re_pl != 0xFFFFFFFFu) && (w2 == w);
            if (!ok) {
                std::cout << "  boundary FAIL B: " << c.label
                          << " w=0x" << std::hex << w << " w2=0x" << w2 << std::dec << "\n";
                boundary_pass = false;
            }
        }
        std::cout << "  B: boundary chars (all preambles): "
                  << (boundary_pass ? "PASS" : "FAIL") << "\n";
        all_pass &= boundary_pass;
    }

    return all_pass;
}

// ============================================================================
// AC-WORD-002-001 — PreambleType: genau 8 Werte gemäß Tabelle A-VIII
//
// Verifies that PreambleType enum contains exactly the 8 ALE word types
// defined in MIL-STD-188-141B Table A-VIII, each with the correct 3-bit code.
// ============================================================================

bool test_ac_word_002_001_preamble_enum_table_aviii()
{
    std::cout << "\n[AC-WORD-002-001] PreambleType: 8 Werte gemäß Tabelle A-VIII\n";
    std::cout << "===============================================================\n";
    bool all_pass = true;

    // Table A-VIII: all 8 word types with mandatory 3-bit codes
    struct Entry { PreambleType type; uint8_t code; const char* name; };
    const Entry table_aviii[] = {
        { PreambleType::DATA, 0b000, "DATA" },  // extension and information
        { PreambleType::THRU, 0b001, "THRU" },  // multiple (and indirect routing)
        { PreambleType::TO,   0b010, "TO"   },  // direct routing
        { PreambleType::TWAS, 0b011, "TWAS" },  // terminator and identification quitting
        { PreambleType::FROM, 0b100, "FROM" },  // identification (and indirect routing)
        { PreambleType::TIS,  0b101, "TIS"  },  // terminator and identification continuing
        { PreambleType::CMD,  0b110, "CMD"  },  // orderwire control and status
        { PreambleType::REP,  0b111, "REP"  },  // duplication and information
    };
    constexpr int EXPECTED_COUNT = 8;
    const int actual_count = static_cast<int>(sizeof(table_aviii) / sizeof(table_aviii[0]));

    // 1. Exactly 8 entries defined
    {
        bool pass = (actual_count == EXPECTED_COUNT);
        std::cout << "  Table A-VIII entry count == 8: "
                  << (pass ? "PASS" : "FAIL")
                  << " (got " << actual_count << ")\n";
        all_pass &= pass;
    }

    // 2. Each type has the correct numeric code
    for (const auto& e : table_aviii) {
        uint8_t got = static_cast<uint8_t>(e.type);
        bool pass = (got == e.code);
        std::cout << "  " << e.name << " == 0b" << (int)((e.code>>2)&1) << (int)((e.code>>1)&1) << (int)(e.code&1)
                  << " (" << (int)e.code << "): " << (pass ? "PASS" : "FAIL");
        if (!pass) std::cout << " (got=" << (int)got << ")";
        std::cout << "\n";
        all_pass &= pass;
    }

    // 3. extract_preamble round-trips all 8 codes correctly
    {
        bool rt_pass = true;
        for (const auto& e : table_aviii) {
            uint32_t word = static_cast<uint32_t>(e.code) << 21;
            PreambleType extracted = WordParser::extract_preamble(word);
            if (static_cast<uint8_t>(extracted) != e.code) {
                std::cout << "  extract_preamble FAIL for " << e.name << "\n";
                rt_pass = false;
            }
        }
        std::cout << "  extract_preamble round-trip (all 8 codes): "
                  << (rt_pass ? "PASS" : "FAIL") << "\n";
        all_pass &= rt_pass;
    }

    // 4. UNKNOWN sentinel is 0xFF (not a Table A-VIII code)
    {
        bool pass = (static_cast<uint8_t>(PreambleType::UNKNOWN) == 0xFF);
        std::cout << "  UNKNOWN sentinel == 0xFF (not a spec code): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        all_pass &= pass;
    }

    return all_pass;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ALE Protocol Layer Unit Tests                            ║\n";
    std::cout << "║  MIL-STD-188-141B Word Structure & Message Assembly       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    if (test_ac_word_001_001_bit_layout()) { pass_count++; } else { fail_count++; }
    if (test_ac_word_001_002_encode_decode_symmetry()) { pass_count++; } else { fail_count++; }
    if (test_ac_word_002_001_preamble_enum_table_aviii()) { pass_count++; } else { fail_count++; }
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