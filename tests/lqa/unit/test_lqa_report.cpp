/**
 * @file test_lqa_report.cpp
 * @brief Unit tests for LQA Report encoder/decoder and lqa_age_code (Plan §Block C, C1)
 */

#include "LQA/lqa_report.h"
#include "LQA/lqa_metrics.h"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace ale;

// ─── Encoder / Decoder Round-Trip ─────────────────────────────────────────────

static void test_roundtrip_single() {
    std::cout << "Test: LQA Report round-trip (1 report)..." << std::endl;

    LQAReport r;
    r.frequency_hz = 7073000u;
    r.age   = 2;
    r.mp    = 3;
    r.sinad = 18;
    r.ber   = 5;

    const auto payloads = LQAReportEncoder::pack_reports({r});

    // Stateless round-trip via unpack_reports():
    const auto decoded = LQAReportDecoder::unpack_reports(payloads, 1);
    assert(decoded.size() == 1 && decoded[0].frequency_hz == 7073000u);

    // Stateful decoder path:
    LQAReportDecoder dec;
    dec.start(LQAReportEncoder::encode_report_cmd(1));
    assert(dec.active());
    bool done = false;
    for (auto p : payloads) {
        done = dec.feed(p);
        if (done) break;
    }
    assert(done);
    assert(dec.reports().size() == 1);
    const auto& o = dec.reports()[0];
    assert(o.frequency_hz == 7073000u);
    assert(o.age   == 2);
    assert(o.mp    == 3);
    assert(o.sinad == 18);
    assert(o.ber   == 5);

    std::cout << "  PASS" << std::endl;
}

static void test_roundtrip_multi() {
    std::cout << "Test: LQA Report round-trip (5 reports)..." << std::endl;

    std::vector<LQAReport> reports;
    for (int i = 0; i < 5; ++i) {
        LQAReport r;
        r.frequency_hz = static_cast<uint32_t>(7000000u + i * 100000u);
        r.age   = static_cast<uint8_t>(i % 8);
        r.mp    = static_cast<uint8_t>(i % 7);
        r.sinad = static_cast<uint8_t>(10 + i);
        r.ber   = static_cast<uint8_t>(i * 2);
        reports.push_back(r);
    }

    const auto payloads = LQAReportEncoder::pack_reports(reports);
    // 5 × 36 = 180 bits → ceil(180/21) = 9 DATA words
    assert(payloads.size() == 9);

    LQAReportDecoder dec;
    dec.start(LQAReportEncoder::encode_report_cmd(5));
    bool done = false;
    for (auto p : payloads) {
        done = dec.feed(p);
        if (done) break;
    }
    assert(done);
    assert(dec.reports().size() == 5);

    for (int i = 0; i < 5; ++i) {
        const auto& o = dec.reports()[static_cast<size_t>(i)];
        assert(o.frequency_hz == static_cast<uint32_t>(7000000u + i * 100000u));
        assert(o.age   == static_cast<uint8_t>(i % 8));
        assert(o.mp    == static_cast<uint8_t>(i % 7));
        assert(o.sinad == static_cast<uint8_t>(10 + i));
        assert(o.ber   == static_cast<uint8_t>(i * 2));
    }
    std::cout << "  PASS" << std::endl;
}

static void test_sentinel_fields() {
    std::cout << "Test: LQA Report sentinel fields preserved..." << std::endl;

    LQAReport r;  // default: sinad=31, ber=31, mp=7
    r.frequency_hz = 14250000u;
    r.age = 7;

    const auto payloads = LQAReportEncoder::pack_reports({r});
    LQAReportDecoder dec;
    dec.start(LQAReportEncoder::encode_report_cmd(1));
    for (auto p : payloads) if (dec.feed(p)) break;

    assert(dec.reports().size() == 1);
    assert(dec.reports()[0].sinad == 31u);
    assert(dec.reports()[0].ber   == 31u);
    assert(dec.reports()[0].mp    == 7u);
    assert(dec.reports()[0].age   == 7u);

    std::cout << "  PASS" << std::endl;
}

// ─── Header encoding ──────────────────────────────────────────────────────────

static void test_header_encoding() {
    std::cout << "Test: CMD 'r' header encoding..." << std::endl;

    const uint32_t hdr = LQAReportEncoder::encode_report_cmd(5);
    // [20:14]='r'(0x72), [13:12]=00, [11]=0, [10:8]=101=5, [7:0]=5
    assert(((hdr >> 14) & 0x7Fu) == 0x72u);  // char 'r'
    assert(((hdr >> 12) & 0x3u)  == 0u);      // type=header
    assert(((hdr >>  8) & 0x7u)  == 5u);      // KR4-2=101
    assert((hdr & 0xFFu)         == 5u);      // count=5

    std::cout << "  PASS" << std::endl;
}

// ─── lqa_age_code (Table VI) ─────────────────────────────────────────────────

static void test_age_code() {
    std::cout << "Test: lqa_age_code (Table VI) for all intervals..." << std::endl;

    const uint32_t now = 100000000u;
    // 0: 0-15 min (< 900000 ms)
    assert(lqa_age_code(now - 500000u,  now) == 0u);
    // 1: 15-30 min
    assert(lqa_age_code(now - 1200000u, now) == 1u);
    // 2: 30-60 min
    assert(lqa_age_code(now - 2700000u, now) == 2u);
    // 3: 1-4 h
    assert(lqa_age_code(now - 7200000u, now) == 3u);
    // 4: 4-8 h
    assert(lqa_age_code(now - 21600000u, now) == 4u);
    // 5: 8-16 h
    assert(lqa_age_code(now - 43200000u, now) == 5u);
    // 6: 16-25 h
    assert(lqa_age_code(now - 72000000u, now) == 6u);
    // 7: >25 h
    assert(lqa_age_code(now - 95000000u, now) == 7u);
    // 7: unknown (last_contact_ms=0)
    assert(lqa_age_code(0u, now) == 7u);

    std::cout << "  PASS" << std::endl;
}

// ─── LQAReportDecoder: inactive when count=0 ─────────────────────────────────

static void test_decoder_empty() {
    std::cout << "Test: LQAReportDecoder inactive on count=0..." << std::endl;

    LQAReportDecoder dec;
    dec.start(LQAReportEncoder::encode_report_cmd(0));
    assert(!dec.active());

    std::cout << "  PASS" << std::endl;
}

int main() {
    test_roundtrip_single();
    test_roundtrip_multi();
    test_sentinel_fields();
    test_header_encoding();
    test_age_code();
    test_decoder_empty();

    std::cout << "\nAll LQA report tests PASSED." << std::endl;
    return 0;
}
