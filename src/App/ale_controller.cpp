/**
 * \file App/ale_controller.cpp
 */

#include "App/ale_controller.h"
#include "LQA/lqa_metrics.h"
#include "PAL/radio.h"
#include "Word/ale_word.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

// Validates a candidate ALE address against the Basic-38 character set and
// the 3–15 char length limit (A.5.2.4.2 / REQ-ADDR-001/002/004).
// The AddressEncoder will silently truncate >15-char inputs (DD-007 safety
// net), but the app boundary must surface invalid input to the caller.
static bool is_valid_ale_address(const std::string& addr) {
    if (addr.size() < 3 || addr.size() > 15) return false;
    for (char c : addr)
        if (!ale::WordParser::is_valid_basic38_char(c)) return false;
    return true;
}

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

    MetricsConfig mc;
    mc.averaging_window = 1;
    lqa_db_metrics_.set_config(mc);
    lqa_db_metrics_.set_database(&lqa_database_);
    sm_.set_lqa_metrics(&lqa_db_metrics_);

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
    // When ptt_lead_deadline_ms_ is active (PTT just asserted, radio not yet in TX),
    // words are buffered in pending_tx_words_ and flushed by update() after the lead.
    sm_.set_transmit_callback([this](const ALEWord& w) {
        if (ptt_lead_deadline_ms_ > 0) {
            pending_tx_words_.push_back({ w, audio_device_ != nullptr });
            return;
        }
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

    // SM frame assembler → passive monitor (on_frame_decoded)
    sm_.set_frame_assembled_callback([this](const ALEMessage& frame) {
        const uint32_t fid = monitor_frame_id_++;
        if (on_frame_decoded)
            on_frame_decoded(frame, fid);
    });

    // RX pipeline word → SM
    demodulator_.set_word_callback([this](const ALEWord& w) {
        on_received_word(w);
    });

    // SM RX-enable control → pipeline (SM disables RX during TX phases)
    // PTT transitions mirror RX enable: RX off = TX active = PTT on.
    // PTT lead (RX→TX): PTT asserted immediately, audio words buffered for
    // ptt_lead_ms to let the radio's CAT/CI-V command settle before audio starts.
    // PTT tail (TX→RX): demodulator and PTT release deferred by ptt_tail_ms to
    // let the audio buffer drain fully before switching to RX.
    sm_.set_rx_enabled_callback([this](bool rx_on) {
        sm_rx_enabled_ = rx_on;
        if (manual_ptt_) {
            if (!rx_on) demodulator_.set_enabled(false);
            emit_event(rx_on ? pal::EventType::PTT_OFF : pal::EventType::PTT_ON);
            return;
        }
        if (rx_on) {
            // TX→RX: cancel any pending lead, apply tail
            ptt_lead_deadline_ms_ = 0;
            pending_tx_words_.clear();
            if (config_.ptt_tail_ms > 0) {
                ptt_tail_deadline_ms_ = now_ms_ + config_.ptt_tail_ms;
                // actual PTT release + demod enable happen in update()
            } else {
                if (radio_) radio_->set_ptt(false);
                demodulator_.set_enabled(true);
            }
        } else {
            // RX→TX: cancel any pending tail, assert PTT, start lead
            ptt_tail_deadline_ms_ = 0;
            demodulator_.set_enabled(false);
            if (radio_) radio_->set_ptt(true);
            ptt_lead_deadline_ms_ = (config_.ptt_lead_ms > 0)
                ? now_ms_ + config_.ptt_lead_ms : 0;
        }
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

bool ALEController::set_self_address(const std::string& addr)
{
    if (!is_valid_ale_address(addr)) return false;
    self_addr_ = addr;
    sm_.set_self_address(addr);
    return true;
}

void ALEController::apply_config(const ALEStationConfig& cfg)
{
    set_golay_mode(cfg.golay_mode);
    set_min_unanimous_votes(cfg.min_unanimous_votes);
    set_adaptive_fec(cfg.adaptive_fec);
    set_manual_accept_mode(cfg.manual_accept_mode, cfg.accept_timeout_ms);
    set_debug_rx(cfg.debug_rx);
    set_scan_dwell_ms(cfg.scan_dwell_ms);
    set_sounding_interval_sec(cfg.sounding_interval_sec);
    set_link_idle_timeout_sec(cfg.link_idle_timeout_sec);
    set_max_tune_time_ms(cfg.max_tune_time_ms);
    set_ptt_lead_ms(cfg.ptt_lead_ms);
    set_ptt_tail_ms(cfg.ptt_tail_ms);
    config_.assumed_scan_channels = cfg.assumed_scan_channels;
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

bool ALEController::send_sounding()
{
    if (!sm_.send_sounding()) {
        emit_status("Manual sounding rejected — only available while IDLE or scanning");
        return false;
    }
    emit_status("Manual sounding — transmitting on current channel");
    return true;
}

bool ALEController::send_sounding_sweep(const std::vector<Channel>& channels)
{
    if (!sm_.send_sounding_sweep(channels)) {
        emit_status("Sounding sweep rejected — only available while IDLE or scanning");
        return false;
    }
    emit_status("Sounding sweep — " + std::to_string(channels.size())
                + " channel(s)");
    return true;
}

void ALEController::apply_target_scan_channels_for(const std::string& target_addr)
{
    // Fallback: assumed_scan_channels wenn gesetzt, sonst eigene Kanalliste.
    uint32_t n = (config_.assumed_scan_channels > 0)
                     ? config_.assumed_scan_channels
                     : static_cast<uint32_t>(calling_channels_.size());

    // Net-Override falls Kontakt einem bekannten Net angehört.
    const Contact* c = contact_store_.find(target_addr);
    if (c) {
        for (const auto& net_name : c->net_members) {
            if (const Net* net = net_store_.find(net_name)) {
                n = net_scan_channel_count(*net, calling_channels_);
                emit_status("Net '" + net_name + "': scanning call sized for "
                            + std::to_string(n) + " channel(s)");
                break;
            }
        }
    }

    sm_.set_target_scan_channels(n);
    emit_status("Scanning call: C=" + std::to_string(n) + " channel(s)");
}

LQACmdPayload ALEController::compute_lqa_payload(uint32_t freq_hz) const {
    LQACmdPayload p{};  // sentinels: sinad=31, ber=31, mp=7
    const auto e = lqa_database_.get_entry(freq_hz, "");
    if (!e) return p;
    if (e->sinad_db > 0.0f) {
        // sinad_db is already stored as a float SINAD code (0-30) via update_entry_extended
        const float s = e->sinad_db;
        p.sinad = (s <= 0.0f) ? 0u : (s >= 30.0f) ? 30u : static_cast<uint8_t>(s + 0.5f);
    }
    if (e->ber > 0.0f) {
        const auto ber_score = static_cast<uint8_t>(std::min(48.0f, e->ber * 50.0f));
        p.ber = ber_score_to_lqa_code(ber_score);
    }
    if (e->multipath_score > 0.0f) {
        p.mp = multipath_delay_to_lqa_code(e->multipath_score * 6.0f);
    }
    return p;
}

bool ALEController::initiate_call(const std::string& target_addr)
{
    if (!is_valid_ale_address(target_addr)) {
        emit_status("ERROR: address '" + target_addr
                    + "' invalid — must be 3–15 Basic-38 characters (A-Z, 0-9, @, ?)");
        return false;
    }

    // Channel ordering: always use LQA data when available (Spec: mandatory).
    // Priority: 1) station-specific bilateral scores (A.5.4.5), 2) aggregate
    // channel scores (soundings from any station), 3) user-configured order.
    uint32_t first_call_freq_hz = calling_channels_.empty()
        ? 0u : calling_channels_.front().rx_frequency_hz;
    if (!calling_channels_.empty()) {
        auto ranked = lqa_analyzer_.rank_channels_for_station(target_addr);
        const bool has_station_data = !ranked.empty();

        auto score_for = [&ranked](uint32_t freq) -> float {
            for (const auto& r : ranked)
                if (r.frequency_hz == freq) return r.score;
            return -1.0f;  // unlisted: sort after all ranked entries
        };

        std::vector<Channel> ordered = calling_channels_;
        std::stable_sort(ordered.begin(), ordered.end(),
            [&](const Channel& a, const Channel& b) {
                if (has_station_data) {
                    return score_for(a.rx_frequency_hz) > score_for(b.rx_frequency_hz);
                }
                float sa = lqa_analyzer_.compute_channel_aggregate_score(a.rx_frequency_hz);
                float sb = lqa_analyzer_.compute_channel_aggregate_score(b.rx_frequency_hz);
                if (sa > 0.0f || sb > 0.0f) return sa > sb;
                return false;
            });
        sm_.set_calling_channels(ordered);
        first_call_freq_hz = ordered.front().rx_frequency_hz;
        if (has_station_data) {
            emit_status("LQA: channel order optimised for " + target_addr
                        + " (best: " + std::to_string(first_call_freq_hz) + " Hz)");
        }
    }

    apply_target_scan_channels_for(target_addr);

    // Block A4 — Queue CMD LQA (KA1=1) for the calling station's frame.
    // Gated on lqa_exchange_enabled; channel ranking above is always active.
    sent_ka1_ = false;
    last_call_target_.clear();
    last_call_freq_hz_ = 0;
    if (!calling_channels_.empty() && config_.lqa_exchange_enabled) {
        LQACmdPayload p = compute_lqa_payload(first_call_freq_hz);
        p.ka1 = true;
        sm_.set_pending_lqa_cmd(encode_lqa_cmd(p));
        sent_ka1_          = true;
        last_call_target_  = target_addr;
        last_call_freq_hz_ = first_call_freq_hz;
    }

    emit_status("Initiating call to " + target_addr);
    return sm_.initiate_call(target_addr);
}

bool ALEController::initiate_single_channel_call(const std::string& target_addr)
{
    if (!is_valid_ale_address(target_addr)) {
        emit_status("ERROR: address '" + target_addr
                    + "' invalid — must be 3–15 Basic-38 characters (A-Z, 0-9, @, ?)");
        return false;
    }
    Channel cur = get_current_channel();
    sm_.set_calling_channels({ cur });
    sm_.set_target_scan_channels(1);
    sent_ka1_ = false;
    last_call_target_.clear();
    last_call_freq_hz_ = 0;
    if (config_.lqa_exchange_enabled) {
        LQACmdPayload p = compute_lqa_payload(cur.rx_frequency_hz);
        p.ka1 = true;
        sm_.set_pending_lqa_cmd(encode_lqa_cmd(p));
        sent_ka1_         = true;
        last_call_target_ = target_addr;
        last_call_freq_hz_ = cur.rx_frequency_hz;
    }
    emit_status("Initiating single-channel call to " + target_addr);
    return sm_.initiate_call(target_addr);
}

bool ALEController::initiate_group_call(const std::vector<std::string>& members)
{
    if (members.empty()) return false;
    for (const auto& m : members) {
        if (!is_valid_ale_address(m)) {
            emit_status("ERROR: group member '" + m
                        + "' invalid — must be 3–15 Basic-38 characters (A-Z, 0-9, @, ?)");
            return false;
        }
    }

    // First-member-only simplification — see header doc.
    apply_target_scan_channels_for(members.front());

    emit_status("Initiating group call (" + std::to_string(members.size()) + " members)");
    return sm_.initiate_group_call(members);
}

void ALEController::reject_call()
{
    // Manual accept is a POST-link decision (the handshake auto-completed). A
    // reject therefore tears down the already-established link with a TWAS
    // termination frame and returns to AVAILABLE. Works for both the pending
    // (LINKED_PENDING_OPERATOR) case and an auto-accepted link the operator
    // chooses to drop. No-op when no link is active.
    if (pending_operator_accept_ || is_link_active()) {
        pending_operator_accept_ = false;
        emit_status("Rejecting call — terminating link (TWAS)");
        terminate_link();
        return;
    }
    emit_status("Reject has no effect — no active link to terminate");
}

void ALEController::accept_call()
{
    // In manual-accept mode the link is already up (handshake auto-completed);
    // Accept just clears the pending-operator gate so the operator's voice/data
    // session is "blessed". No-op otherwise (auto-accept links need no blessing).
    if (pending_operator_accept_) {
        pending_operator_accept_ = false;
        emit_status("Call accepted by operator — link retained");
        return;
    }
}

void ALEController::set_manual_accept_mode(bool on, uint32_t decision_timeout_ms)
{
    config_.manual_accept_mode = on;
    config_.accept_timeout_ms  = decision_timeout_ms;  // retained for config export; no longer gates the handshake
    // The SM no longer pauses for manual accept (post-link operator gate instead);
    // set_require_explicit_accept() is a no-op kept for API compatibility.
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

void ALEController::set_manual_ptt(bool on)
{
    manual_ptt_ = on;
    if (on) {
        // Immediate TX override: cancel pending timing, disable demod, assert PTT
        ptt_tail_deadline_ms_ = 0;
        ptt_lead_deadline_ms_ = 0;
        pending_tx_words_.clear();
        demodulator_.set_enabled(false);
        if (radio_) radio_->set_ptt(true);
    } else {
        // Restore SM control: release PTT only if SM currently wants RX
        if (sm_rx_enabled_) {
            if (radio_) radio_->set_ptt(false);
            demodulator_.set_enabled(true);
        }
        // If SM wants TX, the rx_enabled_callback already manages PTT — nothing to do
    }
    emit_event(on ? pal::EventType::PTT_ON : pal::EventType::PTT_OFF);
}

// ── Main-loop drivers ─────────────────────────────────────────────────────────

void ALEController::update(uint32_t now_ms)
{
    now_ms_ = now_ms;

    // PTT lead: flush buffered TX words once the lead time has elapsed
    if (ptt_lead_deadline_ms_ > 0 && now_ms >= ptt_lead_deadline_ms_) {
        ptt_lead_deadline_ms_ = 0;
        for (auto& [w, has_dev] : pending_tx_words_) {
            modulator_.enqueue_word(w);
            if (has_dev && audio_device_)
                audio_device_->arm_frame_complete([this]() { sm_.on_word_complete(); });
        }
        pending_tx_words_.clear();
    }

    // PTT tail: release PTT + enable demodulator once audio buffer has drained
    if (ptt_tail_deadline_ms_ > 0 && !manual_ptt_ && now_ms >= ptt_tail_deadline_ms_) {
        ptt_tail_deadline_ms_ = 0;
        if (radio_) radio_->set_ptt(false);
        demodulator_.set_enabled(true);
    }

    sm_.update(now_ms);

    maybe_emit_call_alert();

    // Commit a settled received-sounding frame to the LQA DB (full address +
    // frame-averaged snr/ber). Checked every tick — cheap, and time-sensitive.
    if (!sounding_caller_acc_.empty() && sounding_settle_ms_ > 0
        && (now_ms - sounding_settle_ms_) >= ALETimingConstants::Tdrw_ms) {
        commit_sounding_sample();
    }

    // Periodic multi-channel sounding sweep (set_automatic_sounding). Start a
    // sweep over the configured net's channels every interval, gated on IDLE/
    // SCANNING. A running sweep holds the SM in SOUNDING, which blocks re-entry.
    if (auto_sounding_on_ && !auto_sounding_net_.empty()
        && auto_sounding_interval_ms_ > 0) {
        const ALEState st = sm_.get_state();
        if ((st == ALEState::IDLE || st == ALEState::SCANNING)
            && (now_ms - auto_sounding_last_ms_) >= auto_sounding_interval_ms_) {
            auto channels = resolve_net_sounding_channels(auto_sounding_net_);
            if (!channels.empty()) {
                sm_.send_sounding_sweep(channels);
                emit_status("Auto-sounding sweep on net '" + auto_sounding_net_
                            + "' (" + std::to_string(channels.size()) + " channels)");
            }
            // Re-arm whether or not channels resolved (avoid busy-looping).
            auto_sounding_last_ms_ = now_ms_;
        }
    }

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

void ALEController::maybe_emit_call_alert()
{
    // Fire the incoming-call alert (and any collected AMD) exactly once per
    // handshake, the moment the caller's conclusion has fully settled. That is
    // when the SM leaves WAIT_CYCLE_END (→ AWAIT_ACCEPT in manual-accept mode,
    // → SLOT_WAIT otherwise), at which point sm_.get_caller_address() holds the
    // complete, reassembled address (TIS + DATA/REP). Emitting earlier (at the
    // TIS word) would report a truncated caller for >3-char addresses.
    //
    // In manual-accept mode AWAIT_ACCEPT holds until accept_call()/reject_call(),
    // so the alert still precedes the operator decision. handle_handshake()
    // advances one phase per update() tick, so this reliably catches the
    // post-WAIT_CYCLE_END phase before LINKED.
    if (call_alert_fired_) return;
    if (sm_.get_state() != ALEState::HANDSHAKE) return;
    if (!sm_.is_hs_conclusion_rcvd()) return;
    if (sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END) return;

    call_alert_fired_ = true;
    const std::string caller = sm_.get_caller_address();

    // Block A4 (responder) — queue CMD LQA (KA1=0) for the response frame.
    // Done here (once, at alert time) so it's set for both auto-accept and manual-accept.
    if (config_.lqa_exchange_enabled) {
        if (const Channel* ch = sm_.get_current_channel()) {
            LQACmdPayload resp_p = compute_lqa_payload(ch->rx_frequency_hz);
            resp_p.ka1 = false;
            sm_.set_pending_lqa_cmd(encode_lqa_cmd(resp_p));
        }
    }

    // Block A5 — store bilateral SINAD/BER/MP received from caller's CMD 'a'.
    if (config_.lqa_exchange_enabled && pending_bilateral_valid_) {
        if (!caller.empty()) {
            const auto& bp = pending_bilateral_payload_;
            lqa_database_.update_bilateral(pending_bilateral_freq_hz_, caller,
                                            bp.sinad, bp.ber, bp.mp, 0u);
            emit_status("LQA bilateral RX: " + caller
                        + " SINAD=" + std::to_string(bp.sinad));

            // Block C5 TX — if caller set KA1=1, generate and queue LQA Report.
            if (bp.ka1) {
                const auto entries = lqa_database_.get_entries_for_station(caller, 25.0f);
                if (!entries.empty()) {
                    const uint32_t now = lqa_database_.get_current_time_ms();
                    std::vector<LQAReport> reports;
                    for (const auto& e : entries) {
                        LQAReport r;
                        r.frequency_hz = e.frequency_hz;
                        r.age   = lqa_age_code(e.last_contact_ms, now);
                        r.sinad = (e.sinad_db > 0.0f)
                            ? static_cast<uint8_t>(std::min(30.0f, e.sinad_db)) : kSinadLqaNoValue;
                        r.ber   = (e.ber > 0.0f)
                            ? ber_score_to_lqa_code(
                                  static_cast<uint8_t>(std::min(48.0f, e.ber * 50.0f)))
                            : kBerLqaNoValue;
                        r.mp    = multipath_delay_to_lqa_code(e.multipath_score * 6.0f);
                        reports.push_back(r);
                    }
                    sm_.set_pending_lqa_report_seq(ALESequenceBuilder::lqa_report(reports));
                    emit_status("LQA Report queued for " + caller + " ("
                                + std::to_string(reports.size()) + " channels)");
                }
            }
        }
        pending_bilateral_valid_ = false;
    }

    if (!amd_text_acc_.empty() && on_amd_received) {
        const auto p = amd_text_acc_.find_last_not_of(" @");
        if (p != std::string::npos)
            on_amd_received(caller, amd_text_acc_.substr(0, p + 1));
    }
    if (on_call_received) on_call_received(caller);
    emit_event(pal::EventType::ALE_CALL_RECEIVED, caller);
}

void ALEController::commit_sounding_sample()
{
    if (sounding_caller_acc_.empty() || sounding_word_count_ == 0) {
        sounding_caller_acc_.clear();
        return;
    }
    const float avg_snr = sounding_snr_sum_ / static_cast<float>(sounding_word_count_);
    const float avg_ber = sounding_ber_sum_ / static_cast<float>(sounding_word_count_);
    lqa_analyzer_.process_sounding(sounding_caller_acc_, sounding_freq_hz_,
                                   avg_snr, avg_ber);
    sounding_caller_acc_.clear();
    sounding_settle_ms_  = 0;
    sounding_word_count_ = 0;
    sounding_snr_sum_    = 0.0f;
    sounding_ber_sum_    = 0.0f;
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
    if (from == ALEState::HANDSHAKE) {
        last_caller_.clear();
        call_alert_fired_           = false;
        pending_bilateral_valid_    = false;  // Block A5 — stale data from previous handshake
    }

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

            // Block B3 — attach CMD NOISE to this sounding cycle.
            if (config_.lqa_exchange_enabled) {
                const auto stats = lqa_metrics_.get_noise_floor_stats(now_ms_);
                // Convert dBm to 7-bit CMD NOISE scale (0–126 dBm; 127=no report).
                // Clamp: values below -120 or above 6 dBm (relative 0.1µV offset) → 127.
                auto to_noise_code = [](float dbm) -> uint8_t {
                    if (dbm <= -120.0f) return 127u;
                    const int v = static_cast<int>(dbm + 120.0f);  // shift to 0-based
                    return (v < 0 || v > 126) ? 127u : static_cast<uint8_t>(v);
                };
                const uint8_t max_code  = to_noise_code(stats.max_dbm);
                const uint8_t mean_code = to_noise_code(stats.mean_dbm);
                if (max_code != 127u || mean_code != 127u) {
                    // noise_seq.words().front().raw_payload is the 21-bit payload;
                    // SM's handle_sounding() masks with 0x1FFFFF so pass as-is.
                    const auto noise_seq = ALESequenceBuilder::noise_cmd(max_code, mean_code);
                    if (!noise_seq.empty())
                        sm_.set_pending_noise_cmd(noise_seq.words().front().raw_payload);
                }
            }
        }
    }

    // Link exited (except via HANDSHAKE_COMPLETE → LINKED)
    if (from == ALEState::LINKED && to != ALEState::LINKED) {
        link_start_ms_ = 0;
        pending_operator_accept_ = false;  // clear any unresolved manual-accept gate
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
            // Block A5 — SAM side: if JOE sent CMD 'a' in the response frame, store it.
            if (config_.lqa_exchange_enabled && pending_bilateral_valid_) {
                const std::string peer = sm_.get_to_address();
                if (!peer.empty()) {
                    const auto& bp = pending_bilateral_payload_;
                    lqa_database_.update_bilateral(pending_bilateral_freq_hz_, peer,
                                                   bp.sinad, bp.ber, bp.mp, 0u);
                    emit_status("LQA bilateral RX: " + peer
                                + " SINAD=" + std::to_string(bp.sinad));
                }
            }
            pending_bilateral_valid_ = false;

            // Block A6 — successful call: bilateral data was (or wasn't) received.
            // Flush any pending response-frame word metrics BEFORE marking bilateral
            // attempted: commit_sounding_sample() stores the TIS:peer FROM-direction
            // quality so mark_bilateral_attempted finds a real entry to annotate
            // instead of creating a zero-score stub (which scores ~6 recency-only).
            commit_sounding_sample();
            if (config_.lqa_exchange_enabled && sent_ka1_ && !last_call_target_.empty())
                lqa_database_.mark_bilateral_attempted(last_call_freq_hz_, last_call_target_);
            sent_ka1_ = false; last_call_target_.clear(); last_call_freq_hz_ = 0;

            // SAM side: to_address holds the responding station.
            // JOE side: caller_address holds the calling station.
            const std::string& peer = !sm_.get_to_address().empty()
                ? sm_.get_to_address()
                : sm_.get_caller_address();
            // Manual-accept post-link gate: the handshake already completed (link
            // is up); in manual mode the operator must still Accept/Reject the
            // established link. on_link_established fires either way (the link is
            // genuinely up); the GUI uses its auto-accept setting to decide whether
            // to show the active-call panel or the pending Answer/Decline panel.
            if (config_.manual_accept_mode) {
                pending_operator_accept_ = true;
                emit_status("LINK ESTABLISHED with " + peer
                            + " — pending operator accept");
            } else {
                emit_status("LINK ESTABLISHED with " + peer);
            }
            link_start_ms_ = now_ms_;
            if (on_link_established) on_link_established(peer);
            emit_event(pal::EventType::ALE_LINK_ESTABLISHED, peer);
            break;
        }
        case OperatorEvent::CALL_REJECTED:
            // Block A6 — flush any partial response metrics before marking
            commit_sounding_sample();
            if (config_.lqa_exchange_enabled && sent_ka1_ && !last_call_target_.empty())
                lqa_database_.mark_bilateral_attempted(last_call_freq_hz_, last_call_target_);
            sent_ka1_ = false; last_call_target_.clear(); last_call_freq_hz_ = 0;
            emit_status("Call rejected by remote station (TWAS)");
            if (on_link_terminated) on_link_terminated("Call rejected");
            emit_event(pal::EventType::ALE_LINK_TERMINATED, "Call rejected");
            break;
        case OperatorEvent::NO_CHANNELS_LEFT:
            // Block A6 — flush any partial response metrics before marking
            commit_sounding_sample();
            if (config_.lqa_exchange_enabled && sent_ka1_ && !last_call_target_.empty())
                lqa_database_.mark_bilateral_attempted(last_call_freq_hz_, last_call_target_);
            sent_ka1_ = false; last_call_target_.clear(); last_call_freq_hz_ = 0;
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

        // ── Passive monitor tap: neutral decoded-word notification ──────────
        // Fires for every successfully decoded word regardless of local protocol
        // state. Does not consult self_address, expected_caller, or any SM gate.
        if (on_word_decoded)
            on_word_decoded(word, monitor_frame_id_);
    }

    // Capture caller identity as it arrives word-by-word in HANDSHAKE/WAIT_CYCLE_END.
    //
    // Protocol (A.5.2.3.2.1):  TIS:XXX [DATA:YYY [REP:ZZZ [DATA:... [REP:...]]]]
    //   - TIS = anchor word (first 3 chars, possibly @-padded)
    //   - DATA/REP alternates for chars 4-6, 7-9, 10-12, 13-15
    //   - Trailing '@' stuffing is stripped by trim_ale_address()
    //
    // Caller identity is accumulated here word-by-word but NOT emitted yet: the
    // conclusion may carry a >3-char address (TIS + DATA/REP), so emitting at the
    // TIS word would report a truncated caller (e.g. "DL3" for "DL3HC"). The
    // single on_call_received / on_amd_received emission happens in update() once
    // the conclusion has fully settled (WAIT_CYCLE_END → AWAIT_ACCEPT/SLOT_WAIT),
    // using the authoritative sm_.get_caller_address(). See maybe_emit_call_alert().
    if (sm_.get_state() == ALEState::HANDSHAKE
        && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END)
    {
        // ── Caller identity + DATA/REP AMD text (gated on word.valid) ──────
        if (word.valid) {
            const std::string chunk = trim_ale_address(word.address);

            if (word.type == PreambleType::TIS && last_caller_.empty()) {
                last_caller_ = chunk;   // conclusion anchor (first 3 chars)
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

    if (config_.lqa_exchange_enabled) {
        // ── Block A5 — CMD LQA (char 'a') bilateral RX ───────────────────────
        // Captures in HANDSHAKE/WAIT_CYCLE_END (JOE receiving SAM's CMD 'a')
        // and in CALLING/LISTENING (SAM receiving JOE's CMD 'a' in the response).
        {
            const ALEState  cur_st = sm_.get_state();
            const bool in_hs_wce  = (cur_st == ALEState::HANDSHAKE
                                      && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END);
            const bool in_ca_lst  = (cur_st == ALEState::CALLING
                                      && sm_.get_calling_phase() == CallingPhase::LISTENING);
            if (word.type == PreambleType::CMD && word.address[0] == 'a'
                    && (in_hs_wce || in_ca_lst)) {
                const Channel* ch = sm_.get_current_channel();
                if (ch && ch->rx_frequency_hz > 0) {
                    pending_bilateral_payload_  = decode_lqa_cmd(word.raw_payload);
                    pending_bilateral_valid_    = true;
                    pending_bilateral_freq_hz_  = ch->rx_frequency_hz;
                }
            }
        }

        // ── Block B4 — CMD NOISE (char 'n') RX ───────────────────────────────
        if (word.type == PreambleType::CMD && word.address[0] == 'n') {
            if (const Channel* ch = sm_.get_current_channel()) {
                const uint8_t max_db  = (word.raw_payload >> 7) & 0x7Fu;
                const uint8_t mean_db =  word.raw_payload       & 0x7Fu;
                lqa_database_.update_noise_floor(ch->rx_frequency_hz, max_db, mean_db,
                                                  word.timestamp_ms);
            }
        }

        // ── Block C5 RX — LQA Report (CMD 'r' header + DATA payloads) ────────
        if (word.type == PreambleType::CMD && word.address[0] == 'r') {
            lqa_report_decoder_.start(word.raw_payload);
        }
        if (lqa_report_decoder_.active() && word.type == PreambleType::DATA) {
            if (lqa_report_decoder_.feed(word.raw_payload)) {
                // Determine sender: SAM→JOE (CALLING/LISTENING) or JOE→SAM (HANDSHAKE/WAIT_CYCLE_END)
                const std::string sender = !sm_.get_to_address().empty()
                    ? sm_.get_to_address()
                    : sm_.get_caller_address();
                if (!sender.empty()) {
                    for (const auto& r : lqa_report_decoder_.reports())
                        lqa_database_.update_bilateral(r.frequency_hz, sender,
                                                       r.sinad, r.ber, r.mp, 0u);
                    emit_status("LQA Report RX from " + sender + ": "
                                + std::to_string(lqa_report_decoder_.reports().size()) + " channels");
                }
                lqa_report_decoder_.reset();
            }
        }
    }

    // LQA sounding: accumulate a foreign sounding frame (TIS + DATA/REP
    // extension words) received while listening. A sounding is the sender's
    // self-address conclusion (§A.5.3.1), so a >3-char self address arrives as
    // TIS:XXX + DATA:YYY [+ REP:ZZZ ...]. Capturing only the TIS word (the old
    // behaviour) truncated the address to 3 chars in the LQA DB. The full
    // address is committed once the frame settles (Tdrw of silence after the
    // last word) — see commit_sounding_sample(), driven from update().
    // unanimous_votes (0-48) and fec_errors proxy for SNR and BER; averaged
    // across the frame's words per A.5.4.1.1 ("linear average BER/LQA").
    if (word.valid && (sm_.get_state() == ALEState::SCANNING
                       || sm_.get_state() == ALEState::IDLE)) {
        const bool is_tis  = (word.type == PreambleType::TIS);
        const bool is_ext  = (word.type == PreambleType::DATA
                              || word.type == PreambleType::REP);
        if (is_tis || (is_ext && !sounding_caller_acc_.empty())) {
            const Channel* ch = sm_.get_current_channel();
            if (ch && ch->rx_frequency_hz > 0) {
                constexpr float kMaxVotes = 48.0f;
                const float snr_db = (word.unanimous_votes / kMaxVotes) * 31.0f;
                const float ber    = (word.fec_errors > 0)
                                     ? static_cast<float>(word.fec_errors) / 50.0f
                                     : 0.0f;
                // A new TIS (re)starts the frame — the conclusion is sent twice
                // (Trs redundancy), so the last copy's TIS+ext wins.
                if (is_tis) {
                    sounding_caller_acc_  = trim_ale_address(word.address);
                    sounding_freq_hz_     = ch->rx_frequency_hz;
                    sounding_word_count_  = 1;
                    sounding_snr_sum_     = snr_db;
                    sounding_ber_sum_     = ber;
                } else {
                    sounding_caller_acc_ += trim_ale_address(word.address);
                    sounding_word_count_ += 1;
                    sounding_snr_sum_    += snr_db;
                    sounding_ber_sum_    += ber;
                }
                sounding_settle_ms_ = now_ms_;  // controller clock — same as the commit check in update()
            }
        }
    }

    sm_.process_received_word(word);
}

std::string ALEController::display_state() const
{
    const ALEState st = sm_.get_state();
    if (st == ALEState::HANDSHAKE)
        return "HANDSHAKE";
    if (st == ALEState::CALLING) {
        const CallingPhase cp = sm_.get_calling_phase();
        if (cp == CallingPhase::LISTENING || cp == CallingPhase::SENDING_ACK)
            return "HANDSHAKE";
    }
    return ALEStateMachine::state_name(st);
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

void ALEController::set_automatic_sounding(bool on, uint32_t interval_sec,
                                           const std::string& net_name)
{
    auto_sounding_on_          = on && !net_name.empty();
    auto_sounding_interval_ms_ = on ? interval_sec * 1000u : 0u;
    auto_sounding_net_         = on ? net_name : std::string{};
    // Arm the first sweep for `interval` from now (don't fire immediately on
    // enable — the operator just turned it on and may still be configuring).
    auto_sounding_last_ms_     = now_ms_;
    if (auto_sounding_on_) {
        emit_status("Periodic sounding on net '" + net_name + "' every "
                    + std::to_string(interval_sec) + " s");
    } else {
        emit_status("Periodic multi-channel sounding off");
    }
}

std::vector<Channel> ALEController::resolve_net_sounding_channels(
    const std::string& net_name) const
{
    std::vector<Channel> out;
    const Net* net = net_store_.find(net_name);
    if (!net) return out;
    for (const auto& ch_id : net->channel_ids) {
        for (const auto& ch : calling_channels_) {
            if (ch.id == ch_id && ch.enabled) {
                out.push_back(ch);
                break;
            }
        }
    }
    return out;
}

void ALEController::set_sounding_interval_sec(uint32_t sec)
{
    config_.sounding_interval_sec = sec;
    AnalyzerConfig cfg = lqa_analyzer_.get_config();
    cfg.sounding_interval_ms = sec * 1000u;
    lqa_analyzer_.set_config(cfg);
}

void ALEController::set_scan_dwell_ms(uint32_t ms)
{
    config_.scan_dwell_ms = ms;
    ScanConfig cfg = sm_.get_scan_config();
    cfg.dwell_time_ms = ms;
    sm_.configure_scan(cfg);
}

void ALEController::set_link_idle_timeout_sec(uint32_t sec)
{
    config_.link_idle_timeout_sec = sec;
    TimingParameters tp = sm_.get_timing_parameters();
    tp.Twa_ms = sec * 1000u;
    sm_.set_timing_parameters(tp);
}

void ALEController::set_max_tune_time_ms(uint32_t ms)
{
    config_.max_tune_time_ms = ms;
    TimingParameters tp = sm_.get_timing_parameters();
    tp.Tt_ms = ms;
    sm_.set_timing_parameters(tp);
}

void ALEController::set_ptt_lead_ms(uint32_t ms) { config_.ptt_lead_ms = ms; }
void ALEController::set_ptt_tail_ms(uint32_t ms) { config_.ptt_tail_ms = ms; }

std::vector<std::string> ALEController::get_all_lqa_entries() const
{
    std::vector<std::string> out;
    const uint32_t now = lqa_database_.get_current_time_ms();  // same clock as LQADatabase itself
    for (const auto& e : lqa_database_.get_all_entries()) {
        const uint32_t age_ms = (now > e.last_activity_ms()) ? (now - e.last_activity_ms()) : 0u;
        // Fields: freq|station|snr_db|ber|sinad_db|score|age_ms
        //        |bilateral_sinad|bilateral_ber|bilateral_mp|display_score
        // score already incorporates the bilateral fallback (see compute_score),
        // so display_score == score; bilateral_* are shipped so the GUI can show
        // the peer-reported SINAD/BER/MP when no local FROM measurement exists.
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "%u|%s|%.1f|%.4f|%.1f|%.1f|%u|%u|%u|%u|%.1f",
                      e.frequency_hz, e.remote_station.c_str(),
                      e.snr_db, e.ber, e.sinad_db, e.score, age_ms,
                      static_cast<unsigned>(e.bilateral_sinad),
                      static_cast<unsigned>(e.bilateral_ber),
                      static_cast<unsigned>(e.bilateral_mp),
                      e.score);
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
    f << "assumed_scan_channels=" << config_.assumed_scan_channels << "\n";
    f << "golay_mode=" << static_cast<int>(config_.golay_mode) << "\n";
    f << "min_unanimous_votes=" << static_cast<int>(config_.min_unanimous_votes) << "\n";
    f << "adaptive_fec=" << (config_.adaptive_fec ? 1 : 0) << "\n";
    f << "debug_rx=" << (config_.debug_rx ? 1 : 0) << "\n";
    f << "manual_accept_mode=" << (config_.manual_accept_mode ? 1 : 0) << "\n";
    f << "manual_accept_timeout_ms=" << config_.accept_timeout_ms << "\n";
    f << "scan_dwell_ms=" << config_.scan_dwell_ms << "\n";
    f << "sounding_interval_sec=" << config_.sounding_interval_sec << "\n";
    f << "link_idle_timeout_sec=" << config_.link_idle_timeout_sec << "\n";
    f << "max_tune_time_ms=" << config_.max_tune_time_ms << "\n";
    return f.good();
}

bool ALEController::import_settings(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    ALEStationConfig cfg = config_;  // start from current defaults

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
        } else if (key == "assumed_scan_channels" || key == "target_scan_channels") {
            cfg.assumed_scan_channels = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "golay_mode") {
            cfg.golay_mode = static_cast<GolayMode>(std::stoi(val));
        } else if (key == "min_unanimous_votes") {
            cfg.min_unanimous_votes = static_cast<uint8_t>(std::stoi(val));
        } else if (key == "adaptive_fec") {
            cfg.adaptive_fec = (val == "1");
        } else if (key == "debug_rx") {
            cfg.debug_rx = (val == "1");
        } else if (key == "manual_accept_mode") {
            cfg.manual_accept_mode = (val == "1");
        } else if (key == "manual_accept_timeout_ms") {
            cfg.accept_timeout_ms = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "scan_dwell_ms") {
            cfg.scan_dwell_ms = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "sounding_interval_sec") {
            cfg.sounding_interval_sec = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "link_idle_timeout_sec") {
            cfg.link_idle_timeout_sec = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "max_tune_time_ms") {
            cfg.max_tune_time_ms = static_cast<uint32_t>(std::stoul(val));
        }
    }
    apply_config(cfg);
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
    if (!set_self_address(addr)) return false;  // validates + drives sm_
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
    if (cmd == "CMD:ACCEPT") {
        accept_call();
        return "OK: accepting call";
    }
    if (cmd == "CMD:REJECT") {
        reject_call();
        return "OK: rejecting call";
    }
    if (cmd == "CMD:SCAN" || cmd == "CMD:START_SCANNING") {
        start_scanning();
        return "OK: scanning";
    }
    if (cmd == "CMD:STOP_SCANNING") {
        start_available();
        return "OK: available (idle)";
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
            "  CMD:ACCEPT                          accept incoming call (manual-accept mode)\n"
            "  CMD:REJECT                          reject incoming call (TWAS)\n"
            "  CMD:START_SCANNING                  start channel scanning (alias: CMD:SCAN)\n"
            "  CMD:STOP_SCANNING                   stop scanning, return to IDLE (available)\n"
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
