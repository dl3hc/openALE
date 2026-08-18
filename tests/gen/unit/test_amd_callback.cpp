/**
 * \file tests/gen/unit/test_amd_callback.cpp
 * \brief Unit tests for AMD immediate-notification behavior (AC-GEN-014-001)
 *
 * MIL-STD-188-141B A.5.7.2 requires that a received AMD message be
 * immediately reported to the operator layer — no buffering without
 * notification.
 *
 * AMD frame wire format (encode_amd / ale_orderwire_protocols.cpp):
 *   [TO dest × N] [CMD msg_1-3] [DATA msg_4-6]? [REP msg_7-9]? ... [TIS src]
 *
 * The CMD preamble word is mandatory and distinguishes AMD from individual
 * calls that carry DATA words only for address extension (long callsigns).
 * A frame concludes only on TIS/TWAS (A.5.2.3/A.5.5.4.4) — FROM is a mid-frame
 * quick-ID, never a terminator, so every assembled-frame test below closes
 * with TIS.
 *
 * Verifies:
 *   TEST 1  CallTypeDetector::is_amd() — TO+FROM+CMD detected as AMD
 *   TEST 2  CallTypeDetector::is_amd() — TO+FROM (no CMD) is NOT AMD
 *   TEST 3  CallTypeDetector::detect() — AMD wins over INDIVIDUAL when CMD present
 *   TEST 4  MessageAssembler — AMD message assembled with call_type=AMD
 *   TEST 5  MessageAssembler — AMD data content extracted into data_content
 *   TEST 6  MessageAssembler — AMD multi-word data (CMD+DATA) assembled correctly
 *   TEST 7  MessageAssembler — no AMD fired without on_amd_received set (no crash)
 */

#include "Protocol/Message/ale_message.h"
#include "Word/ale_word.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

