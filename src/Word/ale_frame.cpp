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
    auto words = AddressEncoder::encode(dest, PreambleType::TO);
    std::vector<ALEWord> doubled;
    doubled.reserve(words.size() * 2);
    doubled.insert(doubled.end(), words.begin(), words.end());
    doubled.insert(doubled.end(), words.begin(), words.end());
    return Frame(std::move(doubled));
}

Frame ALEFrameBuilder::leading_group(const std::string& relay, const std::string& dest) {
    bool multi_word = (AddressEncoder::encode(relay, PreambleType::TO).size() > 1 ||
                       AddressEncoder::encode(dest,  PreambleType::TO).size() > 1);
    PreambleType anchor = multi_word ? PreambleType::TO : PreambleType::THRU;
    auto words = AddressEncoder::encode_group({relay, dest}, anchor);
    std::vector<ALEWord> doubled;
    doubled.reserve(words.size() * 2);
    doubled.insert(doubled.end(), words.begin(), words.end());
    doubled.insert(doubled.end(), words.begin(), words.end());
    return Frame(std::move(doubled));
}

Frame ALEFrameBuilder::conclusion(const std::string& self, bool is_reject) {
    const PreambleType anchor = is_reject ? PreambleType::TWAS : PreambleType::TIS;
    return Frame(AddressEncoder::encode(self, anchor));
}

Frame ALEFrameBuilder::ack_frame(const std::string& to_addr, const std::string& self_addr) {
    auto to_words = AddressEncoder::encode(to_addr, PreambleType::TO);
    std::vector<ALEWord> words;
    words.reserve(to_words.size() * 2 + AddressEncoder::encode(self_addr, PreambleType::TIS).size());
    words.insert(words.end(), to_words.begin(), to_words.end());
    words.insert(words.end(), to_words.begin(), to_words.end());
    auto tis_words = AddressEncoder::encode(self_addr, PreambleType::TIS);
    words.insert(words.end(), tis_words.begin(), tis_words.end());
    return Frame(std::move(words));
}

Frame ALEFrameBuilder::response_frame(const std::string& caller_addr,
                                       const std::string& self_addr,
                                       bool is_reject) {
    if (is_reject)
        return Frame(AddressEncoder::encode(self_addr, PreambleType::TWAS));
    auto caller_words = AddressEncoder::encode(caller_addr, PreambleType::TO);
    std::vector<ALEWord> words;
    words.reserve(caller_words.size() * 2 + AddressEncoder::encode(self_addr, PreambleType::TIS).size());
    words.insert(words.end(), caller_words.begin(), caller_words.end());
    words.insert(words.end(), caller_words.begin(), caller_words.end());
    auto tis_words = AddressEncoder::encode(self_addr, PreambleType::TIS);
    words.insert(words.end(), tis_words.begin(), tis_words.end());
    return Frame(std::move(words));
}

Frame ALEFrameBuilder::termination_frame(const std::string& peer_addr,
                                          const std::string& self_addr) {
    auto peer_words = AddressEncoder::encode(peer_addr, PreambleType::TO);
    std::vector<ALEWord> words;
    words.reserve(peer_words.size() * 2 + AddressEncoder::encode(self_addr, PreambleType::TWAS).size());
    words.insert(words.end(), peer_words.begin(), peer_words.end());
    words.insert(words.end(), peer_words.begin(), peer_words.end());
    auto twas_words = AddressEncoder::encode(self_addr, PreambleType::TWAS);
    words.insert(words.end(), twas_words.begin(), twas_words.end());
    return Frame(std::move(words));
}

} // namespace ale
