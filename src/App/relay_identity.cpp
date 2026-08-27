#include "App/relay_identity.h"
#include "PAL/logger.h"

#include "ed25519.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <bcrypt.h>
#  include <aclapi.h>
#  include <sddl.h>
#  pragma comment(lib, "bcrypt.lib")
#  pragma comment(lib, "advapi32.lib")
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace ale {

std::string iso8601_utc(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

namespace {

// ── Base64 (encode-only for the public key/signature; decode-only for the
// persisted seed) — small self-contained codec. http_poster.cpp does not
// currently expose a reusable base64 helper (checked during implementation:
// only a comment referencing "base64/opaque" tokens, no actual codec), so
// this is a deliberate, minimal, single-purpose implementation kept local
// to this translation unit rather than a shared utility.
constexpr char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += kB64Chars[(n >> 6) & 0x3F];
        out += kB64Chars[n & 0x3F];
    }
    const size_t rem = len - i;
    if (rem == 1) {
        const uint32_t n = uint32_t(data[i]) << 16;
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += kB64Chars[(n >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decodes exactly the bytes present (ignores padding/whitespace at the end);
// returns false if any non-padding character is invalid.
bool base64_decode(const std::string& in, std::string& out) {
    out.clear();
    uint32_t buf = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const int v = base64_decode_char(c);
        if (v < 0) return false;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return true;
}

// ── CSPRNG seed ──────────────────────────────────────────────────────────────

bool generate_seed(unsigned char seed[32]) {
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(nullptr, seed, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        pal::log_error("RelayIdentity", "BCryptGenRandom failed (status=0x%lx)",
                        static_cast<unsigned long>(status));
        return false;
    }
    return true;
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    if (!f) {
        pal::log_error("RelayIdentity", "cannot open /dev/urandom");
        return false;
    }
    f.read(reinterpret_cast<char*>(seed), 32);
    return static_cast<bool>(f);
#endif
}

// Best-effort file-permission hardening. POSIX enforces 0600 at creation
// time (O_EXCL). Windows has no equivalent atomic primitive via <fstream>,
// so this narrows the ACL after the fact and only logs on failure — per the
// design doc, Windows ACL APIs are inconsistent across filesystems (e.g.
// FAT32 network shares) and a failure here must not block startup.
void restrict_key_file_permissions(const std::string& path) {
#ifdef _WIN32
    PSECURITY_DESCRIPTOR sd = nullptr;
    // Owner (OW) + Local System (SY) get full access; everyone else is
    // denied. ConvertStringSecurityDescriptorToSecurityDescriptor is the
    // simplest correct way to build this without hand-rolling ACL structs.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:PAI(A;;FA;;;OW)(A;;FA;;;SY)", SDDL_REVISION_1, &sd, nullptr)) {
        pal::log_warn("RelayIdentity",
                       "could not build restrictive ACL for %s (err=%lu) — file permissions "
                       "left at filesystem defaults", path.c_str(), GetLastError());
        return;
    }
    PACL dacl = nullptr;
    BOOL dacl_present = FALSE, dacl_defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(sd, &dacl_present, &dacl, &dacl_defaulted) || !dacl_present) {
        pal::log_warn("RelayIdentity", "could not extract DACL for %s — permissions left at defaults",
                       path.c_str());
        LocalFree(sd);
        return;
    }
    const DWORD rc = SetNamedSecurityInfoA(
        const_cast<char*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, dacl, nullptr);
    if (rc != ERROR_SUCCESS) {
        pal::log_warn("RelayIdentity",
                       "failed to apply restrictive ACL to %s (err=%lu) — file permissions "
                       "left at filesystem defaults", path.c_str(), rc);
    }
    LocalFree(sd);
#else
    // POSIX: handled at creation via O_CREAT|O_EXCL, mode 0600 (see
    // write_identity_file). Nothing further to do here.
    (void)path;
#endif
}

constexpr const char* kFileMagic = "v1";

bool write_identity_file(const std::string& path, const std::string& callsign,
                          const unsigned char seed[32]) {
    const std::string seed_b64 = base64_encode(seed, 32);
    std::string contents = std::string(kFileMagic) + "\n" + callsign + "\n" + seed_b64 + "\n";

#ifdef _WIN32
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        pal::log_error("RelayIdentity", "failed to open %s for writing", path.c_str());
        return false;
    }
    f << contents;
    f.close();
    if (!f) {
        pal::log_error("RelayIdentity", "failed to write %s", path.c_str());
        return false;
    }
    restrict_key_file_permissions(path);
    return true;
#else
    // O_CREAT|O_EXCL would fail on overwrite (regenerate-on-callsign-change
    // case), so this always truncates-and-writes with 0600 explicitly rather
    // than relying on O_EXCL for the permission guarantee.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        pal::log_error("RelayIdentity", "failed to open %s for writing", path.c_str());
        return false;
    }
    const ssize_t n = ::write(fd, contents.data(), contents.size());
    ::close(fd);
    if (n < 0 || static_cast<size_t>(n) != contents.size()) {
        pal::log_error("RelayIdentity", "failed to write %s", path.c_str());
        return false;
    }
    return true;
#endif
}

// Parses the 3-line key file. Returns false on any structural problem
// (caller then regenerates rather than treating it as fatal).
bool read_identity_file(const std::string& path, std::string& callsign, unsigned char seed[32]) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string magic, callsign_line, seed_b64;
    if (!std::getline(f, magic) || magic != kFileMagic) return false;
    if (!std::getline(f, callsign_line) || callsign_line.empty()) return false;
    if (!std::getline(f, seed_b64) || seed_b64.empty()) return false;

    std::string decoded;
    if (!base64_decode(seed_b64, decoded) || decoded.size() != 32) return false;

    callsign = callsign_line;
    std::memcpy(seed, decoded.data(), 32);
    return true;
}

