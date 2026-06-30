/**
 * @file lqa_metrics.cpp
 * @brief Implementation of LQA Metrics Collector
 */

#include "LQA/lqa_metrics.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace ale {

LQAMetrics::LQAMetrics(LQADatabase* database)
    : database_(database) {
    accumulated_.frequency_hz = 0;
    accumulated_.total_fec_errors = 0;
    accumulated_.total_words = 0;
}

void LQAMetrics::set_config(const MetricsConfig& config) {
    config_ = config;
}

MetricsConfig LQAMetrics::get_config() const {
    return config_;
}

void LQAMetrics::set_database(LQADatabase* database) {
    database_ = database;
}

void LQAMetrics::add_sample(const MetricsSample& sample,
                           uint32_t frequency_hz,
                           const std::string& remote_station) {
    // Rolling 60-min noise window (AC-CHAN-004-001)
    if (sample.noise_power_dbm > -120.0f) {
        const uint32_t now_ms = (sample.timestamp_ms != 0) ? sample.timestamp_ms : 0u;
        noise_window_.push_back({now_ms, sample.noise_power_dbm});
        // Prune entries older than 3600 s when now_ms is known and non-zero
        if (now_ms > 3600000u) {
            const uint32_t cutoff = now_ms - 3600000u;
            noise_window_.erase(
                std::remove_if(noise_window_.begin(), noise_window_.end(),
                    [cutoff](const NoiseFloorSample& s){ return s.timestamp_ms < cutoff; }),
                noise_window_.end());
        }
    }

    // A.5.4.1.1: accumulate non-unanimous vote count per received word
    ber_acc_.add_word(sample.non_unanimous_count, sample.golay_uncorrectable);

    // Add to averaging window
    samples_.push_back(sample);

    // Store context for database update
    accumulated_.frequency_hz = frequency_hz;
    accumulated_.remote_station = remote_station;
    accumulated_.total_fec_errors += sample.fec_errors_corrected;
    accumulated_.total_words++;

    // When window is full, compute averages and update database
    if (samples_.size() >= config_.averaging_window) {
        update_database(frequency_hz, remote_station);

        // Keep last sample for continuity
        MetricsSample last = samples_.back();
        samples_.clear();
        samples_.push_back(last);
    }
}

void LQAMetrics::update_database(uint32_t frequency_hz, 
                                const std::string& remote_station) {
    if (!database_ || samples_.empty()) {
        return;
    }
    
    // Compute averaged metrics
    MetricsSample avg = compute_average();
    
    // A.5.4.1.1: BER = averaged non-unanimous vote count over all received words (0–48)
    float ber = static_cast<float>(ber_acc_.ber_score());
    
    // A.5.4.1.2: SINAD = time-averaged Goertzel measurement over signal duration.
    // avg.sinad_db is already averaged across all received words (per compute_average()).
    // Clamp to LQA code [0,30] before storing (REQ-CHAN-013).
    float sinad_code = static_cast<float>(sinad_to_lqa_code(avg.sinad_db));

    // Detect multipath if enabled
    float multipath_score = 0.0f;
    if (config_.enable_multipath) {
        std::vector<float> signal_samples;
        for (const auto& s : samples_) {
            signal_samples.push_back(s.signal_power_dbm);
        }
        multipath_score = detect_multipath(signal_samples);
    }
    
    // Measure noise floor
    std::vector<float> noise_samples;
    for (const auto& s : samples_) {
        noise_samples.push_back(s.noise_power_dbm);
    }
    float noise_floor = measure_noise_floor(noise_samples);
    
    // Update database with extended metrics
    database_->update_entry_extended(
        frequency_hz,
        remote_station,
        avg.snr_db,
        ber,
        sinad_code,
        multipath_score,
        noise_floor,
        accumulated_.total_fec_errors,
        accumulated_.total_words,
        avg.timestamp_ms
    );
    
    // Reset accumulators for next window
    accumulated_.total_fec_errors = 0;
    accumulated_.total_words = 0;
    ber_acc_.reset();
}

