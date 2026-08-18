/**
 * \file apps/bridge/tls_support.cpp
 */

#include "tls_support.h"
#include "PAL/logger.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  pragma comment(lib, "iphlpapi.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <cerrno>
#  include <fcntl.h>
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <fstream>

namespace bridge {

// ── Non-blocking BIO callbacks ──────────────────────────────────────────────
//
// The connection's raw socket handle is encoded directly as the BIO `void*`
// context (no separate allocation) — see attach() below. These translate the
// platform's "would block" error into mbedTLS's WANT_READ/WANT_WRITE sentinel
// returns, which is how mbedTLS knows to retry rather than fail.

namespace {

#ifdef _WIN32
int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    const auto sock = static_cast<SOCKET>(reinterpret_cast<std::intptr_t>(ctx));
    const int n = ::send(sock, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
    if (n == SOCKET_ERROR) {
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE : -1;
    }
    return n;
}
int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    const auto sock = static_cast<SOCKET>(reinterpret_cast<std::intptr_t>(ctx));
    const int n = ::recv(sock, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (n == SOCKET_ERROR) {
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ : -1;
    }
    return n;
}
void set_nonblocking_impl(tls_sock_t sock) {
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &mode);
}
#else
int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    const int sock = static_cast<int>(reinterpret_cast<std::intptr_t>(ctx));
    const ssize_t n = ::send(sock, buf, len, 0);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE : -1;
    }
    return static_cast<int>(n);
}
int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    const int sock = static_cast<int>(reinterpret_cast<std::intptr_t>(ctx));
    const ssize_t n = ::recv(sock, buf, len, 0);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ : -1;
    }
    return static_cast<int>(n);
}
void set_nonblocking_impl(tls_sock_t sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags != -1) fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}
#endif

} // namespace

void tls_set_nonblocking(tls_sock_t sock) { set_nonblocking_impl(sock); }

// ── Local interface enumeration (for certificate SANs) ──────────────────────

namespace {

// Every up, non-loopback IPv4/IPv6 address bound to a local interface — used
// to pre-populate the self-signed cert's SAN list so it validates against
// whatever LAN IP a browser is pointed at, without requiring the caller to
// know it in advance.
std::vector<std::string> local_ip_addresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    ULONG buf_len = 15000;
    std::vector<unsigned char> buf(buf_len);
    auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(buf_len);
        addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &buf_len);
    }
    if (ret != NO_ERROR) return out;
    for (auto* a = addrs; a != nullptr; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* ua = a->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
            char ip[INET6_ADDRSTRLEN] = {0};
            const sockaddr* sa = ua->Address.lpSockaddr;
            const void* addr_ptr = nullptr;
            if (sa->sa_family == AF_INET)
                addr_ptr = &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr;
            else if (sa->sa_family == AF_INET6)
                addr_ptr = &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
            else
                continue;
            if (inet_ntop(sa->sa_family, addr_ptr, ip, sizeof(ip))) out.emplace_back(ip);
        }
    }
#else
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return out;
    for (auto* a = ifap; a != nullptr; a = a->ifa_next) {
        if (a->ifa_addr == nullptr) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        char ip[INET6_ADDRSTRLEN] = {0};
        if (a->ifa_addr->sa_family == AF_INET) {
            const auto* sa = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
            if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) out.emplace_back(ip);
        } else if (a->ifa_addr->sa_family == AF_INET6) {
            const auto* sa = reinterpret_cast<const sockaddr_in6*>(a->ifa_addr);
            if (inet_ntop(AF_INET6, &sa->sin6_addr, ip, sizeof(ip))) out.emplace_back(ip);
        }
    }
    freeifaddrs(ifap);
#endif
    return out;
}

// Raw address bytes (4 for IPv4, 16 for IPv6) if \p s is an IP literal, else
// empty — used to decide whether a SAN entry is an IP-address or DNS-name
// type, and to supply the DER-encoded value the former requires.
std::string ip_literal_bytes(const std::string& s) {
    unsigned char buf[16];
    if (inet_pton(AF_INET, s.c_str(), buf) == 1) return std::string(reinterpret_cast<char*>(buf), 4);
    if (inet_pton(AF_INET6, s.c_str(), buf) == 1) return std::string(reinterpret_cast<char*>(buf), 16);
    return {};
}

void add_unique(std::vector<std::string>& names, const std::string& name) {
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
}

} // namespace

// ── Self-signed certificate generation ──────────────────────────────────────
//
// EC (P-256) rather than RSA: generation is near-instant and the key/cert are
// small — plenty for a LAN tool's self-signed use, and universally supported
// by every browser this GUI targets.

