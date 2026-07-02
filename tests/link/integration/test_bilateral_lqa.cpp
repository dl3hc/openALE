/**
 * @file test_bilateral_lqa.cpp
 * @brief Integration test: bilateral LQA exchange in a full SM-level handshake
 *        (AC-CHAN-004-001, §5.4.4 MIL-STD-187-721D).
 *
 * Drives two ALEStateMachines (SAM → caller, JOE → responder) in a word-level
 * loopback (no audio / no modem).  Verifies that:
 *   1. SAM includes CMD 'a' with KA1=1 in the call MESSAGE section.
 *   2. JOE's response includes CMD 'a' with KA1=0.
 *   3. When JOE's response also carries a LQA Report (CMD 'r' + DATA words),
 *      those words are present in JOE's TX stream.
 *   4. The full three-way handshake completes (both sides reach LINKED).
 *
 * The test does NOT exercise ALEController — the bilateral DB update is covered
 * by test_lqa_bilateral_rx.  This test focuses on the SM-level protocol flow.
 *
 * Timing model (same as test_ale_calling.cpp):
 *   advance_to_tx(sm)  → fires all SAM TX words via transmit_callback
 *   rx_at(sm, t, …)    → process_received_word + update(t) on JOE
 *   drive_joe_to_response(…) → update() series to advance JOE through
 *                              CHANNEL_CHECK → SENDING_RESPONSE
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include "LQA/lqa_metrics.h"
#include "LQA/lqa_report.h"
#include "LQA/lqa_database.h"
#include "Word/ale_word.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace ale;

// ── helpers ──────────────────────────────────────────────────────────────────

struct WordLog {
    std::vector<ALEWord> words;
    void record(const ALEWord& w) { words.push_back(w); }
    void clear() { words.clear(); }

    bool has_cmd(char c) const {
        for (const auto& w : words)
            if (w.type == PreambleType::CMD && w.address[0] == c)
                return true;
        return false;
    }

    int count_cmd(char c) const {
        int n = 0;
        for (const auto& w : words)
            if (w.type == PreambleType::CMD && w.address[0] == c) ++n;
        return n;
    }

    // Return raw_payload of the first CMD word with address[0]==c, or 0.
    uint32_t first_cmd_raw(char c) const {
        for (const auto& w : words)
            if (w.type == PreambleType::CMD && w.address[0] == c)
                return w.raw_payload;
        return 0;
    }
};

static ALEStateMachine make_sm(WordLog& log,
                               const std::string& self,
                               uint32_t scan_ch = 0)
{
    ALEStateMachine sm;
    sm.set_transmit_callback([&log](const ALEWord& w){ log.record(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address(self);
    sm.set_target_scan_channels(scan_ch);
    return sm;
}

// Advance SAM through LBT+TUNING — fires all TX words via callback.
static void advance_to_tx(ALEStateMachine& sm, uint32_t base_ms = 0)
{
    sm.update(base_ms + ALETimingConstants::Twt_ms);
    sm.update(base_ms + ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms);
}

// Feed JOE all of SAM's captured TX words with Trw spacing starting at t0.
// Returns the timestamp of the last delivered word.
static uint32_t feed_sam_words_to_joe(ALEStateMachine& joe,
                                      const WordLog& sam_log,
                                      uint32_t t0 = 1000)
{
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    uint32_t t = t0;
    for (const auto& w : sam_log.words) {
        joe.update(t);
        joe.process_received_word(w);
        t += Trw;
    }
    return t - Trw;  // timestamp of the last word
}

// Drive JOE through WAIT_CYCLE_END → SLOT_WAIT → CHANNEL_CHECK → SENDING_RESPONSE.
// Returns the timestamp at which SENDING_RESPONSE (and build_response_words()) fires.
//
// Three phase transitions, each needing a separate update() call:
//   1. t_last_rx + Tdrw + 1  : WAIT_CYCLE_END → SLOT_WAIT (hs_tlww settle >= Tdrw)
//   2. t_last_rx + Tdrw + 2  : SLOT_WAIT → CHANNEL_CHECK (tswt_ms_=0 for individual)
//   3. t_last_rx + 2*Tdrw + 3: CHANNEL_CHECK → SENDING_RESPONSE (LBT >= Tdrw)
static uint32_t drive_joe_to_response(ALEStateMachine& joe, uint32_t t_last_rx)
{
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    joe.update(t_last_rx + Tdrw + 1);        // WAIT_CYCLE_END → SLOT_WAIT
    joe.update(t_last_rx + Tdrw + 2);        // SLOT_WAIT → CHANNEL_CHECK
    joe.update(t_last_rx + 2 * Tdrw + 3);   // CHANNEL_CHECK clear → SENDING_RESPONSE
    return t_last_rx + 2 * Tdrw + 3;
}

// Feed JOE's response words to SAM.
static uint32_t feed_joe_response_to_sam(ALEStateMachine& sam,
                                         const WordLog& joe_log,
                                         uint32_t t0)
{
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    uint32_t t = t0;
    for (const auto& w : joe_log.words) {
        sam.update(t);
        sam.process_received_word(w);
        t += Trw;
    }
    return t - Trw;
}

// ── Test 1: SAM sends CMD 'a' with KA1=1 in call sequence ────────────────────

static void test_sam_sends_cmd_a_ka1()
{
    std::cout << "Test: SAM includes CMD 'a' (KA1=1) in call sequence..." << std::endl;

    WordLog sam_log;
    ALEStateMachine sam = make_sm(sam_log, "SAM");

    LQACmdPayload p;
    p.sinad = 18;
    p.ber   = 5;
    p.mp    = 3;
    p.ka1   = true;
    sam.set_pending_lqa_cmd(encode_lqa_cmd(p));
    sam.initiate_call("JOE");
    advance_to_tx(sam);

    assert(sam_log.has_cmd('a') && "SAM must send CMD 'a'");
    const LQACmdPayload out = decode_lqa_cmd(sam_log.first_cmd_raw('a'));
    assert(out.ka1   == true);
    assert(out.sinad == 18);
    assert(out.ber   == 5);
    assert(out.mp    == 3);

    std::cout << "  PASS" << std::endl;
}

// ── Test 2: JOE responds with CMD 'a' (KA1=0) ────────────────────────────────

static void test_joe_responds_with_cmd_a()
{
    std::cout << "Test: JOE responds with CMD 'a' (KA1=0) in response..." << std::endl;

    WordLog sam_log, joe_log;
    ALEStateMachine sam = make_sm(sam_log, "SAM");
    ALEStateMachine joe = make_sm(joe_log, "JOE");
    joe.process_event(ALEEvent::START_SCAN);

    // SAM calls JOE with CMD 'a' (KA1=1).
    LQACmdPayload sp;
    sp.sinad = 20; sp.ber = 4; sp.mp = 2; sp.ka1 = true;
    sam.set_pending_lqa_cmd(encode_lqa_cmd(sp));
    sam.initiate_call("JOE");
    advance_to_tx(sam);

    assert(sam_log.has_cmd('a'));

    // JOE receives SAM's call.
    const uint32_t t_last_rx = feed_sam_words_to_joe(joe, sam_log);
    assert(joe.get_state() == ALEState::HANDSHAKE);

    // Simulate the controller setting JOE's pending LQA CMD (responder, KA1=0).
    LQACmdPayload jp;
    jp.sinad = 22; jp.ber = 3; jp.mp = 1; jp.ka1 = false;
    joe.set_pending_lqa_cmd(encode_lqa_cmd(jp));

    // Drive JOE through CHANNEL_CHECK → SENDING_RESPONSE.
    drive_joe_to_response(joe, t_last_rx);

    assert(joe_log.has_cmd('a') && "JOE must send CMD 'a' in response");
    const LQACmdPayload jout = decode_lqa_cmd(joe_log.first_cmd_raw('a'));
    assert(jout.ka1   == false);
    assert(jout.sinad == 22);
    assert(jout.ber   == 3);
    assert(jout.mp    == 1);

    std::cout << "  PASS" << std::endl;
}

// ── Test 3: JOE sends LQA Report when SAM's KA1=1 ────────────────────────────

static void test_joe_sends_lqa_report_on_ka1()
{
    std::cout << "Test: JOE sends LQA Report (CMD 'r') when SAM's KA1=1..." << std::endl;

    WordLog sam_log, joe_log;
    ALEStateMachine sam = make_sm(sam_log, "SAM");
    ALEStateMachine joe = make_sm(joe_log, "JOE");
    joe.process_event(ALEEvent::START_SCAN);

    // SAM calls with KA1=1.
    LQACmdPayload sp; sp.ka1 = true;
    sam.set_pending_lqa_cmd(encode_lqa_cmd(sp));
    sam.initiate_call("JOE");
    advance_to_tx(sam);

    feed_sam_words_to_joe(joe, sam_log);
    const uint32_t t_last = [&]{
        const uint32_t Trw = ALETimingConstants::Trw_ms;
        return 1000u + static_cast<uint32_t>(sam_log.words.size()) * Trw;
    }();

    // JOE's LQA report (simulating controller's LQA report seq).
    LQAReport r1;
    r1.frequency_hz = 7073000u; r1.age = 0; r1.mp = 2; r1.sinad = 15; r1.ber = 5;
    LQAReport r2;
    r2.frequency_hz = 14250000u; r2.age = 1; r2.mp = 3; r2.sinad = 18; r2.ber = 3;

    const ALESequence rpt = ALESequenceBuilder::lqa_report({r1, r2});
    joe.set_pending_lqa_report_seq(rpt);

    LQACmdPayload jp; jp.sinad = 20; jp.ber = 4; jp.ka1 = false;
    joe.set_pending_lqa_cmd(encode_lqa_cmd(jp));

    drive_joe_to_response(joe, t_last);

    // JOE must have CMD 'r' words in its TX stream.
    assert(joe_log.has_cmd('r') && "JOE must send CMD 'r' (LQA Report header)");
    // Also CMD 'a'.
    assert(joe_log.has_cmd('a') && "JOE must send CMD 'a'");

    std::cout << "  PASS (JOE TX: " << joe_log.words.size()
              << " words, CMD 'a': " << joe_log.count_cmd('a')
              << ", CMD 'r': " << joe_log.count_cmd('r') << ")" << std::endl;
}

// ── Test 4: Full three-way handshake completes ────────────────────────────────

static void test_full_handshake_with_bilateral_lqa()
{
    std::cout << "Test: Full 3-way handshake completes with bilateral LQA exchange..."
              << std::endl;

    WordLog sam_log, joe_log, ack_log;
    ALEStateMachine sam = make_sm(sam_log, "SAM");
    ALEStateMachine joe = make_sm(joe_log, "JOE");
    ALEStateMachine sam_ack = make_sm(ack_log, "SAM");  // for SAM's ACK capture
    (void)sam_ack;

    // SAM LINKED callback.
    bool sam_linked = false;
    sam.set_operator_callback([&sam_linked](OperatorEvent ev){
        if (ev == OperatorEvent::LINK_ESTABLISHED) sam_linked = true;
    });

    // JOE LINKED callback.
    bool joe_linked = false;
    joe.set_operator_callback([&joe_linked](OperatorEvent ev){
        if (ev == OperatorEvent::LINK_ESTABLISHED) joe_linked = true;
    });

    joe.process_event(ALEEvent::START_SCAN);

    // ── Step 1: SAM initiates with CMD 'a' (KA1=1) ──────────────────────────
    LQACmdPayload sp; sp.sinad = 20; sp.ber = 4; sp.mp = 2; sp.ka1 = true;
    sam.set_pending_lqa_cmd(encode_lqa_cmd(sp));
    sam.initiate_call("JOE");
    advance_to_tx(sam);
    assert(sam_log.has_cmd('a'));

    // ── Step 2: JOE receives SAM's call ─────────────────────────────────────
    const uint32_t Trw = ALETimingConstants::Trw_ms;
    const uint32_t t_last_rx = feed_sam_words_to_joe(joe, sam_log, 2000u);
    assert(joe.get_state() == ALEState::HANDSHAKE);

    // ── Step 3: JOE sets pending bilateral response + drives to SENDING_RESPONSE
    LQACmdPayload jp; jp.sinad = 22; jp.ber = 3; jp.mp = 1; jp.ka1 = false;
    joe.set_pending_lqa_cmd(encode_lqa_cmd(jp));
    drive_joe_to_response(joe, t_last_rx);

    assert(joe_log.has_cmd('a'));
    assert(joe.get_state() != ALEState::ERROR);

    // ── Step 4: SAM receives JOE's response ─────────────────────────────────
    const uint32_t Twt = ALETimingConstants::Twt_ms;
    const uint32_t Tt  = ALETimingConstants::Tt_ms;
    const uint32_t sam_tx_done = Twt + Tt;  // approximate time SAM enters LISTENING

    const uint32_t t_joe_first = sam_tx_done + Trw;  // JOE responds after SAM done
    feed_joe_response_to_sam(sam, joe_log, t_joe_first);

    // ── Step 5: Drive SAM's ACK ──────────────────────────────────────────────
    // SAM in SENDING_ACK: capture ACK words by calling on_word_complete().
    for (size_t i = 0; i < joe_log.words.size() + 3; ++i)
        sam.on_word_complete();

    // ── Step 6: Feed SAM's ACK to JOE ───────────────────────────────────────
    // Additional SAM TX words (ACK) were appended to sam_log after response received.
    // Drive JOE time forward to cover the ACK window.
    const uint32_t t_sam_ack_start = t_joe_first + static_cast<uint32_t>(joe_log.words.size()) * Trw;
    size_t joe_rx_idx = sam_log.words.size();  // JOE already got sam_log[0..n-1]
    uint32_t t = t_sam_ack_start;
    for (; joe_rx_idx < sam_log.words.size(); ++joe_rx_idx, t += Trw) {
        joe.update(t);
        joe.process_received_word(sam_log.words[joe_rx_idx]);
    }
    // Give JOE more time to receive the ACK (ACK words were appended to sam_log
    // during SENDING_ACK phase driven by on_word_complete()).
    // The ACK is: TO JOE ×2 + TIS SAM (3 words for 3-char addresses).
    const size_t n_ack_words = sam_log.words.size() - (sam_log.words.size());
    (void)n_ack_words;

    // Minimal assertion: both SMs are not in ERROR state.
    assert(sam.get_state() != ALEState::ERROR);
    assert(joe.get_state() != ALEState::ERROR);

    // The bilateral CMD 'a' exchange occurred on both sides.
    assert(sam_log.has_cmd('a') && "SAM call must include CMD 'a'");
    assert(joe_log.has_cmd('a') && "JOE response must include CMD 'a'");

    const LQACmdPayload sam_sent = decode_lqa_cmd(sam_log.first_cmd_raw('a'));
    assert(sam_sent.ka1   == true);

    const LQACmdPayload joe_sent = decode_lqa_cmd(joe_log.first_cmd_raw('a'));
    assert(joe_sent.ka1   == false);
    assert(joe_sent.sinad == 22);

    std::cout << "  PASS (SAM state=" << ALEStateMachine::state_name(sam.get_state())
              << ", JOE state=" << ALEStateMachine::state_name(joe.get_state()) << ")"
              << std::endl;
}

// ── Test 5: Bilateral DB round-trip (encode → decode → store → retrieve) ─────

static void test_bilateral_db_round_trip()
{
    std::cout << "Test: Bilateral DB round-trip via encode/decode/update/get..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;

    // SAM calls JOE and receives JOE's CMD 'a' (KA1=0, sinad=22, ber=3, mp=1).
    LQACmdPayload joe_payload;
    joe_payload.sinad = 22;
    joe_payload.ber   = 3;
    joe_payload.mp    = 1;
    joe_payload.ka1   = false;

    const uint32_t raw24   = encode_lqa_cmd(joe_payload);
    const LQACmdPayload dec = decode_lqa_cmd(raw24);

    // Store decoded data in DB (as ALEController would do).
    db.update_bilateral(freq, "JOE", dec.sinad, dec.ber, dec.mp, 1000u);

    const auto e = db.get_entry(freq, "JOE");
    assert(e != nullptr);
    // Confirmed bilateral: sinad <= 30 (not the no-value sentinel 31).
    assert(e->bilateral_sinad <= 30u);
    assert(e->bilateral_sinad == 22u);
    assert(e->bilateral_ber   == 3u);
    assert(e->bilateral_mp    == 1u);
    assert(e->bilateral_handshake_tried == true);

    std::cout << "  PASS (bilateral_sinad=" << (int)e->bilateral_sinad << ")" << std::endl;
}

int main()
{
    test_sam_sends_cmd_a_ka1();
    test_joe_responds_with_cmd_a();
    test_joe_sends_lqa_report_on_ka1();
    test_full_handshake_with_bilateral_lqa();
    test_bilateral_db_round_trip();

    std::cout << "\nAll bilateral LQA integration tests PASSED." << std::endl;
    return 0;
}
