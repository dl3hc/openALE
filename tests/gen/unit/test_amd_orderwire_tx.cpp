/**
 * \file tests/gen/unit/test_amd_orderwire_tx.cpp
 * \brief Tests for AC-GEN-014-002: AMD transmission in ACK frame (A.5.7.2.2)
 *
 * MIL-STD-188-141B A.5.7.2.2 requires AMD to be placed in the message section
 * of the ACK frame (third handshake frame), NOT in the calling frame.  The
 * complete calling cycle (call + response) must precede the AMD content.
 *
 * Verifies:
 *   TEST 1  encode_amd() — empty input yields empty word list
 *   TEST 2  encode_amd() — first word is CMD, remaining alternate DATA/REP
 *   TEST 3  encode_amd() — partial last triplet is padded with SP
 *   TEST 4  encode_amd() — 90-character text produces exactly 30 words
 *   TEST 5  encode_amd() — out-of-range characters sanitised to '?'
 *   TEST 6  TX sequence: calling frame has NO AMD; AMD appears only in ACK frame
 *   TEST 7  TX sequence: without AMD pending, calling frame and ACK frame are plain
 *   TEST 8  Calling phase: with AMD, LEADING_CALL→CONCLUSION (no MESSAGE); ACK frame has AMD
 */

#include "Protocol/Message/ale_orderwire_protocols.h"
#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_encode_amd_empty()
{
    std::cout << "[TEST 1] encode_amd(\"\") yields empty word list\n";

    auto words = encode_amd("");
    assert(words.empty() && "empty input must produce no words");
    std::cout << "  words.size() = 0  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_encode_amd_word_structure()
{
    std::cout << "[TEST 2] encode_amd() — first word CMD, rest alternate DATA/REP\n";

    // "HELLO WORLD" = 11 chars → ceil(11/3)=4 words (padded "HELLO WORLD ")
    auto words = encode_amd("HELLO WORLD");
    assert(words.size() == 4 && "11 chars -> 4 words");

    assert(words[0].type == PreambleType::CMD  && "word[0] must be CMD (AMD header)");
    assert(words[1].type == PreambleType::DATA && "word[1] must be DATA");
    assert(words[2].type == PreambleType::REP  && "word[2] must be REP");
    assert(words[3].type == PreambleType::DATA && "word[3] must be DATA");

    // First word payload carries first 3 characters
    assert(words[0].address[0] == 'H' && words[0].address[1] == 'E' && words[0].address[2] == 'L'
           && "CMD word must carry first 3 chars");
    assert(words[0].valid && "CMD word must be valid");

    std::cout << "  4 words, types: CMD DATA REP DATA  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_encode_amd_padding()
{
    std::cout << "[TEST 3] encode_amd() — partial last triplet padded with SP\n";

    // "HI" = 2 chars → 1 word, padded to "HI "
    auto words = encode_amd("HI");
    assert(words.size() == 1 && "2 chars -> 1 CMD word (padded)");
    assert(words[0].type == PreambleType::CMD);
    assert(words[0].address[0] == 'H');
    assert(words[0].address[1] == 'I');
    assert(words[0].address[2] == ' ' && "third char must be SP (0x20) padding");
    std::cout << "  address = \"HI \", last char is SP  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_encode_amd_max_length()
{
    std::cout << "[TEST 4] encode_amd() — 90-char text yields exactly 30 words\n";

    std::string text(90, 'A');
    auto words = encode_amd(text);
    assert(words.size() == 30 && "90 chars / 3 chars per word = 30 words");

    // Extra characters beyond 90 must be silently truncated
    std::string over(95, 'B');
    auto truncated = encode_amd(over);
    assert(truncated.size() == 30 && "95 chars truncated to 30 words");
    std::cout << "  30 words for 90 chars, truncation at 91+  PASSED\n\n";
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
void test_encode_amd_sanitisation()
{
    std::cout << "[TEST 5] encode_amd() — out-of-range chars sanitised to '?'\n";

    // 0x01 (SOH) is outside Expanded-64 (0x20–0x5F)
    std::string text;
    text += '\x01';  // non-printable
    text += 'A';
    text += 'B';
    auto words = encode_amd(text);
    assert(words.size() == 1);
    assert(words[0].address[0] == '?' && "control char must become '?'");
    assert(words[0].address[1] == 'A');
    assert(words[0].address[2] == 'B');
    std::cout << "  '\\x01' → '?'  PASSED\n\n";
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
// Integration: calling frame has NO AMD; AMD appears in the ACK frame (A.5.7.2.2).
void test_amd_only_in_ack_frame()
{
    std::cout << "[TEST 6] TX sequence: calling frame has no AMD; AMD only in ACK frame\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(0);  // no scan pass — simplest calling frame

    ALEStateMachine::PendingMessage msg;
    msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
    msg.content = "HELLO";
    sm.set_pending_message(msg);

    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });

    sm.initiate_call("JOE");

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t tx0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    sm.update(ALETimingConstants::Twt_ms);  // LBT → TUNING
    sm.update(tx0);                          // TUNING → LEADING_CALL, words enqueued

    // Calling frame: leading(2) + conclusion(1) = 3 words, NO AMD
    // [0] TO "JOE"  [1] TO "JOE"  [2] TIS "SAM"
    assert(sent.size() == 3 && "calling frame: leading(2)+conclusion(1) = 3 words, no AMD");
    for (size_t i = 0; i < 3; ++i)
        assert(sent[i].type != PreambleType::CMD && "calling frame must have no CMD (no AMD)");

    auto send_slot = [&](uint32_t t) { sm.update(t); sm.on_word_complete(); };

    // Drain calling frame: LEADING_CALL(×2) + CONCLUSION(×1) → LISTENING
    send_slot(tx0 + 0 * Trw);
    send_slot(tx0 + 1 * Trw);
    send_slot(tx0 + 2 * Trw);

    // Simulate JOE's response: TO[SAM] then TIS[JOE]
    const uint32_t t_listen = tx0 + 3 * Trw;
    sm.update(t_listen);

    ALEWord to_sam{};
    to_sam.type = PreambleType::TO;
    strncpy(to_sam.address, "SAM", 3);
    to_sam.valid = true;
    sm.process_received_word(to_sam);

    ALEWord tis_joe{};
    tis_joe.type = PreambleType::TIS;
    strncpy(tis_joe.address, "JOE", 3);
    tis_joe.valid = true;
    sm.process_received_word(tis_joe);  // sets tlww_start_ms

    // Advance past Tdrw → SENDING_ACK → build_ack_words() fires
    sm.update(t_listen + ALETimingConstants::Tdrw_ms);
    sm.update(t_listen + ALETimingConstants::Tdrw_ms + 1);  // build_ack_words() called here

    // ACK frame with AMD "HELLO": TO[JOE]×2 + CMD[HEL] + DATA[LO ] + TIS[SAM] = 5 words
    // sent[] = calling(3) + ack(5) = 8 total
    assert(sent.size() == 8 && "calling(3) + ACK(5) = 8 total words");

    // ACK frame starts at index 3
    assert(sent[3].type == PreambleType::TO   && "ACK[0] must be TO");
    assert(sent[4].type == PreambleType::TO   && "ACK[1] must be TO");
    assert(sent[5].type == PreambleType::CMD  && "ACK[2] must be CMD (AMD start)");
    assert(sent[6].type == PreambleType::DATA && "ACK[3] must be DATA (AMD continuation)");
    assert(sent[7].type == PreambleType::TIS  && "ACK[4] must be TIS (conclusion)");

    std::cout << "  calling: TO×2 TIS (no AMD)\n";
    std::cout << "  ACK:     TO×2 CMD DATA TIS  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
// Integration: Without AMD pending, calling frame is plain (no message section).
void test_no_amd_skips_message_phase()
{
    std::cout << "[TEST 7] TX sequence: no AMD pending → calling frame has no message section\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.add_scan_channel(Channel(7100000, "USB"));
    sm.set_target_scan_channels(1);

    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });

    sm.initiate_call("JOE");

    const uint32_t tx0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms);
    sm.update(tx0);

    // Expected: scanning(2) + leading(2) + conclusion(1) = 5 words, no CMD/DATA/REP
    assert(sent.size() == 5 && "without AMD: scanning(2)+leading(2)+conclusion(1) = 5 words");

    for (const auto& w : sent) {
        assert(w.type != PreambleType::CMD  && "no CMD without AMD");
        (void)w;
    }

    for (const auto& w : sent) {
        assert(w.type != PreambleType::DATA && "no DATA without AMD");
        (void)w;
    }

    assert(sent.back().type == PreambleType::TIS && "last word must be TIS (conclusion)");

    std::cout << "  5 words: TO×4 TIS — no CMD/DATA in sequence  PASSED\n\n";
}

// ── TEST 8 ───────────────────────────────────────────────────────────────────
// Integration: with AMD pending, calling phase skips MESSAGE (no AMD in calling
// frame); AMD rides in the ACK frame after the full handshake.
void test_calling_phase_transitions_with_amd()
{
    std::cout << "[TEST 8] Calling phase: AMD → LEADING_CALL→CONCLUSION (no MESSAGE); ACK has AMD\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(0);  // no scan pass

    ALEStateMachine::PendingMessage msg;
    msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
    msg.content = "TEST";
    sm.set_pending_message(msg);

    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });

    sm.initiate_call("JOE");

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t tx0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    sm.update(ALETimingConstants::Twt_ms);
    sm.update(tx0);

    auto send_slot = [&](uint32_t t) { sm.update(t); sm.on_word_complete(); };

    // Calling frame: leading(2) + conclusion(1) — MESSAGE phase skipped (no AMD in calling)
    send_slot(tx0 + 0 * Trw);  // LEADING_CALL word 1
    send_slot(tx0 + 1 * Trw);  // LEADING_CALL word 2 → CONCLUSION (not MESSAGE)
    assert(sm.get_calling_phase() == CallingPhase::CONCLUSION
           && "with AMD, LEADING_CALL must skip MESSAGE and go directly to CONCLUSION");

    send_slot(tx0 + 2 * Trw);  // CONCLUSION word → LISTENING
    assert(sm.get_calling_phase() == CallingPhase::LISTENING
           && "after conclusion, must enter LISTENING");

    // Simulate JOE's response
    const uint32_t t_listen = tx0 + 3 * Trw;
    sm.update(t_listen);

    ALEWord to_sam{};
    to_sam.type = PreambleType::TO;
    strncpy(to_sam.address, "SAM", 3);
    to_sam.valid = true;
    sm.process_received_word(to_sam);

    ALEWord tis_joe{};
    tis_joe.type = PreambleType::TIS;
    strncpy(tis_joe.address, "JOE", 3);
    tis_joe.valid = true;
    sm.process_received_word(tis_joe);

    // Advance past Tdrw → SENDING_ACK → build_ack_words()
    sm.update(t_listen + ALETimingConstants::Tdrw_ms);
    sm.update(t_listen + ALETimingConstants::Tdrw_ms + 1);

    // ACK frame: "TEST" = 4 chars → 2 AMD words (CMD "TES", DATA "T  ")
    // sent[] = calling(3) + ACK(5): TO[JOE]×2 + CMD[TES] + DATA[T  ] + TIS[SAM]
    assert(sent.size() == 8 && "calling(3) + ACK with AMD(5) = 8 total");

    assert(sent[3].type == PreambleType::TO  && "ACK[0] TO");
    assert(sent[4].type == PreambleType::TO  && "ACK[1] TO");
    assert(sent[5].type == PreambleType::CMD && "ACK[2] CMD (AMD start)");
    assert(sent[6].type == PreambleType::DATA && "ACK[3] DATA (AMD continuation)");
    assert(sent[7].type == PreambleType::TIS && "ACK[4] TIS (conclusion)");

    std::cout << "  Calling: LEADING→CONCLUSION (no MESSAGE)\n";
    std::cout << "  ACK frame: TO×2 CMD DATA TIS  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==============================================\n";
    std::cout << "AC-GEN-014-002 — AMD orderwire TX section\n";
    std::cout << "==============================================\n\n";

    test_encode_amd_empty();
    test_encode_amd_word_structure();
    test_encode_amd_padding();
    test_encode_amd_max_length();
    test_encode_amd_sanitisation();
    test_amd_only_in_ack_frame();
    test_no_amd_skips_message_phase();
    test_calling_phase_transitions_with_amd();

    std::cout << "All tests PASSED.\n";
    return 0;
}
