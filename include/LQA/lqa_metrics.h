/**
 * @file lqa_metrics.h
 * @brief LQA Metrics Collection
 * 
 * Collects Link Quality Analysis metrics from various sources:
 * - SNR from FFT demodulator (Phase 1)
 * - BER estimation from Golay FEC corrections (Phase 1)
 * - SINAD calculation
 * - Multipath detection
 * - Noise floor measurement
 * 
 * Clean-room implementation from MIL-STD-188-141B specification.
 */

#pragma once

#include "LQA/lqa_database.h"
#include <cstdint>
#include <vector>
#include <functional>

namespace ale {

/**
 * @brief Real-time metrics sample from demodulator/decoder
 *
 * Captured during word reception for immediate LQA updates.
 */
struct MetricsSample {
    float snr_db;              ///< Signal-to-Noise Ratio (dB)
    float signal_power_dbm;    ///< Signal power (dBm)
    float noise_power_dbm;     ///< Noise power (dBm)
    int fec_errors_corrected;  ///< Golay errors corrected this word
    bool decode_success;       ///< Word decoded successfully
    float multipath_delay_ms;  ///< Estimated multipath delay (ms)
    uint32_t timestamp_ms;     ///< Sample timestamp

    // BER unanimous-vote fields (REQ-CHAN-011, REQ-CHAN-012)
    uint8_t non_unanimous_count; ///< Non-unanimous majority-vote bits this word (0–48)
    bool golay_uncorrectable;    ///< True if Golay reported uncorrectable errors in either half

    MetricsSample()
        : snr_db(0.0f), signal_power_dbm(-120.0f), noise_power_dbm(-120.0f),
          fec_errors_corrected(0), decode_success(false),
          multipath_delay_ms(0.0f), timestamp_ms(0),
          non_unanimous_count(0), golay_uncorrectable(false) {}
};

/**
 * @brief BER accumulator for unanimous-vote-based BER measurement (REQ-CHAN-011, REQ-CHAN-012)
 *
 * After word-sync is established, feed one entry per received ALE word.
 * At frame end call ber_score() for the averaged BER code (0–48).
 *
 * Algorithm (MIL-STD-188-141B A.5.4.1.1):
 *   - Golay uncorrectable in either half → add 48 to running_sum
 *   - Otherwise                          → add non_unanimous_count (0–48)
 *   BER = running_sum / word_count       (integer, range 0–48)
 */
class BerAccumulator {
public:
    BerAccumulator() : running_sum_(0), word_count_(0) {}

    /** Feed one decoded ALE word into the accumulator. */
    void add_word(uint8_t non_unanimous_count, bool golay_uncorrectable) {
        running_sum_ += golay_uncorrectable ? 48u : non_unanimous_count;
        ++word_count_;
    }

    /**
     * Compute the averaged BER score.
     * @return 0–48 (0 = perfect, 48 = worst); returns 0 when no words fed.
     */
    uint8_t ber_score() const {
        if (word_count_ == 0) return 0;
        uint32_t score = running_sum_ / word_count_;
        return static_cast<uint8_t>(score > 48u ? 48u : score);
    }

    void reset() { running_sum_ = 0; word_count_ = 0; }

    uint32_t word_count() const { return word_count_; }

private:
    uint32_t running_sum_;
    uint32_t word_count_;
};

/**
 * @brief Configuration for LQA metrics collection
 */
struct MetricsConfig {
    bool enable_sinad = true;         ///< Calculate SINAD metric
    bool enable_multipath = true;     ///< Detect multipath
    uint32_t averaging_window = 10;   ///< Samples to average
    float multipath_threshold_db = 3.0f;  ///< Multipath detection threshold
};

/**
 * @brief LQA Metrics Collector
 *
 * Collects quality metrics from FSK demodulator and FEC decoder,
 * computes derived metrics (SINAD, multipath score), and feeds
 * LQA database.
 *
 * Usage:
 * @code
 * LQAMetrics metrics(&lqa_database);
 *
 * // During word reception:
 * MetricsSample sample;
 * sample.snr_db = demodulator.get_snr();
 * sample.fec_errors_corrected = decoder.get_error_count();
 * metrics.add_sample(sample, frequency, station);
 *
 * // Metrics automatically update LQA database
 * @endcode
 */
class LQAMetrics {
public:
    /**
     * @brief Rolling 60-minute noise-floor statistics (AC-CHAN-004-001).
     *
     * Derived from noise_power_dbm samples collected over the last 3600 s.
     * Both fields default to -120.0f when no samples are available.
     */
    struct NoiseFloorStats {
        float max_dbm  = -120.0f;  ///< Rolling maximum over 60 min (dBm)
        float mean_dbm = -120.0f;  ///< Rolling mean over 60 min (dBm)
    };

    /**
     * @brief Construct metrics collector
     * @param database LQA database to update (can be nullptr for standalone)
     */
    explicit LQAMetrics(LQADatabase* database = nullptr);
    
