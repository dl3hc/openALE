/**
 * \file App/ale_controller.cpp
 */

#include "App/ale_controller.h"
#include "PAL/radio.h"
#include "Word/ale_word.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

static bool is_ale_mode(const std::string& s) {
    static const char* kModes[] = {
        "USB","LSB","AM","FM","FMW","CW","CW_R",
        "FSK","RTTY","DATA_USB","DATA_LSB", nullptr
    };
    for (int i = 0; kModes[i]; ++i)
        if (s == kModes[i]) return true;
    return false;
}

// Parse one channel specification:
//   rx_hz[:tx_hz]  [mode]  [label...]
// Examples:
//   14250000
//   14250000 USB
//   14250000:14260000 LSB DX-Channel
static std::optional<ale::Channel> parse_channel_spec(const std::string& raw)
{
    std::string s = raw;
    auto cpos = s.find('#');
    if (cpos != std::string::npos) s = s.substr(0, cpos);
    // trim
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    s = s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    if (s.empty()) return std::nullopt;

    std::istringstream iss(s);
    std::vector<std::string> toks;
    {
        std::string tok;
        while (iss >> tok) toks.push_back(std::move(tok));
    }
    if (toks.empty()) return std::nullopt;

    // optional leading "ID:<id>" token (written by format_channel_line(); old
    // files/specs without it just get an id auto-assigned by the caller).
    std::string id;
    if (toks[0].rfind("ID:", 0) == 0) {
        id = toks[0].substr(3);
        toks.erase(toks.begin());
        if (toks.empty()) return std::nullopt;
    }

    // first token: rx_hz or rx_hz:tx_hz
    uint32_t rx_hz = 0, tx_hz = 0;
    auto colon = toks[0].find(':');
    if (colon != std::string::npos) {
        rx_hz = static_cast<uint32_t>(std::stoul(toks[0].substr(0, colon)));
        const std::string tx_str = toks[0].substr(colon + 1);
        tx_hz = tx_str.empty() ? 0u : static_cast<uint32_t>(std::stoul(tx_str));
    } else {
        rx_hz = static_cast<uint32_t>(std::stoul(toks[0]));
    }
    if (rx_hz == 0) return std::nullopt;

    // second token (optional): mode string
    std::string mode = "USB";
    size_t label_start = 1;
    if (toks.size() > 1 && is_ale_mode(toks[1])) {
        mode = toks[1];
        label_start = 2;
    }

    // remaining tokens: label
    std::string label;
    for (size_t i = label_start; i < toks.size(); ++i) {
        if (!label.empty()) label += ' ';
        label += toks[i];
    }

    ale::Channel ch(rx_hz, tx_hz, mode, mode);
    ch.label = label;
    ch.id    = id;
    return ch;
}

// Serialize one Channel to a file line ([ID:id] rx_hz tx_hz mode [label])
static std::string format_channel_line(const ale::Channel& ch)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u %u %s",
                  ch.rx_frequency_hz,
                  ch.tx_frequency_hz,
                  ch.rx_mode.c_str());
    std::string line;
    if (!ch.id.empty()) { line += "ID:"; line += ch.id; line += ' '; }
    line += buf;
    if (!ch.label.empty()) { line += ' '; line += ch.label; }
    return line;
}

// Smallest unused "C-<n>" id (n >= 1) given the current channel list.
static std::string next_free_channel_id(const std::vector<ale::Channel>& channels)
{
    for (uint32_t n = 1; ; ++n) {
        const std::string candidate = "C-" + std::to_string(n);
        bool used = false;
        for (const auto& c : channels)
            if (c.id == candidate) { used = true; break; }
        if (!used) return candidate;
    }
}

// Split a comma-separated string into trimmed, non-empty tokens.
static std::vector<std::string> split_csv(const std::string& s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const size_t end   = (comma == std::string::npos) ? s.size() : comma;
        size_t a = start, b = end;
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        if (b > a) out.push_back(s.substr(a, b - a));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

// Join channel IDs back into the same comma-separated form split_csv() expects.
static std::string join_csv(const std::vector<std::string>& items)
{
    std::string out;
    for (const auto& it : items) {
        if (!out.empty()) out += ',';
        out += it;
    }
    return out;
}

static pal::RadioMode mode_from_string(const std::string& s) {
    if (s == "LSB")      return pal::RadioMode::LSB;
    if (s == "CW")       return pal::RadioMode::CW;
    if (s == "CW_R")     return pal::RadioMode::CW_R;
    if (s == "FM")       return pal::RadioMode::FM;
    if (s == "FMW")      return pal::RadioMode::FMW;
    if (s == "AM")       return pal::RadioMode::AM;
    if (s == "FSK")      return pal::RadioMode::FSK;
    if (s == "RTTY")     return pal::RadioMode::RTTY;
    if (s == "DATA_LSB") return pal::RadioMode::DATA_LSB;
    if (s == "DATA_USB") return pal::RadioMode::DATA_USB;
    return pal::RadioMode::USB;
}

// Inverse of mode_from_string() — used by the radio-facing getters so the
// controller never has to keep a second copy of "current mode" itself.
static std::string mode_to_string(pal::RadioMode m) {
    switch (m) {
        case pal::RadioMode::LSB:      return "LSB";
        case pal::RadioMode::CW:       return "CW";
        case pal::RadioMode::CW_R:     return "CW_R";
        case pal::RadioMode::FM:       return "FM";
        case pal::RadioMode::FMW:      return "FMW";
        case pal::RadioMode::AM:       return "AM";
        case pal::RadioMode::FSK:      return "FSK";
        case pal::RadioMode::RTTY:     return "RTTY";
        case pal::RadioMode::DATA_LSB: return "DATA_LSB";
        case pal::RadioMode::DATA_USB: return "DATA_USB";
        default:                       return "USB";
    }
}

} // namespace

