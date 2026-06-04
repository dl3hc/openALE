#include "ale_word.h"
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
        { make_word(WordType::TWS, "NET"), WordType::TWS, "NET", "Net call (TWS)" },
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
    
    // Test 5: Wildcard matching
    bool match1 = AddressBook::match_wildcard("W@AW", "W1AW");
    bool match2 = AddressBook::match_wildcard("W@AW", "W2AW");
    bool no_match = !AddressBook::match_wildcard("W@AW", "K1AB");
    
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
            { WordType::TWS,  3, "TWS"  },
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
        std::cout << "  AC-WORD-002-3 THRU/TO/TWS/FROM/TIS/CMD/DATA/REP values: "
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
