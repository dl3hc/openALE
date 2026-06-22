/**
 * @file test_lqa_database.cpp
 * @brief Unit tests for LQA Database
 */

#include "LQA/lqa_database.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>

using namespace ale;

void test_database_creation() {
    std::cout << "Test: Database creation..." << std::endl;
    
    LQADatabase db;
    assert(db.get_entry_count() == 0);
    
    std::cout << "  PASS" << std::endl;
}

void test_basic_entry_update() {
    std::cout << "Test: Basic entry update..." << std::endl;
    
    LQADatabase db;
    
    // Add entry
    db.update_entry(7073000, "REMOTE", 20.0f, 0.01f, 2, 100);
    
    assert(db.get_entry_count() == 1);
    
    // Retrieve entry
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(entry->frequency_hz == 7073000);
    assert(entry->remote_station == "REMOTE");
    assert(std::abs(entry->snr_db - 20.0f) < 0.1f);
    assert(std::abs(entry->ber - 0.01f) < 0.001f);
    assert(entry->fec_errors == 2);
    assert(entry->total_words == 100);
    
    std::cout << "  PASS" << std::endl;
}

void test_time_weighted_averaging() {
    std::cout << "Test: Time-weighted averaging..." << std::endl;
    
    LQADatabase db;
    
    // First update
    db.update_entry(7073000, "REMOTE", 20.0f, 0.01f, 1, 10);
    
    auto entry1 = db.get_entry(7073000, "REMOTE");
    float snr1 = entry1->snr_db;
    
    // Second update (should average with first)
    db.update_entry(7073000, "REMOTE", 25.0f, 0.005f, 1, 10);
    
    auto entry2 = db.get_entry(7073000, "REMOTE");
    float snr2 = entry2->snr_db;
    
    // SNR should be weighted average (closer to second value due to decay)
    assert(snr2 > snr1);
    assert(snr2 < 25.0f);  // But not fully 25 due to averaging
    
    // Errors and words should accumulate
    assert(entry2->fec_errors == 2);
    assert(entry2->total_words == 20);
    assert(entry2->sample_count == 2);
    
    std::cout << "  PASS" << std::endl;
}

void test_extended_metrics() {
    std::cout << "Test: Extended metrics update..." << std::endl;
    
    LQADatabase db;
    
    db.update_entry_extended(
        7073000, "REMOTE",
        22.0f,    // SNR
        0.001f,   // BER
        20.0f,    // SINAD
        0.3f,     // Multipath score
        -110.0f,  // Noise floor
        1, 50
    );
    
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(std::abs(entry->sinad_db - 20.0f) < 0.1f);
    assert(std::abs(entry->multipath_score - 0.3f) < 0.01f);
    assert(std::abs(entry->noise_floor_dbm + 110.0f) < 0.1f);
    
    std::cout << "  PASS" << std::endl;
}

void test_multiple_stations() {
    std::cout << "Test: Multiple stations..." << std::endl;
    
    LQADatabase db;
    
    db.update_entry(7073000, "ALFA", 22.0f, 0.001f, 1, 50);
    db.update_entry(7073000, "BRAVO", 18.0f, 0.01f, 2, 50);
    db.update_entry(10142000, "ALFA", 25.0f, 0.0005f, 0, 50);
    
    assert(db.get_entry_count() == 3);
    
    // Get entries for channel
    auto channel_entries = db.get_entries_for_channel(7073000);
    assert(channel_entries.size() == 2);
    
    // Get entries for station
    auto station_entries = db.get_entries_for_station("ALFA");
    assert(station_entries.size() == 2);
    
    std::cout << "  PASS" << std::endl;
}

