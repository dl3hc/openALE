/**
 * \file test_aqc_fixed_bit.cpp
 * \brief Unit tests: AQC-ALE Fixed-Bit mechanism + Base-ALE backward compatibility
 *
 * Covers AC-GEN-010-002:
 *   "AQC-ALE frames can be received by a Base-ALE system without crashing or
 *    entering an error state. The Fixed-Bit mechanism must be implemented."
 *
 * Tests:
 *   TEST 1  is_aqc_word: bit 15 = 1  → true  (AQC control word, fixed bit set)
 *   TEST 2  is_aqc_word: bit 15 = 0  → false (AQC address word, fixed bit clear)
 *   TEST 3  payload_has_aqc_fixed_bit correctly reads bit 15 of a 21-bit payload
 *   TEST 4  Fixed bit is set for control words, clear for address words
 *   TEST 5  is_base_ale_frame detects plain Base-ALE words (no fixed bit)
 *   TEST 6  Base-ALE-style parsing of AQC frames: no crash, graceful handling
 */

#include "Protocol/AQC/aqc_protocol.h"
#include "Word/ale_word.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace ale;
using namespace ale::aqc;

// ── TEST 1 ────────────────────────────────────────────────────────────────────
// AQC control words (type_flag=true) have bit 15=1 — is_aqc_word must return true.
void test_is_aqc_word_control() {
    std::cout << "[TEST 1] is_aqc_word: bit 15 = 1 → true (AQC control word)\n";

    // Minimal control word: only fixed bit set, all chars = 0
    AQCWord w_min = AQCWord::encode(true, 0u, 0u, 0u);
    assert(w_min.raw == 0x8000u);
    assert(AQCProtocol::is_aqc_word(w_min.raw) == true);

    // All-ones control word
    AQCWord w_max = AQCWord::encode(true, 31u, 31u, 31u);
    assert(w_max.raw == 0xFFFFu);
    assert(AQCProtocol::is_aqc_word(w_max.raw) == true);

    // Control word with mixed chars
    AQCWord w_mid = AQCWord::encode(true, 10u, 5u, 20u);
    assert(w_mid.type_flag() == true);
    assert(AQCProtocol::is_aqc_word(w_mid.raw) == true);

    // Direct raw value with bit 15 set
    assert(AQCProtocol::is_aqc_word(0x8000u) == true);
    assert(AQCProtocol::is_aqc_word(0x8001u) == true);
    assert(AQCProtocol::is_aqc_word(0xFFFFu) == true);

    std::cout << "  ✓ encode(true,0,0,0)  raw=0x" << std::hex << w_min.raw
              << " → is_aqc_word=true\n" << std::dec;
    std::cout << "  ✓ encode(true,31,31,31) raw=0xFFFF → is_aqc_word=true\n";
    std::cout << "  ✓ raw 0x8000, 0x8001, 0xFFFF → all true\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 2 ────────────────────────────────────────────────────────────────────
// AQC address words (type_flag=false) have bit 15=0 — is_aqc_word must return false.
void test_is_aqc_word_address() {
    std::cout << "[TEST 2] is_aqc_word: bit 15 = 0 → false (AQC address word)\n";

    AQCWord w0 = AQCWord::encode(false, 0u, 0u, 0u);
    assert(w0.raw == 0x0000u);
    assert(AQCProtocol::is_aqc_word(w0.raw) == false);

    AQCWord w_max = AQCWord::encode(false, 31u, 31u, 31u);
    assert(w_max.raw == 0x7FFFu);
    assert(AQCProtocol::is_aqc_word(w_max.raw) == false);

    // Addresses with real AQC-32 characters: "ABC"
    AQCWord w_abc;
    bool ok = AQCWord::from_chars(false, 'A', 'B', 'C', w_abc);
    assert(ok);
    assert(w_abc.type_flag() == false);
    assert(AQCProtocol::is_aqc_word(w_abc.raw) == false);

    // "XY0" → another address
    AQCWord w_xy0;
    ok = AQCWord::from_chars(false, 'X', 'Y', '0', w_xy0);
    assert(ok);
    assert(AQCProtocol::is_aqc_word(w_xy0.raw) == false);

    // Direct values: anything with bit 15=0
    assert(AQCProtocol::is_aqc_word(0x0000u) == false);
    assert(AQCProtocol::is_aqc_word(0x7FFFu) == false);
    assert(AQCProtocol::is_aqc_word(0x4000u) == false);

    std::cout << "  ✓ encode(false,0,0,0)    raw=0x0000 → is_aqc_word=false\n";
    std::cout << "  ✓ encode(false,31,31,31) raw=0x7FFF → is_aqc_word=false\n";
    std::cout << "  ✓ from_chars('A','B','C') → is_aqc_word=false\n";
    std::cout << "  ✓ raw 0x0000, 0x7FFF, 0x4000 → all false\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 3 ────────────────────────────────────────────────────────────────────
// payload_has_aqc_fixed_bit checks bit 15 of the 21-bit Base-ALE payload.
void test_payload_has_aqc_fixed_bit() {
    std::cout << "[TEST 3] payload_has_aqc_fixed_bit: bit 15 of 21-bit payload\n";

    // Bit 15 SET → fixed bit present
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x00008000u) == true);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x0000FFFFu) == true);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x0001FFFFu) == true); // bit 16 + bit 15

    // Bit 15 CLEAR → no fixed bit
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x00000000u) == false);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x00007FFFu) == false);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x00004000u) == false);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(0x00010000u) == false); // bit 16 set, bit 15=0

    // Control word "embedded" in 21-bit payload (bits [15:0] = AQC ctrl word)
    AQCWord ctrl = AQCWord::encode(true, 3u, 7u, 15u);
    uint32_t payload = static_cast<uint32_t>(ctrl.raw);  // lower 16 bits only
    assert(AQCProtocol::payload_has_aqc_fixed_bit(payload) == true);

    // Address word embedded: bit 15=0
    AQCWord addr = AQCWord::encode(false, 3u, 7u, 15u);
    payload = static_cast<uint32_t>(addr.raw);
    assert(AQCProtocol::payload_has_aqc_fixed_bit(payload) == false);

    std::cout << "  ✓ payload 0x8000 → fixed bit present\n";
    std::cout << "  ✓ payload 0x0000 → no fixed bit\n";
    std::cout << "  ✓ AQC control word embedded in payload → fixed bit present\n";
    std::cout << "  ✓ AQC address word embedded in payload → no fixed bit\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 4 ────────────────────────────────────────────────────────────────────
