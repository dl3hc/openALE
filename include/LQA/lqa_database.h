/**
 * @file lqa_database.h
 * @brief Link Quality Analysis Database
 * 
 * MIL-STD-188-141B Appendix A: LQA (Link Quality Analysis) System
 * 
 * Persistent storage of channel quality metrics with per-channel, per-station
 * tracking. Implements time-weighted averaging (recent data weighted higher),
 * configurable history depth, and save/load for persistence across sessions.
 * 
 * Clean-room implementation from MIL-STD-188-141B specification.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace ale {

/**
 * @brief Channel-quality scale bounds (MIL-STD-188-141B A.4.1.5, figure A-4).
 *
 * The internal channel-quality value runs from 0 (worst) to 30 (best) on a
 * SINAD basis; the value 31 is reserved as the "unknown / no measurement"
 * sentinel (REQ-GEN-003 / AC-GEN-001-002, AC-GEN-005-3). A measured quality
 * therefore must never exceed LQA_QUALITY_MAX — only the explicit
 * LQA_QUALITY_UNKNOWN sentinel may take the value 31.
 *
 * (Note: the on-air CMD-LQA SINAD code uses 0 = ≤0 dB (worst) … 30 = 30 dB (best),
 *  i.e. higher code = better SINAD.  The CMD BER code is the opposite: 0 = no errors
 *  (best) … 30 = max errors (worst), i.e. lower code = better BER.  The Figure A-27
 *  channel-selection aggregate sums both directions and picks the lowest total, which
 *  is equivalent to our internal higher-is-better ranking — the two are just negations
 *  of each other.  This constant governs the internal [0, 30] representation.)
 */
static constexpr float LQA_QUALITY_MAX     = 30.0f;  ///< best measured quality
static constexpr float LQA_QUALITY_MIN     = 0.0f;   ///< worst measured quality
static constexpr float LQA_QUALITY_UNKNOWN = 31.0f;  ///< "no measurement" sentinel

/**
 * @brief Single LQA entry for a specific channel/station combination
 *
 * Tracks all quality metrics for one remote station on one frequency.
 */
struct LQAEntry {
    uint32_t frequency_hz;           ///< Channel frequency in Hz
    std::string remote_station;      ///< Remote station address (or "" for sounding)
    float snr_db;                    ///< Signal-to-Noise Ratio (dB)
    float ber;                       ///< Averaged non-unanimous 2/3-vote count (0.0–48.0, A.5.4.1.1)
    float sinad_db;                  ///< FROM-direction SINAD in dB [0,30].  The lqa_metrics
                                     ///< path stores the rounded CMD code (sinad_to_lqa_code);
                                     ///< the sounding path stores the raw Goertzel dB value.
                                     ///< Both are numerically identical on the [0,30] range.
    int fec_errors;                  ///< Total Golay FEC errors corrected
    int total_words;                 ///< Total ALE words received
    float multipath_score;           ///< Multipath severity (0.0=none, 1.0=severe)
    float noise_floor_dbm;           ///< Noise floor measurement (dBm)
    uint32_t last_sounding_ms;       ///< Timestamp of last sounding (ms since epoch)
    uint32_t last_contact_ms;        ///< Timestamp of last contact (ms since epoch)
    float score;                     ///< Computed composite quality score (0=worst .. 30=best)
    uint32_t sample_count;           ///< Number of samples in this entry

    // ── Bilateral (TO direction) — populated from CMD LQA words sent by remote ──
    // SINAD, BER, and MP are independent measurements; 31/7 are "no-data" sentinels.
    // LQA Score and SINAD are separate concepts: score is a composite; SINAD is
    // a single physical measurement (Signal+Noise+Distortion / Noise+Distortion).
    uint8_t bilateral_sinad;  ///< SINAD code [0-30] as reported by remote; 31 = no data
    uint8_t bilateral_ber;    ///< BER code   [0-30] as reported by remote; 31 = no data
    uint8_t bilateral_mp;     ///< MP code    [0-7]  as reported by remote;  7 = not measured
    bool    bilateral_handshake_tried; ///< true = call attempted, no CMD LQA received ("X" in Figure A-27)
                                       ///<        false = never tried ("-" in Figure A-27)