void test_score_computation() {
    std::cout << "Test: Score computation..." << std::endl;
    
    LQADatabase db;
    LQAConfig config;
    config.snr_weight = 0.5f;
    config.success_weight = 0.3f;
    config.recency_weight = 0.2f;
    db.set_config(config);
    
    // High SNR, low BER = high score
    db.update_entry(7073000, "REMOTE", 28.0f, 0.001f, 0, 100);

    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(entry->score > 20.0f);            // Should be high quality
    assert(entry->score <= LQA_QUALITY_MAX); // Bounded to 30 (AC-GEN-001-002)

    std::cout << "  Score: " << entry->score << std::endl;
    std::cout << "  PASS" << std::endl;
}

// AC-GEN-001-002: the internal channel-quality scale is 0 (worst) .. 30 (best);
// no computed LQA value may leave [0, 30], and the 31 "unknown" sentinel is
// never produced by compute_score, even under saturating inputs.
void test_score_range_bounds() {
    std::cout << "Test: Score range bounds [0,30]..." << std::endl;

    LQADatabase db;
    LQAConfig config;  // default weights sum to 1.0
    db.set_config(config);

    // (1) Saturating "perfect" inputs: huge SNR, zero BER, fresh contact.
    db.update_entry(14100000, "BEST", 999.0f, 0.0f, 0, 1000);
    auto best = db.get_entry(14100000, "BEST");
    assert(best != nullptr);
    assert(best->score <= LQA_QUALITY_MAX);          // never exceeds 30
    assert(best->score < LQA_QUALITY_UNKNOWN);        // never reaches 31 sentinel
    assert(best->score >= LQA_QUALITY_MIN);

    // (2) Worst-case inputs: negative SNR, BER = 1.0 → score floored at 0.
    db.update_entry(14200000, "WORST", -50.0f, 1.0f, 99, 1000);
    auto worst = db.get_entry(14200000, "WORST");
    assert(worst != nullptr);
    assert(worst->score >= LQA_QUALITY_MIN);          // never below 0
    assert(worst->score <= LQA_QUALITY_MAX);

    std::cout << "  best=" << best->score << " worst=" << worst->score << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_prune_stale_entries() {
    std::cout << "Test: Prune stale entries..." << std::endl;
    
    LQADatabase db;
    LQAConfig config;
    config.max_age_ms = 100;  // 100ms max age
    db.set_config(config);
    
    // Add entry
    db.update_entry(7073000, "REMOTE", 20.0f, 0.01f, 1, 50);
    assert(db.get_entry_count() == 1);
    
    // Wait for entry to become stale
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Prune
    int removed = db.prune_stale_entries();
    assert(removed == 1);
    assert(db.get_entry_count() == 0);
    
    std::cout << "  PASS" << std::endl;
}

void test_save_and_load() {
    std::cout << "Test: Save and load database..." << std::endl;
    
    const std::string filepath = "test_lqa.db";
    
    // Create database with data
    {
        LQADatabase db;
        db.update_entry(7073000, "ALFA", 22.0f, 0.001f, 1, 50);
        db.update_entry(7073000, "BRAVO", 18.0f, 0.01f, 2, 50);
        db.update_entry(10142000, "CHARLIE", 25.0f, 0.0005f, 0, 50);
        
        assert(db.save_to_file(filepath));
    }
    
    // Load database
    {
        LQADatabase db;
        assert(db.load_from_file(filepath));
        
        assert(db.get_entry_count() == 3);
        
        auto entry = db.get_entry(7073000, "ALFA");
        assert(entry != nullptr);
        assert(entry->remote_station == "ALFA");
        assert(std::abs(entry->snr_db - 22.0f) < 0.1f);
    }
    
    // Clean up
    std::remove(filepath.c_str());
    
    std::cout << "  PASS" << std::endl;
}

void test_export_csv() {
    std::cout << "Test: Export to CSV..." << std::endl;
    
    const std::string filepath = "test_lqa.csv";
    
    LQADatabase db;
    db.update_entry(7073000, "ALFA", 22.0f, 0.001f, 1, 50);
    db.update_entry(10142000, "BRAVO", 18.0f, 0.01f, 2, 50);
    
    assert(db.export_to_csv(filepath));
    
    // Verify file exists and has content
    std::ifstream file(filepath);
    assert(file.is_open());
    
    std::string line;
    std::getline(file, line);  // Header
    assert(line.find("Frequency") != std::string::npos);
    
    std::getline(file, line);  // First entry
    assert(line.find("ALFA") != std::string::npos || line.find("BRAVO") != std::string::npos);
    
    file.close();
    
    // Clean up
    std::remove(filepath.c_str());
    
    std::cout << "  PASS" << std::endl;
}

void test_get_all_entries() {
    std::cout << "Test: Get all entries..." << std::endl;
    
    LQADatabase db;
    
    db.update_entry(7073000, "ALFA", 22.0f, 0.001f, 1, 50);
    db.update_entry(7073000, "BRAVO", 18.0f, 0.01f, 2, 50);
    db.update_entry(10142000, "CHARLIE", 25.0f, 0.0005f, 0, 50);
    
    auto all_entries = db.get_all_entries();
    assert(all_entries.size() == 3);
    
    // Verify all stations present
    bool found_alfa = false, found_bravo = false, found_charlie = false;
    for (const auto& entry : all_entries) {
        if (entry.remote_station == "ALFA") found_alfa = true;
        if (entry.remote_station == "BRAVO") found_bravo = true;
        if (entry.remote_station == "CHARLIE") found_charlie = true;
    }
    assert(found_alfa && found_bravo && found_charlie);
    
    std::cout << "  PASS" << std::endl;
}

void test_configuration() {
    std::cout << "Test: Configuration..." << std::endl;

    LQADatabase db;

    LQAConfig config;
    config.snr_weight = 0.6f;
    config.success_weight = 0.3f;
    config.recency_weight = 0.1f;
    config.max_age_ms = 600000;

    db.set_config(config);

    auto retrieved = db.get_config();
    assert(std::abs(retrieved.snr_weight - 0.6f) < 0.01f);
    assert(std::abs(retrieved.success_weight - 0.3f) < 0.01f);
    assert(std::abs(retrieved.recency_weight - 0.1f) < 0.01f);
    assert(retrieved.max_age_ms == 600000);

    std::cout << "  PASS" << std::endl;
}

// AC-GEN-006-003: LQA data must survive >= 1 hour without external power.
// Verified via: (a) kMinRetentionMs == 3600000, (b) default max_age_ms >= kMinRetentionMs,
// (c) save/load round-trip preserves timestamps so aging accumulates correctly across sessions.
void test_lqa_one_hour_retention_ac_gen_006_003() {
    std::cout << "Test: LQA 1-hour retention (AC-GEN-006-003)..." << std::endl;

    // (a) named constant must be exactly 1 hour
    static_assert(LQADatabase::kMinRetentionMs == 3600000u,
                  "kMinRetentionMs must be 3600000ms per REQ-GEN-018");

    // (b) default LQAConfig must retain entries for at least 1 hour
    assert(LQAConfig{}.max_age_ms >= LQADatabase::kMinRetentionMs);

    // (c) timestamps survive save/load so age accumulates correctly across power cycles
    const std::string filepath = "test_lqa_retention.db";
    const uint32_t ts = 1000000u;  // fixed contact timestamp (ms)

    {
        LQADatabase db;
        db.update_entry(7073000, "ALFA", 22.0f, 0.001f, 1, 50, ts);
        assert(db.save_to_file(filepath));
    }

    {
        LQADatabase db;
        assert(db.load_from_file(filepath));
        assert(db.get_entry_count() == 1);
        auto entry = db.get_entry(7073000, "ALFA");
        assert(entry != nullptr);
        // Named station → last_contact_ms must equal the original timestamp
        assert(entry->last_contact_ms == ts);
    }

    std::remove(filepath.c_str());
    std::cout << "  PASS" << std::endl;
}

// AC-GEN-006-002: kCapacity >= 4000; overflow evicts oldest entry.
void test_lqa_database_min_capacity_4000() {
    std::cout << "Test: LQA database min capacity 4000 (AC-GEN-006-002)..." << std::endl;

    // Verify compile-time constant
    static_assert(LQADatabase::kCapacity >= 4000,
                  "kCapacity must be at least 4000 per REQ-GEN-017");

    // Fill to capacity + 5 using unique (freq, station) pairs.
    // Use sequential timestamps so the oldest is deterministic.
    LQADatabase db;
    const size_t over = LQADatabase::kCapacity + 5;
    for (size_t i = 0; i < over; ++i) {
        uint32_t freq = static_cast<uint32_t>(1000000 + i);
        std::string station = "ST" + std::to_string(i);
        db.update_entry(freq, station, 15.0f, 0.01f, 0, 10,
                        static_cast<uint32_t>(i + 1));  // timestamp = i+1 ms
    }

    // Size must not exceed capacity
    assert(db.get_entry_count() <= LQADatabase::kCapacity);

    // The very first entry (timestamp=1, oldest) must have been evicted
    assert(db.get_entry(1000000, "ST0") == nullptr);

    // The most recently inserted entry must still be present
    uint32_t last_freq = static_cast<uint32_t>(1000000 + over - 1);
    std::string last_station = "ST" + std::to_string(over - 1);
    assert(db.get_entry(last_freq, last_station) != nullptr);

    std::cout << "  kCapacity=" << LQADatabase::kCapacity
              << " entry_count=" << db.get_entry_count() << std::endl;
    std::cout << "  PASS" << std::endl;
}

// ── Bilateral tests (AC-GEN-017-3, Figure A-27) ──────────────────────────────

// New entry must have bilateral fields at "no-data" sentinels (bilateral_sinad=31,
// bilateral_ber=31, bilateral_mp=7) and handshake_tried=false ("-" state).
void test_bilateral_defaults() {
    std::cout << "Test: Bilateral fields default to no-data sentinels (\"-\" state)..." << std::endl;

    LQADatabase db;
    db.update_entry(7073000, "ALFA", 20.0f, 0.01f, 0, 10);

    auto e = db.get_entry(7073000, "ALFA");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 31u);
    assert(e->bilateral_ber   == 31u);
    assert(e->bilateral_mp    ==  7u);
    assert(e->bilateral_handshake_tried == false);

    std::cout << "  PASS" << std::endl;
}

