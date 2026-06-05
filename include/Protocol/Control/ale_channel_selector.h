/**
 * \file ale_channel_selector.h
 * \brief Channel selection and Listen-Before-Transmit for ALE
 *
 * ChannelSelector picks the best outbound channel from the scan list using
 * LQA scores.  ListenBeforeTransmit performs a carrier-sense check before
 * the transmitter is keyed.
 *
 * Specification: MIL-STD-188-141B Appendix A, Section A.5.2.2
 */

#pragma once

#include "Stores/ale_data_store.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ale {

// ── ChannelSelector ───────────────────────────────────────────────────────────

/**
 * \enum SelectionPolicy
 * Strategy for choosing the best transmit channel
 */
enum class SelectionPolicy {
    BEST_LQA,    ///< Highest composite LQA score across enabled channels
    ROUND_ROBIN, ///< Cycle through enabled channels regardless of LQA
    FIXED,       ///< Always use a single pre-configured channel
};

/**
 * \class ChannelSelector
 * Select the best ALE channel for outbound transmissions
 *
 * Queries LQAStore to find the highest-scoring enabled channel for a
 * given remote station.  Falls back to the first enabled channel when
 * no LQA data is available.
 */
class ChannelSelector {
public:
    explicit ChannelSelector(const ChannelStore& channels,
                             const LQAStore&     lqa);

    /**
     * Select the best channel for a call to \p remote.
     *
     * \param remote  Remote station address (Basic 38)
     * \return Frequency in Hz of the selected channel, or 0 if none available
     */
    uint32_t select_for_call(const std::string& remote) const;

    /**
     * Select the next channel for an outbound sounding.
     *
     * Uses round-robin to distribute soundings evenly across all enabled
     * channels regardless of the current SelectionPolicy.
     *
     * \return Frequency in Hz, or 0 if the channel list is empty
     */
    uint32_t select_for_sounding() const;

    void            set_policy(SelectionPolicy policy) { policy_ = policy; }
    SelectionPolicy policy()                     const { return policy_; }

    /** Pre-configure the channel used when policy == FIXED. */
    void     set_fixed_channel(uint32_t frequency_hz) { fixed_freq_ = frequency_hz; }
    uint32_t fixed_channel()                    const { return fixed_freq_; }

private:
    const ChannelStore& channels_;
    const LQAStore&     lqa_;
    SelectionPolicy     policy_     = SelectionPolicy::BEST_LQA;
    uint32_t            fixed_freq_ = 0;
    mutable size_t      rr_index_   = 0;

    uint32_t select_best_lqa(const std::vector<Channel>& candidates) const;
    uint32_t select_round_robin(const std::vector<Channel>& candidates) const;
};

// ── ListenBeforeTransmit ──────────────────────────────────────────────────────

/**
 * \class ListenBeforeTransmit
 * Carrier-sense check before keying the transmitter (MIL-STD-188-141B A.5.2.2)
 *
 * The transmitter must not be keyed if the channel is occupied.  This class
 * enforces a configurable listen window by calling a user-supplied
 * energy-detect function.  When disabled (e.g. for testing), check() always
 * returns true.
 */
class ListenBeforeTransmit {
public:
    /**
     * Energy-detect callback supplied by the platform layer.
     * \param frequency_hz  Channel to check
     * \return true if the channel is clear (no carrier detected)
     */
    using ClearCallback = std::function<bool(uint32_t frequency_hz)>;

    explicit ListenBeforeTransmit(ClearCallback is_clear,
                                  uint32_t listen_ms = 1000);

    /**
     * Perform a listen-before-transmit check on \p frequency_hz.
     *
     * Invokes the energy-detect callback.  Returns true if the channel is
     * clear and transmission may proceed; false if the channel is busy.
     *
     * \param frequency_hz  Channel to check
     * \return true if clear, false if busy
     */
    bool check(uint32_t frequency_hz) const;

    void     set_listen_ms(uint32_t ms) { listen_ms_ = ms; }
    uint32_t listen_ms()          const { return listen_ms_; }

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled()           const { return enabled_; }

private:
    ClearCallback is_clear_;
    uint32_t      listen_ms_;
    bool          enabled_ = true;
};

} // namespace ale
