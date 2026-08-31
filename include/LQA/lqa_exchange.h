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

    // Discard any incomplete inbound CMD 'a' / Block C5 report state. Called at
    // the start of every new handshake, every new outgoing call, and on
    // entering LINKED (on_handshake_start() covers the responder role;
    // ALEController also calls this directly on entering CALLING for the
    // caller role, which never goes through HANDSHAKE — and again on
    // entering LINKED, since the RX path that feeds report_decoder_ has no
    // state gate and keeps running through the whole linked session).
    // Without this, an interrupted Block C5 report (dropped/corrupted DATA
    // word, e.g. on a marginal Test-Channel sweep channel) leaves
    // report_decoder_ stuck active_=true, and every later unrelated DATA
    // word received (in ANY subsequent call, including AMD during LINKED)
    // gets spliced into the stale buffer and eventually decoded as bogus
    // LQA data.
    void reset();

    // Encode outgoing CMD 'a': read DB for (freq_hz, target), encode, queue via
    // sm_queue_cmd_a. request_report=true sets KA1 and records call context so
    // on_call_concluded can call mark_bilateral_attempted on failure.
    void encode_outgoing(uint32_t freq_hz, const std::string& target, bool request_report);

    // Build the raw CMD 'a' word directly and RETURN it, for a hand-built frame
    // that bypasses the SM's own call/response builders (e.g. the LINKED-state
    // AMD delivery-confirmation Response frame, which ALEController assembles at
    // the controller level). Unlike encode_outgoing(), this neither queues via a
    // callback nor touches the sent_ka1_/last_call_target_ bookkeeping (that is
    // specific to the normal call-cycle bilateral-exchange lifecycle, not this
    // one-off Response). request_report=false (the default) does NOT set KA1 —
    // the confirmation Response carries the CMD LQA word itself, not a Block C5
    // report request.
    uint32_t build_cmd_a_word(uint32_t freq_hz, const std::string& target,
                              bool request_report = false) const {
        LQACmdPayload p = build_payload(freq_hz, target);
        p.ka1           = request_report;
        return encode_lqa_cmd(p);   // free fn, LQA/lqa_metrics.h
    }

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

    // Block C5 reports carry one word per ~1.7 entries (36 bits/entry, 21
    // bits/word) plus a header word, inserted into a handshake response frame
    // that shares the modem's MAX_TX_SEQUENCE_WORDS=64 budget with the TO/TIS
    // address words and CMD 'a'. get_entries_for_station() returns every entry
    // on file for the peer with no cap, so a long-lived or heavily-tested peer
    // (Test-Channel's per-channel sweep against one target is the prime case)
    // can otherwise grow a report past that budget and overflow the TX queue.
    //
    // The tighter governing bound is the SPEC's message-section limit, not the
    // modem queue: Tm max basic = 30×Trw = 11.76 s (A.5.8.4). The response's
    // message section is CMD 'a' + CMD 'r' + DATA, and the caller per A.5.5.3.3
    // only waits Tlc + Tm max for the conclusion. 16 entries → 1 + 1 +
    // ceil(16×36/21) = 30 words = exactly Tm max basic; the previous 20-entry
    // cap (36 words ≈ 14.1 s) exceeded it — issue #5's responder-side share
    // (the caller's fixed listen budget, fixed separately in
    // ALEStateMachine::check_link_timeout, had no term for any of this).
    static constexpr size_t kMaxReportEntries = 16;
    static_assert(1u + 1u + (kMaxReportEntries * 36u + 20u) / 21u <= 30u,
                  "response message section (CMD 'a' + CMD 'r' + DATA) must stay "
                  "within Tm max basic = 30 words (A.5.8.4)");

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
