/**
 * @file lqa_history.h
 * @brief Append-only LQA measurement history (ale_monitor Propagation Analysis)
 *
 * Unlike LQADatabase (lqa_database.h), which blends every new measurement into
 * a single time-weighted-averaged row per (frequency, station) and persists as
 * a full-snapshot binary rewrite, LQAHistoryStore keeps every raw measurement
 * forever (subject to a retention window) as an append-only log. It exists
 * purely to feed trend/history views (Station History, Channel History, Heat
 * Maps, Time-of-Day Analysis) — it does not participate in channel scoring or
 * selection, and LQADatabase is not touched or repurposed by this class.
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ale {

/**
 * @brief One immutable, timestamped LQA measurement.
 */
struct LQAHistorySample {
    uint64_t    ts_ms = 0;         ///< Wall-clock epoch ms (std::chrono::system_clock)
    uint32_t    frequency_hz = 0;  ///< Channel frequency in Hz
    std::string station;           ///< Remote station address (never empty — see record())
    float       sinad_db = 0.0f;   ///< FROM-direction SINAD in dB [0,30]
    float       ber = 0.0f;        ///< Averaged non-unanimous vote count (0.0-48.0, same units as LQAEntry::ber)
    float       score = 0.0f;      ///< Composite LQADatabase score at measurement time [0,30]
};

/**
 * @brief Append-only, retention-pruned store of LQAHistorySample records.
 *
 * Single-threaded use only (matches ale_monitor's single-threaded main loop —
 * no internal locking). Persistence is append-mode: record() writes one line
 * per sample to an already-open file handle, never rewriting prior lines.
 */
class LQAHistoryStore {
public:
    struct Config {
        uint32_t retention_days = 90;  ///< In-memory retention window; 0 = unlimited
        bool     enabled = true;       ///< false = record() is a no-op
    };

    LQAHistoryStore();
    ~LQAHistoryStore();

    /// Set retention/enabled configuration. May be called before or after load_from_file().
    void set_config(const Config& cfg);

    /// Current configuration.
    Config get_config() const;

    /**
     * @brief Append a new measurement.
     *
     * No-op if !enabled or sample.station is empty (sounding-aggregate stub
     * entries are not meaningful for per-station history, matching the same
     * filter ALEController::get_all_lqa_entries() already applies for display).
     * Appends to the in-memory buffer and, if a file is open (open_append()),
     * writes one pipe-delimited line and flushes immediately.
     */
    void record(const LQAHistorySample& sample);

    /**
     * @brief Query stored samples with optional filters.
     *
     * @param since_ms   0 = no lower time bound, else only samples with ts_ms >= since_ms
     * @param station    empty = no station filter
     * @param freq_hz    0 = no frequency filter
     * @param limit      0 = unlimited, else return at most this many (most-recent-first)
     */
    std::vector<LQAHistorySample> query(uint64_t since_ms,
                                        const std::string& station,
                                        uint32_t freq_hz,
                                        size_t limit) const;

    /**
     * @brief One-time load of the full history file into memory, pruning any
     * rows older than the configured retention window. Call once at startup,
     * before open_append().
     * @return true if the file was read (a missing file is not an error — it
     *         just means there is no prior history yet).
     */
    bool load_from_file(const std::string& path);

    /**
     * @brief Open (or create) @p path in append mode and keep the handle open
     * for the process lifetime, so subsequent record() calls persist
     * immediately. Safe to call even if the file doesn't exist yet.
     */
    bool open_append(const std::string& path);

    /// Close the append file handle, if open. Safe to call multiple times.
    void close();

    /**
     * @brief Explicitly wipe both the in-memory buffer and the on-disk file.
     *
     * Deliberately separate from LQADatabase's LQA_CLEAR — history must never
     * be wiped as a side effect of clearing the live/operational view.
     * Re-opens the (now-empty) file in append mode afterward if it was open.
     */
    bool clear_and_truncate(const std::string& path);

    /// Number of samples currently held in memory.
    size_t size() const;

private:
    Config cfg_;
    std::vector<LQAHistorySample> samples_;  ///< Time-ordered oldest-first
    std::ofstream append_file_;

    /// Drop samples older than the retention window from the front of samples_.
    void prune_locked();
};

} // namespace ale
