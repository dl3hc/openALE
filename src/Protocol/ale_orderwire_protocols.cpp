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

// ── DTM — Data Text Message (A.5.7.3) ────────────────────────────────────────

// CRC-16/CCITT (poly 0x1021, init 0xFFFF) over the data bytes (A.5.7.3).
static uint16_t compute_dtm_crc(const char* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(static_cast<unsigned char>(data[i])) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                                  : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

std::vector<ALEWord> encode_dtm(const std::string& text, bool crc_enabled)
{
    const size_t n = std::min(text.size(), size_t{90});  // 30 data words × 3 chars

    std::vector<ALEWord> words;
    words.reserve(1 + (n + 2) / 3 + (crc_enabled ? 1 : 0));

    // Word 0: CMD DTM — Basic-38 identifier "DTM" (A.5.7.3)
    const char dtm_id[3] = {'D', 'T', 'M'};
    words.push_back(WordParser::make_word(PreambleType::CMD, dtm_id));

    // Data words: Expanded-64, alternating DATA/REP
    for (size_t i = 0; i < n; i += 3) {
        char c[3] = {' ', ' ', ' '};
        for (size_t j = 0; j < 3 && i + j < n; ++j) {
            const char ch = text[i + j];
            c[j] = (static_cast<unsigned char>(ch) >= 0x20
                    && static_cast<unsigned char>(ch) <= 0x5F) ? ch : '?';
        }
        // Odd total words so far → next is DATA; even → REP (maintains alternation after CMD)
        const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                              :                            PreambleType::REP;
        words.push_back(WordParser::make_word(pt, c));
    }

    // Optional CRC word: DATA or REP preamble (avoids consecutive same preamble)
    if (crc_enabled) {
        const uint16_t crc = compute_dtm_crc(text.c_str(), n);
        // Encode 16-bit CRC into 3 Expanded-64 chars (6 bits each, low-to-high)
        char crc_chars[3];
        crc_chars[0] = static_cast<char>(0x20 + (crc         & 0x3Fu));
        crc_chars[1] = static_cast<char>(0x20 + ((crc >>  6) & 0x3Fu));
        crc_chars[2] = static_cast<char>(0x20 + ((crc >> 12) & 0x0Fu));
        const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                              :                            PreambleType::REP;
        words.push_back(WordParser::make_word(pt, crc_chars));
    }

    return words;
}

// ── DBM — Data Block Message (A.5.7.4) ────────────────────────────────────────

// Build a transparent-binary DATA/REP word from 3 raw bytes.
// DBM is not restricted to Expanded-64: any byte 0x00–0xFF is valid.
// The MSB of each byte is masked off (& 0x7F) to fit the 7-bit ALE character slot.
static ALEWord make_dbm_data_word(PreambleType pt, const uint8_t bytes[3])
{
    ALEWord w{};
    w.type = pt;
    const uint32_t b0 = bytes[0] & 0x7Fu;
    const uint32_t b1 = bytes[1] & 0x7Fu;
    const uint32_t b2 = bytes[2] & 0x7Fu;
    w.raw_payload = (b0 << 14) | (b1 << 7) | b2;
    w.address[0]  = static_cast<char>(b0);
    w.address[1]  = static_cast<char>(b1);
    w.address[2]  = static_cast<char>(b2);
    w.address[3]  = '\0';
    w.valid       = true;
    return w;
}

// CRC-16/CCITT (poly 0x1021, init 0xFFFF) over raw binary bytes (A.5.7.4).
static uint16_t compute_dbm_crc(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                                  : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

std::vector<ALEWord> encode_dbm(const std::vector<uint8_t>& payload, bool crc_enabled)
{
    std::vector<ALEWord> words;
    words.reserve(1 + (payload.size() + 2) / 3 + (crc_enabled ? 1 : 0));

    // Word 0: CMD DBM — Basic-38 identifier "DBM" (A.5.7.4)
    const char dbm_id[3] = {'D', 'B', 'M'};
    words.push_back(WordParser::make_word(PreambleType::CMD, dbm_id));

    // Data words: transparent binary, alternating DATA/REP
    for (size_t i = 0; i < payload.size(); i += 3) {
        uint8_t bytes[3] = {0, 0, 0};  // zero-pad partial last triplet
        for (size_t j = 0; j < 3 && i + j < payload.size(); ++j)
            bytes[j] = payload[i + j];
        const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                              :                            PreambleType::REP;
        words.push_back(make_dbm_data_word(pt, bytes));
    }

    // CRC word: CRC-16/CCITT over the payload bytes
    if (crc_enabled) {
        const uint16_t crc = compute_dbm_crc(payload.data(), payload.size());
        const uint8_t crc_bytes[3] = {
            static_cast<uint8_t>(crc >> 8),   // high byte
            static_cast<uint8_t>(crc & 0xFFu), // low byte
            0x00u                              // padding
        };
        const PreambleType pt = (words.size() % 2 == 1) ? PreambleType::DATA
                              :                            PreambleType::REP;
        words.push_back(make_dbm_data_word(pt, crc_bytes));
    }

    return words;
}

} // namespace ale
