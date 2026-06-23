/**
 * \file test_ale_calling.cpp
 * \brief Acceptance tests for FEAT-WORD-002 — Address Words (TO/TIS/TWAS/THRU/FROM)
 *
 * Covers REQ-WORD-003 through REQ-WORD-007 per MIL-STD-188-141B A.5.2.3.2.
 *
 * Timing model for state-machine tests (callback-driven, DD-013):
 *   Phase transitions happen in on_word_complete(), not in update().
 *   These tests drive the SM in isolation — no modem, no audio device.
 *   on_word_complete() is called directly to simulate audio-frame completion.
 *
 *   With set_target_scan_channels(0) the CALLING state enters LEADING_CALL
 *   directly. For a 3-char address addr = "ABC" (1 word), Tc = 1 × Trw:
 *     update(0)      → LEADING seq 1 fires (TO "ABC");  on_word_complete()
 *     update(Trw_ms) → LEADING seq 2 fires (TO "ABC");  on_word_complete() → CONCLUSION
 *     update(2×Trw)  → CONCLUSION fires (TIS self)
 */

#include "Word/ale_word.h"
#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>

namespace ale {

// ============================================================================
// Helper
// ============================================================================

struct WordCapture {
    std::vector<ALEWord> words;
    void record(const ALEWord& w) { words.push_back(w); }
    void clear() { words.clear(); }
    size_t size() const { return words.size(); }
    bool empty() const { return words.empty(); }
};

static ALEStateMachine make_sm(WordCapture& cap,
                               const std::string& self = "SAM",
                               uint32_t scan_ch = 0)
{
    ALEStateMachine sm;
    sm.set_transmit_callback([&cap](const ALEWord& w){ cap.record(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address(self);
    sm.set_target_scan_channels(scan_ch);
    return sm;
}

// Drive through LBT+TUNING.  At tune-complete the SM enqueues the COMPLETE
// TX sequence back-to-back (leading call + conclusion; plus scanning section
// when scan_ch > 0), so cap then holds every word of the call in order.
// The conclusion (TIS self [+DATA/REP]) is the tail of the capture.
static void advance_to_tx_start(ALEStateMachine& sm)
{
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms);  // LBT → TUNING
    sm.update(T_TX);                         // TUNING ends → full sequence enqueued
}

// Build a received address word from a 3-char address (for process_received_word).
static ALEWord rx_word(PreambleType t, const char a3[3])
{
    const char ch[3] = { a3[0], a3[1], a3[2] };
    return WordParser::make_word(t, ch);
}

// Deliver one received word at protocol time t: advance the SM clock to t, then
// feed the word.  update(t) runs the WAIT_CYCLE_END / WAIT_ACK timeout checks at
// the word boundary, exactly as the real RX path does between word arrivals.
static void rx_at(ALEStateMachine& sm, uint32_t t, PreambleType type, const char* a3)
{
    const char ch[3] = { a3[0], a3[1], a3[2] };
    sm.update(t);
    sm.process_received_word(WordParser::make_word(type, ch));
}

// ============================================================================
// RX MULTI-WORD — receiving (accepting) calls whose addresses exceed 3 chars.
//
// A calling cycle for an N-char address carries TO + DATA + REP + … extension
// words; the leading call sends the WHOLE address twice (Tlc = 2 × Tc).  The
// longer the address, the later the conclusion (TIS) arrives relative to the
// moment the called station detects its own first word and enters HANDSHAKE.
// These tests drive that RX timeline word-by-word and verify the conclusion is
// still collected (regression for the WAIT_CYCLE_END timeout being measured
// from frame start instead of from the last received word).
// ============================================================================

// Helper: feed a full incoming individual call (scanning + leading + conclusion)
// for self-address `self`, called by `caller`, on the SM grid (one word / Trw).
// Returns the SM after the conclusion word has been delivered.
static void feed_incoming_call(ALEStateMachine& sm,
                               const std::string& self,
                               const std::string& caller,
                               uint32_t t0 = 1000)
{
    const uint32_t Trw = ALETimingConstants::Trw_ms;

    // What SAM (caller) transmits to reach `self`:
    //   scanning: TO <self first-word>            × 2 slots
    //   leading : full TO <self address>          × 2  (Tlc = 2 × Tc)
    //   conclusion: TIS <caller address>
    const auto self_to   = AddressEncoder::encode(self,   PreambleType::TO);
    const auto caller_tis = AddressEncoder::encode(caller, PreambleType::TIS);

    uint32_t slot = 0;
    auto at = [&](){ return t0 + (slot++) * Trw; };

    // Scanning section: first word of the destination, twice.
    rx_at(sm, at(), PreambleType::TO, self_to.front().address);
    rx_at(sm, at(), PreambleType::TO, self_to.front().address);

    // Leading call: whole destination address, sent twice.
    for (int copy = 0; copy < 2; ++copy)
        for (const auto& w : self_to)
            rx_at(sm, at(), w.type, w.address);

    // Conclusion: caller's whole address (TIS + DATA/REP extensions).
    for (const auto& w : caller_tis)
        rx_at(sm, at(), w.type, w.address);
}

// JOE has a >3-char own address; a 3-char SAM calls it.  The leading call (full
// JOE address, doubled) pushes the conclusion well past the old Twce window.
bool test_rx_multiword_self_address()
{
    std::cout << "\n[RX-MULTIWORD] Accept call addressed to a >3-char own address\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"JOEMAN", 0);
    sm.process_event(ALEEvent::START_SCAN);

    feed_incoming_call(sm, /*self=*/"JOEMAN", /*caller=*/"SAM");

    bool detected   = sm.get_state() == ALEState::HANDSHAKE
                   || sm.get_state() == ALEState::LINKED;
    bool got_concl  = sm.is_hs_conclusion_rcvd();
    bool caller_ok  = sm.get_caller_address() == "SAM";

    std::cout << "  reached/stayed in HANDSHAKE: " << (detected ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
    std::cout << "  conclusion (TIS) collected: " << (got_concl ? "PASS" : "FAIL") << "\n";
    std::cout << "  caller address = \"SAM\": " << (caller_ok ? "PASS" : "FAIL")
              << " (got \"" << sm.get_caller_address() << "\")\n";

    return detected && got_concl && caller_ok;
}

// Reconstruct a whole address from a contiguous run of TX words (anchor word
// followed by DATA/REP extension words), mirroring how a peer reassembles it.
static std::string reassemble(const std::vector<ALEWord>& w, size_t begin, size_t count)
{
    std::string a;
    for (size_t i = begin; i < begin + count && i < w.size(); ++i)
        a += trim_ale_address(w[i].address);
    return a;
}

// Full accept path at the maximum 15-char address length, for BOTH endpoints:
// JOE must collect SAM's whole 15-char address from the conclusion and then
// transmit a response that re-addresses that whole address (TO caller × 2) and
// announces its own whole 15-char address (TIS self).
bool test_rx_multiword_full_accept_15char()
{
    std::cout << "\n[RX-MULTIWORD] Full accept path at 15-char addresses\n";

    const std::string self   = "VERYLONGCALLSIG";  // 15 chars (5 words)
    const std::string caller = "SAMUELBRAVOXRAY";  // 15 chars (5 words)

    WordCapture cap;  // captures JOE's transmitted response words
    ALEStateMachine sm = make_sm(cap, self, 0);
    sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;

    feed_incoming_call(sm, self, caller);            // scanning + leading + conclusion

    bool caller_ok = sm.get_caller_address() == caller;
    std::cout << "  whole caller address collected: " << (caller_ok ? "PASS" : "FAIL")
              << " (got \"" << sm.get_caller_address() << "\")\n";

    // Drive JOE through the conclusion settle → SLOT_WAIT → CHANNEL_CHECK (LBT
    // clear, no words) → SENDING_RESPONSE, which builds the response frame.
    uint32_t t = 1000u + 16u * Trw;                  // time of last conclusion word
    sm.update(t + Tdrw + 1);                          // WAIT_CYCLE_END settle → SLOT_WAIT
    sm.update(t + Tdrw + 2);                          // SLOT_WAIT (Tswt=0) → CHANNEL_CHECK
    sm.update(t + Tdrw + 2 + Tdrw + 1);               // CHANNEL_CHECK (Tdrw=2×Trw LBT) clear → SENDING_RESPONSE

    // Response frame (Figure A-30): TO caller × 2 + TIS self.
    bool count_ok = cap.size() == 15;                 // 5 (TO) × 2 + 5 (TIS)
    bool to_addr_ok  = count_ok && reassemble(cap.words, 0, 5)  == caller
                                && reassemble(cap.words, 5, 5)  == caller;
    bool tis_addr_ok = count_ok && cap.words[10].type == PreambleType::TIS
                                && reassemble(cap.words, 10, 5) == self;

    std::cout << "  response = TO caller ×2 + TIS self (" << cap.size() << " words): "
              << (count_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  response re-addresses whole 15-char caller: "
              << (to_addr_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  response announces whole 15-char self: "
              << (tis_addr_ok ? "PASS" : "FAIL") << "\n";

    return caller_ok && count_ok && to_addr_ok && tis_addr_ok;
}

// Regression: a CALLER must reassemble the responder's multi-word address from
// the response conclusion (TIS + DATA/REP) WITHOUT being polluted by stale
// handshake-collecting flags left over from a previous incoming call.
//
// Scenario: DL3HC was the called station in a prior (aborted) handshake — this
// leaves hs_conclusion_rcvd = true (cleared only on HANDSHAKE entry). DL3HC then
// places its own outbound call to DF3SR. The response addresses DL3HC: the own-TO
// DATA extension "HC@" appears BEFORE the responder's TIS. With the cross-state
// flag leak, those "HC" words were classified as caller-extension and appended to
// to_address ("HC"+"HC" = "HCHC"); the fix scopes `collecting` to the current
// state so to_address is built only from the responder's TIS conclusion.
bool test_caller_multiword_peer_not_polluted_by_stale_hs()
{
    std::cout << "\n[RX-MULTIWORD] Caller peer reassembly ignores stale handshake flags\n";

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"DL3HC", 0);
    sm.process_event(ALEEvent::START_SCAN);

    // 1) Prior incoming call from SAMUEL → enters HANDSHAKE, sets hs_conclusion_rcvd.
    feed_incoming_call(sm, /*self=*/"DL3HC", /*caller=*/"SAMUEL");
    bool stale_set = sm.is_hs_conclusion_rcvd();

    // 2) Abort the handshake → back to SCANNING; hs_conclusion_rcvd stays true.
    sm.process_event(ALEEvent::LINK_TIMEOUT);

    // 3) DL3HC now calls DF3SR. Drive LBT/TUNING from the current clock base.
    const uint32_t base = 1000u + 12u * Trw;
    sm.update(base);
    sm.initiate_call("DF3SR");
    sm.update(base + Twt);        // LBT → TUNING
    sm.update(base + Twt + Tt);   // TUNING → full TX sequence enqueued

    // 4) Drain the caller's TX (leading TO DF3SR ×2 + conclusion TIS DL3HC = 6 words) → LISTENING.
    for (int i = 0; i < 6; ++i) sm.on_word_complete();

    // 5) Responder's frame addresses DL3HC: TO:DL3 DATA:HC@ ×2 + TIS:DF3 DATA:SR@.
    uint32_t t = base + Twt + Tt;
    rx_at(sm, t += Trw, PreambleType::TO,   "DL3");
    rx_at(sm, t += Trw, PreambleType::DATA, "HC@");
    rx_at(sm, t += Trw, PreambleType::TO,   "DL3");
    rx_at(sm, t += Trw, PreambleType::DATA, "HC@");
    rx_at(sm, t += Trw, PreambleType::TIS,  "DF3");
    rx_at(sm, t += Trw, PreambleType::DATA, "SR@");

    bool peer_ok = sm.get_to_address() == "DF3SR";
    std::cout << "  prior handshake left hs_conclusion_rcvd: " << (stale_set ? "PASS" : "FAIL") << "\n";
    std::cout << "  caller peer = \"DF3SR\": " << (peer_ok ? "PASS" : "FAIL")
              << " (got \"" << sm.get_to_address() << "\")\n";

    return stale_set && peer_ok;
}

// ============================================================================
// MANUAL ACCEPT — post-link operator gate (LINKED_PENDING_OPERATOR)
// ============================================================================
// Manual accept no longer pauses the ALE handshake: the responder always
// auto-completes the 3-way handshake within Twr/Twrt and links, then the
// operator Accept/Reject decision is applied to the *already-established* link
// by ALEController (the SM's accept_call()/reject_call() are no-ops). These
// tests verify the SM side of that contract.

// Drive a 3-char caller through WAIT_CYCLE_END's conclusion settle. With manual
// accept now non-gating, the settle advances to SLOT_WAIT (not AWAIT_ACCEPT).
static uint32_t drive_to_conclusion_settle(ALEStateMachine& sm, const std::string& self,
                                           const std::string& caller)
{
    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;

    sm.set_require_explicit_accept(true, 10000);  // now a stored no-op
    feed_incoming_call(sm, self, caller);   // 1-word addresses: 5 words, slots 0..4

    const uint32_t t_last   = 1000u + 4u * Trw;   // time of the conclusion's only word
    const uint32_t t_settle = t_last + Tdrw + 1;
    sm.update(t_settle);                           // WAIT_CYCLE_END settle → SLOT_WAIT (auto)
    return t_settle;
}

// With manual-accept mode on, the responder must NOT pause in AWAIT_ACCEPT — it
// auto-advances through SLOT_WAIT/CHANNEL_CHECK/SENDING_RESPONSE and sends the
// normal accept response, exactly like auto-accept (MIL-STD-188-141B Twr/Twrt
// interoperability).
bool test_manual_accept_auto_completes_handshake()
{
    std::cout << "\n[ACCEPT] manual-accept mode does not gate the handshake\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
    sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw = ALETimingConstants::Trw_ms;
    uint32_t t = drive_to_conclusion_settle(sm, "JOE", "SAM");

    bool not_gated = sm.get_handshake_phase() != HandshakePhase::AWAIT_ACCEPT;
    std::cout << "  did not enter AWAIT_ACCEPT: " << (not_gated ? "PASS" : "FAIL") << "\n";

    // SLOT_WAIT (Tswt=0) → CHANNEL_CHECK (Tdrw=2×Trw LBT) → SENDING_RESPONSE.
    sm.update(t + 1);
    sm.update(t + 2);
    sm.update(t + 2 + 2 * Trw + 1);

    bool count_ok = cap.size() == 3;   // TO caller ×2 + TIS self, 1 word each
    bool addr_ok  = count_ok && cap.words[0].type == PreambleType::TO
                              && trim_ale_address(cap.words[0].address) == "SAM"
                              && cap.words[2].type == PreambleType::TIS
                              && trim_ale_address(cap.words[2].address) == "JOE";
    std::cout << "  auto accept response sent (3 words): " << (count_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  response addresses caller/self correctly: " << (addr_ok ? "PASS" : "FAIL") << "\n";

    return not_gated && count_ok && addr_ok;
}

// The SM's accept_call()/reject_call() are no-ops now (the post-link decision
// belongs to ALEController::accept_call()/reject_call()). Both return false and
// transmit nothing by themselves.
bool test_sm_accept_reject_are_noops()
{
    std::cout << "\n[ACCEPT] SM accept_call()/reject_call() are no-ops\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
    sm.process_event(ALEEvent::START_SCAN);

    drive_to_conclusion_settle(sm, "JOE", "SAM");  // lands in SLOT_WAIT, nothing sent yet

    bool accept_noop = !sm.accept_call();
    bool reject_noop = !sm.reject_call();
    std::cout << "  accept_call() is a no-op (false): " << (accept_noop ? "PASS" : "FAIL") << "\n";
    std::cout << "  reject_call() is a no-op (false): " << (reject_noop ? "PASS" : "FAIL") << "\n";

    bool nothing_sent = cap.empty();   // no TWAS / response triggered by the no-ops
    std::cout << "  no words transmitted by the no-ops: " << (nothing_sent ? "PASS" : "FAIL") << "\n";

    return accept_noop && reject_noop && nothing_sent;
}

// 3-char JOE accepts a call from a >3-char SAM.  The caller address must be
// reassembled from TIS + DATA across the conclusion.
bool test_rx_multiword_caller_address()
{
    std::cout << "\n[RX-MULTIWORD] Reassemble a >3-char caller address from TIS+DATA\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
    sm.process_event(ALEEvent::START_SCAN);

    feed_incoming_call(sm, /*self=*/"JOE", /*caller=*/"SAMUEL");

    bool got_concl = sm.is_hs_conclusion_rcvd();
    bool caller_ok = sm.get_caller_address() == "SAMUEL";

    std::cout << "  conclusion (TIS) collected: " << (got_concl ? "PASS" : "FAIL") << "\n";
    std::cout << "  caller address = \"SAMUEL\": " << (caller_ok ? "PASS" : "FAIL")
              << " (got \"" << sm.get_caller_address() << "\")\n";

    return got_concl && caller_ok;
}

// ============================================================================
// AC-LINK-002-002 — Response LBT: Tdrw = 2×Trw channel check before responding
//
// Verifies (REQ-LINK-006 / A.5.5.3.3 / Table A-XV "Tdrw = 784 ms"):
//   (1) After the call's conclusion settles, JOE enters CHANNEL_CHECK and must
//       wait the full Tdrw = 2×Trw = 784 ms before transmitting its response —
//       it does NOT transmit after only 1×Trw.  The LBT must span the spec's
//       detect-redundant-word period so a competing transmission is caught.
//   (2) Once Tdrw elapses with the channel clear, JOE sends its response frame.
//   (3) Any word received during the CHANNEL_CHECK window means the channel is
//       busy → JOE aborts the handshake and sends nothing (AC-LINK-019-3).
// ============================================================================
bool test_ac_link_002_002_response_lbt_two_trw()
{
    std::cout << "\n[AC-LINK-002-002] Response LBT waits Tdrw=2×Trw, aborts on busy channel\n";
    bool all_ok = true;

    const uint32_t Trw  = ALETimingConstants::Trw_ms;   // 392 ms
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;  // 784 ms = 2×Trw

    // ── Part 1: clear channel — LBT must span the full Tdrw window ───────────
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
        sm.process_event(ALEEvent::START_SCAN);
        feed_incoming_call(sm, /*self=*/"JOE", /*caller=*/"SAM");

        const uint32_t t_last = 1000u + 4u * Trw;        // last conclusion word
        sm.update(t_last + Tdrw + 1);                     // WAIT_CYCLE_END settle → SLOT_WAIT
        sm.update(t_last + Tdrw + 2);                     // SLOT_WAIT (Tswt=0) → CHANNEL_CHECK
        const uint32_t lbt0 = t_last + Tdrw + 2;         // CHANNEL_CHECK entry time

        // After only 1×Trw the LBT must NOT be over: still CHANNEL_CHECK, nothing sent.
        sm.update(lbt0 + Trw);
        const bool still_checking = sm.get_handshake_phase() == HandshakePhase::CHANNEL_CHECK
                                 && cap.empty();

        // After the full Tdrw the LBT clears → SENDING_RESPONSE with a frame.
        sm.update(lbt0 + Tdrw);
        const bool responded = sm.get_handshake_phase() == HandshakePhase::SENDING_RESPONSE
                            && !cap.empty();

        all_ok &= still_checking && responded;
        std::cout << "  no TX after 1×Trw (LBT < Tdrw):   " << (still_checking ? "PASS" : "FAIL") << "\n";
        std::cout << "  response sent after full Tdrw:     " << (responded ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 2: busy channel — a word during LBT aborts the response ─────────
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
        sm.process_event(ALEEvent::START_SCAN);
        feed_incoming_call(sm, /*self=*/"JOE", /*caller=*/"SAM");

        const uint32_t t_last = 1000u + 4u * Trw;
        sm.update(t_last + Tdrw + 1);                     // → SLOT_WAIT
        sm.update(t_last + Tdrw + 2);                     // → CHANNEL_CHECK
        const uint32_t lbt0 = t_last + Tdrw + 2;

        // A third station's word arrives mid-LBT → channel busy → abort.
        sm.update(lbt0 + Trw);
        sm.process_received_word(rx_word(PreambleType::TO, "XYZ"));

        const bool aborted = sm.get_state() != ALEState::HANDSHAKE;  // back to pre-link (SCANNING)
        const bool no_tx   = cap.empty();
        all_ok &= aborted && no_tx;
        std::cout << "  busy channel aborts handshake:     " << (aborted ? "PASS" : "FAIL")
                  << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
        std::cout << "  no response transmitted when busy: " << (no_tx ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-LINK-003-001 + NOTE 1 — Called station completes the 3-way handshake.
//
// Covers the JOE (called) side of A.5.5.3.4 + NOTE 1, which the existing tests
// did NOT exercise (they only drive SAM's ACK-sending side or inject
// HANDSHAKE_COMPLETE directly):
//   (A) After sending its response, JOE waits in WAIT_ACK and enters LINKED
//       when it reads a valid in-window ACK ("TO JOE" + "TIS SAM").
//   (B) NOTE 1 ping-pong prevention: JOE does NOT transmit a second response to
//       SAM's ACK — the ACK is consumed as the handshake's third frame.
//   (C) NOTE 1 late ACK: if no ACK arrives within JOE's Twr window, JOE aborts
//       to its pre-link state; a subsequent "TO JOE" is then treated as a NEW
//       individual call (re-enters HANDSHAKE), not as a continuation.
//
// Drives JOE entirely through the real RX/TX path (no HANDSHAKE_COMPLETE
// injection): feed_incoming_call → settle → SLOT_WAIT → CHANNEL_CHECK (Tdrw)
// → SENDING_RESPONSE (drained via on_word_complete) → WAIT_ACK.
// ============================================================================

// Drive a freshly-scanning JOE all the way to WAIT_ACK with its response sent.
// Returns the SM clock value at WAIT_ACK entry (== hs_ack_start_ms).
static uint32_t drive_joe_to_wait_ack(ALEStateMachine& sm)
{
    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;

    sm.process_event(ALEEvent::START_SCAN);
    feed_incoming_call(sm, /*self=*/"JOE", /*caller=*/"SAM");   // → HANDSHAKE/WAIT_CYCLE_END

    const uint32_t t_last = 1000u + 4u * Trw;                   // last conclusion word
    sm.update(t_last + Tdrw + 1);                               // settle → SLOT_WAIT
    sm.update(t_last + Tdrw + 2);                               // SLOT_WAIT → CHANNEL_CHECK
    const uint32_t lbt0 = t_last + Tdrw + 2;
    sm.update(lbt0 + Tdrw);                                     // CHANNEL_CHECK clear → SENDING_RESPONSE
    for (int i = 0; i < 3; ++i) sm.on_word_complete();         // drain TO SAM ×2 + TIS JOE → WAIT_ACK
    return lbt0 + Tdrw;                                         // == hs_ack_start_ms
}

bool test_called_station_ack_to_linked_and_note1()
{
    std::cout << "\n[AC-LINK-003-001 / NOTE 1] Called station: WAIT_ACK → LINKED, ping-pong, late ACK\n";
    bool all_ok = true;

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;

    // ── Part A+B: valid in-window ACK → LINKED, no second response ───────────
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
        const uint32_t ack0 = drive_joe_to_wait_ack(sm);

        const bool in_wait_ack = sm.get_handshake_phase() == HandshakePhase::WAIT_ACK;
        const bool response_sent = cap.size() == 3;   // TO SAM ×2 + TIS JOE
        cap.clear();                                   // watch for any further (ping-pong) TX

        // SAM's ACK arrives well within JOE's Twr window: "TO JOE" then "TIS SAM".
        sm.update(ack0 + 100);
        sm.process_received_word(rx_word(PreambleType::TO,  "JOE"));
        sm.update(ack0 + 100 + Trw);
        sm.process_received_word(rx_word(PreambleType::TIS, "SAM"));
        sm.update(ack0 + 100 + Trw + Tdrw + 1);        // ACK conclusion settle → LINKED

        const bool linked      = sm.get_state() == ALEState::LINKED;
        const bool no_pingpong = cap.empty();          // JOE sent nothing in response to the ACK

        // "...and set a wait-for-activity timeout Twa." (A.5.5.3.4 / AC-LINK-003-001)
        // Verify the default Twa, that activity defers termination, and that
        // sustained inactivity drops the link.
        const uint32_t t_link    = ack0 + 100 + Trw + Tdrw + 1;  // LINKED-entry clock
        const bool twa_default   = sm.get_timing_parameters().Twa_ms == 30000u;

        sm.update(t_link + 20000);      // 20 s into the link (< Twa)
        sm.on_link_activity();          // user-layer traffic resets the Twa timer
        sm.update(t_link + 40000);      // 40 s since link, but only 20 s since activity
        const bool kept_on_activity = sm.get_state() == ALEState::LINKED;

        sm.update(t_link + 40000 + 30000 + 1);  // > Twa of silence since the activity
        const bool dropped_on_idle  = sm.get_state() != ALEState::LINKED;

        all_ok &= in_wait_ack && response_sent && linked && no_pingpong
               && twa_default && kept_on_activity && dropped_on_idle;
        std::cout << "  JOE reached WAIT_ACK after sending response: "
                  << ((in_wait_ack && response_sent) ? "PASS" : "FAIL") << "\n";
        std::cout << "  valid in-window ACK → LINKED:                "
                  << (linked ? "PASS" : "FAIL")
                  << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
        std::cout << "  NOTE 1: no second response to ACK (no ping-pong): "
                  << (no_pingpong ? "PASS" : "FAIL") << "\n";
        std::cout << "  Twa default = 30000 ms:                      "
                  << (twa_default ? "PASS" : "FAIL") << "\n";
        std::cout << "  activity defers Twa termination:             "
                  << (kept_on_activity ? "PASS" : "FAIL") << "\n";
        std::cout << "  Twa inactivity drops the link:               "
                  << (dropped_on_idle ? "PASS" : "FAIL") << "\n";
    }

    // ── Part C: no/late ACK → abort to pre-link; late \"TO JOE\" = new call ───
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, /*self=*/"JOE", 0);
        const uint32_t ack0 = drive_joe_to_wait_ack(sm);
        cap.clear();

        // No ACK within JOE's Twr window (Twr_slow + Tdrw + (Tdrw−Tlww) = 2091 ms).
        const uint32_t twr_window = ale::Twr_slow_int
                                  + static_cast<uint32_t>(ale::Tdrw_ms)
                                  + (ALETimingConstants::Tdrw_ms - ALETimingConstants::Tlww_ms);
        sm.update(ack0 + twr_window + 1);              // WAIT_ACK timeout → pre-link (SCANNING)
        const bool aborted = sm.get_state() != ALEState::HANDSHAKE;

        // A late "TO JOE" (what NOTE 1 calls a late ACK) is now a NEW call.
        sm.update(ack0 + twr_window + 100);
        sm.process_received_word(rx_word(PreambleType::TO, "JOE"));
        const bool new_call = sm.get_state() == ALEState::HANDSHAKE;

        all_ok &= aborted && new_call;
        std::cout << "  no ACK within Twr → abort to pre-link state: "
                  << (aborted ? "PASS" : "FAIL")
                  << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
        std::cout << "  NOTE 1: late \"TO JOE\" treated as a new call: "
                  << (new_call ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-WORD-003-1 — TO for individual calls
// REQ-WORD-003: "TO shall be used in individual call protocols for single
// stations, and in net call protocols for multiple net member stations."
// ============================================================================

bool test_ac_003_1_individual_scanning_uses_to()
{
    std::cout << "\n[AC-WORD-003-1] TO in individual-call scanning phase\n";
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/1);
    sm.initiate_call("N1XYZ");
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms); // LBT → TUNING
    sm.update(T_TX);                        // TUNING → SCANNING_CALL
    sm.update(T_TX);                        // slot 0: first scanning word fires

    bool ok = !cap.empty() && cap.words[0].type == PreambleType::TO;
    std::cout << "  scanning first word = TO: " << (ok ? "PASS" : "FAIL");
    if (!cap.empty())
        std::cout << " (got " << WordParser::word_type_name(cap.words[0].type) << ")";
    std::cout << "\n";
    return ok;
}

// REQ-WORD-003: TO used in net call protocols (word-level: TO encodes a net
// address identically to a station address — same Basic 38 character set).
bool test_ac_003_1_to_encodes_net_address()
{
    std::cout << "\n[AC-WORD-003-1] TO word type encodes net address (word level)\n";

    const char net3[3] = { 'N', 'E', 'T' };
    uint32_t payload = WordParser::encode_ascii(net3, PreambleType::TO);
    bool enc_ok = (payload != 0xFFFFFFFF);

    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, PreambleType::TO, decoded);
    bool match = dec_ok && strncmp(decoded, "NET", 3) == 0;

    bool pass = enc_ok && match;
    std::cout << "  encode/decode \"NET\" as TO: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    return pass;
}

// ============================================================================
// AC-WORD-003-2 — TO word contains the first three characters of the address
// ============================================================================

bool test_ac_003_2_to_first_three_chars()
{
    std::cout << "\n[AC-WORD-003-2] TO word = first 3 chars of the address\n";

    struct Case { const char* addr; const char expected[4]; };
    const Case cases[] = {
        { "N1XYZ",       "N1X" },
        { "NET123",      "NET" },
        { "MIAMI",       "MIA" },
        { "ABC",         "ABC" },
        { "W1ABCDEFG",   "W1A" },
    };

    bool all_pass = true;
    for (const auto& c : cases) {
        // Build TO word using the first 3 chars of the address
        char first3[3] = { c.addr[0], c.addr[1], c.addr[2] };
        uint32_t payload = WordParser::encode_ascii(first3, PreambleType::TO);
        char decoded[4] = {};
        bool dec_ok = WordParser::decode_ascii(payload, PreambleType::TO, decoded);
        bool match = dec_ok && strncmp(decoded, c.expected, 3) == 0;
        all_pass &= match;
        std::cout << "  addr=\"" << c.addr << "\" → TO[0..2]=\"" << c.expected
                  << "\": " << (match ? "PASS" : "FAIL");
        if (!match) std::cout << " (got \"" << decoded << "\")";
        std::cout << "\n";
    }

    // State-machine confirmation: scanning word carries first 3 chars
    {
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, "SAM", 1);
        sm.initiate_call("N1XYZ");
        const uint32_t T_TX2 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
        sm.update(ALETimingConstants::Twt_ms); // LBT → TUNING
        sm.update(T_TX2);                       // TUNING → SCANNING_CALL
        sm.update(T_TX2);                       // slot 0: first scanning word fires
        bool sm_ok = !cap.empty()
                  && cap.words[0].type == PreambleType::TO
                  && strncmp(cap.words[0].address, "N1X", 3) == 0;
        all_pass &= sm_ok;
        std::cout << "  SM scanning word addr = \"N1X\": " << (sm_ok ? "PASS" : "FAIL");
        if (!cap.empty())
            std::cout << " (got \"" << std::string(cap.words[0].address, 3) << "\")";
        std::cout << "\n";
    }
    return all_pass;
}

// ============================================================================
// AC-WORD-003-3 — Extended addresses continue with alternating DATA and REP
// REQ-WORD-003: "Extended addresses shall be contained in immediately
// following, alternating DATA and REP words."
// Sequence: TO, DATA, REP, DATA, REP (max 5 words = 15 chars)
// ============================================================================

bool test_ac_003_3_extended_address_data_rep_sequence()
{
    std::cout << "\n[AC-WORD-003-3] Extended addresses: TO, DATA, REP, DATA, REP\n";

    struct Case {
        const char* addr;
        size_t expected_word_count;
        PreambleType expected_types[5];
    };

    const Case cases[] = {
        // 3 chars: TO only (no extension)
        { "ABC",             1, { PreambleType::TO,
                                  PreambleType::UNKNOWN, PreambleType::UNKNOWN,
                                  PreambleType::UNKNOWN, PreambleType::UNKNOWN } },
        // 6 chars: TO + DATA
        { "K6KBCD",          2, { PreambleType::TO,   PreambleType::DATA,
                                  PreambleType::UNKNOWN, PreambleType::UNKNOWN,
                                  PreambleType::UNKNOWN } },
        // 9 chars: TO + DATA + REP
        { "CALLSIGNX",       3, { PreambleType::TO,   PreambleType::DATA,
                                  PreambleType::REP,
                                  PreambleType::UNKNOWN, PreambleType::UNKNOWN } },
        // 12 chars: TO + DATA + REP + DATA
        { "LONGERCALLXY",    4, { PreambleType::TO,   PreambleType::DATA,
                                  PreambleType::REP,   PreambleType::DATA,
                                  PreambleType::UNKNOWN } },
        // 15 chars: TO + DATA + REP + DATA + REP
        { "VERYLONGCALLSIG", 5, { PreambleType::TO,   PreambleType::DATA,
                                  PreambleType::REP,   PreambleType::DATA,
                                  PreambleType::REP } },
    };

    bool all_pass = true;
    const uint32_t T_TX3 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    const uint32_t Trw3  = ALETimingConstants::Trw_ms;

    for (const auto& c : cases) {
        // Drive leading call seq 1 via LBT+TUNING then one slot per word.
        WordCapture cap;
        ALEStateMachine sm = make_sm(cap, "SAM", 0);
        sm.initiate_call(c.addr);
        sm.update(ALETimingConstants::Twt_ms); // LBT → TUNING
        sm.update(T_TX3);                       // TUNING → LEADING_CALL
        for (size_t slot = 0; slot < c.expected_word_count; ++slot) {
            sm.update(T_TX3 + static_cast<uint32_t>(slot) * Trw3);
            sm.on_word_complete();
        }

        bool count_ok = (cap.size() >= c.expected_word_count);
        bool types_ok = true;
        for (size_t i = 0; i < c.expected_word_count && i < cap.size(); ++i) {
            if (cap.words[i].type != c.expected_types[i]) {
                types_ok = false;
                break;
            }
        }

        bool pass = count_ok && types_ok;
        all_pass &= pass;

        std::cout << "  addr=\"" << c.addr << "\" (len=" << strlen(c.addr)
                  << ") → " << c.expected_word_count << " word(s) [";
        for (size_t i = 0; i < c.expected_word_count; ++i) {
            if (i) std::cout << ",";
            std::cout << WordParser::word_type_name(c.expected_types[i]);
        }
        std::cout << "]: " << (pass ? "PASS" : "FAIL");
        if (!count_ok)
            std::cout << " (got " << cap.size() << " words)";
        if (count_ok && !types_ok) {
            std::cout << " (got [";
            for (size_t i = 0; i < cap.size() && i < 5; ++i) {
                if (i) std::cout << ",";
                std::cout << WordParser::word_type_name(cap.words[i].type);
            }
            std::cout << "])";
        }
        std::cout << "\n";
    }
    return all_pass;
}

// ============================================================================
// REQ-WORD-004 — TIS: conclusion word type is TIS, carries first 3 chars of
// calling station's address; extended via DATA/REP same as TO.
// ============================================================================

bool test_tis_conclusion_word_type()
{
    std::cout << "\n[REQ-WORD-004] TIS in conclusion phase\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"SAM", 0);
    sm.initiate_call("ABC");
    advance_to_tx_start(sm);
    // cap = leading TO "ABC" × 2 + conclusion TIS "SAM" (3 words total)

    bool has_tis = cap.size() == 3 && cap.words[2].type == PreambleType::TIS;
    std::cout << "  conclusion word = TIS: " << (has_tis ? "PASS" : "FAIL");
    if (cap.size() >= 3)
        std::cout << " (got " << WordParser::word_type_name(cap.words[2].type) << ")";
    std::cout << "\n";

    bool addr_ok = has_tis && strncmp(cap.words[2].address, "SAM", 3) == 0;
    std::cout << "  TIS address = \"SAM\": " << (addr_ok ? "PASS" : "FAIL");
    if (has_tis)
        std::cout << " (got \"" << std::string(cap.words[2].address, 3) << "\")";
    std::cout << "\n";

    return has_tis && addr_ok;
}

bool test_tis_extended_address()
{
    std::cout << "\n[REQ-WORD-004] TIS + DATA + REP for own address > 3 chars\n";

    // Self address "SAMUELB" (7 chars) → TIS "SAM", DATA "UEL", REP "B@@"
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAMUELB", 0);
    sm.initiate_call("ABC");
    advance_to_tx_start(sm);
    // cap = leading TO "ABC" × 2 + conclusion TIS+DATA+REP (5 words total)

    bool count_ok  = cap.size() == 5;
    bool type_tis  = count_ok && cap.words[2].type == PreambleType::TIS;
    bool type_data = count_ok && cap.words[3].type == PreambleType::DATA;
    bool type_rep  = count_ok && cap.words[4].type == PreambleType::REP;
    bool pass = type_tis && type_data && type_rep;

    std::cout << "  TIS+DATA+REP for \"SAMUELB\": " << (pass ? "PASS" : "FAIL");
    std::cout << " (" << cap.size() << " words [";
    for (size_t i = 2; i < cap.size() && i < 5; ++i) {
        if (i > 2) std::cout << ",";
        std::cout << WordParser::word_type_name(cap.words[i].type);
    }
    std::cout << "])\n";
    return pass;
}

// ============================================================================
// REQ-WORD-005 — TWAS: preamble 3 (PreambleType::TWAS), Basic 38, distinct from TIS
// TIS and TWAS must not be used in the same frame (different preamble bits).
// ============================================================================

bool test_twas_word_encoding()
{
    std::cout << "\n[REQ-WORD-005] TWAS (PreambleType::TWAS) encode/decode\n";

    const char chars[3] = { 'R', 'E', 'J' };
    uint32_t payload = WordParser::encode_ascii(chars, PreambleType::TWAS);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, PreambleType::TWAS, decoded);
    bool match = dec_ok && strncmp(decoded, "REJ", 3) == 0;

    bool pass = enc_ok && match;
    std::cout << "  encode/decode \"REJ\" as TWAS: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";

    // TWAS uses Basic 38 character set
    bool basic38 = WordParser::uses_basic38(PreambleType::TWAS);
    std::cout << "  TWAS uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    return pass && basic38;
}

bool test_tis_twas_different_preambles()
{
    std::cout << "\n[REQ-WORD-005] TIS and TWAS have distinct preamble bits\n";

    // Per Table A-II: TIS = preamble 5, TWAS = preamble 3
    uint8_t tis_bits  = static_cast<uint8_t>(PreambleType::TIS);
    uint8_t twas_bits = static_cast<uint8_t>(PreambleType::TWAS);
    bool distinct = (tis_bits != twas_bits);
    std::cout << "  TIS=" << static_cast<int>(tis_bits)
              << " TWAS=" << static_cast<int>(twas_bits)
              << " distinct: " << (distinct ? "PASS" : "FAIL") << "\n";

    // Verify preamble round-trip through bit layout
    auto make_word = [](PreambleType t, const char ch[3]) -> uint32_t {
        uint32_t pl = WordParser::encode_ascii(ch, t);
        return (static_cast<uint32_t>(t) << 21) | pl;
    };
    const char abc[3] = { 'A', 'B', 'C' };
    ALEWord tis_w, twas_w;
    WordParser p;
    p.parse_from_bits(make_word(PreambleType::TIS, abc),  tis_w);
    p.parse_from_bits(make_word(PreambleType::TWAS, abc), twas_w);
    bool tis_rt  = (tis_w.type  == PreambleType::TIS);
    bool twas_rt = (twas_w.type == PreambleType::TWAS);
    std::cout << "  TIS  round-trip: " << (tis_rt  ? "PASS" : "FAIL") << "\n";
    std::cout << "  TWAS round-trip: " << (twas_rt ? "PASS" : "FAIL") << "\n";

    return distinct && tis_rt && twas_rt;
}

// ============================================================================
// REQ-WORD-006 — THRU: preamble 1, Basic 38, used in group-call scanning only.
// No extended addresses (exactly 3 chars per THRU word).
// Group-call state machine path is not yet implemented; word-level tests only.
// ============================================================================

bool test_thru_word_encoding()
{
    std::cout << "\n[REQ-WORD-006] THRU encode/decode (word level)\n";

    // THRU carries exactly 3 Basic 38 chars (no extension words)
    const char chars[3] = { 'A', 'B', 'C' };
    uint32_t payload = WordParser::encode_ascii(chars, PreambleType::THRU);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, PreambleType::THRU, decoded);
    bool match = dec_ok && strncmp(decoded, "ABC", 3) == 0;
    bool basic38 = WordParser::uses_basic38(PreambleType::THRU);

    bool pass = enc_ok && match && basic38;
    std::cout << "  encode/decode \"ABC\" as THRU: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    std::cout << "  THRU uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    // Preamble must be 1 per Table A-II
    bool preamble_ok = (static_cast<uint8_t>(PreambleType::THRU) == 1);
    std::cout << "  THRU preamble == 1: " << (preamble_ok ? "PASS" : "FAIL") << "\n";

    return pass && preamble_ok;
}

bool test_thru_rejects_invalid_basic38()
{
    std::cout << "\n[REQ-WORD-006] THRU rejects non-Basic-38 characters\n";

    // Lowercase 'a' is not in Basic 38
    const char bad[3] = { 'a', 'b', 'c' };
    uint32_t payload = WordParser::encode_ascii(bad, PreambleType::THRU);
    bool rejected = (payload == 0xFFFFFFFF);
    std::cout << "  lowercase chars rejected: " << (rejected ? "PASS" : "FAIL") << "\n";
    return rejected;
}

// ============================================================================
// REQ-WORD-007 — FROM: preamble 4, Basic 38, same extension structure as TO.
// Optional identifier for the sending station.
// ============================================================================

bool test_from_word_encoding()
{
    std::cout << "\n[REQ-WORD-007] FROM encode/decode (word level)\n";

    const char chars[3] = { 'W', '1', 'A' };
    uint32_t payload = WordParser::encode_ascii(chars, PreambleType::FROM);
    bool enc_ok = (payload != 0xFFFFFFFF);
    char decoded[4] = {};
    bool dec_ok = WordParser::decode_ascii(payload, PreambleType::FROM, decoded);
    bool match = dec_ok && strncmp(decoded, "W1A", 3) == 0;
    bool basic38 = WordParser::uses_basic38(PreambleType::FROM);

    bool pass = enc_ok && match && basic38;
    std::cout << "  encode/decode \"W1A\" as FROM: " << (pass ? "PASS" : "FAIL");
    if (!match) std::cout << " (got \"" << decoded << "\")";
    std::cout << "\n";
    std::cout << "  FROM uses Basic 38: " << (basic38 ? "PASS" : "FAIL") << "\n";

    // Preamble must be 4 per Table A-II
    bool preamble_ok = (static_cast<uint8_t>(PreambleType::FROM) == 4);
    std::cout << "  FROM preamble == 4: " << (preamble_ok ? "PASS" : "FAIL") << "\n";

    return pass && preamble_ok;
}

bool test_from_extended_address_uses_data_rep()
{
    std::cout << "\n[REQ-WORD-007] FROM extended address uses DATA/REP (same as TO)\n";

    // FROM with a 6-char address: FROM "ABC" + DATA "DEF"
    WordParser p;
    auto make_word = [](PreambleType t, const char ch[3]) -> uint32_t {
        uint32_t pl = WordParser::encode_ascii(ch, t);
        return (static_cast<uint32_t>(t) << 21) | pl;
    };

    const char abc[3] = { 'A', 'B', 'C' };
    const char def_[3] = { 'D', 'E', 'F' };

    ALEWord from_w, data_w;
    p.parse_from_bits(make_word(PreambleType::FROM, abc),  from_w);
    p.parse_from_bits(make_word(PreambleType::DATA, def_), data_w);

    bool from_ok = (from_w.type == PreambleType::FROM) && strncmp(from_w.address, "ABC", 3) == 0;
    bool data_ok = (data_w.type == PreambleType::DATA) && strncmp(data_w.address, "DEF", 3) == 0;

    std::cout << "  FROM \"ABC\": " << (from_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  DATA \"DEF\": " << (data_ok ? "PASS" : "FAIL") << "\n";
    return from_ok && data_ok;
}

// ============================================================================
// Additional: all five routing preambles use Basic 38 character set
// ============================================================================

bool test_all_routing_preambles_use_basic38()
{
    std::cout << "\n[WORD-002] All routing preambles use Basic 38 character set\n";

    const PreambleType routing_types[] = {
        PreambleType::TO, PreambleType::TIS, PreambleType::TWAS,
        PreambleType::THRU, PreambleType::FROM
    };

    bool all_pass = true;
    for (auto t : routing_types) {
        bool b38 = WordParser::uses_basic38(t);
        all_pass &= b38;
        std::cout << "  " << WordParser::word_type_name(t)
                  << " uses Basic 38: " << (b38 ? "PASS" : "FAIL") << "\n";
    }
    return all_pass;
}

// ============================================================================
// AC-WORD-006-8/9, AC-WORD-007-8/9 — THRU and FROM preamble values are
// reserved for future indirect/relay protocols and AQC-ALE (A.5.2.3.2.4/5).
// Spec compliance is assured by the correct preamble assignment per Table A-II;
// no additional runtime check is required beyond verifying the enum values.
// ============================================================================

bool test_thru_from_preamble_reserved()
{
    std::cout << "\n[AC-WORD-006-8/9, AC-WORD-007-8/9] THRU/FROM preamble values reserved\n";

    // Per MIL-STD-188-141B Table A-II:
    //   THRU = 1  (reserved for indirect addressing / relay / AQC-ALE)
    //   FROM = 4  (reserved for indirect addressing / relay / AQC-ALE)
    bool thru_is_1 = (static_cast<uint8_t>(PreambleType::THRU) == 1);
    bool from_is_4 = (static_cast<uint8_t>(PreambleType::FROM) == 4);
    std::cout << "  THRU preamble == 1 (relay/AQC reserved): "
              << (thru_is_1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  FROM preamble == 4 (relay/AQC reserved): "
              << (from_is_4 ? "PASS" : "FAIL") << "\n";

    // Verify round-trip preserves the reserved values
    const char abc[3] = {'A','B','C'};
    ALEWord tw = WordParser::make_word(PreambleType::THRU, abc);
    ALEWord fw = WordParser::make_word(PreambleType::FROM, abc);
    bool thru_rt = tw.valid && (tw.type == PreambleType::THRU);
    bool from_rt = fw.valid && (fw.type == PreambleType::FROM);
    std::cout << "  THRU round-trip: " << (thru_rt ? "PASS" : "FAIL") << "\n";
    std::cout << "  FROM round-trip: " << (from_rt ? "PASS" : "FAIL") << "\n";

    return thru_is_1 && from_is_4 && thru_rt && from_rt;
}

// ============================================================================
// AC-WORD-007-4 — FROM appears at most once per ALE frame (A.5.2.3.2.5)
// ============================================================================

bool test_from_count_valid()
{
    std::cout << "\n[AC-WORD-007-4] FROM appears at most once per ALE frame\n";

    const char sam[3] = {'S','A','M'};
    const char cmd[3] = {'C','M','D'};

    // No FROM → valid
    std::vector<ALEWord> seq_none = {
        WordParser::make_word(PreambleType::TO,  sam),
        WordParser::make_word(PreambleType::TIS, sam),
    };
    bool v1 = FrameValidator::from_count_valid(seq_none);
    std::cout << "  no FROM: " << (v1 ? "PASS" : "FAIL") << "\n";

    // Exactly one FROM → valid
    std::vector<ALEWord> seq_one = {
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::CMD,  cmd),
    };
    bool v2 = FrameValidator::from_count_valid(seq_one);
    std::cout << "  one FROM: " << (v2 ? "PASS" : "FAIL") << "\n";

    // Two FROM words → invalid
    std::vector<ALEWord> seq_two = {
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::CMD,  cmd),
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::CMD,  cmd),
    };
    bool v3 = !FrameValidator::from_count_valid(seq_two);
    std::cout << "  two FROM rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3;
}

// ============================================================================
// AC-WORD-007-5/7 — FROM appears only immediately before CMD (A.5.2.3.2.5).
// Conformant systems ignore FROM words not in this position.
// ============================================================================

bool test_from_precedes_cmd_only()
{
    std::cout << "\n[AC-WORD-007-5/7] FROM appears only immediately before CMD\n";

    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    const char cmd[3] = {'C','M','D'};

    // FROM directly before CMD → valid
    std::vector<ALEWord> seq_direct = {
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::CMD,  cmd),
    };
    bool v1 = FrameValidator::from_precedes_cmd_only(seq_direct);
    std::cout << "  FROM, CMD: " << (v1 ? "PASS" : "FAIL") << "\n";

    // FROM + DATA address extension then CMD → valid
    std::vector<ALEWord> seq_ext = {
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::DATA, uel),
        WordParser::make_word(PreambleType::CMD,  cmd),
    };
    bool v2 = FrameValidator::from_precedes_cmd_only(seq_ext);
    std::cout << "  FROM, DATA, CMD: " << (v2 ? "PASS" : "FAIL") << "\n";

