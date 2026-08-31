/**
 * \file address_encoder.cpp
 * \brief AddressEncoder implementation.
 *
 * See address_encoder.h for full design rationale and usage contract.
 *
 * Core encoding rule (A.5.2.3.2.1 / A.5.2.4.3):
 *
 *   chunk_index  0         1      2      3      4
 *   word type    anchor   DATA   REP   DATA   REP
 *
 * "anchor" = first_word_type from the caller (TO, TIS, TWAS, FROM …). The
 * DATA/REP alternation is fixed and identical for every address and call
 * phase — only the anchor type differs.
 *
 * encode_first() = encode().front(), not a separate algorithm: proves
 * scanning and leading call share the same rule, differing only in word count.
 */

#include "Word/address_encoder.h"
#include <algorithm>

namespace ale {

// Extension word types for chunks[1..4] (index 0 = chunk[1], etc.).
// Alternates DATA, REP, DATA, REP per AC-WORD-010-2/3.
static constexpr PreambleType EXT[4] = {
    PreambleType::DATA, PreambleType::REP,
    PreambleType::DATA, PreambleType::REP
};

// ── Private helpers ──────────────────────────────────────────────────────────

// static
std::vector<std::string> AddressEncoder::chunk(const std::string& addr) {
    std::vector<std::string> chunks;
    const size_t len = std::min(addr.size(), size_t(15)); // A.5.2.4.2: max 15 chars

    for (size_t i = 0; i < len; i += 3) {
        std::string c = addr.substr(i, std::min(size_t(3), len - i));
        while (c.size() < 3)
            c += '@';   // A.5.2.4.3: pad with utility symbol '@' (0x40)
        chunks.push_back(std::move(c));
    }

    if (chunks.empty())
        chunks.push_back("@@@");   // degenerate: empty address → one padding word

    return chunks;
}

// static
ALEWord AddressEncoder::make(PreambleType type, const std::string& chunk3) {
    // WordParser::make_word sets address[] and raw_payload via parse_from_bits,
    // ensuring ALEWord::encode() produces the correct 49-bit output on TX.
    return WordParser::make_word(type, chunk3.c_str());
}

// ── Public API ───────────────────────────────────────────────────────────────

// static
std::vector<ALEWord> AddressEncoder::encode(const std::string& addr,
                                             PreambleType           first_word_type) {
    const auto chunks = chunk(addr);

    std::vector<ALEWord> words;
    words.reserve(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i) {
        PreambleType t = (i == 0) ? first_word_type : EXT[i - 1];
        words.push_back(make(t, chunks[i]));
    }

    return words;
}

// static
ALEWord AddressEncoder::encode_first(const std::string& addr,
                                     PreambleType           first_word_type) {
    // Reuses chunk()'s padding/splitting rule without allocating the full
    // word vector — only the first chunk is needed.
    return make(first_word_type, chunk(addr).front());
}

// static
std::vector<ALEWord> AddressEncoder::encode_group(const std::vector<std::string>& addrs,
                                                    PreambleType                        first_word_type) {
    if (addrs.empty())
        return {};

    std::vector<ALEWord> result;

    // Tracks last non-REP preamble type exactly as reconstruct_to_addresses()
    // does, so the "new recipient" decision mirrors the decoder's logic.
    //
    // Rule (mirrors reconstruct_to_addresses):
    //   last_non_rep==TO after address N   → N+1 may begin with REP (decoder
    //                                         treats REP as new recipient)
    //   last_non_rep==DATA after address N → N+1 needs a fresh anchor (TO);
    //                                         REP-after-DATA would extend the
    //                                         current address instead
    PreambleType last_non_rep = PreambleType::UNKNOWN;

    for (size_t a = 0; a < addrs.size(); ++a) {
        const auto chunks = chunk(addrs[a]);

        for (size_t i = 0; i < chunks.size(); ++i) {
            PreambleType t;

            if (i == 0) {
                if (a == 0) {
                    t = first_word_type;    // first address always uses anchor
                } else if (last_non_rep == PreambleType::TO) {
                    // Prev address ended on TO → decoder treats REP as new
                    // recipient (reconstruct_to_addresses).
                    t = PreambleType::REP;
                } else {
                    // Prev address ended on DATA → REP-after-DATA would
                    // extend, not start a new recipient; use a fresh anchor.
                    t = first_word_type;
                }
            } else {
                t = EXT[i - 1];   // extension chunks always follow DATA/REP pattern
            }

            result.push_back(make(t, chunks[i]));

            // REP does not update last_non_rep (mirrors decoder behaviour).
            if (t != PreambleType::REP)
                last_non_rep = t;
        }
    }

    return result;
}

} // namespace ale
