/**
 * \file tests/gen/unit/test_cmd_function_decode.cpp
 * \brief Tests for decode_cmd_function() — TABLE A-XVI CMD function decode
 *
 * MIL-STD-188-141B A.5.6 / TABLE A-XVI (Summary of CMD functions):
 *   Word layout: W1-W3 CMD preamble | W4-W10 first char (7-bit ASCII) |
 *                W11-W17 second char (two-char functions) | payload bits
 *   CMD function characters occupy the 0x60-0x7F range; the 0x20-0x5F range
 *   is the message-word range (AMD any Expanded-64 char, DTM/DBM by text).
 *
 * Verifies:
 *   TEST 1  one-character functions decode with name set, second == 0
 *   TEST 2  two-character groups ('m', 't', 'v') decode via second char
 *   TEST 3  'x'/'y'/'z'/'{' decode as CRC (A.5.6.1), second cleared
 *   TEST 4  group first char with unknown second char → unknown (name == nullptr)
 *   TEST 5  first chars absent from TABLE A-XVI → unknown (name == nullptr)
 *   TEST 6  0x20-0x5F message range decodes as AMD header
 *   TEST 7  bit extraction: first/second char slots map to W4-W10/W11-W17
 *   TEST 8  cross-check against the authoritative encoders:
 *            encode_amd()[0] → AMD, encode_dtm()[0] → DTM, encode_dbm()[0] → DBM
 */

#include "Protocol/Message/ale_orderwire_protocols.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using namespace ale;

