#include "App/location_relay_service.h"
#include "App/http_poster.h"
#include "App/relay_identity.h"
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
    j += "\"frequency_hz\":"; j += std::to_string(r.frequency_hz); j += ',';
    j += "\"comment\":";     json_escape_append(j, r.comment);
    j += '}';
    return j;
}

namespace {

// The ingest URL is configured as .../api/v1/locations; the register
// endpoint lives alongside it at .../api/v1/register. Simple suffix
// replacement rather than full URL parsing — both are always literal path
// segments on the same host by construction (see docs/LOCATION_SHARING_CONCEPT.md §9).
std::string register_url_from_ingest_url(const std::string& ingest_url) {
    static const std::string kSuffix = "/locations";
    if (ingest_url.size() >= kSuffix.size() &&
        ingest_url.compare(ingest_url.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        return ingest_url.substr(0, ingest_url.size() - kSuffix.size()) + "/register";
    }
    // Unexpected URL shape — fall back to appending; register_identity()
    // logs the outcome either way, so a malformed URL just shows up as a
    // failed registration rather than crashing anything.
    return ingest_url + "/../register";
}

} // namespace

// ── Service ─────────────────────────────────────────────────────────────────────

void LocationRelayService::start(const Config& cfg) {
    if (running_.load()) stop();
    cfg_ = cfg;
    {
        std::lock_guard<std::mutex> g(conn_mtx_);
        conn_state_   = CS_UNKNOWN;   // fresh run — initial probe re-establishes it
        last_drained_ = CS_UNKNOWN;
    }
    {
        std::lock_guard<std::mutex> g(reg_mtx_);
        reg_state_ = REG_UNKNOWN;
    }

    identity_ = load_or_create_relay_identity(cfg_.identity_key_path, cfg_.callsign);
    if (!identity_) {
        pal::log_error("LocationRelay",
                        "failed to load/create relay identity at %s — refusing to start "
                        "(never sends unauthenticated requests)",
                        cfg_.identity_key_path.c_str());
        push_status("Location Relay: identity error — see log, service not started");
        return;
    }

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

int LocationRelayService::conn_state() const {
    std::lock_guard<std::mutex> g(conn_mtx_);
    return conn_state_;
}

int LocationRelayService::registration_state() const {
    std::lock_guard<std::mutex> g(reg_mtx_);
    return reg_state_;
}

std::string LocationRelayService::identity_callsign() const {
    return identity_ ? identity_->callsign : std::string();
}

std::string LocationRelayService::identity_public_key_b64() const {
    return identity_ ? relay_identity_public_key_b64(*identity_) : std::string();
}

bool LocationRelayService::pop_conn_state(int& out) {
    std::lock_guard<std::mutex> g(conn_mtx_);
    if (conn_state_ != last_drained_) {
        last_drained_ = conn_state_;
        out = conn_state_;
        return true;
    }
    return false;
}

void LocationRelayService::update_conn_state(bool reached, int http_status) {
    const int new_state = reached
        ? ((http_status >= 200 && http_status < 300) ? CS_CONNECTED : CS_SERVER_ERROR)
        : CS_DISCONNECTED;

    bool changed = false;
    {
        std::lock_guard<std::mutex> g(conn_mtx_);
        if (conn_state_ != new_state) {
            conn_state_ = new_state;
            changed = true;
        }
    }
    if (!changed) return;

    switch (new_state) {
        case CS_CONNECTED:
            pal::log_info("LocationRelay", "connection to %s established (HTTP %d)",
                          cfg_.url.c_str(), http_status);
            push_status("Location API: connected (HTTP " + std::to_string(http_status) + ")");
            break;
        case CS_DISCONNECTED:
            pal::log_warn("LocationRelay",
                          "connection to %s lost — endpoint unreachable (no response)",
                          cfg_.url.c_str());
            push_status("Location API: no connection");
            break;
        case CS_SERVER_ERROR:
            pal::log_warn("LocationRelay",
                          "server at %s replied HTTP %d (reachable, not OK)",
                          cfg_.url.c_str(), http_status);
            push_status("Location API: server error (HTTP " + std::to_string(http_status) + ")");
            break;
        default:
            break;
    }
}

void LocationRelayService::update_reg_state(int http_status, const std::string& response_body) {
    // No JSON parser here — the response bodies are tiny, fixed-shape
    // {"error":"unauthorized","reason":"<reason>"} objects from auth.js, so
    // a substring check on the reason token is sufficient and avoids pulling
    // in a JSON library for one field.
    int new_state = reg_state_;
    if (http_status >= 200 && http_status < 300) {
        new_state = REG_APPROVED;
    } else if (http_status == 409) {
        // Duplicate report (already-seen broadcast) still means this
        // identity is approved and sending successfully.
        new_state = REG_APPROVED;
    } else if (http_status == 403 && response_body.find("revoked") != std::string::npos) {
        new_state = REG_REVOKED;
    } else if (http_status == 401 && response_body.find("pending_approval") != std::string::npos) {
        new_state = REG_PENDING;
    } else if (http_status == 401 || http_status == 403) {
        new_state = REG_REJECTED;
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> g(reg_mtx_);
        if (reg_state_ != new_state) {
            reg_state_ = new_state;
            changed = true;
        }
    }
    if (!changed) return;

    switch (new_state) {
        case REG_APPROVED:
            pal::log_info("LocationRelay", "identity %s approved by relay server",
                           cfg_.callsign.c_str());
            push_status("Location Relay: identity approved");
            break;
        case REG_PENDING:
            pal::log_warn("LocationRelay",
                           "identity %s pending operator approval (admin-cli.js approve %s)",
                           cfg_.callsign.c_str(), cfg_.callsign.c_str());
            push_status("Location Relay: pending admin approval");
            break;
        case REG_REVOKED:
            pal::log_warn("LocationRelay", "identity %s has been revoked by the relay operator",
                           cfg_.callsign.c_str());
            push_status("Location Relay: identity revoked");
            break;
        case REG_REJECTED:
            pal::log_warn("LocationRelay", "identity %s rejected by relay server (status=%d)",
                           cfg_.callsign.c_str(), http_status);
            push_status("Location Relay: signature/identity rejected");
            break;
        default:
            break;
    }
}

void LocationRelayService::register_identity() {
    if (!identity_) return;
    const std::string url = register_url_from_ingest_url(cfg_.url);
    const std::string body = "{\"callsign\":\"" + cfg_.callsign + "\",\"public_key\":\"" +
                              identity_public_key_b64() + "\"}";
    RelayAuth no_auth;  // register is intentionally unauthenticated
    HttpPostResult res;
    const bool sent = http_post_json(url, no_auth, body, cfg_.ca_cert_path, res);
    if (!sent) {
        pal::log_warn("LocationRelay", "registration POST to %s did not complete (no response)",
                       url.c_str());
        return;
    }
    if (res.status == 201) {
        pal::log_info("LocationRelay", "registered new identity %s (pending approval)",
                       cfg_.callsign.c_str());
        update_reg_state(401, "pending_approval");  // reuse the pending classification
    } else if (res.status == 200) {
        update_reg_state(401, "pending_approval");
    } else if (res.status == 409) {
        // Already known to the server — its body tells us which: registering
        // an approved or revoked callsign both 409, distinguished by
        // {"error":"already_approved"|"revoked"}. Classify immediately
        // rather than waiting for an ingest attempt that may never come
        // (e.g. this station hasn't relayed anything yet) — otherwise the
        // GUI is stuck showing "Status: unknown" indefinitely after an
        // operator has already approved the identity.
        if (res.body.find("revoked") != std::string::npos) {
            update_reg_state(403, "revoked");
        } else {
            update_reg_state(200, res.body);  // already_approved (or any other 409 shape)
        }
        pal::log_info("LocationRelay", "registration for %s: %s", cfg_.callsign.c_str(),
                       res.body.c_str());
    } else {
        pal::log_warn("LocationRelay", "registration for %s returned HTTP %d: %s",
                       cfg_.callsign.c_str(), res.status, res.body.c_str());
    }
}

void LocationRelayService::run_health_check() {
    RelayAuth auth;  // unauthenticated — matches the relay server's public GET endpoints
    HttpPostResult res;
    const bool reached = http_probe(cfg_.url, auth, cfg_.ca_cert_path, res);
    update_conn_state(reached, res.status);
}

void LocationRelayService::worker_loop() {
    pal::log_info("LocationRelay", "worker thread started");
    // Idempotent on the server (already-pending/already-approved both return
    // a defined, non-error response) — so it's safe to always attempt this
    // rather than tracking "is this key file brand new" state here.
    register_identity();

    static constexpr uint32_t kBackoffMs[3] = { 5000, 15000, 45000 };
    static constexpr uint32_t kMaxRetries   = 3;
    static constexpr std::time_t kStaleAfterSec = 600;  // 10 min

    const auto idle_poll = std::chrono::milliseconds(500);
    // Fire the initial connection check immediately (epoch < now), then on a
    // fixed cadence while idle. Every real send also refreshes conn_state and
    // pushes this deadline forward, so a busy stream does not re-probe.
    auto next_health_check = std::chrono::steady_clock::time_point{};
    const auto health_interval =
        std::chrono::seconds(cfg_.health_check_interval_sec > 0
                             ? cfg_.health_check_interval_sec : 60);

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
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_health_check) {
                run_health_check();
                next_health_check = now + health_interval;
            }
            std::this_thread::sleep_for(idle_poll);
            continue;
        }

