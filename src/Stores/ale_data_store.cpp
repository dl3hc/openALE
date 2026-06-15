/**
 * \file ale_data_store.cpp
 * \brief ALE data store implementations
 */

#include "Stores/ale_data_store.h"
#include <algorithm>

namespace ale {

// ── ChannelStore ──────────────────────────────────────────────────────────────

void ChannelStore::add_channel(const Channel& ch) {
    if (!has_channel(ch.rx_frequency_hz))
        channels_.push_back(ch);
}

void ChannelStore::remove_channel(uint32_t rx_frequency_hz) {
    channels_.erase(
        std::remove_if(channels_.begin(), channels_.end(),
            [&](const Channel& c) { return c.rx_frequency_hz == rx_frequency_hz; }),
        channels_.end());
}

bool ChannelStore::has_channel(uint32_t rx_frequency_hz) const {
    return std::any_of(channels_.begin(), channels_.end(),
        [&](const Channel& c) { return c.rx_frequency_hz == rx_frequency_hz; });
}

void ChannelStore::set_enabled(uint32_t rx_frequency_hz, bool enabled) {
    for (auto& c : channels_)
        if (c.rx_frequency_hz == rx_frequency_hz) { c.enabled = enabled; return; }
}

std::vector<Channel> ChannelStore::enabled_channels() const {
    std::vector<Channel> out;
    for (const auto& c : channels_)
        if (c.enabled) out.push_back(c);
    return out;
}

void ChannelStore::clear() { channels_.clear(); }

// ── SelfAddressStore ──────────────────────────────────────────────────────────

void SelfAddressStore::set_primary(const std::string& address) {
    primary_ = address;
}

void SelfAddressStore::add_secondary(const std::string& address) {
    if (std::find(secondary_.begin(), secondary_.end(), address) == secondary_.end())
        secondary_.push_back(address);
}

void SelfAddressStore::add_net(const std::string& net_address) {
    if (std::find(nets_.begin(), nets_.end(), net_address) == nets_.end())
        nets_.push_back(net_address);
}

bool SelfAddressStore::matches_self(const std::string& address) const {
    if (address == primary_) return true;
    if (std::find(secondary_.begin(), secondary_.end(), address) != secondary_.end())
        return true;
    if (std::find(nets_.begin(), nets_.end(), address) != nets_.end())
        return true;
    return false;
}

// ── OtherStationStore ────────────────────────────────────────────────────────

void OtherStationStore::add_station(const StationInfo& info) {
    if (!has_station(info.address))
        stations_.push_back(info);
}

void OtherStationStore::update_contact(const std::string& address,
                                        uint32_t timestamp_ms) {
    for (auto& s : stations_) {
        if (s.address == address) {
            s.last_contact_ms = timestamp_ms;
            ++s.contact_count;
            return;
        }
    }
    // Implicitly add previously unknown stations seen on air
    StationInfo info;
    info.address         = address;
    info.last_contact_ms = timestamp_ms;
    info.contact_count   = 1;
    stations_.push_back(info);
}

bool OtherStationStore::has_station(const std::string& address) const {
    return std::any_of(stations_.begin(), stations_.end(),
        [&](const StationInfo& s) { return s.address == address; });
}

const StationInfo* OtherStationStore::get(const std::string& address) const {
    for (const auto& s : stations_)
        if (s.address == address) return &s;
    return nullptr;
}

void OtherStationStore::clear() { stations_.clear(); }

// ── LQAStore ─────────────────────────────────────────────────────────────────

void LQAStore::record(uint32_t frequency_hz, const std::string& remote,
                      float snr_db, float ber, int fec_errors, int total_words,
                      uint32_t timestamp_ms) {
    db_.update_entry(frequency_hz, remote, snr_db, ber, fec_errors,
                     total_words, timestamp_ms);
}

float LQAStore::best_score(uint32_t frequency_hz) const {
    auto entries = db_.get_entries_for_channel(frequency_hz);
    float best = -1.0f;
    for (const auto& e : entries)
        if (e.score > best) best = e.score;
    return best;
}

uint32_t LQAStore::best_channel(const std::vector<uint32_t>& candidates) const {
    if (candidates.empty()) return 0;
    uint32_t best_freq = candidates.front();
    float    best      = -1.0f;
    for (uint32_t freq : candidates) {
        float s = best_score(freq);
        if (s > best) { best = s; best_freq = freq; }
    }
    return best_freq;
}

// ── MessageStore ──────────────────────────────────────────────────────────────

MessageStore::MessageStore(size_t capacity) : capacity_(capacity) {}

void MessageStore::push(const ALEMessage& msg) {
    if (messages_.size() >= capacity_)
        messages_.erase(messages_.begin());
    messages_.push_back(msg);
}

bool MessageStore::pop_oldest(ALEMessage& out) {
    if (messages_.empty()) return false;
    out = messages_.front();
    messages_.erase(messages_.begin());
    return true;
}

} // namespace ale