    // FROM followed by TIS instead of CMD → invalid
    std::vector<ALEWord> seq_no_cmd = {
        WordParser::make_word(PreambleType::FROM, sam),
        WordParser::make_word(PreambleType::TIS,  sam),
    };
    bool v3 = !FrameValidator::from_precedes_cmd_only(seq_no_cmd);
    std::cout << "  FROM, TIS (no CMD) rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // FROM at end of sequence (no following word) → invalid
    std::vector<ALEWord> seq_orphan = {
        WordParser::make_word(PreambleType::TO,   sam),
        WordParser::make_word(PreambleType::FROM, sam),
    };
    bool v4 = !FrameValidator::from_precedes_cmd_only(seq_orphan);
    std::cout << "  orphan FROM rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // No FROM → valid (vacuously true)
    std::vector<ALEWord> seq_no_from = {
        WordParser::make_word(PreambleType::TO,  sam),
        WordParser::make_word(PreambleType::TIS, sam),
    };
    bool v5 = FrameValidator::from_precedes_cmd_only(seq_no_from);
    std::cout << "  no FROM: " << (v5 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5;
}

// ============================================================================
// AC-WORD-006-1/7 — THRU only in scanning section (A.5.2.3.2.4).
// Conformant systems ignore calls that use their address in THRU outside scanning.
// ============================================================================

bool test_thru_in_scanning_only()
{
    std::cout << "\n[AC-WORD-006-1/7] THRU only in scanning section\n";

    const char abc[3] = {'A','B','C'};
    const char xyz[3] = {'X','Y','Z'};
    const char sam[3] = {'S','A','M'};

    // THRU/REP before any leading/conclusion word → valid
    std::vector<ALEWord> seq_valid = {
        WordParser::make_word(PreambleType::THRU, abc),
        WordParser::make_word(PreambleType::REP,  abc),
        WordParser::make_word(PreambleType::TO,   abc),
    };
    bool v1 = FrameValidator::thru_in_scanning_section_only(seq_valid);
    std::cout << "  THRU, REP, TO: " << (v1 ? "PASS" : "FAIL") << "\n";

    // THRU after TO → invalid (outside scanning section)
    std::vector<ALEWord> seq_after_to = {
        WordParser::make_word(PreambleType::TO,   abc),
        WordParser::make_word(PreambleType::THRU, abc),
    };
    bool v2 = !FrameValidator::thru_in_scanning_section_only(seq_after_to);
    std::cout << "  THRU after TO rejected: " << (v2 ? "PASS" : "FAIL") << "\n";

    // THRU after TIS → invalid
    std::vector<ALEWord> seq_after_tis = {
        WordParser::make_word(PreambleType::TIS,  sam),
        WordParser::make_word(PreambleType::THRU, abc),
    };
    bool v3 = !FrameValidator::thru_in_scanning_section_only(seq_after_tis);
    std::cout << "  THRU after TIS rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // THRU after TWAS → invalid
    std::vector<ALEWord> seq_after_tws = {
        WordParser::make_word(PreambleType::TWAS,  sam),
        WordParser::make_word(PreambleType::THRU, abc),
    };
    bool v4 = !FrameValidator::thru_in_scanning_section_only(seq_after_tws);
    std::cout << "  THRU after TWAS rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // Scanning only (no leading/conclusion) → valid
    std::vector<ALEWord> seq_scan_only = {
        WordParser::make_word(PreambleType::THRU, abc),
        WordParser::make_word(PreambleType::REP,  abc),
        WordParser::make_word(PreambleType::THRU, xyz),
        WordParser::make_word(PreambleType::REP,  xyz),
    };
    bool v5 = FrameValidator::thru_in_scanning_section_only(seq_scan_only);
    std::cout << "  scanning only (THRU, REP pairs): " << (v5 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5;
}

// ============================================================================
// AC-WORD-006-2 — THRU and REP alternate in scanning section (A.5.2.3.2.4).
// ============================================================================

bool test_thru_rep_alternates()
{
    std::cout << "\n[AC-WORD-006-2] THRU and REP alternate in scanning section\n";

    const char abc[3] = {'A','B','C'};
    const char xyz[3] = {'X','Y','Z'};

    // One complete pair → valid
    bool v1 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, abc),
        WordParser::make_word(PreambleType::REP,  abc),
    });
    std::cout << "  THRU, REP: " << (v1 ? "PASS" : "FAIL") << "\n";

    // Two complete pairs → valid
    bool v2 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, abc),
        WordParser::make_word(PreambleType::REP,  abc),
        WordParser::make_word(PreambleType::THRU, xyz),
        WordParser::make_word(PreambleType::REP,  xyz),
    });
    std::cout << "  THRU, REP, THRU, REP: " << (v2 ? "PASS" : "FAIL") << "\n";

    // Lone THRU without REP → invalid (incomplete pair)
    bool v3 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, abc),
    });
    std::cout << "  lone THRU rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

    // REP before THRU → invalid
    bool v4 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::REP,  abc),
        WordParser::make_word(PreambleType::THRU, abc),
    });
    std::cout << "  REP before THRU rejected: " << (v4 ? "PASS" : "FAIL") << "\n";

    // THRU, THRU → invalid (two THRU in a row)
    bool v5 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, abc),
        WordParser::make_word(PreambleType::THRU, xyz),
    });
    std::cout << "  THRU, THRU rejected: " << (v5 ? "PASS" : "FAIL") << "\n";

    // Empty → valid (no violation)
    bool v6 = FrameValidator::thru_rep_alternates({});
    std::cout << "  empty: " << (v6 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5 && v6;
}

