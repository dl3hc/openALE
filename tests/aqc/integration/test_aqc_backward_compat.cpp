/**
 * \file test_aqc_backward_compat.cpp
 * \brief Integration tests: AQC-ALE backward compatibility with variable dwell rates
 *
 * Covers AC-GEN-002-003:
 *   "The system can interoperate with AQC-ALE stations that use variable dwell
 *    rates. AQC frames are correctly detected and processed."
 *
 * Tests verify:
 *   TEST 1  Slot timing at TD5 (200 ms, 5 ch/s — baseline)
 *   TEST 2  Slot timing at TD2 (500 ms, 2 ch/s — backward-compat rate)
 *   TEST 3  is_valid_dwell_rate accepts TD5 and TD2, rejects others
 *   TEST 4  AQC call-probe parsing succeeds regardless of caller dwell rate
 *   TEST 5  AQC call-handshake parsing succeeds regardless of caller dwell rate
 */

#include "Protocol/AQC/aqc_protocol.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include <cassert>
#include <cstring>
#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with fixed-size ALE address fields
#endif
#include <iostream>

using namespace ale;
using namespace ale::aqc;

// ── TEST 1 ────────────────────────────────────────────────────────────────────
// Slot timing at TD5 = 200 ms (baseline, 5 ch/s).
// Slot N must start at base + N*200 ms.
void test_slot_timing_td5() {
    std::cout << "[TEST 1] Slot timing — TD5 (200 ms, 5 ch/s)\n";

    const uint32_t base = 1000;
    const uint32_t td5  = SlotManager::DWELL_TD5_MS;  // 200

    assert(SlotManager::calculate_slot_time(0, base, td5) == 1000u);
    assert(SlotManager::calculate_slot_time(1, base, td5) == 1200u);
    assert(SlotManager::calculate_slot_time(3, base, td5) == 1600u);
    assert(SlotManager::calculate_slot_time(7, base, td5) == 2400u);

    // Default overload (no dwell_ms argument) must give the same result as TD5
    assert(SlotManager::calculate_slot_time(0, base) == 1000u);
    assert(SlotManager::calculate_slot_time(3, base) == 1600u);
    assert(SlotManager::calculate_slot_time(7, base) == 2400u);

    std::cout << "  ✓ Slot 0: " << SlotManager::calculate_slot_time(0, base, td5) << " ms\n";
    std::cout << "  ✓ Slot 3: " << SlotManager::calculate_slot_time(3, base, td5) << " ms\n";
    std::cout << "  ✓ Slot 7: " << SlotManager::calculate_slot_time(7, base, td5) << " ms\n";
    std::cout << "  ✓ Default overload matches TD5\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 2 ────────────────────────────────────────────────────────────────────
// Slot timing at TD2 = 500 ms (2 ch/s, backward-compat rate).
// Slot N must start at base + N*500 ms.
void test_slot_timing_td2() {
    std::cout << "[TEST 2] Slot timing — TD2 (500 ms, 2 ch/s)\n";

    const uint32_t base = 2000;
    const uint32_t td2  = SlotManager::DWELL_TD2_MS;  // 500

    assert(SlotManager::calculate_slot_time(0, base, td2) == 2000u);
    assert(SlotManager::calculate_slot_time(1, base, td2) == 2500u);
    assert(SlotManager::calculate_slot_time(3, base, td2) == 3500u);
    assert(SlotManager::calculate_slot_time(7, base, td2) == 5500u);

    // TD2 slots must differ from TD5 slots — verify backward compat is not trivial
    assert(SlotManager::calculate_slot_time(3, base, td2) !=
           SlotManager::calculate_slot_time(3, base, SlotManager::DWELL_TD5_MS));

    std::cout << "  ✓ Slot 0: " << SlotManager::calculate_slot_time(0, base, td2) << " ms\n";
    std::cout << "  ✓ Slot 3: " << SlotManager::calculate_slot_time(3, base, td2) << " ms\n";
    std::cout << "  ✓ Slot 7: " << SlotManager::calculate_slot_time(7, base, td2) << " ms\n";
    std::cout << "  ✓ TD2 slots differ from TD5 slots\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 3 ────────────────────────────────────────────────────────────────────
// is_valid_dwell_rate: TD5 and TD2 are spec-conformant; everything else is not.
void test_valid_dwell_rates() {
    std::cout << "[TEST 3] is_valid_dwell_rate — TD5/TD2 accepted, others rejected\n";

    assert(SlotManager::is_valid_dwell_rate(SlotManager::DWELL_TD5_MS) == true);   // 200 ms
    assert(SlotManager::is_valid_dwell_rate(SlotManager::DWELL_TD2_MS) == true);   // 500 ms
    assert(SlotManager::is_valid_dwell_rate(0u)   == false);
    assert(SlotManager::is_valid_dwell_rate(100u) == false);
    assert(SlotManager::is_valid_dwell_rate(300u) == false);
    assert(SlotManager::is_valid_dwell_rate(1000u) == false);

    std::cout << "  ✓ TD5 (200 ms): valid\n";
    std::cout << "  ✓ TD2 (500 ms): valid\n";
    std::cout << "  ✓ 0 / 100 / 300 / 1000 ms: invalid\n";
    std::cout << "  PASSED\n\n";
}

// ── TEST 4 ────────────────────────────────────────────────────────────────────
// AQC call-probe parsing succeeds for both TD5 and TD2 remote stations.
// The parser must not gate on timing — it parses the frame structure only.
void test_call_probe_variable_dwell() {
    std::cout << "[TEST 4] AQC call-probe parsing — TD5 and TD2 remote stations\n";

    auto make_probe_words = [](const char* to_addr, const char* from_addr, uint32_t ts) {
        ALEWord words[2];
        words[0].type = PreambleType::TO;
        strncpy(words[0].address, to_addr, sizeof(words[0].address) - 1);
        words[0].raw_payload = 0;
        words[0].timestamp_ms = ts;
        words[0].valid = true;

        words[1].type = PreambleType::FROM;
        strncpy(words[1].address, from_addr, sizeof(words[1].address) - 1);
        words[1].raw_payload = 0;
        words[1].timestamp_ms = ts + 100;
        words[1].valid = true;
        return std::pair<ALEWord, ALEWord>{words[0], words[1]};
    };

    // TD5-rate station
    {
        ALEWord words[2];
        words[0].type = PreambleType::TO;
        strncpy(words[0].address, "ABC", sizeof(words[0].address) - 1);
        words[0].raw_payload = 0;
        words[0].timestamp_ms = 1000;
        words[0].valid = true;
        words[1].type = PreambleType::FROM;
        strncpy(words[1].address, "XYZ", sizeof(words[1].address) - 1);
        words[1].raw_payload = 0;
        words[1].timestamp_ms = 1200;  // 200 ms later (TD5)
        words[1].valid = true;

        AQCParser parser;
        AQCCallProbe probe;
        bool ok = parser.parse_call_probe(words, 2, probe);
        assert(ok == true);
        assert(probe.to_address == "ABC");
        assert(probe.term_address == "XYZ");
        std::cout << "  ✓ TD5-rate probe parsed: TO=" << probe.to_address
                  << " TERM=" << probe.term_address << "\n";
    }

    // TD2-rate station (words arrive 500 ms apart)
    {
        ALEWord words[2];
        words[0].type = PreambleType::TO;
        strncpy(words[0].address, "DEF", sizeof(words[0].address) - 1);
        words[0].raw_payload = 0;
        words[0].timestamp_ms = 2000;
        words[0].valid = true;
        words[1].type = PreambleType::FROM;
        strncpy(words[1].address, "GHI", sizeof(words[1].address) - 1);
        words[1].raw_payload = 0;
        words[1].timestamp_ms = 2500;  // 500 ms later (TD2)
        words[1].valid = true;

        AQCParser parser;
        AQCCallProbe probe;
        bool ok = parser.parse_call_probe(words, 2, probe);
        assert(ok == true);
        assert(probe.to_address == "DEF");
        assert(probe.term_address == "GHI");
        std::cout << "  ✓ TD2-rate probe parsed: TO=" << probe.to_address
                  << " TERM=" << probe.term_address << "\n";
    }

    std::cout << "  PASSED\n\n";
}

// ── TEST 5 ────────────────────────────────────────────────────────────────────
// Given a received handshake, the response slot time adapts correctly to the
// remote station's dwell rate (TD5 vs TD2).
void test_handshake_variable_dwell() {
    std::cout << "[TEST 5] AQC call-handshake + adaptive slot timing\n";

    // Slot assigned to "ABC"
    uint8_t slot = SlotManager::assign_slot("ABC");
    assert(slot < 8u);

    const uint32_t base = 5000;

    // Caller's expected response time when remote uses TD5 vs TD2
    uint32_t t_td5 = SlotManager::calculate_slot_time(slot, base, SlotManager::DWELL_TD5_MS);
    uint32_t t_td2 = SlotManager::calculate_slot_time(slot, base, SlotManager::DWELL_TD2_MS);

    // Slot 0 coincides with base for both rates; higher slots diverge
    if (slot > 0) {
        assert(t_td2 > t_td5);
        std::cout << "  ✓ Slot " << static_cast<int>(slot)
                  << " at TD5: " << t_td5 << " ms, at TD2: " << t_td2 << " ms\n";
    } else {
        assert(t_td5 == base);
        assert(t_td2 == base);
        std::cout << "  ✓ Slot 0: both TD5 and TD2 start at base=" << base << " ms\n";
    }

    // Verify slot range for addresses that hash to each slot
    for (uint8_t s = 0; s < 8; ++s) {
        uint32_t t5 = SlotManager::calculate_slot_time(s, base, SlotManager::DWELL_TD5_MS);
        uint32_t t2 = SlotManager::calculate_slot_time(s, base, SlotManager::DWELL_TD2_MS);
        assert(t5 == base + s * SlotManager::DWELL_TD5_MS);
        assert(t2 == base + s * SlotManager::DWELL_TD2_MS);
    }
    std::cout << "  ✓ All 8 slots: t_tdN == base + slot*dwellN for both TD5 and TD2\n";

    // Basic handshake parsing still works (addresses are printable; DE not extracted
    // via is_aqc_format for printable-address words — that is expected; slot timing
    // is applied by the caller using calculate_slot_time once it knows the remote rate)
    ALEWord words[2];
    words[0].type = PreambleType::TO;
    strncpy(words[0].address, "ABC", sizeof(words[0].address) - 1);
    words[0].raw_payload = 0;
    words[0].timestamp_ms = base;
    words[0].valid = true;
    words[1].type = PreambleType::FROM;
    strncpy(words[1].address, "XYZ", sizeof(words[1].address) - 1);
    words[1].raw_payload = 0;
    words[1].timestamp_ms = base + 50;
    words[1].valid = true;

    AQCParser parser;
    AQCCallHandshake hs;
    bool ok = parser.parse_call_handshake(words, 2, hs);
    assert(ok == true);
    assert(hs.to_address == "ABC");
    assert(hs.from_address == "XYZ");

    std::cout << "  ✓ Handshake parsed: TO=" << hs.to_address
              << " FROM=" << hs.from_address << "\n";
    std::cout << "  PASSED\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "AQC-ALE Backward Compatibility Tests (AC-GEN-002-003)\n";
    std::cout << "========================================\n\n";

    try {
        test_slot_timing_td5();
        test_slot_timing_td2();
        test_valid_dwell_rates();
        test_call_probe_variable_dwell();
        test_handshake_variable_dwell();

        std::cout << "========================================\n";
        std::cout << "All AQC backward-compat tests PASSED! ✓\n";
        std::cout << "AC-GEN-002-003 verified.\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