// update_bilateral() stores all three codes and sets handshake_tried=true.
void test_update_bilateral() {
    std::cout << "Test: update_bilateral() stores SINAD/BER/MP codes (AC-GEN-017-3)..." << std::endl;

    LQADatabase db;
    db.update_entry(7073000, "ALFA", 20.0f, 0.01f, 0, 10);
    db.update_bilateral(7073000, "ALFA", 12u, 5u, 3u);

    auto e = db.get_entry(7073000, "ALFA");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 12u);
    assert(e->bilateral_ber   ==  5u);
    assert(e->bilateral_mp    ==  3u);
    assert(e->bilateral_handshake_tried == true);

    std::cout << "  PASS" << std::endl;
}

// update_bilateral() creates a stub entry when none exists yet.
void test_update_bilateral_creates_entry() {
    std::cout << "Test: update_bilateral() creates stub entry when no FROM data exists..." << std::endl;

    LQADatabase db;
    assert(db.get_entry_count() == 0);
    db.update_bilateral(7073000, "BRAVO", 20u, 8u, 2u);

    assert(db.get_entry_count() == 1);
    auto e = db.get_entry(7073000, "BRAVO");
    assert(e != nullptr);
    assert(e->bilateral_sinad == 20u);
    assert(e->bilateral_handshake_tried == true);
    // FROM fields remain at defaults
    assert(e->snr_db == 0.0f);

    std::cout << "  PASS" << std::endl;
}

