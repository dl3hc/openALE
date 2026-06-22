/**
 * @file test_lqa_metrics.cpp
 * @brief Unit tests for LQA Metrics Collector
 */

#include "LQA/lqa_metrics.h"
#include "LQA/lqa_database.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

using namespace ale;

void test_metrics_creation() {
    std::cout << "Test: Metrics collector creation..." << std::endl;
    
    LQAMetrics metrics;
    assert(metrics.get_sample_count() == 0);
    
    std::cout << "  PASS" << std::endl;
}

void test_add_sample() {
    std::cout << "Test: Add metrics sample..." << std::endl;
    
    LQAMetrics metrics;
    
    MetricsSample sample;
    sample.snr_db = 20.0f;
    sample.signal_power_dbm = -50.0f;
    sample.noise_power_dbm = -70.0f;
    sample.fec_errors_corrected = 1;
    sample.decode_success = true;
    
    metrics.add_sample(sample, 7073000, "REMOTE");
    
    assert(metrics.get_sample_count() == 1);
    
    std::cout << "  PASS" << std::endl;
}

void test_averaging_window() {
    std::cout << "Test: Averaging window..." << std::endl;
    
    LQADatabase db;
    LQAMetrics metrics(&db);
    
    MetricsConfig config;
    config.averaging_window = 5;
    metrics.set_config(config);
    
    // Add 5 samples (fills window)
    for (int i = 0; i < 5; i++) {
        MetricsSample sample;
        sample.snr_db = 20.0f + i;  // 20, 21, 22, 23, 24
        sample.fec_errors_corrected = 1;
        metrics.add_sample(sample, 7073000, "REMOTE");
    }
    
    // Window should have rolled over (keeping 1 sample)
    assert(metrics.get_sample_count() == 1);
    
    // Database should have entry
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(entry->total_words == 5);
    
    std::cout << "  PASS" << std::endl;
}

