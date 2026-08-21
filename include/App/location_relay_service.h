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
#include "Protocol/Message/ale_gpr.h"

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
    struct Config {
        std::string url;
        std::string token;
        uint16_t    queue_size       = 64;
        uint32_t    min_interval_sec = 30;  ///< per-source throttle
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

    void worker_loop();
    static std::string to_json(const LocationReport& r);
};

} // namespace ale