    // A.5.4.5.1: recently failed handshake → deprioritise in channel ranking.
    // Not persisted (volatile within session; 0 = no known failure).
    uint32_t last_failed_handshake_ms = 0;

    // Sounding conclusion type received from this station.
    // true  = last sounding used TWAS (station not available for active link establishment).
    // false = last sounding used TIS  (station available) — also the default when no sounding heard.
    // Only meaningful when last_sounding_ms > 0.
    bool sounding_twas = false;

    // ── Propagation context at measurement time (v3) ─────────────────────────
    float solar_elevation_deg_at_measurement = 0.0f; ///< –90..+90 deg; 0 = not recorded
    float sfi_at_measurement                 = 0.0f; ///< Solar Flux Index (sfu); 0 = not recorded

    /**
     * @brief Default constructor — bilateral fields initialised to "no data" sentinels.
     */
    LQAEntry()
        : frequency_hz(0), remote_station(""), snr_db(0.0f), ber(0.0f),
          sinad_db(0.0f), fec_errors(0), total_words(0),
          multipath_score(0.0f), noise_floor_dbm(-120.0f),
          last_sounding_ms(0), last_contact_ms(0), score(0.0f),
          sample_count(0),
          bilateral_sinad(31u), bilateral_ber(31u), bilateral_mp(7u),
          bilateral_handshake_tried(false),
          last_failed_handshake_ms(0),
          solar_elevation_deg_at_measurement(0.0f),
          sfi_at_measurement(0.0f) {}

    /**
     * @brief Timestamp of the most recent activity on this channel/station.
     *
     * An entry is touched in one of two ways: by a real contact with a named
     * station (updates last_contact_ms) or by a channel sounding with no
     * specific station (updates last_sounding_ms). "How recently did we last
     * see anything here?" is therefore the later of the two timestamps.
     * Used for recency scoring and for pruning stale entries.
     *
     * @return The later of last_contact_ms and last_sounding_ms (ms since epoch)
     */
    uint32_t last_activity_ms() const {
        return (last_contact_ms > last_sounding_ms) ? last_contact_ms
                                                    : last_sounding_ms;
    }
};

/// Weight of the mandatory BER metric in from_direction_quality(); the remainder
/// (1 - kBerLeadWeight) is the secondary SINAD contribution. BER must dominate so
/// that an error-free decode is never rated "Poor" regardless of the leakage-SINAD
/// proxy, while SINAD still discriminates between two otherwise error-free channels.
static constexpr float kBerLeadWeight = 0.7f;

/**
 * @brief FROM-direction (locally measured) channel quality, [0, 30], higher = better.
 *
 * This is the single combined per-direction "LQA score" of MIL-STD-188-141B
 * figure A-27 (FROM cell), on the operator convention (higher = better; A.5.4.1
 * and A.5.4.5.1 Note 1). It is used only for internal channel ranking and the
 * operator quality rating — NOT for the on-air CMD-LQA SINAD field (A.5.4.2.2),
 * which is encoded separately from the raw sinad_db.
 *
 * Per A.5.4.1.1 the mandatory *primary* channel-quality metric is the 2/3-majority-
 * vote BER count (0–48, lower = better): an error-free reception (all words
 * unanimous) is the standard's strongest positive evidence that a channel is
 * usable. SINAD (A.5.4.1.2) is a *secondary refinement*, never a veto — the
 * per-symbol in-band SINAD proxy can read only ~6–7 dB even on a flawless digital
 * decode, so letting SINAD dominate mis-rates good channels as "Poor". This
 * function therefore leads with BER (kBerLeadWeight) and lets SINAD adjust it.
 *
 * @return BER-led quality in [0, 30], or -1.0f when no FROM measurement exists
 *         (caller should fall back to the bilateral / composite score).
 */
float from_direction_quality(const LQAEntry& entry);

/**
 * @brief TO-direction (peer-reported via CMD LQA) channel quality, [0, 30], higher = better.
 *
 * Mirrors from_direction_quality() but operates on the bilateral CMD LQA codes
 * sent by the remote station (A.5.4.2): bilateral_ber is the 2/3-vote count
 * (0–30, lower = better; 31 = no value) and bilateral_sinad is the SINAD code
 * (0–30 dB, higher = better; 31 = no measurement).
 *
 * Uses the same BER-led weighting as from_direction_quality() so both directions
 * are scored consistently: a remote station that decoded us perfectly (bilateral
 * BER = 0) is not penalised when its SINAD proxy reads only ~6 dB due to the
 * same Goertzel leakage artifact. Mixing a BER-led FROM with a SINAD-dominated
 * TO (e.g. via min(sinad, ber_q)) would unfairly halve the bilateral score on
 * otherwise clean links.
 *
 * @return BER-led TO quality in [0, 30], or -1.0f when neither bilateral_ber
 *         nor bilateral_sinad carries a measurement (caller falls back to composite score).
 */
