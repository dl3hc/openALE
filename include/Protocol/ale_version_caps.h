/**
 * \file ale_version_caps.h
 * \brief VERSION CMD + CAPABILITIES Query encoding/decoding
 *        (FEAT-GEN-011, FEAT-GEN-012, MIL-STD-188-141B A.5.6.6.1/A.5.6.6.2)
 *
 * VERSION CMD (A.5.6.6.1):
 *   CMD word payload uses 'v', '/', 's' (summary) or 'v', '/', 'f' (full)
 *   encoded as raw 7-bit ASCII in the 21-bit payload.
 *
 * CAPABILITIES QUERY (A.5.6.6.2.1):
 *   CMD word payload uses 'c', '/', 'q' encoded as raw 7-bit ASCII.
 */

#pragma once

#include "Word/ale_word.h"
#include <array>
#include <cstdint>

namespace ale {
namespace version_caps {

// ── KVC component-selection bitmask values ────────────────────────────────────
static constexpr uint8_t KVC_HARDWARE    = 0x01u; ///< Hardware component
static constexpr uint8_t KVC_OP_FW       = 0x02u; ///< Operating firmware
static constexpr uint8_t KVC_NM_FW       = 0x04u; ///< Network management firmware
static constexpr uint8_t KVC_ALL         = KVC_HARDWARE | KVC_OP_FW | KVC_NM_FW;

// ── KVF format-selection bitmask values ──────────────────────────────────────
static constexpr uint8_t KVF_SUMMARY = 0x01u; ///< Summary format
static constexpr uint8_t KVF_FULL    = 0x02u; ///< Full format

// ── Raw ASCII codes used in VERSION CMD payload ──────────────────────────────
static constexpr char VERSION_FAMILY_CHAR = 'v'; ///< 0x76 — version family
static constexpr char VERSION_SEP_CHAR    = '/'; ///< 0x2F — command separator
static constexpr char VERSION_SUMMARY_CHAR = 's'; ///< 0x73 — summary sub-type
static constexpr char VERSION_FULL_CHAR   = 'f'; ///< 0x66 — full sub-type

/**
 * \struct VersionCmd
 * Parameters for a VERSION CMD request word (A.5.6.6.1).
 */
struct VersionCmd {
    uint8_t kvc_mask; ///< Component bitmask (KVC_HARDWARE | KVC_OP_FW | KVC_NM_FW)
    uint8_t kvf_mask; ///< Format bitmask (KVF_SUMMARY or KVF_FULL)
};

/**
 * Encode a VERSION CMD word per MIL-STD-188-141B A.5.6.6.1.
 *
 * Produces a CMD-preamble word whose 21-bit payload holds:
 *   bits 20-14 = 'v' (0x76) — version family identifier
 *   bits 13-7  = '/' (0x2F) — command sub-type separator
 *   bits 6-0   = 's' (0x73) when KVF_SUMMARY is set in cmd.kvf_mask,
 *                 'f' (0x66) when KVF_FULL is set.
 *
 * \param cmd  Component and format selection.
 * \return     Constructed ALEWord (preamble=CMD, valid=true).
 */
ALEWord encode_version_cmd(const VersionCmd& cmd);

/**
 * Return true when \p word is a CMD word whose payload begins with 'v', '/'.
 */
bool is_version_cmd(const ALEWord& word);

/**
 * Decode the VERSION CMD word back to a VersionCmd.
 *
 * char3 == 's' → kvf_mask = KVF_SUMMARY; char3 == 'f' → KVF_FULL.
 * kvc_mask is always KVC_ALL (A.5.6.6.1 requests all components).
 *
 * \pre is_version_cmd(word) == true
 */
VersionCmd decode_version_cmd(const ALEWord& word);

// ── Raw ASCII codes used in CAPABILITIES CMD payload ─────────────────────────
static constexpr char CAPS_FAMILY_CHAR = 'c'; ///< 0x63 — capabilities family
static constexpr char CAPS_SEP_CHAR    = '/'; ///< 0x2F — command separator
static constexpr char CAPS_QUERY_CHAR  = 'q'; ///< 0x71 — query sub-type
static constexpr char CAPS_REPORT_CHAR = 'r'; ///< 0x72 — report sub-type

/**
 * \struct CapabilitiesQuery
 * Parameters for a CAPABILITIES Query CMD word (A.5.6.6.2.1).
 */
struct CapabilitiesQuery {
    bool full_report_requested; ///< Reserved; always true for a c/q word
};

/**
 * Encode a CAPABILITIES QUERY CMD word per MIL-STD-188-141B A.5.6.6.2.1.
 *
 * Produces a CMD-preamble word whose 21-bit payload holds:
 *   bits 20-14 = 'c' (0x63) — capabilities family identifier
 *   bits 13-7  = '/' (0x2F) — command sub-type separator
 *   bits 6-0   = 'q' (0x71) — query sub-type
 *
 * \return Constructed ALEWord (preamble=CMD, valid=true).
 */
ALEWord encode_capabilities_query(const CapabilitiesQuery& query);

/**
 * Return true when \p word is a CMD word whose payload is 'c', '/', 'q'.
 */
bool is_capabilities_query(const ALEWord& word);

// ── CAPABILITIES REPORT (A.5.6.6.2.2) ────────────────────────────────────────

/**
 * \struct CapabilitiesReport
 * Parameters for a CAPABILITIES Report frame (A.5.6.6.2.2).
 *
 * Timing fields use unsigned integers; flag fields are bool.
 * All timing fields are stored without sign.
 */
struct CapabilitiesReport {
    uint8_t  scan_rate_chps;     ///< Scan rate in channels per second (2, 5 or 10)
    uint8_t  channels_scanned;   ///< Number of channels in the scan list
    uint16_t max_tune_time_ms;   ///< Maximum tune time in milliseconds
    uint16_t turnaround_time_ms; ///< Link turnaround time in milliseconds
    uint16_t activity_timeout_s; ///< Activity timeout in seconds
    uint16_t listen_time_ms;     ///< Listen time before transmitting in milliseconds

