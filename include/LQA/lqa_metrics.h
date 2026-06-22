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

} // namespace ale
