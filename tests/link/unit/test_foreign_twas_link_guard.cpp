/**
 * \file test_foreign_twas_link_guard.cpp
 * \brief Regression: a foreign station's TWAS must never terminate our link.
 *
 * Live incident (2026-08-31, DL3HC ↔ DC7SU): while LINKED and exchanging AMD,
 * a third station (SL3ZXB) started sounding on the same channel. A sounding
 * concludes with TWAS — and TWAS is ALSO the link-termination word. The
 * sounding station's TWAS[SL3] tore down the link to DC7SU (LINKED → IDLE).
 * Per A.5.5.3.5 only the LINKED PEER's TWAS terminates the link; any other
 * station's TWAS is passive-channel traffic and must be ignored.
 *
 * Guard under test (full-address semantics): ALECallProcessor::process_
 * received_word() LINKED branch arms on a TWAS prefix-matching
 * sm.active_call_to, accumulates the conclusion's DATA/REP address
 * extensions, and ALEStateMachine::handle_linked() decides at the Tdrw
 * settle: LINK_TERMINATED only when the accumulated FULL address equals
 * active_call_to. The anchor word alone NEVER terminates — a foreign station
 * sharing the peer's first 3 chars (DC7XY vs DC7SU) must not tear the link
 * down. This file pins that so it can never regress silently again.
 *
 * Tests:
 *   TEST 1  Reported incident replay: LINKED to DC7SU, repeated SL3ZXB
 *           sounding bursts (TWAS[SL3]+DATA[ZXB], 8 cycles incl. BER-degraded
 *           repeats) → link survives, no termination frame transmitted.
 *   TEST 2  Another foreign callsign (OH2) while LINKED → survives.
 *   TEST 3  TWAS[DC7] anchor ALONE must NOT terminate a DC7SU link; the full
 *           conclusion TWAS[DC7]+DATA[SU@] terminates at the Tdrw settle.
 *   TEST 4  Link survives a full AMD exchange interleaved with foreign
 *           sounding traffic (the incident's exact sequence shape).
 *   TEST 5  Shared-prefix foreign station: DC7XY sounding (TWAS[DC7]+
 *           DATA[XY]) while linked to DC7SU → survives the settle compare.
 *   TEST 6  3-char peer (JOE): the anchor IS the full address — TWAS[JOE]
 *           + settle terminates.
 */

#include "Protocol/Control/ale_state_machine.h"
#include <cstring>
#include <iostream>
#include <vector>
#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with fixed-size ALE address fields
#endif

using namespace ale;

namespace {

// LINKED setup, TEST-15 pattern: caller-side link with active_call_to bound
// to the full (possibly multi-word) target address.
bool reach_linked(ALEStateMachine& sm, const char* peer, std::vector<ALEWord>& sent) {
    sm.set_self_address("DL3HC");   // multi-word self address, as in the incident
    sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });
    sm.set_state_callback([](ALEState, ALEState) {});
    sm.set_channel_callback([](const Channel&) {});
    sm.set_rx_enabled_callback([](bool) {});
    sm.set_operator_callback([](OperatorEvent) {});

    sm.initiate_call(peer);                        // IDLE → CALLING; active_call_to = peer
    sm.process_event(ALEEvent::HANDSHAKE_COMPLETE); // CALLING → LINKED
    return sm.get_state() == ALEState::LINKED;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