static ALEWord make_word(PreambleType type, const char* addr, uint32_t ts = 0)
{
    ALEWord w;
    w.type        = type;
    w.valid       = true;
    w.timestamp_ms = ts;
    for (int i = 0; i < 3; ++i) w.address[i] = addr[i] ? addr[i] : '\0';
    w.address[3] = '\0';
    w.raw_payload  = 0;
    w.unanimous_votes = 48;
    w.fec_errors   = 0;
    return w;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_amd_detected_with_cmd()
{
    std::cout << "[TEST 1] is_amd() — TO+FROM+CMD detected as AMD\n";

    std::vector<ALEWord> words;
    words.push_back(make_word(PreambleType::TO,   "BOB"));
    words.push_back(make_word(PreambleType::FROM, "SAM"));
    words.push_back(make_word(PreambleType::CMD,  "HEL"));

    bool result = CallTypeDetector::is_amd(words);
    assert(result && "TO+FROM+CMD must be detected as AMD");
    (void)result;
    std::cout << "  is_amd(TO,FROM,CMD) = true  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_no_amd_without_cmd()
{
    std::cout << "[TEST 2] is_amd() — TO+FROM (no CMD) is NOT AMD\n";

    std::vector<ALEWord> words;
    words.push_back(make_word(PreambleType::TO,   "BOB"));
    words.push_back(make_word(PreambleType::FROM, "SAM"));

    bool result = CallTypeDetector::is_amd(words);
    assert(!result && "TO+FROM without CMD must NOT be AMD");
    (void)result;
    std::cout << "  is_amd(TO,FROM) = false  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_detect_prefers_amd_over_individual()
{
    std::cout << "[TEST 3] detect() — AMD takes precedence over INDIVIDUAL when CMD present\n";

    std::vector<ALEWord> words;
    words.push_back(make_word(PreambleType::TO,   "BOB"));
    words.push_back(make_word(PreambleType::FROM, "SAM"));
    words.push_back(make_word(PreambleType::CMD,  "HI!"));

    CallType ct = CallTypeDetector::detect(words);
    assert(ct == CallType::AMD && "detect() must return AMD when TO+FROM+CMD present");
    (void)ct;
    std::cout << "  detect(TO,FROM,CMD) = AMD  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_assembler_amd_call_type()
{
    std::cout << "[TEST 4] MessageAssembler — AMD call_type after assembly\n";

    MessageAssembler asm_;

    // AMD frame: TO dest + CMD message-start + TIS conclusion (A.5.2.3: a
    // frame concludes only on TIS/TWAS).
    asm_.add_word(make_word(PreambleType::TO,   "BOB", 100));
    asm_.add_word(make_word(PreambleType::CMD,  "HEL", 110));
    bool complete = asm_.add_word(make_word(PreambleType::TIS, "SAM", 120));

    ALEMessage msg;
    bool got = asm_.get_message(msg);

    assert(complete && "Assembler must signal completion at TIS");
    (void)complete;
    assert(got      && "get_message must return true after completion");
    (void)got;
    assert(msg.complete && "message.complete must be true");
    assert(msg.call_type == CallType::AMD && "call_type must be AMD");
    std::cout << "  call_type = AMD  PASSED\n\n";
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
void test_assembler_amd_data_content()
{
    std::cout << "[TEST 5] MessageAssembler — AMD data content extracted from CMD word\n";

    MessageAssembler asm_;
    asm_.add_word(make_word(PreambleType::TO,   "BOB", 100));
    asm_.add_word(make_word(PreambleType::CMD,  "HEL", 110));
    asm_.add_word(make_word(PreambleType::TIS,  "SAM", 120));

    ALEMessage msg;
    asm_.get_message(msg);

    assert(!msg.data_content.empty() && "AMD data_content must not be empty");
    assert(msg.data_content[0] == "HEL" && "first data word must be 'HEL' (from CMD)");
    std::cout << "  data_content[0] = \"" << msg.data_content[0] << "\"  PASSED\n\n";
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
void test_assembler_amd_multi_word_data()
{
    std::cout << "[TEST 6] MessageAssembler — AMD multi-word message (CMD+DATA)\n";

    MessageAssembler asm_;
    asm_.add_word(make_word(PreambleType::TO,   "BOB", 100));
    asm_.add_word(make_word(PreambleType::CMD,  "HEL", 110));  // first AMD word
    asm_.add_word(make_word(PreambleType::DATA, "LO@", 120));  // continuation
    asm_.add_word(make_word(PreambleType::TIS,  "SAM", 130));

    ALEMessage msg;
    asm_.get_message(msg);

    assert(msg.call_type == CallType::AMD && "multi-word AMD still detected as AMD");
    assert(msg.data_content.size() >= 2 && "data_content must contain CMD + DATA words");
    std::cout << "  call_type = AMD, data_content.size() = " << msg.data_content.size()
              << "  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
void test_no_crash_without_callback()
{
    std::cout << "[TEST 7] AMD assembly without callback set — no crash\n";

    // ALEController is not instantiated here; we verify the MessageAssembler
    // lower layer (which has no callback) handles AMD cleanly, meaning that
    // higher-layer code that registers on_amd_received = nullptr must not crash.
    // The controller guards its commit helpers on the callback being set.
    // This test confirms the lower-level assembly succeeds without side effects.

    MessageAssembler asm_;
    asm_.add_word(make_word(PreambleType::TO,   "BOB", 0));
    asm_.add_word(make_word(PreambleType::CMD,  "HI!", 1));
    asm_.add_word(make_word(PreambleType::TIS,  "SAM", 2));

    ALEMessage msg;
    bool ok = asm_.get_message(msg);
    assert(ok && "AMD message assembled without crash");
    (void)ok;
    assert(msg.call_type == CallType::AMD);
    std::cout << "  No crash, call_type = AMD  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "========================================\n";
    std::cout << "AC-GEN-014-001 — AMD immediate callback\n";
    std::cout << "========================================\n\n";

    test_amd_detected_with_cmd();
    test_no_amd_without_cmd();
    test_detect_prefers_amd_over_individual();
    test_assembler_amd_call_type();
    test_assembler_amd_data_content();
    test_assembler_amd_multi_word_data();
    test_no_crash_without_callback();

    std::cout << "All tests PASSED.\n";
    return 0;
}
