/**
 * \file ale_channel_manager.cpp
 * \brief ALEChannelManager implementation
 */

#include "Protocol/Control/ale_channel_manager.h"
#include <algorithm>

namespace ale {

void ALEChannelManager::configure(const ScanConfig& config) {
    scan_ = config;
}

void ALEChannelManager::add_channel(const Channel& ch) {
    scan_.scan_list.push_back(ch);
}

const Channel* ALEChannelManager::current() const {
    if (scan_.scan_list.empty()) return nullptr;
    if (scan_.channel_index >= scan_.scan_list.size()) return nullptr;
    return &scan_.scan_list[scan_.channel_index];
}

const Channel* ALEChannelManager::select_best() const {
    if (scan_.scan_list.empty()) return nullptr;
    const Channel* best = &scan_.scan_list[0];
    for (const auto& ch : scan_.scan_list)
        if (ch.lqa_score > best->lqa_score)
            best = &ch;
    return best;
}

void ALEChannelManager::start(uint32_t current_time_ms) {
    last_hop_ms_ = current_time_ms;
    if (!scan_.scan_list.empty())
        apply(0, current_time_ms);
    else
        scan_.channel_index = 0;
}

bool ALEChannelManager::hop_next(uint32_t current_time_ms) {
    if (scan_.scan_list.empty()) return false;
    const uint32_t next = (scan_.channel_index + 1) % scan_.scan_list.size();
    apply(next, current_time_ms);
    last_hop_ms_ = current_time_ms;
    return true;
}

bool ALEChannelManager::check_dwell_timeout(uint32_t current_time_ms) const {
    return (current_time_ms - last_hop_ms_) >= scan_.dwell_time_ms;
}

void ALEChannelManager::update_lqa_score(uint32_t idx, float score) {
    if (idx < scan_.scan_list.size())
        scan_.scan_list[idx].lqa_score = score;
}

void ALEChannelManager::hop_calling(const Channel& ch) {
    if (on_change_)
        on_change_(ch);
}

void ALEChannelManager::set_channel_callback(std::function<void(const Channel&)> cb) {
    on_change_ = std::move(cb);
}

void ALEChannelManager::apply(uint32_t index, uint32_t current_time_ms) {
    if (index >= scan_.scan_list.size()) return;
    scan_.channel_index = index;
    scan_.scan_list[index].last_scan_time_ms = current_time_ms;
    if (on_change_)
        on_change_(scan_.scan_list[index]);
}

} // namespace ale
