/**
 * \file test_version_caps.cpp
 * \brief Unit tests for VERSION CMD encoding (AC-GEN-011-001)
 *        and CAPABILITIES QUERY encoding (AC-GEN-012-001)
 *        and CAPABILITIES REPORT encoding (AC-GEN-012-002)
 *
 * Verifies:
 *   TEST 1  encode_version_cmd (summary) — CMD preamble is set
 *   TEST 2  encode_version_cmd (summary) — payload chars are 'v', '/', 's'
 *   TEST 3  encode_version_cmd (full)    — payload char3 is 'f'
 *   TEST 4  is_version_cmd detects VERSION CMD words
 *   TEST 5  decode_version_cmd round-trips format selection
 *   TEST 6  non-VERSION CMD words rejected by is_version_cmd
 *   TEST 7  encode_capabilities_query    — CMD preamble is set
 *   TEST 8  encode_capabilities_query    — payload chars are 'c', '/', 'q'
 *   TEST 9  is_capabilities_query detects CAPABILITIES QUERY words
 *   TEST 10 non-capabilities-query words rejected by is_capabilities_query
 *   TEST 11 encode_capabilities_report   — first word is CMD c/r
 *   TEST 12 encode_capabilities_report   — 5 DATA/REP words follow (DATA,REP,DATA,REP,DATA)
 *   TEST 13 is_capabilities_report detects c/r CMD word; rejects others
 */

#include "Protocol/ale_version_caps.h"
#include "Word/ale_word.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace ale;
using namespace ale::version_caps;

// ── TEST 1 ────────────────────────────────────────────────────────────────────
void test_summary_cmd_preamble() {
    std::cout << "[TEST 1] encode_version_cmd summary — CMD preamble\n";

    VersionCmd req{KVC_ALL, KVF_SUMMARY};
    ALEWord word = encode_version_cmd(req);

    assert(word.type == PreambleType::CMD);
    assert(word.valid == true);

    std::cout << "  preamble = CMD  PASSED\n\n";
}

// ── TEST 2 ────────────────────────────────────────────────────────────────────
void test_summary_payload_chars() {
    std::cout << "[TEST 2] encode_version_cmd summary — payload 'v', '/', 's'\n";

    VersionCmd req{KVC_ALL, KVF_SUMMARY};
    ALEWord word = encode_version_cmd(req);

    // Extract raw 7-bit chars from 21-bit payload
    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((word.raw_payload >>  0u) & 0x7Fu);

    assert(c0 == 'v');
    assert(c1 == '/');
    assert(c2 == 's');

    // address[] convenience copy must match
    assert(word.address[0] == 'v');
    assert(word.address[1] == '/');
    assert(word.address[2] == 's');
    assert(word.address[3] == '\0');

    std::cout << "  payload = 'v'(0x"
              << std::hex << static_cast<int>(c0) << ") '/'(0x"
              << static_cast<int>(c1) << ") 's'(0x"
              << static_cast<int>(c2) << std::dec << ")  PASSED\n\n";
}

// ── TEST 3 ────────────────────────────────────────────────────────────────────
void test_full_payload_char3() {
    std::cout << "[TEST 3] encode_version_cmd full — payload char3 is 'f'\n";

    VersionCmd req{KVC_ALL, KVF_FULL};
    ALEWord word = encode_version_cmd(req);

    assert(word.type == PreambleType::CMD);
    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((word.raw_payload >>  0u) & 0x7Fu);

    assert(c0 == 'v');
    assert(c1 == '/');
    assert(c2 == 'f');
    (void)c0; (void)c1; // suppress unused-in-Release warnings (assert is no-op)

    std::cout << "  payload char3 = 'f'(0x" << std::hex
              << static_cast<int>(c2) << std::dec << ")  PASSED\n\n";
}

// ── TEST 4 ────────────────────────────────────────────────────────────────────
void test_is_version_cmd_detects() {
    std::cout << "[TEST 4] is_version_cmd detects VERSION CMD words\n";

    VersionCmd req{KVC_ALL, KVF_SUMMARY};
    ALEWord word = encode_version_cmd(req);
    assert(is_version_cmd(word) == true);

    // Full-format word is also detected
    VersionCmd req_f{KVC_ALL, KVF_FULL};
    ALEWord word_f = encode_version_cmd(req_f);
    assert(is_version_cmd(word_f) == true);

    std::cout << "  is_version_cmd(summary) = true  PASSED\n";
    std::cout << "  is_version_cmd(full)    = true  PASSED\n\n";
}

