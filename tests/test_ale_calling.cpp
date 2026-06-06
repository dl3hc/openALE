/**
 * \file test_ale_calling.cpp
 * \brief Acceptance tests for FEAT-WORD-002 — Address Words (TO/TIS/TWAS/THRU/FROM)
 *
 * Covers REQ-WORD-003 through REQ-WORD-007 per MIL-STD-188-141B A.5.2.3.2.
 *
 * Timing model for state-machine tests:
 *   initiate_call() is called before any update(), so current_time_ms = 0
 *   and call_phase_start_ms = 0 at phase entry.
 *
 *   With set_target_scan_channels(0) the CALLING state enters LEADING_CALL
 *   directly. For a 3-char address addr = "ABC", tc_ms = 1 × Trw = 392 ms:
 *     update(      0) → leading seq 1 fires (TO "ABC")
 *     update(  Trw_ms) → leading seq 2 fires (TO "ABC")
 *     update(2*Trw_ms) → phase_elapsed >= 2×tc → transition to CONCLUSION (no tx)
 *     update(2*Trw_ms+1) → conclusion fires (TIS self)
 */

#include "Word/ale_word.h"
#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>

namespace ale {

// ============================================================================
// Helper
// ============================================================================

struct WordCapture {
    std::vector<ALEWord> words;
    void record(const ALEWord& w) { words.push_back(w); }
    void clear() { words.clear(); }
    size_t size() const { return words.size(); }
    bool empty() const { return words.empty(); }
};

static ALEStateMachine make_sm(WordCapture& cap,
                               const std::string& self = "SAM",
                               uint32_t scan_ch = 0)
{
    ALEStateMachine sm;
    sm.set_transmit_callback([&cap](const ALEWord& w){ cap.record(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address(self);
    sm.set_target_scan_channels(scan_ch);
    return sm;
}

// Drive through leading call and return to CONCLUSION phase.
// Works for 3-char addresses where tc_ms = 1 × Trw = 392 ms.
static void advance_to_conclusion(ALEStateMachine& sm, WordCapture& cap)
{
    // tc_ms = 392 (3-char address → 1 word → 1 × Trw)
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    sm.update(0);          // seq 1
    sm.update(Trw);        // seq 2
    sm.update(2 * Trw);    // phase_elapsed == 2×tc → transition (no tx)
    cap.clear();
    sm.update(2 * Trw + 1); // conclusion fires
}

// ============================================================================
// AC-WORD-003-1 — TO for individual calls
// REQ-WORD-003: "TO shall be used in individual call protocols for single
// stations, and in net call protocols for multiple net member stations."
// ============================================================================

bool test_ac_003_1_individual_scanning_uses_to()
{
    std::cout << "\n[AC-WORD-003-1] TO in individual-call scanning phase\n";
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/1);
    sm.initiate_call("N1XYZ");
    sm.update(0);  // first scanning slot fires at t=0

    bool ok = !cap.empty() && cap.words[0].type == WordType::TO;
    std::cout << "  scanning first word = TO: " << (ok ? "PASS" : "FAIL");
    if (!cap.empty())
        std::cout << " (got " << WordParser::word_type_name(cap.words[0].type) << ")";
    std::cout << "\n";
    return ok;
}

// REQ-WORD-003: TO used in net call protocols (word-level: TO encodes a net
// address identically to a station address — same Basic 38 character set).
bool test_ac_003_1_to_encodes_net_address()
{
    std::cout << "\n[AC-WORD-003-1] TO word type encodes net address (word level)\n";

    const char net3[3] = { 'N', 'E', 'T' };
    uint32_t payload = WordParser::encode_ascii(net3, WordType::TO);
    bool enc_ok = (payload != 0xFFFFFFFF);

    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, WordType::TO, decoded);
    bool match = dec_ok && strncmp(decoded, "NET", 3) == 0;

    bool pass = enc_ok && match;
    std::cout << "  encode/decode \"NET\" as TO: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    return pass;
}

// ============================================================================
// AC-WORD-003-2 — TO word contains the first three characters of the address
// ============================================================================

bool test_ac_003_2_to_first_three_chars()
{
    std::cout << "\n[AC-WORD-003-2] TO word = first 3 chars of the address\n";

    struct Case { const char* addr; const char expected[4]; };
    const Case cases[] = {
        { "N1XYZ",       "N1X" },
        { "NET123",      "NET" },
        { "MIAMI",       "MIA" },
        { "ABC",         "ABC" },
        { "W1ABCDEFG",   "W1A" },
    };

    bool all_pass = true;
    for (const auto& c : cases) {
        // Build TO word using the first 3 chars of the address
        char first3[3] = { c.addr[0], c.addr[1], c.addr[2] };
        uint32_t payload = WordParser::encode_ascii(first3, WordType::TO);
        char decoded[4] = {};
        bool dec_ok = WordParser::decode_ascii(payload, WordType::TO, decoded);
        bool match = dec_ok && strncmp(decoded, c.expected, 3) == 0;
        all_pass &= match;
        std::cout << "  addr=\"" << c.addr << "\" → TO[0..2]=\"" << c.expected
                  << "\": " << (match ? "PASS" : "FAIL");
        if (!match) std::cout << " (got \"" << decoded << "\")";
        std::cout << "\n";
    }

    // State-machine confirmation: scanning word carries first 3 chars
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, "SAM", 1);
        sm.initiate_call("N1XYZ");
        sm.update(0);
        bool sm_ok = !cap.empty()
                  && cap.words[0].type == WordType::TO
                  && strncmp(cap.words[0].address, "N1X", 3) == 0;
        all_pass &= sm_ok;
        std::cout << "  SM scanning word addr = \"N1X\": " << (sm_ok ? "PASS" : "FAIL");
        if (!cap.empty())
            std::cout << " (got \"" << std::string(cap.words[0].address, 3) << "\")";
        std::cout << "\n";
    }
    return all_pass;
}

