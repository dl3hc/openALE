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
#  pragma comment(lib, "winhttp.lib")

namespace ale {

bool http_post_json(const std::string& url, const std::string& token,
                     const std::string& json_body, HttpPostResult& out) {
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

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!token.empty()) {
        // Bearer tokens are ASCII (base64/opaque) — this narrow->wide widening
        // is safe and deliberately not a full UTF-8 conversion.
        const std::wstring wtoken(token.begin(), token.end());
        headers += L"Authorization: Bearer " + wtoken + L"\r\n";
    }

    bool completed = false;
    if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<char*>(json_body.data()),
                            static_cast<DWORD>(json_body.size()),
                            static_cast<DWORD>(json_body.size()), 0)) {
        pal::log_warn("LocationRelay", "WinHttpSendRequest failed (err=%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        pal::log_warn("LocationRelay", "WinHttpReceiveResponse failed (err=%lu)", GetLastError());
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
bool http_probe(const std::string& url, const std::string& token, HttpPostResult& out) {
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

    std::wstring headers;
    if (!token.empty()) {
        const std::wstring wtoken(token.begin(), token.end());
        headers += L"Authorization: Bearer " + wtoken + L"\r\n";
    }

    bool completed = false;
    if (!WinHttpSendRequest(req, headers.empty() ? nullptr : headers.c_str(),
                            static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        pal::log_warn("LocationRelay", "probe: WinHttpSendRequest failed (err=%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        pal::log_warn("LocationRelay", "probe: WinHttpReceiveResponse failed (err=%lu)", GetLastError());
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
// POSIX: raw HTTP/1.0 for http:// only (local test server — see
// location_url_allowed()). https:// has no TLS backend here yet.
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>

namespace ale {

bool http_post_json(const std::string& url, const std::string& token,
                     const std::string& json_body, HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "malformed URL: %s", url.c_str());
        return false;
    }
    if (scheme == "https") {
        pal::log_warn("LocationRelay",
                       "HTTPS relay needs a TLS backend on Linux (OpenSSL/libcurl) — "
                       "not yet implemented, dropping report");
        return false;
    }

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
        std::string req = "POST " + path + " HTTP/1.0\r\nHost: " + host + "\r\n";
        req += "Content-Type: application/json\r\n";
        if (!token.empty()) req += "Authorization: Bearer " + token + "\r\n";
        req += "Content-Length: " + std::to_string(json_body.size()) + "\r\n";
        req += "Connection: close\r\n\r\n";
        req += json_body;
        ::send(s, req.c_str(), static_cast<int>(req.size()), 0);

        char buf[4096];
        ssize_t n;
        std::string raw;
        while ((n = ::read(s, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            raw += buf;
        }
        const size_t line_end = raw.find("\r\n");
        const size_t sp1 = raw.find(' ');
        if (line_end != std::string::npos && sp1 != std::string::npos && sp1 < line_end)
            out.status = std::atoi(raw.c_str() + sp1 + 1);
        const size_t sep = raw.find("\r\n\r\n");
        if (sep != std::string::npos) out.body = raw.substr(sep + 4);
        completed = (out.status != 0);
    }
    ::close(s);
    freeaddrinfo(res);
    return completed;
}

// Connection health check (GET, no body) — raw HTTP/1.0 for http:// only,
// mirroring http_post_json's POSIX limitations (https:// needs a TLS backend).
bool http_probe(const std::string& url, const std::string& token, HttpPostResult& out) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!parse_url(url, scheme, host, port, path)) {
        pal::log_warn("LocationRelay", "probe: malformed URL: %s", url.c_str());
        return false;
    }
    if (scheme == "https") {
        pal::log_warn("LocationRelay",
                       "probe: HTTPS relay needs a TLS backend on Linux — not yet implemented");
        return false;
    }

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
        std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\n";
        if (!token.empty()) req += "Authorization: Bearer " + token + "\r\n";
        req += "Connection: close\r\n\r\n";
        ::send(s, req.c_str(), static_cast<int>(req.size()), 0);

        char buf[4096];
        ssize_t n;
        std::string raw;
        while ((n = ::read(s, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            raw += buf;
            // Only the status line matters — stop once we've parsed it.
            if (out.status != 0 && raw.find("\r\n\r\n") != std::string::npos) break;
        }
        const size_t line_end = raw.find("\r\n");
        const size_t sp1 = raw.find(' ');
        if (line_end != std::string::npos && sp1 != std::string::npos && sp1 < line_end)
            out.status = std::atoi(raw.c_str() + sp1 + 1);
        completed = (out.status != 0);
    }
    ::close(s);
    freeaddrinfo(res);
    return completed;
}

} // namespace ale

#endif
