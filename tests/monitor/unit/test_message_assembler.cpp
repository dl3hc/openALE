/**
 * \file test_message_assembler.cpp
 * \brief Unit tests for MessageAssembler and CallTypeDetector
 *
 * Validates that the assembler correctly buffers ALE words into frames and
 * classifies them independently of local protocol state — a prerequisite for
 * wiring MessageAssembler output to the ALE Monitor (passive decoded-traffic
 * display).
 *
 * Frame structures tested (MIL-STD-188-141B A.5.2.3):
 *   Sounding     : TIS self [+ DATA/REP multi-word extension]
 *   Call/response: TO dest × N + TIS self
 *   AMD          : TO dest × N + TIS self + DATA text words
 *   Timeout      : partial frame discarded silently on inter-word silence gap
 */

#include "Protocol/Message/ale_message.h"
#include "Word/ale_word.h"
#include <iostream>
#include <cstring>
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

namespace ale {

// ── helpers ─────────────────────────────────────────────────────────────────

static ALEWord make(PreambleType t, const char* a3, uint32_t ts_ms = 0)
{
    const char ch[3] = { a3[0], a3[1], a3[2] };
    ALEWord w = WordParser::make_word(t, ch);
    w.timestamp_ms = ts_ms;
    return w;
}

static ALEWord make_data(const char* a3, uint32_t ts_ms = 0)
{
    // DATA/REP use Expanded-64 charset; just make a valid word with spaces
    ALEWord w{};
    w.type         = PreambleType::DATA;
    w.valid        = true;
    w.timestamp_ms = ts_ms;
    std::memcpy(w.address, a3, 3);
    w.address[3]   = '\0';
    return w;
}

static ALEWord make_cmd(const char* a3, uint32_t ts_ms = 0)
{
    // CMD words use Expanded-64 charset in this implementation.
    // Direct-set type (WordParser::make_word rejects Expanded-64 chars via Basic-38).
    ALEWord w{};
    w.type         = PreambleType::CMD;
    w.valid        = true;
    w.timestamp_ms = ts_ms;
    std::memcpy(w.address, a3, 3);
    w.address[3]   = '\0';
    return w;
}

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_EQ(a, b, label)                                    \
    do {                                                           \
        if ((a) == (b)) {                                         \
            std::cout << "  PASS  " << (label) << "\n";          \
            ++g_pass;                                              \
        } else {                                                   \
            std::cout << "  FAIL  " << (label) << "\n";          \
            ++g_fail;                                              \
        }                                                          \
    } while(0)

#define EXPECT_TRUE(cond, label)  EXPECT_EQ((cond), true,  (label))
#define EXPECT_FALSE(cond, label) EXPECT_EQ((cond), false, (label))

// ── test 1: simple sounding (TIS only) ──────────────────────────────────────

static void test_sounding_basic()
{
    std::cout << "\n[1] Simple sounding — TIS only\n";

    MessageAssembler asm_;
    bool complete = asm_.add_word(make(PreambleType::TIS, "SAM", 100));
    EXPECT_TRUE(complete, "add_word returns true on TIS");

    ALEMessage msg;
    EXPECT_TRUE(asm_.get_message(msg), "get_message returns true");
    EXPECT_EQ(msg.call_type, CallType::SOUNDING, "call_type == SOUNDING");
    EXPECT_EQ(msg.from_address, std::string("SAM"), "from_address == SAM");
    EXPECT_TRUE(msg.to_addresses.empty(), "to_addresses empty");
    EXPECT_TRUE(msg.complete, "msg.complete");
}

// ── test 2: individual call (TO × N + TIS) ──────────────────────────────────

static void test_individual_call()
{
    std::cout << "\n[2] Individual call — TO x4 + TIS\n";

    MessageAssembler asm_;
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,  "JOE", 100)), "TO 1 not complete");
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,  "JOE", 200)), "TO 2 not complete");
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,  "JOE", 300)), "TO 3 not complete");
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,  "JOE", 400)), "TO 4 not complete");
    bool complete = asm_.add_word(make(PreambleType::TIS, "SAM", 500));
    EXPECT_TRUE(complete, "TIS completes frame");

    ALEMessage msg;
    EXPECT_TRUE(asm_.get_message(msg), "get_message ok");
    EXPECT_EQ(msg.call_type, CallType::INDIVIDUAL, "call_type == INDIVIDUAL");
    EXPECT_EQ(msg.from_address, std::string("SAM"), "from_address == SAM");
    // to_addresses must be deduplicated: 4 × "JOE" → 1 × "JOE"
    EXPECT_EQ(msg.to_addresses.size(), (size_t)1, "to_addresses deduplicated to 1 entry");
    if (!msg.to_addresses.empty())
        EXPECT_EQ(msg.to_addresses[0], std::string("JOE"), "to_addresses[0] == JOE");
    EXPECT_EQ(msg.words.size(), (size_t)5, "frame has 5 words total");
}

