/**
 * \file apps/bridge/base64.h
 * \brief Minimal Base64 encoder — header-only.
 *
 * Only encoding is needed (RFC6455 Sec-WebSocket-Accept); no decoder.
 */
#pragma once

#include <cstdint>
#include <string>

namespace bridge {

inline std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16)
                          | (static_cast<uint32_t>(data[i + 1]) << 8)
                          |  static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
    }

    const size_t rem = len - i;
    if (rem == 1) {
        const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

inline std::string base64_encode(const std::string& s) {
    return base64_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

} // namespace bridge
