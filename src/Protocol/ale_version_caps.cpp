/**
 * \file ale_version_caps.cpp
 * \brief VERSION CMD + CAPABILITIES Query/Report implementation
 *        (FEAT-GEN-011/012, MIL-STD-188-141B A.5.6.6.1/A.5.6.6.2)
 */

#include "Protocol/ale_version_caps.h"
#include <array>
#include <cstring>

namespace ale {
namespace version_caps {

// ── Internal helper ───────────────────────────────────────────────────────────

static uint32_t pack_payload(char c0, char c1, char c2)
{
    // 3 × 7-bit packing identical to WordParser::encode_ascii but without
    // character-set validation (VERSION CMD uses chars outside Basic-38).
    return ((static_cast<uint32_t>(c0) & 0x7Fu) << 14u)
         | ((static_cast<uint32_t>(c1) & 0x7Fu) <<  7u)
         |  (static_cast<uint32_t>(c2) & 0x7Fu);
}

// ── Public API ────────────────────────────────────────────────────────────────

ALEWord encode_version_cmd(const VersionCmd& cmd)
{
    // 3rd char: 's'=summary, 'f'=full; defaults to summary if both/neither bit set.
    const char sub = (cmd.kvf_mask & KVF_FULL) && !(cmd.kvf_mask & KVF_SUMMARY)
                     ? VERSION_FULL_CHAR
                     : VERSION_SUMMARY_CHAR;

    ALEWord word;
    word.type        = PreambleType::CMD;
    word.raw_payload = pack_payload(VERSION_FAMILY_CHAR, VERSION_SEP_CHAR, sub);
    word.address[0]  = VERSION_FAMILY_CHAR;
    word.address[1]  = VERSION_SEP_CHAR;
    word.address[2]  = sub;
    word.address[3]  = '\0';
    word.valid       = true;
    word.fec_errors  = 0;
    word.unanimous_votes = 0;
    word.timestamp_ms    = 0;
    return word;
}

bool is_version_cmd(const ALEWord& word)
{
    if (word.type != PreambleType::CMD) return false;
    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    return c0 == VERSION_FAMILY_CHAR && c1 == VERSION_SEP_CHAR;
}

VersionCmd decode_version_cmd(const ALEWord& word)
{
    const char sub = static_cast<char>(word.raw_payload & 0x7Fu);
    VersionCmd cmd{};
    cmd.kvc_mask = KVC_ALL;
    cmd.kvf_mask = (sub == VERSION_FULL_CHAR) ? KVF_FULL : KVF_SUMMARY;
    return cmd;
}

// ── CAPABILITIES QUERY (A.5.6.6.2.1) ─────────────────────────────────────────

ALEWord encode_capabilities_query(const CapabilitiesQuery& /*query*/)
{
    ALEWord word;
    word.type        = PreambleType::CMD;
    word.raw_payload = pack_payload(CAPS_FAMILY_CHAR, CAPS_SEP_CHAR, CAPS_QUERY_CHAR);
    word.address[0]  = CAPS_FAMILY_CHAR;
    word.address[1]  = CAPS_SEP_CHAR;
    word.address[2]  = CAPS_QUERY_CHAR;
    word.address[3]  = '\0';
    word.valid       = true;
    word.fec_errors  = 0;
    word.unanimous_votes = 0;
    word.timestamp_ms    = 0;
    return word;
}

bool is_capabilities_query(const ALEWord& word)
{
    if (word.type != PreambleType::CMD) return false;
    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((word.raw_payload >>  0u) & 0x7Fu);
    return c0 == CAPS_FAMILY_CHAR && c1 == CAPS_SEP_CHAR && c2 == CAPS_QUERY_CHAR;
}

// ── CAPABILITIES REPORT (A.5.6.6.2.2) ────────────────────────────────────────

static ALEWord make_data_word(PreambleType pt, uint32_t payload)
{
    ALEWord w;
    w.type            = pt;
    w.raw_payload     = payload & 0x1FFFFFu;
    w.valid           = true;
    w.fec_errors      = 0;
    w.unanimous_votes = 0;
    w.timestamp_ms    = 0;
    w.address[0] = w.address[1] = w.address[2] = w.address[3] = '\0';
    return w;
}

std::array<ALEWord, 6> encode_capabilities_report(const CapabilitiesReport& rpt)
{
    std::array<ALEWord, 6> frame;

    // Word 0: CMD c/r
    ALEWord& cmd       = frame[0];
    cmd.type           = PreambleType::CMD;
    cmd.raw_payload    = pack_payload(CAPS_FAMILY_CHAR, CAPS_SEP_CHAR, CAPS_REPORT_CHAR);
    cmd.address[0]     = CAPS_FAMILY_CHAR;
    cmd.address[1]     = CAPS_SEP_CHAR;
    cmd.address[2]     = CAPS_REPORT_CHAR;
    cmd.address[3]     = '\0';
    cmd.valid          = true;
    cmd.fec_errors     = 0;
    cmd.unanimous_votes = 0;
    cmd.timestamp_ms   = 0;

    // Word 1 (DATA): [20:13] scan_rate_chps, [12:5] channels_scanned
    frame[1] = make_data_word(PreambleType::DATA,
        (static_cast<uint32_t>(rpt.scan_rate_chps)   << 13u) |
        (static_cast<uint32_t>(rpt.channels_scanned) <<  5u));

    // Word 2 (REP): [20:13] max_tune_time_ms/10, [12:5] turnaround_time_ms/10
    const uint8_t tune_s = static_cast<uint8_t>(
        rpt.max_tune_time_ms > 2550u ? 0xFFu : rpt.max_tune_time_ms / 10u);
    const uint8_t turn_s = static_cast<uint8_t>(
        rpt.turnaround_time_ms > 2550u ? 0xFFu : rpt.turnaround_time_ms / 10u);
    frame[2] = make_data_word(PreambleType::REP,
        (static_cast<uint32_t>(tune_s) << 13u) |
        (static_cast<uint32_t>(turn_s) <<  5u));

    // Word 3 (DATA): [20:10] activity_timeout_s (11 bits), [9:0] listen_time_ms (10 bits)
    frame[3] = make_data_word(PreambleType::DATA,
        ((static_cast<uint32_t>(rpt.activity_timeout_s) & 0x7FFu) << 10u) |
         (static_cast<uint32_t>(rpt.listen_time_ms)               & 0x3FFu));

    // Word 4 (REP): feature-capability flags
    frame[4] = make_data_word(PreambleType::REP,
        (static_cast<uint32_t>(rpt.accept_all_calls)              << 20u) |
        (static_cast<uint32_t>(rpt.accept_any_calls)              << 19u) |
        (static_cast<uint32_t>(rpt.amd_enabled)                   << 18u) |
        (static_cast<uint32_t>(rpt.dtm_enabled)                   << 17u) |
        (static_cast<uint32_t>(rpt.dbm_enabled)                   << 16u) |
        ((static_cast<uint32_t>(rpt.lp_levels_mask) & 0x3Fu)      << 10u) |
        (static_cast<uint32_t>(rpt.time_service_enabled)          <<  9u) |
        (static_cast<uint32_t>(rpt.alqa_enabled)                  <<  8u) |
        (static_cast<uint32_t>(rpt.orderwire_enabled)             <<  7u) |
        (static_cast<uint32_t>(rpt.scheduling_enabled)            <<  6u));

    // Word 5 (DATA): reserved for future expansion
    frame[5] = make_data_word(PreambleType::DATA, 0u);

    return frame;
}

bool is_capabilities_report(const ALEWord& word)
{
    if (word.type != PreambleType::CMD) return false;
    const char c0 = static_cast<char>((word.raw_payload >> 14u) & 0x7Fu);
    const char c1 = static_cast<char>((word.raw_payload >>  7u) & 0x7Fu);
    const char c2 = static_cast<char>((word.raw_payload >>  0u) & 0x7Fu);
    return c0 == CAPS_FAMILY_CHAR && c1 == CAPS_SEP_CHAR && c2 == CAPS_REPORT_CHAR;
}

} // namespace version_caps
} // namespace ale
