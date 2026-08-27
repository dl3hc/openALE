/**
 * @file App/location_relay_service.h
 * @brief LocationRelayService — forwards received ALE-GPR positions to a
 *        configured web API. docs/LOCATION_SHARING_CONCEPT.md.
 *
 * Modeled directly on SfiService (App/sfi_service.h): opt-in daemon thread,
 * bounded queue, never touches the ALE tick/audio/radio path. The gate
 * (is_shareable) and dedup-key builder are free functions so they stay
 * individually unit-testable without spinning up the worker thread.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "App/ale_station_config.h"
#include "App/relay_identity.h"
#include "Protocol/Message/ale_gpr.h"
#include <optional>

namespace ale {

/// A shredded, gated report — built by the caller, consumed by the worker.
/// Konzept §8.
struct LocationReport {
    // Identities
    std::string observer;      ///< self address at receive time
    std::string source;        ///< GPR OBJECT field (authoritative)
    std::string relay;         ///< AMD sender, set only if != source
    std::string source_type = "ale_gpr";

    // Original + structured (Konzept §20.5)
    std::string raw_gpr;
    bool   has_position  = false;
    double lat = 0.0, lon = 0.0;             ///< possibly rounded (privacy)
    bool   has_altitude  = false;
    double altitude      = 0.0;
    char   altitude_unit = 0;
    bool   has_timestamp = false;
    std::time_t timestamp_utc = 0;
    std::string comment;                     ///< cleared if include_comment=false

    // Context
    std::string call_context;                ///< ALLCALL|INDIVIDUAL|NET|GROUP|LINKED
    std::time_t received_at = 0;              ///< UTC receive time
    uint32_t    frequency_hz = 0;             ///< RX channel freq the report arrived on (0 = unknown)

    // Dedup anchor (Konzept §11) — computed by the caller via make_dedup_key()
    // before enqueue(); enqueue() only looks it up.
    std::string dedup_key;

    // Worker-owned bookkeeping (irrelevant to callers)
    std::time_t enqueued_at  = 0;   ///< for stale-discard (>10 min)
    uint32_t    retry_count  = 0;
};

/// Privacy/shareability gate (Konzept §5) — pure, individually testable.
/// source_addr is the GPR OBJECT field (never the observer's own address —
/// own transmitted positions are deliberately never relayed).
bool is_shareable(const AleGpr& gpr, const std::string& source_addr,
                   const std::string& call_context, const std::string& self_addr,
                   const ALEStationConfig& cfg);

/// Dedup anchor (Konzept §11): source + "|" + GPR TIME field + "|" + rounded
/// lat/lon. Anchored on the GPR's own TIME field, not receive time, because
/// the same broadcast GPR reaches multiple observers with an identical TIME.
std::string make_dedup_key(const AleGpr& gpr, const std::string& source_addr,
                            uint8_t round_digits);

class LocationRelayService {
public:
    /// Connection state of the configured API endpoint, re-evaluated on every
    /// send and by a periodic idle health check. Surfaced live to the GUI pill
    /// where "Running" used to be the only signal (which only proved the worker
    /// thread existed, not that the endpoint was reachable).
    enum ConnState : int {
        CS_UNKNOWN       = 0,  ///< no check completed yet (initial)
        CS_CONNECTED     = 1,  ///< last response round-tripped with a 2xx
        CS_DISCONNECTED  = 2,  ///< no response at all (DNS / connect / TLS / timeout)
        CS_SERVER_ERROR  = 3,  ///< reachable but replied non-2xx (auth, 5xx, ...)
    };

    /// Registration status of this instance's Ed25519 identity against the
    /// configured relay server, inferred from the last ingest/register
    /// response (no separate server read endpoint needed — see
    /// apps/ale_bridge.cpp's LOCATION_SHARING_GET). Surfaced to the GUI so
    /// "pending approval" is distinguishable from "signature rejected" from
    /// "revoked" instead of one generic auth-failure status.
    enum RegState : int {
        REG_UNKNOWN  = 0,  ///< no registration/ingest attempt has completed yet
        REG_PENDING  = 1,  ///< registered, awaiting operator approval (admin-cli.js)
        REG_APPROVED = 2,  ///< an ingest has been accepted (2xx) or deduped (409)
        REG_REJECTED = 3,  ///< server rejected the signature/identity for a reason other than pending/revoked
        REG_REVOKED  = 4,  ///< operator revoked this callsign (admin-cli.js revoke)
    };

    struct Config {
        std::string url;
        std::string identity_key_path;  ///< location_relay_identity.key path (next to station.state)
        std::string callsign;           ///< station's own callsign — the identity's signing name
        std::string ca_cert_path;  ///< pinned server cert (PEM); empty = system trust store
        uint16_t    queue_size       = 64;
        uint32_t    min_interval_sec = 30;  ///< per-source throttle
        uint32_t    health_check_interval_sec = 60;  ///< idle endpoint probe cadence
    };

    LocationRelayService() = default;
    ~LocationRelayService() { stop(); }
    LocationRelayService(const LocationRelayService&) = delete;
    LocationRelayService& operator=(const LocationRelayService&) = delete;

    /// Start the background worker thread. No-op if already running.
    void start(const Config& cfg);
    /// Stop the worker thread and wait for it to join. Idempotent.
    void stop();
    bool is_running() const { return running_.load(); }

    /// Snapshot of the current endpoint connection state (for LOCATION_SHARING_GET).
    int conn_state() const;

    /// Snapshot of the current registration state (for LOCATION_SHARING_GET).
    int registration_state() const;

    /// This instance's callsign/public-key fingerprint, if an identity has
    /// been loaded/created — empty strings before start() succeeds. The
    /// public key is what an operator reads aloud/pastes to cross-check
    /// against admin-cli.js's `list` output.
    std::string identity_callsign() const;
    std::string identity_public_key_b64() const;

    /// Drains the connection state if it has changed since the last call —
    /// returns true and sets out when a transition occurred. Call from the main
    /// thread (bridge main loop) to emit live WS updates to the GUI pill.
    bool pop_conn_state(int& out);

    /// Gate a pre-built report through per-source dedup (LRU on dedup_key)
    /// and throttle (min_interval_sec), then queue it. Drops the oldest
    /// queued report if the bounded queue is full (best-effort, Konzept §12).
    /// No-op (silently dropped) on a dedup or throttle hit.
    void enqueue(LocationReport report);

    /// Drains one queued status/log line, if any. Call from the main thread
    /// (bridge main loop) to surface ALE-Log / WS status — mirrors
    /// SfiService's PendingUpdate pattern but pull-based, since these are
    /// fire-and-forget log lines rather than state the controller must apply.
    bool pop_status(std::string& out);

    /// Surfaces a configuration problem that prevents start() from ever being
    /// called (e.g. no self address/callsign configured — there's nothing to
    /// sign requests as). The bridge already logs these via pal::log_warn for
    /// the console/file log, but that's invisible to a GUI operator; this
    /// puts the same message on the pop_status() drain so it also lands in
    /// the ALE Log, same as every other Location Relay status line.
    void report_config_error(const std::string& msg) { push_status(msg); }

private:
    std::atomic<bool> running_{false};
    std::thread        worker_;
    Config              cfg_;

    std::mutex                 q_mtx_;
    std::deque<LocationReport> queue_;

    // Per-source dedup LRU (bounded 256 keys) + per-source throttle.
    std::mutex                        dedup_mtx_;
    std::unordered_set<std::string>   dedup_set_;
    std::deque<std::string>           dedup_order_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_sent_;

    std::mutex              status_mtx_;
    std::deque<std::string> status_queue_;
    void push_status(const std::string& s);

    // Endpoint connection state — written by the worker, read by the main
    // thread via conn_state()/pop_conn_state().
    mutable std::mutex conn_mtx_;
    int conn_state_   = CS_UNKNOWN;   ///< current state (guarded by conn_mtx_)
    int last_drained_ = CS_UNKNOWN;   ///< last value handed to pop_conn_state()

    mutable std::mutex reg_mtx_;
    int reg_state_ = REG_UNKNOWN;     ///< guarded by reg_mtx_

    // Identity loaded/created by start(); nullopt only before a successful
    // start() (or if identity load/creation failed, in which case the worker
    // thread never runs at all — see start()).
    std::optional<RelayIdentity> identity_;

    /// Reclassify the connection from a send/probe outcome and, on transition,
    /// log via pal::logger + push a status line. reachable = a response came
    /// back at all (DNS/connect/TLS/timeout failures set it false).
    void update_conn_state(bool reached, int http_status);
    /// Reclassify registration state from an ingest response's status/body
    /// (server includes a "reason" field distinguishing pending/revoked/etc.
    /// — see tools/location-relay-server/auth.js).
    void update_reg_state(int http_status, const std::string& response_body);
    /// Periodic idle probe of cfg_.url (http_probe, no data written).
    void run_health_check();
    /// Best-effort POST to /api/v1/register with this instance's public key.
    /// Safe to call unconditionally at worker startup — the server treats an
    /// already-approved or already-pending callsign idempotently.
    void register_identity();

    void worker_loop();
    static std::string to_json(const LocationReport& r);
};

} // namespace ale
