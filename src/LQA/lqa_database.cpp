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
    // Default configuration already set in LQAConfig struct
}

LQADatabase::~LQADatabase() {
    // Nothing to clean up
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
    // Time-weighted averaging: newer samples get more weight
    // Formula: weighted_avg = (old * decay * old_samples + new) / (old_samples * decay + 1)
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
        // Entry exists - perform time-weighted averaging
        LQAEntry& entry = it->second;
        uint32_t old_samples = entry.sample_count;
        
        // Update metrics with time weighting
        entry.snr_db = time_weighted_average(entry.snr_db, snr_db, old_samples);
        entry.ber = time_weighted_average(entry.ber, ber, old_samples);
        entry.fec_errors += fec_errors;
        entry.total_words += total_words;
        entry.sample_count++;
        
        // An empty station address means this measurement came from a channel
        // sounding (no specific peer); a named station means a real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }
        
        // Recompute score
        entry.score = compute_score(entry);
        
    } else {
        // Create new entry
        LQAEntry entry;
        entry.frequency_hz = frequency_hz;
        entry.remote_station = remote_station;
        entry.snr_db = snr_db;
        entry.ber = ber;
        entry.fec_errors = fec_errors;
        entry.total_words = total_words;
        entry.sample_count = 1;
        
        // An empty station address means this measurement came from a channel
        // sounding (no specific peer); a named station means a real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }
        
        // Compute initial score
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
        // Entry exists - perform time-weighted averaging
        LQAEntry& entry = it->second;
        uint32_t old_samples = entry.sample_count;
        
        // Update all metrics with time weighting
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
        
        // An empty station address means this measurement came from a channel
        // sounding (no specific peer); a named station means a real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }
        
        // Recompute score
        entry.score = compute_score(entry);
        
    } else {
        // Create new entry
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
        
        // An empty station address means this measurement came from a channel
        // sounding (no specific peer); a named station means a real contact.
        const bool is_sounding = remote_station.empty();
        if (is_sounding) {
            entry.last_sounding_ms = now;
        } else {
            entry.last_contact_ms = now;
        }
        
        // Compute initial score
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
    // Recompute so the score reflects the freshly stored bilateral measurement
    // (compute_score falls back to bilateral_quality_score when no FROM-direction
    // snr/word data exists — see compute_score below).
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
        // A sounding is not a contact; only stamp the sounding timestamp so
        // last_activity_ms() / pruning reflect that we heard this station.
        entry.last_sounding_ms = now;
        evict_oldest_if_full();
        entries_[key] = entry;
        it = entries_.find(key);
    }
    it->second.sounding_twas      = twas;
    it->second.last_sounding_ms   = now;
    it->second.score              = compute_score(it->second);
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
    // Store noise floor: use mean_db for the noise_floor_dbm field.
    // The noise_floor_dbm field is in dBm; CMD NOISE uses a 7-bit scale
    // (dB rel. 0.1 µV/3kHz), so we store it as a float directly for now.
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
    // See lqa_database.h for the full rationale (A.5.4.1.1 BER primary, A.5.4.1.2
    // SINAD secondary). Returns [0,30] higher=better, or -1 when no FROM data.
    const bool has_ber   = entry.total_words > 0;
    const bool has_sinad = entry.sinad_db > 0.0f;

    // BER quality: non-unanimous 2/3-vote count (0–48, lower=better) → [0,30].
    // 0 votes (all words unanimous) = 30 (best); ≥48 = 0 (worst).
    const float ber_q = has_ber
        ? (1.0f - std::min(1.0f, entry.ber / 48.0f)) * LQA_QUALITY_MAX
        : -1.0f;
    // SINAD quality: dB directly, clamped to the [0,30] scale (A.5.4.1.2).
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
    // Mirror of from_direction_quality() for the peer-reported (TO) direction.
    // Uses bilateral CMD-LQA codes: bilateral_ber (0–30, lower=better; 31=no value)
    // and bilateral_sinad (0–30 dB, higher=better; 31=no measurement).
    // BER-led weighting matches the FROM direction so mixing FROM+TO averages
    // apples-to-apples and a perfect decode (BER=0) is not penalised by the
    // ~6 dB Goertzel SINAD floor on the remote station.
    const bool has_ber   = (entry.bilateral_ber   <= 30u);
    const bool has_sinad = (entry.bilateral_sinad <= 30u);

    // BER quality: bilateral vote count (0–30, lower=better) → [0,30] higher=better.
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

    // Quality scale is 0 (worst) .. 30 (best); 31 is the reserved "unknown"
    // sentinel and must never be produced here (AC-GEN-001-002, A.4.1.5).

    // FROM-direction (locally measured) quality: BER-led per A.5.4.1.1 (the
    // mandatory primary metric), refined by SINAD (A.5.4.1.2) as a secondary
    // term — see from_direction_quality(). When no local measurement exists
    // (e.g. a bilateral-only stub created by update_bilateral/
    // mark_bilateral_attempted after a call), fall back to the bilateral
    // (TO-direction) quality a peer reported about our signal so the score —
    // and the GUI LQA table — reflects a real measurement instead of 0.
    const float from_q = from_direction_quality(entry);
    if (from_q >= 0.0f) {
        // Combined snr+success weight so the total weights still sum to 1.0.
        score += from_q * (config_.snr_weight + config_.success_weight);
    } else {
        // Bilateral TO-direction quality (SINAD dB higher=better, BER lower=better,
        // per A.5.4.1/A.5.4.2 — no SINAD inversion).
        score += bilateral_quality_score(entry)
                 * (config_.snr_weight + config_.success_weight);
    }

    // Recency component (0-30 scale): recent contact = 30, old contact = 0
    uint32_t now = get_current_time_ms();
    uint32_t last_activity = entry.last_activity_ms();

    if (last_activity > 0) {
        uint32_t age_ms = now - last_activity;
        float age_factor = 1.0f - (static_cast<float>(age_ms) / config_.max_age_ms);
        age_factor = std::max(0.0f, std::min(1.0f, age_factor));
        float recency = age_factor * LQA_QUALITY_MAX;
        score += recency * config_.recency_weight;
    }

    // Clamp to [0, 30] range — never reach the 31 "unknown" sentinel
    return std::min(LQA_QUALITY_MAX, std::max(LQA_QUALITY_MIN, score));
}

