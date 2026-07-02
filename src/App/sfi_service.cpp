#include "App/sfi_service.h"

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
        std::printf("[SFI] WinHttpOpen failed (err=%lu)\n", GetLastError());
        return false;
    }

    HINTERNET conn = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) {
        std::printf("[SFI] WinHttpConnect failed (err=%lu)\n", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path,
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) {
        std::printf("[SFI] WinHttpOpenRequest failed (err=%lu)\n", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    bool ok = false;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        std::printf("[SFI] WinHttpSendRequest failed (err=%lu)\n", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        std::printf("[SFI] WinHttpReceiveResponse failed (err=%lu)\n", GetLastError());
    } else {
        // Check HTTP status code
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            std::printf("[SFI] HTTP %lu (expected 200)\n", status);
        } else {
            char buf[4096];
            DWORD got;
            while (WinHttpReadData(req, buf, sizeof(buf) - 1, &got) && got > 0) {
                buf[got] = '\0';
                body += buf;
            }
            std::printf("[SFI] HTTP 200 — received %zu bytes\n", body.size());
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
        std::printf("[SFI] DNS lookup failed for %s\n", host);
        return false;
    }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) { freeaddrinfo(res); std::printf("[SFI] socket() failed\n"); return false; }
    bool ok = false;
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        std::printf("[SFI] connect() to %s:80 failed\n", host);
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
            std::printf("[SFI] HTTP response has no header/body separator\n");
        } else {
            body = raw.substr(sep + 4);
            std::printf("[SFI] HTTP response received — %zu bytes body\n", body.size());
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
    std::printf("[SFI] fetch thread started\n");
    while (running_.load()) {
        std::printf("[SFI] fetching from services.swpc.noaa.gov...\n");
        float sfi = 0.0f;
        if (fetch_sfi(sfi)) {
            std::printf("[SFI] solar flux index: %.0f sfu  (next refresh in %u min)\n",
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
            std::printf("[SFI] fetch failed; retrying in %u s\n", kRetryInitMs / 1000u);
            const auto end = std::chrono::steady_clock::now()
                           + std::chrono::milliseconds(kRetryInitMs);
            while (running_.load() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    std::printf("[SFI] fetch thread stopped\n");
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
        std::printf("[SFI] JSON parse failed — body: %s\n", snip.c_str());
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
