/**
 * \file ale_channel_types.h
 * \brief ALE channel types: Channel, ScanConfig, LinkQuality
 *
 * Canonical channel type shared by ALEStateMachine, ChannelStore, ChannelSelector,
 * and future ALEChannelManager components.
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Protocol/Control/ale_timing.h"

namespace ale {

/**
 * \struct Channel
 * A single ALE channel combining persistent configuration and runtime state.
 *
 * Persistent fields (used by ChannelStore):
 *   frequency_hz  — channel frequency
 *   label         — optional human-readable name
 *   enabled       — whether the channel participates in scan/sounding
 *
 * Runtime fields (used by ALEStateMachine and ALEChannelManager):
 *   mode              — modulation mode (e.g. "USB")
 *   lqa_score         — composite link-quality score
 *   last_scan_time_ms — timestamp of the last scan dwell
 *   call_count        — number of calls attempted on this channel
 */
struct Channel {
    uint32_t    frequency_hz;
    std::string label;
    bool        enabled;
    std::string mode;
    float       lqa_score;
    uint32_t    last_scan_time_ms;
    uint32_t    call_count;

    Channel()
        : frequency_hz(0), label(""), enabled(true), mode("USB"),
          lqa_score(0.0f), last_scan_time_ms(0), call_count(0) {}

    Channel(uint32_t freq, const std::string& m = "USB")
        : frequency_hz(freq), label(""), enabled(true), mode(m),
          lqa_score(0.0f), last_scan_time_ms(0), call_count(0) {}
};

/**
 * \struct ScanConfig
 * Scan-list configuration for ALEStateMachine.
 */
struct ScanConfig {
    std::vector<Channel> scan_list;
    uint32_t dwell_time_ms;
    uint32_t channel_index;
    bool     enabled;

    // dwell_time_ms default = TD5_MS (200 ms, 5 chps basic) per MIL-STD-188-141B Annex B.
    ScanConfig() : dwell_time_ms(static_cast<uint32_t>(ale::TD5_MS)), channel_index(0), enabled(false) {}
};

/**
 * \struct LinkQuality
 * Snapshot of link-quality metrics reported to ALEStateMachine.
 */
struct LinkQuality {
    float    snr_db;
    float    ber;
    uint32_t fec_errors;
    uint32_t total_words;
    uint32_t timestamp_ms;

    LinkQuality() : snr_db(0.0f), ber(0.0f), fec_errors(0),
                    total_words(0), timestamp_ms(0) {}
};

} // namespace ale
