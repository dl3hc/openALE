/**
 * @file lqa_analyzer.h
 * @brief LQA Analyzer for Sounding and Channel Selection
 * 
 * Analyzes sounding results, ranks channels by quality, and selects
 * best channels for outbound calls. Integrates with Phase 3 state machine.
 * 
 * Clean-room implementation from MIL-STD-188-141B specification.
 */

#pragma once

#include "LQA/lqa_database.h"
#include "LQA/lqa_metrics.h"
#include "LQA/solar_position.h"
#include "Protocol/Control/ale_channel_types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace ale {
    
enum class SelectionMode {
    LINK_ESTABLISHMENT,  // A.5.4.5.1: bilateral (FROM+TO)/2, tiebreak by balance
    BROADCAST,           // A.5.4.5.2: TO-direction only (peer→us is irrelevant)
    LISTENING,           // A.5.4.5.3: FROM-direction only (us←peer)
};

/**
 * @brief Channel ranking entry
 * 
 * Associates a channel with its quality score for selection.
 */
struct ChannelRank {
    uint32_t frequency_hz;    ///< Channel frequency
    float score;              ///< Composite LQA score (0=worst .. 30=best)
    std::string best_station; ///< Station with best LQA on this channel
    uint32_t last_update_ms;  ///< Last update timestamp
    float from_quality = -1.0f;  ///< local FROM score used for bilateral; -1 = unknown
    float to_quality   = -1.0f;  ///< peer TO score used for bilateral; -1 = unknown
    
    ChannelRank()
        : frequency_hz(0), score(0.0f), best_station(""), last_update_ms(0), from_quality(-1.0f), to_quality(-1.0f) {}
    
    ChannelRank(uint32_t freq, float sc, const std::string& st, uint32_t ts)
        : frequency_hz(freq), score(sc), best_station(st), last_update_ms(ts), from_quality(-1.0f), to_quality(-1.0f) {}
};

/**
 * @brief Configuration for LQA analyzer
 */
struct AnalyzerConfig {
    float min_acceptable_score = 10.0f;   ///< Minimum score for usable channel
    uint32_t sounding_interval_ms = 300000; ///< Sounding interval (5 minutes)
    bool prefer_recent_contacts = true;   ///< Weight recent contacts higher
    bool enable_automatic_sounding = false; ///< Auto-sound periodically

    // A.5.4.5.1: recently-failed-handshake penalty
    uint32_t handshake_fail_penalty_window_ms = 300000; ///< Penalty window (5 min)
    float    handshake_fail_score_factor      = 0.5f;   ///< Score multiplier while penalised

    // Propagation-aware scoring weights (0 = disabled)
    float tod_weight = 0.15f;  ///< Solar-elevation similarity bonus weight
    float sfi_weight = 0.10f;  ///< SFI similarity bonus weight
};

/**
 * @brief LQA Analyzer
 * 
 * High-level LQA analysis for channel selection and quality management.
 * Processes sounding results, ranks channels, and provides best channel
 * selection for calls.
 * 
 * Usage:
 * @code
 * LQAAnalyzer analyzer(&lqa_database);
 * 
 * // Process received sounding (TIS word)
 * analyzer.process_sounding("REMOTE", frequency, snr, ber);
 * 
 * // Get best channel for call
 * auto best = analyzer.get_best_channel_for_station("REMOTE");
 * if (best) {
 *     radio.set_frequency(best->frequency_hz);
 *     state_machine.make_call("REMOTE");
 * }
 * 
 * // Rank all channels
 * auto ranked = analyzer.rank_all_channels();
 * for (const auto& rank : ranked) {
 *     std::cout << rank.frequency_hz << ": " << rank.score << std::endl;
 * }
 * @endcode
 */
class LQAAnalyzer {
public:
    /**
     * @brief Construct analyzer
     * @param database LQA database to analyze
     */
    explicit LQAAnalyzer(LQADatabase* database);
    