// ============================================================================
// AC-WORD-003-3 — Extended addresses continue with alternating DATA and REP
// REQ-WORD-003: "Extended addresses shall be contained in immediately
// following, alternating DATA and REP words."
// Sequence: TO, DATA, REP, DATA, REP (max 5 words = 15 chars)
// ============================================================================

bool test_ac_003_3_extended_address_data_rep_sequence()
{
    std::cout << "\n[AC-WORD-003-3] Extended addresses: TO, DATA, REP, DATA, REP\n";

    struct Case {
        const char* addr;
        size_t expected_word_count;
        WordType expected_types[5];
    };

    const Case cases[] = {
        // 3 chars: TO only (no extension)
        { "ABC",             1, { WordType::TO,
                                  WordType::UNKNOWN, WordType::UNKNOWN,
                                  WordType::UNKNOWN, WordType::UNKNOWN } },
        // 6 chars: TO + DATA
        { "K6KBCD",          2, { WordType::TO,   WordType::DATA,
                                  WordType::UNKNOWN, WordType::UNKNOWN,
                                  WordType::UNKNOWN } },
        // 9 chars: TO + DATA + REP
        { "CALLSIGNX",       3, { WordType::TO,   WordType::DATA,
                                  WordType::REP,
                                  WordType::UNKNOWN, WordType::UNKNOWN } },
        // 12 chars: TO + DATA + REP + DATA
        { "LONGERCALLXY",    4, { WordType::TO,   WordType::DATA,
                                  WordType::REP,   WordType::DATA,
                                  WordType::UNKNOWN } },
        // 15 chars: TO + DATA + REP + DATA + REP
        { "VERYLONGCALLSIG", 5, { WordType::TO,   WordType::DATA,
                                  WordType::REP,   WordType::DATA,
                                  WordType::REP } },
    };

    bool all_pass = true;
    for (const auto& c : cases) {
        // Drive leading call seq 1 directly (target_scan_channels=0)
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, "SAM", 0);
        sm.initiate_call(c.addr);
        sm.update(0);  // seq 1 fires immediately

        bool count_ok = (cap.size() >= c.expected_word_count);
        bool types_ok = true;
        for (size_t i = 0; i < c.expected_word_count && i < cap.size(); ++i) {
            if (cap.words[i].type != c.expected_types[i]) {
                types_ok = false;
                break;
            }
        }

        bool pass = count_ok && types_ok;
        all_pass &= pass;

        std::cout << "  addr=\"" << c.addr << "\" (len=" << strlen(c.addr)
                  << ") → " << c.expected_word_count << " word(s) [";
        for (size_t i = 0; i < c.expected_word_count; ++i) {
            if (i) std::cout << ",";
            std::cout << WordParser::word_type_name(c.expected_types[i]);
        }
        std::cout << "]: " << (pass ? "PASS" : "FAIL");
        if (!count_ok)
            std::cout << " (got " << cap.size() << " words)";
        if (count_ok && !types_ok) {
            std::cout << " (got [";
            for (size_t i = 0; i < cap.size() && i < 5; ++i) {
                if (i) std::cout << ",";
                std::cout << WordParser::word_type_name(cap.words[i].type);
            }
            std::cout << "])";
        }
        std::cout << "\n";
    }
    return all_pass;
}

