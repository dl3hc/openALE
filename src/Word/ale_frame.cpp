/**
 * \file ale_frame.cpp
 * \brief Frame- und ALEFrameBuilder-Implementierung
 */

#include "Word/ale_frame.h"
#include "Word/address_encoder.h"

namespace ale {

// ── Frame ────────────────────────────────────────────────────────────────────

std::vector<uint64_t> Frame::encode() const {
    std::vector<uint64_t> result;
    result.reserve(words_.size());
    for (const auto& word : words_)
        result.push_back(word.encode());
    return result;
}

// ── Shared frame shapes ───────────────────────────────────────────────────────

// Transmit the given words twice in a row.  The leading call and the addressed
// part of a handshake frame are both sent doubled because Tlc = 2 × Tc
// (MIL-STD-188-141B A.5.5.3.1) — two complete copies give the receiver a second
// chance to acquire the word grid.
static std::vector<ALEWord> sent_twice(const std::vector<ALEWord>& words) {
    std::vector<ALEWord> doubled;
    doubled.reserve(words.size() * 2);
    doubled.insert(doubled.end(), words.begin(), words.end());  // first copy
    doubled.insert(doubled.end(), words.begin(), words.end());  // second copy
    return doubled;
}

// A handshake frame: the addressed station's word(s) sent twice, followed by
// our own single conclusion word.  Used for the accept, ACK and termination
// frames, which share this exact shape and differ only in the conclusion type
// (TIS = identify/accept, TWAS = reject/terminate).
static Frame doubled_address_then_conclusion(const std::string& addressed_addr,
                                             PreambleType       addressed_type,
                                             const std::string& self_addr,
                                             PreambleType       conclusion_type) {
    std::vector<ALEWord> words = sent_twice(AddressEncoder::encode(addressed_addr, addressed_type));
    const auto conclusion = AddressEncoder::encode(self_addr, conclusion_type);
    words.insert(words.end(), conclusion.begin(), conclusion.end());
    return Frame(std::move(words));
}

// ── ALEFrameBuilder ───────────────────────────────────────────────────────────

Frame ALEFrameBuilder::scanning_individual(const std::string& dest) {
    return Frame({ AddressEncoder::encode_first(dest, PreambleType::TO) });
}

Frame ALEFrameBuilder::scanning_group(const std::string& relay, const std::string& dest) {
    return Frame(std::vector<ALEWord>{
        AddressEncoder::encode_first(relay, PreambleType::THRU),
        AddressEncoder::encode_first(dest,  PreambleType::REP)
    });
}

Frame ALEFrameBuilder::leading_individual(const std::string& dest) {
    // Leading call: the full TO address, sent twice (Tlc = 2 × Tc, A.5.5.3.1).
    return Frame(sent_twice(AddressEncoder::encode(dest, PreambleType::TO)));
}

Frame ALEFrameBuilder::leading_group(const std::string& relay, const std::string& dest) {
    // A single-word relay+dest can use the compact THRU anchor; if either address
    // needs extension words, fall back to TO so the group encoding stays valid.
    bool multi_word = (AddressEncoder::encode(relay, PreambleType::TO).size() > 1 ||
                       AddressEncoder::encode(dest,  PreambleType::TO).size() > 1);
    PreambleType anchor = multi_word ? PreambleType::TO : PreambleType::THRU;
    // Leading call, sent twice (Tlc = 2 × Tc, A.5.5.3.1).
    return Frame(sent_twice(AddressEncoder::encode_group({relay, dest}, anchor)));
}

Frame ALEFrameBuilder::conclusion(const std::string& self, bool is_reject) {
    const PreambleType anchor = is_reject ? PreambleType::TWAS : PreambleType::TIS;
    return Frame(AddressEncoder::encode(self, anchor));
}

Frame ALEFrameBuilder::ack_frame(const std::string& to_addr, const std::string& self_addr) {
    // ACK (A.5.5.3.4 / Figure A-31): TO peer (×2) + TIS self.
    return doubled_address_then_conclusion(to_addr,   PreambleType::TO,
                                           self_addr, PreambleType::TIS);
}

Frame ALEFrameBuilder::response_frame(const std::string& caller_addr,
                                       const std::string& self_addr,
                                       bool is_reject) {
    // Reject (FEAT-FRAME-005 / AC-FRAME-010-1): TWAS self only — no TO prefix.
    if (is_reject)
        return Frame(AddressEncoder::encode(self_addr, PreambleType::TWAS));
    // Accept (A.5.5.3.3 / Figure A-30): TO caller (×2) + TIS self.
    return doubled_address_then_conclusion(caller_addr, PreambleType::TO,
                                           self_addr,   PreambleType::TIS);
}

Frame ALEFrameBuilder::termination_frame(const std::string& peer_addr,
                                          const std::string& self_addr) {
    // Termination (T-07 / A.5.5.3.5): TO peer (×2) + TWAS self.
    return doubled_address_then_conclusion(peer_addr, PreambleType::TO,
                                           self_addr, PreambleType::TWAS);
}

} // namespace ale
