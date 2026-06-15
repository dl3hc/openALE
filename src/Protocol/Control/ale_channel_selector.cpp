/**
 * \file protocol/ale_channel_selector.cpp
 * \brief ChannelSelector and ListenBeforeTransmit implementations
 */

#include "Protocol/Control/ale_channel_selector.h"
#include <algorithm>

namespace ale {

// ── ChannelSelector ───────────────────────────────────────────────────────────

ChannelSelector::ChannelSelector(const ChannelStore& channels,
                                 const LQAStore&     lqa)
    : channels_(channels), lqa_(lqa) {}

uint32_t ChannelSelector::select_for_call(const std::string& /*remote*/) const {
    auto candidates = channels_.enabled_channels();
    if (candidates.empty()) return 0;

    switch (policy_) {
        case SelectionPolicy::BEST_LQA:    return select_best_lqa(candidates);
        case SelectionPolicy::ROUND_ROBIN: return select_round_robin(candidates);
        case SelectionPolicy::FIXED:       return fixed_freq_;
    }
    return candidates.front().rx_frequency_hz;
}

uint32_t ChannelSelector::select_for_sounding() const {
    auto candidates = channels_.enabled_channels();
    return select_round_robin(candidates);
}

uint32_t ChannelSelector::select_best_lqa(const std::vector<Channel>& candidates) const {
    std::vector<uint32_t> freqs;
    freqs.reserve(candidates.size());
    for (const auto& c : candidates) freqs.push_back(c.rx_frequency_hz);
    uint32_t best = lqa_.best_channel(freqs);
    return best ? best : freqs.front();
}

uint32_t ChannelSelector::select_round_robin(const std::vector<Channel>& candidates) const {
    if (candidates.empty()) return 0;
    uint32_t freq = candidates[rr_index_ % candidates.size()].rx_frequency_hz;
    ++rr_index_;
    return freq;
}

// ── ListenBeforeTransmit ──────────────────────────────────────────────────────

ListenBeforeTransmit::ListenBeforeTransmit(ClearCallback is_clear,
                                           uint32_t listen_ms)
    : is_clear_(std::move(is_clear)), listen_ms_(listen_ms) {}

bool ListenBeforeTransmit::check(uint32_t rx_frequency_hz) const {
    if (!enabled_) return true;
    return is_clear_(rx_frequency_hz);
}

} // namespace ale
