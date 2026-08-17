/**
 * \file apps/bridge/tls_support.h
 * \brief Native TLS (mbedTLS) support for WsServer — HTTPS/WSS so the browser
 *        GUI gets a "secure context" (required by getUserMedia/enumerateDevices/
 *        AudioWorklet) over LAN/remote access, not just localhost.
 *
 * Kept mbedTLS-opaque on purpose: ws_server.cpp only ever touches TlsConn/
 * TlsServerContext through this header, never mbedTLS's own headers/types —
 * the .cpp is the only translation unit that includes <mbedtls/...>.
 *
 * Threading: identical to the rest of ws_server.cpp — every method here is
 * called from the single I/O thread only, no locking.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace bridge {

// Platform socket handle — deliberately duplicated from the identical
// platform shim already private to ws_server.cpp/rigctld_server.cpp (see
// those files' top-of-file `raw_sock_t` typedef) rather than sharing a
// header, matching this project's existing convention for that small block.
#ifdef _WIN32
using tls_sock_t = std::intptr_t;  // SOCKET, widened to survive the header boundary
#else
using tls_sock_t = int;
#endif

struct TlsOptions {
    bool        enabled = false;
    std::string cert_path;  // empty → "openale_cert.pem" next to the executable
    std::string key_path;   // empty → "openale_key.pem" next to the executable
};

/// Result of a non-blocking mbedTLS handshake/send/recv step.
enum class TlsIoResult { Ok, WantRead, WantWrite, Failed, Closed };

/// Puts a socket into non-blocking mode. Only ever applied to TLS
/// connections — plaintext connections stay blocking (gated by poll()
/// readiness, unchanged from before TLS support existed).
void tls_set_nonblocking(tls_sock_t sock);

/// Generates a self-signed EC (P-256) certificate + private key and writes
/// them as PEM to \p cert_path / \p key_path, if both files don't already
/// exist. A no-op returning true if they're already present — callers don't
/// need to check existence themselves. Returns false only on a genuine
/// generation/write failure.
bool ensure_self_signed_cert(const std::string& cert_path, const std::string& key_path);

/// Owns the mbedTLS server-wide state (RNG, loaded cert/key, ssl_config)
/// shared by every connection. One instance per WsServer, constructed only
/// when TLS is enabled.
class TlsServerContext {
public:
    TlsServerContext();
    ~TlsServerContext();
    TlsServerContext(const TlsServerContext&)            = delete;
    TlsServerContext& operator=(const TlsServerContext&) = delete;

    /// Auto-generates the cert/key (see ensure_self_signed_cert) if the
    /// configured paths don't exist, loads them, and prepares the shared
    /// server config. Returns false on any unrecoverable error (bad/corrupt
    /// PEM, unwritable directory, ...) — caller should fail startup.
    bool init(const TlsOptions& opts);

    struct Impl;
    Impl& impl() { return *impl_; }  // TlsConn-only access to the mbedTLS internals

private:
    std::unique_ptr<Impl> impl_;
};

/// One per-connection TLS session. The underlying socket must already be in
/// non-blocking mode (tls_set_nonblocking) before attach().
class TlsConn {
public:
    explicit TlsConn(TlsServerContext& server_ctx);
    ~TlsConn();
    TlsConn(const TlsConn&)            = delete;
    TlsConn& operator=(const TlsConn&) = delete;

    /// Bind this session to a raw socket. Call once, immediately after accept().
    void attach(tls_sock_t sock);

    /// Drive the handshake forward by one non-blocking step. Call again
    /// (once per poll() readiness notification) while it returns
    /// WantRead/WantWrite; Ok means the handshake completed.
    TlsIoResult handshake();

    /// Non-blocking encrypted I/O, semantically mirroring send()/recv():
    /// on Ok, *out_n holds the byte count transferred (always > 0); on
    /// WantRead/WantWrite/Failed/Closed, *out_n is left untouched — the
    /// caller should treat WantRead/WantWrite as "no data this round, retry
    /// next poll iteration", exactly like it already does for a plain
    /// socket recv() with nothing ready.
    TlsIoResult send(const uint8_t* data, size_t len, size_t* out_n);
    TlsIoResult recv(uint8_t* buf, size_t len, size_t* out_n);

    /// Best-effort clean shutdown: a single non-blocking close_notify
    /// attempt. The caller closes the raw socket immediately after
    /// regardless of whether this fully completed (avoids adding a whole
    /// extra drain-on-close state machine for a LAN tool's short-lived
    /// per-file HTTP connections).
    void close_notify();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bridge
