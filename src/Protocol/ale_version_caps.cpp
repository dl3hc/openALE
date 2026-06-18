/**
 * \file ale_version_caps.cpp
 * \brief VERSION CMD + CAPABILITIES Query implementation
 *        (FEAT-GEN-011/012, MIL-STD-188-141B A.5.6.6.1/A.5.6.6.2)
 */

#include "Protocol/ale_version_caps.h"
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
    // Third character: 's' for summary, 'f' for full.
    // Defaults to summary when both bits are set or neither is set.
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

} // namespace version_caps
} // namespace ale
