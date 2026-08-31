/**
 * \file test_wait_ack_twas_guard.cpp
 * \brief Regression: a foreign station's TWAS during WAIT_ACK must never
 *        decline our own pending call.
 *
 * Found 2026-08-31 during the OFS Phase 3d OF-0 audit
 * (docs/FRAMING_STANDARD.md §10.1): react_handshake_()'s WAIT_ACK branch
 * handled WordRole::TWAS_WORD (frame 3 concluding TWAS instead of TIS —
 * Ion2G-style AMD decline, A.5.5.3.4) with NO address check at all, unlike
 * every sibling case in the same switch (TIS_CALLER/DATA_EXTENSION/TO_SELF
 * are all gated on the expected caller or on timing). classify() tags
 * WordRole::TWAS_WORD unconditionally for any TWAS word in any state, so a
 * foreign station sounding while we (JOE) wait for our own caller's (SAM's)
 * ACK/decline would have misread as "SAM declined my AMD" and aborted the
 * pending call. Same bug shape as the original LINKED-TWAS incident
 * (tests/link/unit/test_foreign_twas_link_guard.cpp), same fix shape: the
 * decision moved from the bare word to ALEStateMachine::
 * handle_completed_frame_handshake_(), which reads the FrameReassembler's
 * completed conclusion and requires an EXACT full-address match against
 * caller_address before firing AMD_DECLINED_LINK.
 *
 * No prior test exercised AMD_DECLINED_LINK's RX path at all — this file
 * is the first.
 *
 * Tests:
 *   TEST 1  Foreign TWAS (unrelated callsign) during WAIT_ACK → survives,
 *           HANDSHAKE/WAIT_ACK unaffected, no AMD_RECEIVED_NO_LINK.
 *   TEST 2  Anchor alone must NOT decline; the full multi-word conclusion
 *           must, at the Tdrw settle.
 *   TEST 3  Shared-prefix foreign station (DL3XY vs caller DL3HC) → survives.
 *   TEST 4  3-char caller (anchor IS the full address) → declines at settle.
 *   TEST 5  Genuine TIS ACK still completes to LINKED normally (sanity: the
 *           TWAS-decline path change didn't disturb the TIS/ACK path).
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

// Drives a fresh SM (self "JOE") through an incoming call from `caller`
// (may be multi-word) up to HANDSHAKE/WAIT_ACK, exactly like
// test_state_machine.cpp TEST 14's setup. `caller_words` supplies the
// calling-cycle conclusion words (TIS[anchor] [+ DATA[ext]]) so both 3-char
// and multi-word callers can be exercised. Returns the current_time_ms
// reach_wait_ack left the SM at via `end_ms` — WAIT_ACK(1)'s ACK-turnaround
// deadline is only ~1699 ms from hs_ack_start_ms (unlike LINKED, which has
// no such timeout), so callers MUST resume from `end_ms` with small offsets,
// never jump to a round wall-clock value the way ForeignTwasLinkGuard does.
bool reach_wait_ack(ALEStateMachine& sm, const std::vector<ALEWord>& caller_conclusion,
                     std::vector<ALEWord>& sent, std::vector<OperatorEvent>& ops,
                     uint32_t& end_ms) {
    sm.set_self_address("JOE");
    sm.set_target_scan_channels(1);
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState) {});
    sm.set_channel_callback([](const Channel&) {});
    sm.set_rx_enabled_callback([](bool) {});
    sm.set_operator_callback([&](OperatorEvent e) { ops.push_back(e); });

    sm.process_event(ALEEvent::START_SCAN);   // → SCANNING

    const char joe3[3] = {'J', 'O', 'E'};
    uint32_t t = 1000u;
    auto rx = [&](const ALEWord& w) {
        sm.update(t);
        sm.process_received_word(w);
        t += Trw;
    };
    rx(WordParser::make_word(PreambleType::TO, joe3));   // SCANNING → HANDSHAKE
    rx(WordParser::make_word(PreambleType::TO, joe3));   // re-anchor
    rx(WordParser::make_word(PreambleType::TO, joe3));   // leading ×2
    rx(WordParser::make_word(PreambleType::TO, joe3));
    for (const auto& w : caller_conclusion) rx(w);        // TIS[caller] [+ DATA ext]

    if (sm.get_state() != ALEState::HANDSHAKE) return false;

    const uint32_t settle_t = t + Tdrw;                  // WAIT_CYCLE_END settle
    sm.update(settle_t);                                  // → SLOT_WAIT
    sm.update(settle_t + 1);                              // SLOT_WAIT(0ms) → CHANNEL_CHECK
    sm.update(settle_t + 1 + Tdrw);                        // LBT clear → SENDING_RESPONSE

    if (sm.get_handshake_phase() != HandshakePhase::SENDING_RESPONSE) return false;

    const uint32_t wp = sm.get_words_pending();            // capture BEFORE draining
    for (uint32_t i = 0; i < wp; ++i) sm.on_word_complete();

    end_ms = settle_t + 1 + Tdrw;   // hs_ack_start_ms is armed at this same instant
    return sm.get_handshake_phase() == HandshakePhase::WAIT_ACK;
}

bool has_op(const std::vector<OperatorEvent>& ops, OperatorEvent e) {
    for (auto o : ops) if (o == e) return true;
    return false;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────
bool test_foreign_twas_during_wait_ack() {
    std::cout << "\n[TEST 1] Foreign TWAS (OH2) during WAIT_ACK must not decline our call\n";
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
    const char sam3[3] = {'S', 'A', 'M'};
    check(reach_wait_ack(sm, {WordParser::make_word(PreambleType::TIS, sam3)}, sent, ops, end_ms),
          "Reached HANDSHAKE/WAIT_ACK (caller SAM)");

    const char oh2[3] = {'O', 'H', '2'};
    uint32_t t = end_ms + 50;   // stay well inside the ~1699ms WAIT_ACK(1) window
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, oh2));
    t += Tdrw + 1;
    sm.update(t);   // settle: "OH2" != "SAM" → discarded

    check(sm.get_state() == ALEState::HANDSHAKE, "Still in HANDSHAKE after foreign TWAS");
    check(sm.get_handshake_phase() == HandshakePhase::WAIT_ACK, "Still in WAIT_ACK");
    check(!has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "No AMD_RECEIVED_NO_LINK fired");

    if (all_pass) std::cout << "PASS: foreign TWAS during WAIT_ACK ignored\n";
    return all_pass;
}

// ── TEST 2 ───────────────────────────────────────────────────────────────
bool test_anchor_alone_does_not_decline() {
    std::cout << "\n[TEST 2] Anchor alone must NOT decline; full conclusion must\n";
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
        check(reach_wait_ack(sm,
                  {WordParser::make_word(PreambleType::TIS, dl3),
                   WordParser::make_word(PreambleType::DATA, hc_at)},
                  sent, ops, end_ms),
              "Reached HANDSHAKE/WAIT_ACK (caller DL3HC)");

        uint32_t t = end_ms + 50;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
        t += Tdrw + 1;
        sm.update(t);   // settle: accumulated "DL3" != "DL3HC" → discarded

        check(sm.get_state() == ALEState::HANDSHAKE,
              "TWAS[DL3] anchor ALONE does not decline (full address required)");
        check(!has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "No AMD_RECEIVED_NO_LINK fired");
    }

    // Part B — the caller's real decline frame: TWAS[DL3]+DATA[HC@].
    {
        ALEStateMachine sm;
        std::vector<ALEWord> sent;
        std::vector<OperatorEvent> ops;
        uint32_t end_ms = 0;
        check(reach_wait_ack(sm,
                  {WordParser::make_word(PreambleType::TIS, dl3),
                   WordParser::make_word(PreambleType::DATA, hc_at)},
                  sent, ops, end_ms),
              "Reached HANDSHAKE/WAIT_ACK (caller DL3HC, part B)");

        uint32_t t = end_ms + 50;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
        t += Trw;
        sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::DATA, hc_at));
        t += Tdrw + 1;
        sm.update(t);   // settle: "DL3"+"HC" == "DL3HC" → AMD_DECLINED_LINK

        check(sm.get_state() != ALEState::HANDSHAKE,
              "full conclusion TWAS[DL3]+DATA[HC@] declines at settle");
        check(has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "AMD_RECEIVED_NO_LINK fired");
    }

    if (all_pass) std::cout << "PASS: full-address decline semantics correct\n";
    return all_pass;
}

// ── TEST 3 ───────────────────────────────────────────────────────────────
bool test_shared_prefix_foreign_station() {
    std::cout << "\n[TEST 3] Foreign DL3XY (shares caller's first word) must not decline\n";
    std::cout << "====================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    const char dl3[3]   = {'D', 'L', '3'};
    const char hc_at[3] = {'H', 'C', '@'};
    check(reach_wait_ack(sm,
              {WordParser::make_word(PreambleType::TIS, dl3),
               WordParser::make_word(PreambleType::DATA, hc_at)},
              sent, ops, end_ms),
          "Reached HANDSHAKE/WAIT_ACK (caller DL3HC)");

    const char xy_at[3] = {'X', 'Y', '@'};
    uint32_t t = end_ms + 50;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dl3));
    t += Trw;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::DATA, xy_at));
    t += Tdrw + 1;
    sm.update(t);   // settle: "DL3XY" != "DL3HC" → discarded

    check(sm.get_state() == ALEState::HANDSHAKE,
          "HANDSHAKE survives foreign DL3XY (shared first word)");
    check(sm.get_handshake_phase() == HandshakePhase::WAIT_ACK, "Still in WAIT_ACK");
    check(!has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "No AMD_RECEIVED_NO_LINK fired");

    if (all_pass) std::cout << "PASS: shared-prefix foreign station ignored\n";
    return all_pass;
}

// ── TEST 4 ───────────────────────────────────────────────────────────────
bool test_three_char_caller_declines() {
    std::cout << "\n[TEST 4] 3-char caller (SAM): anchor IS the full address\n";
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
    const char sam3[3] = {'S', 'A', 'M'};
    check(reach_wait_ack(sm, {WordParser::make_word(PreambleType::TIS, sam3)}, sent, ops, end_ms),
          "Reached HANDSHAKE/WAIT_ACK (caller SAM)");

    uint32_t t = end_ms + 50;
    sm.update(t); sm.process_received_word(WordParser::make_word(PreambleType::TWAS, sam3));
    t += Tdrw + 1;
    sm.update(t);   // settle: no extensions followed; "SAM" == "SAM" → decline

    check(sm.get_state() != ALEState::HANDSHAKE,
          "caller SAM's TWAS[SAM] declines at settle (3-char full address)");
    check(has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "AMD_RECEIVED_NO_LINK fired");

    if (all_pass) std::cout << "PASS: 3-char caller decline works\n";
    return all_pass;
}

// ── TEST 5 ───────────────────────────────────────────────────────────────
bool test_genuine_ack_still_links() {
    std::cout << "\n[TEST 5] Genuine TIS ACK still completes to LINKED (sanity)\n";
    std::cout << "============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<OperatorEvent> ops;
    uint32_t end_ms = 0;
    const char sam3[3] = {'S', 'A', 'M'};
    check(reach_wait_ack(sm, {WordParser::make_word(PreambleType::TIS, sam3)}, sent, ops, end_ms),
          "Reached HANDSHAKE/WAIT_ACK (caller SAM)");

    const char joe3[3] = {'J', 'O', 'E'};
    uint32_t t = end_ms + 50;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe3));
    t += Trw;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe3));
    t += Trw;
    sm.update(t);              sm.process_received_word(WordParser::make_word(PreambleType::TIS, sam3));
    t += Tdrw;
    sm.update(t);              // ACK conclusion settle → LINKED

    check(sm.get_state() == ALEState::LINKED, "Reached LINKED via genuine TIS ACK");
    check(!has_op(ops, OperatorEvent::AMD_RECEIVED_NO_LINK), "No AMD_RECEIVED_NO_LINK fired");

    if (all_pass) std::cout << "PASS: TIS/ACK path unaffected by the TWAS-decline change\n";
    return all_pass;
}

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "WAIT_ACK TWAS-decline guard (A.5.5.3.4 caller-match decline)\n";
    std::cout << "==========================================================\n";

    bool ok = true;
    ok &= test_foreign_twas_during_wait_ack();
    ok &= test_anchor_alone_does_not_decline();
    ok &= test_shared_prefix_foreign_station();
    ok &= test_three_char_caller_declines();
    ok &= test_genuine_ack_still_links();

    if (ok) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}