float LQADatabase::bilateral_quality_score(const LQAEntry& entry) const {
    // Delegate to the free-function so the formula is identical across all callers.
    const float q = to_direction_quality(entry);
    if (q < 0.0f) return 0.0f;  // no bilateral measurement
    return std::min(LQA_QUALITY_MAX, std::max(LQA_QUALITY_MIN, q));
}

bool LQADatabase::save_to_file(const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    const char magic[] = "PCALE_LQA";
    file.write(magic, sizeof(magic));

    uint32_t version = 3;   // v3 adds solar_elevation_deg_at_measurement + sfi_at_measurement
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write config
    file.write(reinterpret_cast<const char*>(&config_), sizeof(config_));
    
    // Write entry count
    uint32_t count = static_cast<uint32_t>(entries_.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    // Write each entry
    for (const auto& pair : entries_) {
        const LQAEntry& entry = pair.second;
        
        // Write frequency
        file.write(reinterpret_cast<const char*>(&entry.frequency_hz), 
                  sizeof(entry.frequency_hz));
        
        // Write station name (length-prefixed string)
        uint32_t name_len = static_cast<uint32_t>(entry.remote_station.length());
        file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        file.write(entry.remote_station.c_str(), name_len);
        
        // Write metrics
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
    
    // Read and verify header
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

    // Read config (sizeof(LQAConfig) unchanged between v2 and v3)
    file.read(reinterpret_cast<char*>(&config_), sizeof(config_));
    if (!file.good()) return false;

    // v2 files were saved with max_age_ms = 3600000; upgrade to the new 25 h default
    // so old sessions benefit from the extended retention without a manual settings change.
    if (version < 3 && config_.max_age_ms < 90000000u)
        config_.max_age_ms = 90000000u;

    // Read entry count
    uint32_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    // A truncated/corrupt file can hand back a garbage count here — reject
    // anything past the store's own capacity outright rather than trusting it.
    if (!file.good() || count > kCapacity) return false;

    // Clear existing entries
    entries_.clear();
    
    // Read each entry
    for (uint32_t i = 0; i < count; i++) {
        LQAEntry entry;
        
        // Read frequency
        file.read(reinterpret_cast<char*>(&entry.frequency_hz),
                 sizeof(entry.frequency_hz));

        // Read station name (length-prefixed string). A truncated file can
        // hand back an arbitrary 32-bit garbage value here — without a sanity
        // cap, resize() on it throws std::length_error/bad_alloc, uncaught,
        // which crashes the process on the very next startup after any
        // partial/corrupt lqa.bin. Station addresses are always short, so
        // reject anything implausible instead of trusting the file blindly.
        uint32_t name_len;
        file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        if (!file.good() || name_len > 256) return false;
        entry.remote_station.resize(name_len);
        file.read(&entry.remote_station[0], name_len);
        if (!file.good()) return false;

        // Read metrics
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

        // v3: propagation context (v2 files leave fields at constructor defaults: 0.0f)
        if (version >= 3) {
            file.read(reinterpret_cast<char*>(&entry.solar_elevation_deg_at_measurement), 4);
            file.read(reinterpret_cast<char*>(&entry.sfi_at_measurement), 4);
        }

        // A truncated file mid-entry leaves the fixed-size reads above holding
        // whatever the buffer already contained — catch it here rather than
        // silently storing a partially-real entry.
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
    
    // Write CSV header
    file << "Frequency(Hz),Station,SNR(dB),BER,SINAD_Code,FEC_Errors,Total_Words,"
         << "Multipath,Noise_Floor(dBm),Last_Sounding_ms,Last_Contact_ms,Score,Samples,"
         << "Bilateral_SINAD,Bilateral_BER,Bilateral_MP,Bilateral_Tried\n";

    // Write each entry
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