void test_ber_estimation() {
    std::cout << "Test: BER estimation..." << std::endl;
    
    LQAMetrics metrics;
    
    // Perfect reception (no errors)
    float ber1 = metrics.estimate_ber(0, 100);
    assert(ber1 == 0.0f);
    
    // Some errors
    float ber2 = metrics.estimate_ber(10, 100);
    assert(ber2 > 0.0f);
    assert(ber2 < 1.0f);
    
    // High error rate
    float ber3 = metrics.estimate_ber(50, 100);
    assert(ber3 > ber2);
    
    std::cout << "  BER estimates: " << ber1 << ", " << ber2 << ", " << ber3 << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_sinad_calculation() {
    std::cout << "Test: SINAD calculation..." << std::endl;
    
    LQAMetrics metrics;
    
    // High SNR should give high SINAD
    float sinad1 = metrics.calculate_sinad(30.0f, -30.0f);
    assert(sinad1 > 20.0f);
    
    // Low SNR should give lower SINAD
    float sinad2 = metrics.calculate_sinad(10.0f, -30.0f);
    assert(sinad2 < sinad1);
    
    std::cout << "  SINAD values: " << sinad1 << ", " << sinad2 << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_multipath_detection() {
    std::cout << "Test: Multipath detection..." << std::endl;
    
    LQAMetrics metrics;
    
    // No multipath (stable signal)
    std::vector<float> stable_signal = {-50.0f, -50.1f, -49.9f, -50.0f, -50.1f};
    float mp1 = metrics.detect_multipath(stable_signal);
    assert(mp1 < 0.2f);  // Low multipath score
    
    // Multipath (varying signal)
    std::vector<float> fading_signal = {-50.0f, -45.0f, -55.0f, -48.0f, -52.0f};
    float mp2 = metrics.detect_multipath(fading_signal);
    assert(mp2 > mp1);  // Higher multipath score
    
    std::cout << "  Multipath scores: " << mp1 << ", " << mp2 << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_noise_floor_measurement() {
    std::cout << "Test: Noise floor measurement..." << std::endl;
    
    LQAMetrics metrics;
    
    std::vector<float> noise_samples = {-120.0f, -118.0f, -122.0f, -119.0f};
    float noise_floor = metrics.measure_noise_floor(noise_samples);
    
    // Should return minimum
    assert(std::abs(noise_floor + 122.0f) < 0.1f);
    
    std::cout << "  Noise floor: " << noise_floor << " dBm" << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_database_integration() {
    std::cout << "Test: Database integration..." << std::endl;
    
    LQADatabase db;
    LQAMetrics metrics(&db);
    
    MetricsConfig config;
    config.averaging_window = 3;
    config.enable_sinad = true;
    config.enable_multipath = true;
    metrics.set_config(config);
    
    // Add samples to trigger database update
    for (int i = 0; i < 3; i++) {
        MetricsSample sample;
        sample.snr_db = 22.0f;
        sample.signal_power_dbm = -50.0f;
        sample.noise_power_dbm = -72.0f;
        sample.fec_errors_corrected = 1;
        sample.decode_success = true;
        
        metrics.add_sample(sample, 7073000, "REMOTE");
    }
    
    // Database should have entry with extended metrics
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(std::abs(entry->snr_db - 22.0f) < 0.5f);
    assert(entry->total_words == 3);
    assert(entry->sinad_db > 0.0f);
    
    std::cout << "  Database entry - SNR: " << entry->snr_db 
              << ", SINAD: " << entry->sinad_db
              << ", Score: " << entry->score << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_averaged_sample() {
    std::cout << "Test: Averaged sample..." << std::endl;
    
    LQAMetrics metrics;
    
    // Add multiple samples
    for (int i = 0; i < 5; i++) {
        MetricsSample sample;
        sample.snr_db = 20.0f + i;  // 20, 21, 22, 23, 24
        sample.signal_power_dbm = -50.0f - i;
        metrics.add_sample(sample, 7073000, "REMOTE");
    }
    
    // Get average
    auto avg = metrics.get_averaged_sample();
    
    // Average SNR should be around 22.0 (middle value)
    assert(std::abs(avg.snr_db - 22.0f) < 1.0f);
    
    std::cout << "  Average SNR: " << avg.snr_db << " dB" << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_reset() {
    std::cout << "Test: Reset metrics..." << std::endl;
    
    LQAMetrics metrics;
    
    // Add samples
    for (int i = 0; i < 5; i++) {
        MetricsSample sample;
        sample.snr_db = 20.0f;
        metrics.add_sample(sample, 7073000, "REMOTE");
    }
    
    assert(metrics.get_sample_count() > 0);
    
    // Reset
    metrics.reset();
    assert(metrics.get_sample_count() == 0);
    
    std::cout << "  PASS" << std::endl;
}

void test_multiple_frequencies() {
    std::cout << "Test: Multiple frequencies..." << std::endl;
    
    LQADatabase db;
    LQAMetrics metrics(&db);
    
    MetricsConfig config;
    config.averaging_window = 2;
    metrics.set_config(config);
    
    // Add samples for different frequencies
    for (int i = 0; i < 2; i++) {
        MetricsSample sample;
        sample.snr_db = 22.0f;
        sample.fec_errors_corrected = 1;
        metrics.add_sample(sample, 7073000, "REMOTE");
    }
    
    for (int i = 0; i < 2; i++) {
        MetricsSample sample;
        sample.snr_db = 18.0f;
        sample.fec_errors_corrected = 2;
        metrics.add_sample(sample, 10142000, "REMOTE");
    }
    
    // Database should have entries for both frequencies
    auto entry1 = db.get_entry(7073000, "REMOTE");
    auto entry2 = db.get_entry(10142000, "REMOTE");
    
    assert(entry1 != nullptr);
    assert(entry2 != nullptr);
    assert(entry1->snr_db > entry2->snr_db);  // Different SNR
    
    std::cout << "  PASS" << std::endl;
}

void test_configuration() {
    std::cout << "Test: Configuration..." << std::endl;

    LQAMetrics metrics;

    MetricsConfig config;
    config.enable_sinad = false;
    config.enable_multipath = false;
    config.averaging_window = 20;
    config.multipath_threshold_db = 5.0f;

    metrics.set_config(config);

    auto retrieved = metrics.get_config();
    assert(retrieved.enable_sinad == false);
    assert(retrieved.enable_multipath == false);
    assert(retrieved.averaging_window == 20);
    assert(std::abs(retrieved.multipath_threshold_db - 5.0f) < 0.1f);

    std::cout << "  PASS" << std::endl;
}

// --- AC-CHAN-002-001: BerAccumulator tests (REQ-CHAN-011, REQ-CHAN-012) ---

// AC-CHAN-002-001 / REQ-CHAN-011: BER=0 when all votes are unanimous and no Golay errors
void test_ber_accumulator_perfect_reception() {
    std::cout << "Test: BerAccumulator - perfect reception (all unanimous) -> BER=0..." << std::endl;

    BerAccumulator acc;
    // 5 words, each with 0 non-unanimous bits and no uncorrectable errors
    for (int i = 0; i < 5; ++i) {
        acc.add_word(0, false);
    }

    uint8_t ber = acc.ber_score();
    assert(ber == 0);
    assert(acc.word_count() == 5);

    std::cout << "  BER=" << (int)ber << "  PASS" << std::endl;
}

// AC-CHAN-002-001 / REQ-CHAN-011: BER=48 when all 48 bits are non-unanimous (worst case)
void test_ber_accumulator_worst_case() {
    std::cout << "Test: BerAccumulator - worst case (48 non-unanimous per word) -> BER=48..." << std::endl;

    BerAccumulator acc;
    for (int i = 0; i < 4; ++i) {
        acc.add_word(48, false);
    }

    uint8_t ber = acc.ber_score();
    assert(ber == 48);

    std::cout << "  BER=" << (int)ber << "  PASS" << std::endl;
}

// AC-CHAN-002-001 / REQ-CHAN-012: Golay uncorrectable -> penalty of 48 regardless of non_unanimous
void test_ber_accumulator_golay_uncorrectable_penalty() {
    std::cout << "Test: BerAccumulator - Golay uncorrectable adds 48 penalty (REQ-CHAN-012)..." << std::endl;

    BerAccumulator acc;
    // Word with uncorrectable errors: non_unanimous ignored, 48 is added
    acc.add_word(5, true);   // non_unanimous=5 is discarded; 48 is added

    uint8_t ber = acc.ber_score();
    assert(ber == 48);

    std::cout << "  BER=" << (int)ber << "  PASS" << std::endl;
}

// AC-CHAN-002-001 / REQ-CHAN-012: Linear average over mixed words
void test_ber_accumulator_averaging() {
    std::cout << "Test: BerAccumulator - linear average over frame (REQ-CHAN-012)..." << std::endl;

    BerAccumulator acc;
    // Word 1: 0 non-unanimous, no uncorrectable -> contributes 0
    acc.add_word(0, false);
    // Word 2: 24 non-unanimous, no uncorrectable -> contributes 24
    acc.add_word(24, false);
    // Word 3: uncorrectable -> contributes 48
    acc.add_word(0, true);
    // running_sum = 0+24+48 = 72, word_count = 3, ber = 72/3 = 24
    uint8_t ber = acc.ber_score();
    assert(ber == 24);

    std::cout << "  BER=" << (int)ber << " (expected 24)  PASS" << std::endl;
}

// AC-CHAN-002-001 / REQ-CHAN-011: BER range is always 0-48
void test_ber_accumulator_range_clamped() {
    std::cout << "Test: BerAccumulator - score clamped to 0-48..." << std::endl;

    BerAccumulator acc;
    // Saturate with max-penalty words
    for (int i = 0; i < 10; ++i) {
        acc.add_word(48, false);
    }
    uint8_t ber = acc.ber_score();
    assert(ber <= 48);
    assert(ber == 48);

    std::cout << "  BER=" << (int)ber << "  PASS" << std::endl;
}

// AC-CHAN-002-001: Reset clears accumulator state
void test_ber_accumulator_reset() {
    std::cout << "Test: BerAccumulator - reset clears state..." << std::endl;

    BerAccumulator acc;
    acc.add_word(10, false);
    acc.add_word(20, false);
    assert(acc.word_count() == 2);

    acc.reset();
    assert(acc.word_count() == 0);
    assert(acc.ber_score() == 0);  // no words -> 0

    std::cout << "  PASS" << std::endl;
}

// --- AC-CHAN-002-002: sinad_to_lqa_code range [0, 30] (REQ-CHAN-013) ---

// Negative SINAD → code 0
void test_sinad_code_negative_clamped_to_zero() {
    std::cout << "Test: sinad_to_lqa_code - negative dB clamped to 0..." << std::endl;

    LQAMetrics metrics;
    assert(metrics.sinad_to_lqa_code(-5.0f) == 0);
    assert(metrics.sinad_to_lqa_code(-100.0f) == 0);

    std::cout << "  PASS" << std::endl;
}

// 0 dB → code 0
void test_sinad_code_zero_db() {
    std::cout << "Test: sinad_to_lqa_code - 0 dB -> code 0..." << std::endl;

    LQAMetrics metrics;
    assert(metrics.sinad_to_lqa_code(0.0f) == 0);

    std::cout << "  PASS" << std::endl;
}

// 30 dB → code 30
void test_sinad_code_thirty_db() {
    std::cout << "Test: sinad_to_lqa_code - 30 dB -> code 30..." << std::endl;

    LQAMetrics metrics;
    assert(metrics.sinad_to_lqa_code(30.0f) == 30);

    std::cout << "  PASS" << std::endl;
}

// Above 30 dB → code 30
void test_sinad_code_above_max_clamped_to_thirty() {
    std::cout << "Test: sinad_to_lqa_code - values >30 dB clamped to 30..." << std::endl;

    LQAMetrics metrics;
    assert(metrics.sinad_to_lqa_code(35.0f) == 30);
    assert(metrics.sinad_to_lqa_code(100.0f) == 30);

    std::cout << "  PASS" << std::endl;
}

// Intermediate values are rounded correctly
void test_sinad_code_rounding() {
    std::cout << "Test: sinad_to_lqa_code - intermediate values rounded to nearest integer..." << std::endl;

    LQAMetrics metrics;
    assert(metrics.sinad_to_lqa_code(15.0f) == 15);
    assert(metrics.sinad_to_lqa_code(15.4f) == 15);   // rounds down
    assert(metrics.sinad_to_lqa_code(15.5f) == 16);   // rounds up
    assert(metrics.sinad_to_lqa_code(15.7f) == 16);   // rounds up
    assert(metrics.sinad_to_lqa_code(1.0f)  == 1);
    assert(metrics.sinad_to_lqa_code(29.9f) == 30);

    std::cout << "  PASS" << std::endl;
}

// Exhaustive sweep: no code ever outside [0, 30]
void test_sinad_code_range_never_outside_bounds() {
    std::cout << "Test: sinad_to_lqa_code - no code outside [0,30] for any input..." << std::endl;

    LQAMetrics metrics;
    // Sweep from -10 to +40 in 0.1 steps
    for (float db = -10.0f; db <= 40.0f; db += 0.1f) {
        assert(metrics.sinad_to_lqa_code(db) <= 30);
    }

    std::cout << "  PASS" << std::endl;
}

// AC-CHAN-002-001: MetricsSample carries non_unanimous_count and golay_uncorrectable fields
void test_metrics_sample_ber_fields() {
    std::cout << "Test: MetricsSample has non_unanimous_count and golay_uncorrectable fields..." << std::endl;

    MetricsSample s;
    // Default values
    assert(s.non_unanimous_count == 0);
    assert(s.golay_uncorrectable == false);

    s.non_unanimous_count = 12;
    s.golay_uncorrectable = true;
    assert(s.non_unanimous_count == 12);
    assert(s.golay_uncorrectable == true);

    std::cout << "  PASS" << std::endl;
}

int main() {
    std::cout << "=== LQA Metrics Tests ===" << std::endl;

    test_metrics_creation();
    test_add_sample();
    test_averaging_window();
    test_ber_estimation();
    test_sinad_calculation();
    test_multipath_detection();
    test_noise_floor_measurement();
    test_database_integration();
    test_averaged_sample();
    test_reset();
    test_multiple_frequencies();
    test_configuration();

    // AC-CHAN-002-001 BerAccumulator tests
    test_ber_accumulator_perfect_reception();
    test_ber_accumulator_worst_case();
    test_ber_accumulator_golay_uncorrectable_penalty();
    test_ber_accumulator_averaging();
    test_ber_accumulator_range_clamped();
    test_ber_accumulator_reset();
    test_metrics_sample_ber_fields();

    // AC-CHAN-002-002 sinad_to_lqa_code tests
    test_sinad_code_negative_clamped_to_zero();
    test_sinad_code_zero_db();
    test_sinad_code_thirty_db();
    test_sinad_code_above_max_clamped_to_thirty();
    test_sinad_code_rounding();
    test_sinad_code_range_never_outside_bounds();

    std::cout << "\n=== All LQA Metrics Tests Passed ===" << std::endl;
    return 0;
}