// ============================================================================
// REQ-WORD-004 — TIS: conclusion word type is TIS, carries first 3 chars of
// calling station's address; extended via DATA/REP same as TO.
// ============================================================================

bool test_tis_conclusion_word_type()
{
    std::cout << "\n[REQ-WORD-004] TIS in conclusion phase\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"SAM", 0);
    sm.initiate_call("ABC");
    advance_to_conclusion(sm, cap);  // cap now holds conclusion words

    bool has_tis = !cap.empty() && cap.words[0].type == WordType::TIS;
    std::cout << "  conclusion first word = TIS: " << (has_tis ? "PASS" : "FAIL");
    if (!cap.empty())
        std::cout << " (got " << WordParser::word_type_name(cap.words[0].type) << ")";
    std::cout << "\n";

    bool addr_ok = has_tis && strncmp(cap.words[0].address, "SAM", 3) == 0;
    std::cout << "  TIS address = \"SAM\": " << (addr_ok ? "PASS" : "FAIL");
    if (has_tis)
        std::cout << " (got \"" << std::string(cap.words[0].address, 3) << "\")";
    std::cout << "\n";

    return has_tis && addr_ok;
}

bool test_tis_extended_address()
{
    std::cout << "\n[REQ-WORD-004] TIS + DATA + REP for own address > 3 chars\n";

    // Self address "SAMUELB" (7 chars) → TIS "SAM", DATA "UEL", REP "B@@"
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAMUELB", 0);
    sm.initiate_call("ABC");

    // tc_ms for "ABC" = 1 × Trw. Drive to conclusion.
    sm.update(0); sm.update(ALETimingConstants::Trw_ms); sm.update(2 * ALETimingConstants::Trw_ms); cap.clear();
    sm.update(2 * ALETimingConstants::Trw_ms + 1);  // conclusion fires

    // Own address is 7 chars → 3 logical words: TIS + DATA + REP
    bool count_ok = cap.size() >= 3;
    bool type_tis  = count_ok && cap.words[0].type == WordType::TIS;
    bool type_data = count_ok && cap.words[1].type == WordType::DATA;
    bool type_rep  = count_ok && cap.words[2].type == WordType::REP;
    bool pass = type_tis && type_data && type_rep;

    std::cout << "  TIS+DATA+REP for \"SAMUELB\": " << (pass ? "PASS" : "FAIL");
    std::cout << " (" << cap.size() << " words [";
    for (size_t i = 0; i < cap.size() && i < 3; ++i) {
        if (i) std::cout << ",";
        std::cout << WordParser::word_type_name(cap.words[i].type);
    }
    std::cout << "])\n";
    return pass;
}

// ============================================================================
// REQ-WORD-005 — TWAS: preamble 3 (WordType::TWAS), Basic 38, distinct from TIS
// TIS and TWAS must not be used in the same frame (different preamble bits).
// ============================================================================

bool test_twas_word_encoding()
{
    std::cout << "\n[REQ-WORD-005] TWAS (WordType::TWAS) encode/decode\n";

    const char chars[3] = { 'R', 'E', 'J' };
    uint32_t payload = WordParser::encode_ascii(chars, WordType::TWAS);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, WordType::TWAS, decoded);
    bool match = dec_ok && strncmp(decoded, "REJ", 3) == 0;

    bool pass = enc_ok && match;
    std::cout << "  encode/decode \"REJ\" as TWAS: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";

    // TWAS uses Basic 38 character set
    bool basic38 = WordParser::uses_basic38(WordType::TWAS);
    std::cout << "  TWAS uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    return pass && basic38;
}

