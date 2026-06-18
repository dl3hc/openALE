/**
 * \file src/Protocol/ale_orderwire_protocols.cpp
 * \brief ALE orderwire message encoding (MIL-STD-188-141B A.5.7)
 */

#include "Protocol/ale_orderwire_protocols.h"
#include "Word/ale_word.h"
#include <algorithm>

namespace ale {

// Build a CMD AMD word: CMD preamble (110) with Expanded-64 payload (A.5.7.2.2).
// encode_ascii(DATA) uses the Expanded-64 encoding — same 21-bit payload as DATA/REP
// but transmitted with the CMD preamble so receivers recognise it as an AMD header.
static ALEWord make_cmd_amd_word(const char chars[3])
{
    const uint32_t payload = WordParser::encode_ascii(chars, PreambleType::DATA);
    if (payload == 0xFFFFFFFF) return ALEWord{};  // should not happen after sanitising
    ALEWord w{};
    w.type        = PreambleType::CMD;
    w.raw_payload = payload;
    w.address[0]  = chars[0];
    w.address[1]  = chars[1];
    w.address[2]  = chars[2];
    w.address[3]  = '\0';
    w.valid       = true;
    return w;
}

std::vector<ALEWord> encode_amd(const std::string& text)
{
    const size_t n = std::min(text.size(), size_t{90});  // 30 words × 3 chars (A.5.7.2.3)
    std::vector<ALEWord> words;
    if (n == 0) return words;
    words.reserve((n + 2) / 3);

    for (size_t i = 0; i < n; i += 3) {
        char c[3] = {' ', ' ', ' '};  // SP pad for partial last triplet (A.5.7.2.2)
        for (size_t j = 0; j < 3 && i + j < n; ++j) {
            const char ch = text[i + j];
            c[j] = (static_cast<unsigned char>(ch) >= 0x20
                    && static_cast<unsigned char>(ch) <= 0x5F) ? ch : '?';
        }
        if (words.empty()) {
            // First word: CMD AMD with Expanded-64 payload
            words.push_back(make_cmd_amd_word(c));
        } else {
            // Subsequent words: alternating DATA (odd index) / REP (even index)
            const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                                  :                            PreambleType::REP;
            words.push_back(WordParser::make_word(pt, c));
        }
    }
    return words;
}

} // namespace ale
