#include "App/location_relay_service.h"
#include "App/http_poster.h"
#include "PAL/logger.h"

#include <cmath>
#include <cstdio>

namespace ale {

// ── Gate + dedup key (free functions, individually testable) ──────────────────

bool is_shareable(const AleGpr& gpr, const std::string& source_addr,
                   const std::string& call_context, const std::string& self_addr,
                   const ALEStationConfig& cfg) {
    if (!cfg.location_sharing_enabled) return false;
    if (source_addr.empty() || source_addr == self_addr) return false;  // never relay our own TX
    if (!gpr.valid_gpr_structure) return false;

    if (call_context == "ALLCALL")         return cfg.location_sharing_allcall;
    if (call_context == "INDIVIDUAL")      return cfg.location_sharing_individual;
    if (call_context == "NET")             return cfg.location_sharing_net;
    if (call_context == "GROUP")           return cfg.location_sharing_group;
    if (call_context == "LINKED")          return cfg.location_sharing_linked;
    return false;  // unknown call_context — fail closed
}

std::string make_dedup_key(const AleGpr& gpr, const std::string& source_addr,
                            uint8_t round_digits) {
    std::string key = source_addr;
    key += '|';
    key += gpr.has_timestamp ? std::to_string(static_cast<long long>(gpr.timestamp_utc))
                              : gpr.time_raw;
    key += '|';
    if (gpr.has_latitude && gpr.has_longitude) {
        const double scale = std::pow(10.0, round_digits);
        const double rlat  = std::round(gpr.latitude_deg * scale) / scale;
        const double rlon  = std::round(gpr.longitude_deg * scale) / scale;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f,%.*f", round_digits, rlat, round_digits, rlon);
        key += buf;
    }
    return key;
}

// ── JSON body (Konzept §9) ─────────────────────────────────────────────────────

namespace {

void json_escape_append(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

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

} // namespace

std::string LocationRelayService::to_json(const LocationReport& r) {
    std::string j = "{";
    j += "\"observer\":";    json_escape_append(j, r.observer);    j += ',';
    j += "\"source\":";      json_escape_append(j, r.source);      j += ',';
    j += "\"relay\":";       json_escape_append(j, r.relay);       j += ',';
    j += "\"source_type\":"; json_escape_append(j, r.source_type); j += ',';
    j += "\"raw_gpr\":";     json_escape_append(j, r.raw_gpr);     j += ',';
    if (r.has_position) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "\"latitude\":%.6f,\"longitude\":%.6f,",
                      r.lat, r.lon);
        j += buf;
    } else {
        j += "\"latitude\":null,\"longitude\":null,";
    }
    if (r.has_altitude) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "\"altitude\":%.1f,", r.altitude);
        j += buf;
        j += "\"altitude_unit\":";
        json_escape_append(j, std::string(1, r.altitude_unit ? r.altitude_unit : 'M'));
        j += ',';
    } else {
        j += "\"altitude\":null,\"altitude_unit\":null,";
    }
    if (r.has_timestamp) {
        j += "\"timestamp\":"; json_escape_append(j, iso8601_utc(r.timestamp_utc)); j += ',';
    } else {
        j += "\"timestamp\":null,";
    }
    j += "\"received_at\":"; json_escape_append(j, iso8601_utc(r.received_at)); j += ',';
    j += "\"call_type\":";   json_escape_append(j, r.call_context); j += ',';
    j += "\"frequency_hz\":"; j += std::to_string(r.frequency_hz);
    j += '}';
    return j;
}

// ── Service ─────────────────────────────────────────────────────────────────────

void LocationRelayService::start(const Config& cfg) {
    if (running_.load()) stop();
    cfg_ = cfg;
    running_ = true;
    worker_ = std::thread(&LocationRelayService::worker_loop, this);
}