namespace ale {

ALEController::ALEController()
    : lqa_analyzer_(&lqa_database_)
{
    wire_callbacks();

    // LQA auto-sounding: fire sm_.send_sounding() when the scanner is
    // currently dwelling on a channel that has stale (or absent) LQA data.
    lqa_analyzer_.set_sounding_callback([this](uint32_t freq) {
        const Channel* ch = sm_.get_current_channel();
        if (ch && ch->rx_frequency_hz == freq)
            sm_.send_sounding();
    });
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
        if (radio_) radio_->set_ptt(!rx_on);
        emit_event(rx_on ? pal::EventType::PTT_OFF : pal::EventType::PTT_ON);
    });

    // Channel hops (scan / calling) → radio frequency/mode change
    sm_.set_channel_callback([this](const Channel& ch) {
        if (!radio_) return;
        pal::Channel pal_ch;
        pal_ch.rx_frequency = ch.rx_frequency_hz;
        pal_ch.tx_frequency = ch.effective_tx_hz();
        pal_ch.rx_mode      = mode_from_string(ch.rx_mode);
        pal_ch.tx_mode      = mode_from_string(ch.tx_mode);
        pal_ch.power        = ch.power_pct;
        pal_ch.antenna      = ch.antenna;
        radio_->set_channel(pal_ch);
    });
}

// ── Radio control ─────────────────────────────────────────────────────────────

void ALEController::set_radio(pal::IRadio* r)
{
    radio_ = r;
}

// ── Radio / VFO control (manual tuning) ────────────────────────────────────────
// All queries/commands go straight through radio_ (pal::IRadio) — no shadow
// frequency/mode state is kept here (see header doc on this section).

Channel ALEController::get_current_channel() const
{
    if (radio_) {
        const pal::Channel pc = radio_->get_channel();
        return Channel(pc.rx_frequency, pc.tx_frequency,
                        mode_to_string(pc.rx_mode), mode_to_string(pc.tx_mode));
    }
    if (const Channel* ch = sm_.get_current_channel()) return *ch;
    return Channel{};
}

uint32_t ALEController::get_current_frequency() const
{
    return get_current_channel().rx_frequency_hz;
}

std::string ALEController::get_current_mode() const
{
    return get_current_channel().rx_mode;
}

bool ALEController::set_frequency(uint32_t hz)
{
    if (!radio_ || hz == 0) return false;
    pal::Channel pc = radio_->get_channel();
    pc.rx_frequency = hz;
    pc.tx_frequency = hz;  // simplex
    radio_->set_channel(pc);
    manual_channel_idx_ = -1;
    return true;
}

bool ALEController::set_mode(const std::string& mode)
{
    if (!radio_ || !is_ale_mode(mode)) return false;
    pal::Channel pc = radio_->get_channel();
    pc.rx_mode = pc.tx_mode = mode_from_string(mode);
    radio_->set_channel(pc);
    return true;
}

bool ALEController::step_channel(int direction)
{
    if (!radio_ || calling_channels_.empty()) return false;
    const int n = static_cast<int>(calling_channels_.size());
    manual_channel_idx_ = ((manual_channel_idx_ < 0 ? 0 : manual_channel_idx_) + direction % n + n) % n;

    const Channel& target = calling_channels_[manual_channel_idx_];
    pal::Channel pc = radio_->get_channel();
    pc.rx_frequency = target.rx_frequency_hz;
    pc.tx_frequency = target.effective_tx_hz();
    pc.rx_mode = pc.tx_mode = mode_from_string(target.rx_mode);
    radio_->set_channel(pc);
    return true;
}

void ALEController::set_tune_step(uint32_t hz)
{
    tune_step_hz_ = hz;
}

uint32_t ALEController::get_tune_step() const
{
    return tune_step_hz_;
}

void ALEController::nudge_frequency(int direction)
{
    if (!radio_) return;
    pal::Channel pc = radio_->get_channel();
    const int64_t new_freq = static_cast<int64_t>(pc.rx_frequency) + direction * static_cast<int64_t>(tune_step_hz_);
    pc.rx_frequency = pc.tx_frequency = static_cast<uint32_t>(std::max<int64_t>(0, new_freq));
    radio_->set_channel(pc);
    manual_channel_idx_ = -1;
}

bool ALEController::get_ptt_state() const
{
    return radio_ ? radio_->is_transmitting() : false;
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
    calling_channels_ = channels;
    sm_.set_calling_channels(channels);
}