        if (std::time(nullptr) - report.enqueued_at > kStaleAfterSec) {
            pal::log_warn("LocationRelay", "report for %s stale, discarded",
                          report.source.c_str());
            continue;
        }

        // Signed fresh at send time, not at enqueue time — the replay window
        // is only 300s, and reports can sit queued (retry backoff, throttle)
        // for longer than that.
        const std::string body = to_json(report);
        const RelaySignature signature = sign_relay_request(*identity_, body);
        const RelayAuth auth{ signature.callsign, signature.timestamp, signature.signature_b64 };

        HttpPostResult res;
        const bool sent = http_post_json(cfg_.url, auth, body, cfg_.ca_cert_path, res);
        // A send is itself a connectivity sample — reclassify the endpoint and
        // defer the next idle probe so we don't double-probe after traffic.
        update_conn_state(sent, res.status);
        if (sent) update_reg_state(res.status, res.body);
        next_health_check = std::chrono::steady_clock::now() + health_interval;

        if (sent && res.status >= 200 && res.status < 300) {
            pal::log_info("LocationRelay", "report accepted (%d) for %s",
                          res.status, report.source.c_str());
            push_status("Location API: report accepted (" + std::to_string(res.status) + ")");
        } else if (sent && res.status == 409) {
            pal::log_info("LocationRelay", "server duplicate (409) for %s",
                          report.source.c_str());
        } else if (sent && res.status == 403 && res.body.find("observer_mismatch") != std::string::npos) {
            pal::log_error("LocationRelay",
                            "observer_mismatch (403) for %s — station callsign vs signing "
                            "identity mismatch; report dropped, not retried", report.source.c_str());
            push_status("Location API: observer mismatch — check station callsign");
        } else if (sent && (res.status == 401 || res.status == 403)) {
            pal::log_warn("LocationRelay", "auth failed (%d) — dropping report, check identity/approval",
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
