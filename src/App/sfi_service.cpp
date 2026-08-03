#include "App/sfi_service.h"
#include "PAL/logger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

// ── Platform HTTP fetch ───────────────────────────────────────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib, "winhttp.lib")

static bool http_get_body(const wchar_t* host, const wchar_t* path, std::string& body) {
    HINTERNET session = WinHttpOpen(L"ALE-SFI/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        pal::log_error("SFI", "WinHttpOpen failed (err=%lu)", GetLastError());
        return false;
    }

    HINTERNET conn = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) {
        pal::log_error("SFI", "WinHttpConnect failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path,
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) {
        pal::log_error("SFI", "WinHttpOpenRequest failed (err=%lu)", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    bool ok = false;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        pal::log_error("SFI", "WinHttpSendRequest failed (err=%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        pal::log_error("SFI", "WinHttpReceiveResponse failed (err=%lu)", GetLastError());
    } else {
        // Check HTTP status code
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            pal::log_warn("SFI", "HTTP %lu (expected 200)", status);
        } else {
            char buf[4096];
            DWORD got;
            while (WinHttpReadData(req, buf, sizeof(buf) - 1, &got) && got > 0) {
                buf[got] = '\0';
                body += buf;
            }
            pal::log_debug("SFI", "HTTP 200 — received %zu bytes", body.size());
            ok = !body.empty();
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

#else
// POSIX: raw HTTP/1.0 over TCP (no TLS — NOAA also serves on port 80)
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>

static bool http_get_body(const char* host, const char* path, std::string& body) {
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, "80", &hints, &res) != 0) {
        pal::log_error("SFI", "DNS lookup failed for %s", host);
        return false;
    }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) { freeaddrinfo(res); pal::log_error("SFI", "socket() failed"); return false; }
    bool ok = false;
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        pal::log_error("SFI", "connect() to %s:80 failed", host);
    } else {
        std::string req = "GET ";
        req += path;
        req += " HTTP/1.0\r\nHost: ";
        req += host;
        req += "\r\nConnection: close\r\n\r\n";
        ::send(s, req.c_str(), static_cast<int>(req.size()), 0);
        char buf[4096];
        ssize_t n;
        std::string raw;
        while ((n = ::read(s, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            raw += buf;
        }
        const size_t sep = raw.find("\r\n\r\n");
        if (sep == std::string::npos) {
            pal::log_error("SFI", "HTTP response has no header/body separator");
        } else {
            body = raw.substr(sep + 4);
            pal::log_debug("SFI", "HTTP response received — %zu bytes body", body.size());
            ok = !body.empty();
        }
    }
    ::close(s);
    freeaddrinfo(res);
    return ok;
}
#endif

namespace ale {

// ── SfiService ────────────────────────────────────────────────────────────────

void SfiService::start(SfiCallback on_update) {
    if (running_.load()) stop();
    {
        std::lock_guard<std::mutex> g(cb_mtx_);
        on_update_ = std::move(on_update);
    }
    running_ = true;
    worker_ = std::thread(&SfiService::worker_loop, this);
}

void SfiService::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void SfiService::worker_loop() {
    pal::log_info("SFI", "fetch thread started");
    while (running_.load()) {
        pal::log_debug("SFI", "fetching from services.swpc.noaa.gov...");
        float sfi = 0.0f;
        if (fetch_sfi(sfi)) {
            pal::log_info("SFI", "solar flux index: %.0f sfu  (next refresh in %u min)",
                          sfi, kRefreshMs / 60000u);
            sfi_.store(sfi);
            {
                std::lock_guard<std::mutex> g(cb_mtx_);
                if (on_update_) on_update_(sfi);
            }
            const auto end = std::chrono::steady_clock::now()
                           + std::chrono::milliseconds(kRefreshMs);
            while (running_.load() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } else {
            pal::log_warn("SFI", "fetch failed; retrying in %u s", kRetryInitMs / 1000u);
            const auto end = std::chrono::steady_clock::now()
                           + std::chrono::milliseconds(kRetryInitMs);
            while (running_.load() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    pal::log_info("SFI", "fetch thread stopped");
}

bool SfiService::fetch_sfi(float& out) {
    std::string body;
#ifdef _WIN32
    if (!http_get_body(L"services.swpc.noaa.gov",
                       L"/products/summary/10cm-flux.json", body))
        return false;
#else
    if (!http_get_body("services.swpc.noaa.gov",
                       "/products/summary/10cm-flux.json", body))
        return false;
#endif
    if (!parse_sfi_json(body, out)) {
        // Print first 150 chars of body to help diagnose unexpected formats
        const std::string snip = body.size() > 150 ? body.substr(0, 150) + "…" : body;
        pal::log_warn("SFI", "JSON parse failed — body: %s", snip.c_str());
        return false;
    }
    return true;
}

bool SfiService::parse_sfi_json(const std::string& body, float& sfi) {
    // Current NOAA format: [{"flux":201,"time_tag":"2026-07-01T20:00:00"}]
    //   key = "flux" (lowercase), value = bare number
    size_t p = body.find("\"flux\":");
    if (p != std::string::npos) {
        const size_t vs = body.find_first_of("-0123456789", p + 7);
        if (vs != std::string::npos) {
            try {
                const float v = std::stof(body.substr(vs));
                if (v >= 1.0f && v <= 999.0f) { sfi = v; return true; }
            } catch (...) {}
        }
    }
    // Legacy NOAA format: {"Flux":"165","A":"3",...}
    //   key = "Flux" (capitalized), value = quoted string
    p = body.find("\"Flux\":\"");
    if (p != std::string::npos) {
        const size_t vs = p + 8;
        const size_t ve = body.find('"', vs);
        if (ve != std::string::npos && ve != vs) {
            try {
                const float v = std::stof(body.substr(vs, ve - vs));
                if (v >= 1.0f && v <= 999.0f) { sfi = v; return true; }
            } catch (...) {}
        }
    }
    return false;
}

} // namespace ale
