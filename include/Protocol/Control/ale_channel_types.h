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
 * A single ALE channel — persistent configuration + runtime state.
 *
 * Implements MIL-STD-188-141B A.4.3.1 / Table A-III channel memory.
 *
 * Persistent fields (stored in ChannelStore, 100-channel nonvolatile memory):
 *   rx_frequency_hz  — receive frequency; used as the channel key
 *   tx_frequency_hz  — transmit frequency; 0 = simplex (= rx_frequency_hz)
 *   rx_mode          — modulation mode for RX (e.g. "USB")
 *   tx_mode          — modulation mode for TX (usually same as RX)
 *   label            — optional human-readable name
 *   enabled          — participates in scan/sounding
 *   rx_only          — R-only channel (no TX)
 *   voice_use        — voice traffic allowed on this channel
 *   data_use         — data traffic allowed on this channel
 *   secure           — security setting (DO; clear=false/secure=true)
 *   power_pct        — TX power 0–100% (DO)
 *   antenna          — antenna port 1–4 (DO)
 *   sounding_interval_ms — periodic sounding interval; 0 = no sounding
 *
 * Runtime fields (maintained by ALEStateMachine / ALEChannelManager):
 *   next_sound_ms    — countdown until next sounding transmission
 *   lqa_score        — composite link-quality score
 *   last_scan_time_ms— timestamp of the last scan dwell
 *   call_count       — calls attempted on this channel
 */
struct Channel {
    // ── Frequency (A.4.3.1 / Table A-III) ────────────────────────────────
    uint32_t    rx_frequency_hz = 0;   ///< RX frequency in Hz (channel key)
    uint32_t    tx_frequency_hz = 0;   ///< TX frequency in Hz; 0 = simplex

    // ── Mode ─────────────────────────────────────────────────────────────
    std::string rx_mode = "USB";       ///< Modulation mode for RX
    std::string tx_mode = "USB";       ///< Modulation mode for TX

    // ── Configuration ────────────────────────────────────────────────────
    std::string label;
    bool        enabled              = true;
    bool        rx_only              = false;  ///< R-only (no TX)
    bool        voice_use            = true;
    bool        data_use             = true;
    bool        secure               = false;  ///< DO: security mode
    uint8_t     power_pct            = 100;    ///< DO: TX power 0–100%
    uint8_t     antenna              = 1;      ///< DO: antenna port 1–4
    uint32_t    sounding_interval_ms = 0;      ///< 0 = no sounding

    // ── Runtime ──────────────────────────────────────────────────────────
    uint32_t    next_sound_ms     = 0;   ///< Countdown until next sounding
    float       lqa_score         = 0.f;
    uint32_t    last_scan_time_ms = 0;
    uint32_t    call_count        = 0;

    // ── Helpers ──────────────────────────────────────────────────────────

    /** Effective TX frequency: falls back to RX frequency for simplex. */
    uint32_t effective_tx_hz() const {
        return tx_frequency_hz ? tx_frequency_hz : rx_frequency_hz;
    }

    Channel() = default;

    /** Simplex channel: single frequency, optional mode. */
    explicit Channel(uint32_t rx_hz, const std::string& mode = "USB")
        : rx_frequency_hz(rx_hz), tx_frequency_hz(0),
          rx_mode(mode), tx_mode(mode) {}

    /** Half-duplex channel: separate TX and RX frequencies (and modes). */
    Channel(uint32_t rx_hz, uint32_t tx_hz,
            const std::string& r_mode = "USB", const std::string& t_mode = "USB")
        : rx_frequency_hz(rx_hz), tx_frequency_hz(tx_hz),
          rx_mode(r_mode), tx_mode(t_mode) {}
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
