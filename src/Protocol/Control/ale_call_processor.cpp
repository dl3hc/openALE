/**
 * \file Protocol/Control/ale_call_processor.cpp
 * \brief ALE received-word processing: classification, per-state reactions,
 *        LQA update, frame-assembly driving. Split out of ALEStateMachine so
 *        the SM does only states + transitions (+ time evolution / TX).
 *
 * Stateless: all state lives in the SM (MessageAssembler, lqa_metrics_,
 * frame_assembled_cb_, receive-related fields). Static friend methods take
 * the SM by reference and operate on its state directly.
 */

#include "Protocol/Control/ale_call_processor.h"
#include "Protocol/Control/ale_timing.h"
#include "FEC/golay.h"            // MAX_GOLAY_ERRORS
#include "PAL/logger.h"
#include <algorithm>
#include <string>

namespace ale {

// Forwards protocol-level debug events to the SM's injected trace callback
// (ALECallProcessor is a friend of ALEStateMachine, reaches trace_cb_ directly).
// Zero overhead when no callback set.
#define CP_TRACE(sm, msg) do { if ((sm).trace_cb_) (sm).trace_cb_(msg); } while (0)

// ── AllCall address recognition (A.5.5.4.4) — from ale_word_decoder.cpp ──
bool ALECallProcessor::is_allcall_address_(const std::string& addr, const std::string& self)
{
    if (addr.size() < 2 || addr[0] != '@' || addr[1] == '@') return false;
    if (addr[1] == '?') return true;                       // global AllCall
    return !self.empty() && self.back() == addr[1];        // selective AllCall
}

// ── Classification — logic from ALEWordDecoder::decode() + the WordDecodeContext
//    assembly ALEStateMachine::process_received_word used to do inline.
//    Reads the SM's current state/phase + self address.
ALECallProcessor::WordRole ALECallProcessor::classify(const ALEStateMachine& sm, const ALEWord& word)
{
    WordRole r;
    const std::string addr = trim_ale_address(word.address);

    const bool lbt_active =
        (sm.current_state == ALEState::HANDSHAKE && sm.handshake_phase == HandshakePhase::CHANNEL_CHECK)
        || (sm.current_state == ALEState::SOUNDING && sm.sounding_phase_ == SoundingPhase::LBT);

    // Every valid word during LBT = channel busy.
    if (lbt_active) { r.type = WordRole::CHANNEL_BUSY; return r; }

    // OFS Phase 3c: CALLING's "still collecting the responder's conclusion"
    // question is exactly the reassembler's own parse-position fact (FR-03) —
    // it has already completed a conclusion run in the current candidate
    // (candidate()->complete) and the word classify() is looking at right
    // now was accepted extending it (last_role()==ADDRESS_EXTENSION, since
    // the reassembler is fed this same word first, see process_received_word
    // below). Replaces collecting_remote_conclusion, which is now gone —
    // classify() no longer needs a second bookkeeping flag for a fact the
    // reassembler already tracks.
    const bool reassembler_extends_conclusion =
        sm.frame_reassembler_.candidate()
        && sm.frame_reassembler_.candidate()->complete
        && sm.frame_reassembler_.last_role() == ParseRole::ADDRESS_EXTENSION;

    // collecting/expected_caller are STATE-SCOPED (see former note in SM's
    // process_received_word): ORing across states leaks stale flags.
    // HANDSHAKE keeps its own flags, not the reassembler predicate above:
    // hs_conclusion_rcvd/hs_ack_tis_rcvd are public API
    // (ALEStateMachine::is_hs_conclusion_rcvd(), consumed by
    // App/ale_controller.cpp) with pinned cross-phase persistence semantics
    // unrelated to parse position (test_ale_calling.cpp: hs_conclusion_rcvd
    // deliberately STAYS true after a HANDSHAKE abort, cleared only on the
    // next HANDSHAKE entry — the reassembler's candidate has no such
    // memory). See docs/FRAMING_STANDARD.md §10 "intentionally remaining".
    // LINKED has no clause here: TWAS termination-frame accumulation moved to
    // the FrameReassembler (fed independently below, OFS Phase 3b) — LINKED's
    // DATA/REP words classify NONE and are simply not acted on by classify()'s
    // caller (ALEStateMachine::handle_completed_frame_() reads the
    // reassembler's own address run at the frame boundary instead).
    const bool collecting =
        (sm.current_state == ALEState::CALLING   && reassembler_extends_conclusion)
        || (sm.current_state == ALEState::HANDSHAKE && (sm.hs_conclusion_rcvd || sm.hs_ack_tis_rcvd));

    // DATA/REP after TIS → multi-part address of the peer.
    if (collecting && (word.type == PreambleType::DATA || word.type == PreambleType::REP)) {
        r.type = WordRole::DATA_EXTENSION;
        r.address = addr;
        return r;
    }

    const std::string self = sm.address_book.get_self_address();

    // AllCall (A.5.5.4.4): TO to AllCall wildcard. One-way broadcast — receiver
    // does NOT respond, freezes and collects the conclusion. Only recognized here
    // (selective pertinence needs self_address); SM handles freeze + conclusion.
    if (word.type == PreambleType::TO && is_allcall_address_(addr, self)) {
        r.type = WordRole::ALLCALL;
        r.address = addr;
        return r;
    }

    // TO to us → call to our own address. Per A.5.2.5.1 scanning TO word carries
    // only first ≤3 chars of destination; self_address may be longer → prefix
    // comparison. TWAS deliberately excluded: per A.5.2.3.1.3 a TWAS word's
    // address is always the identity of whoever is currently transmitting, never
    // a callee reference — matching it against self would misclassify a far
    // station's conclusion as TO_SELF whenever its callsign prefix-matches ours.
    if (word.type == PreambleType::TO
        && !addr.empty()
        && self.size() >= addr.size()
        && self.compare(0, addr.size(), addr) == 0) {
        r.type = WordRole::TO_SELF;
        r.address = addr;
        return r;
    }

    // TIS → conclusion begin; check expected_caller if set.
    if (word.type == PreambleType::TIS) {
        // expected_caller locks the called station onto the calling peer during
        // HANDSHAKE only; during CALLING a stale caller_address must not gate TIS.
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
    // NOTE: AllCall reception is NOT a scanning sub-state. AllCall word triggers
    // CALL_DETECTED above → SCANNING exits to HANDSHAKE (allcall_silent_), which
    // receives the broadcast + TIS/TWAS conclusion and returns to SCANNING. So the
    // scanner has zero AllCall coupling; any non-call word below is just foreign
    // traffic handled by the generic A.5.3.1 dwell freeze.

    // A.5.3.1: any valid word on channel means ALE traffic in progress. Freeze the
    // dwell timer so scanner stays long enough to receive the full frame (incl.
    // TIS/TWAS conclusion and DATA address extension words) before deciding traffic
    // isn't for us. scan_pause_settle_ms_ refreshed every word; handle_scanning()
    // hops once Tdrw silence elapses.
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
            sm.to_address        = r.address;
            sm.active_call_from  = r.address;
            sm.tlww_start_ms     = sm.current_time_ms;
        }
        break;
    case WordRole::DATA_EXTENSION:
        // classify() only emits DATA_EXTENSION here once the reassembler has
        // an open conclusion run (see reassembler_extends_conclusion above),
        // so this is always a legitimate extension — no separate gate needed.
        sm.to_address      += r.address;
        sm.active_call_from = sm.to_address;
        sm.tlww_start_ms    = sm.current_time_ms;
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
        case WordRole::TWAS_WORD:
            // TWAS concluding the calling cycle (A.5.5.3.2): for AllCall/wildcard
            // addresses this is the spec-normal "no response" outcome (A.5.5.4.4:
            // "Calls to wildcard addresses that conclude with TWAS shall be
            // processed identically to the AllCall protocol"), not an error.
            // Capture identity exactly like TIS_CALLER so a multi-word address
            // settles via DATA_EXTENSION (classify()'s `collecting` gate keys off
            // hs_conclusion_rcvd regardless of which branch set it);
            // handle_handshake()'s settle timer then aborts via
            // hs_conclusion_is_twas_ rather than linking — but only after
            // caller_address is fully known, so any AMD already reassembled by
            // rx_accumulate_call_amd() (e.g. ALE-GPR position report) is correctly
            // attributed/dispatched instead of dropped by on_sm_state_change()'s
            // caller.empty() guard.
            if (!sm.hs_conclusion_rcvd) {
                sm.caller_address         = r.address;
                sm.active_call_from       = r.address;
                sm.hs_conclusion_rcvd     = true;
                sm.hs_conclusion_is_twas_ = true;
                sm.hs_tlww_start_ms       = sm.current_time_ms;
            }
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
            // "TO JOE" — start of SAM's ACK frame (A.5.5.3.4). Records arrival so
            // handle_handshake() can switch from narrow Twr start-window to
            // frame-limited conclusion wait.
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
            // Multi-word remote address puts DATA[ext] words BEFORE TO[addr] in the
            // ACK frame (A.5.2.4.1 order). DATA arrives within the Twr narrow
            // window; TO_SELF arrives ~Trw later, often past the 2091ms absolute
            // limit. Arm hs_ack_to_ms here so handle_handshake() switches from the
            // absolute-start window to the silence-based frame-limit check
            // (sub-phase 2), completing correctly once TIS arrives. Spurious DATA
            // during WAIT_ACK is harmless: sub-phase 2 aborts on 5×Trw silence if
            // TIS never comes.
            if (sm.hs_ack_to_ms == 0)
                sm.hs_ack_to_ms = sm.current_time_ms;
            sm.hs_tlww_start_ms = sm.current_time_ms;
            break;
        case WordRole::TWAS_WORD:
            // TWAS instead of TIS as frame 3's conclusion, Ion2G-style: caller sent
            // an AMD (already delivered via rx_accumulate_call_amd()) and declined
            // to link (link_after_send=false on their side). Graceful outcome, not
            // a failure — a plain call always concludes frame 3 with TIS, so
            // caller-side TWAS-as-frame-3 only happens here.
            if (sm.operator_callback)
                sm.operator_callback(OperatorEvent::AMD_RECEIVED_NO_LINK);
            sm.process_event(ALEEvent::AMD_DECLINED_LINK);
            break;
        default:
            break;
        }
    }
}

