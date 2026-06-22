/**
 * @file test_lqa_bilateral_rx.cpp
 * @brief Unit tests for bilateral LQA RX path — CMD 'a' reception and
 *        bilateral database update (AC-CHAN-004-001, Table A-XIV).
 *
 * Covers:
 *   - decode_lqa_cmd() correctly extracts KA1, SINAD, BER, MP fields
 *   - lqa_database_.update_bilateral() stores data with correct sentinels
 *   - mark_bilateral_attempted() produces the "X" state (tried, no data)
 *   - SM integration: CMD 'a' word delivered to process_received_word()
 *     does not crash and leaves the SM in a valid state
 */

#include "Protocol/Control/ale_state_machine.h"
#include "LQA/lqa_metrics.h"
#include "LQA/lqa_database.h"
#include "Word/ale_word.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace ale;

// ── helpers ──────────────────────────────────────────────────────────────────

static ALEWord make_cmd_a_word(const LQACmdPayload& p)
{
    const uint32_t raw24 = encode_lqa_cmd(p);
    // Build the ALEWord the same way the SM's transmit path does.
    ALEWord w;
    w.type        = PreambleType::CMD;
    w.address[0]  = 'a';
    w.address[1]  = '\0';
    w.address[2]  = '\0';
    w.raw_payload = raw24 & 0x1FFFFFu;  // strip preamble bits (21-bit payload)
    w.valid       = true;
    return w;
}

// ── decode / encode tests ─────────────────────────────────────────────────────

static void test_decode_lqa_cmd_fields()
{
    std::cout << "Test: decode_lqa_cmd() extracts all fields correctly..." << std::endl;

    LQACmdPayload p;
    p.sinad = 20;
    p.ber   = 8;
    p.mp    = 4;
    p.ka1   = true;

    const uint32_t raw24 = encode_lqa_cmd(p);
    const LQACmdPayload out = decode_lqa_cmd(raw24);

    assert(out.sinad == 20);
    assert(out.ber   == 8);
    assert(out.mp    == 4);
    assert(out.ka1   == true);

    std::cout << "  PASS" << std::endl;
}

static void test_decode_lqa_cmd_no_value_sentinels()
{
    std::cout << "Test: decode_lqa_cmd() preserves no-value sentinels..." << std::endl;

    LQACmdPayload p;  // defaults: sinad=31, ber=31, mp=7, ka1=false
    const uint32_t raw24 = encode_lqa_cmd(p);
    const LQACmdPayload out = decode_lqa_cmd(raw24);

    assert(out.sinad == kSinadLqaNoValue);
    assert(out.ber   == kBerLqaNoValue);
    assert(out.mp    == kMpLqaNotMeasured);
    assert(out.ka1   == false);

    std::cout << "  PASS" << std::endl;
}

static void test_decode_ka1_false_without_report_request()
{
    std::cout << "Test: decode_lqa_cmd() KA1=0 when responder reports only..." << std::endl;

    LQACmdPayload p;
    p.sinad = 15;
    p.ber   = 3;
    p.mp    = 2;
    p.ka1   = false;  // responder side — does not request report back

    const LQACmdPayload out = decode_lqa_cmd(encode_lqa_cmd(p));
    assert(out.ka1   == false);
    assert(out.sinad == 15);
    assert(out.ber   == 3);
    assert(out.mp    == 2);

    std::cout << "  PASS" << std::endl;
}

// ── database update tests ─────────────────────────────────────────────────────

static void test_update_bilateral_stores_data()
{
    std::cout << "Test: update_bilateral() stores SINAD/BER/MP in DB entry..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;

    db.update_bilateral(freq, "JOE", /*sinad_code=*/18, /*ber_code=*/5, /*mp_code=*/3, 1000u);

    const auto e = db.get_entry(freq, "JOE");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 18u);
    assert(e->bilateral_ber   == 5u);
    assert(e->bilateral_mp    == 3u);
    assert(e->bilateral_handshake_tried == true);

    std::cout << "  PASS" << std::endl;
}

