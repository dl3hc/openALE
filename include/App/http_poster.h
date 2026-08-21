/**
 * @file App/http_poster.h
 * @brief Thin HTTP POST abstraction for LocationRelayService (Konzept §19.1).
 *
 * Windows: WinHTTP with TLS (WINHTTP_FLAG_SECURE for https://). POSIX: raw
 * HTTP/1.0 for http:// only (local test-server exception, see
 * location_url_allowed()) — https:// on POSIX logs a WARN and fails until a
 * real TLS backend (OpenSSL/libcurl) is added, per the concept doc's Open
 * Question #1.
 */
#pragma once

#include <string>

namespace ale {

struct HttpPostResult {
    int         status = 0;  ///< HTTP status code; 0 = no response ever received
    std::string body;        ///< response body (diagnostics only)
};

/// POSTs json_body to url with "Content-Type: application/json" and, if
/// token is non-empty, "Authorization: Bearer <token>". Returns true iff a
/// response round-tripped — check out.status for the outcome. A false return
/// means no response at all (DNS/connect/TLS failure, or unsupported scheme
/// on this platform), distinct from a false-but-nonzero-status HTTP failure.
bool http_post_json(const std::string& url, const std::string& token,
                     const std::string& json_body, HttpPostResult& out);

/// HTTPS-only policy (Konzept §10): true for https://, and for
/// http://127.0.0.1 or http://localhost (local test-server exception).
bool location_url_allowed(const std::string& url);

} // namespace ale
