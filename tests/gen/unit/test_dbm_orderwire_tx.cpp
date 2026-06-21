/**
 * \file tests/gen/unit/test_dbm_orderwire_tx.cpp
 * \brief Tests for AC-GEN-016-001: DBM CRC16 and transparent binary data mode
 *
 * MIL-STD-188-141B A.5.7.4 — Data Block Message frame layout:
 *   CMD DBM  (Basic-38 identifier "DBM")
 *   DATA/REP pairs (transparent binary, any byte 0x00–0xFF)
 *   Optional CRC-16/CCITT word
 *
 * Verifies:
 *   TEST 1  encode_dbm({}) — empty payload yields exactly one word (CMD DBM), no CRC
 *   TEST 2  encode_dbm({}) crc_enabled — empty payload + CRC yields 2 words
 *   TEST 3  encode_dbm()   — first word is CMD with address "DBM"
 *   TEST 4  encode_dbm()   — data words alternate DATA/REP after CMD
 *   TEST 5  encode_dbm()   — partial last triplet is zero-padded
 *   TEST 6  encode_dbm()   — binary bytes outside Expanded-64 are accepted (transparent)
 *            bytes ≤ 0x7F stored as-is; bytes > 0x7F have MSB masked off (7-bit slot)
 *   TEST 7  encode_dbm()   — crc_enabled appends exactly one additional word
 *   TEST 8  encode_dbm()   — CRC word does not repeat preceding preamble
 *   TEST 9  encode_dbm()   — CRC-16 word encodes high byte then low byte
 *   TEST 10 encode_dbm()   — no consecutive identical preamble types in any frame
 */

