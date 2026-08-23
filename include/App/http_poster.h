/**
 * @file App/http_poster.h
 * @brief Thin HTTP POST abstraction for LocationRelayService (Konzept §19.1).
 *
 * Windows: WinHTTP with TLS (WINHTTP_FLAG_SECURE for https://). POSIX: raw
 * HTTP/1.0 framing, with mbedTLS wrapping the socket for https:// (mbedTLS is
 * already a project dependency — see the GUI bridge's own HTTPS/WSS support).
 *
 * Certificate trust (both backends): if ca_cert_path is non-empty, the
 * connection is pinned to exactly that certificate — the expected case for a
 * self-signed LAN relay server. If empty, falls back to the platform's
 * default trust store (works for a real CA-signed cert; fails closed for a
 * self-signed one, with a log line pointing at ca_cert_path).
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
/// means no response at all (DNS/connect/TLS/cert-pin failure), distinct
/// from a false-but-nonzero-status HTTP failure. ca_cert_path pins the
/// expected server certificate for https:// (see file doc comment); ignored
/// for http://.
bool http_post_json(const std::string& url, const std::string& token,
                     const std::string& json_body, const std::string& ca_cert_path,
                     HttpPostResult& out);

/// GETs url (no body) with the Bearer token if non-empty. Returns true iff a
/// response round-tripped — i.e. the host was reachable and answered. Used by
/// LocationRelayService's periodic connection health check; never writes data
/// (a probe, not an ingest). out.status is the HTTP code (0 if no response).
/// A true return with out.status==0 would be inconsistent on these backends —
/// treat any true return as "reachable", and out.status for the sub-label.
/// ca_cert_path: see http_post_json.
bool http_probe(const std::string& url, const std::string& token,
                 const std::string& ca_cert_path, HttpPostResult& out);

/// HTTPS-only policy (Konzept §10): true for https://, and for
/// http://127.0.0.1 or http://localhost (local test-server exception).
bool location_url_allowed(const std::string& url);

} // namespace ale
