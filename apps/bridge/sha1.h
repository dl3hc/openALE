/**
 * \file apps/bridge/sha1.h
 * \brief Minimal SHA-1 (FIPS 180-4) — header-only.
 *
 * Only used for the RFC6455 WebSocket handshake (Sec-WebSocket-Accept).
 * Not for anything security-sensitive — SHA-1 is exactly what the WebSocket
 * spec mandates for that handshake, nothing more.
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace bridge {

inline uint32_t sha1_rotl(uint32_t v, int bits) {
    return (v << bits) | (v >> (32 - bits));
}

/** SHA-1 digest of \p msg, 20 raw bytes. */
inline std::array<uint8_t, 20> sha1_digest(const std::string& msg) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // ── Padding ──────────────────────────────────────────────────────────
    std::string data = msg;
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8u;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back(static_cast<char>(0x00));
    for (int i = 7; i >= 0; --i)
        data.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

    // ── Process 64-byte chunks ──────────────────────────────────────────
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(data.data() + chunk + i * 4);
            w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                 | (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);         k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                  k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                  k = 0xCA62C1D6; }

            const uint32_t temp = sha1_rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out{};
    uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }
    return out;
}

} // namespace bridge