MetricsSample LQAMetrics::compute_average() const {
    if (samples_.empty()) {
        return MetricsSample();
    }
    
    MetricsSample avg;
    float sum_snr = 0.0f;
    float sum_sinad = 0.0f;
    float sum_signal = 0.0f;
    float sum_noise = 0.0f;
    float sum_multipath = 0.0f;

    for (const auto& s : samples_) {
        sum_snr += s.snr_db;
        sum_sinad += s.sinad_db;
        sum_signal += s.signal_power_dbm;
        sum_noise += s.noise_power_dbm;
        sum_multipath += s.multipath_delay_ms;
    }

    size_t n = samples_.size();
    avg.snr_db = sum_snr / n;
    avg.sinad_db = sum_sinad / n;
    avg.signal_power_dbm = sum_signal / n;
    avg.noise_power_dbm = sum_noise / n;
    avg.multipath_delay_ms = sum_multipath / n;
    
    // Use most recent timestamp
    avg.timestamp_ms = samples_.back().timestamp_ms;
    
    return avg;
}

float LQAMetrics::calculate_sinad(float snr_db, float distortion_db) const {
    // SINAD = 10 * log10((S+N+D)/(N+D))
    // Where S = signal, N = noise, D = distortion
    
    // Convert SNR to linear
    float snr_linear = std::pow(10.0f, snr_db / 10.0f);
    
    // Assume distortion is relative to signal
    float distortion_linear = std::pow(10.0f, distortion_db / 10.0f);
    
    // SINAD calculation
    // S+N+D = S + N + D (where N is 1 relative to signal at SNR)
    // N+D = 1 + D (relative to signal)
    float signal_plus_noise_plus_distortion = snr_linear + 1.0f + distortion_linear;
    float noise_plus_distortion = 1.0f + distortion_linear;
    
    float sinad_linear = signal_plus_noise_plus_distortion / noise_plus_distortion;
    float sinad_db = 10.0f * std::log10(sinad_linear);
    
    return sinad_db;
}

uint8_t LQAMetrics::sinad_to_lqa_code(float sinad_db) const {
    if (sinad_db <= 0.0f) return 0u;
    if (sinad_db >= 30.0f) return 30u;
    return static_cast<uint8_t>(std::round(sinad_db));
}

float LQAMetrics::estimate_ber(int errors_corrected, int total_words) const {
    if (total_words == 0) {
        return 0.0f;
    }
    
    // Golay (24,12) can correct up to 3 bit errors per 24-bit codeword
    // Each ALE word has 24 bits
    // BER estimation: errors / (total_bits)
    
    // Conservative estimate: assume each correction was 1 bit error
    // (actual could be 1-3 bits)
    float total_bits = total_words * 24.0f;
    float estimated_bit_errors = static_cast<float>(errors_corrected);
    
    float ber = estimated_bit_errors / total_bits;
    
    // Clamp to valid range
    return std::min(1.0f, std::max(0.0f, ber));
}

float LQAMetrics::detect_multipath(const std::vector<float>& samples) const {
    if (samples.size() < 3) {
        return 0.0f;
    }
    
    // Multipath detection via signal variance
    // High variance in signal power indicates multipath fading
    
    // Calculate mean
    float mean = std::accumulate(samples.begin(), samples.end(), 0.0f) / samples.size();
    
    // Calculate variance
    float variance = 0.0f;
    for (float sample : samples) {
        float diff = sample - mean;
        variance += diff * diff;
    }
    variance /= samples.size();
    
    // Standard deviation
    float std_dev = std::sqrt(variance);
    
    // Normalize to 0-1 scale
    // Threshold: > 3 dB std dev indicates significant multipath
    float multipath_score = std_dev / config_.multipath_threshold_db;
    
    // Clamp to 0-1
    return std::min(1.0f, std::max(0.0f, multipath_score));
}

float LQAMetrics::measure_noise_floor(const std::vector<float>& samples) const {
    if (samples.empty()) {
        return -120.0f;  // Default very low noise floor
    }
    
    // Noise floor is the minimum power level observed
    float min_power = *std::min_element(samples.begin(), samples.end());
    
    return min_power;
}