// ── test 3: sounding must NOT trigger on TO+TIS ──────────────────────────────

static void test_to_tis_not_sounding()
{
    std::cout << "\n[3] TO + TIS must NOT be classified as SOUNDING\n";

    MessageAssembler asm_;
    asm_.add_word(make(PreambleType::TO, "JOE", 100));
    bool complete = asm_.add_word(make(PreambleType::TIS, "SAM", 200));
    EXPECT_TRUE(complete, "TO + TIS completes frame");

    ALEMessage msg;
    asm_.get_message(msg);
    EXPECT_EQ(msg.call_type, CallType::INDIVIDUAL, "TO+TIS → INDIVIDUAL, not SOUNDING");
}

// ── test 4: response frame (TO dest + TIS responder) ─────────────────────────
// Wire-identical to individual call — both are classified as INDIVIDUAL.

static void test_response_frame()
{
    std::cout << "\n[4] Response frame — TO caller x2 + TIS responder\n";

    MessageAssembler asm_;
    asm_.add_word(make(PreambleType::TO,  "SAM", 100));
    asm_.add_word(make(PreambleType::TO,  "SAM", 200));
    bool complete = asm_.add_word(make(PreambleType::TIS, "JOE", 300));
    EXPECT_TRUE(complete, "Response frame completes");

    ALEMessage msg;
    asm_.get_message(msg);
    EXPECT_EQ(msg.call_type, CallType::INDIVIDUAL, "Response → INDIVIDUAL");
    EXPECT_EQ(msg.from_address, std::string("JOE"), "Responder in from_address");
    EXPECT_EQ(msg.to_addresses.size(), (size_t)1, "Caller deduped to 1 entry");
    if (!msg.to_addresses.empty())
        EXPECT_EQ(msg.to_addresses[0], std::string("SAM"), "to[0] == SAM");
}

// ── test 5: AMD frame (TO + TIS + DATA text) ─────────────────────────────────

static void test_amd_frame()
{
    std::cout << "\n[5] AMD — TO + TIS + DATA text words\n";

    MessageAssembler asm_;
    // AMD frame: call section + conclusion (TIS) + orderwire DATA
    // Note: TIS triggers completion — DATA arrives in a new frame.
    // This is a known limitation; test verifies the current behavior.
    asm_.add_word(make(PreambleType::TO,  "JOE", 100));
    asm_.add_word(make(PreambleType::TO,  "JOE", 200));
    bool complete_on_tis = asm_.add_word(make(PreambleType::TIS, "SAM", 300));
    EXPECT_TRUE(complete_on_tis, "TIS concludes even AMD call section");

    ALEMessage msg;
    asm_.get_message(msg);
    // DATA words arrive after TIS → they start a NEW frame (known limitation)
    EXPECT_EQ(msg.call_type, CallType::INDIVIDUAL, "TO+TIS classified as INDIVIDUAL");

    // DATA continuation starts a fresh frame
    bool complete_on_data = asm_.add_word(make_data("HI ", 400));
    EXPECT_FALSE(complete_on_data, "DATA alone does not complete a frame");
}

// ── test 6: timeout resets partial frame ─────────────────────────────────────

