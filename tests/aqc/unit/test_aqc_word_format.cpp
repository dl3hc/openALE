/**
 * \file test_aqc_word_format.cpp
 * \brief Unit tests for AQCWord 16-bit compact format (AC-GEN-010-001)
 *
 * Verifies:
 *   TEST 1  AQCWord is 16 bits wide — distinct from ALEWord (24-bit)
 *   TEST 2  Bit-layout: type_flag at bit 15, chars at [14:10]/[9:5]/[4:0]
 *   TEST 3  Encode/decode roundtrip for address and control words
 *   TEST 4  AQC-32 character set (A-Z = codes 0-25, '0'-'5' = codes 26-31)
 *   TEST 5  from_chars / to_chars convenience helpers
 *   TEST 6  Characters outside AQC-32 rejected by from_chars
 */

#include "Protocol/AQC/aqc_protocol.h"
#include "Word/ale_word.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace ale;
using namespace ale::aqc;

// ── TEST 1 ────────────────────────────────────────────────────────────────────
void test_word_width() {
    std::cout << "[TEST 1] AQCWord is 16 bits; ALEWord payload is 24 bits\n";

    // AQCWord.raw holds the entire compact word in 16 bits.
    static_assert(sizeof(AQCWord::raw) == 2u, "AQCWord.raw must be 16 bits");
    assert(sizeof(AQCWord{}.raw) == 2u);

    // ALEWord carries a 24-bit raw_payload (3-bit preamble + 21-bit payload).
    // WORD_BITS is the manifest constant defined in ale_word.h.
    static_assert(WORD_BITS == 24u, "ALEWord total word size must be 24 bits");

    // The size difference must be exactly 8 bits (one byte) — clearly distinct.
    static_assert(WORD_BITS - sizeof(AQCWord::raw) * 8u == 8u,
                  "AQCWord must be 8 bits narrower than ALEWord");

    std::cout << "  ✓ AQCWord.raw  = " << sizeof(AQCWord::raw) * 8u << " bits\n";
    std::cout << "  ✓ ALEWord bits = " << WORD_BITS                 << " bits\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 2 ────────────────────────────────────────────────────────────────────
void test_bit_layout() {
    std::cout << "[TEST 2] Bit-layout: type_flag[15], char1[14:10], char2[9:5], char3[4:0]\n";

    // Only type_flag set → raw = 0x8000
    AQCWord w1 = AQCWord::encode(true, 0, 0, 0);
    assert(w1.raw == 0x8000u);
    assert(w1.type_flag() == true);
    assert(w1.char1() == 0u);
    assert(w1.char2() == 0u);
    assert(w1.char3() == 0u);

    // type_flag clear, char1=1 → raw = 0x0400
    AQCWord w2 = AQCWord::encode(false, 1u, 0u, 0u);
    assert(w2.raw == 0x0400u);
    assert(w2.type_flag() == false);
    assert(w2.char1() == 1u);

    // char2=1 → raw = 0x0020
    AQCWord w3 = AQCWord::encode(false, 0u, 1u, 0u);
    assert(w3.raw == 0x0020u);
    assert(w3.char2() == 1u);

    // char3=1 → raw = 0x0001
    AQCWord w4 = AQCWord::encode(false, 0u, 0u, 1u);
    assert(w4.raw == 0x0001u);
    assert(w4.char3() == 1u);

    // All max fields: type_flag + char1=31 + char2=31 + char3=31 → 0xFFFF
    AQCWord w5 = AQCWord::encode(true, 31u, 31u, 31u);
    assert(w5.raw == 0xFFFFu);

    std::cout << "  ✓ encode(true,0,0,0)       → 0x" << std::hex << w1.raw << std::dec << "\n";
    std::cout << "  ✓ encode(false,1,0,0)      → 0x" << std::hex << w2.raw << std::dec << "\n";
    std::cout << "  ✓ encode(false,0,1,0)      → 0x" << std::hex << w3.raw << std::dec << "\n";
    std::cout << "  ✓ encode(false,0,0,1)      → 0x" << std::hex << w4.raw << std::dec << "\n";
    std::cout << "  ✓ encode(true,31,31,31)    → 0x" << std::hex << w5.raw << std::dec << "\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 3 ────────────────────────────────────────────────────────────────────
void test_encode_decode_roundtrip() {
    std::cout << "[TEST 3] Encode/decode roundtrip for address and control words\n";

    struct Case { bool tf; uint8_t c1, c2, c3; };
    Case cases[] = {
        { false,  0u,  0u,  0u },   // all zero, address word
        { true,  31u, 31u, 31u },   // all max, control word
        { false,  0u,  1u, 15u },   // mixed
        { true,  10u, 20u,  5u },   // mixed control
        { false, 25u,  0u, 26u },   // 'Z', 'A', '0' in AQC-32
    };

    for (const auto& tc : cases) {
        AQCWord w = AQCWord::encode(tc.tf, tc.c1, tc.c2, tc.c3);
        assert(w.type_flag() == tc.tf);
        assert(w.char1()     == tc.c1);
        assert(w.char2()     == tc.c2);
        assert(w.char3()     == tc.c3);
        // Re-decode from raw
        AQCWord w2(w.raw);
        assert(w2.type_flag() == tc.tf);
        assert(w2.char1()     == tc.c1);
        assert(w2.char2()     == tc.c2);
        assert(w2.char3()     == tc.c3);
    }

    std::cout << "  ✓ All " << sizeof(cases)/sizeof(cases[0]) << " roundtrip cases match\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 4 ────────────────────────────────────────────────────────────────────
void test_aqc32_charset() {
    std::cout << "[TEST 4] AQC-32 character set (A-Z = 0-25, '0'-'5' = 26-31)\n";

    // A-Z → codes 0-25
    for (int i = 0; i < 26; ++i) {
        char ch = static_cast<char>('A' + i);
        uint8_t code = AQCWord::char_to_aqc32(ch);
        assert(code == static_cast<uint8_t>(i));
        assert(AQCWord::aqc32_to_char(code) == ch);
    }

    // '0'-'5' → codes 26-31
    for (int i = 0; i < 6; ++i) {
        char ch = static_cast<char>('0' + i);
        uint8_t code = AQCWord::char_to_aqc32(ch);
        assert(code == static_cast<uint8_t>(26 + i));
        assert(AQCWord::aqc32_to_char(code) == ch);
    }

    // Exactly 32 valid codes (0-31)
    for (uint8_t c = 0; c <= 31u; ++c) {
        assert(AQCWord::aqc32_to_char(c) != '\0');
    }

    std::cout << "  ✓ A-Z   → codes 0-25\n";
    std::cout << "  ✓ '0'-'5' → codes 26-31\n";
    std::cout << "  ✓ All 32 codes decode to non-null character\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 5 ────────────────────────────────────────────────────────────────────
void test_from_chars_to_chars() {
    std::cout << "[TEST 5] from_chars / to_chars convenience helpers\n";

    AQCWord w;
    bool ok = AQCWord::from_chars(false, 'A', 'B', 'C', w);
    assert(ok);

    char d1, d2, d3;
    bool decoded = w.to_chars(d1, d2, d3);
    assert(decoded);
    assert(d1 == 'A');
    assert(d2 == 'B');
    assert(d3 == 'C');
    assert(w.type_flag() == false);

    // Control word
    AQCWord wc;
    ok = AQCWord::from_chars(true, 'Z', '0', '5', wc);
    assert(ok);
    bool dc = wc.to_chars(d1, d2, d3);
    assert(dc);
    assert(d1 == 'Z' && d2 == '0' && d3 == '5');
    assert(wc.type_flag() == true);

    std::cout << "  ✓ from_chars('A','B','C') → to_chars → 'ABC'\n";
    std::cout << "  ✓ from_chars('Z','0','5') → to_chars → 'Z05' (control word)\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 6 ────────────────────────────────────────────────────────────────────
void test_invalid_chars_rejected() {
    std::cout << "[TEST 6] Characters outside AQC-32 rejected by from_chars\n";

    AQCWord w;
    // '6'-'9' are NOT in AQC-32 (only '0'-'5' are)
    assert(!AQCWord::from_chars(false, 'A', '6', 'C', w));
    assert(!AQCWord::from_chars(false, '9', 'B', 'C', w));
    // '@' and '?' are in Basic-38 but not AQC-32
    assert(!AQCWord::from_chars(false, '@', 'B', 'C', w));
    assert(!AQCWord::from_chars(false, 'A', '?', 'C', w));
    // Space and lowercase are also invalid
    assert(!AQCWord::from_chars(false, ' ', 'B', 'C', w));
    assert(!AQCWord::from_chars(false, 'a', 'B', 'C', w));

    // char_to_aqc32 returns 0xFF for invalid chars
    assert(AQCWord::char_to_aqc32('6')  == 0xFFu);
    assert(AQCWord::char_to_aqc32('@')  == 0xFFu);
    assert(AQCWord::char_to_aqc32('?')  == 0xFFu);
    assert(AQCWord::char_to_aqc32(' ')  == 0xFFu);
    assert(AQCWord::char_to_aqc32('a')  == 0xFFu);

    std::cout << "  ✓ '6','7','8','9' rejected (not in AQC-32)\n";
    std::cout << "  ✓ '@','?',' ','a' rejected\n";
    std::cout << "  PASSED\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "AQCWord 16-bit format tests (AC-GEN-010-001)\n";
    std::cout << "========================================\n\n";

    try {
        test_word_width();
        test_bit_layout();
        test_encode_decode_roundtrip();
        test_aqc32_charset();
        test_from_chars_to_chars();
        test_invalid_chars_rejected();

        std::cout << "========================================\n";
        std::cout << "All AQCWord format tests PASSED! (6/6)\n";
        std::cout << "AC-GEN-010-001 verified.\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
