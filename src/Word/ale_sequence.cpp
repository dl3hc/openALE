/**
 * \file ale_sequence.cpp
 * \brief ALESequence and ALESequenceBuilder implementation
 *
 * Spec: MIL-STD-188-141B §A.5.2.5, §A.5.5.3
 */

#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"

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

ALESequence ALESequenceBuilder::scanning_call_group(const std::string& relay,
                                                    const std::string& dest,
                                                    uint32_t scan_channels) {
    // §A.5.2.5.1 / §A.5.5.4.3: alternating THRU:relay / REP:dest pairs,
    // repeated scan_channels × 2 times (pair = 2 words → total = scan_channels × 4).
    if (scan_channels == 0)
        return ALESequence{};

    const ALEWord thru = AddressEncoder::encode_first(relay, PreambleType::THRU);
    const ALEWord rep  = AddressEncoder::encode_first(dest,  PreambleType::REP);
    const uint32_t pairs = scan_channels * 2u;
    std::vector<ALEWord> words;
    words.reserve(pairs * 2u);
    for (uint32_t i = 0; i < pairs; ++i) {
        words.push_back(thru);
        words.push_back(rep);
    }
    return ALESequence(std::move(words));
}

ALESequence ALESequenceBuilder::leading_call(const std::string& dest) {
    // §A.5.5.3.1: full TO address, sent twice (Tlc = 2×Tc).
    return ALESequence(sent_twice(AddressEncoder::encode(dest, PreambleType::TO)));
}

ALESequence ALESequenceBuilder::leading_call_group(const std::string& relay,
                                                   const std::string& dest) {
    // Short addresses (both ≤ 3 chars) use the compact THRU anchor (Figure e).
    // If either needs extension words, fall back to TO (Figure f).
    bool multi_word = (AddressEncoder::encode(relay, PreambleType::TO).size() > 1 ||
                       AddressEncoder::encode(dest,  PreambleType::TO).size() > 1);
    PreambleType anchor = multi_word ? PreambleType::TO : PreambleType::THRU;
    return ALESequence(sent_twice(AddressEncoder::encode_group({relay, dest}, anchor)));
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