bool test_tis_twas_different_preambles()
{
    std::cout << "\n[REQ-WORD-005] TIS and TWAS have distinct preamble bits\n";

    // Per Table A-II: TIS = preamble 5, TWAS = preamble 3
    uint8_t tis_bits  = static_cast<uint8_t>(WordType::TIS);
    uint8_t twas_bits = static_cast<uint8_t>(WordType::TWAS);
    bool distinct = (tis_bits != twas_bits);
    std::cout << "  TIS=" << static_cast<int>(tis_bits)
              << " TWAS=" << static_cast<int>(twas_bits)
              << " distinct: " << (distinct ? "PASS" : "FAIL") << "\n";

    // Verify preamble round-trip through bit layout
    auto make_word = [](WordType t, const char ch[3]) -> uint32_t {
        uint32_t pl = WordParser::encode_ascii(ch, t);
        return (static_cast<uint32_t>(t) << 21) | pl;
    };
    const char abc[3] = { 'A', 'B', 'C' };
    ALEWord tis_w, twas_w;
    WordParser p;
    p.parse_from_bits(make_word(WordType::TIS, abc),  tis_w);
    p.parse_from_bits(make_word(WordType::TWAS, abc), twas_w);
    bool tis_rt  = (tis_w.type  == WordType::TIS);
    bool twas_rt = (twas_w.type == WordType::TWAS);
    std::cout << "  TIS  round-trip: " << (tis_rt  ? "PASS" : "FAIL") << "\n";
    std::cout << "  TWAS round-trip: " << (twas_rt ? "PASS" : "FAIL") << "\n";

    return distinct && tis_rt && twas_rt;
}

// ============================================================================
// REQ-WORD-006 — THRU: preamble 1, Basic 38, used in group-call scanning only.
// No extended addresses (exactly 3 chars per THRU word).
// Group-call state machine path is not yet implemented; word-level tests only.
// ============================================================================

bool test_thru_word_encoding()
{
    std::cout << "\n[REQ-WORD-006] THRU encode/decode (word level)\n";

    // THRU carries exactly 3 Basic 38 chars (no extension words)
    const char chars[3] = { 'A', 'B', 'C' };
    uint32_t payload = WordParser::encode_ascii(chars, WordType::THRU);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, WordType::THRU, decoded);
    bool match = dec_ok && strncmp(decoded, "ABC", 3) == 0;
    bool basic38 = WordParser::uses_basic38(WordType::THRU);

    bool pass = enc_ok && match && basic38;
    std::cout << "  encode/decode \"ABC\" as THRU: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    std::cout << "  THRU uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    // Preamble must be 1 per Table A-II
    bool preamble_ok = (static_cast<uint8_t>(WordType::THRU) == 1);
    std::cout << "  THRU preamble == 1: " << (preamble_ok ? "PASS" : "FAIL") << "\n";

    return pass && preamble_ok;
}

bool test_thru_rejects_invalid_basic38()
{
    std::cout << "\n[REQ-WORD-006] THRU rejects non-Basic-38 characters\n";

    // Lowercase 'a' is not in Basic 38
    const char bad[3] = { 'a', 'b', 'c' };
    uint32_t payload = WordParser::encode_ascii(bad, WordType::THRU);
    bool rejected = (payload == 0xFFFFFFFF);
    std::cout << "  lowercase chars rejected: " << (rejected ? "PASS" : "FAIL") << "\n";
    return rejected;
}

// ============================================================================
// REQ-WORD-007 — FROM: preamble 4, Basic 38, same extension structure as TO.
// Optional identifier for the sending station.
// ============================================================================

