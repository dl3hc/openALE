#include "App/gps_service.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace ale;

static void test_gpgga_fix() {
    std::cout << "Test: parse_gpgga valid fix sentence..." << std::endl;
    // Munich area: lat 48°07.038'N, lon 011°31.000'E
    const std::string s = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    double lat = 0, lon = 0;
    assert(GpsService::parse_gpgga(s, lat, lon));
    // 48°07.038' = 48 + 7.038/60 = 48.1173°
    assert(std::abs(lat - 48.1173) < 0.0001);
    // 011°31.000' = 11 + 31.0/60 = 11.5167°
    assert(std::abs(lon - 11.5167) < 0.0001);
    std::cout << "  lat=" << lat << " lon=" << lon << " PASS" << std::endl;
}

static void test_gpgga_no_fix() {
    std::cout << "Test: parse_gpgga quality=0 returns false..." << std::endl;
    const std::string s = "$GPGGA,123519,4807.038,N,01131.000,E,0,08,0.9,545.4,M,46.9,M,,*47";
    double lat = 0, lon = 0;
    assert(!GpsService::parse_gpgga(s, lat, lon));
    std::cout << "  PASS" << std::endl;
}

static void test_gpgga_south_west() {
    std::cout << "Test: parse_gpgga S/W hemisphere..." << std::endl;
    // Sydney area: 33°52'S, 151°12'E
    const std::string s = "$GPGGA,235947,3352.000,S,15112.000,E,1,08,1.0,100.0,M,0.0,M,,*00";
    double lat = 0, lon = 0;
    assert(GpsService::parse_gpgga(s, lat, lon));
    assert(lat < 0.0);   // South = negative
    assert(lon > 0.0);   // East = positive
    assert(std::abs(lat - (-33.8667)) < 0.001);
    assert(std::abs(lon - 151.2)      < 0.001);
    std::cout << "  PASS" << std::endl;
}

static void test_gprmc_active() {
    std::cout << "Test: parse_gprmc active sentence..." << std::endl;
    const std::string s = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    double lat = 0, lon = 0;
    bool active = false;
    assert(GpsService::parse_gprmc(s, lat, lon, active));
    assert(active);
    assert(std::abs(lat - 48.1173) < 0.0001);
    assert(std::abs(lon - 11.5167) < 0.0001);
    std::cout << "  PASS" << std::endl;
}

static void test_gprmc_void() {
    std::cout << "Test: parse_gprmc void (V) returns false..." << std::endl;
    const std::string s = "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    double lat = 0, lon = 0;
    bool active = false;
    assert(!GpsService::parse_gprmc(s, lat, lon, active));
    assert(!active);
    std::cout << "  PASS" << std::endl;
}

static void test_tpv_json_3d_fix() {
    std::cout << "Test: parse_tpv_json 3D fix..." << std::endl;
    const std::string json =
        R"({"class":"TPV","device":"/dev/ttyACM0","mode":3,"lat":48.1173,"lon":11.5167,"speed":0.0})";
    double lat = 0, lon = 0;
    bool fix_ok = false;
    assert(GpsService::parse_tpv_json(json, lat, lon, fix_ok));
    assert(fix_ok);
    assert(std::abs(lat - 48.1173) < 0.0001);
    assert(std::abs(lon - 11.5167) < 0.0001);
    std::cout << "  PASS" << std::endl;
}

static void test_tpv_json_2d_fix() {
    std::cout << "Test: parse_tpv_json 2D fix (mode=2)..." << std::endl;
    const std::string json = R"({"class":"TPV","mode":2,"lat":48.1,"lon":11.5})";
    double lat = 0, lon = 0;
    bool fix_ok = false;
    assert(GpsService::parse_tpv_json(json, lat, lon, fix_ok));
    assert(fix_ok);
    std::cout << "  PASS" << std::endl;
}

static void test_tpv_json_no_fix() {
    std::cout << "Test: parse_tpv_json mode=1 (no fix)..." << std::endl;
    const std::string json = R"({"class":"TPV","mode":1,"lat":0.0,"lon":0.0})";
    double lat = 0, lon = 0;
    bool fix_ok = false;
    assert(GpsService::parse_tpv_json(json, lat, lon, fix_ok));
    assert(!fix_ok);
    std::cout << "  PASS" << std::endl;
}

static void test_nmea_coord() {
    std::cout << "Test: nmea_coord conversion..." << std::endl;
    // 4807.038 N = 48 + 7.038/60 = 48.1173°
    assert(std::abs(GpsService::nmea_coord("4807.038", "N") - 48.1173) < 0.0001);
    // 01131.000 E = 11 + 31.0/60 = 11.5167°
    assert(std::abs(GpsService::nmea_coord("01131.000", "E") - 11.5167) < 0.0001);
    // S / W are negative
    assert(GpsService::nmea_coord("3352.000", "S") < 0.0);
    assert(GpsService::nmea_coord("01512.000", "W") < 0.0);
    std::cout << "  PASS" << std::endl;
}

int main() {
    test_gpgga_fix();
    test_gpgga_no_fix();
    test_gpgga_south_west();
    test_gprmc_active();
    test_gprmc_void();
    test_tpv_json_3d_fix();
    test_tpv_json_2d_fix();
    test_tpv_json_no_fix();
    test_nmea_coord();

    std::cout << "\nAll GPS parser tests PASSED." << std::endl;
    return 0;
}
