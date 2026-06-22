/**
 * @file lqa_report.h
 * @brief LQA Report Protocol — §5.4.4 MIL-STD-187-721D
 *
 * Bulk exchange of stored multi-channel LQA measurements.
 * Format: CMD 'r' header word + DATA words carrying bit-packed reports.
 *
 * Per-report layout (36 bits, KR4-2=101 absolute frequency):
 *   [35:16] freq designator  20 bits  (freq_hz / 100, 100 Hz resolution)
 *   [15:13] age              3 bits   (Table VI age code)
 *   [12:10] mp               3 bits   (multipath code [0-7])
 *   [9:5]   sinad            5 bits   (SINAD code [0-30]; 31 = no value)
 *   [4:0]   ber              5 bits   (BER code   [0-30]; 31 = no value)
 *
 * Multiple reports are bit-packed contiguously and sliced into 21-bit
 * DATA-word payloads (same 21-bit payload field as all other ALE words).
 * N reports → ceil(N * 36 / 21) DATA words.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace ale {

/** Single LQA Report entry for one channel (§5.4.4 Figure 7). */
struct LQAReport {
    uint32_t frequency_hz;  ///< Channel frequency in Hz
    uint8_t  age;           ///< 3-bit age code [0-7] per Table VI
    uint8_t  mp;            ///< 3-bit multipath code [0-7]
    uint8_t  sinad;         ///< 5-bit SINAD code [0-30]; 31 = no value
    uint8_t  ber;           ///< 5-bit BER code   [0-30]; 31 = no value

    LQAReport() : frequency_hz(0), age(7), mp(7), sinad(31), ber(31) {}
};

/**
 * Encodes LQA Report messages into CMD + DATA word payloads.
 *
 * CMD 'r' header raw payload (21 bits, preamble stripped):
 *   [20:14] 'r' = 0x72
 *   [13:12] Type = 00 (header)
 *   [11]    KR5  = 0 (DTM format)
 *   [10:8]  KR4-2= 101 (absolute frequency designator)
 *   [7:0]   Chan = report count
 */
class LQAReportEncoder {
public:
    /** Build CMD 'r' header raw payload (21-bit, preamble stripped). */
    static uint32_t encode_report_cmd(uint8_t report_count);

    /** Build CMD 'r' request raw payload (Type=10, age field). */
    static uint32_t encode_report_request(uint8_t max_age_code);

    /**
     * Bit-pack all reports into 21-bit DATA payloads.
     * Each report is 36 bits; reports are concatenated and sliced.
     * @return Vector of 21-bit raw payloads (one per DATA word).
     */
    static std::vector<uint32_t> pack_reports(const std::vector<LQAReport>& reports);
};

/**
 * Stateful decoder that reassembles LQA reports from DATA word payloads.
 *
 * Usage:
 *   if (cmd_r_header_seen) decoder.start(header_raw_payload);
 *   if (decoder.active() && data_word_seen)
 *       if (decoder.feed(data_raw_payload)) { use decoder.reports(); decoder.reset(); }
 */
class LQAReportDecoder {
public:
    LQAReportDecoder() = default;

    /**
     * Start accumulation from a CMD 'r' header raw payload.
     * Extracts expected report count and resets internal state.
     */
    void start(uint32_t header_raw);

    /** Returns true while awaiting DATA words to complete the report set. */
    bool active() const { return active_; }

    /**
     * Feed one DATA word's raw 21-bit payload.
     * @return true when all expected reports have been received and decoded.
     */
    bool feed(uint32_t data_raw);

    /** Access decoded reports (valid only after feed() returns true). */
    const std::vector<LQAReport>& reports() const { return reports_; }

    /** Reset decoder to idle state. */
    void reset();

    /**
     * Stateless unpack: convert pre-collected 21-bit DATA payloads into reports.
     * Useful for unit testing without driving the stateful feed() loop.
     */
    static std::vector<LQAReport> unpack_reports(const std::vector<uint32_t>& payloads,
                                                  uint8_t count);

private:
    bool                    active_         = false;
    uint8_t                 expected_count_ = 0;
    uint32_t                bits_needed_    = 0;  ///< total bits = count * 36
    uint32_t                bits_collected_ = 0;
    std::vector<uint32_t>   payloads_;            ///< accumulated 21-bit chunks
    std::vector<LQAReport>  reports_;

    static std::vector<LQAReport> unpack(const std::vector<uint32_t>& payloads,
                                          uint8_t count);
};

} // namespace ale
