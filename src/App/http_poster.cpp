#include "App/http_poster.h"
#include "PAL/logger.h"

#include <cstdlib>

// ── URL parsing (shared by both platform backends) ────────────────────────────

namespace {

bool parse_url(const std::string& url, std::string& scheme, std::string& host,
                uint16_t& port, std::string& path) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    scheme = url.substr(0, scheme_end);
    if (scheme != "http" && scheme != "https") return false;

    const size_t host_start = scheme_end + 3;
    const size_t path_start = url.find('/', host_start);
    const std::string hostport = (path_start == std::string::npos)
        ? url.substr(host_start) : url.substr(host_start, path_start - host_start);
    path = (path_start == std::string::npos) ? "/" : url.substr(path_start);

    const size_t colon = hostport.find(':');
    if (colon == std::string::npos) {
        host = hostport;
        port = (scheme == "https") ? 443 : 80;
    } else {
        host = hostport.substr(0, colon);
        try { port = static_cast<uint16_t>(std::stoul(hostport.substr(colon + 1))); }
        catch (...) { return false; }
    }
    return !host.empty();
}

// Builds the three Ed25519 auth header lines (each "Name: value\r\n") for
// `auth`, or an empty string if auth.callsign is empty (unauthenticated
// request — e.g. the public register/probe cases). Shared by both platform
// backends so the wire format can't drift between them.
std::string build_relay_headers(const ale::RelayAuth& auth) {
    if (auth.callsign.empty()) return {};
    std::string h;
    h += "Authorization: Ed25519 " + auth.callsign + "\r\n";
    h += "X-Timestamp: " + auth.timestamp + "\r\n";
    h += "X-Signature: " + auth.signature_b64 + "\r\n";
    return h;
}

} // namespace

namespace ale {

bool location_url_allowed(const std::string& url) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) return false;
    if (scheme == "https") return true;
    return scheme == "http" && (host == "127.0.0.1" || host == "localhost");
}

} // namespace ale

// ── Platform POST backend ──────────────────────────────────────────────────────

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#  include <wincrypt.h>
#  pragma comment(lib, "winhttp.lib")
#  pragma comment(lib, "crypt32.lib")

#  include <cstring>
#  include <fstream>
#  include <vector>

namespace ale {

namespace {

// Loads a PEM certificate file and returns its DER bytes (empty on failure).
std::vector<BYTE> load_pem_cert_der(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    const std::string pem((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DWORD der_len = 0;
    if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                               CRYPT_STRING_BASE64HEADER, nullptr, &der_len, nullptr, nullptr))
        return {};
    std::vector<BYTE> der(der_len);
    if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                               CRYPT_STRING_BASE64HEADER, der.data(), &der_len, nullptr, nullptr))
        return {};
    der.resize(der_len);
    return der;
}

// Relaxes WinHTTP's default chain validation just enough to let an unknown
// (e.g. self-signed) CA through the handshake, so the pin check below gets a
// chance to run at all — hostname/date/usage checks stay enforced. Only
// called when the caller supplied a specific cert to pin against; without
// ca_cert_path, default full system-trust-store validation is untouched.
void winhttp_allow_unknown_ca(HINTERNET req) {
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
}

// True iff the certificate the server actually presented on `req` matches
// ca_cert_path exactly (DER byte-for-byte) — exact-cert pinning rather than
// chain-of-trust validation, appropriate for a known self-signed LAN cert.
bool winhttp_cert_pin_matches(HINTERNET req, const std::string& ca_cert_path) {
    const std::vector<BYTE> pinned_der = load_pem_cert_der(ca_cert_path);
    if (pinned_der.empty()) {
        pal::log_warn("LocationRelay", "ca_cert_path unreadable/invalid PEM: %s", ca_cert_path.c_str());
        return false;
    }
    PCCERT_CONTEXT server_cert = nullptr;
    DWORD ctx_size = sizeof(server_cert);
    if (!WinHttpQueryOption(req, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &server_cert, &ctx_size) || !server_cert) {
        pal::log_warn("LocationRelay", "could not retrieve server certificate for pin check");
        return false;
    }
    const bool match = server_cert->cbCertEncoded == pinned_der.size() &&
        std::memcmp(server_cert->pbCertEncoded, pinned_der.data(), pinned_der.size()) == 0;
    CertFreeCertificateContext(server_cert);
    if (!match) pal::log_warn("LocationRelay", "server certificate does not match the configured ca_cert_path pin");
    return match;
}

} // namespace

