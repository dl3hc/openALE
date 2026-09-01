/**
 * @file lqa_database.cpp
 * @brief Implementation of LQA Database
 */

#include "LQA/lqa_database.h"
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace ale {

LQADatabase::LQADatabase() {
}

LQADatabase::~LQADatabase() {
}

void LQADatabase::set_config(const LQAConfig& config) {
    config_ = config;
}

LQAConfig LQADatabase::get_config() const {
    return config_;
}

uint32_t LQADatabase::get_current_time_ms() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

float LQADatabase::time_weighted_average(float old_value, float new_value,
                                         uint32_t old_samples) const {
    // weighted_avg = (old * decay * old_samples + new) / (old_samples * decay + 1); newer samples weighted more
    float decay = config_.time_decay_factor;
    float weighted_old = old_value * decay * old_samples;
    float total_weight = old_samples * decay + 1.0f;
    return (weighted_old + new_value) / total_weight;
}

void LQADatabase::evict_oldest_if_full() {
    if (entries_.size() < kCapacity) return;
    auto oldest = entries_.begin();
    for (auto it = std::next(oldest); it != entries_.end(); ++it) {
        if (it->second.last_activity_ms() < oldest->second.last_activity_ms())
            oldest = it;
    }
    entries_.erase(oldest);
}

void LQADatabase::update_entry(uint32_t frequency_hz,
                               const std::string& remote_station,
                               float snr_db,
                               float ber,
                               int fec_errors,
                               int total_words,
                               uint32_t timestamp_ms) {
    EntryKey key{frequency_hz, remote_station};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;
    
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        // Time-weighted averaging over existing entry
        LQAEntry& entry = it->second;
        uint32_t old_samples = entry.sample_count;

        entry.snr_db = time_weighted_average(entry.snr_db, snr_db, old_samples);
        entry.ber = time_weighted_average(entry.ber, ber, old_samples);
        entry.fec_errors += fec_errors;
        entry.total_words += total_words;
        entry.sample_count++;

        // Empty station = channel-sounding measurement (no peer); named station = real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }

        entry.score = compute_score(entry);

    } else {
        LQAEntry entry;
        entry.frequency_hz = frequency_hz;
        entry.remote_station = remote_station;
        entry.snr_db = snr_db;
        entry.ber = ber;
        entry.fec_errors = fec_errors;
        entry.total_words = total_words;
        entry.sample_count = 1;

        // Empty station = channel-sounding measurement (no peer); named station = real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }

        entry.score = compute_score(entry);

        evict_oldest_if_full();
        entries_[key] = entry;
    }
}

void LQADatabase::update_entry_extended(uint32_t frequency_hz,
                                       const std::string& remote_station,
                                       float snr_db,
                                       float ber,
                                       float sinad_db,
                                       float multipath_score,
                                       float noise_floor_dbm,
                                       int fec_errors,
                                       int total_words,
                                       uint32_t timestamp_ms) {
    EntryKey key{frequency_hz, remote_station};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;
    
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        // Time-weighted averaging over existing entry
        LQAEntry& entry = it->second;
        uint32_t old_samples = entry.sample_count;

        entry.snr_db = time_weighted_average(entry.snr_db, snr_db, old_samples);
        entry.ber = time_weighted_average(entry.ber, ber, old_samples);
        entry.sinad_db = time_weighted_average(entry.sinad_db, sinad_db, old_samples);
        entry.multipath_score = time_weighted_average(entry.multipath_score,
                                                      multipath_score, old_samples);
        entry.noise_floor_dbm = time_weighted_average(entry.noise_floor_dbm,
                                                      noise_floor_dbm, old_samples);
        entry.fec_errors += fec_errors;
        entry.total_words += total_words;
        entry.sample_count++;

        // Empty station = channel-sounding measurement (no peer); named station = real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }

        entry.score = compute_score(entry);

    } else {
        LQAEntry entry;
        entry.frequency_hz = frequency_hz;
        entry.remote_station = remote_station;
        entry.snr_db = snr_db;
        entry.ber = ber;
        entry.sinad_db = sinad_db;
        entry.multipath_score = multipath_score;
        entry.noise_floor_dbm = noise_floor_dbm;
        entry.fec_errors = fec_errors;
        entry.total_words = total_words;
        entry.sample_count = 1;

        // Empty station = channel-sounding measurement (no peer); named station = real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }

        entry.score = compute_score(entry);

        evict_oldest_if_full();
        entries_[key] = entry;
    }
}

