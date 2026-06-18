/**
 * \file ale_data_store.h
 * \brief ALE data stores: channels, self-addresses, known stations, LQA, messages,
 *        and operating parameters
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Protocol/Control/ale_channel_types.h"
#include "Protocol/Message/ale_message.h"
#include "LQA/lqa_database.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace ale {

// ── IPersistenceBackend ───────────────────────────────────────────────────────

/**
 * \class IPersistenceBackend
 * Abstract persistence interface for nonvolatile channel storage.
 *
 * Concrete implementations (file, EEPROM, …) derive from this class.
 * MIL-STD-188-141B Table A-III requires nonvolatile channel storage.
 */
class IPersistenceBackend {
public:
    virtual ~IPersistenceBackend() = default;
    virtual bool save(const std::vector<Channel>& channels) = 0;
    virtual bool load(std::vector<Channel>& channels) = 0;
};

/**
 * \class FileChannelBackend
 * File-based implementation of IPersistenceBackend.
 *
 * Format: one channel per line — [ID:<id>] rx_hz tx_hz mode [label]
 * Lines starting with '#' and blank lines are ignored on load.
 */
class FileChannelBackend : public IPersistenceBackend {
public:
    explicit FileChannelBackend(std::string path) : path_(std::move(path)) {}

    bool save(const std::vector<Channel>& channels) override;
    bool load(std::vector<Channel>& channels) override;

private:
    std::string path_;
};

// ── ChannelStore ──────────────────────────────────────────────────────────────

/**
 * \class ChannelStore
 * Ordered list of ALE channels used for scanning and sounding
 *
 * Channels are kept in insertion order; duplicates (same rx_frequency_hz) are
 * silently ignored.
 */
class ChannelStore {
public:
    /** MIL-STD-188-141B Table A-III: 100-channel nonvolatile channel memory. */
    static constexpr size_t kCapacity = 100;

    ChannelStore() = default;

    /** Returns false if the channel is a duplicate (same rx_frequency_hz) or the store is full. */
    bool add_channel(const Channel& ch);
    void remove_channel(uint32_t rx_frequency_hz);
    bool has_channel(uint32_t rx_frequency_hz) const;
    void set_enabled(uint32_t rx_frequency_hz, bool enabled);

    const std::vector<Channel>& all()              const { return channels_; }
    std::vector<Channel>        enabled_channels() const;
    size_t size()  const { return channels_.size(); }
    bool   empty() const { return channels_.empty(); }
    void   clear();

    /** Persist current channels via \p backend (MIL-STD-188-141B nonvolatile storage). */
    bool save(IPersistenceBackend& backend) const;

    /** Replace current channels with those loaded from \p backend. */
    bool load(IPersistenceBackend& backend);

private:
    std::vector<Channel> channels_;
};

// ── NetStore ─────────────────────────────────────────────────────────────────

/**
 * \struct Net
 * A named net (group) and the channel IDs assigned to it.
 *
 * Channels carry their own scan/sounding eligibility (Channel::enabled, the
 * Annex B "SCAN Y/N" flag); a net is just an ordered set of channel IDs.
 */
struct Net {
    std::string              name;
    std::vector<std::string> channel_ids;
};

/**
 * \class NetStore
 * Named nets and their channel-ID membership.
 *
 * Nets are kept in insertion order; duplicate names are rejected. Channel
 * membership is by Channel::id (see ale_channel_types.h), not by frequency,
 * so reassigning a channel's frequency doesn't disturb net membership.
 */
class NetStore {
public:
    NetStore() = default;

    bool add_net(const std::string& name);
    bool remove_net(const std::string& name);

    /** Add channel_id to net's member list (no-op, returns true, if already a member). */
    bool assign_channel(const std::string& net_name, const std::string& channel_id);
    bool unassign_channel(const std::string& net_name, const std::string& channel_id);

    /** Remove channel_id from every net's member list (e.g. after deleting a channel). */
    void unassign_channel_everywhere(const std::string& channel_id);

    const Net* find(const std::string& name) const;
    const std::vector<Net>& all() const { return nets_; }
    size_t size()  const { return nets_.size(); }
    bool   empty() const { return nets_.empty(); }
    void   clear();

private:
    std::vector<Net> nets_;
    Net* find_mutable(const std::string& name);
};

/**
 * Count of \p net's member channels that are currently scan/sounding-enabled
 * (Channel::enabled, the Annex B "SCAN Y/N" flag) — this is "C" in
 * Tsc = C × 2 × Trw for a call to a station belonging to this net.
 */
uint32_t net_scan_channel_count(const Net& net, const std::vector<Channel>& channels);

// ── ContactStore ─────────────────────────────────────────────────────────────

