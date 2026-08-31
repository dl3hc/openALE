/**
 * \file test_calling_twas_guard.cpp
 * \brief Regression: a foreign station's TWAS during CALLING/LISTENING must
 *        never reject our own outbound call.
 *
 * Sibling fix to tests/link/unit/test_wait_ack_twas_guard.cpp — same bug
 * shape (docs/FRAMING_STANDARD.md §10.1), the other survivor from the OFS
 * Phase 3d OF-0 audit. react_calling_()'s LISTENING branch handled
 * WordRole::TWAS_WORD (AC-LINK-019-10, "responder rejects with TWAS") with
 * NO address check at all: classify() tags TWAS_WORD unconditionally for
 * any TWAS word in any state, so a foreign station sounding while we (SAM)
 * waited for our own dialed callee's response would misread as "the callee
 * rejected my call" and abort.
 *
 * Unlike the WAIT_ACK gap, this one WAS pinned —
 * tests/link/unit/test_rx_characterization.cpp TEST 7 asserted the
 * rejection fired immediately on the bare word. The OFS Phase 3 kickoff
 * prompt flagged this exact path as "keep word-level unless the owner
 * approves the re-pin" (docs/FRAMING_STANDARD.md §10.1). Owner approval was
 * given 2026-08-31 (same day as the WAIT_ACK fix); TEST 7 is re-pinned
 * alongside this file to expect the settle-delayed decision instead of the
 * immediate one — the OUTCOME (CALL_REJECTED for a genuine full-address
 * rejection) is unchanged, only the TIMING moved from the bare word to the
 * Tdrw frame boundary, exactly like T-03/F-05 and the WAIT_ACK/F-04 fix.
 *
 * Tests:
 *   TEST 1  Foreign TWAS (unrelated callsign) during LISTENING → survives,
 *           CALLING/LISTENING unaffected, no CALL_REJECTED.
 *   TEST 2  Anchor alone must NOT reject; the full multi-word conclusion
 *           must, at the Tdrw settle.
 *   TEST 3  Shared-prefix foreign station (DL3XY vs dialed DL3HC) → survives.
 *   TEST 4  3-char callee (JOE): anchor IS the full address → rejects at
 *           settle (mirrors RxCharacterization TEST 7's re-pinned scenario).
 *   TEST 5  Genuine TIS response still completes to LINKED normally
 *           (sanity: the TWAS-rejection path change didn't disturb it).
 */

#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <vector>
#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with fixed-size ALE address fields
#endif

using namespace ale;

namespace {

const uint32_t Trw  = ALETimingConstants::Trw_ms;    // 392
const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;   // 784

// Drives a fresh SM (self "SAM") through initiate_call(callee) up to
// CALLING/LISTENING. `tx_slots` is the TX word-completion count the
// callee's calling cycle takes to drain — 5 for a 3-char callee (2 scan +
// 2 leading + 1 conclusion), 7 for a 2-word callee (2 scan + 4 leading + 1
// conclusion) — matching test_rx_characterization.cpp TEST 7/TEST 6's
// pattern exactly. Returns the current_time_ms reach_listening left the SM
// at via `end_ms`; LISTENING(a)'s response-turnaround budget is generous
// (~2744 ms, listening_response_wait_ms_()) but callers should still build
// on `end_ms` with small offsets rather than round wall-clock jumps.
bool reach_listening(ALEStateMachine& sm, const char* callee, uint32_t tx_slots,
                      std::vector<ALEWord>& sent, std::vector<OperatorEvent>& ops,
                      uint32_t& end_ms) {
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(1);
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState) {});
    sm.set_channel_callback([](const Channel&) {});
    sm.set_rx_enabled_callback([](bool) {});
    sm.set_operator_callback([&](OperatorEvent e) { ops.push_back(e); });

    if (!sm.initiate_call(callee)) return false;

    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;
    const uint32_t tx0 = Twt + Tt;
    auto send_slot = [&](uint32_t slot_t) { sm.update(slot_t); sm.on_word_complete(); };

    sm.update(Twt);
    sm.update(tx0);
    for (uint32_t i = 0; i < tx_slots; ++i)
        send_slot(tx0 + i * Trw);

    end_ms = tx0 + tx_slots * Trw + 100;
    return sm.get_calling_phase() == CallingPhase::LISTENING;
}

bool has_op(const std::vector<OperatorEvent>& ops, OperatorEvent e) {
    for (auto o : ops) if (o == e) return true;
    return false;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────
bool test_foreign_twas_during_listening() {
    std::cout << "\n[TEST 1] Foreign TWAS (OH2) during LISTENING must not reject our call\n";
    std::cout << "======================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    check(reach_listening(sm, "JOE", 5, sent, ops, end_ms), "Reached LISTENING (dialed JOE)");

    const char oh2[3] = {'O', 'H', '2'};
    uint32_t t = end_ms;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, oh2));
    t += Tdrw + 1;
    sm.update(t);   // settle: "OH2" != "JOE" → discarded

    check(sm.get_state() == ALEState::CALLING, "Still in CALLING after foreign TWAS");
    check(sm.get_calling_phase() == CallingPhase::LISTENING, "Still in LISTENING");
    check(!has_op(ops, OperatorEvent::CALL_REJECTED), "No CALL_REJECTED fired");

    if (all_pass) std::cout << "PASS: foreign TWAS during LISTENING ignored\n";
    return all_pass;
}