// ── LINKED-state AMD delivery confirmation RX detection ──────────────────
// Mirrors react_calling_ (LISTENING) for sender awaiting peer Response, and
// react_handshake_ WAIT_ACK for receiver awaiting sender ACK. Only sets timing
// flags the SM's handle_linked_amd_* drivers consume; never consumes/blocks the
// word (rx_accumulate_linked_amd etc. still see it).
void ALECallProcessor::react_linked_amd_confirm_(ALEStateMachine& sm, const WordRole& r)
{
    switch (sm.linked_amd_phase_) {
    case LinkedAmdPhase::LISTENING:              // sender: peer's Response frame
        switch (r.type) {
        case WordRole::TO_SELF:
            if (!sm.linked_amd_resp_detected_) sm.linked_amd_resp_detected_ = true;
            break;
        case WordRole::TIS_CALLER:
            if (sm.linked_amd_resp_detected_ && sm.linked_amd_resp_tlww_ms_ == 0)
                sm.linked_amd_resp_tlww_ms_ = sm.current_time_ms;
            break;
        case WordRole::DATA_EXTENSION:           // multi-word peer conclusion — re-arm settle
            if (sm.linked_amd_resp_tlww_ms_ != 0)
                sm.linked_amd_resp_tlww_ms_ = sm.current_time_ms;
            break;
        default: break;
        }
        break;
    case LinkedAmdPhase::WAIT_ACK:               // receiver: sender's ACK frame
        switch (r.type) {
        case WordRole::TO_SELF:
            if (!sm.linked_amd_ack_to_detected_) sm.linked_amd_ack_to_detected_ = true;
            break;
        case WordRole::TIS_CALLER:
            if (!sm.linked_amd_ack_tis_rcvd_) {
                sm.linked_amd_ack_tis_rcvd_ = true;
                sm.linked_amd_ack_tlww_ms_  = sm.current_time_ms;
            }
            break;
        case WordRole::DATA_EXTENSION:
            if (sm.linked_amd_ack_tis_rcvd_) sm.linked_amd_ack_tlww_ms_ = sm.current_time_ms;
            break;
        default: break;
        }
        break;
    default: break;                              // NONE / RESPONDING / SENDING_ACK — TX phases
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
    // Diagnostic (2026-08-07): unconditional per-word trace (valid AND invalid),
    // before any classification/state mutation — only point that answers "did the
    // demodulator hand the SM this word, and was it valid" for the open truncated
    // multi-word-address investigation (real-radio conclusion's DATA extension
    // word consistently absent from RX log, which only shows valid classified
    // words). Silent by default (LogLevel::TRACE, filtered unless min level lowered).
    if (word.valid) {
        pal::log_trace("RXWord", "%s [%s] fec_errors=%u sinad=%.1f votes=%u",
                        WordParser::word_type_name(word.type), word.address,
                        word.fec_errors, word.sinad_db, word.unanimous_votes);
    } else {
        pal::log_trace("RXWord", "INVALID golay_uncorrectable=%d fec_errors=%u",
                        word.golay_uncorrectable ? 1 : 0, word.fec_errors);
    }

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

    // OFS Phase 2 shadow feed: the FrameReassembler observes the same valid-
    // word stream classify() sees, at the same point — roles from parse
    // position (FR-01..05). Nothing consumes its output yet (Phase 3).
    sm.frame_reassembler_.on_word(word, sm.current_time_ms);

    const WordRole r = classify(sm, word);

    switch (sm.current_state) {
        case ALEState::IDLE:      react_idle_(sm, r);            break;
        case ALEState::SCANNING:  react_scanning_(sm, r);        break;
        case ALEState::CALLING:   react_calling_(sm, r);         break;
        case ALEState::HANDSHAKE: react_handshake_(sm, r, word); break;
        case ALEState::LINKED:
            // T-03 peer TWAS termination-frame recognition (A.5.5.3.5) moved to
            // the FrameReassembler + ALEStateMachine::handle_completed_frame_()
            // (OFS Phase 3b, docs/FRAMING_STANDARD.md §6 F-05) — the reassembler
            // accumulates the conclusion's full address independently of
            // classify(), so no per-word arming lives here any more.
            // AMD delivery-confirmation Response/ACK detection (no-op if idle).
            react_linked_amd_confirm_(sm, r);
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