// ── Operator actions ──────────────────────────────────────────────────────────

void ALEController::start_available()
{
    emit_status("Available — fixed channel, listening for incoming calls");
    // If we were scanning, drop back to IDLE so callers (e.g. the GUI Scan
    // toggle) get a real state transition + state event. STOP_SCAN is a no-op
    // from any non-SCANNING state (incl. IDLE — see ale_state_machine.cpp), so
    // this is safe for ale_cli.cpp's start-from-IDLE use too.
    sm_.process_event(ALEEvent::STOP_SCAN);
    // SM stays in IDLE; enable RX pipeline directly since enter_state(IDLE)
    // is not called at construction (only on re-entry via transition_to).
    demodulator_.set_enabled(true);
}

void ALEController::start_scanning()
{
    emit_status("Starting ALE scanner — channel hopping");
    sm_.process_event(ALEEvent::START_SCAN);
}

void ALEController::apply_target_scan_channels_for(const std::string& target_addr)
{
    const Contact* c = contact_store_.find(target_addr);
    if (!c) return;
    for (const auto& net_name : c->net_members) {
        if (const Net* net = net_store_.find(net_name)) {
            const uint32_t count = net_scan_channel_count(*net, calling_channels_);
            sm_.set_target_scan_channels(count);
            emit_status("Net '" + net_name + "': scanning call sized for " + std::to_string(count)
                        + " channel(s)");
            return;  // first resolvable net wins (a contact may list several)
        }
    }
}

bool ALEController::initiate_call(const std::string& target_addr)
{
    // Reorder calling channels by LQA history for this station (best first).
    // Falls back silently to the user-configured order when no LQA data exist.
    if (!calling_channels_.empty()) {
        auto ranked = lqa_analyzer_.rank_channels_for_station(target_addr);
        if (!ranked.empty()) {
            auto score_for = [&ranked](uint32_t freq) -> float {
                for (const auto& r : ranked)
                    if (r.frequency_hz == freq) return r.score;
                return 0.0f;
            };
            std::vector<Channel> ordered = calling_channels_;
            std::stable_sort(ordered.begin(), ordered.end(),
                [&score_for](const Channel& a, const Channel& b) {
                    return score_for(a.rx_frequency_hz) > score_for(b.rx_frequency_hz);
                });
            sm_.set_calling_channels(ordered);
            emit_status("LQA: channel order optimised for " + target_addr
                        + " (best: " + std::to_string(ordered.front().rx_frequency_hz) + " Hz)");
        }
    }

    apply_target_scan_channels_for(target_addr);

    emit_status("Initiating call to " + target_addr);
    return sm_.initiate_call(target_addr);
}

bool ALEController::initiate_group_call(const std::vector<std::string>& members)
{
    if (members.empty()) return false;

    // First-member-only simplification — see header doc.
    apply_target_scan_channels_for(members.front());

    emit_status("Initiating group call (" + std::to_string(members.size()) + " members)");
    return sm_.initiate_group_call(members);
}

void ALEController::reject_call()
{
    emit_status("Rejecting incoming call (TWAS)");
    sm_.reject_call();
}

void ALEController::accept_call()
{
    if (sm_.accept_call())
        emit_status("Accepting incoming call");
}

void ALEController::set_manual_accept_mode(bool on, uint32_t decision_timeout_ms)
{
    sm_.set_require_explicit_accept(on, decision_timeout_ms);
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
    now_ms_ = now_ms;
    sm_.update(now_ms);

    // Offline mode: no audio device, so drive word-completion directly by
    // pulling all pending symbol frames and firing on_word_complete per frame.
    if (!audio_device_) {
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modulator_.pull_symbol_frame(syms))
            sm_.on_word_complete();
    }

    // LQA: prune stale entries and trigger auto-sounding when enabled.
    // Throttled to once per second — the database is small but no need to
    // iterate every audio frame.
    if (now_ms - lqa_update_ms_ >= 1000u) {
        lqa_analyzer_.update();
        lqa_update_ms_ = now_ms;
    }
}

