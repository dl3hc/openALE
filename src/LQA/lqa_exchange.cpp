/**
 * @file LQA/lqa_exchange.cpp
 */

#include "LQA/lqa_exchange.h"
#include <algorithm>

namespace ale {

LqaExchangeManager::LqaExchangeManager(
        LQADatabase&                             db,
        std::function<bool(const std::string&)>  is_self,
        std::function<void(uint32_t)>            sm_queue_cmd_a,
        std::function<void(ALESequence)>         sm_queue_report)
    : db_(db)
    , is_self_(std::move(is_self))
    , sm_queue_cmd_a_(std::move(sm_queue_cmd_a))
    , sm_queue_report_(std::move(sm_queue_report))
{}

void LqaExchangeManager::on_handshake_start()
{
    reset();
}

void LqaExchangeManager::reset()
{
    pending_valid_ = false;
    report_decoder_.reset();
}

LQACmdPayload LqaExchangeManager::build_payload(uint32_t freq_hz,
                                                  const std::string& target) const
{
    LQACmdPayload p{};  // sentinels: sinad=31, ber=31, mp=7
    // A.5.4.2: report what WE measured FROM target; prefer station-specific
    // entry, fall back to channel aggregate if none yet.
    auto e = (!target.empty()) ? db_.get_entry(freq_hz, target) : nullptr;
    if (!e || e->total_words == 0)
        e = db_.get_entry(freq_hz, "");
    if (!e) return p;
    if (e->sinad_db > 0.0f) {
        const float s = e->sinad_db;
        p.sinad = (s >= 30.0f) ? 30u : static_cast<uint8_t>(s + 0.5f);
    }
    if (e->total_words > 0) {
        // ber = averaged non-unanimous vote count (0.0-48.0, A.5.4.1.1).
        // total_words>0 means real measurement: BER=0 valid (code 0=best), not
        // the 31 "no value" sentinel (A.5.4.2.1 / Table A-XIII).
        p.ber = ber_score_to_lqa_code(
            static_cast<uint8_t>(std::min(48.0f, std::max(0.0f, e->ber))));
    }
    if (e->multipath_score > 0.0f)
        p.mp = multipath_delay_to_lqa_code(e->multipath_score * 6.0f);
    return p;
}

void LqaExchangeManager::encode_outgoing(uint32_t freq_hz,
                                          const std::string& target,
                                          bool request_report)
{
    sent_ka1_ = false;
    last_call_target_.clear();
    last_call_freq_ = 0;

    LQACmdPayload p = build_payload(freq_hz, target);
    p.ka1           = request_report;
    sm_queue_cmd_a_(encode_lqa_cmd(p));

    if (request_report) {
        sent_ka1_         = true;
        last_call_target_ = target;
        last_call_freq_   = freq_hz;
    }
}

void LqaExchangeManager::on_word_lqa_cmd(uint32_t raw_payload, uint32_t freq_hz)
{
    pending_       = decode_lqa_cmd(raw_payload);
    pending_valid_ = true;
    pending_freq_  = freq_hz;
}

void LqaExchangeManager::on_report_cmd(uint32_t raw_payload)
{
    report_decoder_.start(raw_payload);
}

bool LqaExchangeManager::on_report_data(uint32_t raw_payload,
                                          const std::string& sender,
                                          const std::function<void(const std::string&)>& emit)
{
    if (!report_decoder_.active()) return false;
    if (!report_decoder_.feed(raw_payload)) return false;

    if (!sender.empty() && !is_self_(sender)) {
        for (const auto& r : report_decoder_.reports())
            db_.update_bilateral(r.frequency_hz, sender, r.sinad, r.ber, r.mp, 0u);
        emit("LQA Report RX from " + sender + ": "
             + std::to_string(report_decoder_.reports().size()) + " channels");
    }
    report_decoder_.reset();
    return true;
}

bool LqaExchangeManager::apply_pending(const std::string& peer,
                                        bool can_queue_c5,
                                        const std::function<void(const std::string&)>& emit)
{
    if (!pending_valid_) return false;

    const bool valid_peer = !peer.empty() && !is_self_(peer);
    if (valid_peer) {
        db_.update_bilateral(pending_freq_, peer,
                              pending_.sinad, pending_.ber, pending_.mp, 0u);
        emit("LQA bilateral RX: " + peer + " SINAD=" + std::to_string(pending_.sinad));

        if (can_queue_c5 && pending_.ka1) {
            const uint32_t now = db_.get_current_time_ms();
            auto entries = db_.get_entries_for_station(peer, 25.0f);
            if (!entries.empty()) {
                // Bound the report to the modem's TX word budget (see
                // kMaxReportEntries) regardless of how many channels this peer
                // has on file -- keep the highest-scoring (most useful) entries.
                if (entries.size() > kMaxReportEntries) {
                    std::partial_sort(entries.begin(),
                                       entries.begin() + static_cast<long>(kMaxReportEntries),
                                       entries.end(),
                                       [](const LQAEntry& a, const LQAEntry& b) {
                                           return a.score > b.score;
                                       });
                    entries.resize(kMaxReportEntries);
                }
                std::vector<LQAReport> reports;
                for (const auto& e : entries) {
                    LQAReport r;
                    r.frequency_hz = e.frequency_hz;
                    r.age   = lqa_age_code(e.last_contact_ms, now);
                    r.sinad = (e.sinad_db > 0.0f)
                        ? static_cast<uint8_t>(std::min(30.0f, e.sinad_db))
                        : kSinadLqaNoValue;
                    r.ber   = (e.total_words > 0)
                        ? ber_score_to_lqa_code(
                              static_cast<uint8_t>(
                                  std::min(48.0f, std::max(0.0f, e.ber))))
                        : kBerLqaNoValue;
                    r.mp    = multipath_delay_to_lqa_code(e.multipath_score * 6.0f);
                    reports.push_back(r);
                }
                sm_queue_report_(ALESequenceBuilder::lqa_report(reports));
                emit("LQA Report queued for " + peer + " ("
                     + std::to_string(reports.size()) + " channels)");
            }
        }
    }

    pending_valid_ = false;
    return true;
}

void LqaExchangeManager::on_call_concluded()
{
    if (sent_ka1_ && !last_call_target_.empty() && last_call_freq_ > 0)
        db_.mark_bilateral_attempted(last_call_freq_, last_call_target_);
    sent_ka1_ = false;
    last_call_target_.clear();
    last_call_freq_ = 0;
}

} // namespace ale