bool test_incident_foreign_sounding_burst() {
    std::cout << "\n[TEST 1] Foreign SL3ZXB sounding burst must not kill DC7SU link\n";
    std::cout << "===================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU");
    check(sm.get_active_call_to() == "DC7SU", "active_call_to bound to full peer address");

    sm.update(10'000u);  // establish current_time_ms well past link setup

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};
    uint32_t t = 10'000u;

    // Sounding as seen in the incident log: TWAS[SL3]+DATA[ZXB] conclusion,
    // repeated (sounders re-transmit; repeats may arrive BER-degraded — the
    // guard must hold regardless of decode quality or repetition count).
    for (int burst = 0; burst < 8; ++burst) {
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, sl3));
        t += Trw;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, zxb));
        t += Trw;

        if (sm.get_state() != ALEState::LINKED) {
            check(false, "link still alive during foreign sounding burst");
            std::cout << "  (torn down at burst " << burst << ")\n";
            break;
        }
    }
    check(sm.get_state() == ALEState::LINKED,
          "LINKED survives full foreign sounding burst (incident replay)");
    check(sent.empty(), "no termination frame transmitted (nothing keyed up)");
    check(sm.get_active_call_to() == "DC7SU", "peer binding unchanged by foreign TWAS");

    if (all_pass) std::cout << "PASS: foreign sounding never terminates the link\n";
    return all_pass;
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
bool test_other_foreign_callsign() {
    std::cout << "\n[TEST 2] Other foreign TWAS (OH2) while LINKED → survives\n";
    std::cout << "======================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU");

    sm.update(5'000u);
    const char oh2[3] = {'O','H','2'};
    sm.process_received_word(WordParser::make_word(PreambleType::TWAS, oh2));

    check(sm.get_state() == ALEState::LINKED, "LINKED after foreign TWAS[OH2]");
    check(sent.empty(), "no TX triggered");

    if (all_pass) std::cout << "PASS: unrelated foreign TWAS ignored\n";
    return all_pass;
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
bool test_peer_twas_still_terminates() {
    std::cout << "\n[TEST 3] Anchor alone must NOT terminate; full conclusion must\n";
    std::cout << "============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char dc7[3] = {'D','C','7'};
    const char su_at[3] = {'S','U','@'};

    // Part A — anchor word alone: arms the accumulator but "DC7" != "DC7SU".
    {
        ALEStateMachine sm;
        std::vector<ALEWord> sent;
        check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU");

        uint32_t t = 5'000u;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dc7));
        t += Tdrw + 1;
        sm.update(t);  // settle: accumulated "DC7" != "DC7SU" → discarded

        check(sm.get_state() == ALEState::LINKED,
              "TWAS[DC7] anchor ALONE does not terminate (full address required)");
    }

    // Part B — the peer's real termination frame: TWAS[DC7]+DATA[SU@].
    {
        ALEStateMachine sm;
        std::vector<ALEWord> sent;
        check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU (part B)");

        uint32_t t = 5'000u;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dc7));
        t += Trw;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, su_at));
        t += Tdrw + 1;
        sm.update(t);  // settle: "DC7"+"SU" == "DC7SU" → LINK_TERMINATED

        check(sm.get_state() != ALEState::LINKED,
              "full conclusion TWAS[DC7]+DATA[SU@] terminates at settle");
    }

    if (all_pass) std::cout << "PASS: genuine peer termination still works\n";
    return all_pass;
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
bool test_amd_exchange_with_interleaved_sounding() {
    std::cout << "\n[TEST 4] AMD exchange + foreign sounding interleave → link survives\n";
    std::cout << "==================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU");

    sm.update(10'000u);
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};
    const char dc7[3] = {'D','C','7'};
    const char su_at[3] = {'S','U','@'};
    uint32_t t = 10'000u;

    // Peer is active on the link (address words of an in-flight frame) …
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TO, dc7));
    t += Trw;
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, su_at));
    t += Trw;
    check(sm.get_state() == ALEState::LINKED, "LINKED after peer frame opener");

    // … foreign station starts sounding mid-exchange (the incident's moment) …
    for (int burst = 0; burst < 4 && sm.get_state() == ALEState::LINKED; ++burst) {
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, sl3));
        t += Trw;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, zxb));
        t += Trw;
    }
    check(sm.get_state() == ALEState::LINKED, "LINKED survives sounding mid-AMD-exchange");

    // … exchange continues afterwards.
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TO, dc7));
    t += Trw;
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, su_at));
    check(sm.get_state() == ALEState::LINKED, "LINKED after exchange resumes");

    if (all_pass) std::cout << "PASS: interleaved sounding does not disturb the link\n";
    return all_pass;
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
bool test_shared_prefix_foreign_station() {
    std::cout << "\n[TEST 5] Foreign DC7XY (shares peer's first word) must not terminate\n";
    std::cout << "====================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    check(reach_linked(sm, "DC7SU", sent), "Reached LINKED with DC7SU");

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char dc7[3] = {'D','C','7'};
    const char x_y [3] = {'X','Y','@'};   // DC7XY's extension chunk, '@'-padded

    uint32_t t = 5'000u;
    // DC7XY sounds: TWAS[DC7] prefix-matches the peer ("DC7" of "DC7SU") and
    // ARMS the accumulator — the discriminating moment. DATA[XY] completes
    // the foreign sender's address; at settle "DC7XY" != "DC7SU" → discarded.
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dc7));
    t += Trw;
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, x_y));
    t += Tdrw + 1;
    sm.update(t);  // settle decides

    check(sm.get_state() == ALEState::LINKED,
          "LINKED survives foreign DC7XY sounding (shared first word)");
    check(sm.get_active_call_to() == "DC7SU", "peer binding unchanged");
    check(sent.empty(), "no termination frame transmitted");

    // Repeated DC7XY soundings (sounders re-transmit) must keep failing the
    // full-address compare — never terminate on accumulation alone.
    for (int burst = 0; burst < 4 && sm.get_state() == ALEState::LINKED; ++burst) {
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, dc7));
        t += Trw;
        sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::DATA, x_y));
        t += Trw;
    }
    t += Tdrw + 1;
    sm.update(t);
    check(sm.get_state() == ALEState::LINKED, "LINKED survives repeated DC7XY soundings");

    if (all_pass) std::cout << "PASS: shared-prefix foreign station ignored\n";
    return all_pass;
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
bool test_three_char_peer_terminates() {
    std::cout << "\n[TEST 6] 3-char peer (JOE): anchor IS the full address\n";
    std::cout << "=====================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    check(reach_linked(sm, "JOE", sent), "Reached LINKED with JOE");

    const char joe[3] = {'J','O','E'};
    uint32_t t = 5'000u;
    sm.update(t);  sm.process_received_word(WordParser::make_word(PreambleType::TWAS, joe));
    t += ALETimingConstants::Tdrw_ms + 1;
    sm.update(t);  // settle: no extensions followed; "JOE" == "JOE" → terminate

    check(sm.get_state() != ALEState::LINKED,
          "peer JOE's TWAS[JOE] terminates at settle (3-char full address)");

    if (all_pass) std::cout << "PASS: 3-char peer termination works\n";
    return all_pass;
}

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Foreign-TWAS link guard (A.5.5.3.5 peer-match termination)\n";
    std::cout << "==========================================================\n";

    bool ok = true;
    ok &= test_incident_foreign_sounding_burst();
    ok &= test_other_foreign_callsign();
    ok &= test_peer_twas_still_terminates();
    ok &= test_amd_exchange_with_interleaved_sounding();
    ok &= test_shared_prefix_foreign_station();
    ok &= test_three_char_peer_terminates();

    if (ok) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}