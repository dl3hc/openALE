/**
 * @file test_lqa_bilateral_tx.cpp
 * @brief Unit tests for bilateral LQA TX path — CMD 'a' in call MESSAGE section
 *        (AC-CHAN-004-001, Table A-XIV).
 *
 * Tests that set_pending_lqa_cmd() + initiate_call() causes the SM to include
 * a CMD 'a' word with the correct KA1 flag and SINAD/BER/MP fields in the
 * enqueued call sequence.  Drives the SM in isolation (no audio, no modem).
 *
 * Timing model (same as test_ale_calling.cpp):
 *   update(Twt_ms)       LBT → TUNING
 *   update(Twt_ms+Tt_ms) TUNING → all TX words fired via transmit_callback
 */

#include "Protocol/Control/ale_state_machine.h"
#include "LQA/lqa_metrics.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace ale;

// ── helpers ──────────────────────────────────────────────────────────────────

struct WordCapture {
    std::vector<ALEWord> words;
    void record(const ALEWord& w) { words.push_back(w); }
};

static ALEStateMachine make_sm(WordCapture& cap,
                               const std::string& self = "SAM")
{
    ALEStateMachine sm;
    sm.set_transmit_callback([&cap](const ALEWord& w){ cap.record(w); });
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address(self);
    sm.set_target_scan_channels(0);  // skip scanning call — straight to LEADING_CALL
    return sm;
}

static void advance_to_tx(ALEStateMachine& sm)
{
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms);  // LBT → TUNING
    sm.update(T_TX);                         // TUNING → full sequence enqueued
}

// ── tests ─────────────────────────────────────────────────────────────────────

static void test_cmd_lqa_in_tx_with_ka1_true()
{
    std::cout << "Test: CMD 'a' with KA1=1 appears in TX sequence..." << std::endl;

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);

    LQACmdPayload p;
    p.sinad = 18;
    p.ber   = 5;
    p.mp    = 3;
    p.ka1   = true;
    const uint32_t raw24 = encode_lqa_cmd(p);

    // Must set pending BEFORE initiate_call() — initiate_call() snapshots it.
    sm.set_pending_lqa_cmd(raw24);
    sm.initiate_call("JOE");
    advance_to_tx(sm);

    // Scan captured words for CMD 'a'.
    bool found_cmd_a = false;
    for (const auto& w : cap.words) {
        if (w.type == PreambleType::CMD && w.address[0] == 'a') {
            found_cmd_a = true;
            // Verify KA1=1 is in bit 13 of the raw 24-bit word.
            // re-encode and decode to verify round-trip through SM builder:
            const LQACmdPayload dec = decode_lqa_cmd(w.raw_payload);
            assert(dec.ka1   == true);
            assert(dec.sinad == 18);
            assert(dec.ber   == 5);
            assert(dec.mp    == 3);
            break;
        }
    }
    assert(found_cmd_a && "CMD 'a' word not found in TX sequence");

    std::cout << "  PASS (CMD 'a' found at word index "
              << [&](){
                    for (size_t i = 0; i < cap.words.size(); ++i)
                        if (cap.words[i].type == PreambleType::CMD && cap.words[i].address[0] == 'a')
                            return i;
                    return cap.words.size();
                 }()
              << " of " << cap.words.size() << " total words)" << std::endl;
}

static void test_cmd_lqa_not_in_tx_when_not_set()
{
    std::cout << "Test: No CMD 'a' in TX when pending not set..." << std::endl;

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);
    // No set_pending_lqa_cmd() call.
    sm.initiate_call("JOE");
    advance_to_tx(sm);

    for (const auto& w : cap.words)
        assert(!(w.type == PreambleType::CMD && w.address[0] == 'a')
               && "Unexpected CMD 'a' in TX when none queued");

    std::cout << "  PASS (" << cap.words.size() << " TX words, none is CMD 'a')" << std::endl;
}

static void test_cmd_lqa_only_once_per_call()
{
    std::cout << "Test: Only one CMD 'a' in TX sequence (not repeated)..." << std::endl;

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);

    LQACmdPayload p;
    p.sinad = 10;
    p.ber   = 2;
    p.ka1   = true;
    sm.set_pending_lqa_cmd(encode_lqa_cmd(p));
    sm.initiate_call("JOE");
    advance_to_tx(sm);

    int count = 0;
    for (const auto& w : cap.words)
        if (w.type == PreambleType::CMD && w.address[0] == 'a') ++count;

    assert(count == 1 && "Expected exactly one CMD 'a' word in TX sequence");
    std::cout << "  PASS (CMD 'a' count = " << count << ")" << std::endl;
}

static void test_cmd_lqa_precedes_tis_conclusion()
{
    std::cout << "Test: CMD 'a' appears before TIS conclusion word..." << std::endl;

    WordCapture cap;
    ALEStateMachine sm = make_sm(cap);

    LQACmdPayload p;
    p.sinad = 15;
    p.ka1   = true;
    sm.set_pending_lqa_cmd(encode_lqa_cmd(p));
    sm.initiate_call("JOE");
    advance_to_tx(sm);

    int cmd_a_idx = -1;
    int tis_idx   = -1;
    for (int i = 0; i < static_cast<int>(cap.words.size()); ++i) {
        if (cap.words[i].type == PreambleType::CMD && cap.words[i].address[0] == 'a')
            cmd_a_idx = i;
        if (cap.words[i].type == PreambleType::TIS && tis_idx < 0)
            tis_idx = i;
    }

    assert(cmd_a_idx >= 0 && "CMD 'a' not found");
    assert(tis_idx   >= 0 && "TIS not found");
    assert(cmd_a_idx < tis_idx && "CMD 'a' must precede TIS conclusion");

    std::cout << "  PASS (CMD 'a' @ " << cmd_a_idx
              << ", TIS @ " << tis_idx << ")" << std::endl;
}

static void test_decode_encode_round_trip()
{
    std::cout << "Test: encode_lqa_cmd / decode_lqa_cmd round-trip..." << std::endl;

    LQACmdPayload p;
    p.sinad = 22;
    p.ber   = 7;
    p.mp    = 5;
    p.ka1   = true;
    const uint32_t raw24 = encode_lqa_cmd(p);
    const LQACmdPayload out = decode_lqa_cmd(raw24);

    assert(out.sinad == 22);
    assert(out.ber   == 7);
    assert(out.mp    == 5);
    assert(out.ka1   == true);

    // Verify sentinels also round-trip.
    LQACmdPayload def;  // defaults: sinad=31, ber=31, mp=7, ka1=false
    const uint32_t def_raw = encode_lqa_cmd(def);
    const LQACmdPayload def_out = decode_lqa_cmd(def_raw);
    assert(def_out.sinad == 31);
    assert(def_out.ber   == 31);
    assert(def_out.mp    == 7);
    assert(def_out.ka1   == false);

    std::cout << "  PASS" << std::endl;
}

int main()
{
    test_decode_encode_round_trip();
    test_cmd_lqa_in_tx_with_ka1_true();
    test_cmd_lqa_not_in_tx_when_not_set();
    test_cmd_lqa_only_once_per_call();
    test_cmd_lqa_precedes_tis_conclusion();

    std::cout << "\nAll bilateral LQA TX tests PASSED." << std::endl;
    return 0;
}