static void test_timeout_resets_partial_frame()
{
    std::cout << "\n[6] Timeout resets partial (incomplete) frame\n";

    MessageAssembler asm_;
    asm_.set_timeout(1000);  // 1-second timeout

    // First frame: only TO — never completed
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO, "JOE", 0)), "TO 1 not complete");
    EXPECT_FALSE(asm_.add_word(make(PreambleType::TO, "JOE", 100)), "TO 2 not complete");

    // Timeout: next word arrives 1500 ms after the last → gap > 1000 ms
    // Assembler discards the partial frame and starts fresh with this word.
    bool complete = asm_.add_word(make(PreambleType::TO, "SAM", 1600));
    EXPECT_FALSE(complete, "New word after timeout starts a fresh frame");

    // Verify we can now assemble a new complete frame
    bool complete2 = asm_.add_word(make(PreambleType::TIS, "JOE", 1700));
    EXPECT_TRUE(complete2, "Fresh frame completes normally");

    ALEMessage msg;
    EXPECT_TRUE(asm_.get_message(msg), "get_message after timeout+complete");
    EXPECT_EQ(msg.to_addresses.size(), (size_t)1, "New frame has 1 TO address");
    EXPECT_EQ(msg.call_type, CallType::INDIVIDUAL, "New frame is INDIVIDUAL");
}

// ── test 7: invalid words are ignored ────────────────────────────────────────

static void test_invalid_words_ignored()
{
    std::cout << "\n[7] Invalid words (bad FEC/ASCII) do not enter assembler\n";

    MessageAssembler asm_;
    ALEWord bad{};
    bad.valid        = false;
    bad.type         = PreambleType::TO;
    bad.timestamp_ms = 100;

    EXPECT_FALSE(asm_.add_word(bad), "Invalid word returns false, not added");

    // Valid TIS alone → sounding
    bool complete = asm_.add_word(make(PreambleType::TIS, "SAM", 200));
    EXPECT_TRUE(complete, "Valid TIS after invalid word completes a sounding");

    ALEMessage msg;
    asm_.get_message(msg);
    EXPECT_EQ(msg.call_type, CallType::SOUNDING, "SOUNDING (invalid TO not counted)");
}

// ── test 8: multiple sequential frames ───────────────────────────────────────

static void test_sequential_frames()
{
    std::cout << "\n[8] Two sequential frames processed back-to-back\n";

    MessageAssembler asm_;

    // Frame 1: sounding
    asm_.add_word(make(PreambleType::TIS, "DL3", 100));
    ALEMessage msg1;
    asm_.get_message(msg1);

    // Frame 2: individual call — should start cleanly
    asm_.add_word(make(PreambleType::TO,  "DL3", 200));
    asm_.add_word(make(PreambleType::TIS, "SAM", 300));
    ALEMessage msg2;
    bool ok = asm_.get_message(msg2);
    EXPECT_TRUE(ok, "Second frame available");
    EXPECT_EQ(msg2.call_type, CallType::INDIVIDUAL, "Second frame is INDIVIDUAL");
    EXPECT_EQ(msg2.from_address, std::string("SAM"), "from_address SAM in second frame");
    EXPECT_EQ(msg2.words.size(), (size_t)2, "Second frame has exactly 2 words");
}

// ── test 9: CallTypeDetector directly ────────────────────────────────────────

static void test_call_type_detector()
{
    std::cout << "\n[9] CallTypeDetector — direct API\n";

    // TIS only → SOUNDING
    {
        std::vector<ALEWord> words = { make(PreambleType::TIS, "SAM", 0) };
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::SOUNDING, "TIS only → SOUNDING");
    }
    // TO + TIS → INDIVIDUAL
    {
        std::vector<ALEWord> words = {
            make(PreambleType::TO, "JOE", 0),
            make(PreambleType::TIS, "SAM", 100)
        };
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::INDIVIDUAL, "TO+TIS → INDIVIDUAL");
    }
    // TO + CMD + TIS → AMD
    {
        std::vector<ALEWord> words = {
            make(PreambleType::TO,  "JOE", 0),
            make_cmd("HI ",         100),
            make(PreambleType::TIS, "SAM", 200)
        };
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::AMD, "TO+CMD+TIS → AMD");
    }
    // TWAS + TIS → UNKNOWN: is_net_call() cannot distinguish NET from individual
    // without address-book context; the old TWAS heuristic was wrong.
    {
        std::vector<ALEWord> words = {
            make(PreambleType::TWAS, "NET", 0),
            make(PreambleType::TIS,  "SAM", 100)
        };
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::UNKNOWN, "TWAS+TIS → UNKNOWN (is_net_call retired)");
    }
    // TO + TWAS (termination/net conclusion) → UNKNOWN (no TIS/FROM alongside)
    {
        std::vector<ALEWord> words = {
            make(PreambleType::TO,   "SAM", 0),
            make(PreambleType::TWAS, "JOE", 100)
        };
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::UNKNOWN, "TO+TWAS → UNKNOWN");
    }
    // Empty → UNKNOWN
    {
        std::vector<ALEWord> words;
        EXPECT_EQ(CallTypeDetector::detect(words), CallType::UNKNOWN, "empty → UNKNOWN");
    }
}