bool http_post_json(const std::string& url, const RelayAuth& auth,
                     const std::string& json_body, const std::string& ca_cert_path,
                     HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "malformed URL: %s", url.c_str());
        return false;
    }
    const bool secure = (scheme == "https");
    const std::wstring whost(host.begin(), host.end());
    const std::wstring wpath(path.begin(), path.end());

    HINTERNET session = WinHttpOpen(L"ALE-LocationRelay/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        pal::log_error("LocationRelay", "WinHttpOpen failed (err=%lu)", GetLastError());
        return false;
    }
    HINTERNET conn = WinHttpConnect(session, whost.c_str(), port, 0);
    if (!conn) {
        pal::log_error("LocationRelay", "WinHttpConnect failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", wpath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        pal::log_error("LocationRelay", "WinHttpOpenRequest failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }
    if (secure && !ca_cert_path.empty()) winhttp_allow_unknown_ca(req);

    std::wstring headers = L"Content-Type: application/json\r\n";
    {
        // Ed25519 auth headers are ASCII (callsign/ISO8601/base64) — this
        // narrow->wide widening is safe and deliberately not a full UTF-8
        // conversion.
        const std::string relay_headers = build_relay_headers(auth);
        headers += std::wstring(relay_headers.begin(), relay_headers.end());
    }

    bool completed = false;
    if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<char*>(json_body.data()),
                            static_cast<DWORD>(json_body.size()),
                            static_cast<DWORD>(json_body.size()), 0)) {
        pal::log_warn("LocationRelay", "WinHttpSendRequest failed (err=%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        pal::log_warn("LocationRelay", "WinHttpReceiveResponse failed (err=%lu)", GetLastError());
    } else if (secure && !ca_cert_path.empty() && !winhttp_cert_pin_matches(req, ca_cert_path)) {
        // Cert pin mismatch — treat exactly like any other failed handshake.
    } else {
        DWORD status = 0, status_size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        out.status = static_cast<int>(status);
        char buf[2048];
        DWORD got;
        while (WinHttpReadData(req, buf, sizeof(buf) - 1, &got) && got > 0) {
            buf[got] = '\0';
            out.body += buf;
        }
        completed = true;
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return completed;
}

// Connection health check (GET, no body). Same reachability contract as
// http_post_json: true iff a response round-tripped, regardless of status.
bool http_probe(const std::string& url, const RelayAuth& auth,
                 const std::string& ca_cert_path, HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "probe: malformed URL: %s", url.c_str());
        return false;
    }
    const bool secure = (scheme == "https");
    const std::wstring whost(host.begin(), host.end());
    const std::wstring wpath(path.begin(), path.end());

    HINTERNET session = WinHttpOpen(L"ALE-LocationRelay/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        pal::log_error("LocationRelay", "probe: WinHttpOpen failed (err=%lu)", GetLastError());
        return false;
    }
    HINTERNET conn = WinHttpConnect(session, whost.c_str(), port, 0);
    if (!conn) {
        pal::log_error("LocationRelay", "probe: WinHttpConnect failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", wpath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        pal::log_error("LocationRelay", "probe: WinHttpOpenRequest failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }
    if (secure && !ca_cert_path.empty()) winhttp_allow_unknown_ca(req);

    std::wstring headers;
    {
        const std::string relay_headers = build_relay_headers(auth);
        headers += std::wstring(relay_headers.begin(), relay_headers.end());
    }

    bool completed = false;
    if (!WinHttpSendRequest(req, headers.empty() ? nullptr : headers.c_str(),
                            static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        pal::log_warn("LocationRelay", "probe: WinHttpSendRequest failed (err=%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        pal::log_warn("LocationRelay", "probe: WinHttpReceiveResponse failed (err=%lu)", GetLastError());
    } else if (secure && !ca_cert_path.empty() && !winhttp_cert_pin_matches(req, ca_cert_path)) {
        // Cert pin mismatch — treat exactly like any other failed handshake.
    } else {
        DWORD status = 0, status_size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        out.status = static_cast<int>(status);
        // Drain + discard the body — only reachability/status matters here.
        char buf[2048];
        DWORD got;
        while (WinHttpReadData(req, buf, sizeof(buf) - 1, &got) && got > 0) { /* discard */ }
        completed = true;
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return completed;
}

} // namespace ale

#else
// POSIX: raw HTTP/1.0 framing. https:// is wrapped in mbedTLS (already a
// project dependency — see apps/bridge/tls_support.cpp for the server-side
// counterpart); http:// stays a plain socket, unchanged.
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>

#  include <mbedtls/ctr_drbg.h>
#  include <mbedtls/entropy.h>
#  include <mbedtls/error.h>
#  include <mbedtls/ssl.h>
#  include <mbedtls/x509_crt.h>

#  include <cstring>
#  include <memory>

namespace ale {

namespace {

// mbedTLS BIO callbacks for a blocking POSIX socket — ctx is a pointer to
// the connection's fd. Blocking send()/read() never produce mbedTLS's
// WANT_READ/WANT_WRITE sentinels (those are for non-blocking sockets), so
// these are simple passthroughs; any negative return is treated as a fatal
// I/O error by mbedTLS regardless of its exact value, so a plain -1 suffices
// without pulling in mbedtls/net_sockets.h just for its named constants.
int posix_tls_send(void* ctx, const unsigned char* buf, size_t len) {
    const int fd = *static_cast<int*>(ctx);
    const ssize_t n = ::send(fd, buf, len, 0);
    return n < 0 ? -1 : static_cast<int>(n);
}
int posix_tls_recv(void* ctx, unsigned char* buf, size_t len) {
    const int fd = *static_cast<int*>(ctx);
    const ssize_t n = ::read(fd, buf, len);
    return n < 0 ? -1 : static_cast<int>(n);  // 0 == orderly close, valid for mbedTLS too
}

// One-shot mbedTLS client session for a single request — this file never
// keeps connections alive between calls, so a fresh session per call (like
// the fresh WinHTTP session/connection/request objects in the Windows
// backend above) is simplest.
struct PosixTlsSession {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         ca_cert;
    mbedtls_ssl_config       conf;
    mbedtls_ssl_context      ssl;

    PosixTlsSession() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&ca_cert);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_init(&ssl);
    }
    ~PosixTlsSession() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&ca_cert);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
    PosixTlsSession(const PosixTlsSession&)            = delete;
    PosixTlsSession& operator=(const PosixTlsSession&) = delete;
};

// Common Linux system CA bundle locations, tried in order when the caller
// hasn't pinned a specific certificate. The first one that parses wins.
constexpr const char* kSystemCaBundlePaths[] = {
    "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu
    "/etc/pki/tls/certs/ca-bundle.crt",    // RHEL/Fedora/CentOS
    "/etc/ssl/cert.pem",                   // Alpine and others
};

// Loads the trust anchor into `sess` per the ca_cert_path policy (see the
// file doc comment in http_poster.h): a configured path pins exactly that
// certificate; otherwise fall back to whichever system CA bundle exists.
// Returns false only when nothing could be loaded — the handshake still
// runs with VERIFY_REQUIRED and an empty chain in that case, so it fails
// closed rather than silently trusting an unverified peer.
bool posix_tls_load_trust(PosixTlsSession& sess, const std::string& ca_cert_path) {
    if (!ca_cert_path.empty()) {
        const int ret = mbedtls_x509_crt_parse_file(&sess.ca_cert, ca_cert_path.c_str());
        if (ret == 0) return true;
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        pal::log_warn("LocationRelay", "failed to load ca_cert_path %s: %s",
                       ca_cert_path.c_str(), errbuf);
        return false;
    }
    for (const char* p : kSystemCaBundlePaths) {
        if (mbedtls_x509_crt_parse_file(&sess.ca_cert, p) == 0) return true;
    }
    pal::log_warn("LocationRelay",
                   "no ca_cert_path configured and no system CA bundle found — https:// will "
                   "fail closed; set ca_cert_path to the relay server's certificate");
    return false;
}

// Drives the TLS handshake to completion over an already-connected blocking
// socket `s`. Returns true iff the handshake completed and the peer's
// certificate verified against the loaded trust anchor.
bool posix_tls_handshake(PosixTlsSession& sess, int& s, const std::string& host,
                          const std::string& ca_cert_path) {
    const char* pers = "openALE-LocationRelay";
    if (mbedtls_ctr_drbg_seed(&sess.ctr_drbg, mbedtls_entropy_func, &sess.entropy,
            reinterpret_cast<const unsigned char*>(pers), std::strlen(pers)) != 0) {
        pal::log_warn("LocationRelay", "TLS: ctr_drbg_seed failed");
        return false;
    }
    if (mbedtls_ssl_config_defaults(&sess.conf, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        pal::log_warn("LocationRelay", "TLS: ssl_config_defaults failed");
        return false;
    }
    mbedtls_ssl_conf_rng(&sess.conf, mbedtls_ctr_drbg_random, &sess.ctr_drbg);
    mbedtls_ssl_conf_min_tls_version(&sess.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&sess.conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    posix_tls_load_trust(sess, ca_cert_path);  // best-effort; VERIFY_REQUIRED covers a total miss
    mbedtls_ssl_conf_ca_chain(&sess.conf, &sess.ca_cert, nullptr);

    if (mbedtls_ssl_setup(&sess.ssl, &sess.conf) != 0) {
        pal::log_warn("LocationRelay", "TLS: ssl_setup failed");
        return false;
    }
    if (mbedtls_ssl_set_hostname(&sess.ssl, host.c_str()) != 0) {
        pal::log_warn("LocationRelay", "TLS: set_hostname failed");
        return false;
    }
    mbedtls_ssl_set_bio(&sess.ssl, &s, posix_tls_send, posix_tls_recv, nullptr);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&sess.ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            pal::log_warn("LocationRelay", "TLS handshake with %s failed: %s", host.c_str(), errbuf);
            return false;
        }
    }
    return true;
}

// Sends `req` fully then reads the response until the peer closes the
// connection (this file's HTTP/1.0 requests are always "Connection: close"),
// either directly on the socket or through an established TLS session.
// Returns the raw response bytes; empty on any I/O failure — indistinguish-
// able from "closed before responding", which callers already treat as "no
// response".
std::string posix_transact(int s, mbedtls_ssl_context* ssl, const std::string& req) {
    std::string raw;
    char buf[4096];
    if (ssl) {
        size_t sent = 0;
        while (sent < req.size()) {
            const int n = mbedtls_ssl_write(ssl, reinterpret_cast<const unsigned char*>(req.data()) + sent,
                                             req.size() - sent);
            if (n <= 0) return {};
            sent += static_cast<size_t>(n);
        }
        int n;
        while ((n = mbedtls_ssl_read(ssl, reinterpret_cast<unsigned char*>(buf), sizeof(buf) - 1)) > 0)
            raw.append(buf, static_cast<size_t>(n));
    } else {
        ::send(s, req.c_str(), static_cast<int>(req.size()), 0);
        ssize_t n;
        while ((n = ::read(s, buf, sizeof(buf) - 1)) > 0) raw.append(buf, static_cast<size_t>(n));
    }
    return raw;
}

void parse_http_response(const std::string& raw, HttpPostResult& out) {
    const size_t line_end = raw.find("\r\n");
    const size_t sp1 = raw.find(' ');
    if (line_end != std::string::npos && sp1 != std::string::npos && sp1 < line_end)
        out.status = std::atoi(raw.c_str() + sp1 + 1);
    const size_t sep = raw.find("\r\n\r\n");
    if (sep != std::string::npos) out.body = raw.substr(sep + 4);
}

} // namespace

bool http_post_json(const std::string& url, const RelayAuth& auth,
                     const std::string& json_body, const std::string& ca_cert_path,
                     HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "malformed URL: %s", url.c_str());
        return false;
    }
    const bool secure = (scheme == "https");

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
        pal::log_error("LocationRelay", "DNS lookup failed for %s", host.c_str());
        return false;
    }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        freeaddrinfo(res);
        pal::log_error("LocationRelay", "socket() failed");
        return false;
    }
    bool completed = false;
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        pal::log_error("LocationRelay", "connect() to %s:%u failed", host.c_str(), port);
    } else {
        std::unique_ptr<PosixTlsSession> tls;
        mbedtls_ssl_context* ssl = nullptr;
        bool ready = true;
        if (secure) {
            tls = std::make_unique<PosixTlsSession>();
            ready = posix_tls_handshake(*tls, s, host, ca_cert_path);
            if (ready) ssl = &tls->ssl;
        }
        if (ready) {
            std::string req = "POST " + path + " HTTP/1.0\r\nHost: " + host + "\r\n";
            req += "Content-Type: application/json\r\n";
            req += build_relay_headers(auth);
            req += "Content-Length: " + std::to_string(json_body.size()) + "\r\n";
            req += "Connection: close\r\n\r\n";
            req += json_body;

            const std::string raw = posix_transact(s, ssl, req);
            if (ssl) mbedtls_ssl_close_notify(ssl);
            parse_http_response(raw, out);
            completed = (out.status != 0);
        }
    }
    ::close(s);
    freeaddrinfo(res);
    return completed;
}

