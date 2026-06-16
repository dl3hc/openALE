/**
 * \file ale_sequence.cpp
 * \brief ALESequence and ALESequenceBuilder implementation
 *
 * Spec: MIL-STD-188-141B §A.5.2.5, §A.5.5.3
 */

#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
#include <algorithm>

namespace ale {

// ── ALESequence ──────────────────────────────────────────────────────────────

std::vector<uint64_t> ALESequence::encode() const {
    std::vector<uint64_t> result;
    result.reserve(words_.size());
    for (const auto& word : words_)
        result.push_back(word.encode());
    return result;
}

// ── Internal helpers ─────────────────────────────────────────────────────────

// Send the given word sequence twice end-to-end.
// Used for leading call (Tlc = 2×Tc, §A.5.5.3.1) and the addressed
// part of response/ACK/termination frames (TO×2 prefix).
static std::vector<ALEWord> sent_twice(const std::vector<ALEWord>& words) {
    std::vector<ALEWord> doubled;
    doubled.reserve(words.size() * 2);
    doubled.insert(doubled.end(), words.begin(), words.end());
    doubled.insert(doubled.end(), words.begin(), words.end());
    return doubled;
}

// Build a complete response-shape frame: the addressed station's word(s)
// sent twice, followed by the conclusion word(s) of the transmitting station.
// Used for accept, ACK, and termination frames, which share this shape.
static ALESequence addressed_then_conclusion(const std::string& addressed_addr,
                                             PreambleType       addressed_type,
                                             const std::string& self_addr,
                                             PreambleType       conclusion_type) {
    std::vector<ALEWord> words = sent_twice(
        AddressEncoder::encode(addressed_addr, addressed_type));
    const auto conc = AddressEncoder::encode(self_addr, conclusion_type);
    words.insert(words.end(), conc.begin(), conc.end());
    return ALESequence(std::move(words));
}

// ── ALESequenceBuilder ───────────────────────────────────────────────────────

ALESequence ALESequenceBuilder::scanning_call(const std::string& dest,
                                              uint32_t scan_channels) {
    // §A.5.2.5.1: scanning section = first address word only, no DATA/REP.
    // Tsc = scan_channels × 2 × Trw → materialize as scan_channels × 2 words.
    if (scan_channels == 0)
        return ALESequence{};

    const ALEWord first = AddressEncoder::encode_first(dest, PreambleType::TO);
    const uint32_t count = scan_channels * 2u;
    std::vector<ALEWord> words(count, first);
    return ALESequence(std::move(words));
}

ALESequence ALESequenceBuilder::scanning_call_group(const std::vector<std::string>& members,
                                                    uint32_t scan_channels) {
    // §A.5.5.4.3.1: collect first words of all members, drop duplicates
    // ("sent only once during Tsc"), cap at 5 unique words.
    if (scan_channels == 0 || members.empty())
        return ALESequence{};

    std::vector<std::string> unique_first;
    for (const auto& m : members) {
        const std::string fw = AddressEncoder::encode_first(m, PreambleType::TO).address;
        if (std::find(unique_first.begin(), unique_first.end(), fw) == unique_first.end())
            unique_first.push_back(fw);
        if (unique_first.size() == 5)
            break;
    }

    // Flowchart fallback (A.5.2.5.1, "IS THERE A SINGLE WORD REMAINING?"): once
    // de-duplicated, a single surviving first word is an individual/net scanning
    // call (TO), not a THRU/REP rotation — THRU only has meaning when ≥2 distinct
    // targets are being rotated.
    if (unique_first.size() == 1)
        return scanning_call(unique_first.front(), scan_channels);

    // Same total airtime as an individual scanning call (Tsc = scan_channels × 2 × Trw),
    // rotating through the unique first words. THRU/REP strictly alternate by word
    // position (AC-WORD-006-2) — scan_channels × 2 is always even, so the sequence
    // always ends on a complete THRU-REP pair.
    const uint32_t total = scan_channels * 2u;
    const size_t   u     = unique_first.size();
    std::vector<ALEWord> words;
    words.reserve(total);
    for (uint32_t i = 0; i < total; ++i) {
        const PreambleType p = (i % 2 == 0) ? PreambleType::THRU : PreambleType::REP;
        words.push_back(AddressEncoder::encode_first(unique_first[i % u], p));
    }
    return ALESequence(std::move(words));
}

ALESequence ALESequenceBuilder::leading_call(const std::string& dest) {
    // §A.5.5.3.1: full TO address, sent twice (Tlc = 2×Tc).
    return ALESequence(sent_twice(AddressEncoder::encode(dest, PreambleType::TO)));
}

ALESequence ALESequenceBuilder::leading_call_group(const std::vector<std::string>& members) {
    // §A.5.5.4.3.2: complete addresses of all prospective group members, sent
    // twice, using TO preambles "as usual" — THRU is exclusively a scanning-
    // section preamble (AC-WORD-006-1/7) and must never appear in the leading call.
    return ALESequence(sent_twice(AddressEncoder::encode_group(members, PreambleType::TO)));
}

ALESequence ALESequenceBuilder::conclusion(const std::string& self, bool is_reject) {
    const PreambleType anchor = is_reject ? PreambleType::TWAS : PreambleType::TIS;
    return ALESequence(AddressEncoder::encode(self, anchor));
}

ALESequence ALESequenceBuilder::response(const std::string& caller_addr,
                                         const std::string& self_addr,
                                         bool is_reject) {
    // Reject (§AC-FRAME-010-1): TWAS self only — no TO prefix.
    if (is_reject)
        return ALESequence(AddressEncoder::encode(self_addr, PreambleType::TWAS));
    // Accept (§A.5.5.3.3 / Figure A-30): TO caller (×2) + TIS self.
    return addressed_then_conclusion(caller_addr, PreambleType::TO,
                                     self_addr,   PreambleType::TIS);
}

ALESequence ALESequenceBuilder::ack(const std::string& peer_addr,
                                    const std::string& self_addr) {
    // §A.5.5.3.4 / Figure A-31: TO peer (×2) + TIS self.
    return addressed_then_conclusion(peer_addr, PreambleType::TO,
                                     self_addr, PreambleType::TIS);
}

ALESequence ALESequenceBuilder::termination(const std::string& peer_addr,
                                            const std::string& self_addr) {
    // §A.5.5.3.5 / T-07: TO peer (×2) + TWAS self.
    return addressed_then_conclusion(peer_addr, PreambleType::TO,
                                     self_addr, PreambleType::TWAS);
}

} // namespace ale