void ALEController::feed_audio(const int16_t* samples, uint32_t count)
{
    if (count) {
        int peak = 0;
        for (uint32_t i = 0; i < count; ++i) {
            int v = samples[i] < 0 ? -static_cast<int>(samples[i]) : samples[i];
            if (v > peak) peak = v;
        }
        audio_input_level_ = static_cast<float>(peak) / 32768.0f;
    }
    if (debug_rx_ && count) {
        for (uint32_t i = 0; i < count; ++i) {
            int v = samples[i] < 0 ? -static_cast<int>(samples[i]) : samples[i];
            if (v > dbg_peak_) dbg_peak_ = v;
        }
        dbg_count_ += count;
        if (dbg_count_ >= 4000u) {   // ~0.5 s at 8 kHz
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[RX level] peak=%d  rx=%s  state=%s",
                          dbg_peak_,
                          demodulator_.enabled() ? "on" : "off",
                          ALEStateMachine::state_name(sm_.get_state()));
            emit_status(buf);
            dbg_peak_  = 0;
            dbg_count_ = 0;
        }
    }
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

    // Initialise AMD orderwire tracking when entering HANDSHAKE.
    // amd_skip_count_ = 2×(n-1) where n = words in own encoded address.
    // These are the leading-call DATA/REP extension words to skip before
    // the message section begins (see on_received_word AMD block).
    if (to == ALEState::HANDSHAKE) {
        const int n    = static_cast<int>((self_addr_.length() + 2) / 3);
        amd_skip_count_ = 2 * (n - 1);
        amd_seen_count_ = 0;
        amd_text_acc_.clear();
    }

    // LQA: when we enter SOUNDING, record the sounding timestamp so that
    // is_sounding_due() won't immediately re-trigger after we return to SCANNING.
    // We read back any existing metrics so the time-weighted average is a no-op.
    if (to == ALEState::SOUNDING) {
        const Channel* ch = sm_.get_current_channel();
        if (ch) {
            auto existing = lqa_database_.get_entry(ch->rx_frequency_hz, "");
            const float snr = existing ? existing->snr_db : 0.0f;
            const float ber = existing ? existing->ber    : 0.0f;
            lqa_database_.update_entry(ch->rx_frequency_hz, "", snr, ber, 0, 1);
        }
    }

    // Link exited (except via HANDSHAKE_COMPLETE → LINKED)
    if (from == ALEState::LINKED && to != ALEState::LINKED) {
        link_start_ms_ = 0;
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
            link_start_ms_ = now_ms_;
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
    if (debug_rx_) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "[RX word] %-4s '%s' unanimous=%u fec_err=%u valid=%d state=%s",
                      WordParser::word_type_name(word.type), word.address,
                      static_cast<unsigned>(word.unanimous_votes),
                      static_cast<unsigned>(word.fec_errors),
                      word.valid ? 1 : 0,
                      ALEStateMachine::state_name(sm_.get_state()));
        emit_status(buf);
    }

    // Track latest signal-quality stats (get_current_signal_quality) — same
    // unanimous-votes/fec_errors → snr/ber approximation used by the LQA
    // sounding path below, but kept for any valid word, not just TIS.
    if (word.valid) {
        constexpr float kMaxVotes = 48.0f;
        last_votes_      = word.unanimous_votes;
        last_fec_errors_ = word.fec_errors;
        last_snr_db_     = (word.unanimous_votes / kMaxVotes) * 31.0f;
        last_ber_        = (word.fec_errors > 0) ? static_cast<float>(word.fec_errors) / 50.0f : 0.0f;
    }

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
        && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END)
    {
        // ── Caller identity + DATA/REP AMD text (gated on word.valid) ──────
        if (word.valid) {
            const std::string chunk = trim_ale_address(word.address);

            if (word.type == PreambleType::TIS && last_caller_.empty()) {
                // Caller's conclusion word — fire AMD callback if text was collected.
                last_caller_ = chunk;
                if (!amd_text_acc_.empty() && on_amd_received) {
                    const auto p = amd_text_acc_.find_last_not_of(" @");
                    if (p != std::string::npos)
                        on_amd_received(last_caller_, amd_text_acc_.substr(0, p + 1));
                }
                if (on_call_received) on_call_received(last_caller_);
                emit_event(pal::EventType::ALE_CALL_RECEIVED, last_caller_);
            } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                       && !last_caller_.empty()) {
                last_caller_ += chunk;  // caller-address extension after TIS
            } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                       && last_caller_.empty()) {
                // Before TIS: skip leading-call DATA/REP extensions (2×(n-1) words),
                // then collect AMD message body words (DATA/REP after CMD AMD).
                if (amd_seen_count_ >= amd_skip_count_)
                    amd_text_acc_ += std::string(word.address, 3);
                ++amd_seen_count_;
            }
        }

        // ── CMD AMD word — first message word (A.5.7.2.2) ─────────────────
        // CMD AMD carries Expanded-64 content (0x20-0x5F) in a CMD preamble
        // word.  Basic-38 validation marks it invalid when the first 3 chars
        // include spaces or symbols, so we re-decode from raw_payload regardless
        // of word.valid.  Only collect once we are past the leading-call section.
        if (word.type == PreambleType::CMD && last_caller_.empty()
                && amd_seen_count_ >= amd_skip_count_) {
            char exp64[4];
            if (WordParser::decode_ascii(word.raw_payload, PreambleType::DATA, exp64))
                amd_text_acc_ += std::string(exp64, 3);
        }
    }

    // LQA sounding: record any valid foreign TIS received while listening.
    // A TIS conclusion frame from another station is the primary LQA input;
    // unanimous_votes (0-48) and fec_errors proxy for SNR and BER.
    if (word.valid && word.type == PreambleType::TIS) {
        const ALEState st = sm_.get_state();
        if (st == ALEState::SCANNING || st == ALEState::IDLE) {
            const Channel* ch = sm_.get_current_channel();
            if (ch) {
                constexpr float kMaxVotes = 48.0f;
                const float snr_db = (word.unanimous_votes / kMaxVotes) * 31.0f;
                const float ber    = (word.fec_errors > 0)
                                     ? static_cast<float>(word.fec_errors) / 50.0f
                                     : 0.0f;
                lqa_analyzer_.process_sounding(
                    trim_ale_address(word.address),
                    ch->rx_frequency_hz,
                    snr_db,
                    ber);
            }
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

// ── LQA ──────────────────────────────────────────────────────────────────────

bool ALEController::load_lqa(const std::string& path)
{
    return lqa_database_.load_from_file(path);
}

bool ALEController::save_lqa(const std::string& path) const
{
    return lqa_database_.save_to_file(path);
}

void ALEController::enable_automatic_sounding(bool on)
{
    AnalyzerConfig cfg = lqa_analyzer_.get_config();
    cfg.enable_automatic_sounding = on;
    lqa_analyzer_.set_config(cfg);
}

void ALEController::set_sounding_interval_sec(uint32_t sec)
{
    AnalyzerConfig cfg = lqa_analyzer_.get_config();
    cfg.sounding_interval_ms = sec * 1000u;
    lqa_analyzer_.set_config(cfg);
}

void ALEController::set_scan_dwell_ms(uint32_t ms)
{
    ScanConfig cfg = sm_.get_scan_config();
    cfg.dwell_time_ms = ms;
    sm_.configure_scan(cfg);
}

void ALEController::set_link_idle_timeout_sec(uint32_t sec)
{
    TimingParameters tp = sm_.get_timing_parameters();
    tp.Twa_ms = sec * 1000u;
    sm_.set_timing_parameters(tp);
}

void ALEController::set_max_tune_time_ms(uint32_t ms)
{
    TimingParameters tp = sm_.get_timing_parameters();
    tp.Tt_ms = ms;
    sm_.set_timing_parameters(tp);
}

std::vector<std::string> ALEController::get_all_lqa_entries() const
{
    std::vector<std::string> out;
    const uint32_t now = lqa_database_.get_current_time_ms();  // same clock as LQADatabase itself
    for (const auto& e : lqa_database_.get_all_entries()) {
        const uint32_t age_ms = (now > e.last_activity_ms()) ? (now - e.last_activity_ms()) : 0u;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%u|%s|%.1f|%.4f|%.1f|%.1f|%u",
                      e.frequency_hz, e.remote_station.c_str(),
                      e.snr_db, e.ber, e.sinad_db, e.score, age_ms);
        out.push_back(buf);
    }
    return out;
}

bool ALEController::is_link_active() const
{
    return state() == ALEState::LINKED;
}

uint32_t ALEController::get_call_duration_seconds() const
{
    if (link_start_ms_ == 0 || now_ms_ < link_start_ms_) return 0;
    return (now_ms_ - link_start_ms_) / 1000u;
}

ALEController::SignalQuality ALEController::get_current_signal_quality() const
{
    SignalQuality q;
    q.snr_db     = last_snr_db_;
    q.ber        = last_ber_;
    q.votes      = static_cast<int8_t>(last_votes_);
    q.fec_errors = last_fec_errors_;

    const Channel* ch = sm_.get_current_channel();
    if (ch) {
        const std::string peer = !sm_.get_to_address().empty()
            ? sm_.get_to_address() : sm_.get_caller_address();
        if (auto e = lqa_database_.get_entry(ch->rx_frequency_hz, peer)) {
            q.sinad_db     = e->sinad_db;
            q.multipath_ms = e->multipath_score;  // severity 0-1, not a measured delay
        }
    }
    return q;
}

std::vector<std::string> ALEController::enumerate_audio_inputs() const
{
    std::vector<std::string> out;
    if (!audio_device_) return out;
    for (const auto& d : audio_device_->list_devices())
        if (d.rfind("IN:", 0) == 0) out.push_back(d);
    return out;
}

std::vector<std::string> ALEController::enumerate_audio_outputs() const
{
    std::vector<std::string> out;
    if (!audio_device_) return out;
    for (const auto& d : audio_device_->list_devices())
        if (d.rfind("OUT:", 0) == 0) out.push_back(d);
    return out;
}

float ALEController::get_audio_input_level() const
{
    return audio_input_level_;
}

bool ALEController::test_rig_connection() const
{
    return radio_ ? radio_->is_ready() : false;
}

std::string ALEController::get_rig_connection_status() const
{
    if (!radio_) return "not attached";
    return radio_->is_ready() ? "ready" : "not ready";
}

bool ALEController::export_settings(const std::string& path)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# ALE settings export\n";
    f << "self_address=" << get_primary_self_address() << "\n";
    f << "channel_file=" << channel_file_ << "\n";
    f << "target_scan_channels=" << get_target_scan_channels() << "\n";
    f << "golay_mode=" << static_cast<int>(golay_mode()) << "\n";
    f << "min_unanimous_votes=" << static_cast<int>(min_unanimous_votes()) << "\n";
    f << "adaptive_fec=" << (adaptive_fec() ? 1 : 0) << "\n";
    f << "debug_rx=" << (debug_rx_ ? 1 : 0) << "\n";
    f << "manual_accept_mode=" << (sm_.requires_explicit_accept() ? 1 : 0) << "\n";
    f << "manual_accept_timeout_ms=" << sm_.accept_decision_timeout_ms() << "\n";
    return f.good();
}

