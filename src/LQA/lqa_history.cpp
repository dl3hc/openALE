/**
 * @file lqa_history.cpp
 * @brief Implementation of LQAHistoryStore
 */

#include "LQA/lqa_history.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace ale {

namespace {

uint64_t now_epoch_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/// Splits a pipe-delimited line into exactly 6 fields; returns false (out
/// untouched) if malformed.
bool parse_line(const std::string& line, LQAHistorySample& out) {
    std::vector<std::string> f;
    f.reserve(6);
    size_t start = 0;
    while (true) {
        size_t pos = line.find('|', start);
        f.push_back(line.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (f.size() != 6) return false;
    try {
        out.ts_ms        = std::stoull(f[0]);
        out.frequency_hz = static_cast<uint32_t>(std::stoul(f[1]));
        out.station       = f[2];
        out.sinad_db      = std::stof(f[3]);
        out.ber           = std::stof(f[4]);
        out.score         = std::stof(f[5]);
    } catch (const std::exception&) {
        return false;
    }
    return !out.station.empty();
}

std::string format_line(const LQAHistorySample& s) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%llu|%u|%s|%.2f|%.2f|%.2f",
                  static_cast<unsigned long long>(s.ts_ms), s.frequency_hz, s.station.c_str(),
                  s.sinad_db, s.ber, s.score);
    return buf;
}

} // namespace

LQAHistoryStore::LQAHistoryStore() {}

LQAHistoryStore::~LQAHistoryStore() { close(); }

void LQAHistoryStore::set_config(const Config& cfg) {
    cfg_ = cfg;
    prune_locked();
}

LQAHistoryStore::Config LQAHistoryStore::get_config() const { return cfg_; }

void LQAHistoryStore::prune_locked() {
    if (cfg_.retention_days == 0 || samples_.empty()) return;
    const uint64_t cutoff = now_epoch_ms() -
        static_cast<uint64_t>(cfg_.retention_days) * 86400000ull;
    size_t drop = 0;
    while (drop < samples_.size() && samples_[drop].ts_ms < cutoff) ++drop;
    if (drop > 0) samples_.erase(samples_.begin(), samples_.begin() + static_cast<long>(drop));
}

void LQAHistoryStore::record(const LQAHistorySample& sample) {
    if (!cfg_.enabled || sample.station.empty()) return;
    samples_.push_back(sample);
    prune_locked();
    if (append_file_.is_open()) {
        append_file_ << format_line(sample) << "\n";
        append_file_.flush();
    }
}

std::vector<LQAHistorySample> LQAHistoryStore::query(uint64_t since_ms,
                                                       const std::string& station,
                                                       uint32_t freq_hz,
                                                       size_t limit) const {
    std::vector<LQAHistorySample> out;
    out.reserve(limit != 0 ? std::min(limit, samples_.size()) : samples_.size());
    // samples_ is oldest-first; iterate newest-first so `limit` keeps the most recent rows.
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (since_ms != 0 && it->ts_ms < since_ms) continue;
        if (!station.empty() && it->station != station) continue;
        if (freq_hz != 0 && it->frequency_hz != freq_hz) continue;
        out.push_back(*it);
        if (limit != 0 && out.size() >= limit) break;
    }
    return out;
}

bool LQAHistoryStore::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;  // no prior history yet — not an error
    samples_.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        LQAHistorySample s;
        if (parse_line(line, s)) samples_.push_back(s);
    }
    // Already chronological (append-only); sort defensively in case
    // hand-edited or concatenated out of order.
    std::sort(samples_.begin(), samples_.end(),
              [](const LQAHistorySample& a, const LQAHistorySample& b) { return a.ts_ms < b.ts_ms; });
    prune_locked();
    return true;
}

bool LQAHistoryStore::open_append(const std::string& path) {
    close();
    append_file_.open(path, std::ios::out | std::ios::app);
    return append_file_.is_open();
}

void LQAHistoryStore::close() {
    if (append_file_.is_open()) append_file_.close();
}

bool LQAHistoryStore::clear_and_truncate(const std::string& path) {
    const bool was_open = append_file_.is_open();
    close();
    samples_.clear();
    std::ofstream trunc(path, std::ios::out | std::ios::trunc);
    const bool ok = trunc.is_open();
    trunc.close();
    if (was_open) open_append(path);
    return ok;
}

size_t LQAHistoryStore::size() const { return samples_.size(); }

} // namespace ale
