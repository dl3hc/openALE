#include "LQA/lqa_database.h"
#include "LQA/solar_position.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

using namespace ale;

// v3 persistence: write + read solar_elevation + sfi fields, verify round-trip.
static void test_v3_persistence_roundtrip() {
    std::cout << "Test: LQA v3 propagation fields survive save/load..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 7073000u;

    // Create a sounding entry with real SNR data so sample_count > 0
    db.update_entry_extended(freq, "", 18.0f, 2.0f, 15.0f, 0.1f, -80.0f, 0, 10, 1000000u);
    db.set_propagation_at_measurement(freq, "", 42.5f, 120.0f);

    const std::string path = "test_v3_prop.db";
    assert(db.save_to_file(path));

    LQADatabase db2;
    assert(db2.load_from_file(path));

    const auto entry = db2.get_entry(freq, "");
    assert(entry != nullptr);
    assert(std::abs(entry->solar_elevation_deg_at_measurement - 42.5f) < 0.01f);
    assert(std::abs(entry->sfi_at_measurement - 120.0f) < 0.1f);

    std::remove(path.c_str());
    std::cout << "  PASS" << std::endl;
}

// Negative elevation (night-time measurement) persists correctly.
static void test_v3_negative_elevation() {
    std::cout << "Test: v3 negative solar elevation persists..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 14250000u;
    db.update_entry(freq, "KW9B", 12.0f, 1.0f, 0, 8, 2000000u);
    db.set_propagation_at_measurement(freq, "KW9B", -15.3f, 85.0f);

    const std::string path = "test_v3_neg.db";
    assert(db.save_to_file(path));

    LQADatabase db2;
    assert(db2.load_from_file(path));

    const auto entry = db2.get_entry(freq, "KW9B");
    assert(entry != nullptr);
    assert(std::abs(entry->solar_elevation_deg_at_measurement - (-15.3f)) < 0.02f);
    assert(std::abs(entry->sfi_at_measurement - 85.0f) < 0.1f);

    std::remove(path.c_str());
    std::cout << "  PASS" << std::endl;
}

// Without propagation data (v2-style entries) fields remain at 0.
static void test_no_propagation_fields_zero() {
    std::cout << "Test: Entries without propagation data have 0 fields..." << std::endl;

    LQADatabase db;
    const uint32_t freq = 3500000u;
    db.update_entry(freq, "", 10.0f, 0.5f, 0, 5, 3000000u);

    const auto entry = db.get_entry(freq, "");
    assert(entry != nullptr);
    assert(entry->solar_elevation_deg_at_measurement == 0.0f);
    assert(entry->sfi_at_measurement == 0.0f);

    std::cout << "  PASS" << std::endl;
}

// max_age_ms default is 25 h (90 000 000 ms).
static void test_default_max_age_25h() {
    std::cout << "Test: LQAConfig default max_age_ms is 25 h..." << std::endl;
    LQADatabase db;
    const LQAConfig cfg = db.get_config();
    assert(cfg.max_age_ms == 90000000u);
    std::cout << "  PASS" << std::endl;
}

// Solar elevation math: same lat/lon, different times give different elevations.
static void test_elevation_varies_with_time() {
    std::cout << "Test: Solar elevation varies between summer noon and midnight..." << std::endl;
    // London, June 21 2024
    const float noon    = compute_solar_elevation(51.5, -0.1, 1718971200LL); // noon
    const float midnight = compute_solar_elevation(51.5, -0.1, 1718928000LL); // midnight
    assert(noon > midnight);          // noon always higher than midnight
    assert(noon > 0.0f);              // above horizon at noon
    assert(midnight < 0.0f);          // below horizon at midnight
    std::cout << "  PASS (noon=" << noon << "°, midnight=" << midnight << "°)" << std::endl;
}

int main() {
    test_v3_persistence_roundtrip();
    test_v3_negative_elevation();
    test_no_propagation_fields_zero();
    test_default_max_age_25h();
    test_elevation_varies_with_time();

    std::cout << "\nAll propagation scoring tests PASSED." << std::endl;
    return 0;
}
