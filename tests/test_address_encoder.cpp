/**
 * \file test_address_encoder.cpp
 * \brief Unit and roundtrip tests for AddressEncoder.
 *
 * Test strategy
 * ─────────────
 * 1. encode() — verify word count, preamble types, and address fields for
 *    addresses of every relevant length (3, 4, 5, 6, 7, 9, 12, 15 chars).
 *
 * 2. encode_first() — verify it always returns exactly one word carrying only
 *    the first 3 chars, regardless of address length.
 *
 * 3. encode_group() — verify multi-address interleaving for all combinations
 *    of single-word and multi-word addresses.
 *
 * 4. Roundtrip — for every encode() and encode_group() case, pass the result
 *    to FrameValidator::reconstruct_to_addresses() and assert the original
 *    address(es) come back unchanged.  This verifies that AddressEncoder is
 *    the exact inverse of the decoder.
 *
 * 5. first_word_type — verify that non-TO anchor types (TIS, TWAS, FROM) are
 *    placed correctly in the first word while extension types are unaffected.
 */

#include "Word/ale_word.h"
#include "Word/address_encoder.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>

namespace ale {

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool addr_eq(const ALEWord& w, const char* expected3) {
    return strncmp(w.address, expected3, 3) == 0;
}

// ── encode() tests ────────────────────────────────────────────────────────────

bool test_encode_3chars()
{
    std::cout << "\n[AddressEncoder] encode(): 3-char address (1 word)\n";
    auto words = AddressEncoder::encode("W1A", WordType::TO);

    bool count = words.size() == 1;
    bool type  = count && words[0].type == WordType::TO;
    bool addr  = count && addr_eq(words[0], "W1A");
    bool valid = count && words[0].valid;

    bool pass = count && type && addr && valid;
    std::cout << "  \"W1A\" → 1×TO:W1A: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_4chars()
{
    std::cout << "\n[AddressEncoder] encode(): 4-char address (2 words)\n";
    // "W1AW" → [TO:W1A, DATA:W@@]
    auto words = AddressEncoder::encode("W1AW", WordType::TO);

    bool count = words.size() == 2;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "W1A");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "W@@");

    bool pass = count && t0 && t1;
    std::cout << "  \"W1AW\" → [TO:W1A, DATA:W@@]: " << (pass ? "PASS" : "FAIL") << "\n";
    if (!pass && count >= 2)
        std::cout << "    got [" << WordParser::word_type_name(words[0].type)
                  << ":" << std::string(words[0].address, 3)
                  << ", " << WordParser::word_type_name(words[1].type)
                  << ":" << std::string(words[1].address, 3) << "]\n";
    return pass;
}