    /**
     * @brief Set configuration
     * @param config Configuration parameters
     */
    void set_config(const MetricsConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Configuration
     */
    MetricsConfig get_config() const;
    
    /**
     * @brief Set LQA database for updates
     * @param database Database pointer (nullptr to disable updates)
     */
    void set_database(LQADatabase* database);
    
    /**
     * @brief Add metrics sample from word reception
     * 
     * Accumulates sample in averaging window, computes derived metrics,
     * and updates LQA database when window is full.
     * 
     * @param sample Metrics from demodulator/decoder
     * @param frequency_hz Current channel frequency
     * @param remote_station Remote station address (empty for sounding)
     */
    void add_sample(const MetricsSample& sample,
                   uint32_t frequency_hz,
                   const std::string& remote_station);
    
    /**
     * @brief Calculate SINAD from SNR and distortion
     *
     * SINAD = Signal + Noise + Distortion / Noise + Distortion
     *
     * @param snr_db Signal-to-Noise Ratio (dB)
     * @param distortion_db Distortion level (dB below signal)
     * @return SINAD in dB (raw, unclamped)
     */
    float calculate_sinad(float snr_db, float distortion_db) const;

    /**
     * @brief Convert raw SINAD dB to LQA code in range [0, 30] (REQ-CHAN-013)
     *
     * Maps any float SINAD value onto the integer scale defined by
     * MIL-STD-188-141B A.5.4.1.2:
     *   <= 0 dB  → code 0
     *   >= 30 dB → code 30
     *   else     → round(sinad_db) ∈ [1, 29]
     *
     * @param sinad_db Raw SINAD in dB
     * @return LQA code 0–30 (no value outside this range)
     */
    uint8_t sinad_to_lqa_code(float sinad_db) const;
    
    /**
     * @brief Estimate BER from FEC error count
     * 
     * Uses Golay (24,12) statistics to estimate channel BER.
     * 
     * @param errors_corrected Errors corrected in last N words
     * @param total_words Total words received
     * @return Estimated BER (0.0 - 1.0)
     */
    float estimate_ber(int errors_corrected, int total_words) const;
    
    /**
     * @brief Convert measured multipath delay (ms) to 3-bit LQA code (AC-CHAN-002-003)
     *
     * Maps multipath delay onto the 3-bit CMD LQA MP field
     * (MIL-STD-188-141B A.5.4.2, Table A-XIII):
     *   < 0 ms   → code 0 (negative clamped)
     *   0..6 ms  → code 0..6 (floor to integer ms)
     *   > 6 ms   → code 7  ("6+ ms" saturation / out-of-range)
     *
     * @param delay_ms Measured multipath delay in milliseconds
     * @return 3-bit code 0–7 (7 = 6+ ms or not measured)
     */
    uint8_t multipath_delay_to_lqa_code(float delay_ms) const;

    /**
     * @brief Detect and score multipath
     *
     * Analyzes signal characteristics to detect multipath propagation.
     *
     * @param samples Recent signal samples
     * @return Multipath score (0.0 = none, 1.0 = severe)
     */
    float detect_multipath(const std::vector<float>& samples) const;
    
    /**
     * @brief Measure noise floor
     * 
     * Estimates noise floor during quiet periods (no signal).
     * 
     * @param samples Noise samples (no signal present)
     * @return Noise floor in dBm
     */
    float measure_noise_floor(const std::vector<float>& samples) const;
    
    /**
     * @brief Get averaged metrics for current window
     * 
     * @return Averaged metrics sample
     */
    MetricsSample get_averaged_sample() const;
    
    /**
     * @brief Clear averaging window
     */
    void reset();

    /**
     * @brief Get number of samples in current window
     * @return Sample count
     */
    size_t get_sample_count() const;

    /**
     * @brief Get rolling 60-minute noise-floor statistics (AC-CHAN-004-001).
     *
     * Samples older than 3600 s are excluded.  Returns default stats
     * (max/mean = -120 dBm) when no samples are available.
     *
     * @param now_ms Current monotonic time in milliseconds
     */
    NoiseFloorStats get_noise_floor_stats(uint32_t now_ms = 0) const;

private:
    /**
     * @brief Update LQA database with averaged metrics
     * 
     * @param frequency_hz Channel frequency
     * @param remote_station Station address
     */
    void update_database(uint32_t frequency_hz, const std::string& remote_station);
    
    /**
     * @brief Compute average of samples in window
     * @return Averaged sample
     */
    MetricsSample compute_average() const;
    
    LQADatabase* database_;                  ///< LQA database to update
    MetricsConfig config_;                   ///< Configuration
    std::vector<MetricsSample> samples_;     ///< Averaging window

    // Rolling 60-min noise floor window (AC-CHAN-004-001)
    struct NoiseFloorSample { uint32_t timestamp_ms; float noise_dbm; };
    std::vector<NoiseFloorSample> noise_window_;  ///< Up to 3600 s of samples

    // Accumulated metrics (for database update)
    struct AccumulatedMetrics {
        uint32_t frequency_hz;
        std::string remote_station;
        float avg_snr_db;
        float avg_ber;
        float avg_sinad_db;
        float avg_multipath_score;
        float avg_noise_floor_dbm;
        int total_fec_errors;
        int total_words;
    };
    AccumulatedMetrics accumulated_;
};

// ─── Table A-XIII / A-XIV : CMD LQA Word encoding ───────────────────────────

/// 5-bit BER code sent when no measurement is available (BE=11111).
static constexpr uint8_t kBerLqaNoValue    = 31u;
/// 5-bit SINAD code sent when no measurement is available (SN=11111).
static constexpr uint8_t kSinadLqaNoValue  = 31u;
/// 3-bit MP code sent when multipath is not measured (MP=111).
static constexpr uint8_t kMpLqaNotMeasured =  7u;

/**
 * @brief Map BerAccumulator score [0–48] to Table A-XIII 5-bit code [0–30]
 *
 * MIL-STD-188-141B Table A-XIII: average 2/3-vote counts 0–29 map directly
 * to codes 0–29; any count ≥ 30 → code 30 (11110 = "0.3 or more" BER).
 * Code 31 (11111 = "no value") is NOT returned here — the caller must set
 * kBerLqaNoValue when the BerAccumulator has no words (word_count() == 0).
 *
 * @param ber_score BerAccumulator::ber_score() result (0–48)
 * @return 5-bit code 0–30
 */
uint8_t ber_score_to_lqa_code(uint8_t ber_score);

/**
 * @brief Packed fields for a CMD LQA word (MIL-STD-188-141B Table A-XIV)
 */
struct LQACmdPayload {
    uint8_t ber   = kBerLqaNoValue;    ///< 5-bit BER code 0–30; 31 = no value
    uint8_t sinad = kSinadLqaNoValue;  ///< 5-bit SINAD code 0–30 dB; 31 = no value
    uint8_t mp    = kMpLqaNotMeasured; ///< 3-bit MP code 0–6 ms; 7 = not measured
    bool    ka1   = false;             ///< true → request LQA report from called station
};

/**
 * @brief Encode a CMD LQA payload into a 24-bit ALE word (Table A-XIV)
 *
 * Bit layout (bit 23 = MSB, bit 0 = LSB):
 *   [23:21]  CMD preamble  = 110
 *   [20:14]  'a' character = 1100001 (ALE "analysis")
 *   [13]     KA1
 *   [12:10]  MP[2:0]
 *   [9:5]    SINAD[4:0]
 *   [4:0]    BER[4:0]
 *
 * @param p Payload fields
 * @return 24-bit ALE word
 */
uint32_t encode_lqa_cmd(const LQACmdPayload& p);

/**
 * @brief Decode a 24-bit ALE word into a CMD LQA payload (Table A-XIV)
 *
 * Does NOT verify the CMD preamble or 'a' character — the caller is
 * responsible for word-type checking before calling.
 *
 * @param word24 24-bit received ALE word
 * @return Decoded payload fields
 */
LQACmdPayload decode_lqa_cmd(uint32_t word24);

/**
 * @brief Elapsed time → 3-bit age code per Table VI (§5.4.4 MIL-STD-187-721D).
 *
 * Code | Elapsed time
 * -----|------------------
 *  0   | 0 – 15 min
 *  1   | 15 – 30 min
 *  2   | 30 – 60 min
 *  3   | 1 – 4 h
 *  4   | 4 – 8 h
 *  5   | 8 – 16 h
 *  6   | 16 – 25 h
 *  7   | >25 h or unknown
 *
 * When now_ms < last_contact_ms (wrap / stale), code 7 is returned.
 *
 * @param last_contact_ms  Timestamp of last contact (ms, same clock as now_ms)
 * @param now_ms           Current time (ms)
 * @return 3-bit age code [0-7]
 */
uint8_t lqa_age_code(uint32_t last_contact_ms, uint32_t now_ms);

/**
 * @brief Multipath delay → 3-bit LQA code per MIL-STD-188-141B A.5.4.1.
 *
 * delay_ms | code
 * ---------|------
 *  < 0     |  0  (treated as 0 ms)
 *  0–1 ms  |  0–1
 *  …       |  …
 *  6 ms    |  6
 *  > 6 ms  |  7  (saturation)
 *
 * Codes 0–6 reflect the floored integer millisecond value.
 * Code 7 = "> 6 ms" per spec.
 *
 * @param delay_ms  Estimated multipath delay in milliseconds (≥ 0)
 * @return 3-bit code [0-7]
 */
uint8_t multipath_delay_to_lqa_code(float delay_ms);

} // namespace ale
