/**
 * \file test_rx_characterization.cpp
 * \brief Phase 0 OFS characterization: pin current RX frame-assembly behavior.
 *
 * The OFS refactoring (docs/FRAMING_STANDARD.md) will converge the four ad-hoc
 * reassembly mechanisms onto one FrameReassembler. These tests pin today's
 * observable behavior for every mechanism the refactor is allowed to absorb
 * but not to change: the multi-word conclusion accumulation paths in
 * ALECallProcessor::classify()/react_* and the settle decisions in
 * ALEStateMachine::handle_calling()/handle_handshake(). The Phase 2
 * reassembler must produce identical classifications; these pins are the
 * baseline it is asserted against.
 *
 * Companion pins (already covered elsewhere — deliberately NOT repeated here):
 *   LINKED termination full-address semantics  test_foreign_twas_link_guard.cpp
 *   MessageAssembler AMD assembly             tests/monitor/unit/test_message_assembler.cpp
 *   SoundingIdentityAccumulator                tests/chan/unit/test_sounding_identity_accumulator.cpp
 *
 * What this file adds — none of these paths were pinned with multi-word
 * (>=2-word) addresses before; test_state_machine.cpp uses 3-char callsigns
 * exclusively, so every DATA/REP extension accumulation path was unguarded:
 *
 *   TEST 1  Responder (WAIT_CYCLE_END): multi-word TIS conclusion —
 *           hs_conclusion_rcvd opens, DATA extension completes the caller
 *           identity BEFORE the settle (construct completion), and the
 *           response frame addresses the FULL multi-word caller.
 *   TEST 2  Responder (WAIT_CYCLE_END): TWAS-concluded frame with multi-word
 *           identity (hs_conclusion_is_twas_) — identity captured, no link,
 *           return to pre-link state (A.5.5.3.2 / A.5.5.4.4 semantics).
 *   TEST 3  Responder: expected_caller gate — a foreign TIS after the caller's
 *           conclusion is classified NONE and changes nothing; the response
 *           still addresses the real caller.
 *   TEST 4  Responder: the FIRST conclusion binds identity ungated (mid-frame
 *           acquisition, A.5.2.5.1) — baseline pin of current behavior for the
 *           Phase 2 shadow-mode comparison, explicitly not a spec claim.
 *   TEST 5  Responder (WAIT_ACK): multi-word ACK conclusion — hs_ack_tis_rcvd
 *           + DATA extension re-arm the settle; the re-arm itself is pinned by
 *           an update inside the would-have-settled window.
 *   TEST 6  Caller (LISTENING): multi-word Response conclusion —
 *           collecting_remote_conclusion accumulates the responder identity;
 *           the ACK frame addresses the FULL multi-word responder; drain → LINKED.
 *   TEST 7  Caller (LISTENING): TWAS rejection (AC-LINK-019-10) — operator
 *           CALL_REJECTED + LINK_TIMEOUT → pre-link state.
 *
 * All tests drive the SM standalone (no CMake-heavy radio stack, no audio):
 * process_received_word() at Trw word cadence + update() to cross the Tdrw
 * settles — the same technique as test_foreign_twas_link_guard.cpp.
 */

#include "Protocol/Control/ale_state_machine.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with fixed-size ALE address fields
#endif

using namespace ale;

namespace {

// Shared harness: SM with all callbacks captured, self address set, started
// scanning (the typical responder context — incoming call arrives while
// scanning). Responder tests use this; caller tests drive from IDLE instead.
struct Harness {
    ALEStateMachine sm;
    std::vector<ALEWord> sent;          // every TX word, via transmit_callback
    std::vector<OperatorEvent> ops;     // operator events, in order
    std::vector<std::pair<ALEState, ALEState>> transitions;

    Harness(const char* self) {
        sm.set_self_address(self);
        sm.set_target_scan_channels(1);
        sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });
        sm.set_state_callback([&](ALEState f, ALEState t) { transitions.push_back({f, t}); });
        sm.set_channel_callback([](const Channel&) {});
        sm.set_rx_enabled_callback([](bool) {});
        sm.set_operator_callback([&](OperatorEvent e) { ops.push_back(e); });
    }
};