bool ALEController::import_settings(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    bool     manual_accept_mode    = sm_.requires_explicit_accept();
    uint32_t manual_accept_timeout = sm_.accept_decision_timeout_ms();

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "self_address" && !val.empty()) {
            add_self_address(val);
            set_primary_self_address(val);
        } else if (key == "channel_file") {
            channel_file_ = val;
        } else if (key == "target_scan_channels") {
            set_target_scan_channels(static_cast<uint32_t>(std::stoul(val)));
        } else if (key == "golay_mode") {
            set_golay_mode(static_cast<GolayMode>(std::stoi(val)));
        } else if (key == "min_unanimous_votes") {
            set_min_unanimous_votes(static_cast<uint8_t>(std::stoi(val)));
        } else if (key == "adaptive_fec") {
            set_adaptive_fec(val == "1");
        } else if (key == "debug_rx") {
            set_debug_rx(val == "1");
        } else if (key == "manual_accept_mode") {
            manual_accept_mode = (val == "1");
        } else if (key == "manual_accept_timeout_ms") {
            manual_accept_timeout = static_cast<uint32_t>(std::stoul(val));
        }
    }
    set_manual_accept_mode(manual_accept_mode, manual_accept_timeout);
    return true;
}

// ── Channel management ────────────────────────────────────────────────────────