float to_direction_quality(const LQAEntry& entry);

/**
 * @brief Configuration parameters for LQA scoring algorithm
 * 
 * Weights determine relative importance of different quality factors.
 * All weights should sum to 1.0 for normalized scoring.
 */
struct LQAConfig {
    float snr_weight = 0.5f;         ///< Weight for SNR in composite score
    float success_weight = 0.3f;     ///< Weight for successful reception rate
    float recency_weight = 0.2f;     ///< Weight for recent contact
    uint32_t max_age_ms = 90000000;  ///< Max age before entry expires (25 h — covers full diurnal cycle)
    uint32_t history_depth = 100;    ///< Max entries per channel/station
    float time_decay_factor = 0.9f;  ///< Decay factor for time-weighted averaging
    
    // Thresholds for quality assessment
    float good_snr_db = 20.0f;       ///< SNR threshold for "good" quality
    float poor_snr_db = 6.0f;        ///< SNR threshold for "poor" quality
    float good_ber = 0.001f;         ///< BER threshold for "good" quality
    float poor_ber = 0.1f;           ///< BER threshold for "poor" quality
};

/**
 * @brief Propagation context snapshot used to weight LQA channel scores.
 *
 * Passed to LQAAnalyzer::set_propagation_context() from ALEController each time
 * the operator's position or ionospheric state changes. When position_valid is
 * false, all propagation-based score adjustments are bypassed.
 */
struct PropagationContext {
    bool     position_valid = false;
    double   lat_deg        = 0.0;
    double   lon_deg        = 0.0;
    float    sfi_current    = 0.0f;  ///< Solar Flux Index (sfu); 0 = unknown
    uint32_t now_ms         = 0;
};

/**
 * @brief LQA Database for storing and managing channel quality data
 * 
 * Provides persistent storage of Link Quality Analysis metrics for all
 * channels and stations. Implements time-weighted averaging to give
 * more importance to recent measurements while maintaining history.
 * 
 * Usage:
 * @code
 * LQADatabase db;
 * db.set_config(config);
 * 
 * // Update with new measurement
 * db.update_entry(7073000, "REMOTE", snr, ber, fec_errs, total_words);
 * 
 * // Get current LQA for decision making
 * auto entry = db.get_entry(7073000, "REMOTE");
 * if (entry && entry->score > 20.0f) {
 *     // Good quality, proceed with call
 * }
 * 
 * // Save to disk
 * db.save_to_file("lqa_data.db");
 * @endcode
 */
class LQADatabase {
public:
    /// Total cell capacity (design goal 10 000; min. req. 4 000 per REQ-GEN-017 / AC-GEN-006-002).
    static constexpr size_t kCapacity = 10000;

    /// Minimum data-retention period without external power (REQ-GEN-018 / AC-GEN-006-003): 1 hour.
    static constexpr uint32_t kMinRetentionMs = 3600000u;

    /// A.5.3.1: a sounding transmission repeats its conclusion for ~10-15s.
    /// Two same-channel observations at most this far apart can plausibly be
    /// fragments of ONE physical transmission whose address was learned
    /// incrementally (the TIS/TWAS anchor word decodes before its DATA/REP
    /// extension words). reconcile_sounding_identity() uses this window to
    /// fold/rename such fragments into one canonical entry. Generous margin
    /// (2x the documented max burst) absorbs slow scan-hop return and decode
    /// latency without risking a false merge between two unrelated stations
    /// that happen to share a prefix and are heard minutes apart.
    static constexpr uint32_t kSoundingBurstWindowMs = 30000u;

    /**
     * @brief Construct empty LQA database
     */
    LQADatabase();
    
    /**
     * @brief Destructor
     */
    ~LQADatabase();
    
