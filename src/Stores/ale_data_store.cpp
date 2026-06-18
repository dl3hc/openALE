/**
 * \file ale_data_store.cpp
 * \brief ALE data store implementations
 */

#include "Stores/ale_data_store.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace ale {

// ── ChannelStore ──────────────────────────────────────────────────────────────

bool ChannelStore::add_channel(const Channel& ch) {
    if (has_channel(ch.rx_frequency_hz)) return false;
    if (channels_.size() >= kCapacity)   return false;
    channels_.push_back(ch);
    return true;
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

bool ChannelStore::save(IPersistenceBackend& backend) const {
    return backend.save(channels_);
}

bool ChannelStore::load(IPersistenceBackend& backend) {
    std::vector<Channel> loaded;
    if (!backend.load(loaded)) return false;
    channels_ = std::move(loaded);
    return true;
}

// ── FileChannelBackend ────────────────────────────────────────────────────────

bool FileChannelBackend::save(const std::vector<Channel>& channels) {
    std::ofstream f(path_);
    if (!f.is_open()) return false;
    for (const auto& ch : channels) {
        if (!ch.id.empty()) f << "ID:" << ch.id << ' ';
        f << ch.rx_frequency_hz << ' ' << ch.tx_frequency_hz << ' ' << ch.rx_mode;
        if (!ch.label.empty()) f << ' ' << ch.label;
        f << '\n';
    }
    return f.good();
}

bool FileChannelBackend::load(std::vector<Channel>& channels) {
    std::ifstream f(path_);
    if (!f.is_open()) return false;
    channels.clear();
    std::string line;
    while (std::getline(f, line)) {
        auto cpos = line.find('#');
        if (cpos != std::string::npos) line = line.substr(0, cpos);
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::vector<std::string> toks;
        { std::string tok; while (iss >> tok) toks.push_back(std::move(tok)); }
        if (toks.empty()) continue;

        std::string id;
        if (toks[0].rfind("ID:", 0) == 0) {
            id = toks[0].substr(3);
            toks.erase(toks.begin());
        }
        if (toks.empty()) continue;

        uint32_t rx_hz = 0, tx_hz = 0;
        try { rx_hz = static_cast<uint32_t>(std::stoul(toks[0])); } catch (...) { continue; }
        if (toks.size() > 1) {
            try { tx_hz = static_cast<uint32_t>(std::stoul(toks[1])); } catch (...) {}
        }
        std::string mode = (toks.size() > 2) ? toks[2] : "USB";
        std::string label;
        for (size_t i = 3; i < toks.size(); ++i) {
            if (!label.empty()) label += ' ';
            label += toks[i];
        }
        Channel ch(rx_hz, tx_hz, mode, mode);
        ch.id    = id;
        ch.label = label;
        channels.push_back(ch);
    }
    return true;
}

// ── NetStore ─────────────────────────────────────────────────────────────────

bool NetStore::add_net(const std::string& name) {
    if (find(name)) return false;
    nets_.push_back(Net{name, {}});
    return true;
}

bool NetStore::remove_net(const std::string& name) {
    const size_t before = nets_.size();
    nets_.erase(std::remove_if(nets_.begin(), nets_.end(),
        [&](const Net& n) { return n.name == name; }), nets_.end());
    return nets_.size() != before;
}

Net* NetStore::find_mutable(const std::string& name) {
    for (auto& n : nets_)
        if (n.name == name) return &n;
    return nullptr;
}

bool NetStore::assign_channel(const std::string& net_name, const std::string& channel_id) {
    Net* n = find_mutable(net_name);
    if (!n) return false;
    if (std::find(n->channel_ids.begin(), n->channel_ids.end(), channel_id) == n->channel_ids.end())
        n->channel_ids.push_back(channel_id);
    return true;
}

bool NetStore::unassign_channel(const std::string& net_name, const std::string& channel_id) {
    Net* n = find_mutable(net_name);
    if (!n) return false;
    n->channel_ids.erase(
        std::remove(n->channel_ids.begin(), n->channel_ids.end(), channel_id),
        n->channel_ids.end());
    return true;
}

void NetStore::unassign_channel_everywhere(const std::string& channel_id) {
    for (auto& n : nets_)
        n.channel_ids.erase(
            std::remove(n.channel_ids.begin(), n.channel_ids.end(), channel_id),
            n.channel_ids.end());
}

const Net* NetStore::find(const std::string& name) const {
    for (const auto& n : nets_)
        if (n.name == name) return &n;
    return nullptr;
}

void NetStore::clear() { nets_.clear(); }

uint32_t net_scan_channel_count(const Net& net, const std::vector<Channel>& channels) {
    uint32_t count = 0;
    for (const auto& id : net.channel_ids)
        for (const auto& ch : channels)
            if (ch.id == id && ch.enabled) { ++count; break; }
    return count;
}

// ── ContactStore ─────────────────────────────────────────────────────────────

bool ContactStore::add_or_update(const Contact& c) {
    if (c.callsign.empty()) return false;
    for (auto& existing : contacts_) {
        if (existing.callsign == c.callsign) { existing = c; return true; }
    }
    contacts_.push_back(c);
    return true;
}

bool ContactStore::remove(const std::string& callsign) {
    const size_t before = contacts_.size();
    contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
        [&](const Contact& c) { return c.callsign == callsign; }), contacts_.end());
    return contacts_.size() != before;
}

const Contact* ContactStore::find(const std::string& callsign) const {
    for (const auto& c : contacts_)
        if (c.callsign == callsign) return &c;
    return nullptr;
}

void ContactStore::clear() { contacts_.clear(); }

// ── SelfAddressStore ──────────────────────────────────────────────────────────

bool SelfAddressStore::add(const SelfAddressEntry& entry) {
    if (entry.address.empty()) return false;
    for (auto& existing : entries_) {
        if (existing.address == entry.address) { existing = entry; return true; }
    }
    if (entries_.size() >= kCapacity) return false;
    entries_.push_back(entry);
    if (primary_.empty()) primary_ = entry.address;
    return true;
}

bool SelfAddressStore::remove(const std::string& address) {
    const size_t before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [&](const SelfAddressEntry& e) { return e.address == address; }), entries_.end());
    if (entries_.size() == before) return false;
    if (primary_ == address)
        primary_ = entries_.empty() ? std::string() : entries_.front().address;
    return true;
}

bool SelfAddressStore::set_primary(const std::string& address) {
    if (!find(address)) return false;
    primary_ = address;
    return true;
}

const SelfAddressEntry* SelfAddressStore::find(const std::string& address) const {
    for (const auto& e : entries_)
        if (e.address == address) return &e;
    return nullptr;
}

void SelfAddressStore::clear() {
    entries_.clear();
    primary_.clear();
}

bool SelfAddressStore::matches_self(const std::string& address) const {
    return find(address) != nullptr;
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
