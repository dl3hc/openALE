/**
 * \file Protocol/Control/ale_call_processor.cpp
 * \brief ALE received-word processing — classification, per-state reactions,
 *        LQA update, and frame-assembly driving.  Moved out of ALEStateMachine
 *        so the SM does only states + transitions (+ time evolution / TX).
 *
 * Stateless: all state lives in the SM (MessageAssembler, lqa_metrics_,
 * frame_assembled_cb_, and the receive-related fields).  These static friend
 * methods receive the SM by reference and operate on its state directly.
 */

#include "Protocol/Control/ale_call_processor.h"
#include "Protocol/Control/ale_timing.h"
#include "FEC/golay.h"            // MAX_GOLAY_ERRORS
#include <algorithm>
#include <string>

namespace ale {

// Forward protocol-level debug events to the SM's injected trace callback
// (ALECallProcessor is a friend of ALEStateMachine, so it can reach trace_cb_).
// Zero overhead when no callback is set.
#define CP_TRACE(sm, msg) do { if ((sm).trace_cb_) (sm).trace_cb_(msg); } while (0)

// ── AllCall address recognition (A.5.5.4.4) — verbatim from ale_word_decoder.cpp ──
bool ALECallProcessor::is_allcall_address_(const std::string& addr, const std::string& self)
{
    if (addr.size() < 2 || addr[0] != '@' || addr[1] == '@') return false;
    if (addr[1] == '?') return true;                       // global AllCall
    return !self.empty() && self.back() == addr[1];        // selective AllCall
}

// ── Classification — verbatim logic from ALEWordDecoder::decode() + the
//    WordDecodeContext assembly that ALEStateMachine::process_received_word did
//    inline.  Reads the SM's current state/phase + self address.
ALECallProcessor::WordRole ALECallProcessor::classify(const ALEStateMachine& sm, const ALEWord& word)
{
    WordRole r;
    const std::string addr = trim_ale_address(word.address);

    const bool lbt_active =
        (sm.current_state == ALEState::HANDSHAKE && sm.handshake_phase == HandshakePhase::CHANNEL_CHECK)
        || (sm.current_state == ALEState::SOUNDING && sm.sounding_phase_ == SoundingPhase::LBT);

    // Every valid word during LBT = channel busy.
    if (lbt_active) { r.type = WordRole::CHANNEL_BUSY; return r; }

    // Collecting/expected_caller are STATE-SCOPED (see the note that was in the
    // SM's process_received_word): ORing them across states leaks stale flags.
    const bool collecting =
        (sm.current_state == ALEState::CALLING   && sm.collecting_remote_conclusion)
        || (sm.current_state == ALEState::HANDSHAKE && (sm.hs_conclusion_rcvd || sm.hs_ack_tis_rcvd));

    // DATA/REP after TIS → multi-part address of the peer.
    if (collecting && (word.type == PreambleType::DATA || word.type == PreambleType::REP)) {
        r.type = WordRole::DATA_EXTENSION;
        r.address = addr;
        return r;
    }

    const std::string self = sm.address_book.get_self_address();

    // AllCall (A.5.5.4.4): TO to the AllCall wildcard.  One-way broadcast — the
    // receiver does NOT respond; it freezes and collects the conclusion.  Here we
    // only recognize it (selective pertinence needs self_address) and pass it to
    // the SM; the SM handles the freeze + conclusion.
    if (word.type == PreambleType::TO && is_allcall_address_(addr, self)) {
        r.type = WordRole::ALLCALL;
        r.address = addr;
        return r;
    }

    // TO or TWAS to us → call to our own address.  Per A.5.2.5.1 the scanning TO
    // word carries only the first ≤3 chars of the destination; self_address may be
    // longer → prefix comparison.
    if ((word.type == PreambleType::TO || word.type == PreambleType::TWAS)
        && !addr.empty()
        && self.size() >= addr.size()
        && self.compare(0, addr.size(), addr) == 0) {
        r.type = WordRole::TO_SELF;
        r.address = addr;
        return r;
    }

    // TIS → conclusion begin; check expected_caller if set.
    if (word.type == PreambleType::TIS) {
        // expected_caller locks the called station onto the calling peer during its
        // HANDSHAKE only; during CALLING a stale caller_address must not gate the
        // responder's TIS.
        const std::string expected = (sm.current_state == ALEState::HANDSHAKE)
            ? sm.caller_address.substr(0, 3) : std::string();
        if (expected.empty() || addr == expected) {
            r.type = WordRole::TIS_CALLER;
            r.address = addr;
        }
        return r;
    }

    // TWAS not to us — meaning depends on SM state: rejection (CALLING),
    // conclusion (SCANNING AllCall), termination (LINKED).
    if (word.type == PreambleType::TWAS) {
        r.type = WordRole::TWAS_WORD;
        r.address = addr;
        return r;
    }

    return r;  // NONE
}

// ── on_invalid_word_ — verbatim from ALEStateMachine::handle_invalid_word() ──
void ALECallProcessor::on_invalid_word_(ALEStateMachine& sm)
{
    // Any signal (even invalid) during SOUNDING LBT = channel busy → abort (REQ-CHAN-031)
    if (sm.current_state == ALEState::SOUNDING && sm.sounding_phase_ == SoundingPhase::LBT) {
        CP_TRACE(sm, "[TRACE] handle_invalid_word: signal during SOUNDING LBT → SOUNDING_COMPLETE\n");
        sm.process_event(ALEEvent::SOUNDING_COMPLETE);
        return;
    }
    if (sm.current_state != ALEState::HANDSHAKE) return;
    if (sm.handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
        if (++sm.contiguous_errors > ALETimingConstants::MAX_SCANNING_CALL_ERRORS) {
            CP_TRACE(sm, "[TRACE] handle_invalid_word: "
                     + std::to_string(+sm.contiguous_errors)
                     + " contiguous errors → LINK_TIMEOUT\n");
            sm.process_event(ALEEvent::LINK_TIMEOUT);
        }
    } else if (sm.handshake_phase == HandshakePhase::CHANNEL_CHECK) {
        // Invalid signal on channel during LBT → busy → abort (AC-LINK-019-3)
        CP_TRACE(sm, "[TRACE] handle_invalid_word: invalid word during LBT → LINK_TIMEOUT\n");
        sm.process_event(ALEEvent::LINK_TIMEOUT);
    }
}

// ── detect_incoming_call_ — verbatim from SM::detect_incoming_call() ──
void ALECallProcessor::detect_incoming_call_(ALEStateMachine& sm, const WordRole& r)
{
    if (r.type == WordRole::TO_SELF) {
        sm.active_call_to = r.address;
        sm.process_event(ALEEvent::CALL_DETECTED);
        return;
    }
    if (r.type == WordRole::ALLCALL) {
        sm.allcall_silent_ = true;
        sm.process_event(ALEEvent::CALL_DETECTED);
    }
}

void ALECallProcessor::react_idle_(ALEStateMachine& sm, const WordRole& r)
{
    detect_incoming_call_(sm, r);
}

void ALECallProcessor::react_scanning_(ALEStateMachine& sm, const WordRole& r)
{
    detect_incoming_call_(sm, r);
    if (sm.current_state != ALEState::SCANNING) return;   // transitioned to HANDSHAKE
    // NOTE: AllCall reception is NOT a scanning sub-state.  An AllCall word triggers
    // CALL_DETECTED above → SCANNING exits to HANDSHAKE (allcall_silent_), which
    // receives the broadcast and its TIS/TWAS conclusion and returns to SCANNING.
    // The scanner therefore has zero AllCall coupling — any non-call word below is
    // just foreign traffic handled by the generic A.5.3.1 dwell freeze.

    // A.5.3.1: any valid word on this channel means ALE traffic is in progress.
    // Freeze the dwell timer so the scanner stays long enough to receive the full
    // frame (including TIS/TWAS conclusion and DATA address extension words)
    // before deciding the traffic is not for us.  scan_pause_settle_ms_ is refreshed
    // on every word; handle_scanning() hops once Tdrw silence elapses.
    sm.scanning_phase_    = ScanningPhase::SCAN_PAUSE;
    sm.scan_pause_settle_ms_ = sm.current_time_ms;
}

void ALECallProcessor::react_calling_(ALEStateMachine& sm, const WordRole& r)
{
    if (sm.calling_phase != CallingPhase::LISTENING) return;

    switch (r.type) {
    case WordRole::TO_SELF:
        if (!sm.response_to_detected) {
            sm.response_to_detected = true;
            sm.response_rx_start_ms = sm.current_time_ms;
        }
        break;
    case WordRole::TIS_CALLER:
        if (sm.response_to_detected && sm.tlww_start_ms == 0) {
            sm.to_address                   = r.address;
            sm.active_call_from             = r.address;
            sm.tlww_start_ms                = sm.current_time_ms;
            sm.collecting_remote_conclusion = true;
        }
        break;
    case WordRole::DATA_EXTENSION:
        // Only extend once the responder's TIS has started the conclusion;
        // otherwise a stray DATA before TIS would pollute to_address.
        if (sm.collecting_remote_conclusion) {
            sm.to_address      += r.address;
            sm.active_call_from = sm.to_address;
            sm.tlww_start_ms    = sm.current_time_ms;
        }
        break;
    case WordRole::TWAS_WORD:  // TWAS rejection from called station (AC-LINK-019-10)
        if (sm.operator_callback)
            sm.operator_callback(OperatorEvent::CALL_REJECTED);
        sm.process_event(ALEEvent::LINK_TIMEOUT);
        break;
    default:
        break;
    }
}

void ALECallProcessor::react_handshake_(ALEStateMachine& sm, const WordRole& r, const ALEWord& word)
{
    if (r.type == WordRole::CHANNEL_BUSY) {
        CP_TRACE(sm, "[TRACE] react_handshake: channel busy during LBT → LINK_TIMEOUT\n");
        sm.process_event(ALEEvent::LINK_TIMEOUT);
        return;
    }

    if (sm.handshake_phase == HandshakePhase::WAIT_CYCLE_END) {
        switch (r.type) {
        case WordRole::TIS_CALLER:
            if (!sm.hs_conclusion_rcvd) {
                sm.caller_address     = r.address;
                sm.active_call_from   = r.address;
                sm.hs_conclusion_rcvd = true;
                sm.hs_tlww_start_ms   = sm.current_time_ms;
            }
            break;
        case WordRole::DATA_EXTENSION:
            sm.caller_address  += r.address;
            sm.active_call_from = sm.caller_address;
            sm.hs_tlww_start_ms = sm.current_time_ms;
            break;
        case WordRole::TWAS_WORD:  // TWAS during calling cycle → abort (A.5.5.3.2)
            sm.process_event(ALEEvent::LINK_TIMEOUT);
            break;
        case WordRole::NONE:
            // DATA/REP before TIS → message section has begun; arm Tmmax (AC-LINK-018-5).
            if (!sm.hs_conclusion_rcvd
                && (word.type == PreambleType::DATA || word.type == PreambleType::REP)
                && sm.hs_message_start_ms == 0) {
                sm.hs_message_start_ms = sm.current_time_ms;
            }
            break;
        default:
            break;
        }
    } else if (sm.handshake_phase == HandshakePhase::WAIT_ACK) {
        switch (r.type) {
        case WordRole::TO_SELF:
            // "TO JOE" — start of SAM's ACK frame (A.5.5.3.4).  Records the arrival
            // so handle_handshake() can switch from the narrow Twr start-window to
            // the frame-limited conclusion wait.
            if (sm.hs_ack_to_ms == 0)
                sm.hs_ack_to_ms = sm.current_time_ms;
            break;
        case WordRole::TIS_CALLER:
            if (!sm.hs_ack_tis_rcvd) {
                sm.hs_ack_tis_rcvd  = true;
                sm.hs_tlww_start_ms = sm.current_time_ms;
            }
            break;
        case WordRole::DATA_EXTENSION:
            // A multi-word remote address puts DATA[ext] words BEFORE TO[addr]
            // in the ACK frame (A.5.2.4.1 transmission order).  The DATA word
            // arrives within the Twr narrow window; TO_SELF arrives ~Trw later,
            // often past the 2091 ms absolute limit.  Arm hs_ack_to_ms here so
            // handle_handshake() switches from the absolute-start window to the
            // silence-based frame-limit check (sub-phase 2), which completes
            // correctly once TIS arrives.  A spurious DATA during WAIT_ACK is
            // harmless: sub-phase 2 aborts on 5×Trw silence if TIS never comes.
            if (sm.hs_ack_to_ms == 0)
                sm.hs_ack_to_ms = sm.current_time_ms;
            sm.hs_tlww_start_ms = sm.current_time_ms;
            break;
        case WordRole::TWAS_WORD:  // TWAS instead of ACK → abort
            sm.process_event(ALEEvent::LINK_TIMEOUT);
            break;
        default:
            break;
        }
    }
}

// ── update_lqa — verbatim from ALEStateMachine::update_link_quality() ──
void ALECallProcessor::update_lqa(ALEStateMachine& sm, const LinkQuality& lq)
{
    // Forward to LQAMetrics only when receiving from a settled, known remote station.
    if (sm.lqa_metrics_
            && !sm.active_call_from.empty()
            && (sm.current_state == ALEState::HANDSHAKE
                || sm.current_state == ALEState::LINKED)) {
        MetricsSample sample;
        sample.snr_db               = lq.snr_db;
        sample.sinad_db             = lq.sinad_db;
        sample.non_unanimous_count  = lq.non_unanimous_count;
        sample.golay_uncorrectable  = lq.golay_uncorrectable;
        sample.fec_errors_corrected = static_cast<int>(lq.fec_errors);
        sample.decode_success       = (lq.fec_errors <= MAX_GOLAY_ERRORS);
        sample.timestamp_ms         = lq.timestamp_ms;
        const Channel* ch = sm.channel_manager_.current();
        sm.lqa_metrics_->add_sample(sample,
                                  ch ? ch->rx_frequency_hz : 0u,
                                  sm.active_call_from);
    }

    // Route heuristic LQA score update through channel manager (Schritt 6).
    float score = LQA_QUALITY_MAX - (static_cast<float>(lq.fec_errors) * 3.0f);
    score = std::max(LQA_QUALITY_MIN, std::min(LQA_QUALITY_MAX, score));
    sm.channel_manager_.update_lqa_score(sm.channel_manager_.current_index(), score);
}

// ── process_received_word — verbatim flow from ALEStateMachine::process_received_word() ──
void ALECallProcessor::process_received_word(ALEStateMachine& sm, const ALEWord& word)
{
    if (!word.valid) { on_invalid_word_(sm); return; }
    sm.contiguous_errors = 0;
    sm.last_word_time_ms = sm.current_time_ms;

    LinkQuality lq;
    lq.fec_errors          = word.fec_errors;
    lq.total_words         = 1;
    lq.timestamp_ms        = sm.current_time_ms;
    lq.sinad_db            = word.sinad_db;
    lq.snr_db              = (word.unanimous_votes / 48.0f) * 31.0f;
    lq.golay_uncorrectable = word.golay_uncorrectable;
    lq.non_unanimous_count = word.golay_uncorrectable
        ? 48u
        : (word.unanimous_votes <= 48u
            ? static_cast<uint8_t>(48u - word.unanimous_votes) : 0u);
    update_lqa(sm, lq);

    const WordRole r = classify(sm, word);

    switch (sm.current_state) {
        case ALEState::IDLE:      react_idle_(sm, r);            break;
        case ALEState::SCANNING:  react_scanning_(sm, r);        break;
        case ALEState::CALLING:   react_calling_(sm, r);         break;
        case ALEState::HANDSHAKE: react_handshake_(sm, r, word); break;
        case ALEState::LINKED:
            // T-03: TWAS termination from peer → end link immediately (A.5.5.3.5)
            if (r.type == WordRole::TWAS_WORD)
                sm.process_event(ALEEvent::LINK_TERMINATED);
            break;
        case ALEState::SOUNDING:
            if (r.type == WordRole::CHANNEL_BUSY) {
                // Valid word during LBT → channel busy → abort (AC-SOUND-001-001, REQ-CHAN-031)
                CP_TRACE(sm, "[TRACE] react_sounding: channel busy during LBT → SOUNDING_COMPLETE\n");
                sm.process_event(ALEEvent::SOUNDING_COMPLETE);
            } else if (sm.sounding_phase_ == SoundingPhase::LISTENING) {
                // T-08: im LISTENING-Fenster kann eine Station sofort zurückrufen (A.5.3.4)
                detect_incoming_call_(sm, r);  // TO_SELF → CALL_DETECTED → HANDSHAKE
            }
            break;
        default: break;
    }

    if (sm.message_assembler.add_word(word) && sm.frame_assembled_cb_) {
        ALEMessage assembled;
        if (sm.message_assembler.get_message(assembled))
            sm.frame_assembled_cb_(assembled);
    }
}

} // namespace ale