bool test_from_word_encoding()
{
    std::cout << "\n[REQ-WORD-007] FROM encode/decode (word level)\n";

    const char chars[3] = { 'W', '1', 'A' };
    uint32_t payload = WordParser::encode_ascii(chars, WordType::FROM);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, WordType::FROM, decoded);
    bool match = dec_ok && strncmp(decoded, "W1A", 3) == 0;
    bool basic38 = WordParser::uses_basic38(WordType::FROM);

    bool pass = enc_ok && match && basic38;
    std::cout << "  encode/decode \"W1A\" as FROM: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    std::cout << "  FROM uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    // Preamble must be 4 per Table A-II
    bool preamble_ok = (static_cast<uint8_t>(WordType::FROM) == 4);
    std::cout << "  FROM preamble == 4: " << (preamble_ok ? "PASS" : "FAIL") << "\n";

    return pass && preamble_ok;
}

bool test_from_extended_address_uses_data_rep()
{
    std::cout << "\n[REQ-WORD-007] FROM extended address uses DATA/REP (same as TO)\n";

    // FROM with a 6-char address: FROM "ABC" + DATA "DEF"
    WordParser p;
    auto make_word = [](WordType t, const char ch[3]) -> uint32_t {
        uint32_t pl = WordParser::encode_ascii(ch, t);
        return (static_cast<uint32_t>(t) << 21) | pl;
    };

    const char abc[3] = { 'A', 'B', 'C' };
    const char def_[3] = { 'D', 'E', 'F' };

    ALEWord from_w, data_w;
    p.parse_from_bits(make_word(WordType::FROM, abc),  from_w);
    p.parse_from_bits(make_word(WordType::DATA, def_), data_w);

    bool from_ok = (from_w.type == WordType::FROM) && strncmp(from_w.address, "ABC", 3) == 0;
    bool data_ok = (data_w.type == WordType::DATA) && strncmp(data_w.address, "DEF", 3) == 0;

    std::cout << "  FROM \"ABC\": " << (from_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  DATA \"DEF\": " << (data_ok ? "PASS" : "FAIL") << "\n";
    return from_ok && data_ok;
}

// ============================================================================
// Additional: all five routing preambles use Basic 38 character set
// ============================================================================

bool test_all_routing_preambles_use_basic38()
{
    std::cout << "\n[WORD-002] All routing preambles use Basic 38 character set\n";

    const WordType routing_types[] = {
        WordType::TO, WordType::TIS, WordType::TWAS,
        WordType::THRU, WordType::FROM
    };

    bool all_pass = true;
    for (auto t : routing_types) {
        bool b38 = WordParser::uses_basic38(t);
        all_pass &= b38;
        std::cout << "  " << WordParser::word_type_name(t)
                  << " uses Basic 38: " << (b38 ? "PASS" : "FAIL") << "\n";
    }
    return all_pass;
}

// ============================================================================
// AC-WORD-006-8/9, AC-WORD-007-8/9 — THRU and FROM preamble values are
// reserved for future indirect/relay protocols and AQC-ALE (A.5.2.3.2.4/5).
// Spec compliance is assured by the correct preamble assignment per Table A-II;
// no additional runtime check is required beyond verifying the enum values.
// ============================================================================

bool test_thru_from_preamble_reserved()
{
    std::cout << "\n[AC-WORD-006-8/9, AC-WORD-007-8/9] THRU/FROM preamble values reserved\n";

    // Per MIL-STD-188-141B Table A-II:
    //   THRU = 1  (reserved for indirect addressing / relay / AQC-ALE)
    //   FROM = 4  (reserved for indirect addressing / relay / AQC-ALE)
    bool thru_is_1 = (static_cast<uint8_t>(WordType::THRU) == 1);
    bool from_is_4 = (static_cast<uint8_t>(WordType::FROM) == 4);
    std::cout << "  THRU preamble == 1 (relay/AQC reserved): "
              << (thru_is_1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  FROM preamble == 4 (relay/AQC reserved): "
              << (from_is_4 ? "PASS" : "FAIL") << "\n";

    // Verify round-trip preserves the reserved values
    const char abc[3] = {'A','B','C'};
    ALEWord tw = WordParser::make_word(WordType::THRU, abc);
    ALEWord fw = WordParser::make_word(WordType::FROM, abc);
    bool thru_rt = tw.valid && (tw.type == WordType::THRU);
    bool from_rt = fw.valid && (fw.type == WordType::FROM);
    std::cout << "  THRU round-trip: " << (thru_rt ? "PASS" : "FAIL") << "\n";
    std::cout << "  FROM round-trip: " << (from_rt ? "PASS" : "FAIL") << "\n";

    return thru_is_1 && from_is_4 && thru_rt && from_rt;
}

// ============================================================================
// AC-WORD-007-4 — FROM appears at most once per ALE frame (A.5.2.3.2.5)
// ============================================================================

bool test_from_count_valid()
{
    std::cout << "\n[AC-WORD-007-4] FROM appears at most once per ALE frame\n";

    const char sam[3] = {'S','A','M'};
    const char cmd[3] = {'C','M','D'};

    // No FROM → valid
    std::vector<ALEWord> seq_none = {
        WordParser::make_word(WordType::TO,  sam),
        WordParser::make_word(WordType::TIS, sam),
    };
    bool v1 = FrameValidator::from_count_valid(seq_none);
    std::cout << "  no FROM: " << (v1 ? "PASS" : "FAIL") << "\n";

    // Exactly one FROM → valid
    std::vector<ALEWord> seq_one = {
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::CMD,  cmd),
    };
    bool v2 = FrameValidator::from_count_valid(seq_one);
    std::cout << "  one FROM: " << (v2 ? "PASS" : "FAIL") << "\n";

    // Two FROM words → invalid
    std::vector<ALEWord> seq_two = {
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::CMD,  cmd),
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::CMD,  cmd),
    };
    bool v3 = !FrameValidator::from_count_valid(seq_two);
    std::cout << "  two FROM rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3;
}