// ── TEST 2 ───────────────────────────────────────────────────────────────
bool test_anchor_alone_does_not_reject() {
    std::cout << "\n[TEST 2] Anchor alone must NOT reject; full conclusion must\n";
    std::cout << "============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    const char dl3[3]   = {'D', 'L', '3'};
    const char hc_at[3] = {'H', 'C', '@'};

    // Part A — anchor alone: "DL3" != "DL3HC", must survive.
    {
        ALEStateMachine sm;
        std::vector<ALEWord> sent;
        std::vector<OperatorEvent> ops;
        uint32_t end_ms = 0;
        check(reach_listening(sm, "DL3HC", 7, sent, ops, end_ms),
              "Reached LISTENING (dialed DL3HC)");

        uint32_t t = end_ms;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
        t += Tdrw + 1;
        sm.update(t);   // settle: accumulated "DL3" != "DL3HC" → discarded

        check(sm.get_state() == ALEState::CALLING,
              "TWAS[DL3] anchor ALONE does not reject (full address required)");
        check(!has_op(ops, OperatorEvent::CALL_REJECTED), "No CALL_REJECTED fired");
    }

    // Part B — the callee's real rejection frame: TWAS[DL3]+DATA[HC@].
    {
        ALEStateMachine sm;
        std::vector<ALEWord> sent;
        std::vector<OperatorEvent> ops;
        uint32_t end_ms = 0;
        check(reach_listening(sm, "DL3HC", 7, sent, ops, end_ms),
              "Reached LISTENING (dialed DL3HC, part B)");

        uint32_t t = end_ms;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
        t += Trw;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::DATA, hc_at));
        t += Tdrw + 1;
        sm.update(t);   // settle: "DL3"+"HC" == "DL3HC" → CALL_REJECTED

        check(sm.get_state() != ALEState::CALLING,
              "full conclusion TWAS[DL3]+DATA[HC@] rejects at settle");
        check(has_op(ops, OperatorEvent::CALL_REJECTED), "CALL_REJECTED fired");
    }

    if (all_pass) std::cout << "PASS: full-address rejection semantics correct\n";
    return all_pass;
}

// ── TEST 3 ───────────────────────────────────────────────────────────────
bool test_shared_prefix_foreign_station() {
    std::cout << "\n[TEST 3] Foreign DL3XY (shares callee's first word) must not reject\n";
    std::cout << "===================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    const char dl3[3] = {'D', 'L', '3'};
    check(reach_listening(sm, "DL3HC", 7, sent, ops, end_ms), "Reached LISTENING (dialed DL3HC)");

    const char xy_at[3] = {'X', 'Y', '@'};
    uint32_t t = end_ms;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
    t += Trw;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::DATA, xy_at));
    t += Tdrw + 1;
    sm.update(t);   // settle: "DL3XY" != "DL3HC" → discarded

    check(sm.get_state() == ALEState::CALLING,
          "CALLING survives foreign DL3XY (shared first word)");
    check(sm.get_calling_phase() == CallingPhase::LISTENING, "Still in LISTENING");
    check(!has_op(ops, OperatorEvent::CALL_REJECTED), "No CALL_REJECTED fired");

    if (all_pass) std::cout << "PASS: shared-prefix foreign station ignored\n";
    return all_pass;
}

// ── TEST 4 ───────────────────────────────────────────────────────────────
bool test_three_char_callee_rejects() {
    std::cout << "\n[TEST 4] 3-char callee (JOE): anchor IS the full address\n";
    std::cout << "=========================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    check(reach_listening(sm, "JOE", 5, sent, ops, end_ms), "Reached LISTENING (dialed JOE)");

    const char joe[3] = {'J', 'O', 'E'};
    uint32_t t = end_ms;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, joe));
    t += Tdrw + 1;
    sm.update(t);   // settle: no extensions followed; "JOE" == "JOE" → reject

    check(sm.get_state() != ALEState::CALLING,
          "callee JOE's TWAS[JOE] rejects at settle (3-char full address)");
    check(has_op(ops, OperatorEvent::CALL_REJECTED), "CALL_REJECTED fired");

    if (all_pass) std::cout << "PASS: 3-char callee rejection works\n";
    return all_pass;
}

// ── TEST 5 ───────────────────────────────────────────────────────────────
bool test_genuine_response_still_links() {
    std::cout << "\n[TEST 5] Genuine TIS response still completes to LINKED (sanity)\n";
    std::cout << "==================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    check(reach_listening(sm, "JOE", 5, sent, ops, end_ms), "Reached LISTENING (dialed JOE)");

    const char sam[3] = {'S', 'A', 'M'};
    const char joe[3] = {'J', 'O', 'E'};
    uint32_t t = end_ms;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TO,  sam));
    t += Trw;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TO,  sam));
    t += Trw;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TIS, joe));
    t += Tdrw;
    sm.update(t);              // Response conclusion settle → SENDING_ACK
    sm.update(t + 1);          // build_ack_words() (deferred one tick)

    const uint32_t ack_words = sm.get_words_pending();
    for (uint32_t i = 0; i < ack_words; ++i) sm.on_word_complete();

    check(sm.get_state() == ALEState::LINKED, "Reached LINKED via genuine TIS response");
    check(!has_op(ops, OperatorEvent::CALL_REJECTED), "No CALL_REJECTED fired");

    if (all_pass) std::cout << "PASS: Response/ACK path unaffected by the TWAS-rejection change\n";
    return all_pass;
}

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "LISTENING TWAS-rejection guard (AC-LINK-019-10 callee-match)\n";
    std::cout << "==========================================================\n";

    bool ok = true;
    ok &= test_foreign_twas_during_listening();
    ok &= test_anchor_alone_does_not_reject();
    ok &= test_shared_prefix_foreign_station();
    ok &= test_three_char_callee_rejects();
    ok &= test_genuine_response_still_links();

    if (ok) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}
