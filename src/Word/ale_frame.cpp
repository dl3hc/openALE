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
    return Frame(AddressEncoder::encode(dest, PreambleType::TO));
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

Frame ALEFrameBuilder::conclusion(const std::string& self) {
    return Frame(AddressEncoder::encode(self, PreambleType::TIS));
}

} // namespace ale