// Preamble display name for failure diagnostics.
const char* pname(PreambleType t) {
    switch (t) {
    case PreambleType::DATA: return "DATA";
    case PreambleType::THRU: return "THRU";
    case PreambleType::TO:   return "TO";
    case PreambleType::TWAS: return "TWAS";
    case PreambleType::FROM: return "FROM";
    case PreambleType::TIS:  return "TIS";
    case PreambleType::CMD:  return "CMD";
    case PreambleType::REP:  return "REP";
    }
    return "??";
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
// Responder JOE: caller SAMUEL's frame-1 conclusion is TIS[SAM]+DATA[UEL].
// hs_conclusion_rcvd opens at the anchor; the DATA extension completes the
// identity BEFORE the settle (FR-06 construct completion); the response frame
// addresses the full 2-word caller (TO[SAM] DATA[UEL] doubled + TIS[JOE]).
bool test_handshake_multiword_tis_conclusion() {
    std::cout << "\n[TEST 1] WAIT_CYCLE_END: multi-word TIS conclusion (SAMUEL)\n";
    std::cout << "=============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);      // → SCANNING

    const uint32_t Trw  = ALETimingConstants::Trw_ms;    // 392
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;   // 784
    const char joe[3] = {'J','O','E'};
    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    uint32_t t = 1000u;

    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };

    // SAMUEL calls JOE: scanning TO + leading TO×2, then the conclusion.
    rx(PreambleType::TO,  joe);   // SCANNING → HANDSHAKE (CALL_DETECTED)
    rx(PreambleType::TO,  joe);   // leading ×2 (doubled)
    rx(PreambleType::TO,  joe);
    rx(PreambleType::TIS, sam);   // conclusion anchor → hs_conclusion_rcvd
    check(h.sm.is_hs_conclusion_rcvd(), "TIS[SAM] anchor sets hs_conclusion_rcvd");
    check(h.sm.get_caller_address() == "SAM", "caller identity at anchor = \"SAM\"");

    rx(PreambleType::DATA, uel);  // extension → identity completes pre-settle
    check(h.sm.get_caller_address() == "SAMUEL",
          "DATA[UEL] completes identity to \"SAMUEL\" BEFORE the settle");

    // Settle after the LAST extension word (hs_tlww re-armed per word):
    //   SLOT_WAIT → CHANNEL_CHECK (Tdrw LBT) → SENDING_RESPONSE.
    const size_t resp0 = h.sent.size();            // nothing TX'd in the responder path yet
    const uint32_t settle_t = t - Trw + Tdrw;      // last word + Tdrw
    h.sm.update(settle_t);
    h.sm.update(settle_t + 1);                     // SLOT_WAIT (0 ms) → CHANNEL_CHECK
    h.sm.update(settle_t + 1 + Tdrw);              // LBT clear → SENDING_RESPONSE
    check(h.sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE,
          "Conclusion settle → SENDING_RESPONSE");

    // Response frame addresses the FULL caller: TO[SAM] DATA[UEL] ×2 + TIS[JOE].
    const size_t resp_words = h.sm.get_words_pending();
    std::cout << "  response words queued: " << resp_words
              << " (sent before: " << resp0 << ")\n";
    check(resp_words == 5, "5 response words (2-word caller doubled + TIS)");
    check(h.sent.size() == resp0 + 5, "transmit_callback saw the 5 response words");
    if (h.sent.size() >= resp0 + 5) {
        // sent[] layout: [TO SAM, DATA UEL, TO SAM, DATA UEL, TIS JOE]
        check(h.sent[resp0 + 0].type == PreambleType::TO
              && strncmp(h.sent[resp0 + 0].address, "SAM", 3) == 0, "word 0: TO[SAM]");
        check(h.sent[resp0 + 1].type == PreambleType::DATA
              && strncmp(h.sent[resp0 + 1].address, "UEL", 3) == 0, "word 1: DATA[UEL]");
        check(h.sent[resp0 + 2].type == PreambleType::TO
              && strncmp(h.sent[resp0 + 2].address, "SAM", 3) == 0, "word 2: TO[SAM]");
        check(h.sent[resp0 + 3].type == PreambleType::DATA
              && strncmp(h.sent[resp0 + 3].address, "UEL", 3) == 0, "word 3: DATA[UEL]");
        check(h.sent[resp0 + 4].type == PreambleType::TIS
              && strncmp(h.sent[resp0 + 4].address, "JOE", 3) == 0, "word 4: TIS[JOE]");
    }

    if (all_pass) std::cout << "PASS: multi-word TIS conclusion accumulates + response addresses full caller\n";
    return all_pass;
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
// Responder JOE: frame-1 conclusion TWAS[SL3]+DATA[ZXB] (hs_conclusion_is_twas_).
// Identity must still complete multi-word; the settle aborts (LINK_TIMEOUT →
// pre-link state) instead of linking. Characterizes the same accumulation path
// TEST 1 uses, opened by TWAS instead of TIS.
bool test_handshake_multiword_twas_conclusion() {
    std::cout << "\n[TEST 2] WAIT_CYCLE_END: multi-word TWAS conclusion (SL3ZXB)\n";
    std::cout << "==============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);      // → SCANNING (pre-link state)

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char joe[3] = {'J','O','E'};
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};
    uint32_t t = 1000u;

    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };

    rx(PreambleType::TO,   joe);   // SCANNING → HANDSHAKE
    rx(PreambleType::TO,   joe);   // leading ×2
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TWAS, sl3);   // TWAS conclusion anchor (identity capture like TIS)
    rx(PreambleType::DATA, zxb);   // extension → full identity pre-settle

    check(h.sm.get_state() == ALEState::HANDSHAKE, "Still HANDSHAKE while accumulating");
    check(h.sm.get_caller_address() == "SL3ZXB",
          "TWAS+DATA completes identity to \"SL3ZXB\" BEFORE the settle");
    check(h.sent.empty(), "No response transmitted before the settle decides");

    // Settle: TWAS conclusion → LINK_TIMEOUT → pre-link state (SCANNING), no link.
    h.sm.update(t - Trw + Tdrw + 1);
    check(h.sm.get_state() == ALEState::SCANNING,
          "TWAS-concluded frame: no link, back to pre-link state (SCANNING)");
    check(h.sent.empty(), "No response frame for a TWAS-concluded calling cycle");

    if (all_pass) std::cout << "PASS: multi-word TWAS conclusion captures identity, no link\n";
    return all_pass;
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
// Responder JOE: after SAMUEL's TIS[SAM]+DATA[UEL] conclusion, a foreign
// station's TIS[OH2] arrives inside the settle window. classify()'s
// expected_caller gate (HANDSHAKE only) must classify it NONE: no re-arm, no
// identity pollution. The response still addresses the real caller.
bool test_handshake_foreign_tis_ignored() {
    std::cout << "\n[TEST 3] expected_caller gate: foreign TIS after conclusion ignored\n";
    std::cout << "=====================================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char joe[3] = {'J','O','E'};
    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    const char oh2[3] = {'O','H','2'};
    uint32_t t = 1000u;

    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };

    rx(PreambleType::TO,   joe);   // → HANDSHAKE
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TIS,  sam);   // real caller conclusion
    rx(PreambleType::DATA, uel);   // → "SAMUEL"
    rx(PreambleType::TIS,  oh2);   // foreign station, inside the settle window

    check(h.sm.get_caller_address() == "SAMUEL",
          "foreign TIS[OH2] does not touch caller identity (stays \"SAMUEL\")");
    check(h.sm.get_state() == ALEState::HANDSHAKE, "foreign TIS causes no state change");

    // Settle anchored at the last DATA (foreign TIS did not re-arm it).
    const uint32_t data_t   = t - 2u * Trw;        // time of DATA[UEL]
    const uint32_t settle_t = data_t + Tdrw;
    h.sm.update(settle_t);
    h.sm.update(settle_t + 1);
    h.sm.update(settle_t + 1 + Tdrw);              // → SENDING_RESPONSE
    check(h.sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE,
          "Settle still advances to SENDING_RESPONSE");

    // The response addresses the REAL caller, not the foreign TIS.
    bool resp_ok = h.sent.size() == 5
        && h.sent[0].type == PreambleType::TO   && strncmp(h.sent[0].address, "SAM", 3) == 0
        && h.sent[1].type == PreambleType::DATA && strncmp(h.sent[1].address, "UEL", 3) == 0
        && h.sent[4].type == PreambleType::TIS  && strncmp(h.sent[4].address, "JOE", 3) == 0;
    check(resp_ok, "Response frame addresses SAMUEL (TO[SAM] DATA[UEL] ×2 + TIS[JOE])");

    if (all_pass) std::cout << "PASS: foreign TIS in the settle window is observation-only\n";
    return all_pass;
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
// Baseline pin (current behavior, explicitly not a spec claim): the FIRST
// conclusion word binds identity ungated — expected_caller is derived from
// caller_address itself, which is empty before any conclusion, so a mid-frame
// acquisition (A.5.2.5.1: a receiver may join at any point) accepts whatever
// conclusion it decodes first and its extensions accumulate onto it. The
// Phase 2 reassembler must reproduce this exactly; the Phase 3 context matrix
// then decides whether the identity is acted on.
bool test_handshake_first_conclusion_binds_ungated() {
    std::cout << "\n[TEST 4] Baseline: first conclusion binds identity ungated\n";
    std::cout << "==========================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char joe[3] = {'J','O','E'};
    const char oh2[3] = {'O','H','2'};
    const char yyy[3] = {'Y','Y','Y'};
    uint32_t t = 1000u;

    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };

    rx(PreambleType::TO,   joe);   // → HANDSHAKE
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TIS,  oh2);   // first conclusion heard — binds
    rx(PreambleType::DATA, yyy);   // extension accumulates onto it

    check(h.sm.get_caller_address() == "OH2YYY",
          "current behavior: first conclusion binds, extension accumulates (\"OH2YYY\")");

    // Settle: proceeds as a normal TIS conclusion (this is an accepted-call path).
    const uint32_t settle_t = t - Trw + Tdrw;
    h.sm.update(settle_t);
    h.sm.update(settle_t + 1);
    h.sm.update(settle_t + 1 + Tdrw);              // → SENDING_RESPONSE
    check(h.sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE,
          "Settle advances normally on the bound identity");

    if (all_pass) std::cout << "PASS: first-conclusion binding pinned as baseline\n";
    return all_pass;
}