// ============================================================================
// AC-WORD-006-4 — Group call has at most 5 different THRU targets (A.5.2.3.2.4).
// ============================================================================

bool test_group_call_max_5_targets()
{
    std::cout << "\n[AC-WORD-006-4] Group call has at most 5 different THRU targets\n";

    // Build scanning section with N distinct targets
    auto make_scan = [](std::initializer_list<const char*> addrs) {
        std::vector<ALEWord> words;
        for (const char* a : addrs) {
            const char ch[3] = {a[0], a[1], a[2]};
            words.push_back(WordParser::make_word(PreambleType::THRU, ch));
            words.push_back(WordParser::make_word(PreambleType::REP,  ch));
        }
        return words;
    };

    // 5 distinct targets → valid
    auto five = make_scan({"AA1", "BB2", "CC3", "DD4", "EE5"});
    bool v1 = FrameValidator::group_call_target_count_valid(five);
    std::cout << "  5 distinct targets: " << (v1 ? "PASS" : "FAIL") << "\n";

    // 6 distinct targets → invalid
    auto six = make_scan({"AA1", "BB2", "CC3", "DD4", "EE5", "FF6"});
    bool v2 = !FrameValidator::group_call_target_count_valid(six);
    std::cout << "  6 distinct targets rejected: " << (v2 ? "PASS" : "FAIL") << "\n";

    // 8 THRU words but only 3 distinct addresses (repeats) → valid
    auto repeat = make_scan({"AA1", "BB2", "CC3", "AA1"});
    bool v3 = FrameValidator::group_call_target_count_valid(repeat);
    std::cout << "  4 words, 3 distinct (1 repeat) accepted: " << (v3 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3;
}

// ============================================================================
// AC-WORD-002-002 — All five address preamble types correct
//
// Consolidated verification per MIL-STD-188-141B A.5.2.3.2 (REQ-WORD-003–007):
//   TO   (2) — Calling section (individual / net)
//   TIS  (5) — Conclusion section (protocol continuation / Accept-Sound)
//   TWAS (3) — Conclusion section (protocol termination / Reject-Sound)
//   THRU (1) — Scanning section of group calls only
//   FROM (4) — Quick ID: only immediately before CMD (Message section)
//
// New coverage added: TIS/TWAS mutual exclusion
//   (AC-WORD-004-5 / AC-WORD-005-5: must not both appear in the same frame)
// ============================================================================

bool test_ac_word_002_002_address_preambles_correct_frames()
{
    std::cout << "\n[AC-WORD-002-002] Address preambles TO/TIS/TWAS/THRU/FROM — correct frames\n";
    std::cout << "============================================================================\n";
    bool all_pass = true;

    // 1. All five types are encodable / decodable with Basic 38 characters
    {
        struct Case { PreambleType type; const char* name; char c1, c2, c3; };
        const Case types[] = {
            { PreambleType::TO,   "TO",   'W', '1', 'A' },
            { PreambleType::TIS,  "TIS",  'S', 'A', 'M' },
            { PreambleType::TWAS, "TWAS", 'R', 'E', 'J' },
            { PreambleType::THRU, "THRU", 'A', 'B', 'C' },
            { PreambleType::FROM, "FROM", 'K', '6', 'K' },
        };
        bool enc_dec_pass = true;
        for (const auto& c : types) {
            const char chars[3] = { c.c1, c.c2, c.c3 };
            uint32_t payload = WordParser::encode_ascii(chars, c.type);
            char decoded[4] = {};
            bool ok = (payload != 0xFFFFFFFF)
                   && WordParser::decode_ascii(payload, c.type, decoded)
                   && decoded[0] == c.c1 && decoded[1] == c.c2 && decoded[2] == c.c3;
            if (!ok) enc_dec_pass = false;
            std::cout << "  encode/decode " << c.name << " \"" << c.c1 << c.c2 << c.c3
                      << "\": " << (ok ? "PASS" : "FAIL") << "\n";
        }
        all_pass &= enc_dec_pass;
    }

    // 2. TO used in Calling section (frame-level): validator allows TO before CMD
    {
        const char sam[3] = {'S','A','M'};
        const char cmd[3] = {'C','M','D'};
        std::vector<ALEWord> calling_frame = {
            WordParser::make_word(PreambleType::TO,  sam),
            WordParser::make_word(PreambleType::CMD, cmd),
        };
        bool ok = FrameValidator::cmd_not_before_address_section(calling_frame)
               && FrameValidator::first_cmd_begins_message_section(calling_frame);
        std::cout << "  TO in Calling section (TO→CMD valid): " << (ok ? "PASS" : "FAIL") << "\n";
        all_pass &= ok;
    }

    // 3. TIS used in Conclusion section: frame with TO→TIS is structurally valid
    {
        const char joe[3] = {'J','O','E'};
        const char sam[3] = {'S','A','M'};
        std::vector<ALEWord> accept_frame = {
            WordParser::make_word(PreambleType::TO,  joe),
            WordParser::make_word(PreambleType::TIS, sam),
        };
        bool ok = FrameValidator::thru_in_scanning_section_only(accept_frame)
               && FrameValidator::no_consecutive_same_preamble(accept_frame);
        std::cout << "  TIS in Conclusion section (TO→TIS valid): " << (ok ? "PASS" : "FAIL") << "\n";
        all_pass &= ok;
    }

    // 4. TWAS used in Conclusion section: frame with TO→TWAS is structurally valid
    {
        const char joe[3] = {'J','O','E'};
        const char sam[3] = {'S','A','M'};
        std::vector<ALEWord> reject_frame = {
            WordParser::make_word(PreambleType::TO,   joe),
            WordParser::make_word(PreambleType::TWAS, sam),
        };
        bool ok = FrameValidator::thru_in_scanning_section_only(reject_frame)
               && FrameValidator::no_consecutive_same_preamble(reject_frame);
        std::cout << "  TWAS in Conclusion section (TO→TWAS valid): " << (ok ? "PASS" : "FAIL") << "\n";
        all_pass &= ok;
    }

    // 5. TIS/TWAS mutually exclusive (AC-WORD-004-5 / AC-WORD-005-5)
    {
        const char sam[3] = {'S','A','M'};
        const char joe[3] = {'J','O','E'};

        // Valid: TIS only
        std::vector<ALEWord> tis_only = {
            WordParser::make_word(PreambleType::TO,  joe),
            WordParser::make_word(PreambleType::TIS, sam),
        };
        bool v1 = FrameValidator::tis_twas_mutually_exclusive(tis_only);
        std::cout << "  TIS only — mutually exclusive: " << (v1 ? "PASS" : "FAIL") << "\n";

        // Valid: TWAS only
        std::vector<ALEWord> twas_only = {
            WordParser::make_word(PreambleType::TO,   joe),
            WordParser::make_word(PreambleType::TWAS, sam),
        };
        bool v2 = FrameValidator::tis_twas_mutually_exclusive(twas_only);
        std::cout << "  TWAS only — mutually exclusive: " << (v2 ? "PASS" : "FAIL") << "\n";

        // Invalid: both TIS and TWAS in the same frame
        std::vector<ALEWord> both = {
            WordParser::make_word(PreambleType::TO,   joe),
            WordParser::make_word(PreambleType::TIS,  sam),
            WordParser::make_word(PreambleType::TWAS, sam),
        };
        bool v3 = !FrameValidator::tis_twas_mutually_exclusive(both);
        std::cout << "  TIS+TWAS in same frame rejected: " << (v3 ? "PASS" : "FAIL") << "\n";

        // Valid: neither TIS nor TWAS (e.g. scanning-only frame)
        std::vector<ALEWord> neither = {
            WordParser::make_word(PreambleType::THRU, joe),
            WordParser::make_word(PreambleType::REP,  joe),
        };
        bool v4 = FrameValidator::tis_twas_mutually_exclusive(neither);
        std::cout << "  Neither TIS nor TWAS — mutually exclusive: " << (v4 ? "PASS" : "FAIL") << "\n";

        all_pass &= (v1 && v2 && v3 && v4);
    }

    // 6. THRU used in Scanning section of group calls only (not after TO/TIS/TWAS)
    {
        const char abc[3] = {'A','B','C'};
        const char sam[3] = {'S','A','M'};
        std::vector<ALEWord> group_scan = {
            WordParser::make_word(PreambleType::THRU, abc),
            WordParser::make_word(PreambleType::REP,  abc),
            WordParser::make_word(PreambleType::TO,   sam),
            WordParser::make_word(PreambleType::TIS,  sam),
        };
        bool ok = FrameValidator::thru_in_scanning_section_only(group_scan)
               && FrameValidator::thru_rep_alternates({
                      WordParser::make_word(PreambleType::THRU, abc),
                      WordParser::make_word(PreambleType::REP,  abc),
                  });
        std::cout << "  THRU in Scanning section only (group call): " << (ok ? "PASS" : "FAIL") << "\n";
        all_pass &= ok;
    }

    // 7. FROM used as Quick ID (only immediately before CMD)
    {
        const char sam[3] = {'S','A','M'};
        const char cmd[3] = {'C','M','D'};
        std::vector<ALEWord> quick_id = {
            WordParser::make_word(PreambleType::FROM, sam),
            WordParser::make_word(PreambleType::CMD,  cmd),
        };
        bool ok = FrameValidator::from_count_valid(quick_id)
               && FrameValidator::from_precedes_cmd_only(quick_id);
        std::cout << "  FROM as Quick ID (FROM→CMD only): " << (ok ? "PASS" : "FAIL") << "\n";
        all_pass &= ok;
    }

    return all_pass;
}

// ============================================================================
// Star group calling (A.5.5.4.3) — N-member ad-hoc group call construction.
//
// scanning_call_group()/leading_call_group() build the TX side of a star
// group call (an ad-hoc list of >=1 individually-addressed members, not a
// fixed two-station pair). Verified against the spec's own worked example
// (BOB, EDGAR, SAM/SAMUEL — A.5.5.4.3.1/.2, Figures A-35/A-34) plus the
// de-dup and 5-unique-first-word cap rules.
// ============================================================================

bool test_scanning_call_group_three_members()
{
    std::cout << "\n[A.5.5.4.3.1] Scanning call group: 3 members rotate THRU/REP\n";

    auto seq = ALESequenceBuilder::scanning_call_group({"BOB", "EDGAR", "SAM"}, /*scan_channels=*/3);
    const auto& w = seq.words();

    struct Expect { PreambleType type; const char* addr; };
    const Expect expect[] = {
        {PreambleType::THRU, "BOB"}, {PreambleType::REP,  "EDG"}, {PreambleType::THRU, "SAM"},
        {PreambleType::REP,  "BOB"}, {PreambleType::THRU, "EDG"}, {PreambleType::REP,  "SAM"},
    };

    bool size_ok = (w.size() == 6);
    std::cout << "  6 words total: " << (size_ok ? "PASS" : "FAIL") << " (size=" << w.size() << ")\n";

    bool seq_ok = size_ok;
    for (size_t i = 0; size_ok && i < 6; ++i)
        seq_ok = seq_ok && (w[i].type == expect[i].type)
                         && (std::strncmp(w[i].address, expect[i].addr, 3) == 0);
    std::cout << "  matches spec rotation (Figure A-35): " << (seq_ok ? "PASS" : "FAIL") << "\n";

    return size_ok && seq_ok;
}

bool test_scanning_call_group_dedup()
{
    std::cout << "\n[A.5.5.4.3.1] Scanning call group: duplicate first words collapsed\n";

    // BOB and BOBCAT share the first word "BOB" -> only 2 unique first words (BOB, SAM).
    auto seq = ALESequenceBuilder::scanning_call_group({"BOB", "BOBCAT", "SAM"}, /*scan_channels=*/2);
    const auto& w = seq.words();

    bool size_ok = (w.size() == 4);  // scan_channels * 2, unaffected by dedup
    std::cout << "  4 words total (scan_channels*2): " << (size_ok ? "PASS" : "FAIL")
              << " (size=" << w.size() << ")\n";

    bool only_two_distinct = true;
    for (const auto& word : w) {
        std::string a(word.address);
        if (a != "BOB" && a != "SAM") only_two_distinct = false;
    }
    std::cout << "  only BOB/SAM rotate (BOBCAT's first word de-duplicated): "
              << (only_two_distinct ? "PASS" : "FAIL") << "\n";

    bool alternates = size_ok
        && w[0].type == PreambleType::THRU && w[1].type == PreambleType::REP
        && w[2].type == PreambleType::THRU && w[3].type == PreambleType::REP;
    std::cout << "  THRU/REP alternate starting with THRU: " << (alternates ? "PASS" : "FAIL") << "\n";

    return size_ok && only_two_distinct && alternates;
}

bool test_scanning_call_group_caps_at_5_unique()
{
    std::cout << "\n[AC-WORD-006-4] Scanning call group: caps at 5 unique first words\n";

    auto seq = ALESequenceBuilder::scanning_call_group(
        {"AA1", "BB2", "CC3", "DD4", "EE5", "FF6"}, /*scan_channels=*/5);
    const auto& w = seq.words();

    bool size_ok = (w.size() == 10);  // scan_channels * 2
    std::cout << "  10 words total: " << (size_ok ? "PASS" : "FAIL") << " (size=" << w.size() << ")\n";

    bool ff6_excluded = true;
    for (const auto& word : w)
        if (std::strncmp(word.address, "FF6", 3) == 0) ff6_excluded = false;
    std::cout << "  6th distinct member (FF6) excluded: " << (ff6_excluded ? "PASS" : "FAIL") << "\n";

    return size_ok && ff6_excluded;
}

bool test_scanning_call_group_single_word_fallback()
{
    std::cout << "\n[A.5.2.5.1] Scanning call group: single surviving word falls back to TO\n";

    // SAM and SAMUEL share the first word "SAM" -> de-dup collapses to 1 unique word,
    // which the flowchart's "IS THERE A SINGLE WORD REMAINING?" branch resolves to a
    // plain individual/net scanning call (TO), not a THRU/REP rotation.
    auto seq = ALESequenceBuilder::scanning_call_group({"SAM", "SAMUEL"}, /*scan_channels=*/2);
    const auto& w = seq.words();

    bool size_ok = (w.size() == 4);
    bool all_to  = size_ok;
    for (const auto& word : w)
        all_to = all_to && (word.type == PreambleType::TO)
                         && (std::strncmp(word.address, "SAM", 3) == 0);
    std::cout << "  falls back to plain TO scanning call (no THRU/REP): "
              << (all_to ? "PASS" : "FAIL") << "\n";

    return size_ok && all_to;
}

bool test_leading_call_group_three_members()
{
    std::cout << "\n[A.5.5.4.3.2] Leading call group: 3-member full addresses, TO anchor\n";

    auto seq = ALESequenceBuilder::leading_call_group({"BOB", "EDGAR", "SAMUEL"});
    const auto& w = seq.words();

    struct Expect { PreambleType type; const char* addr; };
    const Expect one_cycle[] = {
        {PreambleType::TO,   "BOB"}, {PreambleType::REP,  "EDG"}, {PreambleType::DATA, "AR@"},
        {PreambleType::TO,   "SAM"}, {PreambleType::DATA, "UEL"},
    };

    bool size_ok = (w.size() == 10);  // 5-word address sequence, sent twice
    std::cout << "  10 words (5-word address sequence sent twice): " << (size_ok ? "PASS" : "FAIL")
              << " (size=" << w.size() << ")\n";

    bool matches = size_ok;
    for (size_t cycle = 0; matches && cycle < 2; ++cycle)
        for (size_t i = 0; matches && i < 5; ++i) {
            const auto& got = w[cycle * 5 + i];
            matches = (got.type == one_cycle[i].type)
                    && (std::strncmp(got.address, one_cycle[i].addr, 3) == 0);
        }
    std::cout << "  matches spec example (Figure A-34): " << (matches ? "PASS" : "FAIL") << "\n";

    return size_ok && matches;
}

bool test_leading_call_group_no_thru_for_short_addresses()
{
    std::cout << "\n[AC-WORD-006-1/7 regression] Leading call group never uses THRU\n";

    // Both addresses <=3 chars: the old implementation anchored on THRU here,
    // which AC-WORD-006-1/7 forbids outside the scanning section, and which a
    // conformant receiver would discard the leading call for.
    auto seq = ALESequenceBuilder::leading_call_group({"BOB", "SAM"});
    const auto& w = seq.words();

    bool no_thru = true;
    for (const auto& word : w)
        if (word.type == PreambleType::THRU) no_thru = false;
    std::cout << "  no THRU words in leading call: " << (no_thru ? "PASS" : "FAIL") << "\n";

    bool expected = (w.size() == 4)
        && w[0].type == PreambleType::TO  && std::strncmp(w[0].address, "BOB", 3) == 0
        && w[1].type == PreambleType::REP && std::strncmp(w[1].address, "SAM", 3) == 0
        && w[2].type == PreambleType::TO  && std::strncmp(w[2].address, "BOB", 3) == 0
        && w[3].type == PreambleType::REP && std::strncmp(w[3].address, "SAM", 3) == 0;
    std::cout << "  [TO:BOB, REP:SAM] sent twice: " << (expected ? "PASS" : "FAIL") << "\n";

    return no_thru && expected;
}

bool test_initiate_group_call_rejects_empty()
{
    std::cout << "\n[Group call] initiate_group_call rejects an empty member list\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/1);
    bool rejected = !sm.initiate_group_call({});
    std::cout << "  empty member list rejected: " << (rejected ? "PASS" : "FAIL") << "\n";
    return rejected;
}

bool test_initiate_group_call_three_members_tx_sequence()
{
    std::cout << "\n[Group call] initiate_group_call with 3 members — full TX sequence\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/2);
    bool started = sm.initiate_group_call({"BOB", "EDGAR", "SAM2"});
    std::cout << "  call started: " << (started ? "PASS" : "FAIL") << "\n";

    advance_to_tx_start(sm);

    bool got_words = !cap.words.empty();
    std::cout << "  words enqueued: " << (got_words ? "PASS" : "FAIL")
              << " (count=" << cap.size() << ")\n";

    // Structural invariants rather than exact totals: the scanning section
    // (first scan_channels*2 words) must carry only THRU/REP, and THRU must
    // never appear again afterwards (AC-WORD-006-1/7).
    const size_t scan_words = 4;  // scan_channels(2) * 2
    bool scanning_thru_rep_only = true;
    for (size_t i = 0; i < scan_words && i < cap.size(); ++i)
        if (cap.words[i].type != PreambleType::THRU && cap.words[i].type != PreambleType::REP)
            scanning_thru_rep_only = false;
    std::cout << "  scanning section uses only THRU/REP: "
              << (scanning_thru_rep_only ? "PASS" : "FAIL") << "\n";

    bool no_thru_after_scanning = true;
    for (size_t i = scan_words; i < cap.size(); ++i)
        if (cap.words[i].type == PreambleType::THRU) no_thru_after_scanning = false;
    std::cout << "  no THRU after scanning section ends (AC-WORD-006-1/7): "
              << (no_thru_after_scanning ? "PASS" : "FAIL") << "\n";

    return started && got_words && scanning_thru_rep_only && no_thru_after_scanning;
}

// ============================================================================
// AC-GEN-009-002 — "nicht bereits in einem Link" guard:
// A new incoming individual call received while already LINKED must be
// silently ignored; the SM must stay LINKED and must not transmit a response.
// ============================================================================

bool test_individual_call_ignored_when_already_linked()
{
    std::cout << "\n[ALWAYS-ANSWER] Individual call while LINKED is silently ignored\n";

    // Arrange: drive SAM (caller) to LINKED state via normal 3-way handshake.
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, /*self=*/"SAM", /*scan_ch=*/0);
    sm.initiate_call("JOE");
    advance_to_tx_start(sm);
    for (int i = 0; i < 3; ++i) sm.on_word_complete(); // drain leading + conclusion

    const uint32_t t0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    // JOE's response: TO SAM + TIS JOE.
    sm.update(t0 + 100);
    sm.process_received_word(rx_word(PreambleType::TO,  "SAM"));
    sm.update(t0 + 200);
    sm.process_received_word(rx_word(PreambleType::TIS, "JOE"));

    // Conclusion settle → SENDING_ACK → drain ACK → LINKED.
    sm.update(t0 + 200 + ALETimingConstants::Tdrw_ms + 1);
    sm.update(t0 + 200 + ALETimingConstants::Tdrw_ms + 2);
    for (int i = 0; i < 3; ++i) sm.on_word_complete();

    bool linked = (sm.get_state() == ALEState::LINKED);
    std::cout << "  reached LINKED: " << (linked ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    // Act: a third party (OTH) calls SAM while SAM is already LINKED.
    cap.clear();
    const uint32_t t1 = t0 + 200 + ALETimingConstants::Tdrw_ms + 1000;
    feed_incoming_call(sm, /*self=*/"SAM", /*caller=*/"OTH", t1);

    // Advance past the conclusion-settle window to let any auto-response trigger.
    sm.update(t1 + 10u * ALETimingConstants::Trw_ms + ALETimingConstants::Tdrw_ms + 1u);

    bool still_linked = (sm.get_state() == ALEState::LINKED);
    bool no_response  = cap.empty();

    std::cout << "  SM stays in LINKED (incoming call ignored): "
              << (still_linked ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";
    std::cout << "  no response transmitted to new caller: "
              << (no_response ? "PASS" : "FAIL") << "\n";

    return linked && still_linked && no_response;
}

// ============================================================================
// T-07 — Caller-side link termination returns to the pre-link state.
//
// Regression: terminate_link() (CMD:TERMINATE) sends TO peer ×2 + TWAS self
// and must fire LINK_TERMINATED on the LAST sent word so the calling station
// leaves LINKED and returns to its pre-link state (here IDLE).  The previous
// "decrement-and-return" accounting in on_word_complete() waited for one
// frame-completion too many — that event never arrives, so the caller stayed
// stuck in LINKED while the (correctly-behaving) responder went back.
// ============================================================================

bool test_caller_terminate_returns_to_prelink_state()
{
    std::cout << "\n[T-07] Caller CMD:TERMINATE returns to pre-link state\n";

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/0);
    sm.initiate_call("JOE");            // IDLE → CALLING (pre_link_state_ = IDLE)
    advance_to_tx_start(sm);            // enqueue leading (TO JOE ×2) + conclusion (TIS SAM)

    // Drive the 3 TX words (LEADING ×2 + CONCLUSION) → LISTENING.
    for (int i = 0; i < 3; ++i) sm.on_word_complete();

    const uint32_t t0 = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    // JOE's response frame: "TO SAM" then "TIS JOE".
    sm.update(t0 + 100);
    sm.process_received_word(rx_word(PreambleType::TO,  "SAM"));
    sm.update(t0 + 200);
    sm.process_received_word(rx_word(PreambleType::TIS, "JOE"));

    // Conclusion settle (Tdrw = 2×Trw) → SENDING_ACK; build + drain ACK → LINKED.
    sm.update(t0 + 200 + ALETimingConstants::Tdrw_ms + 1);  // LISTENING(c) → SENDING_ACK
    sm.update(t0 + 200 + ALETimingConstants::Tdrw_ms + 2);  // SENDING_ACK → build_ack_words()
    for (int i = 0; i < 3; ++i) sm.on_word_complete();      // ACK drained → LINKED

    bool linked = (sm.get_state() == ALEState::LINKED);
    std::cout << "  reached LINKED: " << (linked ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    // Operator types CMD:TERMINATE → TO JOE ×2 + TWAS SAM, then LINK_TERMINATED.
    sm.terminate_link();
    for (int i = 0; i < 3; ++i) sm.on_word_complete();      // drain termination frame

    bool left_linked  = (sm.get_state() != ALEState::LINKED);
    bool back_to_idle = (sm.get_state() == ALEState::IDLE); // pre_link_state_ was IDLE
    std::cout << "  caller left LINKED after TERMINATE: "
              << (left_linked ? "PASS" : "FAIL") << "\n";
    std::cout << "  caller returned to pre-link state (IDLE): "
              << (back_to_idle ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(sm.get_state()) << ")\n";

    return linked && left_linked && back_to_idle;
}

// ============================================================================
// T-05 — SOUNDING opens the RX window on the LAST sent word.
//
// Regression: on_word_complete() must switch TRANSMITTING → LISTENING itself
// when the queue drains (decrement-then-check), not rely on a later update()
// tick.  The handle_sounding() fallback still covers the zero-word case
// (empty self address → no word, no on_word_complete) — see test_sounding in
// test_state_machine.cpp.
// ============================================================================

bool test_sounding_listen_window_opens_on_last_word()
{
    std::cout << "\n[T-05] SOUNDING opens RX window on last sent word\n";

    WordCapture cap;
    bool rx_open = false;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/0);
    sm.set_rx_enabled_callback([&rx_open](bool on){ rx_open = on; });

    sm.send_sounding();  // IDLE → SOUNDING (LBT phase; AC-SOUND-001-001)
    // Advance past LBT (Twt_ms) so SM transitions to TRANSMITTING and enqueues words.
    // Trs = 2×Ta(caller): conclusion "TIS SAM" sent twice → 2 words (AC-SOUND-003-002).
    sm.update(ALETimingConstants::Twt_ms + 1);

    bool transmitting  = (sm.get_sounding_phase() == SoundingPhase::TRANSMITTING);
    uint32_t n_words   = sm.get_words_pending();
    bool words_queued  = (n_words > 0);
    std::cout << "  TX phase, " << n_words << " word(s) enqueued: "
              << ((transmitting && words_queued) ? "PASS" : "FAIL")
              << " (words=" << n_words << ")\n";

    // The LAST on_word_complete() must switch TRANSMITTING → LISTENING without
    // relying on a later update() tick.  Drain all but the last word first.
    for (uint32_t i = 1; i < n_words; ++i)
        sm.on_word_complete();
    sm.on_word_complete();  // last word → LISTENING

    bool listening = (sm.get_sounding_phase() == SoundingPhase::LISTENING);
    std::cout << "  on_word_complete → LISTENING (no update() needed): "
              << (listening ? "PASS" : "FAIL") << "\n";
    std::cout << "  RX window opened: " << (rx_open ? "PASS" : "FAIL") << "\n";

    return transmitting && words_queued && listening && rx_open;
}

// ============================================================================
// AC-FRAME-002-001 — Scanning Call: Only TO words with first address chunk
// REQ-FRAME-002 / A.5.2.5.1 + A.5.5.3.1
// Module: Word/ale_sequence (ALESequenceBuilder::scanning_call())
// The state machine is a pure consumer — it delegates to the builder and
// the SCANNING_CALL phase is passive (audio layer drives via on_word_complete).
// ============================================================================

bool test_ac_frame_002_001_scanning_call_only_to_words()
{
    std::cout << "\n[AC-FRAME-002-001] Scanning call: nur TO-Worte, C×2 Slots, kein DATA/REP\n";

    // 3 scan channels → Tsc = 3 × 2 × Trw → 6 scanning words at the head of cap
    WordCapture cap;
    ALEStateMachine sm = make_sm(cap, "SAM", /*scan_ch=*/3);
    sm.initiate_call("N1XYZ");
    advance_to_tx_start(sm);  // fires the complete TX sequence into cap

    const uint32_t expected_scan_words = 3u * 2u;  // C × 2

    bool count_ok = cap.size() >= expected_scan_words;
    std::cout << "  cap.size()=" << cap.size()
              << " (>= " << expected_scan_words << "): "
              << (count_ok ? "PASS" : "FAIL") << "\n";
    if (!count_ok) return false;

    bool all_to  = true;
    bool no_ext  = true;
    bool addr_ok = true;
    for (uint32_t i = 0; i < expected_scan_words; ++i) {
        const auto& w = cap.words[i];
        if (w.type != PreambleType::TO) {
            all_to = false;
            std::cout << "  word[" << i << "] type="
                      << WordParser::word_type_name(w.type) << " (expected TO)\n";
        }
        if (w.type == PreambleType::DATA || w.type == PreambleType::REP)
            no_ext = false;
        if (strncmp(w.address, "N1X", 3) != 0) {
            addr_ok = false;
            std::cout << "  word[" << i << "] addr=\""
                      << std::string(w.address, 3) << "\" (expected \"N1X\")\n";
        }
    }

    std::cout << "  all scanning words TO: "   << (all_to  ? "PASS" : "FAIL") << "\n";
    std::cout << "  no DATA/REP in scanning: " << (no_ext  ? "PASS" : "FAIL") << "\n";
    std::cout << "  first 3 chars = \"N1X\": " << (addr_ok ? "PASS" : "FAIL") << "\n";

    return count_ok && all_to && no_ext && addr_ok;
}

// ============================================================================
// Main test runner
// ============================================================================

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEAT-WORD-002 — Address Words: TO/TIS/TWAS/THRU/FROM    ║\n";
    std::cout << "║  MIL-STD-188-141B A.5.2.3.2 Acceptance Tests             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) { ++pass_count; }
        else        { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    // AC-WORD-002-002 — All five address preamble types correct
    run("AC-WORD-002-002 address preambles TO/TIS/TWAS/THRU/FROM correct frames",
        test_ac_word_002_002_address_preambles_correct_frames());

    // AC-WORD-003: TO
    run("AC-WORD-003-1 individual scanning → TO",
        test_ac_003_1_individual_scanning_uses_to());
    run("AC-WORD-003-1 TO encodes net address",
        test_ac_003_1_to_encodes_net_address());
    run("AC-WORD-003-2 TO word = first 3 chars",
        test_ac_003_2_to_first_three_chars());
    run("AC-WORD-003-3 extended addresses: DATA/REP alternation",
        test_ac_003_3_extended_address_data_rep_sequence());

    // AC-FRAME-002-001
    run("AC-FRAME-002-001 scanning call: nur TO-Worte, C×2 Slots, kein DATA/REP",
        test_ac_frame_002_001_scanning_call_only_to_words());

    // REQ-WORD-004: TIS
    run("REQ-WORD-004 conclusion uses TIS",
        test_tis_conclusion_word_type());
    run("REQ-WORD-004 TIS extended address → TIS+DATA+REP",
        test_tis_extended_address());

    // REQ-WORD-005: TWAS
    run("REQ-WORD-005 TWAS encode/decode",
        test_twas_word_encoding());
    run("REQ-WORD-005 TIS and TWAS have distinct preambles",
        test_tis_twas_different_preambles());

    // REQ-WORD-006: THRU
    run("REQ-WORD-006 THRU encode/decode",
        test_thru_word_encoding());
    run("REQ-WORD-006 THRU rejects non-Basic-38 chars",
        test_thru_rejects_invalid_basic38());

    // REQ-WORD-007: FROM
    run("REQ-WORD-007 FROM encode/decode",
        test_from_word_encoding());
    run("REQ-WORD-007 FROM extended address uses DATA/REP",
        test_from_extended_address_uses_data_rep());

    // Cross-cutting
    run("All routing preambles use Basic 38",
        test_all_routing_preambles_use_basic38());

    // AC-WORD-006-8/9, AC-WORD-007-8/9
    run("THRU/FROM preamble values reserved for relay/AQC-ALE",
        test_thru_from_preamble_reserved());

    // AC-WORD-007-4
    run("AC-WORD-007-4 FROM at most once per frame",
        test_from_count_valid());

    // AC-WORD-007-5/7
    run("AC-WORD-007-5/7 FROM only immediately before CMD",
        test_from_precedes_cmd_only());

    // AC-WORD-006-1/7
    run("AC-WORD-006-1/7 THRU only in scanning section",
        test_thru_in_scanning_only());

    // AC-WORD-006-2
    run("AC-WORD-006-2 THRU/REP alternate in scanning section",
        test_thru_rep_alternates());

    // AC-WORD-006-4
    run("AC-WORD-006-4 group call max 5 THRU targets",
        test_group_call_max_5_targets());

    // A.5.5.4.3 — Star group calling (N-member ad-hoc group calls)
    run("A.5.5.4.3.1 scanning_call_group: 3 members rotate THRU/REP",
        test_scanning_call_group_three_members());
    run("A.5.5.4.3.1 scanning_call_group: duplicate first words collapsed",
        test_scanning_call_group_dedup());
    run("AC-WORD-006-4 scanning_call_group: caps at 5 unique first words",
        test_scanning_call_group_caps_at_5_unique());
    run("A.5.2.5.1 scanning_call_group: single surviving word falls back to TO",
        test_scanning_call_group_single_word_fallback());
    run("A.5.5.4.3.2 leading_call_group: 3-member full addresses, TO anchor",
        test_leading_call_group_three_members());
    run("AC-WORD-006-1/7 regression: leading_call_group never uses THRU",
        test_leading_call_group_no_thru_for_short_addresses());
    run("Group call: initiate_group_call rejects empty member list",
        test_initiate_group_call_rejects_empty());
    run("Group call: initiate_group_call 3-member full TX sequence",
        test_initiate_group_call_three_members_tx_sequence());

    // RX multi-word address regression (accepting calls > 3 chars)
    run("RX-MULTIWORD accept call to >3-char own address",
        test_rx_multiword_self_address());
    run("RX-MULTIWORD reassemble >3-char caller address",
        test_rx_multiword_caller_address());

    // Manual accept is a post-link operator gate (LINKED_PENDING_OPERATOR):
    // the handshake auto-completes; SM accept/reject are no-ops.
    run("ACCEPT manual-accept does not gate the handshake",
        test_manual_accept_auto_completes_handshake());
    run("ACCEPT SM accept_call()/reject_call() are no-ops",
        test_sm_accept_reject_are_noops());
    run("RX-MULTIWORD full accept path at 15-char addresses",
        test_rx_multiword_full_accept_15char());
    run("RX-MULTIWORD caller peer not polluted by stale handshake flags",
        test_caller_multiword_peer_not_polluted_by_stale_hs());

    // AC-LINK-002-002 — response LBT (Tdrw=2×Trw) + busy-channel abort
    run("AC-LINK-002-002 response LBT waits Tdrw=2×Trw, aborts on busy channel",
        test_ac_link_002_002_response_lbt_two_trw());

    // AC-LINK-003-001 + NOTE 1 — called station WAIT_ACK → LINKED, ping-pong, late ACK
    run("AC-LINK-003-001/NOTE1 called station: WAIT_ACK→LINKED, no ping-pong, late ACK = new call",
        test_called_station_ack_to_linked_and_note1());

    // AC-GEN-009-002 — "not already in a link" guard
    run("ALWAYS-ANSWER individual call while LINKED is silently ignored",
        test_individual_call_ignored_when_already_linked());

    // T-07 — caller-side termination regression
    run("T-07 caller CMD:TERMINATE returns to pre-link state",
        test_caller_terminate_returns_to_prelink_state());

    // T-05 — sounding RX-window-on-last-word regression
    run("T-05 SOUNDING opens RX window on last sent word",
        test_sounding_listen_window_opens_on_last_word());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count
              << "  Failed: " << std::setw(2) << fail_count
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main()
{
    return ale::run_all_tests();
}
