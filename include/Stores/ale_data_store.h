/**
 * \file ale_data_store.h
 * \brief ALE data stores: channels, self-addresses, known stations, LQA, messages,
 *        and operating parameters
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Protocol/Message/ale_message.h"
#include "LQA/lqa_database.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace ale {

// ── Channel ───────────────────────────────────────────────────────────────────

/**
 * \struct Channel
 * A single ALE channel with its frequency and optional label
 */
struct Channel {
    uint32_t    frequency_hz;
    std::string label;
    bool        enabled;

    Channel() : frequency_hz(0), enabled(true) {}
    explicit Channel(uint32_t freq_hz, std::string lbl = "", bool en = true)
        : frequency_hz(freq_hz), label(std::move(lbl)), enabled(en) {}
};

// ── ChannelStore ──────────────────────────────────────────────────────────────

/**
 * \class ChannelStore
 * Ordered list of ALE channels used for scanning and sounding
 *
 * Channels are kept in insertion order; duplicates (same frequency_hz) are
 * silently ignored.
 */
class ChannelStore {
public:
    ChannelStore() = default;

    void add_channel(const Channel& ch);
    void remove_channel(uint32_t frequency_hz);
    bool has_channel(uint32_t frequency_hz) const;
    void set_enabled(uint32_t frequency_hz, bool enabled);

    const std::vector<Channel>& all()              const { return channels_; }
    std::vector<Channel>        enabled_channels() const;
    size_t size()  const { return channels_.size(); }
    bool   empty() const { return channels_.empty(); }
    void   clear();

private:
    std::vector<Channel> channels_;
};

// ── SelfAddressStore ──────────────────────────────────────────────────────────

/**
 * \class SelfAddressStore
 * Local station addresses: one primary address, optional secondary addresses,
 * and optional net (group) addresses
 *
 * All addresses must be Basic 38 (A-Z, 0-9, '@', '?'), 3-15 characters.
 */
class SelfAddressStore {
public:
    SelfAddressStore() = default;

    void set_primary(const std::string& address);
    void add_secondary(const std::string& address);
    void add_net(const std::string& net_address);

    const std::string&              primary()            const { return primary_; }
    const std::vector<std::string>& secondary_addresses() const { return secondary_; }
    const std::vector<std::string>& net_addresses()       const { return nets_; }

    /** Return true if \p address matches any of the local station's addresses. */
    bool matches_self(const std::string& address) const;

private:
    std::string              primary_;
    std::vector<std::string> secondary_;
    std::vector<std::string> nets_;
};

// ── OtherStationStore ────────────────────────────────────────────────────────

/**
 * \struct StationInfo
 * Known remote station entry with contact metadata
 */
struct StationInfo {
    std::string address;
    std::string name;              ///< Optional friendly name
    uint32_t    last_contact_ms;
    uint32_t    contact_count;

    StationInfo() : last_contact_ms(0), contact_count(0) {}
};

/**
 * \class OtherStationStore
 * Directory of known remote stations
 *
 * Stations can be pre-populated (call book) or discovered automatically when
 * a FROM word is received for an unknown address.
 */
class OtherStationStore {
public:
    OtherStationStore() = default;

    void add_station(const StationInfo& info);
    void update_contact(const std::string& address, uint32_t timestamp_ms);
    bool has_station(const std::string& address) const;

    const StationInfo*              get(const std::string& address) const;
    const std::vector<StationInfo>& all() const { return stations_; }
    void clear();

private:
    std::vector<StationInfo> stations_;
};

// ── LQAStore ─────────────────────────────────────────────────────────────────

/**
 * \class LQAStore
 * Thin facade over LQADatabase providing channel-selection queries
 *
 * Records incoming LQA measurements and exposes a best_channel() query
 * used by ChannelSelector.
 */
class LQAStore {
public:
    LQAStore() = default;

    void record(uint32_t frequency_hz, const std::string& remote,
                float snr_db, float ber, int fec_errors, int total_words,
                uint32_t timestamp_ms = 0);

    /** Return the highest composite score for any station on \p frequency_hz. */
    float    best_score(uint32_t frequency_hz) const;

    /** Return the frequency with the highest LQA score from \p candidates. */
    uint32_t best_channel(const std::vector<uint32_t>& candidates) const;

    const LQADatabase& database() const { return db_; }
    LQADatabase&       database()       { return db_; }

private:
    LQADatabase db_;
};

// ── MessageStore ──────────────────────────────────────────────────────────────

/**
 * \class MessageStore
 * Bounded ring buffer of received ALE messages
 *
 * When the buffer is full the oldest message is evicted to make room.
 * Default capacity: 64 messages.
 */
class MessageStore {
public:
    explicit MessageStore(size_t capacity = 64);

    void push(const ALEMessage& msg);
    bool pop_oldest(ALEMessage& out);

    size_t size()  const { return messages_.size(); }
    bool   empty() const { return messages_.empty(); }
    void   clear()       { messages_.clear(); }

    const std::vector<ALEMessage>& all() const { return messages_; }

private:
    std::vector<ALEMessage> messages_;
    size_t                  capacity_;
};

// ── OperatingParameters ───────────────────────────────────────────────────────

/**
 * \struct OperatingParameters
 * Run-time tunables for the ALE engine
 *
 * These may be changed at any time; the state machine reads them before each
 * significant state transition.
 */
struct OperatingParameters {
    uint32_t scan_dwell_ms       = 500;     ///< Dwell time per channel during scan
    uint32_t sounding_period_ms  = 600000;  ///< Inter-sounding interval (default 10 min)
    uint32_t call_timeout_ms     = 30000;   ///< Max wait for response to a call
    uint32_t amd_max_length      = 90;      ///< Max AMD message length (MIL-STD-188-141B)
    bool     lqa_enabled         = true;    ///< Record and use LQA scores
    bool     amd_enabled         = true;    ///< Accept AMD (Automatic Message Display) calls
    bool     lbt_enabled         = true;    ///< Listen-Before-Transmit
    uint32_t lbt_listen_ms       = 1000;    ///< LBT listen window duration
};

} // namespace ale
