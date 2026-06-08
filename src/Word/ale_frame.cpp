/**
 * \file ale_frame.cpp
 * \brief Frame-Implementierung
 */

#include "Word/ale_frame.h"

namespace ale {

std::vector<uint64_t> Frame::encode() const {
    std::vector<uint64_t> result;
    result.reserve(words_.size());
    for (const auto& word : words_)
        result.push_back(word.encode());
    return result;
}

} // namespace ale
