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
    // SM → Modem: enqueue word for TX, and arm one frame-completion notification.
    // The audio thread pulls symbol frames autonomously via the registered source;
    // it fires frame completion through AudioDevice::arm_frame_complete → tick().
    // In offline mode (no audio device) update() drains pending frames directly.
    sm_.set_transmit_callback([this](const ALEWord& w) {
        modulator_.enqueue_word(w);
        if (audio_device_)
            audio_device_->arm_frame_complete([this]() { sm_.on_word_complete(); });
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
    demodulator_.set_word_callback([this](const ALEWord& w) {
        on_received_word(w);
    });

    // SM RX-enable control → pipeline (SM disables RX during TX phases)
    // PTT transitions mirror RX enable: RX off = TX active = PTT on.
    sm_.set_rx_enabled_callback([this](bool rx_on) {
        demodulator_.set_enabled(rx_on);
        emit_event(rx_on ? pal::EventType::PTT_OFF : pal::EventType::PTT_ON);
    });
}

// ── Audio device ──────────────────────────────────────────────────────────────

void ALEController::set_audio_device(AudioDevice* dev)
{
    audio_device_ = dev;
    if (dev)
        dev->set_symbol_source([this](uint8_t* out) {
            return modulator_.pull_symbol_frame(out);
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

void ALEController::start_available()
{
    emit_status("Available — fixed channel, listening for incoming calls");
    // SM stays in IDLE; enable RX pipeline directly since enter_state(IDLE)
    // is not called at construction (only on re-entry via transition_to).
    demodulator_.set_enabled(true);
}

void ALEController::start_scanning()
{
    emit_status("Starting ALE scanner — channel hopping");
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
    sm_.terminate_link();  // T-07: sendet TO×2+TWAS, dann LINK_TERMINATED
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

    // Offline mode: no audio device, so drive word-completion directly by
    // pulling all pending symbol frames and firing on_word_complete per frame.
    if (!audio_device_) {
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modulator_.pull_symbol_frame(syms))
            sm_.on_word_complete();
    }
}

void ALEController::feed_audio(const int16_t* samples, uint32_t count)
{
    demodulator_.push_samples(samples, count);
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
        emit_event(pal::EventType::ALE_LINK_TERMINATED, "Link state exited");
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
            emit_event(pal::EventType::ALE_LINK_ESTABLISHED, peer);
            break;
        }
        case OperatorEvent::CALL_REJECTED:
            emit_status("Call rejected by remote station (TWAS)");
            if (on_link_terminated) on_link_terminated("Call rejected");
            emit_event(pal::EventType::ALE_LINK_TERMINATED, "Call rejected");
            break;
        case OperatorEvent::NO_CHANNELS_LEFT:
            emit_status("No reply — all calling channels exhausted");
            if (on_link_terminated) on_link_terminated("No reply");
            emit_event(pal::EventType::ALE_LINK_TERMINATED, "No reply");
            break;
        case OperatorEvent::EMERGENCY_ACTIVE:
            emit_status("Emergency manual control is now active");
            emit_event(pal::EventType::SYSTEM_WARNING, "Emergency manual control active");
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
            emit_event(pal::EventType::ALE_CALL_RECEIVED, last_caller_);
        } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                   && !last_caller_.empty()) {
            last_caller_ += chunk;
        }
    }

    sm_.process_received_word(word);
}

void ALEController::set_event_handler(pal::IEventHandler* handler)
{
    event_handler_ = handler;
}

void ALEController::emit_event(pal::EventType type, const std::string& msg, int32_t code)
{
    if (!event_handler_) return;
    pal::Event ev{};
    ev.type         = type;
    ev.source       = "ALEController";
    ev.message      = msg;
    ev.code         = code;
    ev.timestamp_ms = 0;
    ev.data         = nullptr;
    ev.data_size    = 0;
    event_handler_->emit(ev);
}

void ALEController::emit_status(const std::string& msg)
{
    if (on_status_changed) on_status_changed(msg);
}

// ── Command interface ─────────────────────────────────────────────────────────

static std::string cmd_trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

std::string ALEController::process_command(const std::string& raw)
{
    const std::string cmd = cmd_trim(raw);
    if (cmd.empty()) return "";

    if (cmd.rfind("CMD:CALL ", 0) == 0) {
        const std::string target = cmd_trim(cmd.substr(9));
        if (target.empty())
            return "ERROR: CMD:CALL requires a target address";
        if (!initiate_call(target))
            return std::string("ERROR: cannot call in state ")
                   + ALEStateMachine::state_name(state());
        return "OK: calling " + target;
    }
    if (cmd == "CMD:TERMINATE") {
        terminate_link();
        return "OK: terminating link";
    }
    if (cmd == "CMD:REJECT") {
        reject_call();
        return "OK: rejecting call";
    }
    if (cmd == "CMD:SCAN") {
        start_scanning();
        return "OK: scanning";
    }
    if (cmd == "CMD:STATUS") {
        return std::string("STATUS: ") + ALEStateMachine::state_name(state());
    }
    if (cmd.rfind("CMD:ADD_CHANNEL ", 0) == 0) {
        const std::string ch = cmd_trim(cmd.substr(16));
        if (ch.empty())
            return "ERROR: CMD:ADD_CHANNEL requires a channel argument";
        // TODO: implement channel scanning — store ch in scan list and reconfigure SM
        return "TODO: CMD:ADD_CHANNEL " + ch + " (channel scanning not yet implemented)";
    }
    if (cmd == "CMD:HELP") {
        return "Commands: CMD:CALL <ADDR>  CMD:ADD_CHANNEL <CH>  CMD:SCAN"
               "  CMD:TERMINATE  CMD:REJECT  CMD:STATUS";
    }
    return "ERROR: unknown command — try CMD:HELP";
}

} // namespace ale