bool test_encode_6chars()
{
    std::cout << "\n[AddressEncoder] encode(): 6-char address (2 words)\n";
    // "EDWARD" → [TO:EDW, DATA:ARD]
    auto words = AddressEncoder::encode("EDWARD", WordType::TO);

    bool count = words.size() == 2;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "EDW");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "ARD");

    bool pass = count && t0 && t1;
    std::cout << "  \"EDWARD\" → [TO:EDW, DATA:ARD]: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_7chars()
{
    std::cout << "\n[AddressEncoder] encode(): 7-char address (3 words)\n";
    // "EDWARDS" → [TO:EDW, DATA:ARD, REP:S@@]
    auto words = AddressEncoder::encode("EDWARDS", WordType::TO);

    bool count = words.size() == 3;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "EDW");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "ARD");
    bool t2    = count && words[2].type == WordType::REP  && addr_eq(words[2], "S@@");

    bool pass = count && t0 && t1 && t2;
    std::cout << "  \"EDWARDS\" → [TO:EDW, DATA:ARD, REP:S@@]: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_9chars()
{
    std::cout << "\n[AddressEncoder] encode(): 9-char address (3 words, no padding)\n";
    // "CALLSIGNX" → [TO:CAL, DATA:LSI, REP:GNX]
    auto words = AddressEncoder::encode("CALLSIGNX", WordType::TO);

    bool count = words.size() == 3;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "CAL");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "LSI");
    bool t2    = count && words[2].type == WordType::REP  && addr_eq(words[2], "GNX");

    bool pass = count && t0 && t1 && t2;
    std::cout << "  \"CALLSIGNX\" → [TO:CAL, DATA:LSI, REP:GNX]: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_12chars()
{
    std::cout << "\n[AddressEncoder] encode(): 12-char address (4 words)\n";
    // "LONGERCALLXY" → [TO:LON, DATA:GER, REP:CAL, DATA:LXY]
    auto words = AddressEncoder::encode("LONGERCALLXY", WordType::TO);

    bool count = words.size() == 4;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "LON");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "GER");
    bool t2    = count && words[2].type == WordType::REP  && addr_eq(words[2], "CAL");
    bool t3    = count && words[3].type == WordType::DATA && addr_eq(words[3], "LXY");

    bool pass = count && t0 && t1 && t2 && t3;
    std::cout << "  \"LONGERCALLXY\" → 4 words [TO,DATA,REP,DATA]: "
              << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_15chars()
{
    std::cout << "\n[AddressEncoder] encode(): 15-char address (5 words, max)\n";
    // "VERYLONGCALLSIG" → [TO:VER, DATA:YLO, REP:NGC, DATA:ALL, REP:SIG]
    auto words = AddressEncoder::encode("VERYLONGCALLSIG", WordType::TO);

    bool count = words.size() == 5;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "VER");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "YLO");
    bool t2    = count && words[2].type == WordType::REP  && addr_eq(words[2], "NGC");
    bool t3    = count && words[3].type == WordType::DATA && addr_eq(words[3], "ALL");
    bool t4    = count && words[4].type == WordType::REP  && addr_eq(words[4], "SIG");

    bool pass = count && t0 && t1 && t2 && t3 && t4;
    std::cout << "  \"VERYLONGCALLSIG\" → 5 words [TO,DATA,REP,DATA,REP]: "
              << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_truncates_at_15()
{
    std::cout << "\n[AddressEncoder] encode(): address >15 chars is truncated to 15\n";
    // 18-char address → must produce exactly 5 words (15 chars)
    auto words = AddressEncoder::encode("ABCDEFGHIJKLMNOPQR", WordType::TO);
    bool pass = words.size() == 5;
    std::cout << "  18-char addr → 5 words: " << (pass ? "PASS" : "FAIL")
              << " (got " << words.size() << ")\n";
    return pass;
}

bool test_encode_anchor_type()
{
    std::cout << "\n[AddressEncoder] encode(): anchor type is placed in first word only\n";
    bool all_pass = true;

    struct Case { WordType anchor; const char* name; };
    const Case cases[] = {
        { WordType::TO,   "TO"   },
        { WordType::TIS,  "TIS"  },
        { WordType::TWAS, "TWAS" },
        { WordType::FROM, "FROM" },
    };

    for (const auto& c : cases) {
        // 6-char address → [anchor:XXX, DATA:YYY]
        auto words = AddressEncoder::encode("ABCDEF", c.anchor);
        bool first_ok = words.size() >= 1 && words[0].type == c.anchor;
        bool ext_ok   = words.size() >= 2 && words[1].type == WordType::DATA;
        bool pass = first_ok && ext_ok;
        all_pass &= pass;
        std::cout << "  anchor=" << c.name << ": first=" << c.name
                  << " ext=DATA: " << (pass ? "PASS" : "FAIL") << "\n";
    }
    return all_pass;
}

// ── encode_first() tests ──────────────────────────────────────────────────────

bool test_encode_first_always_one_word()
{
    std::cout << "\n[AddressEncoder] encode_first(): always returns exactly 1 word\n";
    bool all_pass = true;

    struct Case { const char* addr; const char* expected3; };
    const Case cases[] = {
        { "W1A",             "W1A" },  // 3-char: same as first word of encode()
        { "W1AWJ",           "W1A" },  // 5-char: only first 3
        { "EDWARD",          "EDW" },  // 6-char: only first 3
        { "VERYLONGCALLSIG", "VER" },  // 15-char: only first 3
    };

    for (const auto& c : cases) {
        ALEWord w = AddressEncoder::encode_first(c.addr, WordType::TO);
        bool type_ok = w.type == WordType::TO;
        bool addr_ok = addr_eq(w, c.expected3);
        bool pass    = type_ok && addr_ok;
        all_pass &= pass;
        std::cout << "  \"" << c.addr << "\" → TO:" << c.expected3 << ": "
                  << (pass ? "PASS" : "FAIL");
        if (!addr_ok)
            std::cout << " (got " << std::string(w.address, 3) << ")";
        std::cout << "\n";
    }
    return all_pass;
}

