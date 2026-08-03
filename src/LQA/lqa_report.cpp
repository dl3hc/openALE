/**
 * @file lqa_report.cpp
 * @brief LQA Report Protocol encoder/decoder — §5.4.4 MIL-STD-187-721D
 */

#include "LQA/lqa_report.h"
#include <algorithm>
#include <cstring>

namespace ale {

// ── LQAReportEncoder ────────────────────────────────────────────────────────

uint32_t LQAReportEncoder::encode_report_cmd(uint8_t report_count) {
    // 21-bit payload: [20:14]='r'(0x72) | [13:12]=00(type=header) |
    //                 [11]=0(DTM) | [10:8]=101(abs freq) | [7:0]=count
    return (0x72u << 14) | (0u << 12) | (0u << 11) | (5u << 8)
           | (report_count & 0xFFu);
}

uint32_t LQAReportEncoder::encode_report_request(uint8_t max_age_code) {
    // 21-bit payload: [20:14]='r'(0x72) | [13:12]=10(type=request) |
    //                 [11:9]=max_age_code[2:0] | [8:0]=0
    return (0x72u << 14) | (2u << 12) | ((max_age_code & 0x7u) << 9);
}

std::vector<uint32_t> LQAReportEncoder::pack_reports(
        const std::vector<LQAReport>& reports) {
    if (reports.empty()) return {};

    // Each report is 36 bits; concatenate into a single bit stream then
    // slice into 21-bit chunks.
    const size_t total_bits = reports.size() * 36u;
    const size_t n_words    = (total_bits + 20u) / 21u;  // ceil

    // Work with a flat bit array (MSB first within each report).
    std::vector<bool> bits;
    bits.reserve(n_words * 21u);

    for (const auto& r : reports) {
        const uint32_t freq_des = r.frequency_hz / 100u;  // 20-bit, 100 Hz res.
        // Push 36 bits MSB-first: freq(20) + age(3) + mp(3) + sinad(5) + ber(5)
        for (int i = 19; i >= 0; --i) bits.push_back((freq_des >> i) & 1u);
        for (int i =  2; i >= 0; --i) bits.push_back((r.age    >> i) & 1u);
        for (int i =  2; i >= 0; --i) bits.push_back((r.mp     >> i) & 1u);
        for (int i =  4; i >= 0; --i) bits.push_back((r.sinad  >> i) & 1u);
        for (int i =  4; i >= 0; --i) bits.push_back((r.ber    >> i) & 1u);
    }

    // Pad to multiple of 21 with zeros.
    while (bits.size() % 21 != 0) bits.push_back(false);

    std::vector<uint32_t> payloads;
    payloads.reserve(n_words);
    for (size_t w = 0; w < bits.size(); w += 21) {
        uint32_t val = 0;
        for (int b = 0; b < 21; ++b)
            val = (val << 1) | (bits[w + b] ? 1u : 0u);
        payloads.push_back(val);
    }
    return payloads;
}

// ── LQAReportDecoder ────────────────────────────────────────────────────────

void LQAReportDecoder::start(uint32_t header_raw) {
    reset();
    // Extract count from lower 8 bits of header payload.
    expected_count_ = static_cast<uint8_t>(header_raw & 0xFFu);
    if (expected_count_ == 0) return;
    bits_needed_ = expected_count_ * 36u;
    active_ = true;
}

bool LQAReportDecoder::feed(uint32_t data_raw) {
    if (!active_) return false;
    payloads_.push_back(data_raw & 0x1FFFFFu);  // 21-bit payload
    bits_collected_ += 21u;
    if (bits_collected_ >= bits_needed_) {
        reports_ = unpack(payloads_, expected_count_);
        active_  = false;
        return true;
    }
    return false;
}

void LQAReportDecoder::reset() {
    active_         = false;
    expected_count_ = 0;
    bits_needed_    = 0;
    bits_collected_ = 0;
    payloads_.clear();
    reports_.clear();
}

std::vector<LQAReport> LQAReportDecoder::unpack_reports(
        const std::vector<uint32_t>& payloads, uint8_t count) {
    return unpack(payloads, count);
}

std::vector<LQAReport> LQAReportDecoder::unpack(
        const std::vector<uint32_t>& payloads, uint8_t count) {
    // Flatten payloads into bit stream (MSB-first within each 21-bit word).
    std::vector<bool> bits;
    bits.reserve(payloads.size() * 21u);
    for (uint32_t p : payloads)
        for (int i = 20; i >= 0; --i)
            bits.push_back((p >> i) & 1u);

    std::vector<LQAReport> result;
    result.reserve(count);
    size_t pos = 0;
    for (uint8_t n = 0; n < count && pos + 35 < bits.size(); ++n) {
        LQAReport r;
        uint32_t freq_des = 0;
        for (int i = 0; i < 20; ++i) freq_des = (freq_des << 1) | (bits[pos++] ? 1u : 0u);
        uint8_t age = 0;
        for (int i = 0; i < 3;  ++i) age = static_cast<uint8_t>((age << 1) | (bits[pos++] ? 1u : 0u));
        uint8_t mp  = 0;
        for (int i = 0; i < 3;  ++i) mp  = static_cast<uint8_t>((mp  << 1) | (bits[pos++] ? 1u : 0u));
        uint8_t sn  = 0;
        for (int i = 0; i < 5;  ++i) sn  = static_cast<uint8_t>((sn  << 1) | (bits[pos++] ? 1u : 0u));
        uint8_t ber = 0;
        for (int i = 0; i < 5;  ++i) ber = static_cast<uint8_t>((ber << 1) | (bits[pos++] ? 1u : 0u));
        r.frequency_hz = freq_des * 100u;
        r.age   = age;
        r.mp    = mp;
        r.sinad = sn;
        r.ber   = ber;
        result.push_back(r);
    }
    return result;
}

} // namespace ale
