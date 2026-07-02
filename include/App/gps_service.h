#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ale {

/**
 * @brief Background GPS fix service: gpsd (TCP/JSON) and/or NMEA serial.
 *
 * Both sources run in independent daemon threads. The first valid fix from
 * either source is immediately forwarded via the FixCallback. Only one source
 * is typically active at a time, controlled by Config::gpsd_enabled and
 * Config::nmea_enabled.
 *
 * Thread safety: start()/stop() must be called from the same (main) thread.
 * has_fix()/lat()/lon() may be called from any thread. The FixCallback fires
 * from a worker thread — callers must not call ALEController from inside it;
 * instead write to a mutex-protected struct and poll from the main loop.
 */
class GpsService {
public:
    struct Config {
        bool        gpsd_enabled = false;
        std::string gpsd_host    = "127.0.0.1";
        uint16_t    gpsd_port    = 2947;
        bool        nmea_enabled = false;
        std::string nmea_port;
        uint32_t    nmea_baud    = 4800;
    };

    /// Callback fired when fix is acquired (valid=true, lat/lon set) or lost (valid=false).
    using FixCallback = std::function<void(bool valid, double lat_deg, double lon_deg)>;

    GpsService() = default;
    ~GpsService() { stop(); }

    GpsService(const GpsService&) = delete;
    GpsService& operator=(const GpsService&) = delete;

    /** Start the GPS background threads. No-op if already running. */
    void start(const Config& cfg, FixCallback on_fix_change = nullptr);

    /** Stop both threads and wait for them to join. Idempotent. */
    void stop();

    /** Thread-safe: true if the last reported fix is valid. */
    bool   has_fix() const;
    double lat()     const;
    double lon()     const;

    // ── Static parsers (public for unit testing) ─────────────────────────────
    static bool   parse_tpv_json(const std::string& json,
                                 double& lat, double& lon, bool& fix_ok);
    static bool   parse_gpgga(const std::string& s, double& lat, double& lon);
    static bool   parse_gprmc(const std::string& s, double& lat, double& lon, bool& active);
    static double nmea_coord(const std::string& coord, const std::string& hemi);

private:
    // Shared fix state (protected by fix_mtx_)
    mutable std::mutex fix_mtx_;
    double             fix_lat_   = 0.0;
    double             fix_lon_   = 0.0;
    bool               fix_valid_ = false;

    std::atomic<bool> running_{false};
    std::thread       gpsd_thread_;
    std::thread       nmea_thread_;

    FixCallback  on_fix_;
    std::mutex   cb_mtx_;

    void update_fix(bool valid, double lat, double lon);
    void gpsd_loop(Config cfg);
    void nmea_loop(Config cfg);
};

} // namespace ale
