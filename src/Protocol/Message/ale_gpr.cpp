#include "Protocol/Message/ale_gpr.h"

#include <cctype>
#include <cmath>
#include <cstdint>

namespace ale {

// ── Portable civil calendar <-> days-since-epoch (Howard Hinnant's algorithm) ──
// Avoids timegm()/_mkgmtime(), neither of which is standard/portable across
// the Windows and Linux targets this project builds for.

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);              // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;    // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

static void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);                    // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;       // [0, 399]
    const int64_t yi = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                    // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                                         // [0, 11]
    d = doy - (153 * mp + 2) / 5 + 1;                                                // [1, 31]
    m = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));                              // [1, 12]
    y = static_cast<int>(yi + (m <= 2));
}

static bool is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
    static const int kDim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && is_leap_year(y)) return 29;
    return kDim[m - 1];
}

static bool is_digit_string(const std::string& s, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// ── Field parsers (docs/ALE_GPR_SPEC.md §5-8) ──────────────────────────────────

static bool parse_latitude(const std::string& raw, double& out_deg) {
    // ^([0-9]{2})([NS])([0-9]+)$
    if (raw.size() < 4) return false;
    if (!is_digit_string(raw, 0, 2)) return false;
    const char hemi = raw[2];
    if (hemi != 'N' && hemi != 'S') return false;
    if (!is_digit_string(raw, 3, raw.size())) return false;

    const int degrees = (raw[0] - '0') * 10 + (raw[1] - '0');
    if (degrees < 0 || degrees > 90) return false;

    const double fraction = std::stod("0." + raw.substr(3));
    const double value = degrees + fraction;
    if (value > 90.0) return false;

    out_deg = (hemi == 'S') ? -value : value;
    return true;
}

static bool parse_longitude(const std::string& raw, double& out_deg) {
    // ^([0-9]{3})([EW])([0-9]+)$
    if (raw.size() < 5) return false;
    if (!is_digit_string(raw, 0, 3)) return false;
    const char hemi = raw[3];
    if (hemi != 'E' && hemi != 'W') return false;
    if (!is_digit_string(raw, 4, raw.size())) return false;

    const int degrees = (raw[0] - '0') * 100 + (raw[1] - '0') * 10 + (raw[2] - '0');
    if (degrees < 0 || degrees > 180) return false;

    const double fraction = std::stod("0." + raw.substr(4));
    const double value = degrees + fraction;
    if (value > 180.0) return false;

    out_deg = (hemi == 'W') ? -value : value;
    return true;
}

static bool parse_altitude(const std::string& raw, double& out_val, char& out_unit) {
    // 1+ digits followed by M or F
    if (raw.size() < 2) return false;
    const char unit = raw.back();
    if (unit != 'M' && unit != 'F') return false;
    if (!is_digit_string(raw, 0, raw.size() - 1)) return false;

    out_val = std::stod(raw.substr(0, raw.size() - 1));
    out_unit = unit;
    return true;
}

static bool parse_timestamp(const std::string& raw, std::time_t& out_ts, char& out_tz) {
    // YYYYMMDDZhhmmss, fixed 15 chars
    if (raw.size() != 15) return false;
    if (!is_digit_string(raw, 0, 8)) return false;
    const char tz = raw[8];
    if (tz < 'A' || tz > 'Z') return false;
    if (!is_digit_string(raw, 9, 15)) return false;

    const int year   = std::stoi(raw.substr(0, 4));
    const int month   = std::stoi(raw.substr(4, 2));
    const int day     = std::stoi(raw.substr(6, 2));
    const int hour    = std::stoi(raw.substr(9, 2));
    const int minute  = std::stoi(raw.substr(11, 2));
    const int second  = std::stoi(raw.substr(13, 2));

    if (month < 1 || month > 12) return false;
    if (day < 1 || day > days_in_month(year, month)) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;

    const int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const int64_t secs = days * 86400 + hour * 3600 + minute * 60 + second;
    out_ts = static_cast<std::time_t>(secs);
    out_tz = tz;
    return true;
}

static bool is_allowed_object_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '#';
}