/**
 * \struct Contact
 * A GUI-facing address-book entry: a remote station plus its net membership
 * and the channels it's reachable on.
 */
struct Contact {
    std::string              callsign;
    std::string              name;
    bool                     enabled       = true;
    std::vector<std::string> net_members;     ///< Net names this contact belongs to
    std::vector<std::string> valid_channels;  ///< Channel IDs; ignored when all_channels
    bool                     all_channels  = true;  ///< "ALL" sentinel
};

/**
 * \class ContactStore
 * Address book of known remote stations (GUI-facing; backs
 * ALEController::add_contact() and friends).
 *
 * Keyed by callsign; add_or_update() overwrites an existing entry in place.
 */
class ContactStore {
public:
    ContactStore() = default;

    bool add_or_update(const Contact& c);
    bool remove(const std::string& callsign);
    const Contact* find(const std::string& callsign) const;

    const std::vector<Contact>& all() const { return contacts_; }
    size_t size()  const { return contacts_.size(); }
    bool   empty() const { return contacts_.empty(); }
    void   clear();

private:
    std::vector<Contact> contacts_;
};

// ── SelfAddressStore ──────────────────────────────────────────────────────────

/**
 * \struct SelfAddressEntry
 * One local station address: enabled/disabled, plus which channels it's
 * valid on (GUI-facing; backs ALEController::add_self_address()).
 */
struct SelfAddressEntry {
    std::string              address;
    bool                     enabled       = true;
    std::vector<std::string> valid_channels;  ///< Channel IDs; ignored when all_channels
    bool                     all_channels  = true;  ///< "ALL" sentinel
};

/**
 * \class SelfAddressStore
 * Local station addresses: an ordered table of entries plus a designated
 * primary address (the one actually used by ALEStateMachine::set_self_address).
 *
 * All addresses must be Basic 38 (A-Z, 0-9, '@', '?'), 3-15 characters.
 */
class SelfAddressStore {
public:
    /** MIL-STD-188-141B REQ-GEN-014: minimum 20 own-station address slots. */
    static constexpr size_t kCapacity = 20;

    SelfAddressStore() = default;

    /** Adds the entry; the first entry added becomes primary automatically.
     *  Returns false if the store is full (kCapacity) and the address is new. */
    bool add(const SelfAddressEntry& entry);
    bool remove(const std::string& address);

    /** Returns false if \p address isn't a known entry. */
    bool set_primary(const std::string& address);
    const std::string& primary() const { return primary_; }

    const SelfAddressEntry* find(const std::string& address) const;
    const std::vector<SelfAddressEntry>& all() const { return entries_; }
    size_t size()  const { return entries_.size(); }
    bool   empty() const { return entries_.empty(); }
    void clear();

    /** Return true if \p address matches any known (enabled or not) entry. */
    bool matches_self(const std::string& address) const;

private:
    std::vector<SelfAddressEntry> entries_;
    std::string                   primary_;
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
    /** MIL-STD-188-141B REQ-GEN-015/016: minimum 100 known remote station slots. */
    static constexpr size_t kCapacity = 100;

    OtherStationStore() = default;

    /** Returns false if the store is full (kCapacity) and the address is new. */
    bool add_station(const StationInfo& info);
    void update_contact(const std::string& address, uint32_t timestamp_ms);
    bool has_station(const std::string& address) const;

    const StationInfo*              get(const std::string& address) const;
    const std::vector<StationInfo>& all() const { return stations_; }
    size_t size()  const { return stations_.size(); }
    bool   empty() const { return stations_.empty(); }
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
    // scan_dwell_ms: TD2_MS (500 ms, 2 chps minimum) per MIL-STD-188-141B Annex B.
    uint32_t scan_dwell_ms       = static_cast<uint32_t>(ale::TD2_MS);  ///< Dwell time per channel during scan
    uint32_t sounding_period_ms  = 600000;  ///< Inter-sounding interval (default 10 min; not an ALE spec constant)
    // call_timeout_ms: Twa_ms (30 s activity timeout) per MIL-STD-188-141B Table A-XV.
    uint32_t call_timeout_ms     = ale::Twa_ms;  ///< Max wait for response to a call
    uint32_t amd_max_length      = 90;      ///< Max AMD message length (MIL-STD-188-141B)
    bool     lqa_enabled         = true;    ///< Record and use LQA scores
    bool     amd_enabled         = true;    ///< Accept AMD (Automatic Message Display) calls
    bool     lbt_enabled         = true;    ///< Listen-Before-Transmit
    uint32_t lbt_listen_ms       = 1000;    ///< LBT listen window (SW default; ALE spec: Twt_ale_ms = 784 ms)
};

} // namespace ale
