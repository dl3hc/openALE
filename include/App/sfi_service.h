#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ale {

/**
 * @brief Background Solar Flux Index (SFI) fetch service.
 *
 * Fetches the NOAA SWPC 10.7 cm solar flux index once at startup and then
 * every hour. On error, retries every 5 minutes. Runs in a dedicated daemon
 * thread; the SfiCallback fires from that thread — callers must not call
 * ALEController directly from it; use the PendingUpdate bridge pattern instead.
 *
 * Only available when sfi_enabled=true in ALEStationConfig.
 */
class SfiService {
public:
    static constexpr float    kUnknown     = 0.0f;
    static constexpr uint32_t kRetryInitMs = 5u * 60u * 1000u;   ///< 5 min on failure
    static constexpr uint32_t kRefreshMs   = 60u * 60u * 1000u;  ///< 1 h after success

    /// Callback fired with the latest SFI value (1–999 sfu) on each successful fetch.
    using SfiCallback = std::function<void(float sfi)>;

    SfiService() = default;
    ~SfiService() { stop(); }

    SfiService(const SfiService&) = delete;
    SfiService& operator=(const SfiService&) = delete;

    /** Start the background fetch thread. No-op if already running. */
    void start(SfiCallback on_update = nullptr);

    /** Stop the worker thread and wait for it to join. Idempotent. */
    void stop();

    /** Thread-safe: last known SFI value, or kUnknown (0) if not yet fetched. */
    float get_sfi()    const { return sfi_.load(); }
    bool  has_sfi()    const { return sfi_.load() > 0.0f; }
    bool  is_running() const { return running_.load(); }

    // ── Static parser (public for unit testing) ───────────────────────────────
    static bool parse_sfi_json(const std::string& body, float& sfi);

private:
    std::atomic<float> sfi_{kUnknown};
    std::atomic<bool>  running_{false};
    std::thread        worker_;

    SfiCallback  on_update_;
    std::mutex   cb_mtx_;

    void worker_loop();
    bool fetch_sfi(float& out);
};

} // namespace ale