void LocationRelayService::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void LocationRelayService::enqueue(LocationReport report) {
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> g(dedup_mtx_);
        if (dedup_set_.count(report.dedup_key)) return;  // duplicate — drop silently

        const auto it = last_sent_.find(report.source);
        if (it != last_sent_.end()) {
            const auto elapsed_s =
                std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
            if (elapsed_s < static_cast<int64_t>(cfg_.min_interval_sec))
                return;  // throttled — drop silently
        }
        last_sent_[report.source] = now;

        dedup_set_.insert(report.dedup_key);
        dedup_order_.push_back(report.dedup_key);
        if (dedup_order_.size() > 256) {
            dedup_set_.erase(dedup_order_.front());
            dedup_order_.pop_front();
        }
    }

    report.enqueued_at = std::time(nullptr);
    {
        std::lock_guard<std::mutex> g(q_mtx_);
        if (queue_.size() >= cfg_.queue_size) {
            queue_.pop_front();  // best-effort: drop oldest (Konzept §12)
            push_status("Location report deferred (queue full, discarded oldest)");
        }
        queue_.push_back(std::move(report));
    }
}

void LocationRelayService::push_status(const std::string& s) {
    std::lock_guard<std::mutex> g(status_mtx_);
    status_queue_.push_back(s);
    if (status_queue_.size() > 32) status_queue_.pop_front();  // bounded, best-effort
}

bool LocationRelayService::pop_status(std::string& out) {
    std::lock_guard<std::mutex> g(status_mtx_);
    if (status_queue_.empty()) return false;
    out = std::move(status_queue_.front());
    status_queue_.pop_front();
    return true;
}

void LocationRelayService::worker_loop() {
    pal::log_info("LocationRelay", "worker thread started");
    static constexpr uint32_t kBackoffMs[3] = { 5000, 15000, 45000 };
    static constexpr uint32_t kMaxRetries   = 3;
    static constexpr std::time_t kStaleAfterSec = 600;  // 10 min

    while (running_.load()) {
        LocationReport report;
        bool have = false;
        {
            std::lock_guard<std::mutex> g(q_mtx_);
            if (!queue_.empty()) {
                report = std::move(queue_.front());
                queue_.pop_front();
                have = true;
            }
        }
        if (!have) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (std::time(nullptr) - report.enqueued_at > kStaleAfterSec) {
            pal::log_warn("LocationRelay", "report for %s stale, discarded",
                          report.source.c_str());
            continue;
        }

        HttpPostResult res;
        const std::string body = to_json(report);
        const bool sent = http_post_json(cfg_.url, cfg_.token, body, res);

        if (sent && res.status >= 200 && res.status < 300) {
            pal::log_info("LocationRelay", "report accepted (%d) for %s",
                          res.status, report.source.c_str());
            push_status("Location API: report accepted (" + std::to_string(res.status) + ")");
        } else if (sent && res.status == 409) {
            pal::log_info("LocationRelay", "server duplicate (409) for %s",
                          report.source.c_str());
        } else if (sent && (res.status == 401 || res.status == 403)) {
            pal::log_warn("LocationRelay", "auth failed (%d) — dropping report, check token",
                          res.status);
            push_status("Location API " + std::to_string(res.status) + " — auth failed");
        } else if (sent && res.status == 422) {
            pal::log_warn("LocationRelay", "malformed report (422) for %s — dropped",
                          report.source.c_str());
        } else {
            // 429, 5xx, or no response at all (network/TLS/timeout) — retry with backoff.
            report.retry_count++;
            if (report.retry_count > kMaxRetries) {
                pal::log_warn("LocationRelay", "giving up on %s after %u retries",
                              report.source.c_str(), kMaxRetries);
                push_status("Location API unavailable: giving up after retries");
            } else {
                const uint32_t backoff_ms = kBackoffMs[report.retry_count - 1];
                pal::log_warn("LocationRelay",
                              "send failed (status=%d), retry %u/%u in %u ms",
                              res.status, report.retry_count, kMaxRetries, backoff_ms);
                if (report.retry_count == 1)
                    push_status("Location API unavailable: connection timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                if (running_.load()) {
                    std::lock_guard<std::mutex> g(q_mtx_);
                    if (queue_.size() < cfg_.queue_size) queue_.push_back(std::move(report));
                }
            }
        }
    }
    pal::log_info("LocationRelay", "worker thread stopped");
}

} // namespace ale