// Connection health check (GET, no body). Same TLS/pinning treatment as
// http_post_json above.
bool http_probe(const std::string& url, const RelayAuth& auth,
                 const std::string& ca_cert_path, HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "probe: malformed URL: %s", url.c_str());
        return false;
    }
    const bool secure = (scheme == "https");

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
        pal::log_error("LocationRelay", "probe: DNS lookup failed for %s", host.c_str());
        return false;
    }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        freeaddrinfo(res);
        pal::log_error("LocationRelay", "probe: socket() failed");
        return false;
    }
    bool completed = false;
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        pal::log_error("LocationRelay", "probe: connect() to %s:%u failed", host.c_str(), port);
    } else {
        std::unique_ptr<PosixTlsSession> tls;
        mbedtls_ssl_context* ssl = nullptr;
        bool ready = true;
        if (secure) {
            tls = std::make_unique<PosixTlsSession>();
            ready = posix_tls_handshake(*tls, s, host, ca_cert_path);
            if (ready) ssl = &tls->ssl;
        }
        if (ready) {
            std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\n";
            req += build_relay_headers(auth);
            req += "Connection: close\r\n\r\n";

            const std::string raw = posix_transact(s, ssl, req);
            if (ssl) mbedtls_ssl_close_notify(ssl);
            parse_http_response(raw, out);
            completed = (out.status != 0);
        }
    }
    ::close(s);
    freeaddrinfo(res);
    return completed;
}

} // namespace ale

#endif