// ── TEST 5 ───────────────────────────────────────────────────────────────────
// Responder JOE: caller SL3ZXB's ACK concludes multi-word (TIS[SL3]+DATA[ZXB]).
// hs_ack_tis_rcvd opens at the TIS anchor; the DATA extension re-arms hs_tlww
// — pinned by an update at TIS+Trw+700ms, which is past TIS+Tdrw (the settle
// would have fired had the extension NOT re-armed it) but before the true
// extension-anchored settle. Then the true settle → LINKED.
bool test_wait_ack_multiword_ack_conclusion() {
    std::cout << "\n[TEST 5] WAIT_ACK: multi-word ACK conclusion (SL3ZXB)\n";
    std::cout << "====================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char joe[3] = {'J','O','E'};
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};
    uint32_t t = 1000u;

    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };

    // Frame 1 from SL3ZXB (multi-word conclusion, as TEST 1).
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TO,   joe);
    rx(PreambleType::TIS,  sl3);
    rx(PreambleType::DATA, zxb);
    const size_t resp_before = h.sent.size();
    const uint32_t f1_settle = t - Trw + Tdrw;
    h.sm.update(f1_settle);                        // → SLOT_WAIT
    h.sm.update(f1_settle + 1);                     // → CHANNEL_CHECK
    h.sm.update(f1_settle + 1 + Tdrw);             // → SENDING_RESPONSE (5 words)

    // Drain our response frame → WAIT_ACK. Capture the count first: it
    // decrements per completion (TEST-14 pattern).
    const uint32_t resp_words = h.sm.get_words_pending();
    for (uint32_t i = 0; i < resp_words; ++i)
        h.sm.on_word_complete();
    check(h.sm.get_handshake_phase() == HandshakePhase::WAIT_ACK,
          "Response drained → WAIT_ACK");
    check(h.sent.size() == resp_before + 5, "Response frame was the 5-word multi-word form");

    // Frame 3 (caller's ACK): TO[JOE]×2 + TIS[SL3] + DATA[ZXB].
    const uint32_t ack0 = f1_settle + 1 + Tdrw + 500;   // inside the WAIT_ACK window
    h.sm.update(ack0);                 h.sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe));
    h.sm.update(ack0 + Trw);           h.sm.process_received_word(WordParser::make_word(PreambleType::TO,  joe));
    const uint32_t tis_t = ack0 + 2u * Trw;
    h.sm.update(tis_t);                 h.sm.process_received_word(WordParser::make_word(PreambleType::TIS, sl3));
    const uint32_t data_t = tis_t + Trw;
    h.sm.update(data_t);               h.sm.process_received_word(WordParser::make_word(PreambleType::DATA, zxb));

    check(h.sm.get_state() == ALEState::HANDSHAKE, "Still HANDSHAKE while ACK conclusion accumulates");
    check(h.sm.get_caller_address() == "SL3ZXB", "caller identity intact (\"SL3ZXB\")");

    // Re-arm pin: this update is TIS+Trw+700 (> TIS+Tdrw) — without the
    // extension re-arm, the settle would have fired HERE and linked early.
    h.sm.update(data_t + 300);
    check(h.sm.get_state() == ALEState::HANDSHAKE,
          "DATA[ZXB] re-armed the settle (no premature LINKED at TIS+Tdrw)");

    // True settle (anchored at the extension) → LINKED.
    h.sm.update(data_t + Tdrw + 1);
    check(h.sm.get_state() == ALEState::LINKED, "ACK conclusion settle → LINKED");
    check(!h.ops.empty()
          && std::find(h.ops.begin(), h.ops.end(), OperatorEvent::LINK_ESTABLISHED) != h.ops.end(),
          "Operator notified LINK_ESTABLISHED");

    if (all_pass) std::cout << "PASS: multi-word ACK conclusion settles on its last word\n";
    return all_pass;
}

