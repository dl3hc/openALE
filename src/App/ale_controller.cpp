/**
 * \file App/ale_controller.cpp
 */

#include "App/ale_controller.h"
#include <cstdio>
#include <string>

namespace ale {

ALEController::ALEController()
{
    wire_callbacks();
}

void ALEController::wire_callbacks()
{
    // SM → Modem: TX word
    sm_.set_transmit_callback([this](const ALEWord& w) {
        modem_.enqueue_word(w);
    });

    // Modem done → SM: word-complete notification
    modem_.set_word_done_callback([this]() {
        sm_.on_word_complete();
    });

    // Modem PCM output → operator audio callback
    modem_.set_tx_callback([this](const int16_t* s, uint32_t n) {
        if (on_tx_audio) on_tx_audio(s, n);
    });

    // SM state transitions
    sm_.set_state_callback([this](ALEState from, ALEState to) {
        on_sm_state_change(from, to);
    });

    // SM operator-level events (LINK_ESTABLISHED, CALL_REJECTED, …)
    sm_.set_operator_callback([this](OperatorEvent ev) {
        on_operator_event(ev);
    });

    // SM protocol-level trace → status
    sm_.set_trace_callback([this](const std::string& msg) {
        emit_status(msg);
    });

    // RX pipeline word → SM
    rx_pipeline_.set_word_callback([this](const ALEWord& w) {
        on_received_word(w);
    });

    // SM RX-enable control → pipeline (SM disables RX during TX phases)
    sm_.set_rx_enabled_callback([this](bool on) {
        rx_pipeline_.set_enabled(on);
    });
}

// ── Configuration ─────────────────────────────────────────────────────────────

void ALEController::set_self_address(const std::string& addr)
{
    self_addr_ = addr;
    sm_.set_self_address(addr);
}

void ALEController::set_target_scan_channels(uint32_t n)
{
    sm_.set_target_scan_channels(n);
}

void ALEController::set_calling_channels(const std::vector<Channel>& channels)
{
    sm_.set_calling_channels(channels);
}

// ── Operator actions ──────────────────────────────────────────────────────────

void ALEController::start_listening()
{
    emit_status("Starting ALE scanner — listening for calls");
    sm_.process_event(ALEEvent::START_SCAN);
}

bool ALEController::initiate_call(const std::string& target_addr)
{
    emit_status("Initiating call to " + target_addr);
    return sm_.initiate_call(target_addr);
}

void ALEController::reject_call()
{
    emit_status("Rejecting incoming call (TWAS)");
    sm_.reject_call();
}

void ALEController::terminate_link()
{
    emit_status("Terminating link");
    sm_.process_event(ALEEvent::LINK_TERMINATED);
}

void ALEController::emergency_stop()
{
    emit_status("EMERGENCY STOP — aborting all ALE operations");
    sm_.emergency_manual_control();
}

// ── Main-loop drivers ─────────────────────────────────────────────────────────

void ALEController::update(uint32_t now_ms)
{
    sm_.update(now_ms);
    modem_.update(now_ms);
}

void ALEController::feed_audio(const int16_t* samples, uint32_t count)
{
    rx_pipeline_.push_samples(samples, count);
}

// ── Private callbacks ─────────────────────────────────────────────────────────

void ALEController::on_sm_state_change(ALEState from, ALEState to)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "State: %s → %s",
                  ALEStateMachine::state_name(from),
                  ALEStateMachine::state_name(to));
    emit_status(buf);

    // Reset caller tracking when leaving HANDSHAKE
    if (from == ALEState::HANDSHAKE)
        last_caller_.clear();

    // Link exited (except via HANDSHAKE_COMPLETE → LINKED)
    if (from == ALEState::LINKED && to != ALEState::LINKED) {
        if (on_link_terminated)
            on_link_terminated("Link state exited");
    }
}

void ALEController::on_operator_event(OperatorEvent ev)
{
    switch (ev) {
        case OperatorEvent::LINK_ESTABLISHED:
        {
            // SAM side: to_address holds the responding station.
            // JOE side: caller_address holds the calling station.
            const std::string& peer = !sm_.get_to_address().empty()
                ? sm_.get_to_address()
                : sm_.get_caller_address();
            emit_status("LINK ESTABLISHED with " + peer);
            if (on_link_established) on_link_established(peer);
            break;
        }
        case OperatorEvent::CALL_REJECTED:
            emit_status("Call rejected by remote station (TWAS)");
            if (on_link_terminated) on_link_terminated("Call rejected");
            break;
        case OperatorEvent::NO_CHANNELS_LEFT:
            emit_status("No reply — all calling channels exhausted");
            if (on_link_terminated) on_link_terminated("No reply");
            break;
        case OperatorEvent::EMERGENCY_ACTIVE:
            emit_status("Emergency manual control is now active");
            break;
    }
}

void ALEController::on_received_word(const ALEWord& word)
{
    // Capture caller identity as it arrives word-by-word in HANDSHAKE/WAIT_CYCLE_END.
    //
    // Protocol (A.5.2.3.2.1):  TIS:XXX [DATA:YYY [REP:ZZZ [DATA:... [REP:...]]]]
    //   - TIS = anchor word (first 3 chars, possibly @-padded)
    //   - DATA/REP alternates for chars 4-6, 7-9, 10-12, 13-15
    //   - Trailing '@' stuffing is stripped by trim_ale_address()
    //
    // on_call_received fires exactly once — when TIS arrives with the first 3 chars.
    // Subsequent DATA/REP words extend last_caller_ without a second callback.
    //
    // This mirrors what ALEWordDecoder + ALEStateMachine do internally, ensuring
    // our display string matches sm_.get_caller_address() at end of frame.
    if (sm_.get_state() == ALEState::HANDSHAKE
        && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END
        && word.valid)
    {
        const std::string chunk = trim_ale_address(word.address);

        if (word.type == PreambleType::TIS && last_caller_.empty()) {
            last_caller_ = chunk;
            if (on_call_received) on_call_received(last_caller_);
        } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                   && !last_caller_.empty()) {
            last_caller_ += chunk;
        }
    }

    sm_.process_received_word(word);
}

void ALEController::emit_status(const std::string& msg)
{
    if (on_status_changed) on_status_changed(msg);
}

} // namespace ale