// ── is_gpr / parse_gpr ─────────────────────────────────────────────────────────

bool is_gpr(const std::string& amd_text) {
    const size_t star = amd_text.find('*');
    const std::string first = (star == std::string::npos) ? amd_text : amd_text.substr(0, star);
    return first == "GPR";
}

AleGpr parse_gpr(const std::string& amd_text, bool strict) {
    AleGpr g;
    g.raw = amd_text;

    std::vector<std::string> fields;
    {
        size_t start = 0;
        for (size_t i = 0; i <= amd_text.size(); ++i) {
            if (i == amd_text.size() || amd_text[i] == '*') {
                fields.push_back(amd_text.substr(start, i - start));
                start = i + 1;
            }
        }
    }

    if (fields.empty() || fields[0] != "GPR") {
        g.errors.push_back("not an ALE-GPR message (missing \"GPR\" prefix)");
        return g;
    }
    if (fields.size() != 7) {
        g.errors.push_back("invalid ALE-GPR structure: expected 7 fields, got " +
                            std::to_string(fields.size()));
        return g;
    }

    g.valid_gpr_structure = true;
    g.object        = fields[1];
    g.latitude_raw  = fields[2];
    g.longitude_raw = fields[3];
    g.altitude_raw  = fields[4];
    g.time_raw      = fields[5];
    g.comment       = fields[6];

    bool strict_ok = true;

    // OBJECT (§4)
    if (g.object.size() < 3 || g.object.size() > 15) {
        g.errors.push_back("object field length out of range [3,15]: " +
                            std::to_string(g.object.size()));
        strict_ok = false;
    }
    for (char c : g.object) {
        if (!is_allowed_object_char(c)) {
            g.errors.push_back("object field contains a disallowed character");
            strict_ok = false;
            break;
        }
    }

    // LATITUDE (§5)
    if (g.latitude_raw.find('#') != std::string::npos) {
        g.manual_or_invalid_position = true;
    } else if (g.latitude_raw.size() > 9 || !parse_latitude(g.latitude_raw, g.latitude_deg)) {
        g.errors.push_back("invalid latitude field: \"" + g.latitude_raw + "\"");
        g.manual_or_invalid_position = true;
        strict_ok = false;
    } else {
        g.has_latitude = true;
    }

    // LONGITUDE (§6)
    if (g.longitude_raw.find('#') != std::string::npos) {
        g.manual_or_invalid_position = true;
    } else if (g.longitude_raw.size() > 10 || !parse_longitude(g.longitude_raw, g.longitude_deg)) {
        g.errors.push_back("invalid longitude field: \"" + g.longitude_raw + "\"");
        g.manual_or_invalid_position = true;
        strict_ok = false;
    } else {
        g.has_longitude = true;
    }

    g.valid_position = g.has_latitude && g.has_longitude;

    // ALTITUDE (§7)
    if (g.altitude_raw.find('#') != std::string::npos) {
        g.manual_or_invalid_position = true;
    } else if (g.altitude_raw.size() > 7 || !parse_altitude(g.altitude_raw, g.altitude, g.altitude_unit)) {
        g.errors.push_back("invalid altitude field: \"" + g.altitude_raw + "\"");
        g.manual_or_invalid_position = true;
        strict_ok = false;
    } else {
        g.has_altitude = true;
    }

    // TIME (§8)
    if (g.time_raw.find('#') != std::string::npos) {
        g.manual_or_invalid_position = true;
    } else if (!parse_timestamp(g.time_raw, g.timestamp_utc, g.timezone_code)) {
        g.errors.push_back("invalid time field: \"" + g.time_raw + "\"");
        g.manual_or_invalid_position = true;
        strict_ok = false;
    } else {
        g.has_timestamp  = true;
        g.valid_timestamp = true;
    }

    // COMMENT (§9)
    if (g.comment.size() > 25) {
        g.errors.push_back("comment field exceeds 25 characters");
        strict_ok = false;
    }

    if (strict && !strict_ok) {
        g.valid_gpr_structure = false;
    }

    return g;
}