// Build a CMD word's 21-bit payload from its character slots.
static uint32_t cmd_raw(uint8_t first, uint8_t second = 0, uint8_t payload7 = 0)
{
    return (static_cast<uint32_t>(first & 0x7Fu) << 14)
         | (static_cast<uint32_t>(second & 0x7Fu) << 7)
         | static_cast<uint32_t>(payload7 & 0x7Fu);
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_single_char_functions()
{
    std::cout << "[TEST 1] one-character functions — name set, second == 0\n";

    const struct { char c; const char* expect; } cases[] = {
        { '`', "Advanced LQA" },  { 'a', "LQA" },      { 'b', "Data block analysis" },
        { 'c', "Channels" },      { 'd', "DTM" },      { 'f', "Frequency select" },
        { 'n', "Noise report" },  { 'p', "Power control" },
        { 'r', "LQA report" },    { '|', "User-unique" }, { '~', "Time exchange" },
    };
    for (const auto& tc : cases) {
        const CmdFunctionInfo f = decode_cmd_function(cmd_raw(tc.c, 0, 0x2Au));
        assert(f.name && "single-char CMD must decode to a name");
        assert(std::string(f.name).find(tc.expect) == 0 && "name must match TABLE A-XVI entry");
        assert(f.first == tc.c && "first char must round-trip");
        assert(f.second == 0 && "one-character functions carry no second char");
        std::cout << "  '" << tc.c << "' -> " << f.name << "  OK\n";
    }
    std::cout << "  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_two_char_functions()
{
    std::cout << "[TEST 2] two-character groups — second char selects the function\n";

    const struct { char a; char b; const char* expect; } cases[] = {
        { 'm', 'n', "Mode: modem negotiation" },
        { 'm', 'q', "Mode: digital squelch" },
        { 't', 't', "Scheduling: tune and wait" },
        { 't', 'h', "Scheduling: halt and wait" },
        { 't', 'x', "Scheduling: do not respond" },
        { 't', 'z', "Scheduling: zulu time" },
        { 'v', 'c', "Capabilities" },
        { 'v', 's', "Version" },
    };
    for (const auto& tc : cases) {
        const CmdFunctionInfo f = decode_cmd_function(cmd_raw(tc.a, tc.b, 0x15u));
        assert(f.name && std::string(f.name).find(tc.expect) == 0
               && "two-char CMD must decode to its TABLE A-XVI function");
        assert(f.first == tc.a && f.second == tc.b && "both chars must round-trip");
        std::cout << "  '" << tc.a << tc.b << "' -> " << f.name << "  OK\n";
    }
    std::cout << "  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_crc_first_chars()
{
    std::cout << "[TEST 3] 'x'/'y'/'z'/'{' decode as CRC (A.5.6.1)\n";

    static const uint8_t crc_first_chars[] = { 'x', 'y', 'z', '{' };
    for (const uint8_t c : crc_first_chars) {
        // FCS bits X15/X14 live in the first char's two LSBs — exercise all
        // four low-bit combinations so a payload-driven miscall can't hide.
        for (uint8_t low = 0; low < 4; ++low) {
            const CmdFunctionInfo f = decode_cmd_function(
                cmd_raw(static_cast<uint8_t>(c | low), 0, 0x7Fu));
            assert(f.name && std::string(f.name).find("CRC") == 0
                   && "CRC first char must decode as CRC regardless of FCS bits");
            assert(f.second == 0 && "CRC words carry FCS bits, not a second char");
        }
        std::cout << "  '" << c << "' -> CRC (all X15/X14 combos)  OK\n";
    }
    std::cout << "  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_group_char_unknown_second()
{
    std::cout << "[TEST 4] group first char + unknown second char → unknown\n";

    // 'e' is not a TABLE A-XVI second character in any group.
    const CmdFunctionInfo f = decode_cmd_function(cmd_raw('t', 'e', 0x00u));
    assert(f.name == nullptr && "no TABLE A-XVI entry for 't''e' — must be unknown");
    assert(f.first == 't' && f.second == 'e' && "chars still extracted for the log line");
    std::cout << "  't''e' -> unknown (chars preserved)  PASSED\n\n";
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
void test_unknown_first_chars()
{
    std::cout << "[TEST 5] first chars absent from TABLE A-XVI → unknown\n";

    // Unused codes inside the function range, plus its edges. (0x60 '`' is
    // NOT here — it is TABLE A-XVI's Advanced LQA entry.)
    static const uint8_t unknown_first_chars[] = { 0x00u, 0x1Fu, 0x65u, 0x77u, 0x7Du, 0x7Fu };
    for (const uint8_t c : unknown_first_chars) {
        const CmdFunctionInfo f = decode_cmd_function(cmd_raw(c, 0x33u, 0x00u));
        assert(f.name == nullptr && "unused first char must decode as unknown");
    }
    std::cout << "  0x00/0x1F/0x65('e')/0x77('w')/0x7D/0x7F -> unknown  PASSED\n\n";
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
void test_message_range_is_amd()
{
    std::cout << "[TEST 6] 0x20-0x5F message range decodes as AMD header\n";

    const CmdFunctionInfo f = decode_cmd_function(cmd_raw('H', 0, 0x00u));
    assert(f.name && std::string(f.name).find("AMD") == 0
           && "Expanded-64 first char must decode as AMD header");
    std::cout << "  'H' -> " << f.name << "  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
void test_bit_extraction()
{
    std::cout << "[TEST 7] char slots map to W4-W10 / W11-W17\n";

    // Hand-built word: 't'(0x74) 'r'(0x72) + 7 payload bits 0b0101010.
    const uint32_t raw = cmd_raw('t', 'r', 0x2Au);
    const CmdFunctionInfo f = decode_cmd_function(raw);
    assert(f.first == 't' && "first char must come from bits 20-14");
    assert(f.second == 'r' && "second char must come from bits 13-7");
    assert(f.name && std::string(f.name).find("Scheduling: respond and wait") == 0);
    std::cout << "  raw=0x" << std::hex << raw << std::dec
              << " -> 't''r' (respond and wait)  PASSED\n\n";
}

// ── TEST 8 ───────────────────────────────────────────────────────────────────
void test_encoder_cross_check()
{
    std::cout << "[TEST 8] cross-check against encode_amd/encode_dtm/encode_dbm\n";

    const auto amd = encode_amd("HELLO WORLD");
    assert(!amd.empty() && amd[0].type == PreambleType::CMD);
    const CmdFunctionInfo fa = decode_cmd_function(amd[0].raw_payload);
    assert(fa.name && std::string(fa.name).find("AMD") == 0
           && "encode_amd CMD word must decode as AMD header");

    const auto dtm = encode_dtm("X");
    assert(!dtm.empty() && dtm[0].type == PreambleType::CMD);
    const CmdFunctionInfo fd = decode_cmd_function(dtm[0].raw_payload);
    assert(fd.name && std::string(fd.name).find("DTM") == 0
           && "encode_dtm CMD word (Basic-38 \"DTM\") must decode as DTM");

    const auto dbm = encode_dbm({ 0x01 });
    assert(!dbm.empty() && dbm[0].type == PreambleType::CMD);
    const CmdFunctionInfo fb = decode_cmd_function(dbm[0].raw_payload);
    assert(fb.name && std::string(fb.name).find("DBM") == 0
           && "encode_dbm CMD word (Basic-38 \"DBM\") must decode as DBM");

    std::cout << "  AMD/DTM/DBM encoder words decode correctly  PASSED\n\n";
}

int main()
{
    test_single_char_functions();
    test_two_char_functions();
    test_crc_first_chars();
    test_group_char_unknown_second();
    test_unknown_first_chars();
    test_message_range_is_amd();
    test_bit_extraction();
    test_encoder_cross_check();

    std::cout << "ALL CMD FUNCTION DECODE TESTS PASSED\n";
    return 0;
}