// ── TEST 6 ───────────────────────────────────────────────────────────────────
// Caller SAM: the responder SL3ZXB's Response frame concludes multi-word
// (TIS[SL3]+DATA[ZXB]). collecting_remote_conclusion accumulates the responder
// identity in to_address (active_call_from); the ACK frame addresses the FULL
// 2-word responder; draining the ACK → LINKED.
bool test_calling_multiword_response_conclusion() {
    std::cout << "\n[TEST 6] LISTENING: multi-word Response conclusion (SL3ZXB)\n";
    std::cout << "=============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("SAM");                              // caller, from IDLE
    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const char sam[3] = {'S','A','M'};
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};

    check(h.sm.initiate_call("SL3ZXB"), "initiate_call(SL3ZXB) accepted");
    check(h.sm.get_active_call_to() == "SL3ZXB", "active_call_to = full target address");

    const uint32_t tx0 = Twt + Tt;                 // first TX slot (TEST-8 timing)
    auto send_slot = [&](uint32_t slot_t) {
        h.sm.update(slot_t);
        h.sm.on_word_complete();
    };

    h.sm.update(Twt);                              // LBT → TUNING
    h.sm.update(tx0);                              // TUNING → SCANNING_CALL
    // Scanning: TO[SL3]×2; leading: TO[SL3] DATA[ZXB] ×2; conclusion: TIS[SAM].
    for (uint32_t i = 0; i < 7; ++i)
        send_slot(tx0 + i * Trw);
    check(h.sm.get_calling_phase() == CallingPhase::LISTENING,
          "7 slots (2 scan + 4 leading + 1 conclusion) → LISTENING");
    check(h.sent.size() == 7, "7 words transmitted for the multi-word call frame");

    // Responder's Response frame: TO[SAM]×2 + TIS[SL3] + DATA[ZXB].
    uint32_t t = tx0 + 7u * Trw + 100;
    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };
    rx(PreambleType::TO,   sam);   // Response addressed to us → response_to_detected
    rx(PreambleType::TO,   sam);
    rx(PreambleType::TIS,  sl3);   // responder conclusion anchor → collecting
    rx(PreambleType::DATA, zxb);   // extension completes the responder identity

    check(h.sm.get_response_to_detected(), "TO[SAM] armed response tracking");
    check(h.sm.get_to_address() == "SL3ZXB",
          "TIS[SL3]+DATA[ZXB] accumulated to \"SL3ZXB\" BEFORE the settle");

    // Settle (anchored at the extension) → SENDING_ACK. build_ack_words() is
    // deferred to the next update() tick (words_pending==0 && call_cycles_
    // in_phase==0 gate in handle_calling), so one more update arms the frame.
    const uint32_t data_t = t - Trw;
    const size_t ack0 = h.sent.size();
    h.sm.update(data_t + Tdrw);
    check(h.sm.get_calling_phase() == CallingPhase::SENDING_ACK,
          "Response conclusion settle → SENDING_ACK");
    h.sm.update(data_t + Tdrw + 1);                // → build_ack_words()

    const size_t ack_words = h.sm.get_words_pending();
    std::cout << "  ACK words queued: " << ack_words << "\n";
    check(ack_words == 5, "5 ACK words (2-word responder doubled + TIS)");
    if (h.sent.size() >= ack0 + 5) {
        check(h.sent[ack0 + 0].type == PreambleType::TO
              && strncmp(h.sent[ack0 + 0].address, "SL3", 3) == 0, "ACK word 0: TO[SL3]");
        check(h.sent[ack0 + 1].type == PreambleType::DATA
              && strncmp(h.sent[ack0 + 1].address, "ZXB", 3) == 0, "ACK word 1: DATA[ZXB]");
        check(h.sent[ack0 + 4].type == PreambleType::TIS
              && strncmp(h.sent[ack0 + 4].address, "SAM", 3) == 0, "ACK word 4: TIS[SAM]");
    }

    // Drain the ACK → HANDSHAKE_COMPLETE → LINKED.
    for (size_t i = 0; i < ack_words; ++i)
        h.sm.on_word_complete();
    check(h.sm.get_state() == ALEState::LINKED, "ACK drained → LINKED");

    if (all_pass) std::cout << "PASS: multi-word Response accumulates + ACK addresses full responder\n";
    return all_pass;
}