static void test_update_bilateral_sentinel_sinad()
{
    std::cout << "Test: update_bilateral() sentinel (31) SINAD preserved..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;

    // Remote sent no-value sentinels (could not measure).
    db.update_bilateral(freq, "JOE", /*sinad=*/31, /*ber=*/31, /*mp=*/7, 1000u);

    const auto e = db.get_entry(freq, "JOE");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 31u);  // "no value" sentinel preserved
    assert(e->bilateral_ber   == 31u);
    assert(e->bilateral_mp    == 7u);
    assert(e->bilateral_handshake_tried == true);

    std::cout << "  PASS" << std::endl;
}

static void test_mark_bilateral_attempted_x_state()
{
    std::cout << "Test: mark_bilateral_attempted() produces X state (tried, no data)..."
              << std::endl;

    LQADatabase db;
    const uint32_t freq = 14250000u;

    db.mark_bilateral_attempted(freq, "ALPHA");

    const auto e = db.get_entry(freq, "ALPHA");
    assert(e != nullptr);
    assert(e->bilateral_handshake_tried == true);
    // X state: tried flag set, but data fields remain at "no data" sentinels.
    assert(e->bilateral_sinad == 31u);
    assert(e->bilateral_ber   == 31u);
    assert(e->bilateral_mp    == 7u);

    std::cout << "  PASS" << std::endl;
}

static void test_update_bilateral_overwrites_existing()
{
    std::cout << "Test: update_bilateral() overwrites previous bilateral data..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7200000u;

    db.update_bilateral(freq, "JOE", 10, 2, 1, 1000u);
    db.update_bilateral(freq, "JOE", 25, 15, 6, 2000u);

    const auto e = db.get_entry(freq, "JOE");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 25u);
    assert(e->bilateral_ber   == 15u);
    assert(e->bilateral_mp    == 6u);

    std::cout << "  PASS" << std::endl;
}

// ── SM integration: CMD 'a' does not crash the SM ───────────────────────────

static void test_sm_accepts_cmd_a_in_handshake()
{
    std::cout << "Test: SM process_received_word() accepts CMD 'a' without crash..."
              << std::endl;

    // Drive the SM to HANDSHAKE/WAIT_CYCLE_END by feeding an incoming call.
    ALEStateMachine sm;
    sm.set_transmit_callback([](const ALEWord&){});
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_channel_callback([](const Channel&){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_self_address("JOE");
    sm.set_target_scan_channels(0);
    sm.process_event(ALEEvent::START_SCAN);

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    // Simulate a minimal incoming call from SAM to JOE (1 word addresses):
    // scanning × 2, leading × 2, conclusion TIS SAM.
    const char joe3[3] = {'J','O','E'};
    const char sam3[3] = {'S','A','M'};
    uint32_t t = 1000u;
    auto rx = [&](PreambleType type, const char a3[3]) {
        sm.update(t);
        sm.process_received_word(WordParser::make_word(type, a3));
        t += Trw;
    };
    rx(PreambleType::TO,  joe3);  // scanning ×2
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TO,  joe3);  // leading ×2
    rx(PreambleType::TO,  joe3);
    rx(PreambleType::TIS, sam3);  // conclusion → HANDSHAKE / WAIT_CYCLE_END

    assert(sm.get_state() == ALEState::HANDSHAKE);

    // Now inject a CMD 'a' word — the SM should not crash.
    LQACmdPayload p;
    p.sinad = 18;
    p.ber   = 5;
    p.mp    = 3;
    p.ka1   = true;
    const ALEWord cmd_a = make_cmd_a_word(p);
    sm.process_received_word(cmd_a);

    // SM must still be alive and in a valid state.
    const ALEState st = sm.get_state();
    assert(st == ALEState::HANDSHAKE || st == ALEState::IDLE || st == ALEState::SCANNING);

    std::cout << "  PASS (SM state after CMD 'a' = "
              << ALEStateMachine::state_name(st) << ")" << std::endl;
}

int main()
{
    test_decode_lqa_cmd_fields();
    test_decode_lqa_cmd_no_value_sentinels();
    test_decode_ka1_false_without_report_request();
    test_update_bilateral_stores_data();
    test_update_bilateral_sentinel_sinad();
    test_mark_bilateral_attempted_x_state();
    test_update_bilateral_overwrites_existing();
    test_sm_accepts_cmd_a_in_handshake();

    std::cout << "\nAll bilateral LQA RX tests PASSED." << std::endl;
    return 0;
}
