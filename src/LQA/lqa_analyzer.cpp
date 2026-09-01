/** @file lqa_analyzer.cpp — LQA Analyzer implementation */

#include "LQA/lqa_analyzer.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <iomanip>
#include <set>

namespace ale {

// Forward declarations for free helpers (defined below rank_channels_for_station)
float channel_from_score(const LQAEntry& entry);
float channel_to_score(const LQAEntry& entry);

LQAAnalyzer::LQAAnalyzer(LQADatabase* database)
    : database_(database), prop_ctx_(), sounding_cb_(nullptr) {
}

void LQAAnalyzer::set_propagation_context(const PropagationContext& ctx) {
    prop_ctx_ = ctx;
}

float LQAAnalyzer::compute_propagation_factor(const LQAEntry& entry) const {
    if (!prop_ctx_.position_valid) return 1.0f;
    if (entry.sample_count == 0 || entry.solar_elevation_deg_at_measurement == 0.0f) return 1.0f;

    const float elev_now  = compute_solar_elevation(
        prop_ctx_.lat_deg, prop_ctx_.lon_deg,
        ms_to_unix_sec(prop_ctx_.now_ms));
    const float elev_diff = std::abs(elev_now - entry.solar_elevation_deg_at_measurement);
    // 45° diff → zero solar similarity; clamp to [0,1]
    const float solar_sim = std::max(0.0f, 1.0f - elev_diff / 45.0f);

    float sfi_sim = 1.0f;
    if (prop_ctx_.sfi_current > 0.0f && entry.sfi_at_measurement > 0.0f) {
        const float sfi_diff = std::abs(prop_ctx_.sfi_current - entry.sfi_at_measurement);
        sfi_sim = std::max(0.0f, 1.0f - sfi_diff / 100.0f);
    }

    // Floor 0.5: real LQA data never fully zeroed by propagation mismatch
    return 0.5f + 0.5f * (solar_sim * sfi_sim);
}

void LQAAnalyzer::set_config(const AnalyzerConfig& config) {
    config_ = config;
}

AnalyzerConfig LQAAnalyzer::get_config() const {
    return config_;
}

void LQAAnalyzer::set_database(LQADatabase* database) {
    database_ = database;
}

uint32_t LQAAnalyzer::get_current_time_ms() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

void LQAAnalyzer::process_sounding(const std::string& station,
                                  uint32_t frequency_hz,
                                  float snr_db,
                                  float ber,
                                  float sinad_db,
                                  bool twas_conclusion,
                                  uint32_t timestamp_ms) {
    if (!database_) return;

    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;

    // A.5.3.1: the sounding conclusion's address is learned incrementally by
    // the caller (anchor word decodes before its DATA/REP extension words),
    // so `station` here may be a still-growing prefix of an identity already
    // known on this channel, or a truncated fragment of one resolved moments
    // ago. Reconcile to the single canonical identity before writing so a
    // dropped extension word can never leave a second, phantom Heard-
    // Stations/LQA row for the same physical station — this holds regardless
    // of how the accumulator upstream happened to time the split.
    const std::string canonical =
        database_->reconcile_sounding_identity(frequency_hz, station, now);

    // A.5.4.1.2: write measured SINAD via update_entry_extended, populating
    // sinad_db for both the channel and station entries; multipath/noise-floor
    // aren't measured during sounding, so use defaults. Empty-station entry =
    // channel-level aggregate used internally by is_sounding_due(); filtered
    // from get_all_lqa_entries() so GUI never shows a phantom "(sounding)" row.
    database_->update_entry_extended(frequency_hz, "", snr_db, ber, sinad_db,
                                     0.0f, -120.0f, 0, 1, now);
    database_->update_entry_extended(frequency_hz, canonical, snr_db, ber, sinad_db,
                                     0.0f, -120.0f, 0, 1, now);

    // Records station availability for active link establishment from the
    // sounding's conclusion word: TIS=available, TWAS=not available. Only the
    // per-station entry carries the flag ("" channel aggregate has no station
    // to be "available"); it's what the GUI displays.
    database_->set_sounding_availability(frequency_hz, canonical, twas_conclusion, now);

    // Record propagation context at measurement time for future similarity scoring.
    if (prop_ctx_.position_valid) {
        const float elev = compute_solar_elevation(
            prop_ctx_.lat_deg, prop_ctx_.lon_deg,
            ms_to_unix_sec(prop_ctx_.now_ms > 0 ? prop_ctx_.now_ms : now));
        database_->set_propagation_at_measurement(frequency_hz, "",        elev, prop_ctx_.sfi_current);
        database_->set_propagation_at_measurement(frequency_hz, canonical, elev, prop_ctx_.sfi_current);
    }
}

void LQAAnalyzer::process_sounding_extended(const std::string& station,
                                           uint32_t frequency_hz,
                                           const MetricsSample& sample) {
    if (!database_) {
        return;
    }
    
    uint32_t now = (sample.timestamp_ms == 0) ? get_current_time_ms() : sample.timestamp_ms;

    // A.5.4.1.1: BER = non-unanimous vote count (0–48); uncorrectable word→48,
    // correctable→48−unanimous_votes. Falls back to decode_success heuristic when no vote count available.
    float ber;
    if (sample.golay_uncorrectable) {
        ber = 48.0f;
    } else if (sample.non_unanimous_count > 0 || sample.decode_success) {
        ber = static_cast<float>(sample.non_unanimous_count);
    } else {
        ber = 48.0f;  // no info, assume worst case
    }

    database_->update_entry_extended(
        frequency_hz,
        station,
        sample.snr_db,
        ber,
        sample.sinad_db,  // A.5.4.1.2: measured Goertzel SINAD (dB)
        sample.multipath_delay_ms / 10.0f,  // Normalize to 0-1
        sample.noise_power_dbm,
        sample.fec_errors_corrected,
        1,  // One word received
        now
    );
}

std::shared_ptr<ChannelRank> LQAAnalyzer::get_best_channel_for_station(
    const std::string& station) const {
    
    if (!database_) {
        return nullptr;
    }
    
    auto entries = database_->get_entries_for_station(station);
    if (entries.empty()) {
        return nullptr;
    }

    // Highest bilateral channel score (A.5.4.5)
    float best_score = -1.0f;
    const LQAEntry* best_entry = nullptr;
    for (const auto& e : entries) {
        float s = bilateral_channel_score(e);
        if (s > best_score) {
            best_score = s;
            best_entry = &e;
        }
    }

    if (!best_entry || best_score < config_.min_acceptable_score) {
        return nullptr;
    }

    return std::make_shared<ChannelRank>(
        best_entry->frequency_hz,
        best_score,
        station,
        best_entry->last_activity_ms()
    );
}

std::shared_ptr<ChannelRank> LQAAnalyzer::get_best_channel() const {
    if (!database_) {
        return nullptr;
    }
    
    auto all_entries = database_->get_all_entries();
    if (all_entries.empty()) {
        return nullptr;
    }
    
    auto best = std::max_element(all_entries.begin(), all_entries.end(),
        [](const LQAEntry& a, const LQAEntry& b) {
            return a.score < b.score;
        });

    if (best->score < config_.min_acceptable_score) {
        return nullptr;
    }

    uint32_t last_update = best->last_activity_ms();

    return std::make_shared<ChannelRank>(
        best->frequency_hz,
        best->score,
        best->remote_station,
        last_update
    );
}

std::vector<ChannelRank> LQAAnalyzer::rank_all_channels(
        SelectionMode mode,
        const std::vector<std::string>& target_stations,
        float min_path_score) const {
    std::vector<ChannelRank> ranks;

    if (!database_) return ranks;

    // Group all entries by frequency, optionally filtered to target_stations.
    std::map<uint32_t, std::vector<LQAEntry>> by_frequency;
    for (const auto& entry : database_->get_all_entries()) {
        if (entry.remote_station.empty()) continue;  // skip channel-only sounding stubs
        if (!target_stations.empty()) {
            bool found = false;
            for (const auto& s : target_stations)
                if (s == entry.remote_station) { found = true; break; }
            if (!found) continue;
        }
        by_frequency[entry.frequency_hz].push_back(entry);
    }

    for (const auto& pair : by_frequency) {
        uint32_t freq             = pair.first;
        const auto& entries       = pair.second;
        float aggregate_score     = 0.0f;
        int   qualifying          = 0;
        uint32_t latest_update    = 0;
        std::string best_station;
        float best_path           = -1.0f;

        for (const auto& e : entries) {
            float path_score;
            switch (mode) {
                case SelectionMode::BROADCAST:
                    path_score = channel_to_score(e);
                    if (path_score < 0) path_score = e.score;
                    break;
                case SelectionMode::LISTENING:
                    path_score = channel_from_score(e);
                    if (path_score < 0) path_score = e.score;
                    break;
                case SelectionMode::LINK_ESTABLISHMENT:
                default:
                    path_score = bilateral_channel_score(e);
                    break;
            }
            if (path_score < min_path_score) continue;  // A.5.4.6 per-path filter
            aggregate_score += path_score;
            ++qualifying;
            uint32_t act = e.last_activity_ms();
            if (act > latest_update) latest_update = act;
            if (path_score > best_path) { best_path = path_score; best_station = e.remote_station; }
        }

        if (qualifying == 0) continue;  // no path meets min_path_score on this channel
        aggregate_score /= static_cast<float>(qualifying);
        ranks.emplace_back(freq, aggregate_score, best_station, latest_update);
    }

    std::sort(ranks.begin(), ranks.end(),
        [](const ChannelRank& a, const ChannelRank& b) { return a.score > b.score; });

    return ranks;
}


std::vector<ChannelRank> LQAAnalyzer::rank_channels_for_station(
    const std::string& station,
    SelectionMode mode) const {
    
    std::vector<ChannelRank> ranks;
    
    if (!database_) {
        return ranks;
    }
    
    auto entries = database_->get_entries_for_station(station);
    
    for (const auto& entry : entries) {
        float from_q = -1.0f;
        float to_q   = -1.0f;
        float score;
        
        switch (mode) {
            case SelectionMode::BROADCAST:
                // TO-direction only (peer→us)
                to_q = channel_to_score(entry);
                score = (to_q >= 0) ? to_q : entry.score;
                from_q = -1.0f;  // not used in this mode
                break;

            case SelectionMode::LISTENING:
                // FROM-direction only (us←peer)
                from_q = channel_from_score(entry);
                score = (from_q >= 0) ? from_q : entry.score;
                to_q = -1.0f;  // not used in this mode
                break;

            case SelectionMode::LINK_ESTABLISHMENT:
            default:
                // Bilateral (FROM+TO)/2 with balance tiebreaker
                score = bilateral_channel_score(entry, from_q, to_q);
                break;
        }

        // Propagation-aware time-of-day/SFI similarity adjustment; applied
        // before the handshake penalty so the penalty computes off the adjusted score.
        score *= compute_propagation_factor(entry);

        // A.5.4.5.1: recently-failed handshake → deprioritise this channel.
        if (entry.last_failed_handshake_ms > 0) {
            const uint32_t now_ms = get_current_time_ms();
            const uint32_t age   = now_ms - entry.last_failed_handshake_ms;
            if (age < config_.handshake_fail_penalty_window_ms)
                score *= config_.handshake_fail_score_factor;
        }

        ranks.emplace_back(entry.frequency_hz, score, station,
                           entry.last_activity_ms());
        // for sort tiebreaker below
        ranks.back().from_quality = from_q;
        ranks.back().to_quality = to_q;
    }

    // Sort by score (highest first), then by balance tiebreaker
    std::sort(ranks.begin(), ranks.end(),
        [](const ChannelRank& a, const ChannelRank& b) {
            if (std::abs(a.score - b.score) > 0.01f) return a.score > b.score;
            // Tiebreaker: prefer more balanced path (A.5.4.5.1)
            const float imb_a = (a.from_quality >= 0 && a.to_quality >= 0)
                ? std::abs(a.from_quality - a.to_quality) : 30.0f;
            const float imb_b = (b.from_quality >= 0 && b.to_quality >= 0)
                ? std::abs(b.from_quality - b.to_quality) : 30.0f;
            return imb_a < imb_b;  // lower imbalance = more balanced = preferred
        });

    return ranks;
}


std::vector<Channel> LQAAnalyzer::rank_channels_for_call(
    const std::string& station,
    const std::vector<Channel>& channels,
    SelectionMode mode) const {
    if (channels.empty()) return channels;

    auto ranked = rank_channels_for_station(station, mode);

    auto score_for = [&ranked](uint32_t freq) -> float {
        for (const auto& r : ranked)
            if (r.frequency_hz == freq) return r.score;
        return -1.0f;  // unlisted: falls to tier 2 or 3
    };

    std::vector<Channel> ordered = channels;
    std::stable_sort(ordered.begin(), ordered.end(),
        [&](const Channel& a, const Channel& b) {
            const float sa = score_for(a.rx_frequency_hz);
            const float sb = score_for(b.rx_frequency_hz);
            // Tier 1: both have station-specific bilateral data
            if (sa >= 0.0f && sb >= 0.0f) return sa > sb;
            // Tier 2: only one has station-specific data — that one goes first
            if (sa >= 0.0f) return true;
            if (sb >= 0.0f) return false;
            // Tier 3: neither has station data — fall back to aggregate sounding scores
            const float agg_a = compute_channel_aggregate_score(a.rx_frequency_hz);
            const float agg_b = compute_channel_aggregate_score(b.rx_frequency_hz);
            if (agg_a > 0.0f || agg_b > 0.0f) return agg_a > agg_b;
            return false;  // no LQA at all: preserve original order (A.5.4 fallback)
        });
    return ordered;
}


float LQAAnalyzer::bilateral_channel_score(const LQAEntry& entry,
                                            float& from_q_out,
                                            float& to_q_out) const {
    // TO direction: BER-led blend (same weighting as FROM), via to_direction_quality();
    // returns -1 when neither bilateral_ber nor bilateral_sinad has a measurement.
    const float to_quality = to_direction_quality(entry);
    if (to_quality < 0.0f) {
        from_q_out = -1.0f;
        to_q_out = -1.0f;
        return entry.score;  // no bilateral data at all — fall back to composite score
    }

    // FROM direction: BER-led combined quality (A.5.4.1.1 primary + A.5.4.1.2
    // SINAD secondary) via from_direction_quality(); falls back to composite when no local measurement.
    float from_quality = from_direction_quality(entry);
    if (from_quality < 0.0f) from_quality = entry.score;

    from_q_out = from_quality;
    to_q_out = to_quality;

    // Bilateral averaging per A.5.4.5.1: (FROM + TO) / 2
    return (from_quality + to_quality) / 2.0f;
}

// Wrapper for backward compatibility
float LQAAnalyzer::bilateral_channel_score(const LQAEntry& entry) const {
    float f, t;
    return bilateral_channel_score(entry, f, t);
}

// New helper functions for rank_channels_for_station
float channel_from_score(const LQAEntry& entry) {
    // BER-led FROM quality (A.5.4.1.1 primary + A.5.4.1.2 SINAD secondary);
    // fall back to the composite score when no local measurement exists.
    const float q = from_direction_quality(entry);
    return (q >= 0.0f) ? q : entry.score;
}

float channel_to_score(const LQAEntry& entry) {
    return to_direction_quality(entry);  // -1.0f when no bilateral data
}

float LQAAnalyzer::compute_channel_aggregate_score(uint32_t frequency_hz) const {
    if (!database_) {
        return 0.0f;
    }
    
    auto entries = database_->get_entries_for_channel(frequency_hz);
    if (entries.empty()) {
        return 0.0f;
    }
    
    float total = 0.0f;
    int   count = 0;
    for (const auto& entry : entries) {
        if (entry.remote_station.empty()) continue;  // skip channel-only sounding entries
        total += bilateral_channel_score(entry);     // now uses (FROM+TO)/2 (Fix 2)
        ++count;
    }
    return count > 0 ? total / static_cast<float>(count) : 0.0f;
}

bool LQAAnalyzer::is_sounding_due(uint32_t frequency_hz) const {
    if (!database_) {
        return true;  // no data, sounding needed
    }

    auto entries = database_->get_entries_for_channel(frequency_hz);
    if (entries.empty()) {
        return true;  // no data, sounding needed
    }

    // most recent sounding on this channel
    uint32_t latest_sounding = 0;
    for (const auto& entry : entries) {
        latest_sounding = std::max(latest_sounding, entry.last_sounding_ms);
    }
    
    if (latest_sounding == 0) {
        return true;  // Never sounded
    }
    
    uint32_t now = get_current_time_ms();
    uint32_t age = now - latest_sounding;
    
    return age >= config_.sounding_interval_ms;
}

std::vector<uint32_t> LQAAnalyzer::get_channels_needing_sounding() const {
    std::vector<uint32_t> channels;
    
    if (!database_) {
        return channels;
    }
    
    auto all_entries = database_->get_all_entries();

    std::set<uint32_t> frequencies;
    for (const auto& entry : all_entries) {
        frequencies.insert(entry.frequency_hz);
    }

    for (uint32_t freq : frequencies) {
        if (is_sounding_due(freq)) {
            channels.push_back(freq);
        }
    }
    
    return channels;
}

void LQAAnalyzer::set_sounding_callback(std::function<void(uint32_t)> callback) {
    sounding_cb_ = callback;
}

void LQAAnalyzer::update() {
    if (!database_) {
        return;
    }
    
    database_->prune_stale_entries();

    if (config_.enable_automatic_sounding && sounding_cb_) {
        auto channels = get_channels_needing_sounding();
        for (uint32_t freq : channels) {
            sounding_cb_(freq);
        }
    }
}

std::string LQAAnalyzer::score_to_quality_level(float score) const {
    if (score >= 25.0f) return "Excellent";
    if (score >= 20.0f) return "Good";
    if (score >= 15.0f) return "Fair";
    if (score >= 10.0f) return "Poor";
    return "Very Poor";
}

std::string LQAAnalyzer::get_channel_quality_summary(uint32_t frequency_hz) const {
    if (!database_) {
        return "No data";
    }
    
    auto entries = database_->get_entries_for_channel(frequency_hz);
    if (entries.empty()) {
        return "No data";
    }
    
    float avg_snr = 0.0f;
    float avg_score = 0.0f;
    for (const auto& e : entries) {
        avg_snr += e.snr_db;
        avg_score += e.score;
    }
    avg_snr /= entries.size();
    avg_score /= entries.size();
    
    std::ostringstream oss;
    oss << score_to_quality_level(avg_score)
        << " (SNR: " << std::fixed << std::setprecision(1) << avg_snr << "dB"
        << ", Score: " << std::fixed << std::setprecision(0) << avg_score << ")";
    
    return oss.str();
}

std::string LQAAnalyzer::get_station_quality_summary(const std::string& station,
                                                     uint32_t frequency_hz) const {
    if (!database_) {
        return "No data";
    }
    
    auto entry = database_->get_entry(frequency_hz, station);
    if (!entry) {
        return "No data";
    }
    
    std::ostringstream oss;
    oss << score_to_quality_level(entry->score)
        << " (SNR: " << std::fixed << std::setprecision(1) << entry->snr_db << "dB"
        << ", BER: " << std::scientific << std::setprecision(2) << entry->ber
        << ", Score: " << std::fixed << std::setprecision(0) << entry->score << ")";
    
    return oss.str();
}

} // namespace ale
