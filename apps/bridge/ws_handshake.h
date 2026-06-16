/**
 * \file apps/bridge/ws_handshake.h
 * \brief RFC6455 opening-handshake helpers (Sec-WebSocket-Accept) — header-only.
 */
#pragma once

#include "sha1.h"
#include "base64.h"
#include <string>

namespace bridge {

// RFC6455 §1.3 — fixed magic GUID appended to the client's Sec-WebSocket-Key
// before SHA1+Base64. Not a secret; it's the spec's own constant.
inline const std::string& ws_guid() {
    static const std::string g = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return g;
}

/**
 * Compute the Sec-WebSocket-Accept value for a given client Sec-WebSocket-Key.
 * RFC6455 example: key "dGhlIHNhbXBsZSBub25jZQ==" -> "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
 */
inline std::string compute_accept_key(const std::string& client_key) {
    const auto digest = sha1_digest(client_key + ws_guid());
    return base64_encode(digest.data(), digest.size());
}

} // namespace bridge