// ── test 10: multi-word address truncation (known limitation) ─────────────────

static void test_multiword_address_limitation()
{
    std::cout << "\n[10] Multi-word address: DATA-before-TIS sounding (orphan_data path)\n";

    // Sounding by station "DL3HC": the ALE 2G sounding sends the suffix (DATA "HC@")
    // BEFORE the prefix (TIS "DL3").  The assembler must reconstruct the full address.
    MessageAssembler asm_;
    EXPECT_FALSE(asm_.add_word(make_data("HC@", 100)), "DATA alone does not complete sounding");
    bool complete_on_tis = asm_.add_word(make(PreambleType::TIS, "DL3", 200));
    EXPECT_TRUE(complete_on_tis, "TIS after DATA completes sounding (2 words)");

    ALEMessage msg;
    asm_.get_message(msg);
    EXPECT_EQ(msg.from_address, std::string("DL3HC"), "from_address assembled from DATA+TIS = DL3HC");
    EXPECT_EQ(msg.call_type, CallType::SOUNDING, "Still correctly SOUNDING");
    EXPECT_EQ(msg.words.size(), (size_t)2, "Frame has 2 words (DATA suffix + TIS prefix)");
}

// ── test 11: TWAS-based frame completion (Fix 3 regression) ─────────────────

static void test_twas_completion()
{
    std::cout << "\n[11] TWAS as frame terminator (Fix 3 regression)\n";

    // TWAS-only frame (announce-only sounding / orphaned rejection word)
    {
        MessageAssembler asm_;
        bool complete = asm_.add_word(make(PreambleType::TWAS, "SAM", 100));
        EXPECT_TRUE(complete, "TWAS alone completes a frame");
        ALEMessage msg;
        EXPECT_TRUE(asm_.get_message(msg), "get_message returns true");
        EXPECT_EQ(msg.call_type, CallType::UNKNOWN, "TWAS-only → UNKNOWN (no TIS/TO)");
        EXPECT_TRUE(msg.complete, "msg.complete");
    }

    // TO × 2 + TWAS (rejection / link-termination frame)
    {
        MessageAssembler asm_;
        EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,   "SAM", 100)), "TO 1 not complete");
        EXPECT_FALSE(asm_.add_word(make(PreambleType::TO,   "SAM", 200)), "TO 2 not complete");
        bool complete = asm_.add_word(make(PreambleType::TWAS, "JOE", 300));
        EXPECT_TRUE(complete, "TO+TO+TWAS completes frame");
        ALEMessage msg;
        asm_.get_message(msg);
        EXPECT_EQ(msg.call_type, CallType::UNKNOWN, "TO+TWAS → UNKNOWN");
        EXPECT_EQ(msg.words.size(), (size_t)3, "Frame contains 3 words");
    }

    // FROM-only must NOT complete (bogus branch removed in Fix 3)
    {
        MessageAssembler asm_;
        bool complete = asm_.add_word(make(PreambleType::FROM, "SAM", 100));
        EXPECT_FALSE(complete, "FROM alone does NOT complete a frame");
        ALEMessage msg;
        EXPECT_FALSE(asm_.get_message(msg), "No complete message available");
    }
}

} // namespace ale

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "═══════════════════════════════════════\n";
    std::cout << "  MessageAssembler Unit Tests\n";
    std::cout << "═══════════════════════════════════════\n";

    ale::test_sounding_basic();
    ale::test_individual_call();
    ale::test_to_tis_not_sounding();
    ale::test_response_frame();
    ale::test_amd_frame();
    ale::test_timeout_resets_partial_frame();
    ale::test_invalid_words_ignored();
    ale::test_sequential_frames();
    ale::test_call_type_detector();
    ale::test_multiword_address_limitation();
    ale::test_twas_completion();

    std::cout << "\n═══════════════════════════════════════\n";
    std::cout << "  PASS: " << ale::g_pass << "  FAIL: " << ale::g_fail << "\n";
    std::cout << "═══════════════════════════════════════\n";
    return ale::g_fail > 0 ? 1 : 0;
}
