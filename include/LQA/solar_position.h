#pragma once
#include <cstdint>
#include <string>

namespace ale {

/**
 * @brief Solar elevation angle in degrees at a given location and time.
 *
 * Uses the Spencer/Michalsky Fourier-series algorithm: < 0.5° error, no
 * external dependencies, suitable for real-time use in the main loop.
 *
 * @param lat_deg  Observer latitude in degrees (positive = North)
 * @param lon_deg  Observer longitude in degrees (positive = East)
 * @param unix_sec UTC seconds since the Unix epoch (1970-01-01T00:00:00Z)
 * @return Solar elevation in degrees; negative = below horizon
 */
float compute_solar_elevation(double lat_deg, double lon_deg, int64_t unix_sec);

/**
 * @brief Convert a Maidenhead grid locator to decimal lat/lon (centre of cell).
 *
 * Accepts 4-char (field+square) and 6-char (field+square+subsquare) locators.
 * Input is case-insensitive.
 *
 * @param grid     Maidenhead locator string, e.g. "IO91wm"
 * @param lat_deg  Output: latitude in degrees (positive = North)
 * @param lon_deg  Output: longitude in degrees (positive = East)
 * @return true on success; false if grid has invalid length or characters
 */
bool maidenhead_to_latlon(const std::string& grid, double& lat_deg, double& lon_deg);

/// Convert milliseconds-since-epoch to Unix seconds (uint32 wraps at ~2106).
inline int64_t ms_to_unix_sec(uint32_t ms) { return static_cast<int64_t>(ms) / 1000; }

} // namespace ale
