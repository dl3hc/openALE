#include "LQA/solar_position.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace ale;

static void test_elevation_summer_noon() {
    std::cout << "Test: London summer solstice noon elevation > 55°..." << std::endl;
    // London: 51.5°N, -0.1°E; June 21, 2024 12:00 UTC ≈ 1718971200
    const float elev = compute_solar_elevation(51.5, -0.1, 1718971200LL);
    std::cout << "  elevation = " << elev << "°" << std::endl;
    assert(elev > 55.0f && elev < 70.0f);
    std::cout << "  PASS" << std::endl;
}

static void test_elevation_summer_midnight() {
    std::cout << "Test: London summer solstice midnight elevation < 0°..." << std::endl;
    // June 21, 2024 00:00 UTC ≈ 1718928000
    const float elev = compute_solar_elevation(51.5, -0.1, 1718928000LL);
    std::cout << "  elevation = " << elev << "°" << std::endl;
    assert(elev < 0.0f);
    std::cout << "  PASS" << std::endl;
}

static void test_elevation_winter_noon() {
    std::cout << "Test: London winter solstice noon elevation 8–25°..." << std::endl;
    // Dec 21, 2024 12:00 UTC ≈ 1734868800
    const float elev = compute_solar_elevation(51.5, -0.1, 1734868800LL);
    std::cout << "  elevation = " << elev << "°" << std::endl;
    assert(elev > 8.0f && elev < 25.0f);
    std::cout << "  PASS" << std::endl;
}

static void test_maidenhead_io91wm() {
    std::cout << "Test: Maidenhead IO91wm → London coordinates..." << std::endl;
    double lat = 0.0, lon = 0.0;
    const bool ok = maidenhead_to_latlon("IO91wm", lat, lon);
    std::cout << "  lat=" << lat << " lon=" << lon << std::endl;
    assert(ok);
    assert(lat > 51.4 && lat < 51.6);
    assert(lon > -0.25 && lon < 0.0);
    std::cout << "  PASS" << std::endl;
}

static void test_maidenhead_case_insensitive() {
    std::cout << "Test: Maidenhead case-insensitive..." << std::endl;
    double lat1 = 0, lon1 = 0, lat2 = 0, lon2 = 0;
    assert(maidenhead_to_latlon("IO91wm", lat1, lon1));
    assert(maidenhead_to_latlon("io91WM", lat2, lon2));
    assert(std::abs(lat1 - lat2) < 0.001);
    assert(std::abs(lon1 - lon2) < 0.001);
    std::cout << "  PASS" << std::endl;
}

static void test_maidenhead_four_char() {
    std::cout << "Test: Maidenhead 4-char (IO91)..." << std::endl;
    double lat = 0, lon = 0;
    assert(maidenhead_to_latlon("IO91", lat, lon));
    // IO91: lon = -20 + 9*2 = -2; lat = 50 + 1 = 51; centre = -1, 51.5
    assert(lat > 50.9 && lat < 52.0);
    assert(lon > -3.0 && lon < 0.0);
    std::cout << "  PASS" << std::endl;
}

static void test_maidenhead_invalid() {
    std::cout << "Test: Invalid Maidenhead returns false..." << std::endl;
    double lat = 0, lon = 0;
    assert(!maidenhead_to_latlon("", lat, lon));
    assert(!maidenhead_to_latlon("ZZ99", lat, lon));  // Z is out of A-R range for fields
    assert(!maidenhead_to_latlon("IO9",  lat, lon));  // too short
    std::cout << "  PASS" << std::endl;
}

static void test_ms_to_unix_sec() {
    std::cout << "Test: ms_to_unix_sec conversion..." << std::endl;
    assert(ms_to_unix_sec(1000u) == 1LL);
    assert(ms_to_unix_sec(0u)    == 0LL);
    assert(ms_to_unix_sec(3000u) == 3LL);
    std::cout << "  PASS" << std::endl;
}

int main() {
    test_elevation_summer_noon();
    test_elevation_summer_midnight();
    test_elevation_winter_noon();
    test_maidenhead_io91wm();
    test_maidenhead_case_insensitive();
    test_maidenhead_four_char();
    test_maidenhead_invalid();
    test_ms_to_unix_sec();

    std::cout << "\nAll solar position tests PASSED." << std::endl;
    return 0;
}