#include "Protocol/Message/ale_orderwire_protocols.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace ale;

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_empty_no_crc()
{
    std::cout << "[TEST 1] encode_dbm({}, false) — empty payload yields exactly one word (CMD DBM)\n";

    auto words = encode_dbm({}, false);
    assert(words.size() == 1 && "empty payload without CRC must produce exactly CMD DBM word");
    assert(words[0].type == PreambleType::CMD && "word must be CMD");
    std::cout << "  words.size() = 1, type = CMD  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_empty_with_crc()
{
    std::cout << "[TEST 2] encode_dbm({}, true) — empty payload + CRC yields 2 words\n";

    auto words = encode_dbm({}, true);
    assert(words.size() == 2 && "empty payload with CRC must produce CMD DBM + CRC word");
    assert(words[0].type == PreambleType::CMD && "first word must be CMD DBM");
    std::cout << "  words.size() = 2, word[0] = CMD  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_cmd_carries_dbm_id()
{
    std::cout << "[TEST 3] encode_dbm() — CMD word carries Basic-38 identifier \"DBM\"\n";

    auto words = encode_dbm({0x41, 0x42, 0x43}, false);
    assert(!words.empty());
    assert(words[0].type == PreambleType::CMD && "first word must be CMD");
    assert(words[0].address[0] == 'D' && "CMD address[0] must be 'D'");
    assert(words[0].address[1] == 'B' && "CMD address[1] must be 'B'");
    assert(words[0].address[2] == 'M' && "CMD address[2] must be 'M'");
    assert(words[0].valid && "CMD DBM word must be valid");
    std::cout << "  CMD word address = \"DBM\"  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_word_structure()
{
    std::cout << "[TEST 4] encode_dbm() — words alternate CMD, DATA, REP, DATA, ...\n";

    // 7 bytes → ceil(7/3) = 3 data words, total 4 words
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    auto words = encode_dbm(payload, false);
    assert(words.size() == 4 && "7 bytes -> CMD + 3 data words");

    assert(words[0].type == PreambleType::CMD  && "word[0] must be CMD");
    assert(words[1].type == PreambleType::DATA && "word[1] must be DATA");
    assert(words[2].type == PreambleType::REP  && "word[2] must be REP");
    assert(words[3].type == PreambleType::DATA && "word[3] must be DATA");
    std::cout << "  4 words, types: CMD DATA REP DATA  PASSED\n\n";
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_padding()
{
    std::cout << "[TEST 5] encode_dbm() — partial last triplet is zero-padded\n";

    // 2 bytes → 1 data word, last byte padded to 0x00
    std::vector<uint8_t> payload = {0x41, 0x42};
    auto words = encode_dbm(payload, false);
    assert(words.size() == 2 && "2 bytes -> CMD + 1 data word");
    assert(words[1].type == PreambleType::DATA);
    // address[0] = 'A' (0x41), address[1] = 'B' (0x42), address[2] = 0x00 (pad)
    assert(static_cast<uint8_t>(words[1].address[0]) == 0x41u && "first byte preserved");
    assert(static_cast<uint8_t>(words[1].address[1]) == 0x42u && "second byte preserved");
    assert(static_cast<uint8_t>(words[1].address[2]) == 0x00u && "third byte must be 0x00 padding");
    std::cout << "  address = {0x41, 0x42, 0x00}, last byte is zero-pad  PASSED\n\n";
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_transparent_binary()
{
    std::cout << "[TEST 6] encode_dbm() — binary bytes outside Expanded-64 are accepted\n";

    // 0x00, 0x01, 0x7F are outside Expanded-64 (0x20–0x5F) — DBM must accept them
    std::vector<uint8_t> payload = {0x00, 0x01, 0x7F};
    auto words = encode_dbm(payload, false);
    assert(words.size() == 2 && "CMD + 1 data word");
    assert(words[1].valid && "word must be valid even for non-ASCII bytes");

    // Bytes 0x00–0x7F are stored in the 7-bit slot unchanged
    assert(static_cast<uint8_t>(words[1].address[0]) == 0x00u && "0x00 stored as-is");
    assert(static_cast<uint8_t>(words[1].address[1]) == 0x01u && "0x01 stored as-is");
    assert(static_cast<uint8_t>(words[1].address[2]) == 0x7Fu && "0x7F stored as-is");

    // Bytes > 0x7F: MSB is masked off by the 7-bit character slot (& 0x7F)
    // 0xFF → 0x7F (1111 1111 & 0111 1111 = 0111 1111)
    // 0x80 → 0x00 (1000 0000 & 0111 1111 = 0000 0000)
    std::vector<uint8_t> high = {0xFF, 0x80};
    auto words2 = encode_dbm(high, false);
    assert(words2.size() == 2);
    assert(static_cast<uint8_t>(words2[1].address[0]) == 0x7Fu && "0xFF masked to 0x7F");
    assert(static_cast<uint8_t>(words2[1].address[1]) == 0x00u && "0x80 masked to 0x00 (MSB stripped)");
    std::cout << "  0x00/0x01/0x7F stored as-is; 0xFF→0x7F, 0x80→0x00 (7-bit mask)  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_crc_appended()
{
    std::cout << "[TEST 7] encode_dbm() crc_enabled — appends exactly one CRC word\n";

    std::vector<uint8_t> payload = {0x41, 0x42, 0x43, 0x44, 0x45};
    auto words_no_crc   = encode_dbm(payload, false);
    auto words_with_crc = encode_dbm(payload, true);
    assert(words_with_crc.size() == words_no_crc.size() + 1
           && "crc_enabled must add exactly one word");
    std::cout << "  crc_enabled adds 1 extra word  PASSED\n\n";
}

// ── TEST 8 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_crc_no_consecutive_same()
{
    std::cout << "[TEST 8] encode_dbm() crc_enabled — CRC word does not repeat preceding preamble\n";

    for (size_t len = 0; len <= 9; ++len) {
        std::vector<uint8_t> payload(len, 0x55u);
        auto words = encode_dbm(payload, true);
        assert(words.size() >= 2);
        assert(words[words.size() - 2].type != words[words.size() - 1].type
               && "CRC word must not share preamble with preceding word");
    }
    std::cout << "  CRC preamble always differs from preceding word (len 0–9)  PASSED\n\n";
}

// ── TEST 9 ───────────────────────────────────────────────────────────────────
void test_encode_dbm_crc_value()
{
    std::cout << "[TEST 9] encode_dbm() — CRC word encodes CRC-16 high byte then low byte\n";

    // CRC-16/CCITT (poly 0x1021, init 0xFFFF) of {0x41, 0x42, 0x43} = 0x7508
    std::vector<uint8_t> payload = {0x41u, 0x42u, 0x43u};
    auto words = encode_dbm(payload, true);
    // CRC word is the last word
    const auto& crc_word = words.back();
    const uint8_t high = static_cast<uint8_t>(crc_word.address[0]);
    const uint8_t low  = static_cast<uint8_t>(crc_word.address[1]);
    const uint16_t crc = (static_cast<uint16_t>(high) << 8) | low;
    assert(crc == 0x7508u && "CRC-16/CCITT of {0x41,0x42,0x43} must be 0x7508");
    std::cout << "  CRC = 0x" << std::hex << crc << std::dec << " (expected 0x7508)  PASSED\n\n";
}

// ── TEST 10 ──────────────────────────────────────────────────────────────────
void test_encode_dbm_no_consecutive_same_preamble()
{
    std::cout << "[TEST 10] encode_dbm() — no two adjacent words share the same preamble\n";

    for (size_t len : {size_t{0}, size_t{1}, size_t{3}, size_t{7}, size_t{9}, size_t{12}}) {
        std::vector<uint8_t> payload(len, 0xAAu);
        for (bool crc : {false, true}) {
            auto words = encode_dbm(payload, crc);
            for (size_t i = 1; i < words.size(); ++i) {
                assert(words[i].type != words[i-1].type
                       && "no two adjacent DBM words may share preamble type");
            }
        }
    }
    std::cout << "  No consecutive identical preamble types in any frame  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==============================================\n";
    std::cout << "AC-GEN-016-001 — DBM CRC16 + transparent binary data mode\n";
    std::cout << "==============================================\n\n";

    test_encode_dbm_empty_no_crc();
    test_encode_dbm_empty_with_crc();
    test_encode_dbm_cmd_carries_dbm_id();
    test_encode_dbm_word_structure();
    test_encode_dbm_padding();
    test_encode_dbm_transparent_binary();
    test_encode_dbm_crc_appended();
    test_encode_dbm_crc_no_consecutive_same();
    test_encode_dbm_crc_value();
    test_encode_dbm_no_consecutive_same_preamble();

    std::cout << "All tests PASSED.\n";
    return 0;
}