// ============================================================================
// AC-WORD-007-5/7 — FROM appears only immediately before CMD (A.5.2.3.2.5).
// Conformant systems ignore FROM words not in this position.
// ============================================================================

bool test_from_precedes_cmd_only()
{
    std::cout << "\n[AC-WORD-007-5/7] FROM appears only immediately before CMD\n";

    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    const char cmd[3] = {'C','M','D'};

    // FROM directly before CMD → valid
    std::vector<ALEWord> seq_direct = {
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::CMD,  cmd),
    };
    bool v1 = FrameValidator::from_precedes_cmd_only(seq_direct);
    std::cout << "  FROM, CMD: " << (v1 ? "PASS" : "FAIL") << "\n";

    // FROM + DATA address extension then CMD → valid
    std::vector<ALEWord> seq_ext = {
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::DATA, uel),
        WordParser::make_word(WordType::CMD,  cmd),
    };
    bool v2 = FrameValidator::from_precedes_cmd_only(seq_ext);
    std::cout << "  FROM, DATA, CMD: " << (v2 ? "PASS" : "FAIL") << "\n";

    // FROM followed by TIS instead of CMD → invalid
    std::vector<ALEWord> seq_no_cmd = {
        WordParser::make_word(WordType::FROM, sam),
        WordParser::make_word(WordType::TIS,  sam),
    };
    bool v3 = !FrameValidator::from_precedes_cmd_only(seq_no_cmd);
    std::cout << "  FROM, TIS (no CMD) rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // FROM at end of sequence (no following word) → invalid
    std::vector<ALEWord> seq_orphan = {
        WordParser::make_word(WordType::TO,   sam),
        WordParser::make_word(WordType::FROM, sam),
    };
    bool v4 = !FrameValidator::from_precedes_cmd_only(seq_orphan);
    std::cout << "  orphan FROM rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // No FROM → valid (vacuously true)
    std::vector<ALEWord> seq_no_from = {
        WordParser::make_word(WordType::TO,  sam),
        WordParser::make_word(WordType::TIS, sam),
    };
    bool v5 = FrameValidator::from_precedes_cmd_only(seq_no_from);
    std::cout << "  no FROM: " << (v5 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5;
}

// ============================================================================
// AC-WORD-006-1/7 — THRU only in scanning section (A.5.2.3.2.4).
// Conformant systems ignore calls that use their address in THRU outside scanning.
// ============================================================================

