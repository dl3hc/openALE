// Standalone smoke test for App/relay_identity.{h,cpp} — Ed25519 identity
// generation, signing, and key-file persistence for Location Relay. Matches
// the project's existing standalone add_executable pattern (see
// test_rigctld_protocol.cpp) — no CTest/enable_testing(), run manually:
//
//   build/test_relay_identity[.exe]
//
// Exits 0 iff all checks pass; prints one PASS/FAIL line per check.

#include "App/relay_identity.h"
#include "ed25519.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

int g_failures = 0;

void check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_failures++;
}

std::string temp_path(const char* suffix) {
#ifdef _WIN32
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    return std::string(dir) + "relay_identity_test_" + suffix;
#else
    return std::string("/tmp/relay_identity_test_") + suffix;
#endif
}

} // namespace

int main() {
    // 1. Deterministic keypair generation from a fixed seed (known-answer
    //    test) — ed25519_create_keypair must be pure/deterministic given the
    //    same 32-byte seed, independent of load_or_create_relay_identity's
    //    file I/O.
    {
        unsigned char seed[32];
        std::memset(seed, 0x42, sizeof(seed));
        unsigned char pub1[32], priv1[64], pub2[32], priv2[64];
        ed25519_create_keypair(pub1, priv1, seed);
        ed25519_create_keypair(pub2, priv2, seed);
        check(std::memcmp(pub1, pub2, 32) == 0 && std::memcmp(priv1, priv2, 64) == 0,
              "deterministic keypair generation from a fixed seed");
    }

    // 2. Sign/verify round-trip.
    {
        const std::string path = temp_path("sign_verify.key");
        std::remove(path.c_str());
        auto identity = ale::load_or_create_relay_identity(path, "TEST1");
        check(identity.has_value(), "load_or_create_relay_identity creates a fresh identity");
        if (identity) {
            const std::string body = R"({"observer":"TEST1","source":"WX1ABC"})";
            const ale::RelaySignature sig = ale::sign_relay_request(*identity, body);
            check(sig.callsign == "TEST1", "signature carries the identity's callsign");
            check(!sig.timestamp.empty(), "signature carries a non-empty timestamp");

            std::string decoded;
            // Minimal inline base64 decode for verification only (mirrors
            // the encode in relay_identity.cpp; kept local to this test to
            // avoid depending on relay_identity.cpp's internal linkage).
            auto b64val = [](char c) -> int {
                if (c >= 'A' && c <= 'Z') return c - 'A';
                if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52;
                if (c == '+') return 62;
                if (c == '/') return 63;
                return -1;
            };
            uint32_t buf = 0; int bits = 0;
            for (char c : sig.signature_b64) {
                if (c == '=') continue;
                const int v = b64val(c);
                if (v < 0) continue;
                buf = (buf << 6) | static_cast<uint32_t>(v);
                bits += 6;
                if (bits >= 8) { bits -= 8; decoded += static_cast<char>((buf >> bits) & 0xFF); }
            }
            check(decoded.size() == 64, "decoded signature is 64 bytes");

            const std::string signed_message = sig.timestamp + "\n" + body;
            const int ok = ed25519_verify(
                reinterpret_cast<const unsigned char*>(decoded.data()),
                reinterpret_cast<const unsigned char*>(signed_message.data()),
                signed_message.size(), identity->public_key);
            check(ok == 1, "sign/verify round-trip succeeds");

            // 3. Tampered-body signature rejection.
            const std::string tampered = signed_message + "x";
            const int bad = ed25519_verify(
                reinterpret_cast<const unsigned char*>(decoded.data()),
                reinterpret_cast<const unsigned char*>(tampered.data()),
                tampered.size(), identity->public_key);
            check(bad == 0, "tampered body fails signature verification");
        }
        std::remove(path.c_str());
    }

    // 4. Key-file round-trip: write then reload yields the same identity.
    {
        const std::string path = temp_path("roundtrip.key");
        std::remove(path.c_str());
        auto first = ale::load_or_create_relay_identity(path, "TEST2");
        check(first.has_value(), "first load_or_create_relay_identity call succeeds");
        auto second = ale::load_or_create_relay_identity(path, "TEST2");
        check(second.has_value(), "second load_or_create_relay_identity call succeeds");
        if (first && second) {
            check(std::memcmp(first->seed, second->seed, 32) == 0,
                  "reloaded identity has the same seed");
            check(std::memcmp(first->public_key, second->public_key, 32) == 0,
                  "reloaded identity has the same public key");
        }
        std::remove(path.c_str());
    }

    // 5. Callsign change triggers regeneration (not a silent identity reuse).
    {
        const std::string path = temp_path("callsign_change.key");
        std::remove(path.c_str());
        auto first = ale::load_or_create_relay_identity(path, "TEST3");
        auto second = ale::load_or_create_relay_identity(path, "TEST4");
        check(first.has_value() && second.has_value(), "both loads succeed across a callsign change");
        if (first && second) {
            check(second->callsign == "TEST4", "reloaded identity carries the new callsign");
            check(std::memcmp(first->seed, second->seed, 32) != 0,
                  "callsign change generates a different seed (not reused)");
        }
        std::remove(path.c_str());
    }

    std::printf("\n%d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
