/**
 * \file App/freq_select_manager.cpp
 */

#include "App/freq_select_manager.h"
#include "Protocol/Control/ale_freq_select.h"
#include "Protocol/Control/ale_timing.h"

namespace ale {

FreqSelectManager::FreqSelectManager(
    ALEStateMachine&                        sm,
    LQAAnalyzer&                            lqa,
    std::function<std::string()>            get_self_addr,
    std::function<bool(const std::string&)> is_self,
    std::function<void(const std::string&)> on_relink,
    std::function<void(const std::string&)> on_status)
    : sm_(sm)
    , lqa_(lqa)
    , get_self_addr_(std::move(get_self_addr))
    , is_self_(std::move(is_self))
    , on_relink_(std::move(on_relink))
    , on_status_(std::move(on_status))
{}

void FreqSelectManager::evaluate(uint32_t now_ms, uint32_t ber_settle_ms, float threshold) {
    if (ber_settle_ms == 0
        || (now_ms - ber_settle_ms) < ALETimingConstants::Tdrw_ms * 4u)
        return;
    if (cooldown_ms_ > 0 && now_ms < cooldown_ms_) return;

    const std::string peer = !sm_.get_to_address().empty()
        ? sm_.get_to_address() : sm_.get_caller_address();
    if (peer.empty() || is_self_(peer)) return;

    const Channel* ch = sm_.get_current_channel();
    if (!ch || ch->rx_frequency_hz == 0) return;

    const auto ranked = lqa_.rank_channels_for_station(peer);
    if (ranked.empty()) return;

    float cur_score = 0.0f;
    for (const auto& r : ranked)
        if (r.frequency_hz == ch->rx_frequency_hz) { cur_score = r.score; break; }

    const float    best_score = ranked.front().score;
    const uint32_t best_hz   = ranked.front().frequency_hz;

    if (best_hz != ch->rx_frequency_hz && best_score > cur_score + threshold) {
        peer_        = peer;
        proposed_hz_ = best_hz;
        phase_       = Phase::PROPOSED;
        timeout_ms_  = now_ms + 3000u;
        on_status_("EFS: proposing " + std::to_string(best_hz / 1000u) + " kHz to " + peer
                   + " (score " + std::to_string(static_cast<int>(best_score))
                   + " > cur " + std::to_string(static_cast<int>(cur_score)) + ")");
        send_orderwire(best_hz);
    }
}

void FreqSelectManager::tick(uint32_t now_ms) {
    if (phase_ == Phase::PROPOSED && now_ms > timeout_ms_) {
        phase_ = Phase::IDLE;
        on_status_("EFS: no response from peer — staying on current channel");
    }
}

void FreqSelectManager::on_word(const ALEWord& word, uint32_t now_ms, float threshold) {
    if (sm_.get_state() != ALEState::LINKED) return;

    if (word.type == PreambleType::CMD && cmd_char_code(word) == 'f') {
        pending_cmd_rx_ = true;
    } else if (pending_cmd_rx_) {
        pending_cmd_rx_ = false;
        if (word.type == PreambleType::DATA) {
            const uint32_t freq_hz = decode_freq_data_word(word.raw_payload);
            const std::string peer = !sm_.get_to_address().empty()
                ? sm_.get_to_address() : sm_.get_caller_address();
            if (phase_ == Phase::PROPOSED) {
                handle_response(freq_hz, now_ms);
            } else if (!peer.empty() && !is_self_(peer)) {
                handle_proposal(freq_hz, peer, now_ms, threshold);
            }
        }
        // Non-DATA word after CMD 'f': ignore (robustness)
    }
}

void FreqSelectManager::reset_pending_cmd() {
    pending_cmd_rx_ = false;
}

void FreqSelectManager::handle_response(uint32_t freq_hz, uint32_t now_ms) {
    if (phase_ != Phase::PROPOSED) return;
    if (freq_hz > 0 && freq_hz == proposed_hz_) {
        // Accept — signal controller to set pending_relink_addr_ and call terminate_link()
        phase_ = Phase::EXECUTING;
        on_status_("EFS: peer accepted " + std::to_string(freq_hz / 1000u) + " kHz — relinking");
        on_relink_(peer_);
        sm_.terminate_link();
    } else if (freq_hz == 0) {
        // Reject — stay on current channel, apply cooldown
        phase_       = Phase::IDLE;
        cooldown_ms_ = now_ms + 60000u;
        on_status_("EFS: rejected by peer — staying on current channel");
    }
    // Other freq_hz values: unexpected — ignore (peer may have sent own proposal after collision)
}

void FreqSelectManager::handle_proposal(uint32_t freq_hz, const std::string& peer,
                                        uint32_t now_ms, float threshold) {
    if (freq_hz == 0) return;
    const Channel* ch = sm_.get_current_channel();
    if (!ch || ch->rx_frequency_hz == 0 || peer.empty()) return;

    // Collision: both stations proposed simultaneously — lexicographically lower address defers
    if (phase_ == Phase::PROPOSED) {
        if (get_self_addr_() < peer_) {
            // We defer — discard our own proposal, evaluate peer's proposal instead
            phase_       = Phase::IDLE;
            proposed_hz_ = 0;
        } else {
            return;  // We take priority; peer should defer
        }
    }

    const auto ranked = lqa_.rank_channels_for_station(peer);
    float cur_score = 0.0f, proposed_score = 0.0f;
    for (const auto& r : ranked) {
        if (r.frequency_hz == ch->rx_frequency_hz) cur_score      = r.score;
        if (r.frequency_hz == freq_hz)             proposed_score = r.score;
    }

    if (proposed_score > cur_score + threshold) {
        on_status_("EFS: accepting proposal for " + std::to_string(freq_hz / 1000u) + " kHz");
        send_orderwire(freq_hz);  // echo-accept
    } else {
        on_status_("EFS: rejecting proposal for " + std::to_string(freq_hz / 1000u) + " kHz");
        send_orderwire(0u);       // reject
    }

    (void)now_ms;  // reserved for future cooldown on outbound rejections
}

void FreqSelectManager::send_orderwire(uint32_t freq_hz) {
    sm_.trigger_linked_orderwire(build_freq_select_sequence(freq_hz));
}

} // namespace ale