void LQADatabase::update_bilateral(uint32_t frequency_hz,
                                    const std::string& remote_station,
                                    uint8_t sinad_code,
                                    uint8_t ber_code,
                                    uint8_t mp_code,
                                    uint32_t timestamp_ms) {
    EntryKey key{frequency_hz, remote_station};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;

    auto it = entries_.find(key);
    if (it == entries_.end()) {
        LQAEntry entry;
        entry.frequency_hz    = frequency_hz;
        entry.remote_station  = remote_station;
        entry.last_contact_ms = now;
        evict_oldest_if_full();
        entries_[key] = entry;
        it = entries_.find(key);
    }

    LQAEntry& entry           = it->second;
    entry.bilateral_sinad     = sinad_code;
    entry.bilateral_ber       = ber_code;
    entry.bilateral_mp        = mp_code;
    entry.bilateral_handshake_tried = true;
    entry.last_contact_ms     = now;
    // Recompute so score reflects the new bilateral measurement (compute_score
    // falls back to bilateral_quality_score when no FROM-direction snr/word data exists).
    entry.score = compute_score(entry);
}

void LQADatabase::mark_bilateral_attempted(uint32_t frequency_hz,
                                            const std::string& remote_station) {
    EntryKey key{frequency_hz, remote_station};
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        LQAEntry entry;
        entry.frequency_hz    = frequency_hz;
        entry.remote_station  = remote_station;
        entry.last_contact_ms = get_current_time_ms();
        evict_oldest_if_full();
        entries_[key] = entry;
        it = entries_.find(key);
    }
    it->second.bilateral_handshake_tried = true;
    it->second.score = compute_score(it->second);
}

void LQADatabase::record_handshake_fail(uint32_t frequency_hz,
                                         const std::string& remote_station,
                                         uint32_t timestamp_ms) {
    EntryKey key{frequency_hz, remote_station};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        LQAEntry entry;
        entry.frequency_hz    = frequency_hz;
        entry.remote_station  = remote_station;
        entry.last_contact_ms = now;
        evict_oldest_if_full();
        entries_[key] = entry;
        it = entries_.find(key);
    }
    it->second.last_failed_handshake_ms = now;
}

void LQADatabase::set_sounding_availability(uint32_t frequency_hz,
                                            const std::string& remote_station,
                                            bool twas,
                                            uint32_t timestamp_ms) {
    EntryKey key{frequency_hz, remote_station};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        LQAEntry entry;
        entry.frequency_hz   = frequency_hz;
        entry.remote_station = remote_station;
        // Sounding != contact; only stamp sounding timestamp so
        // last_activity_ms()/pruning reflect that we heard this station.
        entry.last_sounding_ms = now;
        evict_oldest_if_full();
        entries_[key] = entry;
        it = entries_.find(key);
    }
    it->second.sounding_twas      = twas;
    it->second.last_sounding_ms   = now;
    it->second.score              = compute_score(it->second);
}

std::string LQADatabase::reconcile_sounding_identity(uint32_t frequency_hz,
                                                     const std::string& station,
                                                     uint32_t timestamp_ms) {
    if (station.empty()) return station;  // "" is the channel aggregate, never a station identity
    const uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;

    // Find the most-recently-active same-channel entry, within one
    // sounding-burst window, that is a strict prefix of `station` or of
    // which `station` is a strict prefix. Recency breaks ties among
    // multiple candidates (rare in practice, since the window is short).
    enum class Relation { NONE, EXISTING_IS_FRAGMENT, STATION_IS_FRAGMENT };
    Relation relation = Relation::NONE;
    EntryKey best_key{0, ""};
    uint32_t best_activity = 0;

    for (const auto& pair : entries_) {
        if (pair.first.frequency_hz != frequency_hz) continue;
        const std::string& existing = pair.first.remote_station;
        if (existing.empty() || existing == station) continue;
        const uint32_t activity = pair.second.last_activity_ms();
        if ((now - activity) > kSoundingBurstWindowMs) continue;

        Relation r = Relation::NONE;
        if (existing.size() < station.size()
                && station.compare(0, existing.size(), existing) == 0)
            r = Relation::EXISTING_IS_FRAGMENT;
        else if (station.size() < existing.size()
                && existing.compare(0, station.size(), station) == 0)
            r = Relation::STATION_IS_FRAGMENT;
        if (r == Relation::NONE) continue;

        if (relation == Relation::NONE || activity > best_activity) {
            relation      = r;
            best_key      = pair.first;
            best_activity = activity;
        }
    }

    if (relation == Relation::EXISTING_IS_FRAGMENT) {
        // `station` is the fuller identity just resolved — rename the
        // existing fragment entry forward in place so its accumulated
        // history (score, sample_count, sounding type, propagation
        // context, ...) carries over instead of starting a second,
        // disconnected row.
        LQAEntry entry = entries_.at(best_key);
        entry.remote_station = station;
        entries_.erase(best_key);
        entries_[EntryKey{frequency_hz, station}] = entry;
        return station;
    }
    if (relation == Relation::STATION_IS_FRAGMENT) {
        // `station` is a truncated tail of an identity already resolved
        // moments ago — it is that SAME station's dropped extension word,
        // not a new station. Fold this measurement into the existing entry.
        return best_key.remote_station;
    }
    return station;
}

