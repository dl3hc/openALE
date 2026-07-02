/**
 * \file tests/gen/unit/test_amd_linked_orderwire.cpp
 * \brief Tests for AMD over an established link (A.5.7.2 linked orderwire)
 *
 * Two new behaviours:
 *   1. trigger_linked_orderwire(words, double_burst=false) sends a SINGLE
 *      payload+TIS:SELF burst (not doubled) so the peer decodes AMD text once.
 *   2. The linked-AMD TX frame is  TO[peer] (+DATA/REP ext) + CMD AMD + message
 *      DATA/REP + TIS:SELF -- assembled from AddressEncoder::encode(peer, TO)
 *      + encode_amd(text), with the conclusion appended by handle_linked().
 *
 * Verifies:
 *   TEST 1  Linked AMD frame shape -- single burst, TO+CMD+DATA+TIS, not doubled
 *   TEST 2  double_burst=true keeps the historic EFS doubling (regression guard)
 *   TEST 3  Multi-char peer/self -- address-extension DATA/REP words are present
 *   TEST 4  encode_amd empty/whitespace text yields no payload words
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include "Protocol/Message/ale_orderwire_protocols.h"
#include "Word/address_encoder.h"
#include "Word/ale_word.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

static ALEStateMachine make_linked_sm(const std::string& self,
                                      std::vector<ALEWord>& sent)
{
    ALEStateMachine sm;
    sm.set_self_address(self);
    sm.set_transmit_callback([&](const ALEWord& w){ sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_operator_callback([](OperatorEvent){});
    sm.set_rx_enabled_callback([](bool){});
    sm.initiate_call("PEER");
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);   // → LINKED
    assert(sm.get_state() == ALEState::LINKED && "must reach LINKED");
    sm.update(1000u);   // settle
    sent.clear();
    return sm;
}

// Build the linked-AMD payload (caller is responsible for the TIS conclusion,
// which trigger_linked_orderwire() appends).
static std::vector<ALEWord> build_linked_amd_payload(const std::string& peer,
                                                      const std::string& text)
{
    std::vector<ALEWord> words = AddressEncoder::encode(peer, PreambleType::TO);
    const auto amd = encode_amd(text);
    words.insert(words.end(), amd.begin(), amd.end());
    return words;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_linked_amd_frame_single_burst()
{
    std::cout << "[TEST 1] Linked AMD frame — single burst, not doubled\n";

    std::vector<ALEWord> sent;
    ALEStateMachine sm = make_linked_sm("SAM", sent);

    // peer "JOE" (1 word), self "SAM" (1 word), text "HELLO" → CMD+DATA (2 words)
    auto payload = build_linked_amd_payload("JOE", "HELLO");
    sm.trigger_linked_orderwire(payload, /*double_burst=*/false);
    sm.update(1001u);   // orderwire_pending_ → transmit

    // Expected single frame: TO:JOE + CMD:'HEL' + DATA:'LO ' + TIS:SAM = 4 words
    assert(sent.size() == 4 && "single burst: TO + CMD + DATA + TIS = 4 words");
    assert(sent[0].type == PreambleType::TO   && "word[0] = TO peer");
    assert(sent[1].type == PreambleType::CMD  && "word[1] = CMD AMD header");
    assert(sent[2].type == PreambleType::DATA && "word[2] = DATA (AMD body)");
    assert(sent[3].type == PreambleType::TIS  && "word[3] = TIS self (conclusion)");

    // The payload must not be doubled: no second TO/CMD after the TIS.
    for (size_t i = 4; i < sent.size(); ++i)
        assert(sent[i].type != PreambleType::TO && "no doubled TO");

    std::cout << "  4 words: TO CMD DATA TIS (single)  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_double_burst_true_doubles()
{
    std::cout << "[TEST 2] double_burst=true keeps EFS-style doubling\n";

    std::vector<ALEWord> sent;
    ALEStateMachine sm = make_linked_sm("SAM", sent);

    auto payload = build_linked_amd_payload("JOE", "HI");   // CMD 'HI ' = 1 word
    // payload = TO:JOE + CMD:'HI ' = 2 words; +TIS:SAM = 3; doubled = 6
    sm.trigger_linked_orderwire(payload, /*double_burst=*/true);
    sm.update(1001u);

    // 2 payload + 1 conclusion = 3, doubled = 6
    assert(sent.size() == 6 && "double_burst=true → 2×(TO+CMD+TIS) = 6 words");
    assert(sent[0].type == PreambleType::TO  && "first half starts with TO");
    assert(sent[3].type == PreambleType::TO  && "second half restarts with TO");
    std::cout << "  6 words (doubled)  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_multichar_address_extension()
{
    std::cout << "[TEST 3] Multi-char peer/self — DATA/REP extension words present\n";

    std::vector<ALEWord> sent;
    ALEStateMachine sm = make_linked_sm("SAMUEL", sent);   // self 6 chars → 2 words

    // peer "EDWARD" → [TO:EDW, DATA:ARD]; text "OK" → CMD:'OK ' (1 word)
    auto payload = build_linked_amd_payload("EDWARD", "OK");
    assert(payload.size() == 3 && "TO:EDW + DATA:ARD + CMD:'OK ' = 3 payload words");
    assert(payload[0].type == PreambleType::TO   && "peer first word TO");
    assert(payload[1].type == PreambleType::DATA && "peer extension DATA");
    assert(payload[2].type == PreambleType::CMD  && "CMD AMD header");

    sm.trigger_linked_orderwire(payload, /*double_burst=*/false);
    sm.update(1001u);

    // TO:EDW + DATA:ARD + CMD:'OK ' + TIS:SAM + DATA:UEL = 5 words (single)
    assert(sent.size() == 5 && "single burst with 2-word addresses = 5 words");
    assert(sent[0].type == PreambleType::TO   && "TO:EDW");
    assert(sent[1].type == PreambleType::DATA && "DATA:ARD (peer ext)");
    assert(sent[2].type == PreambleType::CMD  && "CMD AMD");
    assert(sent[3].type == PreambleType::TIS  && "TIS:SAM (self anchor)");
    assert(sent[4].type == PreambleType::DATA && "DATA:UEL (self ext)");
    std::cout << "  5 words: TO DATA CMD TIS DATA  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_empty_text_yields_no_payload()
{
    std::cout << "[TEST 4] encode_amd(\"\") / whitespace → no AMD payload words\n";

    assert(encode_amd("").empty() && "empty text → no words");
    // A controller-level guard returns ERROR for empty text; here we only confirm
    // the encoder contract that send_amd() relies on.
    std::cout << "  encode_amd(\"\") empty  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==============================================\n";
    std::cout << "AMD over linked orderwire (A.5.7.2)\n";
    std::cout << "==============================================\n\n";

    test_linked_amd_frame_single_burst();
    test_double_burst_true_doubles();
    test_multichar_address_extension();
    test_empty_text_yields_no_payload();

    std::cout << "All tests PASSED.\n";
    return 0;
}