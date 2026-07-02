#include "App/sfi_service.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace ale;

// ── Current NOAA format: [{"flux":N,"time_tag":"..."}] ───────────────────────

static void test_parse_current_noaa_format() {
    std::cout << "Test: parse_sfi_json current NOAA format (lowercase numeric)..." << std::endl;
    float sfi = 0.0f;
    assert(SfiService::parse_sfi_json(
        R"([{"flux":201,"time_tag":"2026-07-01T20:00:00"}])", sfi));
    assert(std::abs(sfi - 201.0f) < 0.1f);
    std::cout << "  sfi=" << sfi << " PASS" << std::endl;
}

static void test_parse_current_noaa_low_activity() {
    std::cout << "Test: parse_sfi_json current format low SFI (70)..." << std::endl;
    float sfi = 0.0f;
    assert(SfiService::parse_sfi_json(
        R"([{"flux":70,"time_tag":"2026-01-15T12:00:00"}])", sfi));
    assert(std::abs(sfi - 70.0f) < 0.1f);
    std::cout << "  PASS" << std::endl;
}

// ── Legacy NOAA format: {"Flux":"N","A":"..."}  ───────────────────────────────

static void test_parse_legacy_noaa_format() {
    std::cout << "Test: parse_sfi_json legacy NOAA format (capitalized quoted)..." << std::endl;
    float sfi = 0.0f;
    assert(SfiService::parse_sfi_json(R"({"Flux":"165","A":"3","K":"1"})", sfi));
    assert(std::abs(sfi - 165.0f) < 0.1f);
    std::cout << "  sfi=" << sfi << " PASS" << std::endl;
}

static void test_parse_legacy_high_flux() {
    std::cout << "Test: parse_sfi_json legacy format high Flux (300)..." << std::endl;
    float sfi = 0.0f;
    assert(SfiService::parse_sfi_json(R"({"Flux":"300","A":"99"})", sfi));
    assert(std::abs(sfi - 300.0f) < 0.1f);
    std::cout << "  PASS" << std::endl;
}

// ── Error cases ───────────────────────────────────────────────────────────────

static void test_parse_missing_flux_key() {
    std::cout << "Test: parse_sfi_json missing flux key returns false..." << std::endl;
    float sfi = 0.0f;
    assert(!SfiService::parse_sfi_json(R"({"A":"3","K":"1"})", sfi));
    assert(!SfiService::parse_sfi_json(R"([{"time_tag":"2026-07-01T20:00:00"}])", sfi));
    std::cout << "  PASS" << std::endl;
}

static void test_parse_empty_body() {
    std::cout << "Test: parse_sfi_json empty body returns false..." << std::endl;
    float sfi = 0.0f;
    assert(!SfiService::parse_sfi_json("", sfi));
    std::cout << "  PASS" << std::endl;
}

static void test_parse_out_of_range() {
    std::cout << "Test: parse_sfi_json out-of-range values return false..." << std::endl;
    float sfi = 0.0f;
    assert(!SfiService::parse_sfi_json(R"([{"flux":0}])", sfi));
    assert(!SfiService::parse_sfi_json(R"({"Flux":"0"})", sfi));
    std::cout << "  PASS" << std::endl;
}

static void test_constants() {
    std::cout << "Test: SfiService constants are sane..." << std::endl;
    assert(SfiService::kUnknown == 0.0f);
    assert(SfiService::kRetryInitMs == 5u * 60u * 1000u);   // 5 min
    assert(SfiService::kRefreshMs   == 60u * 60u * 1000u);  // 1 h
    std::cout << "  PASS" << std::endl;
}

int main() {
    test_parse_current_noaa_format();
    test_parse_current_noaa_low_activity();
    test_parse_legacy_noaa_format();
    test_parse_legacy_high_flux();
    test_parse_missing_flux_key();
    test_parse_empty_body();
    test_parse_out_of_range();
    test_constants();

    std::cout << "\nAll SFI service tests PASSED." << std::endl;
    return 0;
}