std::shared_ptr<LQAEntry> LQADatabase::get_entry(uint32_t frequency_hz,
                                                 const std::string& remote_station) const {
    EntryKey key{frequency_hz, remote_station};
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return std::make_shared<LQAEntry>(it->second);
    }
    return nullptr;
}

std::vector<LQAEntry> LQADatabase::get_entries_for_channel(uint32_t frequency_hz) const {
    std::vector<LQAEntry> result;
    for (const auto& pair : entries_) {
        if (pair.first.frequency_hz == frequency_hz) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<LQAEntry> LQADatabase::get_entries_for_station(
        const std::string& remote_station, float max_age_hours) const {
    const uint32_t now = get_current_time_ms();
    const uint32_t max_age_ms = (max_age_hours > 0.0f)
        ? static_cast<uint32_t>(max_age_hours * 3600000.0f) : 0u;
    std::vector<LQAEntry> result;
    for (const auto& pair : entries_) {
        if (pair.first.remote_station != remote_station) continue;
        if (max_age_ms > 0u && (now - pair.second.last_activity_ms()) > max_age_ms) continue;
        result.push_back(pair.second);
    }
    std::sort(result.begin(), result.end(),
              [](const LQAEntry& a, const LQAEntry& b){ return a.frequency_hz < b.frequency_hz; });
    return result;
}

void LQADatabase::update_noise_floor(uint32_t frequency_hz,
                                      uint8_t  max_db,
                                      uint8_t  mean_db,
                                      uint32_t timestamp_ms) {
    if (max_db == 127u && mean_db == 127u) return;  // no-report sentinels
    EntryKey key{frequency_hz, ""};
    uint32_t now = (timestamp_ms == 0) ? get_current_time_ms() : timestamp_ms;
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        evict_oldest_if_full();
        LQAEntry entry;
        entry.frequency_hz   = frequency_hz;
        entry.last_sounding_ms = now;
        entries_[key] = entry;
        it = entries_.find(key);
    }
    // noise_floor_dbm is dBm; CMD NOISE uses a 7-bit scale (dB rel. 0.1 uV/3kHz),
    // stored as float directly for now (mean_db used).
    if (mean_db < 127u)
        it->second.noise_floor_dbm = static_cast<float>(mean_db) - 120.0f;
    it->second.last_sounding_ms = now;
}

std::vector<LQAEntry> LQADatabase::get_all_entries() const {
    std::vector<LQAEntry> result;
    result.reserve(entries_.size());
    for (const auto& pair : entries_) {
        result.push_back(pair.second);
    }
    return result;
}

int LQADatabase::prune_stale_entries() {
    uint32_t now = get_current_time_ms();
    int removed = 0;
    
    auto it = entries_.begin();
    while (it != entries_.end()) {
        const LQAEntry& entry = it->second;

        if ((now - entry.last_activity_ms()) > config_.max_age_ms) {
            it = entries_.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    
    return removed;
}

void LQADatabase::clear() {
    entries_.clear();
}

float from_direction_quality(const LQAEntry& entry) {
    // Full rationale in lqa_database.h (A.5.4.1.1 BER primary, A.5.4.1.2 SINAD
    // secondary). Returns [0,30] higher=better, or -1 when no FROM data.
    const bool has_ber   = entry.total_words > 0;
    const bool has_sinad = entry.sinad_db > 0.0f;

    // BER quality: non-unanimous 2/3-vote count (0-48, lower=better) -> [0,30].
    // 0 votes (all unanimous) = 30 (best); >=48 = 0 (worst).
    const float ber_q = has_ber
        ? (1.0f - std::min(1.0f, entry.ber / 48.0f)) * LQA_QUALITY_MAX
        : -1.0f;
    // SINAD quality: dB directly, clamped to [0,30] (A.5.4.1.2).
    const float sinad_q = has_sinad
        ? std::min(LQA_QUALITY_MAX, entry.sinad_db)
        : -1.0f;

    if (has_ber && has_sinad)
        return kBerLeadWeight * ber_q + (1.0f - kBerLeadWeight) * sinad_q;
    if (has_ber)   return ber_q;    // error-free decode scores high even if SINAD unknown
    if (has_sinad) return sinad_q;  // SINAD-only (e.g. sounding without a decoded frame)
    return -1.0f;                   // no FROM measurement at all
}

float to_direction_quality(const LQAEntry& entry) {
    // Mirror of from_direction_quality() for peer-reported (TO) direction. Uses
    // bilateral CMD-LQA codes: bilateral_ber (0-30, lower=better; 31=no value)
    // and bilateral_sinad (0-30 dB, higher=better; 31=no measurement). BER-led
    // weighting matches FROM direction so FROM+TO mixing is apples-to-apples;
    // a perfect decode (BER=0) isn't penalised by the ~6 dB Goertzel SINAD
    // floor on the remote station.
    const bool has_ber   = (entry.bilateral_ber   <= 30u);
    const bool has_sinad = (entry.bilateral_sinad <= 30u);

    // BER quality: bilateral vote count (0-30, lower=better) -> [0,30] higher=better.
    const float ber_q = has_ber
        ? (LQA_QUALITY_MAX - static_cast<float>(entry.bilateral_ber))
        : -1.0f;
    // SINAD quality: bilateral dB code, clamped to [0,30].
    const float sinad_q = has_sinad
        ? std::min(LQA_QUALITY_MAX, static_cast<float>(entry.bilateral_sinad))
        : -1.0f;

    if (has_ber && has_sinad)
        return kBerLeadWeight * ber_q + (1.0f - kBerLeadWeight) * sinad_q;
    if (has_ber)   return ber_q;
    if (has_sinad) return sinad_q;
    return -1.0f;  // no bilateral measurement at all
}

float LQADatabase::compute_score(const LQAEntry& entry) const {
    float score = 0.0f;

    // Quality scale 0 (worst)..30 (best); 31 is reserved "unknown" sentinel,
    // must never be produced here (AC-GEN-001-002, A.4.1.5).

    // FROM-direction (locally measured) quality: BER-led per A.5.4.1.1 (mandatory
    // primary metric), refined by SINAD (A.5.4.1.2) secondary — see
    // from_direction_quality(). If no local measurement exists (e.g. a
    // bilateral-only stub from update_bilateral/mark_bilateral_attempted), fall
    // back to peer-reported bilateral (TO-direction) quality so score/GUI LQA
    // table reflect a real measurement instead of 0.
    const float from_q = from_direction_quality(entry);
    if (from_q >= 0.0f) {
        // Combined snr+success weight so total weights still sum to 1.0.
        score += from_q * (config_.snr_weight + config_.success_weight);
    } else {
        // Bilateral TO-direction quality (SINAD dB higher=better, BER
        // lower=better, per A.5.4.1/A.5.4.2 — no SINAD inversion).
        score += bilateral_quality_score(entry)
                 * (config_.snr_weight + config_.success_weight);
    }

    // Recency (0-30): recent contact = 30, old contact = 0
    uint32_t now = get_current_time_ms();
    uint32_t last_activity = entry.last_activity_ms();

    if (last_activity > 0) {
        uint32_t age_ms = now - last_activity;
        float age_factor = 1.0f - (static_cast<float>(age_ms) / config_.max_age_ms);
        age_factor = std::max(0.0f, std::min(1.0f, age_factor));
        float recency = age_factor * LQA_QUALITY_MAX;
        score += recency * config_.recency_weight;
    }

    // Clamp to [0,30] — never reach the 31 "unknown" sentinel
    return std::min(LQA_QUALITY_MAX, std::max(LQA_QUALITY_MIN, score));
}

float LQADatabase::bilateral_quality_score(const LQAEntry& entry) const {
    // Delegates to free function so formula matches across all callers.
    const float q = to_direction_quality(entry);
    if (q < 0.0f) return 0.0f;  // no bilateral measurement
    return std::min(LQA_QUALITY_MAX, std::max(LQA_QUALITY_MIN, q));
}

bool LQADatabase::save_to_file(const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    const char magic[] = "PCALE_LQA";
    file.write(magic, sizeof(magic));

    uint32_t version = 3;   // v3 adds solar_elevation_deg_at_measurement + sfi_at_measurement
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    file.write(reinterpret_cast<const char*>(&config_), sizeof(config_));

    uint32_t count = static_cast<uint32_t>(entries_.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& pair : entries_) {
        const LQAEntry& entry = pair.second;

        file.write(reinterpret_cast<const char*>(&entry.frequency_hz),
                  sizeof(entry.frequency_hz));

        // length-prefixed station name
        uint32_t name_len = static_cast<uint32_t>(entry.remote_station.length());
        file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        file.write(entry.remote_station.c_str(), name_len);

        file.write(reinterpret_cast<const char*>(&entry.snr_db), sizeof(entry.snr_db));
        file.write(reinterpret_cast<const char*>(&entry.ber), sizeof(entry.ber));
        file.write(reinterpret_cast<const char*>(&entry.sinad_db), sizeof(entry.sinad_db));
        file.write(reinterpret_cast<const char*>(&entry.fec_errors), sizeof(entry.fec_errors));
        file.write(reinterpret_cast<const char*>(&entry.total_words), sizeof(entry.total_words));
        file.write(reinterpret_cast<const char*>(&entry.multipath_score), 
                  sizeof(entry.multipath_score));
        file.write(reinterpret_cast<const char*>(&entry.noise_floor_dbm), 
                  sizeof(entry.noise_floor_dbm));
        file.write(reinterpret_cast<const char*>(&entry.last_sounding_ms), 
                  sizeof(entry.last_sounding_ms));
        file.write(reinterpret_cast<const char*>(&entry.last_contact_ms), 
                  sizeof(entry.last_contact_ms));
        file.write(reinterpret_cast<const char*>(&entry.score), sizeof(entry.score));
        file.write(reinterpret_cast<const char*>(&entry.sample_count),
                  sizeof(entry.sample_count));

        // v2: bilateral (TO direction) fields
        file.write(reinterpret_cast<const char*>(&entry.bilateral_sinad),
                  sizeof(entry.bilateral_sinad));
        file.write(reinterpret_cast<const char*>(&entry.bilateral_ber),
                  sizeof(entry.bilateral_ber));
        file.write(reinterpret_cast<const char*>(&entry.bilateral_mp),
                  sizeof(entry.bilateral_mp));
        uint8_t tried = entry.bilateral_handshake_tried ? 1u : 0u;
        file.write(reinterpret_cast<const char*>(&tried), sizeof(tried));

        // v3: propagation context at measurement time
        file.write(reinterpret_cast<const char*>(&entry.solar_elevation_deg_at_measurement), 4);
        file.write(reinterpret_cast<const char*>(&entry.sfi_at_measurement), 4);
    }

    file.close();
    return true;
}

bool LQADatabase::load_from_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    char magic[10];
    file.read(magic, sizeof(magic));
    if (!file.good() || std::string(magic) != "PCALE_LQA") {
        return false;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good() || version < 2 || version > 3) {
        return false;  // v1 files lack bilateral fields; only v2 and v3 supported
    }

    // sizeof(LQAConfig) unchanged between v2 and v3
    file.read(reinterpret_cast<char*>(&config_), sizeof(config_));
    if (!file.good()) return false;

    // v2 files saved with max_age_ms=3600000; upgrade to 25h default so old
    // sessions get extended retention without a manual settings change.
    if (version < 3 && config_.max_age_ms < 90000000u)
        config_.max_age_ms = 90000000u;

    uint32_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    // Truncated/corrupt file can yield a garbage count — reject anything past
    // the store's own capacity rather than trusting it.
    if (!file.good() || count > kCapacity) return false;

    entries_.clear();

    for (uint32_t i = 0; i < count; i++) {
        LQAEntry entry;

        file.read(reinterpret_cast<char*>(&entry.frequency_hz),
                 sizeof(entry.frequency_hz));

        // length-prefixed station name. Truncated file can yield arbitrary
        // 32-bit garbage here; without a cap, resize() throws
        // length_error/bad_alloc uncaught, crashing the process on next
        // startup after any partial/corrupt lqa.bin. Station addresses are
        // always short, so reject implausible values instead of trusting the file.
        uint32_t name_len;
        file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        if (!file.good() || name_len > 256) return false;
        entry.remote_station.resize(name_len);
        file.read(&entry.remote_station[0], name_len);
        if (!file.good()) return false;

        file.read(reinterpret_cast<char*>(&entry.snr_db), sizeof(entry.snr_db));
        file.read(reinterpret_cast<char*>(&entry.ber), sizeof(entry.ber));
        file.read(reinterpret_cast<char*>(&entry.sinad_db), sizeof(entry.sinad_db));
        file.read(reinterpret_cast<char*>(&entry.fec_errors), sizeof(entry.fec_errors));
        file.read(reinterpret_cast<char*>(&entry.total_words), sizeof(entry.total_words));
        file.read(reinterpret_cast<char*>(&entry.multipath_score), 
                 sizeof(entry.multipath_score));
        file.read(reinterpret_cast<char*>(&entry.noise_floor_dbm), 
                 sizeof(entry.noise_floor_dbm));
        file.read(reinterpret_cast<char*>(&entry.last_sounding_ms), 
                 sizeof(entry.last_sounding_ms));
        file.read(reinterpret_cast<char*>(&entry.last_contact_ms), 
                 sizeof(entry.last_contact_ms));
        file.read(reinterpret_cast<char*>(&entry.score), sizeof(entry.score));
        file.read(reinterpret_cast<char*>(&entry.sample_count), sizeof(entry.sample_count));

        // v2: bilateral (TO direction) fields
        file.read(reinterpret_cast<char*>(&entry.bilateral_sinad),
                 sizeof(entry.bilateral_sinad));
        file.read(reinterpret_cast<char*>(&entry.bilateral_ber),
                 sizeof(entry.bilateral_ber));
        file.read(reinterpret_cast<char*>(&entry.bilateral_mp),
                 sizeof(entry.bilateral_mp));
        uint8_t tried = 0u;
        file.read(reinterpret_cast<char*>(&tried), sizeof(tried));
        entry.bilateral_handshake_tried = (tried != 0u);

        // v3: propagation context (v2 files leave fields at ctor defaults: 0.0f)
        if (version >= 3) {
            file.read(reinterpret_cast<char*>(&entry.solar_elevation_deg_at_measurement), 4);
            file.read(reinterpret_cast<char*>(&entry.sfi_at_measurement), 4);
        }

        // Truncated file mid-entry leaves fixed-size reads above holding
        // whatever the buffer had — catch here, don't silently store a partial entry.
        if (!file.good()) return false;

        EntryKey key{entry.frequency_hz, entry.remote_station};
        entries_[key] = entry;
    }

    file.close();
    return true;
}

void LQADatabase::set_propagation_at_measurement(uint32_t frequency_hz,
                                                   const std::string& station,
                                                   float solar_elev,
                                                   float sfi)
{
    EntryKey key{frequency_hz, station};
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        evict_oldest_if_full();
        LQAEntry stub;
        stub.frequency_hz   = frequency_hz;
        stub.remote_station = station;
        entries_[key] = stub;
        it = entries_.find(key);
    }
    it->second.solar_elevation_deg_at_measurement = solar_elev;
    it->second.sfi_at_measurement                 = sfi;
}

bool LQADatabase::export_to_csv(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << "Frequency(Hz),Station,SNR(dB),BER,SINAD_Code,FEC_Errors,Total_Words,"
         << "Multipath,Noise_Floor(dBm),Last_Sounding_ms,Last_Contact_ms,Score,Samples,"
         << "Bilateral_SINAD,Bilateral_BER,Bilateral_MP,Bilateral_Tried\n";

    for (const auto& pair : entries_) {
        const LQAEntry& entry = pair.second;
        file << entry.frequency_hz << ","
             << entry.remote_station << ","
             << entry.snr_db << ","
             << entry.ber << ","
             << entry.sinad_db << ","
             << entry.fec_errors << ","
             << entry.total_words << ","
             << entry.multipath_score << ","
             << entry.noise_floor_dbm << ","
             << entry.last_sounding_ms << ","
             << entry.last_contact_ms << ","
             << entry.score << ","
             << entry.sample_count << ","
             << static_cast<int>(entry.bilateral_sinad) << ","
             << static_cast<int>(entry.bilateral_ber) << ","
             << static_cast<int>(entry.bilateral_mp) << ","
             << (entry.bilateral_handshake_tried ? "X" : "-") << "\n";
    }
    
    file.close();
    return true;
}

size_t LQADatabase::get_entry_count() const {
    return entries_.size();
}

} // namespace ale