// mark_bilateral_attempted() sets flag but leaves codes at sentinels ("X" state).
void test_mark_bilateral_attempted() {
    std::cout << "Test: mark_bilateral_attempted() → \"X\" state (tried, no data)..." << std::endl;

    LQADatabase db;
    db.update_entry(7073000, "ALFA", 20.0f, 0.01f, 0, 10);
    db.mark_bilateral_attempted(7073000, "ALFA");

    auto e = db.get_entry(7073000, "ALFA");
    assert(e != nullptr);
    assert(e->bilateral_handshake_tried == true);   // "X"
    assert(e->bilateral_sinad == 31u);              // still no data
    assert(e->bilateral_ber   == 31u);
    assert(e->bilateral_mp    ==  7u);

    std::cout << "  PASS" << std::endl;
}

// Figure A-27: three distinct states per cell
void test_bilateral_x_vs_dash() {
    std::cout << "Test: bilateral states \"-\" / \"X\" / value correctly distinguished..." << std::endl;

    LQADatabase db;

    // "-": never tried
    db.update_entry(7073000, "S1", 20.0f, 0.01f, 0, 10);
    auto dash = db.get_entry(7073000, "S1");
    assert(!dash->bilateral_handshake_tried && dash->bilateral_sinad == 31u);

    // "X": tried but no CMD LQA received
    db.update_entry(7073000, "S2", 20.0f, 0.01f, 0, 10);
    db.mark_bilateral_attempted(7073000, "S2");
    auto x = db.get_entry(7073000, "S2");
    assert(x->bilateral_handshake_tried && x->bilateral_sinad == 31u);

    // valid value: received CMD LQA
    db.update_entry(7073000, "S3", 20.0f, 0.01f, 0, 10);
    db.update_bilateral(7073000, "S3", 15u, 8u, 2u);
    auto val = db.get_entry(7073000, "S3");
    assert(val->bilateral_handshake_tried && val->bilateral_sinad == 15u);

    std::cout << "  PASS" << std::endl;
}