MetricsSample LQAMetrics::get_averaged_sample() const {
    return compute_average();
}

void LQAMetrics::reset() {
    samples_.clear();
    accumulated_.total_fec_errors = 0;
    accumulated_.total_words = 0;
    ber_acc_.reset();
}

size_t LQAMetrics::get_sample_count() const {
    return samples_.size();
}

LQAMetrics::NoiseFloorStats LQAMetrics::get_noise_floor_stats(uint32_t now_ms) const {
    NoiseFloorStats stats;
    if (noise_window_.empty()) return stats;

    // Filter to last 3600 s when now_ms is provided and non-zero
    float sum = 0.0f;
    float max_val = -120.0f;
    size_t count = 0;
    const uint32_t cutoff = (now_ms > 3600000u) ? (now_ms - 3600000u) : 0u;
    for (const auto& s : noise_window_) {
        if (now_ms != 0 && s.timestamp_ms < cutoff) continue;
        if (s.noise_dbm > max_val) max_val = s.noise_dbm;
        sum += s.noise_dbm;
        ++count;
    }
    if (count == 0) return stats;
    stats.max_dbm  = max_val;
    stats.mean_dbm = sum / static_cast<float>(count);
    return stats;
}

// ─── Table A-XIII / A-XIV free functions ─────────────────────────────────────

uint8_t ber_score_to_lqa_code(uint8_t ber_score) {
    // Table A-XIII: votes 0–29 → code 0–29; votes ≥ 30 → code 30 (11110).
    return ber_score >= 30u ? 30u : ber_score;
}

uint32_t encode_lqa_cmd(const LQACmdPayload& p) {
    // Table A-XIV bit layout (bit 23 = MSB):
    //   [23:21] CMD preamble 110
    //   [20:14] 'a' = 1100001 (0x61)
    //   [13]    KA1
    //   [12:10] MP[2:0]
    //   [9:5]   SINAD[4:0]
    //   [4:0]   BER[4:0]
    constexpr uint32_t kCmdPreamble = 0b110u;
    constexpr uint32_t kLqaChar     = 0b1100001u;   // ASCII 'a'
    return (kCmdPreamble                   << 21)
         | (kLqaChar                       << 14)
         | ((p.ka1 ? 1u : 0u)             << 13)
         | (static_cast<uint32_t>(p.mp    & 0x07u) << 10)
         | (static_cast<uint32_t>(p.sinad & 0x1Fu) <<  5)
         |  static_cast<uint32_t>(p.ber   & 0x1Fu);
}

LQACmdPayload decode_lqa_cmd(uint32_t word24) {
    LQACmdPayload p;
    p.ka1   = ((word24 >> 13) & 0x01u) != 0;
    p.mp    =  (word24 >> 10) & 0x07u;
    p.sinad =  (word24 >>  5) & 0x1Fu;
    p.ber   =   word24        & 0x1Fu;
    return p;
}

uint8_t lqa_age_code(uint32_t last_contact_ms, uint32_t now_ms) {
    if (last_contact_ms == 0 || now_ms < last_contact_ms) return 7u;
    const uint32_t elapsed = now_ms - last_contact_ms;
    if (elapsed <   900000u) return 0u;  //  0–15 min
    if (elapsed <  1800000u) return 1u;  // 15–30 min
    if (elapsed <  3600000u) return 2u;  // 30–60 min
    if (elapsed < 14400000u) return 3u;  //  1–4 h
    if (elapsed < 28800000u) return 4u;  //  4–8 h
    if (elapsed < 57600000u) return 5u;  //  8–16 h
    if (elapsed < 90000000u) return 6u;  // 16–25 h
    return 7u;                            // >25 h or unknown
}

uint8_t multipath_delay_to_lqa_code(float delay_ms) {
    if (delay_ms < 0.0f) return 0u;
    if (delay_ms > 6.0f) return 6u;                      // "≥ 6 ms" → saturation code 6 (A.5.4.2.3)
    return static_cast<uint8_t>(std::round(delay_ms));   // round to nearest ms (A.5.4.2.3)
}

} // namespace ale
