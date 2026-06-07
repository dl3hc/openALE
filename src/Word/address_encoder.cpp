/**
 * \file address_encoder.cpp
 * \brief AddressEncoder implementation.
 *
 * See address_encoder.h for the full design rationale and usage contract.
 *
 * Core encoding rule (A.5.2.3.2.1 / A.5.2.4.3):
 *
 *   chunk_index  0         1      2      3      4
 *   word type    anchor   DATA   REP   DATA   REP
 *
 * "anchor" = first_word_type supplied by the caller (TO, TIS, TWAS, FROM …).
 * The DATA/REP alternation is fixed and identical for every address and every
 * call phase.  Only the anchor type differs between sites.
 *
 * encode_first() is not a separate algorithm — it is encode().front().
 * This is intentional: it proves that scanning and leading call use the same
 * underlying rule and differ only in how many words are consumed.
 */

#include "Word/address_encoder.h"
#include <algorithm>

namespace ale {

// Extension word types for chunks[1..4].  Index 0 = chunk[1], etc.
// Alternates DATA, REP, DATA, REP per AC-WORD-010-2/3.
static constexpr WordType EXT[4] = {
    WordType::DATA, WordType::REP,
    WordType::DATA, WordType::REP
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
ALEWord AddressEncoder::make(WordType type, const std::string& chunk3) {
    ALEWord w;
    w.type       = type;
    w.address[0] = chunk3[0];
    w.address[1] = chunk3[1];
    w.address[2] = chunk3[2];
    w.address[3] = '\0';
    w.valid      = true;
    // timestamp_ms is left at 0; the transmit path stamps it at send time.
    return w;
}

// ── Public API ───────────────────────────────────────────────────────────────

// static
std::vector<ALEWord> AddressEncoder::encode(const std::string& addr,
                                             WordType           first_word_type) {
    const auto chunks = chunk(addr);

    std::vector<ALEWord> words;
    words.reserve(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i) {
        WordType t = (i == 0) ? first_word_type : EXT[i - 1];
        words.push_back(make(t, chunks[i]));
    }

    return words;
}

// static
ALEWord AddressEncoder::encode_first(const std::string& addr,
                                     WordType           first_word_type) {
    // Deliberately delegates to encode() to guarantee identical behaviour.
    // "Scanning uses the same rule as leading call, just the first word only."
    return encode(addr, first_word_type).front();
}

// static
std::vector<ALEWord> AddressEncoder::encode_group(const std::vector<std::string>& addrs,
                                                    WordType                        first_word_type) {
    if (addrs.empty())
        return {};

    std::vector<ALEWord> result;

    // Track the last non-REP preamble type exactly as reconstruct_to_addresses()
    // does, so that the "new recipient" decision mirrors the decoder's logic.
    //
    // Rule (mirrors reconstruct_to_addresses):
    //   last_non_rep == anchor(TO) after processing address N
    //     → address N+1 can begin with REP, which decoder treats as new recipient
    //   last_non_rep == DATA after processing address N
    //     → address N+1 must begin with a fresh anchor word (TO), because
    //       REP-after-DATA extends the current address in the decoder
    WordType last_non_rep = WordType::UNKNOWN;

    for (size_t a = 0; a < addrs.size(); ++a) {
        const auto chunks = chunk(addrs[a]);

        for (size_t i = 0; i < chunks.size(); ++i) {
            WordType t;

            if (i == 0) {
                if (a == 0) {
                    t = first_word_type;    // first address always uses anchor
                } else if (last_non_rep == WordType::TO
                        || last_non_rep == first_word_type) {
                    // Previous address ended on an anchor word (single-word address
                    // or a REP that the decoder treats as anchor-equivalent).
                    // REP is the compact "new recipient" marker.
                    t = WordType::REP;
                } else {
                    // Previous address ended on DATA → REP-after-DATA would extend,
                    // not start a new recipient.  Use a new anchor word instead.
                    t = first_word_type;
                }
            } else {
                t = EXT[i - 1];   // extension chunks always follow DATA/REP pattern
            }

            result.push_back(make(t, chunks[i]));

            // REP does not update last_non_rep (mirrors the decoder's behaviour).
            if (t != WordType::REP)
                last_non_rep = t;
        }
    }

    return result;
}

} // namespace ale
