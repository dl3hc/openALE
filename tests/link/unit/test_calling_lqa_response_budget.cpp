/**
 * \file tests/link/unit/test_calling_lqa_response_budget.cpp
 * \brief Regression tests for GitHub issue #5 — CALLING budget vs the
 *        LQA-report-carrying response frame
 *
 * Root cause (issue #5): calc_calling_timeout_ms()'s listen term is a fixed
 * ~5.1 s sized for a bare TO×2+TIS response; it carries no term for the
 * responder's message section. With bilateral LQA exchange enabled on both
 * stations, the responder's response frame carries CMD 'a' + CMD 'r' +
 * DATA… (up to Tm max basic = 30×Trw), and the caller's fixed budget fired
 * LINK_TIMEOUT mid-response, killing the link on both ends.
 *
 * Fix under test (A.5.5.3.3: the caller accepts the conclusion "starting
 * within Tlc (plus Tm max, if message included)"):
 *   - check_link_timeout() CALLING: once response_to_detected, the
 *     response-conclusion window (Tlc + Tm max incl. AMD, anchored at the
 *     first TO-self word) replaces the fixed budget — through SENDING_ACK.
 *   - WAIT_CYCLE_END Tmmax uses Tm max incl. AMD (59×Trw, A.5.8.4), not the
 *     11.76 s basic value (max-length calling-frame AMD races the basic
 *     bound to its TIS conclusion).
 *   - LqaExchangeManager::kMaxReportEntries capped so the response message
 *     section fits Tm max basic (compile-time static_assert there).
 *
 * Verifies:
 *   TEST 1  long LQA-report-carrying response → LINKED (elapsed at TIS
 *            provably exceeds the old fixed budget — the pre-fix abort point)
 *   TEST 2  no response at all → CALLING still aborts (LISTENING(a) window)
 *   TEST 3  response starts then stalls → 5×Trw silence abort still fires,
 *            long before the response-conclusion window
 *   TEST 4  responder: max-length calling-frame AMD (TIS 12.15 s after
 *            message start, past the 11.76 s basic bound) → conclusion
 *            still received, handshake not aborted
 */

#include "Word/ale_word.h"
#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <vector>