// Bilateral fields survive save/load round-trip (format v2).
void test_bilateral_save_load() {
    std::cout << "Test: bilateral fields survive save/load (format v2)..." << std::endl;

    const std::string filepath = "test_lqa_bilateral.db";

    {
        LQADatabase db;
        db.update_entry(7073000, "ALFA", 20.0f, 0.01f, 0, 10);
        db.update_bilateral(7073000, "ALFA", 18u, 6u, 3u);
        db.update_entry(10142000, "BRAVO", 22.0f, 0.005f, 0, 10);
        db.mark_bilateral_attempted(10142000, "BRAVO");  // "X" state
        assert(db.save_to_file(filepath));
    }

    {
        LQADatabase db;
        assert(db.load_from_file(filepath));
        assert(db.get_entry_count() == 2);

        auto alfa = db.get_entry(7073000, "ALFA");
        assert(alfa != nullptr);
        assert(alfa->bilateral_sinad == 18u);
        assert(alfa->bilateral_ber   ==  6u);
        assert(alfa->bilateral_mp    ==  3u);
        assert(alfa->bilateral_handshake_tried == true);

        auto bravo = db.get_entry(10142000, "BRAVO");
        assert(bravo != nullptr);
        assert(bravo->bilateral_handshake_tried == true);
        assert(bravo->bilateral_sinad == 31u);  // "X": tried but no data
    }

    std::remove(filepath.c_str());
    std::cout << "  PASS" << std::endl;
}

int main() {
    std::cout << "=== LQA Database Tests ===" << std::endl;

    test_database_creation();
    test_basic_entry_update();
    test_time_weighted_averaging();
    test_extended_metrics();
    test_multiple_stations();
    test_score_computation();
    test_score_range_bounds();
    test_prune_stale_entries();
    test_save_and_load();
    test_export_csv();
    test_get_all_entries();
    test_configuration();
    test_lqa_database_min_capacity_4000();
    test_lqa_one_hour_retention_ac_gen_006_003();
    test_bilateral_defaults();
    test_update_bilateral();
    test_update_bilateral_creates_entry();
    test_mark_bilateral_attempted();
    test_bilateral_x_vs_dash();
    test_bilateral_save_load();

    std::cout << "\n=== All LQA Database Tests Passed ===" << std::endl;
    return 0;
}
