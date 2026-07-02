#include "LQA/solar_position.h"

#include <cmath>
#include <cctype>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ale {

// ── Spencer/Michalsky solar position algorithm ───────────────────────────────
// Reference: Spencer (1971) + Michalsky (1988) corrections.
// Accuracy: < 0.5° for the period 1950–2050.

static constexpr double kDeg2Rad = M_PI / 180.0;
static constexpr double kRad2Deg = 180.0 / M_PI;

float compute_solar_elevation(double lat_deg, double lon_deg, int64_t unix_sec) {
    // ── Day-of-year (Julian day number) ──────────────────────────────────────
    // Unix time 0 = 1970-01-01T00:00:00Z = Julian Day 2440587.5
    const double jd = static_cast<double>(unix_sec) / 86400.0 + 2440587.5;

    // Julian centuries from J2000.0
    const double T = (jd - 2451545.0) / 36525.0;

    // ── Geometric mean longitude of the sun (degrees, mod 360) ──────────────
    double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    L0 = L0 - 360.0 * std::floor(L0 / 360.0);

    // ── Mean anomaly (degrees) ────────────────────────────────────────────────
    double M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
    M = M * kDeg2Rad;

    // ── Sun's equation of centre ─────────────────────────────────────────────
    const double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(M)
                   + (0.019993 - 0.000101 * T) * std::sin(2.0 * M)
                   + 0.000289 * std::sin(3.0 * M);

    // ── Sun's true longitude (degrees) ───────────────────────────────────────
    const double sun_lon = L0 + C;

    // ── Apparent longitude (degrees) — correct for aberration ────────────────
    const double omega = 125.04 - 1934.136 * T;
    const double apparent_lon = sun_lon - 0.00569 - 0.00478 * std::sin(omega * kDeg2Rad);

    // ── Obliquity of the ecliptic (degrees) ──────────────────────────────────
    double eps0 = 23.0 + (26.0 + (21.448 - T * (46.8150 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
    const double eps = eps0 + 0.00256 * std::cos(omega * kDeg2Rad);

    // ── Declination (radians) ─────────────────────────────────────────────────
    const double dec = std::asin(std::sin(eps * kDeg2Rad) * std::sin(apparent_lon * kDeg2Rad));

    // ── Right ascension (degrees) ─────────────────────────────────────────────
    const double y = std::cos(eps * kDeg2Rad) * std::sin(apparent_lon * kDeg2Rad);
    const double x = std::cos(apparent_lon * kDeg2Rad);
    double ra = std::atan2(y, x) * kRad2Deg;
    ra = ra - 360.0 * std::floor(ra / 360.0);  // wrap to [0, 360)

    // ── Greenwich Mean Sidereal Time (degrees) ───────────────────────────────
    const double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                      + T * T * (0.000387933 - T / 38710000.0);

    // ── Local Hour Angle (degrees) ────────────────────────────────────────────
    double ha = gmst + lon_deg - ra;
    ha = ha - 360.0 * std::floor(ha / 360.0);  // wrap to [0, 360)
    if (ha > 180.0) ha -= 360.0;               // convert to –180..+180
    const double ha_rad = ha * kDeg2Rad;

    // ── Elevation ─────────────────────────────────────────────────────────────
    const double lat_rad = lat_deg * kDeg2Rad;
    const double sin_elev = std::sin(lat_rad) * std::sin(dec)
                          + std::cos(lat_rad) * std::cos(dec) * std::cos(ha_rad);

    return static_cast<float>(std::asin(sin_elev) * kRad2Deg);
}

// ── Maidenhead locator ────────────────────────────────────────────────────────

bool maidenhead_to_latlon(const std::string& grid, double& lat_deg, double& lon_deg) {
    if (grid.size() != 4 && grid.size() != 6) return false;

    // Field pair: A–R (chars 0–1)
    const char f0 = static_cast<char>(std::toupper(static_cast<unsigned char>(grid[0])));
    const char f1 = static_cast<char>(std::toupper(static_cast<unsigned char>(grid[1])));
    if (f0 < 'A' || f0 > 'R' || f1 < 'A' || f1 > 'R') return false;

    // Square pair: 0–9 (chars 2–3)
    if (!std::isdigit(static_cast<unsigned char>(grid[2])) ||
        !std::isdigit(static_cast<unsigned char>(grid[3]))) return false;
    const int sq0 = grid[2] - '0';
    const int sq1 = grid[3] - '0';

    // Field: 20° lon × 10° lat per field letter
    lon_deg = (f0 - 'A') * 20.0 - 180.0;
    lat_deg = (f1 - 'A') * 10.0 - 90.0;

    // Square: 2° lon × 1° lat per digit
    lon_deg += sq0 * 2.0;
    lat_deg += sq1 * 1.0;

    if (grid.size() == 6) {
        // Subsquare pair: a–x (chars 4–5), 5'/2.5' resolution
        const char s0 = static_cast<char>(std::tolower(static_cast<unsigned char>(grid[4])));
        const char s1 = static_cast<char>(std::tolower(static_cast<unsigned char>(grid[5])));
        if (s0 < 'a' || s0 > 'x' || s1 < 'a' || s1 > 'x') return false;
        // Subsquare: 5' lon × 2.5' lat per letter (24 subdivisions per 2°/1°)
        lon_deg += (s0 - 'a') * (2.0 / 24.0);
        lat_deg += (s1 - 'a') * (1.0 / 24.0);
        // Return centre of subsquare cell
        lon_deg += (2.0 / 24.0) / 2.0;
        lat_deg += (1.0 / 24.0) / 2.0;
    } else {
        // Return centre of square cell
        lon_deg += 1.0;
        lat_deg += 0.5;
    }

    return true;
}

} // namespace ale
