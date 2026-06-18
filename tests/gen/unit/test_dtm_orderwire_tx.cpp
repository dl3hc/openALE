/**
 * \file tests/gen/unit/test_dtm_orderwire_tx.cpp
 * \brief Tests for AC-GEN-015-001: DTM CMD DTM + DATA/REP structure
 *
 * MIL-STD-188-141B A.5.7.3 — Data Text Message frame layout:
 *   CMD DTM  (Basic-38 identifier "DTM")
 *   DATA/REP pairs (Expanded-64 payload)
 *   Optional CRC word
 *
 * Verifies:
 *   TEST 1  encode_dtm("") — empty text yields exactly one word (CMD DTM)
 *   TEST 2  encode_dtm()   — first word is CMD with address "DTM"
 *   TEST 3  encode_dtm()   — data words alternate DATA/REP after CMD
 *   TEST 4  encode_dtm()   — partial last triplet padded with SP
 *   TEST 5  encode_dtm()   — 90-char text yields 31 words (1 CMD + 30 data)
 *   TEST 6  encode_dtm()   — out-of-range chars sanitised to '?'
 *   TEST 7  encode_dtm()   — CMD word carries "DTM", not message data
 *   TEST 8  encode_dtm()   — crc_enabled appends one additional word
 *   TEST 9  encode_dtm()   — crc word does not create consecutive same preamble
 *   TEST 10 encode_dtm()   — no consecutive identical preamble types in sequence
 */