bool ALEController::add_channel(const Channel& ch)
{
    Channel ch2 = ch;
    if (ch2.id.empty()) ch2.id = next_free_channel_id(calling_channels_);

    // Replace existing entry with same RX frequency, or append.
    for (auto& c : calling_channels_)
        if (c.rx_frequency_hz == ch2.rx_frequency_hz) { c = ch2; goto apply; }
    calling_channels_.push_back(ch2);
apply:
    sm_.set_calling_channels(calling_channels_);
    if (!channel_file_.empty()) save_channels(channel_file_);
    return true;
}

bool ALEController::del_channel(uint32_t rx_hz)
{
    std::string removed_id;
    for (const auto& c : calling_channels_)
        if (c.rx_frequency_hz == rx_hz) { removed_id = c.id; break; }

    const size_t before = calling_channels_.size();
    calling_channels_.erase(
        std::remove_if(calling_channels_.begin(), calling_channels_.end(),
            [rx_hz](const Channel& c){ return c.rx_frequency_hz == rx_hz; }),
        calling_channels_.end());
    if (calling_channels_.size() == before) return false;

    if (!removed_id.empty()) net_store_.unassign_channel_everywhere(removed_id);
    sm_.set_calling_channels(calling_channels_);
    if (!channel_file_.empty()) save_channels(channel_file_);
    return true;
}

bool ALEController::load_channels(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<Channel> loaded;
    net_store_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("NET:", 0) == 0) {
            std::istringstream iss(line.substr(4));
            std::string name, ids;
            iss >> name;
            std::getline(iss, ids);  // remainder of the line (leading space trimmed by split_csv)
            net_store_.add_net(name);
            for (const auto& id : split_csv(ids))
                net_store_.assign_channel(name, id);
            continue;
        }
        auto ch = parse_channel_spec(line);
        if (ch) loaded.push_back(*ch);
    }
    for (auto& ch : loaded)
        if (ch.id.empty()) ch.id = next_free_channel_id(loaded);
    calling_channels_ = std::move(loaded);
    sm_.set_calling_channels(calling_channels_);
    return true;
}

bool ALEController::save_channels(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# ALE channel list — MIL-STD-188-141B\n";
    f << "# ID:id rx_hz tx_hz mode [label]\n";
    for (const auto& ch : calling_channels_)
        f << format_channel_line(ch) << '\n';
    if (!net_store_.empty()) {
        f << "# NET:name id,id,...\n";
        for (const auto& net : net_store_.all())
            f << "NET:" << net.name << ' ' << join_csv(net.channel_ids) << '\n';
    }
    return f.good();
}

// ── Nets ─────────────────────────────────────────────────────────────────────

bool ALEController::add_net(const std::string& name)
{
    const bool added = net_store_.add_net(name);
    if (added && !channel_file_.empty()) save_channels(channel_file_);
    return added;
}

bool ALEController::del_net(const std::string& name)
{
    const bool removed = net_store_.remove_net(name);
    if (removed && !channel_file_.empty()) save_channels(channel_file_);
    return removed;
}

bool ALEController::assign_channel_to_net(const std::string& net_name, const std::string& channel_id)
{
    const bool ok = net_store_.assign_channel(net_name, channel_id);
    if (ok && !channel_file_.empty()) save_channels(channel_file_);
    return ok;
}

bool ALEController::unassign_channel_from_net(const std::string& net_name, const std::string& channel_id)
{
    const bool ok = net_store_.unassign_channel(net_name, channel_id);
    if (ok && !channel_file_.empty()) save_channels(channel_file_);
    return ok;
}

// ── Contact / address book ────────────────────────────────────────────────────

