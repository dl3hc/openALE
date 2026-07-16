/**
 * \file ale_channel_manager.h
 * \brief ALEChannelManager — active scan-channel state for ALEStateMachine
 *
 * Owns the scan list (ScanConfig), tracks the current channel index, fires
 * the channel-change callback, and maintains the per-channel LQA score
 * heuristic.  Distinct from ChannelSelector (ale_channel_selector.h), which
 * picks the best outbound channel from the persistent ChannelStore/LQAStore.
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Protocol/Control/ale_channel_types.h"
#include <cstdint>
#include <functional>

namespace ale {

/**
 * \class ALEChannelManager
 * Manages the active scan list and channel hopping for ALEStateMachine.
 *
 * Responsibilities:
 *  - Hold and update the ScanConfig scan list
 *  - Advance the channel index on scan hops (hop_next)
 *  - Fire on_change_ whenever the active channel changes
 *  - Track last hop timestamp for dwell-timeout checks
 *  - Store the per-channel LQA heuristic score (Schritt 6)
 *  - Forward channel-change notifications for the calling path (hop_calling)
 */
class ALEChannelManager {
public:
    // ── Configuration ─────────────────────────────────────────────────────
    void configure(const ScanConfig& config);
    void add_channel(const Channel& ch);

    // ── Channel access ─────────────────────────────────────────────────────
    const Channel*    current()        const;
    const Channel*    select_best()    const;
    size_t            channel_count()  const { return scan_.scan_list.size(); }
    uint32_t          current_index()  const { return scan_.channel_index; }
    const ScanConfig& config()         const { return scan_; }

    // ── Sounding-sweep override ────────────────────────────────────────────
    // A multi-channel sounding sweep (ALEStateMachine::send_sounding_sweep)
    // transmits on each channel in turn without disturbing the scan list. While
    // a sweep is in progress, set_override() pins current() to the sweep channel
    // (and fires on_change_ so the radio tunes to it); clear_override() restores
    // normal scan-list current(). The override is cleared on any return to
    // IDLE/SCANNING (see enter_state).
    void set_override(const Channel& ch);
    void clear_override() { override_active_ = false; }

    // ── Scan lifecycle ─────────────────────────────────────────────────────
    /** Reset to channel 0 and fire callback.  Call on SCANNING entry. */
    void start(uint32_t current_time_ms);

    /** Advance to next channel (wraps), fire callback, update last_hop_ms_. */
    bool hop_next(uint32_t current_time_ms);

    /** Returns true when dwell time has elapsed since the last hop. */
    bool check_dwell_timeout(uint32_t current_time_ms) const;

    /** Wall-clock timestamp of the last hop (set by start()/hop_next()). Exposed
     *  so the SM can anchor the synchronous-backend dwell to the tune-issue
     *  time rather than the first update tick (preserves pre-async behavior). */
    uint32_t last_hop_ms() const { return last_hop_ms_; }

    /** Restart the dwell window from `current_time_ms` WITHOUT changing channel.
     *  The SM calls this on the settle edge so the on-channel observation window
     *  equals the full configured dwell, instead of (dwell − async tune latency).
     *  §A.5.3.3: with an async radio the tune completes some time after the hop, so
     *  anchoring at tune-issue would collapse a 200 ms dwell to ~(200 − settle) ms. */
    void anchor_dwell(uint32_t current_time_ms) { last_hop_ms_ = current_time_ms; }

    // ── LQA score update ───────────────────────────────────────────────────
    /** Write a heuristic LQA score for channel at \p idx. */
    void update_lqa_score(uint32_t idx, float score);

    // ── Calling-path notification ──────────────────────────────────────────
    /** Fire on_change_ for \p ch without mutating scan state.
     *  Used by try_next_calling_channel() to signal calling-channel hops. */
    void hop_calling(const Channel& ch);

    // ── Callback ──────────────────────────────────────────────────────────
    void set_channel_callback(std::function<void(const Channel&)> cb);

private:
    ScanConfig scan_;
    uint32_t   last_hop_ms_ = 0;
    bool       scan_started_ = false;  // false = fresh list, first start goes to ch0
    std::function<void(const Channel&)> on_change_;

    // Sounding-sweep override (see set_override()). When active, current()
    // returns &override_ch_ instead of the scan-list entry.
    bool    override_active_ = false;
    Channel override_ch_;

    /** Set channel_index, stamp last_scan_time_ms, fire on_change_. */
    void apply(uint32_t index, uint32_t current_time_ms);
};

} // namespace ale