#include "Protocol/ale_orderwire_protocols.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_empty()
{
    std::cout << "[TEST 1] encode_dtm(\"\") yields exactly one word (CMD DTM)\n";

    auto words = encode_dtm("");
    assert(words.size() == 1 && "empty input must produce exactly CMD DTM word");
    assert(words[0].type == PreambleType::CMD && "word must be CMD");
    std::cout << "  words.size() = 1, type = CMD  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_cmd_carries_dtm_id()
{
    std::cout << "[TEST 2] encode_dtm() — CMD word carries Basic-38 identifier \"DTM\"\n";

    auto words = encode_dtm("HELLO");
    assert(!words.empty());
    assert(words[0].type == PreambleType::CMD && "first word must be CMD");
    assert(words[0].address[0] == 'D' && "CMD address[0] must be 'D'");
    assert(words[0].address[1] == 'T' && "CMD address[1] must be 'T'");
    assert(words[0].address[2] == 'M' && "CMD address[2] must be 'M'");
    assert(words[0].valid && "CMD DTM word must be valid");
    std::cout << "  CMD word address = \"DTM\"  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_word_structure()
{
    std::cout << "[TEST 3] encode_dtm() — words alternate CMD, DATA, REP, DATA, ...\n";

    // "HELLO WORLD" = 11 chars → ceil(11/3) = 4 data words, total 5 words
    auto words = encode_dtm("HELLO WORLD");
    assert(words.size() == 5 && "11 chars + CMD -> 5 words");

    assert(words[0].type == PreambleType::CMD  && "word[0] must be CMD");
    assert(words[1].type == PreambleType::DATA && "word[1] must be DATA");
    assert(words[2].type == PreambleType::REP  && "word[2] must be REP");
    assert(words[3].type == PreambleType::DATA && "word[3] must be DATA");
    assert(words[4].type == PreambleType::REP  && "word[4] must be REP");

    // Data starts in word[1], not word[0] (DTM differs from AMD here)
    assert(words[1].address[0] == 'H' && words[1].address[1] == 'E' && words[1].address[2] == 'L'
           && "first data word must carry first 3 message chars");

    std::cout << "  5 words, types: CMD DATA REP DATA REP  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_padding()
{
    std::cout << "[TEST 4] encode_dtm() — partial last triplet padded with SP\n";

    // "HI" = 2 chars → 1 data word, padded to "HI " — total 2 words
    auto words = encode_dtm("HI");
    assert(words.size() == 2 && "2 chars -> CMD + 1 data word");
    assert(words[0].type == PreambleType::CMD);
    assert(words[1].type == PreambleType::DATA);
    assert(words[1].address[0] == 'H');
    assert(words[1].address[1] == 'I');
    assert(words[1].address[2] == ' ' && "third data char must be SP (0x20) padding");
    std::cout << "  data address = \"HI \", last char is SP  PASSED\n\n";
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_max_length()
{
    std::cout << "[TEST 5] encode_dtm() — 90-char text yields 31 words (CMD + 30 data)\n";

    std::string text(90, 'A');
    auto words = encode_dtm(text);
    assert(words.size() == 31 && "90 chars -> 1 CMD + 30 data = 31 words");
    assert(words[0].type == PreambleType::CMD);

    // Excess beyond 90 must be silently truncated
    std::string over(95, 'B');
    auto truncated = encode_dtm(over);
    assert(truncated.size() == 31 && "95 chars truncated to 31 words");
    std::cout << "  31 words for 90 chars, truncation at 91+  PASSED\n\n";
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_sanitisation()
{
    std::cout << "[TEST 6] encode_dtm() — out-of-range chars sanitised to '?'\n";

    std::string text;
    text += '\x01';  // outside Expanded-64 (0x20–0x5F)
    text += 'A';
    text += 'B';
    auto words = encode_dtm(text);
    assert(words.size() == 2);
    assert(words[1].address[0] == '?' && "control char must become '?'");
    assert(words[1].address[1] == 'A');
    assert(words[1].address[2] == 'B');
    std::cout << "  '\\x01' → '?'  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_cmd_not_message_data()
{
    std::cout << "[TEST 7] encode_dtm() — CMD word carries \"DTM\", not message data\n";

    // First 3 chars of message are "ABC", but CMD must still carry "DTM"
    auto words = encode_dtm("ABCDEF");
    assert(!words.empty());
    assert(words[0].type == PreambleType::CMD);
    // CMD is "DTM", not "ABC"
    assert(!(words[0].address[0] == 'A' && words[0].address[1] == 'B' && words[0].address[2] == 'C')
           && "CMD must NOT carry message data (DTM differs from AMD)");
    assert(words[0].address[0] == 'D' && words[0].address[1] == 'T' && words[0].address[2] == 'M'
           && "CMD must always carry \"DTM\" identifier");
    // Message data starts in word[1]
    assert(words[1].address[0] == 'A' && "first message char must be in word[1]");
    std::cout << "  CMD = \"DTM\", data starts in word[1]  PASSED\n\n";
}

// ── TEST 8 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_crc_appended()
{
    std::cout << "[TEST 8] encode_dtm() crc_enabled — appends exactly one CRC word\n";

    auto words_no_crc  = encode_dtm("HELLO", false);
    auto words_with_crc = encode_dtm("HELLO", true);
    assert(words_with_crc.size() == words_no_crc.size() + 1
           && "crc_enabled must add exactly one word");
    std::cout << "  crc_enabled adds 1 extra word  PASSED\n\n";
}

// ── TEST 9 ───────────────────────────────────────────────────────────────────
void test_encode_dtm_crc_no_consecutive_same()
{
    std::cout << "[TEST 9] encode_dtm() crc_enabled — CRC word does not repeat previous preamble\n";

    // Test with various text lengths so CRC word may be DATA or REP
    for (size_t len = 0; len <= 9; ++len) {
        std::string text(len, 'A');
        auto words = encode_dtm(text, true);
        assert(words.size() >= 2);
        assert(words[words.size() - 2].type != words[words.size() - 1].type
               && "CRC word must not share preamble with preceding word");
    }
    std::cout << "  CRC word preamble always differs from preceding word  PASSED\n\n";
}

// ── TEST 10 ──────────────────────────────────────────────────────────────────
void test_encode_dtm_no_consecutive_same_preamble()
{
    std::cout << "[TEST 10] encode_dtm() — no two adjacent words share the same preamble\n";

    for (size_t len : {size_t{0}, size_t{1}, size_t{3}, size_t{9}, size_t{12}, size_t{90}}) {
        std::string text(len, 'X');
        for (bool crc : {false, true}) {
            auto words = encode_dtm(text, crc);
            for (size_t i = 1; i < words.size(); ++i) {
                assert(words[i].type != words[i-1].type
                       && "no two adjacent DTM words may share preamble type");
            }
        }
    }
    std::cout << "  No consecutive identical preamble types in any frame  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==============================================\n";
    std::cout << "AC-GEN-015-001 — DTM CMD DTM + DATA/REP structure\n";
    std::cout << "==============================================\n\n";

    test_encode_dtm_empty();
    test_encode_dtm_cmd_carries_dtm_id();
    test_encode_dtm_word_structure();
    test_encode_dtm_padding();
    test_encode_dtm_max_length();
    test_encode_dtm_sanitisation();
    test_encode_dtm_cmd_not_message_data();
    test_encode_dtm_crc_appended();
    test_encode_dtm_crc_no_consecutive_same();
    test_encode_dtm_no_consecutive_same_preamble();

    std::cout << "All tests PASSED.\n";
    return 0;
}