bool ALEController::add_contact(const std::string& callsign,
                                const std::string& name,
                                const std::string& status,
                                const std::string& net_members,
                                const std::string& valid_channels)
{
    if (callsign.empty()) return false;
    Contact c;
    c.callsign     = callsign;
    c.name         = name;
    c.enabled      = (status != "disabled");
    c.net_members  = split_csv(net_members);
    c.all_channels = (valid_channels.empty() || valid_channels == "ALL");
    if (!c.all_channels) c.valid_channels = split_csv(valid_channels);
    return contact_store_.add_or_update(c);
}

bool ALEController::remove_contact(const std::string& callsign)
{
    const bool removed = contact_store_.remove(callsign);
    if (removed && selected_contact_ == callsign) selected_contact_.clear();
    return removed;
}

std::vector<std::string> ALEController::get_all_contacts() const
{
    std::vector<std::string> out;
    out.reserve(contact_store_.size());
    for (const auto& c : contact_store_.all()) {
        out.push_back(c.callsign + "|" + c.name + "|" + (c.enabled ? "enabled" : "disabled")
                     + "|" + join_csv(c.net_members)
                     + "|" + (c.all_channels ? "ALL" : join_csv(c.valid_channels)));
    }
    return out;
}

bool ALEController::select_contact(const std::string& callsign)
{
    if (!contact_store_.find(callsign)) return false;
    selected_contact_ = callsign;
    return true;
}

std::string ALEController::get_selected_contact() const
{
    return selected_contact_;
}

// ── Self address table ────────────────────────────────────────────────────────

bool ALEController::add_self_address(const std::string& addr,
                                     const std::string& status,
                                     const std::string& valid_channels)
{
    if (addr.empty()) return false;
    SelfAddressEntry e;
    e.address      = addr;
    e.enabled      = (status != "disabled");
    e.all_channels = (valid_channels.empty() || valid_channels == "ALL");
    if (!e.all_channels) e.valid_channels = split_csv(valid_channels);
    const bool added = self_address_store_.add(e);
    // First entry ever added becomes primary (SelfAddressStore::add()) — apply it.
    if (added && self_address_store_.primary() == addr) set_primary_self_address(addr);
    return added;
}

bool ALEController::remove_self_address(const std::string& addr)
{
    return self_address_store_.remove(addr);
}

std::vector<std::string> ALEController::get_all_self_addresses() const
{
    std::vector<std::string> out;
    out.reserve(self_address_store_.all().size());
    for (const auto& e : self_address_store_.all()) {
        out.push_back(e.address + "|" + (e.enabled ? "enabled" : "disabled")
                     + "|" + (e.all_channels ? "ALL" : join_csv(e.valid_channels)));
    }
    return out;
}

bool ALEController::set_primary_self_address(const std::string& addr)
{
    if (!self_address_store_.set_primary(addr)) return false;
    set_self_address(addr);  // drives sm_.set_self_address() + self_addr_
    return true;
}