bool ensure_self_signed_cert(const std::string& cert_path, const std::string& key_path,
                              const std::vector<std::string>& extra_san) {
    {
        std::ifstream cert_check(cert_path), key_check(key_path);
        if (cert_check.good() && key_check.good()) return true;  // already present
    }

    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context       key;
    mbedtls_x509write_cert   crt;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);

    bool ok = false;
    unsigned char pem[4096];
    std::string san_log;
    do {
        const char* pers = "openALE-cert-gen";
        if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                reinterpret_cast<const unsigned char*>(pers), std::strlen(pers)) != 0) break;

        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) break;
        if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                mbedtls_ctr_drbg_random, &ctr_drbg) != 0) break;

        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);  // self-signed: issuer == subject
        if (mbedtls_x509write_crt_set_subject_name(&crt, "CN=openALE") != 0) break;
        if (mbedtls_x509write_crt_set_issuer_name(&crt, "CN=openALE") != 0) break;
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

        unsigned char serial[20];
        if (mbedtls_ctr_drbg_random(&ctr_drbg, serial, sizeof(serial)) != 0) break;
        serial[0] &= 0x7F;  // keep the DER INTEGER encoding positive
        if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) != 0) break;

        // 10-year validity: a LAN self-signed cert needs a one-time manual
        // browser trust regardless of exact expiry, so a long window avoids
        // surprising anyone who already saved a trust exception for it.
        if (mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20350101000000") != 0) break;

        // Subject Alternative Names: browsers (Chrome/Edge since v58) verify
        // the connection hostname exclusively against SAN entries, never CN
        // above — a cert without a SAN matching the browser's address bar
        // (e.g. a LAN IP) is rejected outright (NET::ERR_CERT_INVALID)
        // instead of showing the normal overridable self-signed warning.
        std::vector<std::string> san_names = {"localhost", "127.0.0.1", "::1"};
        for (const auto& ip : local_ip_addresses()) add_unique(san_names, ip);
        for (const auto& s : extra_san) add_unique(san_names, s);
        for (const auto& n : san_names) { if (!san_log.empty()) san_log += ", "; san_log += n; }

        // mbedtls_x509_san_list/mbedtls_x509_buf only store pointer+len, so
        // the backing bytes must outlive mbedtls_x509write_crt_pem below —
        // std::deque so push_back never invalidates earlier elements' addresses
        // (the SAN list nodes point at each other via raw `next` pointers).
        std::deque<std::string> san_storage;
        std::deque<mbedtls_x509_san_list> san_nodes;
        mbedtls_x509_san_list* san_head = nullptr;
        mbedtls_x509_san_list* san_tail = nullptr;
        for (const auto& name : san_names) {
            const std::string ip_bytes = ip_literal_bytes(name);
            const bool is_ip = !ip_bytes.empty();
            san_storage.push_back(is_ip ? ip_bytes : name);
            const std::string& stored = san_storage.back();

            mbedtls_x509_san_list node{};
            node.node.type = is_ip ? MBEDTLS_X509_SAN_IP_ADDRESS : MBEDTLS_X509_SAN_DNS_NAME;
            node.node.san.unstructured_name.p =
                reinterpret_cast<unsigned char*>(const_cast<char*>(stored.data()));
            node.node.san.unstructured_name.len = stored.size();
            node.next = nullptr;
            san_nodes.push_back(node);

            if (san_tail) san_tail->next = &san_nodes.back();
            else san_head = &san_nodes.back();
            san_tail = &san_nodes.back();
        }
        if (mbedtls_x509write_crt_set_subject_alternative_name(&crt, san_head) != 0) break;

        if (mbedtls_x509write_crt_pem(&crt, pem, sizeof(pem), mbedtls_ctr_drbg_random, &ctr_drbg) != 0) break;
        {
            std::ofstream out(cert_path, std::ios::binary | std::ios::trunc);
            if (!out) break;
            out.write(reinterpret_cast<char*>(pem), static_cast<std::streamsize>(std::strlen(reinterpret_cast<char*>(pem))));
            if (!out) break;
        }

        if (mbedtls_pk_write_key_pem(&key, pem, sizeof(pem)) != 0) break;
        {
            std::ofstream out(key_path, std::ios::binary | std::ios::trunc);
            if (!out) break;
            out.write(reinterpret_cast<char*>(pem), static_cast<std::streamsize>(std::strlen(reinterpret_cast<char*>(pem))));
            if (!out) break;
        }

        ok = true;
    } while (false);

    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (ok) {
        pal::log_info("tls", "generated self-signed certificate: %s / %s (SAN: %s)",
                       cert_path.c_str(), key_path.c_str(), san_log.c_str());
    }
    return ok;
}

// ── TlsServerContext ─────────────────────────────────────────────────────────

struct TlsServerContext::Impl {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         cert;
    mbedtls_pk_context       pkey;
    mbedtls_ssl_config       conf;