bool test_thru_in_scanning_only()
{
    std::cout << "\n[AC-WORD-006-1/7] THRU only in scanning section\n";

    const char abc[3] = {'A','B','C'};
    const char xyz[3] = {'X','Y','Z'};
    const char sam[3] = {'S','A','M'};

    // THRU/REP before any leading/conclusion word → valid
    std::vector<ALEWord> seq_valid = {
        WordParser::make_word(WordType::THRU, abc),
        WordParser::make_word(WordType::REP,  abc),
        WordParser::make_word(WordType::TO,   abc),
    };
    bool v1 = FrameValidator::thru_in_scanning_section_only(seq_valid);
    std::cout << "  THRU, REP, TO: " << (v1 ? "PASS" : "FAIL") << "\n";

    // THRU after TO → invalid (outside scanning section)
    std::vector<ALEWord> seq_after_to = {
        WordParser::make_word(WordType::TO,   abc),
        WordParser::make_word(WordType::THRU, abc),
    };
    bool v2 = !FrameValidator::thru_in_scanning_section_only(seq_after_to);
    std::cout << "  THRU after TO rejected: " << (v2 ? "PASS" : "FAIL") << "\n";

    // THRU after TIS → invalid
    std::vector<ALEWord> seq_after_tis = {
        WordParser::make_word(WordType::TIS,  sam),
        WordParser::make_word(WordType::THRU, abc),
    };
    bool v3 = !FrameValidator::thru_in_scanning_section_only(seq_after_tis);
    std::cout << "  THRU after TIS rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // THRU after TWAS → invalid
    std::vector<ALEWord> seq_after_tws = {
        WordParser::make_word(WordType::TWAS,  sam),
        WordParser::make_word(WordType::THRU, abc),
    };
    bool v4 = !FrameValidator::thru_in_scanning_section_only(seq_after_tws);
    std::cout << "  THRU after TWAS rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // Scanning only (no leading/conclusion) → valid
    std::vector<ALEWord> seq_scan_only = {
        WordParser::make_word(WordType::THRU, abc),
        WordParser::make_word(WordType::REP,  abc),
        WordParser::make_word(WordType::THRU, xyz),
        WordParser::make_word(WordType::REP,  xyz),
    };
    bool v5 = FrameValidator::thru_in_scanning_section_only(seq_scan_only);
    std::cout << "  scanning only (THRU, REP pairs): " << (v5 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5;
}

// ============================================================================
// AC-WORD-006-2 — THRU and REP alternate in scanning section (A.5.2.3.2.4).
// ============================================================================

bool test_thru_rep_alternates()
{
    std::cout << "\n[AC-WORD-006-2] THRU and REP alternate in scanning section\n";

    const char abc[3] = {'A','B','C'};
    const char xyz[3] = {'X','Y','Z'};

    // One complete pair → valid
    bool v1 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(WordType::THRU, abc),
        WordParser::make_word(WordType::REP,  abc),
    });
    std::cout << "  THRU, REP: " << (v1 ? "PASS" : "FAIL") << "\n";

    // Two complete pairs → valid
    bool v2 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(WordType::THRU, abc),
        WordParser::make_word(WordType::REP,  abc),
        WordParser::make_word(WordType::THRU, xyz),
        WordParser::make_word(WordType::REP,  xyz),
    });
    std::cout << "  THRU, REP, THRU, REP: " << (v2 ? "PASS" : "FAIL") << "\n";

    // Lone THRU without REP → invalid (incomplete pair)
    bool v3 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(WordType::THRU, abc),
    });
    std::cout << "  lone THRU rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // REP before THRU → invalid
    bool v4 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(WordType::REP,  abc),
        WordParser::make_word(WordType::THRU, abc),
    });
    std::cout << "  REP before THRU rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // THRU, THRU → invalid (two THRU in a row)
    bool v5 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(WordType::THRU, abc),
        WordParser::make_word(WordType::THRU, xyz),
    });
    std::cout << "  THRU, THRU rejected: " << (v5 ? "PASS" : "FAIL") << "\n";

    // Empty → valid (no violation)
    bool v6 = FrameValidator::thru_rep_alternates({});
    std::cout << "  empty: " << (v6 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5 && v6;
}

// ============================================================================
// AC-WORD-006-4 — Group call has at most 5 different THRU targets (A.5.2.3.2.4).
// ============================================================================

