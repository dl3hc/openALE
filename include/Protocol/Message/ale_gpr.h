/**
 * \file Protocol/Message/ale_gpr.h
 * \brief ALE-GPR (Geo Position Report) parse/generate — HFLINK Open Standard v1.1
 *
 * ALE-GPR encodes a geographic position report as a human-readable ASCII
 * string transported as an AMD orderwire payload (encode_amd()/decode from
 * Protocol/Message/ale_orderwire_protocols.h handle the actual AMD word
 * framing; this module only deals with the plain-text GPR payload itself).
 *
 * Full field-by-field spec: docs/ALE_GPR_SPEC.md (HFLINK ALE-GPR v1.1).
 *
 * Wire format (exactly 7 '*'-delimited fields, no leading/trailing '*'):
 *   GPR*OBJECT*LATITUDE*LONGITUDE*ALTITUDE*TIME*COMMENT
 *
 * Example:
 *   GPR*KQ6XA*37N654321*122W987654*000003M*20050821Z135235*EVERYTHING FINE
 *
 * This module is pure and dependency-free (no ALEController coupling), like
 * GpsService's NMEA parser (src/App/gps_service.cpp) — bool/struct returns,
 * no exceptions, no logging.
 */

#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace ale {

/**
 * \struct AleGpr
 * Parsed representation of an ALE-GPR message, per docs/ALE_GPR_SPEC.md §11.
 *
 * Per spec §15-16, a structurally valid GPR message with unavailable or
 * manually-entered position/time data (marked with '#') is NOT an error —
 * valid_gpr_structure stays true and manual_or_invalid_position is set,
 * rather than discarding the message. Only field-count/prefix mismatches
 * (§10) make valid_gpr_structure false.
 */
struct AleGpr {
    std::string raw;      ///< Original AMD text, preserved verbatim.
    std::string object;   ///< Field 2 — reported station/object (may differ from AMD sender).
    std::string comment;  ///< Field 7 — free text, up to 25 chars, preserved as received.

    std::string latitude_raw;   ///< Field 3, raw (e.g. "37N654321" or "##").
    std::string longitude_raw;  ///< Field 4, raw.
    std::string altitude_raw;   ///< Field 5, raw.
    std::string time_raw;       ///< Field 6, raw.

    bool   has_latitude   = false;
    double latitude_deg   = 0.0;   ///< +N / -S, degrees.
    bool   has_longitude  = false;
    double longitude_deg  = 0.0;   ///< +E / -W, degrees.

    bool   has_altitude   = false;
    double altitude       = 0.0;
    char   altitude_unit  = 0;     ///< 'M' or 'F'.

    bool        has_timestamp  = false;
    std::time_t timestamp_utc  = 0;   ///< Calendar fields interpreted as UTC regardless of timezone_code (spec does not define non-'Z' conversion).
    char        timezone_code  = 0;   ///< 'Z' = UTC/Zulu, 'J' = local (spec §8); stored verbatim, not converted.

    bool valid_gpr_structure = false;  ///< 7-field split + "GPR" prefix present (and, in strict mode, all field-level checks passed).
    bool valid_position      = false;  ///< Both latitude and longitude parsed and in range.
    bool valid_timestamp     = false;  ///< Time field parsed and in range.
    bool manual_or_invalid_position = false;  ///< '#' or unparsable data in latitude/longitude/altitude/time.

    std::vector<std::string> errors;  ///< Human-readable diagnostics (warnings in compatibility mode, rejection reasons in strict mode).
};

/**
 * Detect whether an AMD payload is an ALE-GPR message (spec §17).
 * Checks only that the text up to the first '*' (or the whole text, if no
 * '*' is present) equals "GPR" — cheap enough to call on every received AMD
 * before deciding whether to invoke parse_gpr().
 */
bool is_gpr(const std::string& amd_text);

/**
 * Parse an ALE-GPR AMD payload per docs/ALE_GPR_SPEC.md §10 (algorithm) and
 * §5-9 (field rules).
 *
 * \param amd_text  Raw AMD text (e.g. as received via ALE_AMD_RECEIVED).
 * \param strict     false (default) = compatibility mode: structurally valid
 *                   GPR messages are never rejected outright for field-level
 *                   issues (invalid lengths/ranges/characters) — those are
 *                   reported via valid_position/valid_timestamp/errors, but
 *                   valid_gpr_structure stays true. true = strict mode: any
 *                   field-level violation also clears valid_gpr_structure
 *                   (spec §16).
 */
AleGpr parse_gpr(const std::string& amd_text, bool strict = false);

/**
 * Generate an ALE-GPR AMD payload per docs/ALE_GPR_SPEC.md §13-14.
 *
 * \param object         Reported station/object, 3-15 chars (truncated to 15 if longer).
 * \param lat_deg        Latitude, degrees (+N / -S), -90..+90.
 * \param lon_deg        Longitude, degrees (+E / -W), -180..+180.
 * \param has_altitude   If false, the altitude field is generated as an
 *                       unavailable placeholder ("#" + altitude_unit, or
 *                       "#M" if altitude_unit is 0) per spec §15's '#' convention.
 * \param altitude       Altitude value (used only if has_altitude).
 * \param altitude_unit  'M' or 'F' (used only if has_altitude; defaults to 'M' if 0).
 * \param timestamp_utc  Epoch seconds; formatted as UTC calendar fields.
 * \param timezone_code  Timezone separator char (spec §8); 'Z' if 0.
 * \param comment        Free text, truncated to 25 chars if longer.
 * \return               Canonical "GPR*..." payload string.
 */
std::string generate_gpr(const std::string& object,
                          double lat_deg, double lon_deg,
                          bool has_altitude, double altitude, char altitude_unit,
                          std::time_t timestamp_utc, char timezone_code,
                          const std::string& comment);

} // namespace ale