// ── TEST 5 ────────────────────────────────────────────────────────────────────
void test_decode_roundtrip() {
    std::cout << "[TEST 5] decode_version_cmd round-trip\n";

    // Summary
    {
        VersionCmd req{KVC_ALL, KVF_SUMMARY};
        ALEWord    word = encode_version_cmd(req);
        VersionCmd got  = decode_version_cmd(word);
        assert(got.kvf_mask == KVF_SUMMARY);
        assert(got.kvc_mask == KVC_ALL);
    }

    // Full
    {
        VersionCmd req{KVC_ALL, KVF_FULL};
        ALEWord    word = encode_version_cmd(req);
        VersionCmd got  = decode_version_cmd(word);
        assert(got.kvf_mask == KVF_FULL);
        assert(got.kvc_mask == KVC_ALL);
    }

    std::cout << "  summary round-trip  PASSED\n";
    std::cout << "  full    round-trip  PASSED\n\n";
}

// ── TEST 6 ────────────────────────────────────────────────────────────────────
void test_non_version_rejected() {
    std::cout << "[TEST 6] non-VERSION CMD words rejected by is_version_cmd\n";

    // Non-CMD preamble
    ALEWord to_word;
    to_word.type        = PreambleType::TO;
    to_word.raw_payload = (static_cast<uint32_t>('v') << 14u)
                        | (static_cast<uint32_t>('/') <<  7u)
                        |  static_cast<uint32_t>('s');
    to_word.valid = true;
    assert(is_version_cmd(to_word) == false);

    // CMD but wrong family char
    ALEWord other_cmd;
    other_cmd.type        = PreambleType::CMD;
    other_cmd.raw_payload = (static_cast<uint32_t>('c') << 14u)
                          | (static_cast<uint32_t>('/') <<  7u)
                          |  static_cast<uint32_t>('q');
    other_cmd.valid = true;
    assert(is_version_cmd(other_cmd) == false);

    std::cout << "  TO word with v/s payload rejected   PASSED\n";
    std::cout << "  CMD word with c/q payload rejected  PASSED\n\n";
}

// ── TEST 7 ────────────────────────────────────────────────────────────────────
void test_capabilities_query_preamble() {
    std::cout << "[TEST 7] encode_capabilities_query — CMD preamble\n";

    CapabilitiesQuery qry{true};
    ALEWord word = encode_capabilities_query(qry);

    assert(word.type == PreambleType::CMD);
    assert(word.valid == true);

    std::cout << "  preamble = CMD  PASSED\n\n";
}

// ── TEST 8 ────────────────────────────────────────────────────────────────────
void test_capabilities_query_payload_chars() {
    std::cout << "[TEST 8] encode_capabilities_query — payload 'c', '/', 'q'\n";

    CapabilitiesQuery qry{true};
    ALEWord word = encode_capabilities_query(qry);

    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((word.raw_payload >>  0u) & 0x7Fu);

    assert(c0 == 'c');
    assert(c1 == '/');
    assert(c2 == 'q');

    assert(word.address[0] == 'c');
    assert(word.address[1] == '/');
    assert(word.address[2] == 'q');
    assert(word.address[3] == '\0');

    std::cout << "  payload = 'c'(0x"
              << std::hex << static_cast<int>(c0) << ") '/'(0x"
              << static_cast<int>(c1) << ") 'q'(0x"
              << static_cast<int>(c2) << std::dec << ")  PASSED\n\n";
}

// ── TEST 9 ────────────────────────────────────────────────────────────────────
void test_is_capabilities_query_detects() {
    std::cout << "[TEST 9] is_capabilities_query detects CAPABILITIES QUERY words\n";

    CapabilitiesQuery qry{true};
    ALEWord word = encode_capabilities_query(qry);
    assert(is_capabilities_query(word) == true);

    std::cout << "  is_capabilities_query(c/q) = true  PASSED\n\n";
}

// ── TEST 10 ───────────────────────────────────────────────────────────────────
void test_non_capabilities_query_rejected() {
    std::cout << "[TEST 10] non-capabilities-query words rejected by is_capabilities_query\n";

    // Non-CMD preamble
    ALEWord to_word;
    to_word.type        = PreambleType::TO;
    to_word.raw_payload = (static_cast<uint32_t>('c') << 14u)
                        | (static_cast<uint32_t>('/') <<  7u)
                        |  static_cast<uint32_t>('q');
    to_word.valid = true;
    assert(is_capabilities_query(to_word) == false);

    // CMD but VERSION family (v/s)
    VersionCmd req{KVC_ALL, KVF_SUMMARY};
    ALEWord version_word = encode_version_cmd(req);
    assert(is_capabilities_query(version_word) == false);

    // CMD c/r (report, not query)
    ALEWord cr_word;
    cr_word.type        = PreambleType::CMD;
    cr_word.raw_payload = (static_cast<uint32_t>('c') << 14u)
                        | (static_cast<uint32_t>('/') <<  7u)
                        |  static_cast<uint32_t>('r');
    cr_word.valid = true;
    assert(is_capabilities_query(cr_word) == false);

    std::cout << "  TO word with c/q payload rejected   PASSED\n";
    std::cout << "  CMD v/s (VERSION) rejected           PASSED\n";
    std::cout << "  CMD c/r (REPORT)  rejected           PASSED\n\n";
}

