/**
 * @file App/relay_identity.h
 * @brief Per-callsign Ed25519 identity for Location Relay signing.
 *
 * Replaces the shared-bearer-token model: each openALE instance generates
 * its own Ed25519 keypair on first use, persists only the 32-byte seed to
 * `location_relay_identity.key` (next to station.state, not inside it), and
 * signs every ingest request as that callsign. The relay server verifies
 * the signature against a public key it was handed once at registration
 * time (see tools/location-relay-server/auth.js) and an operator approves
 * new registrations out-of-band with admin-cli.js.
 */
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>

namespace ale {

/// ISO8601 UTC, second resolution ("YYYY-MM-DDTHH:MM:SSZ"). Shared by
/// LocationRelayService::to_json() and sign_relay_request() below — both
/// must agree on wire format byte-for-byte, since the server verifies the
/// signature over this exact timestamp string.
std::string iso8601_utc(std::time_t t);

/// One Ed25519 keypair bound to a callsign. public_key/private_key are
/// re-derived from the persisted 32-byte seed on every load — only the seed
/// ever touches disk.
struct RelayIdentity {
    std::string  callsign;
    unsigned char seed[32]       = {0};  ///< the only secret persisted to disk
    unsigned char public_key[32] = {0};
    unsigned char private_key[64] = {0}; ///< expanded form ed25519_sign() needs
};

/// A signed request's auth material — what http_poster's build_relay_headers
/// turns into Authorization/X-Timestamp/X-Signature.
struct RelaySignature {
    std::string callsign;
    std::string timestamp;      ///< ISO8601 UTC, second resolution
    std::string signature_b64;  ///< base64 of the 64-byte Ed25519 signature
};

/// Loads the identity at key_file_path if present and its stored callsign
/// matches `callsign`; otherwise generates a fresh keypair (CSPRNG seed:
/// BCryptGenRandom on Windows, /dev/urandom on POSIX) and persists it,
/// overwriting any stale file for a different callsign (station callsign
/// changed — logged as a transition, not an error).
///
/// Returns std::nullopt only on an unrecoverable I/O or CSPRNG failure — the
/// caller (LocationRelayService::start) must treat that as "refuse to start
/// the worker loop", never fall back to sending unauthenticated requests.
std::optional<RelayIdentity> load_or_create_relay_identity(
    const std::string& key_file_path, const std::string& callsign);

/// Base64 of the raw 32-byte public key — what a client hands the relay
/// server's POST /api/v1/register and what an operator can read aloud to
/// cross-check against admin-cli.js's `list` output.
std::string relay_identity_public_key_b64(const RelayIdentity& identity);

/// Signs `<timestamp>\n<body>` (timestamp = now, ISO8601 UTC via
/// ale::iso8601_utc) with identity's private key.
RelaySignature sign_relay_request(const RelayIdentity& identity, const std::string& body);

} // namespace ale