    /**
     * @brief Set configuration parameters
     * @param config Configuration struct with weights and thresholds
     */
    void set_config(const LQAConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    LQAConfig get_config() const;
    
    /**
     * @brief Update LQA entry with new measurement
     * 
     * If entry exists, performs time-weighted averaging with existing data.
     * If entry doesn't exist, creates new entry.
     * 
     * @param frequency_hz Channel frequency in Hz
     * @param remote_station Remote station address (empty string for sounding)
     * @param snr_db Signal-to-Noise Ratio in dB
     * @param ber Bit Error Rate (0.0 - 1.0)
     * @param fec_errors Number of FEC errors corrected
     * @param total_words Total words received
     * @param timestamp_ms Timestamp of measurement (0 = use current time)
     */
    void update_entry(uint32_t frequency_hz,
                     const std::string& remote_station,
                     float snr_db,
                     float ber,
                     int fec_errors,
                     int total_words,
                     uint32_t timestamp_ms = 0);
    
    /**
     * @brief Update LQA entry with full metrics
     * 
     * Extended version allowing SINAD, multipath, noise floor updates.
     * 
     * @param frequency_hz Channel frequency in Hz
     * @param remote_station Remote station address
     * @param snr_db Signal-to-Noise Ratio in dB
     * @param ber Bit Error Rate (0.0 - 1.0)
     * @param sinad_db SINAD measurement in dB
     * @param multipath_score Multipath severity (0.0 - 1.0)
     * @param noise_floor_dbm Noise floor in dBm
     * @param fec_errors Number of FEC errors corrected
     * @param total_words Total words received
     * @param timestamp_ms Timestamp of measurement (0 = use current time)
     */
    void update_entry_extended(uint32_t frequency_hz,
                              const std::string& remote_station,
                              float snr_db,
                              float ber,
                              float sinad_db,
                              float multipath_score,
                              float noise_floor_dbm,
                              int fec_errors,
                              int total_words,
                              uint32_t timestamp_ms = 0);
    
    /**
     * @brief Store bilateral (TO direction) SINAD, BER, and MP codes for a channel/station.
     *
     * Called when a CMD LQA word is received from @p remote_station reporting the
     * quality of our transmitted signal.  Sets bilateral_handshake_tried = true.
     * Creates a stub entry (FROM fields at defaults) when none exists yet.
     *
     * @param frequency_hz      Channel frequency in Hz
     * @param remote_station    Station that sent the CMD LQA report
     * @param sinad_code        SINAD code [0-30] (0 = ≤0 dB, worst; 30 = 30 dB, best); 31 = no measurement
     * @param ber_code          BER code   [0-30]; 31 = no value
     * @param mp_code           MP code    [0-7]; 7 = not measured
     * @param timestamp_ms      Measurement timestamp (0 = current time)
     */
    void update_bilateral(uint32_t frequency_hz,
                          const std::string& remote_station,
                          uint8_t sinad_code,
                          uint8_t ber_code,
                          uint8_t mp_code,
                          uint32_t timestamp_ms = 0);

    /**
     * @brief Record that a call to @p remote_station on @p frequency_hz was attempted
     *        but no CMD LQA word was received back.
     *
     * This sets bilateral_handshake_tried = true while leaving the bilateral SINAD/BER/MP
     * at their "no data" sentinels, producing the "X" state in Figure A-27.
     * Creates a stub entry when none exists.
     */
    void mark_bilateral_attempted(uint32_t frequency_hz,
                                  const std::string& remote_station);

    /**
     * @brief Record a failed handshake attempt on @p frequency_hz for @p remote_station
     *        (A.5.4.5.1 — recently failed channels must be deprioritised in ranking).
     *
     * Sets last_failed_handshake_ms to @p timestamp_ms (current time when 0).
     * Creates a stub entry when none exists.
     */
    void record_handshake_fail(uint32_t frequency_hz,
                               const std::string& remote_station,
                               uint32_t timestamp_ms = 0);

    /**
     * @brief Record the sounding conclusion type (TIS vs TWAS) heard from
     *        @p remote_station on @p frequency_hz.
     *
     * TIS = the station invites return calls (available for active link
     * establishment); TWAS = announce-only (not available). Stored on the
     * station's LQA entry so the GUI "Heard Stations / LQA" list can flag
     * availability. Creates a stub entry when none exists. Does not alter
     * last_contact_ms (a sounding is not a contact).
     *
     * @param frequency_hz   Channel frequency in Hz
     * @param remote_station Station that sent the sounding
     * @param twas           true = TWAS conclusion (not available); false = TIS (available)
     * @param timestamp_ms   Sounding timestamp (0 = current time)
     */
    void set_sounding_availability(uint32_t frequency_hz,
                                   const std::string& remote_station,
                                   bool twas,
                                   uint32_t timestamp_ms = 0);

    /**
     * @brief Reconcile a sounding-conclusion station identity that may be a
     *        fragment of, or a fuller resolution of, a station already known
     *        on this channel (A.5.3.1: the address is learned incrementally —
     *        the TIS/TWAS anchor word decodes before its DATA/REP extension
     *        words — so any single reception can be a still-growing prefix,
     *        and a dropped extension word can make a later reception a
     *        truncated tail of an address already resolved moments ago).
     *
     * Call this BEFORE writing @p station to the database (update_entry_extended,
     * set_sounding_availability, ...) and use the returned string as the key
     * for all of those calls instead of @p station directly. This is the
     * single reconciliation point that guarantees the database never grows
     * two separate rows for one physical station just because its address
     * happened to be split across receptions — independent of whatever
     * upstream timing produced the split.
     *
     * Scoped to same-channel entries active within kSoundingBurstWindowMs so
     * two distinct stations that happen to share a prefix, heard minutes
     * apart, are never conflated.
     *
     * @param frequency_hz Channel frequency in Hz
     * @param station      Station identity just resolved from a sounding
     * @param timestamp_ms Measurement timestamp (0 = current time)
     * @return @p station unchanged if no related entry exists; the existing
     *         fuller identity if @p station is a fragment of it (write your
     *         measurement under that identity instead); or @p station itself
     *         after an existing shorter fragment entry has been renamed
     *         forward to it in place (its accumulated history is preserved).
     */
    std::string reconcile_sounding_identity(uint32_t frequency_hz,
                                            const std::string& station,
                                            uint32_t timestamp_ms = 0);

    /**
     * @brief Get LQA entry for specific channel/station
     *
     * @param frequency_hz Channel frequency in Hz
     * @param remote_station Remote station address (empty for sounding)
     * @return Pointer to entry if found, nullptr otherwise
     */
    std::shared_ptr<LQAEntry> get_entry(uint32_t frequency_hz,
                                        const std::string& remote_station) const;
    
    /**
     * @brief Get all LQA entries for a specific channel
     * 
     * @param frequency_hz Channel frequency in Hz
     * @return Vector of all entries for this channel
     */
    std::vector<LQAEntry> get_entries_for_channel(uint32_t frequency_hz) const;
    
    /**
     * @brief Get all LQA entries for a specific station (across all channels).
     *
     * @param remote_station  Remote station address
     * @param max_age_hours   Exclude entries whose last_activity_ms is older than
     *                        this many hours (0 = no age filter, return all).
     * @return Vector of entries sorted by frequency_hz ascending.
     */
    std::vector<LQAEntry> get_entries_for_station(const std::string& remote_station,
                                                   float max_age_hours = 0.0f) const;

    /**
     * @brief Store remote noise-floor broadcast received via CMD 'n' (AC-CHAN-004-002).
     *
     * Updates the noise_floor_dbm field of the sounding entry (remote_station="")
     * for @p frequency_hz.  Creates a stub entry if none exists.
     * The max_db / mean_db values are in the 0–126 dBm scale used by CMD NOISE;
     * 127 = no report (silently ignored).
     *
     * @param frequency_hz   Channel frequency in Hz
     * @param max_db         Max noise floor (7-bit, 127 = no report)
     * @param mean_db        Mean noise floor (7-bit, 127 = no report)
     * @param timestamp_ms   Measurement timestamp (0 = current time)
     */
    void update_noise_floor(uint32_t frequency_hz,
                            uint8_t  max_db,
                            uint8_t  mean_db,
                            uint32_t timestamp_ms = 0);
    
    /**
     * @brief Record the propagation context (solar elevation, SFI) at the time
     *        a sounding or contact was measured on this channel/station.
     *
     * Sets solar_elevation_deg_at_measurement and sfi_at_measurement on the
     * matching entry. Creates a stub entry if none exists yet.
     * Called from LQAAnalyzer::process_sounding() when position is known.
     *
     * @param frequency_hz  Channel frequency in Hz
     * @param station       Remote station address (or "" for sounding)
     * @param solar_elev    Solar elevation in degrees at measurement time (–90..+90)
     * @param sfi           Solar Flux Index at measurement time (0 = unknown)
     */
    void set_propagation_at_measurement(uint32_t frequency_hz,
                                        const std::string& station,
                                        float solar_elev,
                                        float sfi);

    /**
     * @brief Get all LQA entries in database
     *
     * @return Vector of all entries
     */
    std::vector<LQAEntry> get_all_entries() const;

    /**
     * @brief Get current timestamp in milliseconds (same clock used internally
     * for update_entry()'s default timestamp_ms=0 and LQAEntry's last_*_ms
     * fields) — use this to compute entry age consistently.
     * @return Milliseconds since epoch
     */
    uint32_t get_current_time_ms() const;

    /**
     * @brief Remove stale entries older than max_age_ms
     * 
     * @return Number of entries removed
     */
    int prune_stale_entries();
    
    /**
     * @brief Clear all entries from database
     */
    void clear();
    
    /**
     * @brief Save database to file
     * 
     * Saves in binary format for efficient storage and fast loading.
     * 
     * @param filepath Path to save file
     * @return true if successful, false on error
     */
    bool save_to_file(const std::string& filepath) const;
    
    /**
     * @brief Load database from file
     * 
     * Replaces current database contents with loaded data.
     * 
     * @param filepath Path to load file
     * @return true if successful, false on error
     */
    bool load_from_file(const std::string& filepath);
    
    /**
     * @brief Export database to CSV for analysis
     * 
     * @param filepath Path to CSV file
     * @return true if successful, false on error
     */
    bool export_to_csv(const std::string& filepath) const;
    
    /**
     * @brief Get number of entries in database
     * 
     * @return Total entry count
     */
    size_t get_entry_count() const;
    
    /**
     * @brief Compute composite LQA score for an entry
     * 
     * Implements weighted scoring algorithm using SNR, success rate, and recency.
     * Score range: LQA_QUALITY_MIN (0, worst) to LQA_QUALITY_MAX (30, best) per
     * MIL-STD-188-141B A.4.1.5. 31 is reserved as the "unknown" sentinel and is
     * never produced by this function.
     *
     * @param entry Entry to score
     * @return Composite score in [0, 30]
     */
    float compute_score(const LQAEntry& entry) const;

    /**
     * @brief Bilateral (TO-direction) quality score from a peer's CMD LQA report.
     *
     * Unlike compute_score (which uses the FROM-direction snr_db/ber measured
     * locally), this derives a 0-30 quality from the bilateral_* fields a remote
     * station reported about our signal. Per MIL-STD-188-141B A.5.4.1/A.5.4.2:
     *   - SINAD code is dB directly, higher = better (31 = no measurement);
     *   - BER  code is the 2/3-vote count, lower = better (31 = no value).
     * So SINAD contributes positively and BER negatively (no SINAD inversion).
     * Returns 0.0 when neither bilateral field carries a measurement.
     *
     * @param entry Entry with bilateral_* fields populated by update_bilateral()
     * @return Bilateral quality in [0, 30]
     */
    float bilateral_quality_score(const LQAEntry& entry) const;

private:
    /**
     * @brief Internal key for database map
     */
    struct EntryKey {
        uint32_t frequency_hz;
        std::string remote_station;
        
        bool operator<(const EntryKey& other) const {
            if (frequency_hz != other.frequency_hz)
                return frequency_hz < other.frequency_hz;
            return remote_station < other.remote_station;
        }
    };
    
    /**
     * @brief Perform time-weighted averaging between old and new values
     *
     * @param old_value Previous measurement
     * @param new_value New measurement
     * @param old_samples Number of samples in old value
     * @return Weighted average
     */
    float time_weighted_average(float old_value, float new_value,
                               uint32_t old_samples) const;

    /// Evicts the entry with the oldest last_activity_ms() when entries_ is full.
    void evict_oldest_if_full();

    LQAConfig config_;                           ///< Configuration parameters
    std::map<EntryKey, LQAEntry> entries_;      ///< Database of LQA entries
};

} // namespace ale