// ── TEST 11 ───────────────────────────────────────────────────────────────────
void test_capabilities_report_cmd_word()
{
    std::cout << "[TEST 11] encode_capabilities_report — first word is CMD c/r\n";

    CapabilitiesReport rpt{};
    auto frame = encode_capabilities_report(rpt);

    assert(frame[0].type  == PreambleType::CMD);
    assert(frame[0].valid == true);

    const char c0 = static_cast<char>((frame[0].raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((frame[0].raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((frame[0].raw_payload >>  0u) & 0x7Fu);

    assert(c0 == 'c');
    assert(c1 == '/');
    assert(c2 == 'r');
    (void)c0; (void)c1; (void)c2;

    assert(frame[0].address[0] == 'c');
    assert(frame[0].address[1] == '/');
    assert(frame[0].address[2] == 'r');
    assert(frame[0].address[3] == '\0');

    std::cout << "  CMD preamble          PASSED\n";
    std::cout << "  payload 'c'/'/'/'r'   PASSED\n\n";
}

// ── TEST 12 ───────────────────────────────────────────────────────────────────
void test_capabilities_report_five_data_words()
{
    std::cout << "[TEST 12] encode_capabilities_report — 5 DATA/REP words, preamble sequence\n";

    CapabilitiesReport rpt{};
    auto frame = encode_capabilities_report(rpt);

    // Exactly 6 words: 1 CMD + 5 DATA/REP
    assert(frame.size() == 6u);

    // Spec A.5.6.6.2.2: CMD, DATA, REP, DATA, REP, DATA
    assert(frame[0].type == PreambleType::CMD);
    assert(frame[1].type == PreambleType::DATA);
    assert(frame[2].type == PreambleType::REP);
    assert(frame[3].type == PreambleType::DATA);
    assert(frame[4].type == PreambleType::REP);
    assert(frame[5].type == PreambleType::DATA);

    for (std::size_t i = 0; i < frame.size(); ++i)
        assert(frame[i].valid == true);

    std::cout << "  frame size = 6        PASSED\n";
    std::cout << "  frame[0] = CMD        PASSED\n";
    std::cout << "  frame[1] = DATA       PASSED\n";
    std::cout << "  frame[2] = REP        PASSED\n";
    std::cout << "  frame[3] = DATA       PASSED\n";
    std::cout << "  frame[4] = REP        PASSED\n";
    std::cout << "  frame[5] = DATA       PASSED\n\n";
}

// ── TEST 13 ───────────────────────────────────────────────────────────────────
void test_is_capabilities_report_detects()
{
    std::cout << "[TEST 13] is_capabilities_report — detects c/r CMD; rejects others\n";

    // Encoded report CMD word must be detected
    CapabilitiesReport rpt{};
    auto frame = encode_capabilities_report(rpt);
    assert(is_capabilities_report(frame[0]) == true);

    // c/q (query) is NOT a report
    CapabilitiesQuery qry{true};
    ALEWord qword = encode_capabilities_query(qry);
    assert(is_capabilities_report(qword) == false);

    // Non-CMD preamble with c/r payload is rejected
    ALEWord data_cr;
    data_cr.type        = PreambleType::DATA;
    data_cr.raw_payload = (static_cast<uint32_t>('c') << 14u)
                        | (static_cast<uint32_t>('/') <<  7u)
                        |  static_cast<uint32_t>('r');
    data_cr.valid = true;
    assert(is_capabilities_report(data_cr) == false);

    // VERSION CMD v/s is NOT a report
    VersionCmd vcmd{KVC_ALL, KVF_SUMMARY};
    ALEWord vword = encode_version_cmd(vcmd);
    assert(is_capabilities_report(vword) == false);

    std::cout << "  is_capabilities_report(c/r CMD)  = true   PASSED\n";
    std::cout << "  is_capabilities_report(c/q CMD)  = false  PASSED\n";
    std::cout << "  is_capabilities_report(c/r DATA) = false  PASSED\n";
    std::cout << "  is_capabilities_report(v/s CMD)  = false  PASSED\n\n";
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== test_version_caps (AC-GEN-011-001, AC-GEN-012-001, AC-GEN-012-002) ===\n\n";

    test_summary_cmd_preamble();
    test_summary_payload_chars();
    test_full_payload_char3();
    test_is_version_cmd_detects();
    test_decode_roundtrip();
    test_non_version_rejected();
    test_capabilities_query_preamble();
    test_capabilities_query_payload_chars();
    test_is_capabilities_query_detects();
    test_non_capabilities_query_rejected();
    test_capabilities_report_cmd_word();
    test_capabilities_report_five_data_words();
    test_is_capabilities_report_detects();

    std::cout << "=== ALL TESTS PASSED ===\n";
    return 0;
}
