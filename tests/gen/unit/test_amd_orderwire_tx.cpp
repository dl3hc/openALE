/**
 * \file tests/gen/unit/test_amd_orderwire_tx.cpp
 * \brief Tests for AC-GEN-014-002: AMD transmission only in message section
 *
 * MIL-STD-188-141B A.5.7.2 requires AMD messages to be placed exclusively
 * in the Message section of an ALE calling frame (CallingPhase::MESSAGE).
 *
 * Verifies:
 *   TEST 1  encode_amd() — empty input yields empty word list
 *   TEST 2  encode_amd() — first word is CMD, remaining alternate DATA/REP
 *   TEST 3  encode_amd() — partial last triplet is padded with SP
 *   TEST 4  encode_amd() — 90-character text produces exactly 30 words
 *   TEST 5  encode_amd() — out-of-range characters sanitised to '?'
 *   TEST 6  TX sequence: AMD words appear ONLY after leading call and before conclusion
 *   TEST 7  TX sequence: Without AMD pending, MESSAGE phase is skipped (LEADING→CONCLUSION)
 *   TEST 8  Calling phase: transitions LEADING_CALL→MESSAGE→CONCLUSION with AMD
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
// Integration: AMD words appear ONLY between leading and conclusion in TX sequence.
void test_amd_only_in_message_section()
{
    std::cout << "[TEST 6] TX sequence: AMD words ONLY between leading call and conclusion\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.add_scan_channel(Channel(7100000, "USB"));
    sm.set_target_scan_channels(1);

    // Queue AMD message before initiating the call
    ALEStateMachine::PendingMessage msg;
    msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
    msg.content = "HELLO";
    sm.set_pending_message(msg);

    std::vector<ALEWord> sent;
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });

    sm.initiate_call("JOE");  // → CALLING / LBT

    // Advance through LBT + TUNING → enqueue_call_sequence_() fires here
    const uint32_t tx0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms);  // LBT elapses → TUNING
    sm.update(tx0);                          // TUNING elapses → SCANNING_CALL, all words sent

    // Expected frame for "JOE"→"SAM" with AMD "HELLO" (target_scan_channels=1):
    //   [0] TO  "JOE" — scanning pass 1
    //   [1] TO  "JOE" — scanning pass 2 → LEADING_CALL
    //   [2] TO  "JOE" — leading pass 1
    //   [3] TO  "JOE" — leading pass 2 → MESSAGE
    //   [4] CMD "HEL" — AMD word 1 (message section)
    //   [5] DATA"LO " — AMD word 2 (message section, padded)  → CONCLUSION
    //   [6] TIS "SAM" — conclusion

    assert(sent.size() == 7 && "scanning(2)+leading(2)+AMD(2)+conclusion(1) = 7 words");

    // Scanning and leading: all TO
    for (size_t i = 0; i <= 3; ++i)
        assert(sent[i].type == PreambleType::TO && "scanning+leading must be TO words");

    // Message section: CMD then DATA
    assert(sent[4].type == PreambleType::CMD  && "first AMD word must be CMD");
    assert(sent[5].type == PreambleType::DATA && "second AMD word must be DATA");

    // Conclusion: TIS
    assert(sent[6].type == PreambleType::TIS && "conclusion must be TIS");

    // No CMD before message section
    for (size_t i = 0; i <= 3; ++i)
        assert(sent[i].type != PreambleType::CMD && "no CMD before message section");

    std::cout << "  7 words: TO×4 CMD DATA TIS — AMD only in message section  PASSED\n\n";
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
// Integration: Without AMD pending, MESSAGE phase is skipped entirely.
void test_no_amd_skips_message_phase()
{
    std::cout << "[TEST 7] TX sequence: no AMD pending → MESSAGE phase skipped\n";

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
// Integration: Calling phase transitions correctly with AMD.
void test_calling_phase_transitions_with_amd()
{
    std::cout << "[TEST 8] Calling phase: LEADING_CALL→MESSAGE→CONCLUSION with AMD\n";

    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.add_scan_channel(Channel(7100000, "USB"));
    sm.set_target_scan_channels(1);

    ALEStateMachine::PendingMessage msg;
    msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
    msg.content = "TEST";
    sm.set_pending_message(msg);

    sm.initiate_call("JOE");

    const uint32_t tx0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    sm.update(ALETimingConstants::Twt_ms);
    sm.update(tx0);

    auto send_slot = [&](uint32_t t) {
        sm.update(t);
        sm.on_word_complete();
    };

    // Slots 0–1: scanning (target_scan_channels=1 → 2 words)
    send_slot(tx0 + 0 * Trw);  // scan word 1 → still SCANNING_CALL
    send_slot(tx0 + 1 * Trw);  // scan word 2 → LEADING_CALL
    assert(sm.get_calling_phase() == CallingPhase::LEADING_CALL
           && "after scanning, must be LEADING_CALL");

    // Slots 2–3: leading call (2 words)
    send_slot(tx0 + 2 * Trw);  // leading word 1 → still LEADING_CALL
    send_slot(tx0 + 3 * Trw);  // leading word 2 → MESSAGE (AMD present)
    assert(sm.get_calling_phase() == CallingPhase::MESSAGE
           && "with AMD, LEADING_CALL must transition to MESSAGE");

    // Slots 4–5: AMD message section ("TEST" = 4 chars → 2 words)
    send_slot(tx0 + 4 * Trw);  // AMD word 1 → still MESSAGE
    send_slot(tx0 + 5 * Trw);  // AMD word 2 → CONCLUSION
    assert(sm.get_calling_phase() == CallingPhase::CONCLUSION
           && "after message words, must transition to CONCLUSION");

    std::cout << "  Phase path: LEADING_CALL → MESSAGE → CONCLUSION  PASSED\n\n";
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
    test_amd_only_in_message_section();
    test_no_amd_skips_message_phase();
    test_calling_phase_transitions_with_amd();

    std::cout << "All tests PASSED.\n";
    return 0;
}