bool test_encode_first_equals_encode_front()
{
    std::cout << "\n[AddressEncoder] encode_first() == encode().front() for all lengths\n";
    bool all_pass = true;

    const char* addrs[] = { "A", "ABC", "ABCD", "ABCDEF", "ABCDEFGHI", "ABCDEFGHIJKLMNO" };
    for (const char* addr : addrs) {
        ALEWord first    = AddressEncoder::encode_first(addr, WordType::TO);
        ALEWord via_full = AddressEncoder::encode(addr, WordType::TO).front();

        bool type_eq = first.type == via_full.type;
        bool addr_eq_r = strncmp(first.address, via_full.address, 3) == 0;
        bool pass = type_eq && addr_eq_r;
        all_pass &= pass;
        std::cout << "  \"" << addr << "\": " << (pass ? "PASS" : "FAIL") << "\n";
    }
    return all_pass;
}

// ── encode_group() tests ──────────────────────────────────────────────────────

bool test_encode_group_two_single_word()
{
    std::cout << "\n[AddressEncoder] encode_group(): two single-word addresses\n";
    // ["BOB","SAM"] → [TO:BOB, REP:SAM]
    auto words = AddressEncoder::encode_group({"BOB","SAM"}, WordType::TO);

    bool count = words.size() == 2;
    bool t0    = count && words[0].type == WordType::TO  && addr_eq(words[0], "BOB");
    bool t1    = count && words[1].type == WordType::REP && addr_eq(words[1], "SAM");

    bool pass = count && t0 && t1;
    std::cout << "  [\"BOB\",\"SAM\"] → [TO:BOB, REP:SAM]: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_group_second_addr_extended()
{
    std::cout << "\n[AddressEncoder] encode_group(): second address is multi-word\n";
    // ["BOB","SAMUEL"] → [TO:BOB, REP:SAM, DATA:UEL]
    auto words = AddressEncoder::encode_group({"BOB","SAMUEL"}, WordType::TO);

    bool count = words.size() == 3;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "BOB");
    bool t1    = count && words[1].type == WordType::REP  && addr_eq(words[1], "SAM");
    bool t2    = count && words[2].type == WordType::DATA && addr_eq(words[2], "UEL");

    bool pass = count && t0 && t1 && t2;
    std::cout << "  [\"BOB\",\"SAMUEL\"] → [TO:BOB, REP:SAM, DATA:UEL]: "
              << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_group_first_addr_extended()
{
    std::cout << "\n[AddressEncoder] encode_group(): first address is multi-word\n";
    // ["ROBERT","SAM"] → [TO:ROB, DATA:ERT, TO:SAM]
    // (REP-after-DATA would extend, so a fresh TO is needed for the 2nd address)
    auto words = AddressEncoder::encode_group({"ROBERT","SAM"}, WordType::TO);

    bool count = words.size() == 3;
    bool t0    = count && words[0].type == WordType::TO   && addr_eq(words[0], "ROB");
    bool t1    = count && words[1].type == WordType::DATA && addr_eq(words[1], "ERT");
    bool t2    = count && words[2].type == WordType::TO   && addr_eq(words[2], "SAM");

    bool pass = count && t0 && t1 && t2;
    std::cout << "  [\"ROBERT\",\"SAM\"] → [TO:ROB, DATA:ERT, TO:SAM]: "
              << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_group_three_single_word()
{
    std::cout << "\n[AddressEncoder] encode_group(): three single-word addresses\n";
    // ["BOB","SAM","TOM"] → [TO:BOB, REP:SAM, REP:TOM]
    auto words = AddressEncoder::encode_group({"BOB","SAM","TOM"}, WordType::TO);

    bool count = words.size() == 3;
    bool t0    = count && words[0].type == WordType::TO  && addr_eq(words[0], "BOB");
    bool t1    = count && words[1].type == WordType::REP && addr_eq(words[1], "SAM");
    bool t2    = count && words[2].type == WordType::REP && addr_eq(words[2], "TOM");

    bool pass = count && t0 && t1 && t2;
    std::cout << "  [\"BOB\",\"SAM\",\"TOM\"] → [TO:BOB, REP:SAM, REP:TOM]: "
              << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool test_encode_group_empty()
{
    std::cout << "\n[AddressEncoder] encode_group(): empty input → empty output\n";
    auto words = AddressEncoder::encode_group({}, WordType::TO);
    bool pass = words.empty();
    std::cout << "  {} → []: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// ── Roundtrip tests ───────────────────────────────────────────────────────────

bool test_roundtrip_single(const std::string& addr)
{
    auto words     = AddressEncoder::encode(addr, WordType::TO);
    auto recovered = FrameValidator::reconstruct_to_addresses(words);

    bool pass = (recovered.size() == 1) && (recovered[0] == addr);
    std::cout << "  \"" << addr << "\": " << (pass ? "PASS" : "FAIL");
    if (!pass) {
        std::cout << " (got ";
        for (size_t i = 0; i < recovered.size(); ++i) {
            if (i) std::cout << ",";
            std::cout << "\"" << recovered[i] << "\"";
        }
        std::cout << ")";
    }
    std::cout << "\n";
    return pass;
}

bool test_roundtrip_all_single_addresses()
{
    std::cout << "\n[AddressEncoder] roundtrip encode→reconstruct (single address)\n";
    bool all_pass = true;
    all_pass &= test_roundtrip_single("SAM");
    all_pass &= test_roundtrip_single("W1AW");
    all_pass &= test_roundtrip_single("MIAMI");
    all_pass &= test_roundtrip_single("EDWARD");
    all_pass &= test_roundtrip_single("EDWARDS");
    all_pass &= test_roundtrip_single("CALLSIGNX");
    all_pass &= test_roundtrip_single("LONGERCALLXY");
    all_pass &= test_roundtrip_single("VERYLONGCALLSIG");
    return all_pass;
}

bool test_roundtrip_group(const std::vector<std::string>& addrs)
{
    auto words     = AddressEncoder::encode_group(addrs, WordType::TO);
    auto recovered = FrameValidator::reconstruct_to_addresses(words);

    bool pass = (recovered == addrs);
    std::cout << "  [";
    for (size_t i = 0; i < addrs.size(); ++i) { if (i) std::cout << ","; std::cout << "\"" << addrs[i] << "\""; }
    std::cout << "]: " << (pass ? "PASS" : "FAIL");
    if (!pass) {
        std::cout << " (got [";
        for (size_t i = 0; i < recovered.size(); ++i) { if (i) std::cout << ","; std::cout << "\"" << recovered[i] << "\""; }
        std::cout << "])";
    }
    std::cout << "\n";
    return pass;
}

bool test_roundtrip_all_group_addresses()
{
    std::cout << "\n[AddressEncoder] roundtrip encode_group→reconstruct\n";
    bool all_pass = true;
    all_pass &= test_roundtrip_group({"BOB","SAM"});
    all_pass &= test_roundtrip_group({"BOB","SAMUEL"});
    all_pass &= test_roundtrip_group({"ROBERT","SAM"});
    all_pass &= test_roundtrip_group({"BOB","SAM","TOM"});
    all_pass &= test_roundtrip_group({"ROBERT","SAM","ALICE"});
    return all_pass;
}

// ── Main runner ───────────────────────────────────────────────────────────────

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  AddressEncoder — Unit & Roundtrip Tests                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) ++pass_count;
        else { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    // encode()
    run("encode 3-char",                    test_encode_3chars());
    run("encode 4-char",                    test_encode_4chars());
    run("encode 6-char",                    test_encode_6chars());
    run("encode 7-char",                    test_encode_7chars());
    run("encode 9-char",                    test_encode_9chars());
    run("encode 12-char",                   test_encode_12chars());
    run("encode 15-char",                   test_encode_15chars());
    run("encode truncates at 15",           test_encode_truncates_at_15());
    run("encode anchor type placement",     test_encode_anchor_type());

    // encode_first()
    run("encode_first always 1 word",       test_encode_first_always_one_word());
    run("encode_first == encode().front()", test_encode_first_equals_encode_front());

    // encode_group()
    run("encode_group 2×single-word",       test_encode_group_two_single_word());
    run("encode_group second addr extended",test_encode_group_second_addr_extended());
    run("encode_group first addr extended", test_encode_group_first_addr_extended());
    run("encode_group 3×single-word",       test_encode_group_three_single_word());
    run("encode_group empty input",         test_encode_group_empty());

    // roundtrip
    run("roundtrip single addresses",       test_roundtrip_all_single_addresses());
    run("roundtrip group addresses",        test_roundtrip_all_group_addresses());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Results: Passed=" << std::setw(2) << pass_count
              << "  Failed=" << std::setw(2) << fail_count
              << "                              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