    Impl() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&pkey);
        mbedtls_ssl_config_init(&conf);
    }
    ~Impl() {
        mbedtls_ssl_config_free(&conf);
        mbedtls_pk_free(&pkey);
        mbedtls_x509_crt_free(&cert);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

TlsServerContext::TlsServerContext() : impl_(std::make_unique<Impl>()) {}
TlsServerContext::~TlsServerContext() = default;

bool TlsServerContext::init(const TlsOptions& opts) {
    const std::string cert_path = opts.cert_path.empty() ? "openale_cert.pem" : opts.cert_path;
    const std::string key_path  = opts.key_path.empty()  ? "openale_key.pem"  : opts.key_path;

    if (!ensure_self_signed_cert(cert_path, key_path, opts.extra_san)) {
        pal::log_error("tls", "failed to generate self-signed certificate at %s / %s",
                        cert_path.c_str(), key_path.c_str());
        return false;
    }

    const char* pers = "openALE-tls-server";
    int ret = mbedtls_ctr_drbg_seed(&impl_->ctr_drbg, mbedtls_entropy_func, &impl_->entropy,
                                     reinterpret_cast<const unsigned char*>(pers), std::strlen(pers));
    if (ret != 0) { pal::log_error("tls", "ctr_drbg_seed failed (-0x%04x)", -ret); return false; }

    ret = mbedtls_x509_crt_parse_file(&impl_->cert, cert_path.c_str());
    if (ret != 0) {
        pal::log_error("tls", "failed to load certificate %s (-0x%04x)", cert_path.c_str(), -ret);
        return false;
    }

    ret = mbedtls_pk_parse_keyfile(&impl_->pkey, key_path.c_str(), nullptr,
                                    mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    if (ret != 0) {
        pal::log_error("tls", "failed to load private key %s (-0x%04x)", key_path.c_str(), -ret);
        return false;
    }

    ret = mbedtls_ssl_config_defaults(&impl_->conf, MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) { pal::log_error("tls", "ssl_config_defaults failed (-0x%04x)", -ret); return false; }

    mbedtls_ssl_conf_rng(&impl_->conf, mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    mbedtls_ssl_conf_min_tls_version(&impl_->conf, MBEDTLS_SSL_VERSION_TLS1_2);

    ret = mbedtls_ssl_conf_own_cert(&impl_->conf, &impl_->cert, &impl_->pkey);
    if (ret != 0) { pal::log_error("tls", "ssl_conf_own_cert failed (-0x%04x)", -ret); return false; }

    pal::log_info("tls", "TLS enabled (cert=%s, key=%s)", cert_path.c_str(), key_path.c_str());
    return true;
}

// ── TlsConn ──────────────────────────────────────────────────────────────────

struct TlsConn::Impl {
    mbedtls_ssl_context ssl;
};

TlsConn::TlsConn(TlsServerContext& server_ctx) : impl_(std::make_unique<Impl>()) {
    mbedtls_ssl_init(&impl_->ssl);
    mbedtls_ssl_setup(&impl_->ssl, &server_ctx.impl().conf);
}

TlsConn::~TlsConn() {
    mbedtls_ssl_free(&impl_->ssl);
}

void TlsConn::attach(tls_sock_t sock) {
    // Socket handle encoded directly as the BIO context pointer — no separate
    // per-connection allocation needed.
    mbedtls_ssl_set_bio(&impl_->ssl, reinterpret_cast<void*>(static_cast<std::intptr_t>(sock)),
                         bio_send, bio_recv, nullptr);
}

namespace {
TlsIoResult translate(int ret) {
    if (ret == MBEDTLS_ERR_SSL_WANT_READ)  return TlsIoResult::WantRead;
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) return TlsIoResult::WantWrite;
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return TlsIoResult::Closed;
    return TlsIoResult::Failed;
}
} // namespace

TlsIoResult TlsConn::handshake() {
    const int ret = mbedtls_ssl_handshake(&impl_->ssl);
    if (ret == 0) return TlsIoResult::Ok;
    return translate(ret);
}

TlsIoResult TlsConn::send(const uint8_t* data, size_t len, size_t* out_n) {
    const int ret = mbedtls_ssl_write(&impl_->ssl, data, len);
    if (ret > 0) { *out_n = static_cast<size_t>(ret); return TlsIoResult::Ok; }
    return translate(ret);
}

TlsIoResult TlsConn::recv(uint8_t* buf, size_t len, size_t* out_n) {
    const int ret = mbedtls_ssl_read(&impl_->ssl, buf, len);
    if (ret > 0) { *out_n = static_cast<size_t>(ret); return TlsIoResult::Ok; }
    return translate(ret);
}

void TlsConn::close_notify() {
    // Best-effort, single attempt — see header doc comment.
    mbedtls_ssl_close_notify(&impl_->ssl);
}

} // namespace bridge