bool test_group_call_max_5_targets()
{
    std::cout << "\n[AC-WORD-006-4] Group call has at most 5 different THRU targets\n";

    // Build scanning section with N distinct targets
    auto make_scan = [](std::initializer_list<const char*> addrs) {
        std::vector<ALEWord> words;
        for (const char* a : addrs) {
            const char ch[3] = {a[0], a[1], a[2]};
            words.push_back(WordParser::make_word(WordType::THRU, ch));
            words.push_back(WordParser::make_word(WordType::REP,  ch));
        }
        return words;
    };

    // 5 distinct targets → valid
    auto five = make_scan({"AA1", "BB2", "CC3", "DD4", "EE5"});
    bool v1 = FrameValidator::group_call_target_count_valid(five);
    std::cout << "  5 distinct targets: " << (v1 ? "PASS" : "FAIL") << "\n";

    // 6 distinct targets → invalid
    auto six = make_scan({"AA1", "BB2", "CC3", "DD4", "EE5", "FF6"});
    bool v2 = !FrameValidator::group_call_target_count_valid(six);
    std::cout << "  6 distinct targets rejected: " << (v2 ? "PASS" : "FAIL") << "\n";

    // 8 THRU words but only 3 distinct addresses (repeats) → valid
    auto repeat = make_scan({"AA1", "BB2", "CC3", "AA1"});
    bool v3 = FrameValidator::group_call_target_count_valid(repeat);
    std::cout << "  4 words, 3 distinct (1 repeat) accepted: " << (v3 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3;
}

// ============================================================================
// Main test runner
// ============================================================================

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEAT-WORD-002 — Address Words: TO/TIS/TWAS/THRU/FROM    ║\n";
    std::cout << "║  MIL-STD-188-141B A.5.2.3.2 Acceptance Tests             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) { ++pass_count; }
        else        { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    // AC-WORD-003: TO
    run("AC-WORD-003-1 individual scanning → TO",
        test_ac_003_1_individual_scanning_uses_to());
    run("AC-WORD-003-1 TO encodes net address",
        test_ac_003_1_to_encodes_net_address());
    run("AC-WORD-003-2 TO word = first 3 chars",
        test_ac_003_2_to_first_three_chars());
    run("AC-WORD-003-3 extended addresses: DATA/REP alternation",
        test_ac_003_3_extended_address_data_rep_sequence());

    // REQ-WORD-004: TIS
    run("REQ-WORD-004 conclusion uses TIS",
        test_tis_conclusion_word_type());
    run("REQ-WORD-004 TIS extended address → TIS+DATA+REP",
        test_tis_extended_address());

    // REQ-WORD-005: TWAS
    run("REQ-WORD-005 TWAS encode/decode",
        test_twas_word_encoding());
    run("REQ-WORD-005 TIS and TWAS have distinct preambles",
        test_tis_twas_different_preambles());

    // REQ-WORD-006: THRU
    run("REQ-WORD-006 THRU encode/decode",
        test_thru_word_encoding());
    run("REQ-WORD-006 THRU rejects non-Basic-38 chars",
        test_thru_rejects_invalid_basic38());

    // REQ-WORD-007: FROM
    run("REQ-WORD-007 FROM encode/decode",
        test_from_word_encoding());
    run("REQ-WORD-007 FROM extended address uses DATA/REP",
        test_from_extended_address_uses_data_rep());

    // Cross-cutting
    run("All routing preambles use Basic 38",
        test_all_routing_preambles_use_basic38());

    // AC-WORD-006-8/9, AC-WORD-007-8/9
    run("THRU/FROM preamble values reserved for relay/AQC-ALE",
        test_thru_from_preamble_reserved());

    // AC-WORD-007-4
    run("AC-WORD-007-4 FROM at most once per frame",
        test_from_count_valid());

    // AC-WORD-007-5/7
    run("AC-WORD-007-5/7 FROM only immediately before CMD",
        test_from_precedes_cmd_only());

    // AC-WORD-006-1/7
    run("AC-WORD-006-1/7 THRU only in scanning section",
        test_thru_in_scanning_only());

    // AC-WORD-006-2
    run("AC-WORD-006-2 THRU/REP alternate in scanning section",
        test_thru_rep_alternates());

    // AC-WORD-006-4
    run("AC-WORD-006-4 group call max 5 THRU targets",
        test_group_call_max_5_targets());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count
              << "  Failed: " << std::setw(2) << fail_count
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main()
{
    return ale::run_all_tests();
}