    bool accept_all_calls;       ///< Accepts ALLCALL
    bool accept_any_calls;       ///< Accepts ANYCALL
    bool amd_enabled;            ///< AMD orderwire supported
    bool dtm_enabled;            ///< DTM orderwire supported
    bool dbm_enabled;            ///< DBM orderwire supported

    uint8_t lp_levels_mask;      ///< Linking Protection levels supported (bitmask)
    bool time_service_enabled;   ///< Time exchange service available
    bool alqa_enabled;           ///< ALE Link Quality Analysis active
    bool orderwire_enabled;      ///< Orderwire capability present
    bool scheduling_enabled;     ///< Scheduling / time-slot service available
};

/**
 * Encode a CAPABILITIES REPORT frame per MIL-STD-188-141B A.5.6.6.2.2.
 *
 * Returns exactly 6 ALEWords:
 *   [0] CMD  — payload 'c'/'/'/'r'
 *   [1] DATA — scan_rate_chps [20:13], channels_scanned [12:5]
 *   [2] REP  — max_tune_time_ms/10 [20:13], turnaround_time_ms/10 [12:5]
 *   [3] DATA — activity_timeout_s [20:10], listen_time_ms [9:0]
 *   [4] REP  — feature-capability flags [20:6]
 *   [5] DATA — reserved (zero)
 *
 * \return 6-element array: CMD word + 5 DATA/REP words (A.5.6.6.2.2).
 */
std::array<ALEWord, 6> encode_capabilities_report(const CapabilitiesReport& rpt);

/**
 * Return true when \p word is a CMD word whose payload is 'c', '/', 'r'.
 */
bool is_capabilities_report(const ALEWord& word);

} // namespace version_caps
} // namespace ale