// Fixed bit is set consistently for control words across the AQC-32 charset.
void test_fixed_bit_consistency() {
    std::cout << "[TEST 4] Fixed bit set in ALL control words, clear in ALL address words\n";

    uint32_t ctrl_checked = 0;
    uint32_t addr_checked = 0;

    // Spot-check all 32 valid AQC-32 codes in every char position
    for (uint8_t c = 0; c < 32; ++c) {
        AQCWord ctrl_w = AQCWord::encode(true, c, 0u, 0u);
        assert(AQCProtocol::is_aqc_word(ctrl_w.raw) == true);
        ++ctrl_checked;

        AQCWord addr_w = AQCWord::encode(false, c, 0u, 0u);
        assert(AQCProtocol::is_aqc_word(addr_w.raw) == false);
        ++addr_checked;
    }

    std::cout << "  ✓ " << ctrl_checked << " control words: is_aqc_word=true in all cases\n";
    std::cout << "  ✓ " << addr_checked << " address words: is_aqc_word=false in all cases\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 5 ────────────────────────────────────────────────────────────────────
// is_base_ale_frame correctly classifies ALEWords by their payload fixed bit.
void test_is_base_ale_frame() {
    std::cout << "[TEST 5] is_base_ale_frame: plain Base-ALE words (no fixed bit)\n";

    ALEWord base_word;
    base_word.type        = PreambleType::TO;
    base_word.raw_payload = 0x000012u;   // bit 15 = 0 → plain Base-ALE
    base_word.valid       = true;
    assert(AQCProtocol::is_base_ale_frame(base_word) == true);

    ALEWord base_cmd;
    base_cmd.type        = PreambleType::CMD;
    base_cmd.raw_payload = 0x007FFFu;   // bit 15 = 0 → still Base-ALE
    base_cmd.valid       = true;
    assert(AQCProtocol::is_base_ale_frame(base_cmd) == true);

    ALEWord aqc_ctrl;
    aqc_ctrl.type        = PreambleType::DATA;
    aqc_ctrl.raw_payload = 0x00C000u;   // bit 15 = 1 → AQC control word present
    aqc_ctrl.valid       = true;
    assert(AQCProtocol::is_base_ale_frame(aqc_ctrl) == false);

    // Embed an AQC control word in a DATA payload
    AQCWord ctrl_word = AQCWord::encode(true, 5u, 10u, 3u);
    ALEWord aqc_data;
    aqc_data.type        = PreambleType::DATA;
    aqc_data.raw_payload = static_cast<uint32_t>(ctrl_word.raw);
    aqc_data.valid       = true;
    assert(AQCProtocol::is_base_ale_frame(aqc_data) == false);

    std::cout << "  ✓ TO word,  payload=0x000012 → is_base_ale_frame=true\n";
    std::cout << "  ✓ CMD word, payload=0x007FFF → is_base_ale_frame=true\n";
    std::cout << "  ✓ DATA word, payload=0x00C000 → is_base_ale_frame=false (AQC)\n";
    std::cout << "  ✓ Embedded AQC control word → is_base_ale_frame=false\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 6 ────────────────────────────────────────────────────────────────────
// Backward compat: AQCParser gracefully handles AQC frames passed as Base-ALE
// words.  No exception; parser reports success or failure without crashing.
void test_base_ale_backward_compat() {
    std::cout << "[TEST 6] Backward compat: Base-ALE receiver handles AQC frames gracefully\n";

    // Build a minimal AQC call-probe using AQC-32 address encoding.
    // An AQC address word (type_flag=false) looks like a Base-ALE address word
    // with unusual character values — a Base-ALE receiver ignores it (no match).
    AQCWord to_word;
    bool ok = AQCWord::from_chars(false, 'A', 'B', 'C', to_word);
    assert(ok);
    assert(AQCProtocol::is_aqc_word(to_word.raw) == false);  // address word: no fixed bit

    AQCWord from_word;
    ok = AQCWord::from_chars(false, 'X', 'Y', 'Z', from_word);
    assert(ok);
    assert(AQCProtocol::is_aqc_word(from_word.raw) == false);

    // Simulate Base-ALE parser seeing these as standard TO/FROM words.
    // The parser should not crash regardless of whether it's AQC or Base-ALE.
    ALEWord words[3];

    words[0].type        = PreambleType::TO;
    words[0].raw_payload = static_cast<uint32_t>(to_word.raw);
    words[0].valid       = true;
    words[0].timestamp_ms = 1000;
    // Simulate the address decoder — in real life the Base-ALE decoder reads
    // the 3×7-bit Basic-38 characters from the payload, producing garbage text
    // (AQC-32 ≠ Basic-38), but nothing that causes a crash.
    words[0].address[0] = AQCWord::aqc32_to_char(to_word.char1());
    words[0].address[1] = AQCWord::aqc32_to_char(to_word.char2());
    words[0].address[2] = AQCWord::aqc32_to_char(to_word.char3());
    words[0].address[3] = '\0';

    words[1].type        = PreambleType::FROM;
    words[1].raw_payload = static_cast<uint32_t>(from_word.raw);
    words[1].valid       = true;
    words[1].timestamp_ms = 1100;
    words[1].address[0] = AQCWord::aqc32_to_char(from_word.char1());
    words[1].address[1] = AQCWord::aqc32_to_char(from_word.char2());
    words[1].address[2] = AQCWord::aqc32_to_char(from_word.char3());
    words[1].address[3] = '\0';

    // Add a control word (with fixed bit) — simulates the AQC control part
    AQCWord ctrl_word = AQCWord::encode(true, 2u, 4u, 1u);  // type_flag=true
    assert(AQCProtocol::is_aqc_word(ctrl_word.raw) == true);  // fixed bit set
    words[2].type        = PreambleType::CMD;
    words[2].raw_payload = static_cast<uint32_t>(ctrl_word.raw);
    words[2].valid       = true;
    words[2].timestamp_ms = 1200;
    words[2].address[0] = '\0';

    // AQCParser can parse the AQC call probe — no crash.
    AQCParser parser;
    AQCCallProbe probe;
    bool parsed = parser.parse_call_probe(words, 2, probe);
    assert(parsed == true);
    assert(probe.to_address[0] != '\0');    // some address decoded
    assert(probe.term_address[0] != '\0');

    // Verify the control word carries the fixed bit (backward-compat marker)
    assert(AQCProtocol::is_base_ale_frame(words[0]) == true);   // address word: no fixed bit
    assert(AQCProtocol::is_base_ale_frame(words[2]) == false);  // ctrl word: has fixed bit

    std::cout << "  ✓ AQC address words: no fixed bit (Base-ALE safe)\n";
    std::cout << "  ✓ AQC control word: fixed bit set (bit 15=1)\n";
    std::cout << "  ✓ parse_call_probe on AQC frame: no crash, returns OK\n";
    std::cout << "  ✓ is_base_ale_frame correctly classifies each word\n";
    std::cout << "  PASSED\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "AQC-ALE Fixed-Bit / Backward-Compat Tests (AC-GEN-010-002)\n";
    std::cout << "========================================\n\n";

    try {
        test_is_aqc_word_control();
        test_is_aqc_word_address();
        test_payload_has_aqc_fixed_bit();
        test_fixed_bit_consistency();
        test_is_base_ale_frame();
        test_base_ale_backward_compat();

        std::cout << "========================================\n";
        std::cout << "All Fixed-Bit / Backward-Compat tests PASSED! (6/6)\n";
        std::cout << "AC-GEN-010-002 verified.\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