std::string ALEController::get_primary_self_address() const
{
    return self_address_store_.primary();
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
        const std::string spec = cmd_trim(cmd.substr(16));
        if (spec.empty())
            return "ERROR: CMD:ADD_CHANNEL requires: rx_hz[:tx_hz] [mode] [label]";
        auto ch = parse_channel_spec(spec);
        if (!ch)
            return "ERROR: cannot parse channel spec '" + spec + "'";
        add_channel(*ch);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "OK: channel %u Hz added (%zu total)",
                      ch->rx_frequency_hz, calling_channels_.size());
        return buf;
    }
    if (cmd.rfind("CMD:DEL_CHANNEL ", 0) == 0) {
        const std::string arg = cmd_trim(cmd.substr(16));
        if (arg.empty())
            return "ERROR: CMD:DEL_CHANNEL requires rx_hz";
        const uint32_t rx_hz = static_cast<uint32_t>(std::stoul(arg));
        if (!del_channel(rx_hz))
            return "ERROR: channel " + arg + " not found";
        char buf[80];
        std::snprintf(buf, sizeof(buf), "OK: channel %u Hz removed (%zu remaining)",
                      rx_hz, calling_channels_.size());
        return buf;
    }
    if (cmd == "CMD:LIST_CHANNELS") {
        if (calling_channels_.empty())
            return "CHANNELS: (none)";
        std::string out = "CHANNELS:\n";
        for (const auto& c : calling_channels_) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "  %-5s %u Hz  tx=%u  %s%s%s%s",
                          c.id.c_str(), c.rx_frequency_hz, c.effective_tx_hz(),
                          c.rx_mode.c_str(),
                          c.label.empty() ? "" : "  ",
                          c.label.c_str(),
                          c.enabled ? "" : "  [SCAN=N]");
            out += buf; out += '\n';
        }
        return out;
    }
    if (cmd == "CMD:CLEAR_CHANNELS") {
        calling_channels_.clear();
        sm_.set_calling_channels(calling_channels_);
        if (!channel_file_.empty()) save_channels(channel_file_);
        return "OK: channel list cleared";
    }
    if (cmd.rfind("CMD:SAVE_CHANNELS", 0) == 0) {
        std::string path = cmd_trim(cmd.substr(17));
        if (path.empty()) path = channel_file_;
        if (path.empty()) return "ERROR: CMD:SAVE_CHANNELS requires a path";
        if (!save_channels(path))
            return "ERROR: cannot write to '" + path + "'";
        return "OK: channels saved to " + path;
    }
    if (cmd.rfind("CMD:LOAD_CHANNELS ", 0) == 0) {
        const std::string path = cmd_trim(cmd.substr(18));
        if (path.empty()) return "ERROR: CMD:LOAD_CHANNELS requires a path";
        if (!load_channels(path))
            return "ERROR: cannot read '" + path + "'";
        char buf[128];
        std::snprintf(buf, sizeof(buf), "OK: %zu channel(s) loaded from %s",
                      calling_channels_.size(), path.c_str());
        return buf;
    }
    if (cmd.rfind("CMD:ADD_NET ", 0) == 0) {
        const std::string name = cmd_trim(cmd.substr(12));
        if (name.empty())
            return "ERROR: CMD:ADD_NET requires a name";
        if (!add_net(name))
            return "ERROR: net '" + name + "' already exists";
        return "OK: net " + name + " added";
    }
    if (cmd.rfind("CMD:DEL_NET ", 0) == 0) {
        const std::string name = cmd_trim(cmd.substr(12));
        if (name.empty())
            return "ERROR: CMD:DEL_NET requires a name";
        if (!del_net(name))
            return "ERROR: net '" + name + "' not found";
        return "OK: net " + name + " removed";
    }
    if (cmd.rfind("CMD:ASSIGN_CHANNEL ", 0) == 0) {
        std::istringstream iss(cmd.substr(19));
        std::string net_name, channel_id;
        iss >> net_name >> channel_id;
        if (net_name.empty() || channel_id.empty())
            return "ERROR: CMD:ASSIGN_CHANNEL requires <net> <channel_id>";
        if (!assign_channel_to_net(net_name, channel_id))
            return "ERROR: net '" + net_name + "' not found";
        return "OK: " + channel_id + " assigned to " + net_name;
    }
    if (cmd.rfind("CMD:UNASSIGN_CHANNEL ", 0) == 0) {
        std::istringstream iss(cmd.substr(21));
        std::string net_name, channel_id;
        iss >> net_name >> channel_id;
        if (net_name.empty() || channel_id.empty())
            return "ERROR: CMD:UNASSIGN_CHANNEL requires <net> <channel_id>";
        if (!unassign_channel_from_net(net_name, channel_id))
            return "ERROR: net '" + net_name + "' not found";
        return "OK: " + channel_id + " unassigned from " + net_name;
    }
    if (cmd == "CMD:LIST_NETS") {
        if (net_store_.empty())
            return "NETS: (none)";
        std::string out = "NETS:\n";
        for (const auto& net : net_store_.all()) {
            out += "  " + net.name + ": "
                 + (net.channel_ids.empty() ? "(no channels)" : join_csv(net.channel_ids))
                 + "  [scan=" + std::to_string(net_scan_channel_count(net, calling_channels_)) + "]\n";
        }
        return out;
    }
    if (cmd.rfind("CMD:AMD ", 0) == 0) {
        std::string text = cmd_trim(cmd.substr(8));
        if (text.empty())
            return "ERROR: CMD:AMD requires message text (max 90 chars, Expanded-64)";
        if (text.length() > 90) text = text.substr(0, 90);
        ALEStateMachine::PendingMessage msg;
        msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
        msg.content = std::move(text);
        sm_.set_pending_message(msg);
        return "OK: AMD text queued — send with CMD:CALL <ADDR>";
    }
    if (cmd == "CMD:HELP") {
        return
            "Commands:\n"
            "  CMD:CALL <ADDR>                     initiate individual call\n"
            "  CMD:AMD <text>                      queue AMD orderwire for next call (max 90 chars)\n"
            "  CMD:TERMINATE                       terminate current link\n"
            "  CMD:REJECT                          reject incoming call (TWAS)\n"
            "  CMD:SCAN                            start channel scanning\n"
            "  CMD:STATUS                          print current SM state\n"
            "  CMD:ADD_CHANNEL rx_hz[:tx_hz] [mode] [label]  add/update channel\n"
            "  CMD:DEL_CHANNEL rx_hz               remove channel\n"
            "  CMD:LIST_CHANNELS                   list all channels\n"
            "  CMD:CLEAR_CHANNELS                  remove all channels\n"
            "  CMD:SAVE_CHANNELS [path]            save channel list to file\n"
            "  CMD:LOAD_CHANNELS <path>            load channel list from file\n"
            "  CMD:ADD_NET <name>                   add a net\n"
            "  CMD:DEL_NET <name>                   remove a net\n"
            "  CMD:ASSIGN_CHANNEL <net> <id>        assign a channel ID to a net\n"
            "  CMD:UNASSIGN_CHANNEL <net> <id>      remove a channel ID from a net\n"
            "  CMD:LIST_NETS                        list all nets and their channels";
    }
    return "ERROR: unknown command — try CMD:HELP";
}

} // namespace ale