// ── TEST 7 ───────────────────────────────────────────────────────────────────
// Caller SAM: responder JOE rejects with TWAS[JOE] in the Response frame
// (AC-LINK-019-10). Operator gets CALL_REJECTED; LINK_TIMEOUT returns the
// caller to its pre-link state. No ACK is transmitted.
bool test_calling_twas_rejection() {
    std::cout << "\n[TEST 7] LISTENING: TWAS rejection → CALL_REJECTED, no ACK\n";
    std::cout << "==============================================================\n";
    bool all_pass = true;
    auto check = [&](bool cond, const char* label) {
        std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
        all_pass = all_pass && cond;
    };

    Harness h("SAM");                              // caller, from IDLE → pre-link = IDLE
    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const char sam[3] = {'S','A','M'};
    const char joe[3] = {'J','O','E'};

    check(h.sm.initiate_call("JOE"), "initiate_call(JOE) accepted");
    const uint32_t tx0 = Twt + Tt;
    auto send_slot = [&](uint32_t slot_t) {
        h.sm.update(slot_t);
        h.sm.on_word_complete();
    };
    h.sm.update(Twt);
    h.sm.update(tx0);
    for (uint32_t i = 0; i < 5; ++i)               // 2 scan + 2 leading + 1 conclusion
        send_slot(tx0 + i * Trw);
    check(h.sm.get_calling_phase() == CallingPhase::LISTENING, "Reached LISTENING");

    const size_t sent_before = h.sent.size();
    uint32_t t = tx0 + 5u * Trw + 100;
    auto rx = [&](PreambleType ty, const char a3[3]) {
        h.sm.update(t);
        h.sm.process_received_word(WordParser::make_word(ty, a3));
        t += Trw;
    };
    rx(PreambleType::TO,   sam);   // rejection frame still leads with TO[SAM]×2
    rx(PreambleType::TO,   sam);
    rx(PreambleType::TWAS, joe);   // TWAS[JOE] — JOE's self identity, the rejection

    check(h.sm.get_state() == ALEState::IDLE,
          "TWAS rejection → LINK_TIMEOUT → pre-link state (IDLE)");
    check(std::find(h.ops.begin(), h.ops.end(), OperatorEvent::CALL_REJECTED) != h.ops.end(),
          "Operator notified CALL_REJECTED");
    check(h.sent.size() == sent_before, "No ACK transmitted after a rejection");

    if (all_pass) std::cout << "PASS: TWAS rejection path pinned\n";
    return all_pass;
}

} // namespace

int main() {
    std::cout << std::unitbuf;   // flush per write — crash diagnostics survive
    std::cout << "==========================================================\n";
    std::cout << "RX characterization — OFS Phase 0 behavior pins\n";
    std::cout << "==========================================================\n";

    bool ok = true;
    ok &= test_handshake_multiword_tis_conclusion();
    ok &= test_handshake_multiword_twas_conclusion();
    ok &= test_handshake_foreign_tis_ignored();
    ok &= test_handshake_first_conclusion_binds_ungated();
    ok &= test_wait_ack_multiword_ack_conclusion();
    ok &= test_calling_multiword_response_conclusion();
    ok &= test_calling_twas_rejection();

    if (ok) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}