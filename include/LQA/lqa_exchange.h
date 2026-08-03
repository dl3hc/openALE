/**
 * @file LQA/lqa_exchange.h
 * @brief Bilateral LQA exchange manager (MIL-STD-188-141B A.5.4.2 / Blocks A4–A6, C5).
 *
 * Owns the complete CMD 'a' bilateral exchange lifecycle:
 *   - Encode outgoing CMD 'a' (caller Block A3/A4, responder Block A4)
 *   - Decode incoming CMD 'a' (Block A5, both roles)
 *   - Generate and queue Block C5 LQA Report when peer set KA1=1
 *   - Track sent-KA1 call context for mark_bilateral_attempted on failure
 *   - Decode incoming Block C5 LQA Report (CMD 'r' + DATA)
 *
 * ALEController calls the three event entry points below; all encoding
 * detail and DB interaction is absorbed behind this seam.
 */

#pragma once

#include "LQA/lqa_metrics.h"
#include "LQA/lqa_report.h"
#include "Word/ale_sequence.h"
#include <functional>
#include <string>
#include <cstdint>

namespace ale {

class LqaExchangeManager {
public:
    /**
     * @param db              LQA database for all read/write operations
     * @param is_self         Returns true if the address is a local self-address
     * @param sm_queue_cmd_a  Queue a 24-bit CMD 'a' word in the state machine
     * @param sm_queue_report Queue a Block C5 sequence in the state machine
     */
    LqaExchangeManager(LQADatabase&                             db,
                       std::function<bool(const std::string&)>  is_self,
                       std::function<void(uint32_t)>            sm_queue_cmd_a,
                       std::function<void(ALESequence)>         sm_queue_report);

    // Reset pending bilateral state at the start of each new handshake (safety net).
    void on_handshake_start();

    // Encode outgoing CMD 'a': read DB for (freq_hz, target), encode, queue via
    // sm_queue_cmd_a. request_report=true sets KA1 and records call context so
    // on_call_concluded can call mark_bilateral_attempted on failure.
    void encode_outgoing(uint32_t freq_hz, const std::string& target, bool request_report);

    // Capture an incoming CMD 'a' word. Caller must have verified that the SM
    // state/phase is a valid bilateral-exchange window before calling.
    void on_word_lqa_cmd(uint32_t raw_payload, uint32_t freq_hz);

    // Start Block C5 LQA Report RX from a CMD 'r' header word's raw payload.
    void on_report_cmd(uint32_t raw_payload);

    // Feed a DATA word to the Block C5 decoder. Returns true when a complete report
    // was decoded; the entries are immediately written to DB.
    bool on_report_data(uint32_t raw_payload, const std::string& sender,
                         const std::function<void(const std::string&)>& emit);

    // Write pending bilateral payload to DB and optionally queue Block C5 if the
    // peer set KA1=1. Returns true when a bilateral payload was present.
    // can_queue_c5: true on the responder side (JOE); false on the caller side (SAM).
    bool apply_pending(const std::string& peer, bool can_queue_c5,
                        const std::function<void(const std::string&)>& emit);

    // Call after LINK_ESTABLISHED / CALL_REJECTED / NO_CHANNELS_LEFT.
    // Marks bilateral attempted when a KA1=true call was placed. Clears context.
    void on_call_concluded();

    // Exposes call target so ALEController can drive record_handshake_fail over
    // its full calling_channels_ list on NO_CHANNELS_LEFT.
    const std::string& call_target() const { return last_call_target_; }

private:
    LQACmdPayload build_payload(uint32_t freq_hz, const std::string& target) const;

    LQADatabase&                             db_;
    std::function<bool(const std::string&)>  is_self_;
    std::function<void(uint32_t)>            sm_queue_cmd_a_;
    std::function<void(ALESequence)>         sm_queue_report_;

    LQACmdPayload    pending_       = {};
    bool             pending_valid_ = false;
    uint32_t         pending_freq_  = 0;

    bool             sent_ka1_          = false;
    std::string      last_call_target_;
    uint32_t         last_call_freq_    = 0;

    LQAReportDecoder report_decoder_;
};

} // namespace ale
