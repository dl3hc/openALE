/**
 * @file test_lqa_noise_cmd.cpp
 * @brief Unit tests for CMD NOISE encoding (Figure A-26, AC-CHAN-004-002)
 */

#include "Word/ale_sequence.h"
#include "LQA/lqa_database.h"
#include <iostream>
#include <cassert>

using namespace ale;

static void test_noise_cmd_bit_layout() {
    std::cout << "Test: noise_cmd bit layout..." << std::endl;

    const uint8_t max_db  = 65u;
    const uint8_t mean_db = 58u;
    const auto seq = ALESequenceBuilder::noise_cmd(max_db, mean_db);

    assert(seq.size() == 1);
    const ALEWord& w = seq.words()[0];
    assert(w.type == PreambleType::CMD);
    assert(w.address[0] == 'n');
    assert(w.valid);

    // 21-bit payload: [20:14]='n'(0x6E) | [13:7]=max_db | [6:0]=mean_db
    assert(((w.raw_payload >> 14) & 0x7Fu) == 0x6Eu);
    assert(((w.raw_payload >>  7) & 0x7Fu) == max_db);
    assert(( w.raw_payload        & 0x7Fu) == mean_db);

    std::cout << "  PASS" << std::endl;
}

static void test_noise_cmd_sentinel() {
    std::cout << "Test: noise_cmd sentinel values (127=no report)..." << std::endl;

    const auto seq = ALESequenceBuilder::noise_cmd(127u, 127u);
    assert(seq.size() == 1);
    const ALEWord& w = seq.words()[0];
    assert(((w.raw_payload >> 7) & 0x7Fu) == 127u);
    assert(( w.raw_payload       & 0x7Fu) == 127u);

    std::cout << "  PASS" << std::endl;
}

static void test_noise_cmd_rx_update() {
    std::cout << "Test: CMD 'n' RX → update_noise_floor..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;
    // Simulate what the controller does on CMD 'n' reception.
    const uint8_t max_db  = 65u;
    const uint8_t mean_db = 58u;
    db.update_noise_floor(freq, max_db, mean_db, 1000u);

    const auto e = db.get_entry(freq, "");
    assert(e != nullptr);
    // mean_db=58 stored as float: 58 - 120 = -62.0
    const float expected = static_cast<float>(mean_db) - 120.0f;
    assert(std::abs(e->noise_floor_dbm - expected) < 0.01f);

    std::cout << "  PASS" << std::endl;
}

static void test_noise_cmd_sentinel_ignored() {
    std::cout << "Test: CMD 'n' with 127/127 is a no-op..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;
    // 127 = no report; should not create an entry
    db.update_noise_floor(freq, 127u, 127u, 1000u);
    assert(db.get_entry(freq, "") == nullptr);

    std::cout << "  PASS" << std::endl;
}

int main() {
    test_noise_cmd_bit_layout();
    test_noise_cmd_sentinel();
    test_noise_cmd_rx_update();
    test_noise_cmd_sentinel_ignored();

    std::cout << "\nAll LQA noise CMD tests PASSED." << std::endl;
    return 0;
}