// ── generate_gpr (§13-14) ───────────────────────────────────────────────────────

static std::string zero_pad(long value, int width) {
    std::string s = std::to_string(value);
    if (static_cast<int>(s.size()) < width)
        s = std::string(static_cast<size_t>(width) - s.size(), '0') + s;
    return s;
}

std::string generate_gpr(const std::string& object,
                          double lat_deg, double lon_deg,
                          bool has_altitude, double altitude, char altitude_unit,
                          std::time_t timestamp_utc, char timezone_code,
                          const std::string& comment)
{
    std::string obj = object;
    if (obj.size() > 15) obj = obj.substr(0, 15);

    // Latitude: decimal point replaced by hemisphere char, 6-digit fraction.
    const char lat_hemi = (lat_deg < 0.0) ? 'S' : 'N';
    const double lat_abs = std::fabs(lat_deg);
    long lat_deg_i = static_cast<long>(lat_abs);
    long lat_frac  = std::lround((lat_abs - static_cast<double>(lat_deg_i)) * 1000000.0);
    if (lat_frac >= 1000000) { lat_frac -= 1000000; lat_deg_i += 1; }
    const std::string lat_field = zero_pad(lat_deg_i, 2) + lat_hemi + zero_pad(lat_frac, 6);

    // Longitude: zero-padded 3-digit degrees.
    const char lon_hemi = (lon_deg < 0.0) ? 'W' : 'E';
    const double lon_abs = std::fabs(lon_deg);
    long lon_deg_i = static_cast<long>(lon_abs);
    long lon_frac  = std::lround((lon_abs - static_cast<double>(lon_deg_i)) * 1000000.0);
    if (lon_frac >= 1000000) { lon_frac -= 1000000; lon_deg_i += 1; }
    const std::string lon_field = zero_pad(lon_deg_i, 3) + lon_hemi + zero_pad(lon_frac, 6);

    // Altitude: canonical zero-padded form, or "#"+unit when unavailable (§15 placeholder convention).
    std::string alt_field;
    const char alt_unit = altitude_unit ? altitude_unit : 'M';
    if (has_altitude) {
        const long alt_i = std::lround(altitude);
        alt_field = zero_pad(alt_i, 6) + alt_unit;
    } else {
        alt_field = std::string("#") + alt_unit;
    }

    // Time: YYYYMMDDZhhmmss, calendar fields interpreted as UTC.
    int64_t days = static_cast<int64_t>(timestamp_utc) / 86400;
    int64_t rem  = static_cast<int64_t>(timestamp_utc) % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int y; unsigned mo, d;
    civil_from_days(days, y, mo, d);
    const int hh = static_cast<int>(rem / 3600);
    const int mm = static_cast<int>((rem % 3600) / 60);
    const int ss = static_cast<int>(rem % 60);
    const char tz = timezone_code ? timezone_code : 'Z';
    const std::string time_field = zero_pad(y, 4) + zero_pad(static_cast<long>(mo), 2) +
                                    zero_pad(static_cast<long>(d), 2) + std::string(1, tz) +
                                    zero_pad(hh, 2) + zero_pad(mm, 2) + zero_pad(ss, 2);

    // Comment: truncated to 25 chars.
    std::string cmt = comment;
    if (cmt.size() > 25) cmt = cmt.substr(0, 25);

    return "GPR*" + obj + "*" + lat_field + "*" + lon_field + "*" +
           alt_field + "*" + time_field + "*" + cmt;
}

} // namespace ale