void derive_keypair(RelayIdentity& identity) {
    ed25519_create_keypair(identity.public_key, identity.private_key, identity.seed);
}

} // namespace

std::optional<RelayIdentity> load_or_create_relay_identity(const std::string& key_file_path,
                                                             const std::string& callsign) {
    RelayIdentity identity;
    std::string stored_callsign;
    unsigned char stored_seed[32];

    if (read_identity_file(key_file_path, stored_callsign, stored_seed)) {
        if (stored_callsign == callsign) {
            identity.callsign = stored_callsign;
            std::memcpy(identity.seed, stored_seed, 32);
            derive_keypair(identity);
            pal::log_info("RelayIdentity", "loaded existing identity for %s from %s",
                           callsign.c_str(), key_file_path.c_str());
            return identity;
        }
        pal::log_info("RelayIdentity",
                       "station callsign changed (%s -> %s) — regenerating relay identity",
                       stored_callsign.c_str(), callsign.c_str());
    }

    unsigned char seed[32];
    if (!generate_seed(seed)) return std::nullopt;

    identity.callsign = callsign;
    std::memcpy(identity.seed, seed, 32);
    derive_keypair(identity);

    if (!write_identity_file(key_file_path, callsign, seed)) return std::nullopt;

    pal::log_info("RelayIdentity", "generated new relay identity for %s at %s",
                   callsign.c_str(), key_file_path.c_str());
    return identity;
}

std::string relay_identity_public_key_b64(const RelayIdentity& identity) {
    return base64_encode(identity.public_key, 32);
}

RelaySignature sign_relay_request(const RelayIdentity& identity, const std::string& body) {
    RelaySignature out;
    out.callsign  = identity.callsign;
    out.timestamp = iso8601_utc(std::time(nullptr));

    const std::string signed_message = out.timestamp + "\n" + body;
    unsigned char signature[64];
    ed25519_sign(signature, reinterpret_cast<const unsigned char*>(signed_message.data()),
                 signed_message.size(), identity.public_key, identity.private_key);
    out.signature_b64 = base64_encode(signature, 64);
    return out;
}

} // namespace ale