    /**
     * @brief Set configuration
     * @param config Configuration parameters
     */
    void set_config(const AnalyzerConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Configuration
     */
    AnalyzerConfig get_config() const;
    
    /**
     * @brief Set LQA database
     * @param database Database pointer
     */
    void set_database(LQADatabase* database);
    
    /**
     * @brief Process received sounding (TIS/TWAS word)
     *
     * Updates LQA database with sounding result and records the station's
     * availability for active link establishment: a TIS conclusion marks the
     * station available, a TWAS conclusion marks it not available.
     *
     * @param station Station that sent sounding
     * @param frequency_hz Channel frequency
     * @param snr_db Measured SNR
     * @param ber Estimated BER (A.5.4.1.1 non-unanimous vote count, 0–48)
     * @param sinad_db Measured SINAD (dB, A.5.4.1.2); 0.0 = not measured
     * @param twas_conclusion true = sounding ended with TWAS (not available);
     *                        false = TIS (available, default)
     * @param timestamp_ms Sounding timestamp (0 = current time)
     */
    void process_sounding(const std::string& station,
                         uint32_t frequency_hz,
                         float snr_db,
                         float ber,
                         float sinad_db = 0.0f,
                         bool twas_conclusion = false,
                         uint32_t timestamp_ms = 0);
    
    /**
     * @brief Process received sounding with full metrics
     * 
     * @param station Station that sent sounding
     * @param frequency_hz Channel frequency
     * @param sample Full metrics sample
     */
    void process_sounding_extended(const std::string& station,
                                   uint32_t frequency_hz,
                                   const MetricsSample& sample);
    
    /**
     * @brief Get best channel for calling specific station
     * 
     * Selects channel with highest LQA score for given station.
     * 
     * @param station Target station address
     * @return Best channel rank, or nullptr if no suitable channel
     */
    std::shared_ptr<ChannelRank> get_best_channel_for_station(
        const std::string& station) const;
    
    /**
     * @brief Get best overall channel (regardless of station)
     * 
     * Selects channel with highest aggregate LQA score.
     * 
     * @return Best channel rank, or nullptr if no channels
     */
    std::shared_ptr<ChannelRank> get_best_channel() const;
    
    /**
     * @brief Rank all channels by quality (A.5.4.6 multi-station selection)
     *
     * Aggregates LQA scores from all stations (or a filtered subset) per channel,
     * then sorts highest first.  The @p mode controls which direction is weighted:
     *   - LINK_ESTABLISHMENT  bilateral (FROM+TO)/2
     *   - BROADCAST           TO-direction (peer→us, A.5.4.5.2)
     *   - LISTENING           FROM-direction (us←peer, A.5.4.5.3)
     *
     * @param mode            Direction priority (default: LINK_ESTABLISHMENT)
     * @param target_stations Only include entries for these stations; empty = all
     * @param min_path_score  Per-path minimum score; channel aggregated only over
     *                        entries that meet this threshold (A.5.4.6 filter)
     * @return Vector of ranked channels
     */
    std::vector<ChannelRank> rank_all_channels(
        SelectionMode mode = SelectionMode::LINK_ESTABLISHMENT,
        const std::vector<std::string>& target_stations = {},
        float min_path_score = 0.0f) const;
    
/**
     * @brief Rank channels for specific station
     *
     * @param station Target station address
     * @param mode Selection mode (default: LINK_ESTABLISHMENT)
     * @return Vector of ranked channels for this station
     */
    std::vector<ChannelRank> rank_channels_for_station(
        const std::string& station,
        SelectionMode mode = SelectionMode::LINK_ESTABLISHMENT) const;

    /**
     * @brief Sort a channel list for a call to @p station (A.5.4.5).
     *
     * Three-tier ordering:
     *   1. Channels with station-specific bilateral data — sorted by score desc.
     *   2. Channels with only aggregate sounding data — sorted by aggregate score.
     *   3. Channels with no LQA at all — original order preserved.
     *
     * @param station  Target station address
     * @param channels Candidate channels in user-configured order
     * @param mode     Selection mode (pass BROADCAST for one-way per A.5.4.5.2)
     * @return Channels reordered by descending quality
     */
    std::vector<Channel> rank_channels_for_call(
        const std::string& station,
        const std::vector<Channel>& channels,
        SelectionMode mode = SelectionMode::LINK_ESTABLISHMENT) const;

    /**
     * @brief Check if sounding is due for a channel
     * 
     * Determines if sufficient time has passed since last sounding.
     * 
     * @param frequency_hz Channel frequency
     * @return true if sounding should be sent
     */
    bool is_sounding_due(uint32_t frequency_hz) const;
    
    /**
     * @brief Get channels that need sounding
     * 
     * @return Vector of frequencies needing sounding
     */
    std::vector<uint32_t> get_channels_needing_sounding() const;
    
    /**
     * @brief Update the propagation context used by rank_channels_for_station().
     *
     * Called by ALEController::update_propagation_context() whenever the operator's
     * position, GPS fix, or SFI changes. When ctx.position_valid is false, all
     * propagation-based score adjustments are bypassed (graceful degradation).
     *
     * @param ctx  Current observer position + ionospheric state snapshot
     */
    void set_propagation_context(const PropagationContext& ctx);

    /**
     * @brief Register callback for sounding requests
     *
     * Called when automatic sounding is enabled and sounding is due.
     *
     * @param callback Function to call with frequency when sounding needed
     */
    void set_sounding_callback(std::function<void(uint32_t)> callback);
    
    /**
     * @brief Update analyzer (call periodically in main loop)
     * 
     * Checks for automatic sounding triggers, prunes stale data.
     */
    void update();
    
    /**
     * @brief Get quality summary for a channel
     * 
     * @param frequency_hz Channel frequency
     * @return Summary string (e.g., "Good (SNR: 22dB, Score: 28)")
     */
    std::string get_channel_quality_summary(uint32_t frequency_hz) const;
    
    /**
     * @brief Get quality summary for station on channel
     * 
     * @param station Station address
     * @param frequency_hz Channel frequency
     * @return Summary string
     */
    std::string get_station_quality_summary(const std::string& station,
                                            uint32_t frequency_hz) const;

    /**
     * @brief Compute aggregate score for a channel
     *
     * Averages LQA scores from all stations heard on this channel.
     *
     * @param frequency_hz Channel frequency
     * @return Aggregate score
     */
    float compute_channel_aggregate_score(uint32_t frequency_hz) const;

private:
/**
      * @brief Bilateral channel score for a single entry (MIL-STD-188-141B A.5.4.5.1)
      *
      * Both directions use the BER-led blend (kBerLeadWeight × BER_q + (1−k) × SINAD_q)
      * so a perfect decode is not penalised by the ~6 dB Goertzel SINAD floor:
      *   - FROM quality = from_direction_quality(entry)  (BER-led; entry.score fallback)
      *   - TO quality   = to_direction_quality(entry)    (BER-led; bilateral CMD-LQA codes)
      *   - Returns (from_quality + to_quality) / 2  — average of both directions (A.5.4.5.1)
      *
      * Falls back to entry.score when neither bilateral_ber nor bilateral_sinad is present.
      *
      * @param entry  LQA database entry for one (station, channel) pair
      * @return Score in [0, 30] (higher = better quality)
      */
     float bilateral_channel_score(const LQAEntry& entry) const;
     float bilateral_channel_score(const LQAEntry& entry, float& from_q_out, float& to_q_out) const;
    
    /**
     * @brief Get current timestamp in milliseconds
     * @return Milliseconds since epoch
     */
    uint32_t get_current_time_ms() const;
    
    /**
     * @brief Quality level from score
     * @param score LQA score (0=worst .. 30=best)
     * @return Quality level string
     */
    std::string score_to_quality_level(float score) const;
    
    /**
     * @brief Propagation similarity factor for a single LQA entry.
     *
     * Returns a multiplier in [0.5, 1.0] based on how closely the current
     * solar elevation and SFI match those recorded at measurement time.
     * Returns 1.0 (no adjustment) when position is unknown or entry has no
     * propagation data (graceful degradation).
     */
    float compute_propagation_factor(const LQAEntry& entry) const;

    LQADatabase*                  database_;     ///< LQA database
    AnalyzerConfig                config_;       ///< Configuration
    PropagationContext            prop_ctx_;     ///< Current propagation context
    std::function<void(uint32_t)> sounding_cb_; ///< Sounding callback
};

} // namespace ale