namespace ale {

// ── Harness (pattern: tests/word/unit/test_ale_calling.cpp) ──────────────────

struct WordCapture {
    std::vector<ALEWord> words;
    void record(const ALEWord& w) { words.push_back(w); }
    size_t size() const { return words.size(); }
};

static ALEStateMachine make_sm(WordCapture& cap, const std::string& self = "SAM")
{
    ALEStateMachine sm;
    sm.set_transmit_callback([&cap](const ALEWord& w){ cap.record(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address(self);
    sm.set_target_scan_channels(0);   // no scanning section: leading starts at once
    return sm;
}

// Build a received word on the RX grid: advance the SM clock to t, then feed.
static void rx_at(ALEStateMachine& sm, uint32_t t, const ALEWord& w)
{
    sm.update(t);
    sm.process_received_word(w);
}

static ALEWord addr_word(PreambleType t, const char* a3)
{
    const char ch[3] = { a3[0], a3[1], a3[2] };
    return WordParser::make_word(t, ch);
}

// Hand-built CMD function word (TABLE A-XVI layout: first char W4-W10,
// second char W11-W17, payload bits W18-W24) — valid, like the demodulator
// would deliver it. The SM's CALLING path classifies CMD as NONE; what
// matters here is that it re-anchors last_word_time_ms on the RX grid.
static ALEWord cmd_word(char first, uint8_t low7)
{
    ALEWord w{};
    w.type        = PreambleType::CMD;
    w.raw_payload = (static_cast<uint32_t>(static_cast<uint8_t>(first)) << 14)
                  | static_cast<uint32_t>(low7 & 0x7Fu);
    w.address[0]  = first;
    w.address[1]  = ' ';
    w.address[2]  = ' ';
    w.address[3]  = '\0';
    w.valid       = true;
    w.unanimous_votes = 48;
    return w;
}

// Place the caller in CALLING/LISTENING with its own calling frame fully
// transmitted (drained on the protocol clock). Returns the clock time of the
// last TX word.
static uint32_t drain_calling_frame(ALEStateMachine& sm, WordCapture& cap,
                                    const char* target)
{
    sm.initiate_call(target);
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    uint32_t t = T_TX;
    sm.update(ALETimingConstants::Twt_ms);   // LBT → TUNING
    sm.update(t);                            // tune complete → sequence enqueued
    for (size_t i = 0; i < cap.size(); ++i) {
        t += ALETimingConstants::Trw_ms;
        sm.update(t);
        sm.on_word_complete();
    }
    return t;   // TRANSMITTING → LISTENING after the last word
}

// The OLD fixed budget (calc_calling_timeout_ms) for the exact call shape the
// tests place on the air: 1 channel, no scanning section, 3-char target
// (leading 2 words), 3-char self (conclusion 1 word), no MESSAGE section.
static uint32_t old_fixed_budget()
{
    const CallingBudgetParams p{ 1u, 0u, 2u, 1u, 0u };
    return calc_calling_timeout_ms(p);
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
// Both-enabled bilateral-LQA response shape: TO[self]×2 + CMD 'a' + CMD 'r'
// + 28 DATA (16-entry report, the new kMaxReportEntries ceiling) + TIS[peer].
// The TIS lands far past the old fixed budget — the pre-fix abort point —
// and must still reach LINKED.
bool test_long_lqa_response_links()
{
    std::cout << "\n[ISSUE-5] Long LQA-report response → LINKED\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    const uint32_t tx_end = drain_calling_frame(sm, cap, "JOE");

    // Responder turnaround, then the response on the word grid.
    uint32_t t = tx_end + 1600;
    rx_at(sm, t,            addr_word(PreambleType::TO,  "SAM"));   // TO-self ×2
    rx_at(sm, t += Trw,     addr_word(PreambleType::TO,  "SAM"));
    rx_at(sm, t += Trw,     cmd_word('a', 0x00u));                   // CMD 'a'
    rx_at(sm, t += Trw,     cmd_word('r', 0x15u));                   // CMD 'r' header
    for (int i = 0; i < 28; ++i)                                     // report DATA
        rx_at(sm, t += Trw, addr_word(PreambleType::DATA, "XYZ"));
    rx_at(sm, t += Trw,     addr_word(PreambleType::TIS, "JOE"));   // conclusion
    const uint32_t t_tis = t;

    // The conclusion provably lands past the pre-fix budget: this test
    // exercises the bug, not a nearby timing.
    std::cout << "  TIS at t=" << t_tis << " ms, old fixed budget = "
              << old_fixed_budget() << " ms\n";
    if (t_tis <= old_fixed_budget()) {
        std::cout << "  FAIL: TIS not past the old budget — test setup error\n";
        return false;
    }
    if (sm.get_state() != ALEState::CALLING) {
        std::cout << "  FAIL: SM left CALLING before the conclusion arrived\n";
        return false;
    }

    // LISTENING(c) settle → SENDING_ACK → build + drain the ACK → LINKED.
    // (t re-anchored at the settle update — the SM's non-monotonic clock
    // guard holds updates whose timestamp goes backward.)
    t = t_tis + ALETimingConstants::Tdrw_ms + 1;
    sm.update(t);
    for (int i = 0; i < 3; ++i) {
        t += Trw;
        sm.update(t);
        sm.on_word_complete();
    }
    const bool linked = sm.get_state() == ALEState::LINKED;
    std::cout << "  reached LINKED after report-carrying response: "
              << (linked ? "PASS" : "FAIL") << "\n";
    return linked;
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
// No response at all: LISTENING(a) still aborts the call (response-start
// window), independent of the new response-conclusion window.
bool test_no_response_still_aborts()
{
    std::cout << "\n[ISSUE-5] No response → CALLING still aborts (LISTENING(a))\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);
    const uint32_t tx_end = drain_calling_frame(sm, cap, "JOE");

    // Twrt_slow + Tdrw + settle ≈ 3136 ms with no TO-self ever arriving.
    sm.update(tx_end + 3400);
    const bool aborted = sm.get_state() != ALEState::CALLING;
    std::cout << "  CALLING aborted without any response: "
              << (aborted ? "PASS" : "FAIL") << "\n";
    return aborted;
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
// Response starts (TO-self) then stalls: the LISTENING(b) 5×Trw silence
// window must abort it — long before the ~26 s response-conclusion window
// would — so a stalled/bogus response cannot hold the channel.
bool test_stalled_response_aborts_early()
{
    std::cout << "\n[ISSUE-5] Stalled response → 5×Trw silence abort (early)\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    const uint32_t tx_end = drain_calling_frame(sm, cap, "JOE");
    uint32_t t = tx_end + 1600;
    rx_at(sm, t,        addr_word(PreambleType::TO, "SAM"));   // response starts…
    rx_at(sm, t += Trw, addr_word(PreambleType::TO, "SAM"));
    // …then silence (no CMD, no DATA, no TIS).

    sm.update(t + 5u * Trw + 100);
    const bool aborted = sm.get_state() != ALEState::CALLING;
    const uint32_t elapsed_since_to = 5u * Trw + 100;
    std::cout << "  CALLING aborted " << elapsed_since_to
              << " ms after last response word (window is ~26 s): "
              << (aborted ? "PASS" : "FAIL") << "\n";
    return aborted;
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
// Responder side, sibling fix: WAIT_CYCLE_END's Tmmax check uses Tm max
// incl. AMD (59×Trw). A max-length calling-frame AMD (CMD AMD + 30 data
// words) puts its TIS conclusion 31×Trw = 12 152 ms after the first DATA
// word — past the old 11 760 ms basic bound (which aborted the handshake
// before the conclusion), well under the 23 128 ms bound.
bool test_wait_cycle_end_amd_conclusion_within_tm_max_amd()
{
    std::cout << "\n[ISSUE-5 sibling] Long calling-frame AMD → conclusion received\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM");   // responder role
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    // Incoming call to SAM: scanning TO×2 + leading TO×2 → HANDSHAKE.
    uint32_t t = 1000;
    rx_at(sm, t,        addr_word(PreambleType::TO, "SAM"));
    rx_at(sm, t += Trw, addr_word(PreambleType::TO, "SAM"));
    rx_at(sm, t += Trw, addr_word(PreambleType::TO, "SAM"));
    rx_at(sm, t += Trw, addr_word(PreambleType::TO, "SAM"));
    if (sm.get_state() != ALEState::HANDSHAKE) {
        std::cout << "  FAIL: setup error — not in HANDSHAKE\n";
        return false;
    }

    // Message section: CMD AMD + 30 DATA words (90 chars, A.5.7.2.3 max).
    rx_at(sm, t += Trw, cmd_word('H', 0x00u));
    for (int i = 0; i < 30; ++i)
        rx_at(sm, t += Trw, addr_word(PreambleType::DATA, "XYZ"));
    const uint32_t t_msg_start = t - 29u * Trw;   // first DATA word

    // TIS conclusion one word-past-the-max later: 31×Trw after message start
    // (12 152 ms — the frame's own gap past the 30th data word, decisively
    // beyond the old 11 760 ms basic bound, under the 23 128 ms AMD bound).
    rx_at(sm, t += 2u * Trw, addr_word(PreambleType::TIS, "OTH"));
    const uint32_t msg_to_tis = t - t_msg_start;
    std::cout << "  TIS " << msg_to_tis << " ms after message start"
              << " (old bound 11760, new bound 23128)\n";
    if (msg_to_tis <= ALETimingConstants::Tm_max_ms) {
        std::cout << "  FAIL: setup error — TIS not past the old basic bound\n";
        return false;
    }

    const bool conclusion = sm.is_hs_conclusion_rcvd();
    const bool still_hs   = sm.get_state() == ALEState::HANDSHAKE;
    std::cout << "  conclusion received, handshake alive: "
              << ((conclusion && still_hs) ? "PASS" : "FAIL") << "\n";
    return conclusion && still_hs;
}

} // namespace ale

int main()
{
    bool ok = true;
    ok &= ale::test_long_lqa_response_links();
    ok &= ale::test_no_response_still_aborts();
    ok &= ale::test_stalled_response_aborts_early();
    ok &= ale::test_wait_cycle_end_amd_conclusion_within_tm_max_amd();

    std::cout << (ok ? "\nALL ISSUE-5 RESPONSE-BUDGET TESTS PASSED\n"
                     : "\nISSUE-5 RESPONSE-BUDGET TESTS FAILED\n");
    return ok ? 0 : 1;
}