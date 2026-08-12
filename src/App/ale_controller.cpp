/**
 * \file App/ale_controller.cpp
 */

#include "App/ale_controller.h"
#include "LQA/lqa_metrics.h"
#include "LQA/solar_position.h"
#include "PAL/logger.h"
#include "PAL/radio.h"
#include "Protocol/Control/ale_freq_select.h"
#include "Protocol/Message/ale_orderwire_protocols.h"
#include "Word/address_encoder.h"
#include "Word/ale_word.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
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
        "USB","LSB","AM","FM","FMW","CWU","CWL",
        "FSK","RTTY","USB-D","LSB-D", nullptr
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

    // first token: rx_hz, or rx_hz:tx_hz (colon-joined). format_channel_line()
    // writes rx and tx as two separate tokens, so also accept a following
    // all-digits token as tx_hz (keeps the written form self-consistent and
    // stays backward-compatible with old `rx tx mode [label]` files).
    uint32_t rx_hz = 0, tx_hz = 0;
    size_t next = 1;  // index of the next unconsumed token
    auto colon = toks[0].find(':');
    if (colon != std::string::npos) {
        rx_hz = static_cast<uint32_t>(std::stoul(toks[0].substr(0, colon)));
        const std::string tx_str = toks[0].substr(colon + 1);
        tx_hz = tx_str.empty() ? 0u : static_cast<uint32_t>(std::stoul(tx_str));
    } else {
        rx_hz = static_cast<uint32_t>(std::stoul(toks[0]));
        if (toks.size() > 1 && !toks[1].empty()
            && toks[1].find_first_not_of("0123456789") == std::string::npos) {
            tx_hz = static_cast<uint32_t>(std::stoul(toks[1]));
            next = 2;
        }
    }
    if (rx_hz == 0) return std::nullopt;

    // next token (optional): mode string
    std::string mode = "USB";
    size_t label_start = next;
    if (toks.size() > next && is_ale_mode(toks[next])) {
        mode = toks[next];
        label_start = next + 1;
    }

    ale::Channel ch(rx_hz, tx_hz, mode, mode);
    ch.id = id;

    // optional bracketed flags token [OFF,RX,IC,IS,IR,AO] (written by
    // format_channel_line). Treated as flags only when every comma-separated
    // code is a known code; otherwise the token is left as part of the label
    // (so a label that legitimately starts with "[...]" is not swallowed).
    if (label_start < toks.size() && !toks[label_start].empty()
        && toks[label_start].front() == '[' && toks[label_start].back() == ']') {
        const std::string body = toks[label_start].substr(1, toks[label_start].size() - 2);
        std::istringstream fss(body);
        std::string code;
        std::vector<std::string> codes;
        bool all_recognized = true;
        bool any_code = false;
        while (std::getline(fss, code, ',')) {
            const auto a = code.find_first_not_of(" \t");
            const auto b = code.find_last_not_of(" \t");
            if (a == std::string::npos) continue;
            code = code.substr(a, b - a + 1);
            codes.push_back(code);
            any_code = true;
            if (code != "OFF" && code != "RX" && code != "TX" && code != "IC"
                && code != "IS" && code != "IR" && code != "AO")
                all_recognized = false;
        }
        if (any_code && all_recognized) {
            for (const auto& c : codes) {
                if      (c == "OFF") ch.enabled          = false;
                else if (c == "RX")  ch.rx_only          = true;
                else if (c == "TX")  ch.tx_only          = true;
                else if (c == "IC")  ch.inhibit_calling   = true;
                else if (c == "IS")  ch.inhibit_sounding  = true;
                else if (c == "IR")  ch.inhibit_reporting = true;
                else if (c == "AO")  ch.ale_only          = true;  // A.5.4.7.1: short LBT ok
            }
            ++label_start;
        }
    }

    // remaining tokens: label
    std::string label;
    for (size_t i = label_start; i < toks.size(); ++i) {
        if (!label.empty()) label += ' ';
        label += toks[i];
    }
    ch.label = label;
    return ch;
}

// Serialize one Channel to a file line:
//   [ID:id] rx_hz tx_hz mode [flags] [label]
// where [flags] is a bracketed, comma-separated code list emitted only when any
// per-channel flag is non-default (backward-compatible: old files have no token).
// Codes: OFF (enabled=false), RX (rx_only), TX (tx_only), IC (inhibit_calling),
//         IS (inhibit_sounding), IR (inhibit_reporting), AO (ale_only — short LBT).
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
    // Per-channel flags — omitted when all default (keeps old files unchanged).
    std::string flags;
    if (!ch.enabled)            flags += "OFF,";
    if (ch.rx_only)             flags += "RX,";
    if (ch.tx_only)             flags += "TX,";
    if (ch.inhibit_calling)     flags += "IC,";
    if (ch.inhibit_sounding)    flags += "IS,";
    if (ch.inhibit_reporting)   flags += "IR,";
    if (ch.ale_only)            flags += "AO,";
    if (!flags.empty()) {
        flags.pop_back();  // drop trailing comma
        line += " [";
        line += flags;
        line += "]";
    }
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

// Split a pipe-separated string (used for CONTACT: fields).
static std::vector<std::string> split_pipe(const std::string& s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t pipe = s.find('|', start);
        const size_t end  = (pipe == std::string::npos) ? s.size() : pipe;
        out.push_back(s.substr(start, end - start));
        if (pipe == std::string::npos) break;
        start = pipe + 1;
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
    if (s == "LSB")   return pal::RadioMode::LSB;
    if (s == "CWU")   return pal::RadioMode::CW;
    if (s == "CWL")   return pal::RadioMode::CW_R;
    if (s == "FM")    return pal::RadioMode::FM;
    if (s == "FMW")   return pal::RadioMode::FMW;
    if (s == "AM")    return pal::RadioMode::AM;
    if (s == "FSK")   return pal::RadioMode::FSK;
    if (s == "RTTY")  return pal::RadioMode::RTTY;
    if (s == "LSB-D") return pal::RadioMode::DATA_LSB;
    if (s == "USB-D") return pal::RadioMode::DATA_USB;
    return pal::RadioMode::USB;
}

// Inverse of mode_from_string() — used by the radio-facing getters so the
// controller never has to keep a second copy of "current mode" itself.
static std::string mode_to_string(pal::RadioMode m) {
    switch (m) {
        case pal::RadioMode::USB:      return "USB";
        case pal::RadioMode::LSB:      return "LSB";
        case pal::RadioMode::CW:       return "CWU";
        case pal::RadioMode::CW_R:     return "CWL";
        case pal::RadioMode::FM:       return "FM";
        case pal::RadioMode::FMW:      return "FMW";
        case pal::RadioMode::AM:       return "AM";
        case pal::RadioMode::FSK:      return "FSK";
        case pal::RadioMode::RTTY:     return "RTTY";
        case pal::RadioMode::DATA_LSB: return "LSB-D";
        case pal::RadioMode::DATA_USB: return "USB-D";
        default:                       return "USB";
    }
}

} // namespace

namespace ale {

ALEController::ALEController()
    : contact_store_(sm_.get_address_book())
    , lqa_analyzer_(&lqa_database_)
    , lqa_exchange_(lqa_database_,
                    [this](const std::string& a){ return self_address_store_.matches_self(a); },
                    [this](uint32_t w){ sm_.set_pending_lqa_cmd(w); },
                    [this](ALESequence s){ sm_.set_pending_lqa_report_seq(s); })
    , freq_select_(sm_,
                   lqa_analyzer_,
                   [this](){ return self_address_store_.primary(); },
                   [this](const std::string& a){ return self_address_store_.matches_self(a); },
                   [this](const std::string& peer){ pending_relink_addr_ = peer; },
                   [this](const std::string& m){ emit_status(m); })
{
    wire_callbacks();

    // LBT occupancy (A.5.4.7.2): the SM polls this in all three LBT windows.
    // Busy transitions are surfaced to the operator via status (no silent
    // blocking); the A.5.4.7.3 override is handled inside the SM.
    sm_.set_channel_busy_query([this]() {
        // The detector's `busy` flag is the single LBT-busy truth (it already
        // encodes the LBT-enabled and RX-active gates via set_active).  The
        // GUI pill reads the same flag (lbt_busy()), so there is exactly one
        // LBT implementation; this query is just its SM-side consumer.
        const bool busy = occupancy_.is_busy();
        if (busy && !lbt_busy_reported_) {
            char buf[112];
            std::snprintf(buf, sizeof(buf),
                          "LBT: channel busy — level %.0f dB, floor %.0f dB (margin %.0f dB)",
                          occupancy_.level_db(), occupancy_.floor_db(),
                          occupancy_.margin_db());
            emit_status(buf);
        }
        lbt_busy_reported_ = busy;
        return busy;
    });

    MetricsConfig mc;
    mc.averaging_window = 1;
    lqa_db_metrics_.set_config(mc);
    lqa_db_metrics_.set_database(&lqa_database_);
    sm_.set_lqa_metrics(&lqa_db_metrics_);

    // LQA auto-sounding: fire sm_.send_sounding() when the scanner is
    // currently dwelling on a channel that has stale (or absent) LQA data.
    lqa_analyzer_.set_sounding_callback([this](uint32_t freq) {
        const Channel* ch = sm_.get_current_channel();
        // Per-channel inhibit_sounding / rx_only suppress auto-sounding on this channel.
        if (ch && ch->rx_frequency_hz == freq
            && !sounding_inhibited(freq) && !tx_inhibited(freq)) {
            apply_lbt_policy_({ *ch });   // A.5.4.7.1 LBT duration for this channel
            sm_.send_sounding();
        }
    });
}

// ── LBT occupancy (A.5.4.7) ──────────────────────────────────────────────────

void  ALEController::set_lbt_margin_db(float db) { occupancy_.set_margin_db(db); }
float ALEController::lbt_margin_db() const       { return occupancy_.margin_db(); }

// §A.5.3.3 stage-1 operator squelch — pass-through to the demodulator's detector.
void  ALEController::set_scan_squelch_enabled(bool on)   { demodulator_.set_scan_squelch_enabled(on); }
bool  ALEController::scan_squelch_enabled() const        { return demodulator_.scan_squelch_enabled(); }
void  ALEController::set_scan_detect_margin_db(float db) { demodulator_.set_scan_detect_margin_db(db); }
float ALEController::scan_detect_margin_db() const       { return demodulator_.scan_detect_margin_db(); }
float ALEController::scan_floor_db() const               { return demodulator_.scan_floor_db(); }
float ALEController::scan_floor_baseline_db() const      { return demodulator_.scan_floor_baseline_db(); }
float ALEController::calibrate_scan_detector()           { return demodulator_.calibrate_scan_detector(); }
void  ALEController::set_lbt_override(bool on)   { sm_.set_lbt_override(on); }
bool  ALEController::lbt_override() const        { return sm_.lbt_override(); }
bool  ALEController::lbt_busy() const            { return occupancy_.is_busy(); }
float ALEController::lbt_level_db() const        { return occupancy_.level_db(); }
float ALEController::lbt_floor_db() const        { return occupancy_.floor_db(); }

void ALEController::apply_lbt_policy_(const std::vector<Channel>& channels)
{
    // A.5.4.7.1: the short Twt (784 ms) is permitted only when every channel
    // involved is known to carry ALE exclusively; otherwise the LBT pause must
    // be >= 2 s.  Unknown/empty channel sets are treated as shared (spec-safe).
    bool all_ale_only = !channels.empty();
    for (const auto& ch : channels)
        if (!ch.ale_only) { all_ale_only = false; break; }
    sm_.set_lbt_shared(!all_ale_only);
}

void ALEController::notify_channel_changed_(const Channel& ch)
{
    // The detector's EWMA floor is channel-specific.  When the RX frequency
    // changes, drop the floor/busy/vote so the new channel is measured on its
    // own terms instead of inheriting a stale busy from the previous channel
    // (which during scan hops — RX stays active, no set_active edge — could
    // otherwise keep the pill on FREQ BUSY until the floor slowly re-tracked).
    // A same-frequency notification (channel rename) leaves the detector alone.
    if (ch.rx_frequency_hz > 0 && ch.rx_frequency_hz != last_lbt_rx_hz_) {
        occupancy_.reset();
        last_lbt_rx_hz_ = ch.rx_frequency_hz;
    }
    demodulator_.mark_channel_hop();  // §A.5.3.3 stage-1: arm new-channel guard
    dispatch(pal::EventType::CHANNEL_CHANGED, ch.id, 0, &ch, sizeof(ch));
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
        // Passive TX monitor: notify exactly once per SM-emitted word, at emit
        // time, regardless of PTT-lead buffering. Pairs with on_word_decoded so
        // the ALE Monitor shows sent and received words with the same layout.
        {
            ale::WordData wd{ WordParser::word_type_name(w.type), w.address,
                              tx_word_seq_++, w.unanimous_votes, w.fec_errors,
                              w.timestamp_ms, get_current_channel().tx_frequency_hz };
            dispatch(pal::EventType::ALE_WORD_TX, "", 0, &wd, sizeof(wd));
        }

        if (ptt_lead_deadline_ms_ > 0) {
            pending_tx_words_.push_back({ w, audio_device_ != nullptr });
            return;
        }
        modulator_.enqueue_word(w);
        if (audio_device_) {
            // Diagnostic (2026-08-07): confirms this word's audio was actually
            // rendered by the DAC (arm_frame_complete only fires once
            // frames_rendered_ reaches this word's target — real playback
            // position, not mere submission; see WasapiDevice::service_render).
            // Fires on the main thread (tick()), safe to log.
            const char* type_name = WordParser::word_type_name(w.type);
            const std::string addr = w.address;
            audio_device_->arm_frame_complete([this, type_name, addr]() {
                pal::log_trace("TXWord", "rendered %s [%s]", type_name, addr.c_str());
                sm_.on_word_complete();
            });
        }
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

    // SM frame assembler → passive monitor (on_frame_decoded only).
    // Per-word display taps fire from rx_track_signal_quality() in strict
    // on-air arrival order — never reordered or deferred to frame-assembly
    // time. The assembler concludes a frame on the first TIS/TWAS, so a
    // repeating sound (TWAS DATA TWAS DATA …) would otherwise group every
    // later frame as [DATA, TWAS] and display DATA before TWAS.
    sm_.set_frame_assembled_callback([this](const ALEMessage& frame) {
        const uint32_t fid = monitor_frame_id_++;
        ale::FrameData fd{ fid,
                           CallTypeDetector::call_type_name(frame.call_type),
                           frame.from_address.c_str(),
                           frame.words.size(),
                           frame.start_time_ms,
                           frame.duration_ms,
                           get_current_channel().rx_frequency_hz,
                           &frame.to_addresses };
        dispatch(pal::EventType::ALE_FRAME_DECODED, "", 0, &fd, sizeof(fd));
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
        // transition_to() calls this defensively on entry to CALLING/HANDSHAKE/
        // LINKED/SOUNDING regardless of whether RX was already in that state
        // (e.g. IDLE/SCANNING, already RX-on, → HANDSHAKE on an incoming call;
        // WAIT_ACK, already RX-on, → LINKED). Without this guard a redundant
        // true→true call still arms ptt_tail_deadline_ms_ for a release that
        // never had a matching PTT-on, so tick_ptt_timing() later fires a
        // spurious PTT_OFF event and calls extend_peer_wait_window_for_ptt_
        // release_delay() with a bogus delay, corrupting whatever peer-wait
        // timer (WAIT_ACK/LISTENING) happens to be live by then — surfacing as
        // PTT appearing to toggle several times for a single logical TX/RX
        // cycle (seen e.g. around AMD calls, which exercise both transitions).
        if (rx_on == sm_rx_enabled_) return;
        sm_rx_enabled_ = rx_on;
        if (manual_ptt_) {
            if (!rx_on) demodulator_.set_enabled(false);
            dispatch(rx_on ? pal::EventType::PTT_OFF : pal::EventType::PTT_ON);
            return;
        }
        if (rx_on) {
            // TX→RX: cancel any pending lead, apply tail
            ptt_lead_deadline_ms_ = 0;
            pending_tx_words_.clear();
            // arm_frame_complete's "rendered" only proves the last word left
            // openALE's own WASAPI buffer — on shared-mode audio the engine's
            // own mix/forward stage adds further latency before real DAC
            // output (see IAudioDriver::output_latency_ms()). Fold that into
            // the tail automatically so a configured ptt_tail_ms=0 doesn't
            // silently drop it; the user-configured value still covers
            // whatever is downstream of the sound card (radio interface,
            // virtual audio cable, SDR software, etc.), which openALE has no
            // way to measure.
            const uint32_t effective_tail_ms = config_.ptt_tail_ms
                + (audio_device_ ? audio_device_->output_latency_ms() : 0);
            if (effective_tail_ms > 0) {
                ptt_tail_deadline_ms_    = now_ms_ + effective_tail_ms;
                ptt_tail_armed_delay_ms_ = effective_tail_ms;
                // actual PTT release + demod enable happen in update()
            } else {
                set_ptt_and_notify(false);
                // tx_only (Direction=TX): keep RX disabled — transmit-only channel.
                if (!rx_inhibited(get_current_frequency()))
                    demodulator_.set_enabled(true);
                else
                    emit_status("RX disabled — current channel is TX-only (Direction=TX)");
            }
        } else {
            // RX→TX: the SM wants to transmit. rx_only (Direction=RX) is a hard
            // TX prohibition — refuse to key PTT and keep RX enabled so the
            // station keeps listening. The SM is mid-transition (this callback
            // fires synchronously inside sm_.update()), so we cannot abort it
            // here without re-entering the SM; set a flag and abort on the next
            // tick (tick_sm → emergency_manual_control → IDLE, no TWAS in
            // HANDSHAKE — the only autonomous-TX path reachable on a rx_only
            // channel since initiate_call is already gated).
            const Channel cur_tx_ch = get_current_channel();
            if (cur_tx_ch.rx_frequency_hz > 0 && tx_inhibited(cur_tx_ch.rx_frequency_hz)) {
                abort_tx_pending_ = true;
                emit_status("Incoming call not answered — channel " + cur_tx_ch.id
                            + " is RX-only (Direction=RX); TX suppressed");
                dispatch(pal::EventType::PTT_OFF);
                return;
            }
            // RX→TX: cancel any pending tail, assert PTT, start lead
            ptt_tail_deadline_ms_ = 0;
            demodulator_.set_enabled(false);
            set_ptt_and_notify(true);
            ptt_lead_deadline_ms_ = (config_.ptt_lead_ms > 0)
                ? now_ms_ + config_.ptt_lead_ms : 0;
        }
        // Every branch above already dispatches PTT_ON/PTT_OFF itself, via
        // set_ptt_and_notify() at the point the radio is actually commanded
        // (immediately here, or later from tick_ptt_timing() when ptt_tail_ms
        // defers the real release) — no unconditional trailing dispatch here,
        // or the ptt_tail_ms>0 path would emit a premature PTT_OFF event before
        // the hardware has actually released.
    });

    // Idle-timeout warning (Twa lead) → forward to the controller callback so
    // the bridge can push an `idle_warning` event to the GUI popup.
    sm_.set_idle_warning_callback([this](uint32_t remaining_sec) {
        dispatch(pal::EventType::ALE_IDLE_WARNING, "", static_cast<int32_t>(remaining_sec));
    });

    // §A.5.3.3 stage 1: ALE energy on the current channel → open SCAN_PAUSE
    // before a fully-decoded word arrives.  Completely separate from LBT — this is
    // the RX-scan detection path; lbt_channel_busy_() / set_channel_busy_query() are untouched.
    demodulator_.set_ale_energy_callback([this]() {
        // Ignore energy while a tune is still in flight: mid-tune audio is from
        // the previous channel, so a detection then would pause on the wrong
        // channel.  (The modem's 80 ms HOP_GUARD covers the common case; this
        // makes it robust when settle latency exceeds that guard.)
        if (radio_ && !radio_->is_tune_settled()) return;
        sm_.begin_scan_pause(now_ms_);
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
        schedule_mode_verify();
        notify_channel_changed_(ch);
    });
}

// ── Radio control ─────────────────────────────────────────────────────────────

void ALEController::set_radio(pal::IRadio* r)
{
    radio_ = r;
    // Gate the scanner's hop on the radio actually having settled on the channel.
    // is_tune_settled() is true for sync backends (mocks, blocking serial radios)
    // so the gate is a no-op there; async backends (HamlibRadio) return false
    // while a tune is in flight.  This withholds the next hop until the current
    // tune settled — which keeps "at most one tune in flight" (a Stop or ALE-
    // scan pause halts the radio within one physical tune) and restores the
    // spec dwell cadence, since the dwell is measured from the hop and the tune
    // latency overlaps it instead of being added on top.
    sm_.set_hop_ready_query([this]() {
        return !radio_ || radio_->is_tune_settled();
    });
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

const Channel* ALEController::find_channel_by_freq(uint32_t rx_hz) const
{
    if (rx_hz == 0) return nullptr;
    for (const auto& ch : calling_channels_)
        if (ch.rx_frequency_hz == rx_hz) return &ch;
    return nullptr;
}

bool ALEController::reporting_inhibited(uint32_t rx_hz) const
{
    const Channel* ch = find_channel_by_freq(rx_hz);
    return ch && ch->inhibit_reporting;
}

bool ALEController::sounding_inhibited(uint32_t rx_hz) const
{
    const Channel* ch = find_channel_by_freq(rx_hz);
    return ch && ch->inhibit_sounding;
}

bool ALEController::calling_inhibited(uint32_t rx_hz) const
{
    const Channel* ch = find_channel_by_freq(rx_hz);
    return ch && ch->inhibit_calling;
}

bool ALEController::tx_inhibited(uint32_t rx_hz) const
{
    const Channel* ch = find_channel_by_freq(rx_hz);
    return ch && ch->rx_only;
}

bool ALEController::rx_inhibited(uint32_t rx_hz) const
{
    const Channel* ch = find_channel_by_freq(rx_hz);
    return ch && ch->tx_only;
}

bool ALEController::set_frequency(uint32_t hz)
{
    if (!radio_ || hz == 0) return false;
    radio_->set_frequency(hz);
    schedule_mode_verify();
    {   pal::Channel pc = radio_->get_channel();
        notify_channel_changed_(Channel(pc.rx_frequency, pc.tx_frequency,
                                        mode_to_string(pc.rx_mode), mode_to_string(pc.tx_mode)));
    }
    return true;
}

bool ALEController::set_mode(const std::string& mode)
{
    if (!radio_ || !is_ale_mode(mode)) return false;
    radio_->set_mode(mode_from_string(mode));
    schedule_mode_verify();
    {   pal::Channel pc = radio_->get_channel();
        notify_channel_changed_(Channel(pc.rx_frequency, pc.tx_frequency,
                                        mode_to_string(pc.rx_mode), mode_to_string(pc.tx_mode)));
    }
    return true;
}

bool ALEController::set_vfo_channel(uint32_t hz, const std::string& mode)
{
    if (!radio_ || hz == 0) return false;

    // set_channel() sends freq first, mode last. An SDR front-end (Quisk) restores
    // its per-band saved mode asynchronously after the frequency command — often
    // after assert_mode()'s sub-ms readback loop has already returned. The explicit
    // set_mode() call below re-asserts mode after the frequency is stable: no
    // frequency is sent, so no second band-restore is triggered. Mirrors step_channel().
    pal::Channel pc = radio_->get_channel();
    pc.rx_frequency = hz;
    pc.tx_frequency = hz;                       // simplex
    if (!mode.empty())
        pc.rx_mode = pc.tx_mode = mode_from_string(mode);
    radio_->set_channel(pc);
    radio_->set_mode(pc.tx_mode);
    schedule_mode_verify();

    {   pal::Channel cur = radio_->get_channel();
        notify_channel_changed_(Channel(cur.rx_frequency, cur.tx_frequency,
                                        mode_to_string(cur.rx_mode), mode_to_string(cur.tx_mode)));
    }
    return true;
}

bool ALEController::step_channel(int direction)
{
    if (!radio_ || calling_channels_.empty()) return false;

    // Build the channel set to step through: the active net's assigned channels
    // when a net is selected, otherwise all calling_channels_. Mirrors the scoping
    // applied to start_scanning() / initiate_call() so manual stepping, scanning,
    // sounding and calling all iterate the same net's channels. The net's
    // membership (channel_ids) is the set the operator configured in Settings.
    std::vector<Channel> step_set;
    if (!active_scan_net_.empty()) {
        if (const Net* net = net_store_.find(active_scan_net_)) {
            for (const auto& ch : calling_channels_)
                if (std::find(net->channel_ids.begin(), net->channel_ids.end(), ch.id)
                        != net->channel_ids.end())
                    step_set.push_back(ch);
        }
    }
    if (step_set.empty()) {
        // No active net, or the net has no assigned channels → step through all.
        for (const auto& ch : calling_channels_) step_set.push_back(ch);
    }
    if (step_set.empty()) return false;

    const int n = static_cast<int>(step_set.size());
    // Position = the radio's current channel if it is in the step set, else 0.
    // Frequency-match is robust to external retunes and net-membership changes
    // (an index cached across calls would be invalidated by either).
    pal::Channel cur = radio_->get_channel();
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (step_set[i].rx_frequency_hz == cur.rx_frequency) { idx = i; break; }
    idx = (idx + direction % n + n) % n;

    const Channel& target = step_set[idx];
    pal::Channel pc;
    pc.rx_frequency = target.rx_frequency_hz;
    pc.tx_frequency = target.effective_tx_hz();
    pc.rx_mode      = mode_from_string(target.rx_mode);
    pc.tx_mode      = mode_from_string(target.tx_mode);
    pc.power        = target.power_pct;
    pc.antenna      = target.antenna;

    radio_->set_channel(pc);
    // After a frequency change Quisk's band-restore may fire asynchronously
    // after assert_mode()'s sub-ms retries. A mode-only re-assertion here
    // catches any revert that has already landed: no frequency is sent, so
    // no second band-restore is triggered — identical to the panel-button path.
    radio_->set_mode(pc.tx_mode);
    schedule_mode_verify();
    notify_channel_changed_(target);
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
    schedule_mode_verify();
    notify_channel_changed_(Channel(pc.rx_frequency, pc.tx_frequency,
                                    mode_to_string(pc.rx_mode), mode_to_string(pc.tx_mode)));
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
    set_sounding_use_twas(cfg.sounding_use_twas);
    set_sounding_warning_lead_sec(cfg.sounding_warning_lead_sec);
    set_link_idle_timeout_sec(cfg.link_idle_timeout_sec);
    set_test_channel_link_hold_time(cfg.test_channel_link_hold_time);
    set_max_tune_time_ms(cfg.max_tune_time_ms);
    set_ptt_lead_ms(cfg.ptt_lead_ms);
    set_ptt_tail_ms(cfg.ptt_tail_ms);
    config_.assumed_scan_channels        = cfg.assumed_scan_channels;
    config_.relink_enabled               = cfg.relink_enabled;
    config_.relink_improvement_threshold = cfg.relink_improvement_threshold;
    config_.enhanced_freq_select         = cfg.enhanced_freq_select;
    config_.position_source              = cfg.position_source;
    config_.station_lat_deg              = cfg.station_lat_deg;
    config_.station_lon_deg              = cfg.station_lon_deg;
    config_.grid_locator                 = cfg.grid_locator;
    config_.gpsd_host                    = cfg.gpsd_host;
    config_.gpsd_port                    = cfg.gpsd_port;
    config_.nmea_port                    = cfg.nmea_port;
    config_.nmea_baud                    = cfg.nmea_baud;
    config_.sfi_enabled                  = cfg.sfi_enabled;
    update_propagation_context();
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

    // Rebuild scan list from calling_channels_ (the single source of truth).
    // add_channel/del_channel keep calling_channels_ current but only push to
    // sm_.set_calling_channels(); the SM's channel_manager_ scan_list is never
    // touched there, so we populate it fresh every time scanning starts.
    ScanConfig cfg = sm_.get_scan_config();
    cfg.scan_list.clear();

    const Net* scan_net = active_scan_net_.empty() ? nullptr : net_store_.find(active_scan_net_);
    if (scan_net) {
        cfg.dwell_time_ms = scan_net->dwell_ms;
        for (const auto& ch : calling_channels_) {
            if (!ch.enabled) continue;
            if (ch.tx_only) continue;   // tx_only (Direction=TX): can't receive — not scannable
            if (std::find(scan_net->channel_ids.begin(), scan_net->channel_ids.end(), ch.id)
                    == scan_net->channel_ids.end()) continue;
            cfg.scan_list.push_back(ch);
        }
        if (cfg.scan_list.empty()) {
            // Active net has no enabled channels — fall back to all enabled.
            cfg.dwell_time_ms = config_.scan_dwell_ms;
            for (const auto& ch : calling_channels_)
                if (ch.enabled && !ch.tx_only) cfg.scan_list.push_back(ch);
        }
    } else {
        for (const auto& ch : calling_channels_)
            if (ch.enabled && !ch.tx_only) cfg.scan_list.push_back(ch);
    }

    // A.5.4.5.3: sort scan channels by FROM-direction quality.
    // FROM score = locally measured SINAD from received soundings, stored
    // in the sounding entry (empty station key) sinad_db field.
    if (!cfg.scan_list.empty()) {
        std::stable_sort(cfg.scan_list.begin(), cfg.scan_list.end(),
            [this](const Channel& a, const Channel& b) {
                auto ea = lqa_database_.get_entry(a.rx_frequency_hz, "");
                auto eb = lqa_database_.get_entry(b.rx_frequency_hz, "");
                float sa = (ea && ea->sinad_db > 0.0f) ? ea->sinad_db : 0.0f;
                float sb = (eb && eb->sinad_db > 0.0f) ? eb->sinad_db : 0.0f;
                return sa > sb;  // higher FROM-quality first
            });
        sm_.configure_scan(cfg);
    }

    sm_.process_event(ALEEvent::START_SCAN);
}

bool ALEController::send_sounding()
{
    if (tx_inhibited(get_current_frequency())) {
        emit_status("Manual sounding rejected — current channel is RX-only (Direction=RX)");
        return false;
    }
    if (sounding_inhibited(get_current_frequency())) {
        emit_status("Manual sounding rejected — current channel is inhibit-sounding");
        return false;
    }
    // A.5.4.7.1 LBT duration from the sounding channel's ale_only flag
    // (unknown channel → shared → >= 2 s, spec-safe).
    const Channel* cc = find_channel_by_freq(get_current_frequency());
    apply_lbt_policy_(cc ? std::vector<Channel>{ *cc } : std::vector<Channel>{});
    // Re-arm LBT (see initiate_call): drop a stale busy latch from the last
    // 400 ms of dwelling so SoundingPhase::LBT measures fresh; keep the floor.
    occupancy_.clear_busy_latch();
    // Sounding uses the same "call width" C as calling (Tsrs = (C+2)·Ta) — take it
    // from the active scan net's calling_length_c, falling back to the global
    // assumed_scan_channels.  See resolve_sounding_C() and handle_sounding().
    sm_.set_target_scan_channels(resolve_sounding_C(active_scan_net_));
    if (sm_.get_self_address().empty()) {
        emit_status("Manual sounding rejected — no callsign configured");
        return false;
    }
    if (!sm_.send_sounding()) {
        emit_status("Manual sounding rejected — only available while IDLE or scanning");
        return false;
    }
    emit_status("Manual sounding — transmitting on current channel");
    return true;
}

bool ALEController::send_sounding_sweep(const std::vector<Channel>& channels)
{
    apply_lbt_policy_(channels);   // A.5.4.7.1: short Twt only if ALL are ale_only
    // Re-arm LBT (see initiate_call): drop a stale busy latch before the sweep's
    // LBT window; keep the tracked floor so a real signal re-latches in Twt.
    occupancy_.clear_busy_latch();
    if (!sm_.send_sounding_sweep(channels)) {
        emit_status("Sounding sweep rejected — only available while IDLE or scanning");
        return false;
    }
    // Re-arm the auto-sounding timer so this manual sweep counts as "just sounded".
    // Without this, an overdue timer fires again the moment the SM returns to IDLE,
    // causing an immediate double-sounding.
    auto_sounding_last_ms_ = now_ms_;
    sounding_warning_sent_ = false;
    if (sounding_warning_active_) {
        sounding_warning_active_ = false;
        ale::SoundingWarningData swd{ auto_sounding_net_.c_str(), 0, "cancel" };
        dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
    }
    emit_status("Sounding sweep — " + std::to_string(channels.size())
                + " channel(s)");
    return true;
}


bool ALEController::initiate_call(const std::string& target_addr)
{
    // TODO A.5.4.5.2: when one-way broadcast mode is added, pass
    // SelectionMode::BROADCAST to rank_channels_for_call() here.
    if (!is_valid_ale_address(target_addr)) {
        emit_status("ERROR: address '" + target_addr
                    + "' invalid — must be 3–15 Basic-38 characters (A-Z, 0-9, @, ?)");
        return false;
    }

    // Channel ordering: always use LQA data when available (A.5.4.5, mandatory).
    // Per-channel inhibit_calling excludes a channel from the outbound call
    // set (it may still scan / sound). rx_only (Direction=RX) excludes a channel
    // from calling too — it is a hard TX prohibition. Build the callable subset
    // first. If the channel list is non-empty but every channel is filtered out,
    // abort; an empty list (no channels configured) is left to proceed as before
    // so downstream behaviour is unchanged.
    std::vector<Channel> callable;
    callable.reserve(calling_channels_.size());
    for (const auto& ch : calling_channels_)
        if (!ch.inhibit_calling && !ch.rx_only) callable.push_back(ch);
    if (!calling_channels_.empty() && callable.empty()) {
        emit_status("No callable channels — all channels inhibit-calling or RX-only");
        return false;
    }

    // Scope the outbound channel sweep to the active net when one is selected
    // (mirrors start_scanning()). The operator's Network pill drives this. A
    // misconfigured net with no callable channels falls back to all callable
    // channels so calling is never blocked by the selection. C (target scan
    // channels) is left to the contact's-net resolution below — it is a peer
    // property, not ours.
    if (!active_scan_net_.empty()) {
        if (const Net* net = net_store_.find(active_scan_net_)) {
            std::vector<Channel> scoped;
            scoped.reserve(callable.size());
            for (const auto& ch : callable)
                if (std::find(net->channel_ids.begin(), net->channel_ids.end(), ch.id)
                        != net->channel_ids.end())
                    scoped.push_back(ch);
            if (!scoped.empty()) {
                callable = std::move(scoped);
                emit_status("Call scoped to net '" + active_scan_net_ + "' ("
                            + std::to_string(callable.size()) + " channel(s))");
            } else {
                emit_status("Active net '" + active_scan_net_
                            + "' has no callable channels — using all");
            }
        }
    }

    uint32_t first_call_freq_hz = 0u;
    if (!callable.empty()) {
        first_call_freq_hz = callable.front().rx_frequency_hz;
        const bool has_station_data =
            !lqa_analyzer_.rank_channels_for_station(target_addr).empty();
        auto ordered = lqa_analyzer_.rank_channels_for_call(target_addr, callable);
        sm_.set_calling_channels(ordered);
        first_call_freq_hz = ordered.front().rx_frequency_hz;
        if (has_station_data)
            emit_status("LQA: channel order optimised for " + target_addr
                        + " (best: " + std::to_string(first_call_freq_hz) + " Hz)");
    } else {
        // No channels configured — pass the (empty) list through unchanged so
        // the SM handles the no-channel case as it did before inhibit-gating.
        sm_.set_calling_channels(calling_channels_);
    }

    uint32_t C = config_.assumed_scan_channels;
    if (const Contact* ct = contact_store_.find(target_addr))
        for (const auto& nm : ct->net_members)
            if (const Net* n = net_store_.find(nm)) { C = n->calling_length_c; break; }
    sm_.set_target_scan_channels(C);
    emit_status("Scanning call: C=" + std::to_string(C) + " channel(s)");

    // Block A4 — Queue CMD LQA (KA1=1) for the calling station's frame.
    // Gated on lqa_exchange_enabled and the channel's inhibit_reporting flag
    // (bilateral LQA CMD 'a' exchange suppressed per-channel). Channel ranking
    // above is always active.
    if (config_.lqa_exchange_enabled && !reporting_inhibited(first_call_freq_hz))
        lqa_exchange_.encode_outgoing(first_call_freq_hz, target_addr, true);
    else if (config_.lqa_exchange_enabled && reporting_inhibited(first_call_freq_hz))
        emit_status("LQA CMD 'a' exchange suppressed on this call — channel is inhibit-reporting");

    // A.5.4.7.1 LBT duration: short Twt only when every callable channel of
    // this call is marked ALE-only; any shared channel → >= 2 s pause.
    apply_lbt_policy_(callable);

    // Re-arm the LBT decision: drop a busy latch left over from the last ~400 ms
    // of idle dwelling so the SM's LBT window measures fresh (a transient hot
    // block before PTT must not abort the call on tick 0).  The tracked noise
    // floor is kept — a genuinely busy channel re-latches within Twt.  See
    // ChannelOccupancyDetector::clear_busy_latch().
    occupancy_.clear_busy_latch();

    emit_status("Initiating call to " + target_addr);
    if (sm_.initiate_call(target_addr)) {
        dispatch(pal::EventType::ALE_CALL_SENT, target_addr);
        return true;
    }
    return false;
}

bool ALEController::initiate_single_channel_call(const std::string& target_addr)
{
    if (!is_valid_ale_address(target_addr)) {
        emit_status("ERROR: address '" + target_addr
                    + "' invalid — must be 3–15 Basic-38 characters (A-Z, 0-9, @, ?)");
        return false;
    }
    Channel cur = get_current_channel();
    // rx_only (Direction=RX) is a hard TX prohibition: the operator-override
    // single-channel call bypasses inhibit_calling but NOT rx_only — a receive-
    // only channel cannot place a call. get_current_channel() is radio-backed and
    // does not carry the flags, so resolve the configured channel by frequency.
    if (tx_inhibited(cur.rx_frequency_hz)) {
        emit_status("Single-channel call rejected — channel " + cur.id
                    + " is RX-only (Direction=RX)");
        return false;
    }
    sm_.set_calling_channels({ cur });
    // cur is radio-backed and carries no flags; resolve the configured channel
    // for the A.5.4.7.1 ale_only policy (unresolved → shared → >= 2 s LBT).
    const Channel* cfg_ch = find_channel_by_freq(cur.rx_frequency_hz);
    apply_lbt_policy_(cfg_ch ? std::vector<Channel>{ *cfg_ch } : std::vector<Channel>{});
    // Re-arm LBT (see initiate_call): drop a stale busy latch before the SM's
    // LBT window, keep the floor.  Also covers the operator-override path that
    // tick_test_channel() does NOT pre-clear via set_vfo_channel().
    occupancy_.clear_busy_latch();
    uint32_t C = config_.assumed_scan_channels;
    if (const Contact* ct = contact_store_.find(target_addr))
        for (const auto& nm : ct->net_members)
            if (const Net* n = net_store_.find(nm)) { C = n->calling_length_c; break; }
    sm_.set_target_scan_channels(C);
    // Operator-override single-channel call: inhibit_calling does NOT block it
    // (the operator explicitly chose this channel), but inhibit_reporting still
    // suppresses the bilateral LQA CMD 'a' exchange for this channel.
    if (config_.lqa_exchange_enabled && !reporting_inhibited(cur.rx_frequency_hz))
        lqa_exchange_.encode_outgoing(cur.rx_frequency_hz, target_addr, true);
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

    uint32_t C = config_.assumed_scan_channels;
    [&]() {
        for (const auto& m : members)
            if (const Contact* ct = contact_store_.find(m))
                for (const auto& nm : ct->net_members)
                    if (const Net* n = net_store_.find(nm)) { C = n->calling_length_c; return; }
    }();
    sm_.set_target_scan_channels(C);
    emit_status("Scanning call: C=" + std::to_string(C) + " channel(s)");

    // Scope the outbound sweep to the active net when one is selected (mirrors
    // initiate_call / start_scanning). The SM's calling channel set is whatever
    // was last set; set it explicitly so the group call actually sweeps the
    // active net's channels. Empty net → fall back to all callable channels.
    {
        std::vector<Channel> callable;
        callable.reserve(calling_channels_.size());
        for (const auto& ch : calling_channels_)
            if (!ch.inhibit_calling && !ch.rx_only) callable.push_back(ch);
        if (!active_scan_net_.empty()) {
            if (const Net* net = net_store_.find(active_scan_net_)) {
                std::vector<Channel> scoped;
                scoped.reserve(callable.size());
                for (const auto& ch : callable)
                    if (std::find(net->channel_ids.begin(), net->channel_ids.end(), ch.id)
                            != net->channel_ids.end())
                        scoped.push_back(ch);
                if (!scoped.empty()) callable = std::move(scoped);
            }
        }
        if (!callable.empty()) sm_.set_calling_channels(callable);
    }

    emit_status("Initiating group call (" + std::to_string(members.size()) + " members)");
    // Re-arm LBT (see initiate_call): drop a stale busy latch before the SM's
    // LBT window; keep the tracked floor so a real signal re-latches in Twt.
    occupancy_.clear_busy_latch();
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

std::string ALEController::active_peer() const
{
    if (sm_.get_state() != ALEState::LINKED) return {};
    const std::string& to = sm_.get_to_address();
    return !to.empty() ? to : sm_.get_caller_address();
}

std::string ALEController::send_amd(const std::string& target, const std::string& text)
{
    if (text.empty())
        return "ERROR: AMD requires message text (max 90 chars, Expanded-64)";

    // ── LINKED: send AMD over the established link as a single-burst ─────────
    // orderwire frame.  TO[peer] (+DATA/REP ext) + CMD AMD + message + TIS self
    // (the TIS:SELF conclusion is appended by trigger_linked_orderwire()).
    if (sm_.get_state() == ALEState::LINKED) {
        const std::string peer = active_peer();
        if (peer.empty())
            return "ERROR: LINKED but no active peer address available";
        std::vector<ALEWord> words = AddressEncoder::encode(peer, PreambleType::TO);
        const auto amd = encode_amd(text);   // authoritative encoder (sanitise/truncate)
        if (amd.empty())
            return "ERROR: AMD text has no encodable characters";
        words.insert(words.end(), amd.begin(), amd.end());
        // Single-burst (false): a doubled AMD frame would display the text twice.
        sm_.trigger_linked_orderwire(words, /*double_burst=*/false);
        sm_.on_link_activity();   // reset Twa while the burst is queued
        return "OK: AMD sent over linked orderwire to " + peer;
    }

    // ── Not LINKED: queue AMD as the pending message and place a call. ───────
    // A.5.7.2.2: AMD is sent in the ACK frame (third handshake frame), not the calling frame.
    if (!is_valid_ale_address(target))
        return "ERROR: target address invalid — 3–15 Basic-38 chars (A-Z, 0-9, @, ?)";
    ALEStateMachine::PendingMessage msg;
    msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
    msg.content = text;   // encode_amd() sanitises/truncates downstream in the SM
    sm_.set_pending_message(msg);
    if (!initiate_call(target))
        return std::string("ERROR: cannot call in state ")
               + ALEStateMachine::state_name(state());
    return "OK: AMD queued, calling " + target;
}

void ALEController::emergency_stop()
{
    emit_status("EMERGENCY STOP — aborting all ALE operations");
    sm_.emergency_manual_control();   // SM → IDLE (sends TWAS if LINKED)
    pending_tx_words_.clear();        // drop buffered words not yet sent to modulator
    modulator_.abort();               // flush modulator TX queue
    ptt_lead_deadline_ms_ = 0;
    ptt_tail_deadline_ms_ = 0;
    manual_ptt_ = false;
    set_ptt_and_notify(false);        // PTT → RX
    demodulator_.set_enabled(true);   // re-arm demodulator
}

void ALEController::set_ptt_and_notify(bool on)
{
    if (radio_) radio_->set_ptt(on);
    dispatch(on ? pal::EventType::PTT_ON : pal::EventType::PTT_OFF);
}

void ALEController::set_manual_ptt(bool on)
{
    manual_ptt_ = on;
    if (on && tx_inhibited(get_current_frequency())) {
        // rx_only (Direction=RX): manual PTT is a transmit — refuse and stay RX.
        manual_ptt_ = false;
        emit_status("Manual PTT rejected — current channel is RX-only (Direction=RX)");
        return;
    }
    if (on) {
        // Immediate TX override: cancel pending timing, disable demod, assert PTT.
        // Also abort any active protocol operation so the SM stops queuing new words.
        // LINKED is excluded — PTT during a link is for manual voice TX, not abort.
        ptt_tail_deadline_ms_ = 0;
        ptt_lead_deadline_ms_ = 0;
        pending_tx_words_.clear();
        demodulator_.set_enabled(false);
        set_ptt_and_notify(true);
        {
            const ALEState s = sm_.get_state();
            if (s == ALEState::CALLING || s == ALEState::HANDSHAKE ||
                s == ALEState::SOUNDING) {
                sm_.emergency_manual_control();
                // emergency_manual_control() fires rx_enabled_callback(true) via
                // enter_state(IDLE), but manual_ptt_ is true so the callback emits
                // only the PTT_OFF event without actually releasing PTT — PTT stays
                // asserted until the operator releases via set_manual_ptt(false).
            }
        }
    } else {
        // Restore SM control: release PTT only if SM currently wants RX
        if (sm_rx_enabled_) {
            set_ptt_and_notify(false);
            demodulator_.set_enabled(true);
        }
        // If SM wants TX, the rx_enabled_callback already manages PTT — nothing to do
    }
    dispatch(on ? pal::EventType::PTT_ON : pal::EventType::PTT_OFF);
}

// ── Main-loop drivers ─────────────────────────────────────────────────────────

void ALEController::update(uint32_t now_ms)
{
    now_ms_ = now_ms;

    // LBT occupancy (A.5.4.7.2): the detector's `busy` flag is the single
    // LBT-busy truth, read by both the SM's LBT decision and the GUI pill.
    // Sync its active state once per tick to "LBT enabled AND an RX stream is
    // feeding the demodulator".  When either condition drops (operator switch
    // or every TX closes RX), the detector clears its busy flag — so the pill
    // can never latch on FREQ BUSY while no detection is actually running.
    // Gate: LBT enabled AND demodulator is running (ALE TX disables it) AND
    // voice PTT is not active (voice TX also feeds the VAC loopback which
    // would poison the floor — same treatment as ALE TX).
    occupancy_.set_active(lbt_occupancy_enabled_ && demodulator_.enabled() && !voice_tx_active_);

    // Diagnostic: log once when the post-reactivation floor-relearn window
    // (see ChannelOccupancyDetector::finish_block_) finishes settling, comparing
    // the floor just before this TX (pre-TX baseline) against where it settled.
    // A settled floor near 0 dB points at a radio PTT-release mute/squelch tail
    // being mistaken for the new ambient floor; a settled floor close to the
    // pre-TX value with busy still true points elsewhere (relearn not helping,
    // or a genuine AGC pump larger than the settling window can absorb).
    {
        const bool relearning_now = occupancy_.relearning();
        if (lbt_relearn_was_active_ && !relearning_now) {
            pal::log_trace("LBT",
                            "relearn: pre-TX floor=%.0f dB -> settled floor=%.0f dB "
                            "(level=%.0f dB, margin=%.0f dB, busy=%s)",
                            occupancy_.floor_before_relearn_db(), occupancy_.floor_db(),
                            occupancy_.level_db(), occupancy_.margin_db(),
                            occupancy_.is_busy() ? "yes" : "no");
        }
        lbt_relearn_was_active_ = relearning_now;
    }

    // §A.5.3.3 stage-1: arm the ALE-energy detector on the settle EDGE, not at the
    // hop.  With an async radio the audio right after a hop is still the previous
    // channel until the tune completes; the stage-1 detector is level-invariant and
    // fires ~30 ms into any 8-FSK, so if it ran on that pre-settle audio it would fire,
    // get dropped by the is_tune_settled() gate on the energy callback, and its one-shot
    // latch would then block detection of the real (settled) signal for the rest of the
    // dwell — the channel would be hopped over.  Re-arming (mark_channel_hop resets the
    // detector + latch) when is_tune_settled() goes false→true guarantees detection
    // accumulates only on the channel we are actually listening to.  For sync backends
    // is_tune_settled() is always true, so there is no edge and behaviour is unchanged.
    if (sm_.get_state() == ALEState::SCANNING) {
        const bool settled = !radio_ || radio_->is_tune_settled();
        if (settled && !scan_was_settled_)
            demodulator_.mark_channel_hop();
        scan_was_settled_ = settled;
    } else {
        scan_was_settled_ = false;
    }

    tick_ptt_timing(now_ms);
    tick_sm(now_ms);
    tick_frame_settle(now_ms);
    tick_relink(now_ms);
    tick_sounding_sweep(now_ms);
    tick_test_channel(now_ms);
    tick_offline_completion();
    tick_lqa_update(now_ms);
    tick_mode_verify(now_ms);
}

// Check delays after a (non-scanning) channel/mode command: first check catches
// an SDR front-end's asynchronous band-mode revert (~1-2 GUI timer ticks after
// the freq change), the later ones cover stragglers. All lie inside the radio
// backend's re-assert recency window, and each check is a single non-blocking
// CAT read (sync_from_radio) — no sleeps, nothing on the hop path itself.
static constexpr uint32_t MODE_VERIFY_DELAYS_MS[] = { 300, 700, 1500 };
static constexpr int      MODE_VERIFY_CHECKS =
    static_cast<int>(sizeof(MODE_VERIFY_DELAYS_MS) / sizeof(MODE_VERIFY_DELAYS_MS[0]));

// While SCANNING, the per-hop schedule_mode_verify() re-arm is suppressed (a
// 200 ms dwell would supersede the +300 ms one-shot deadline before it ever
// fired). Instead a background verify runs on this fixed bounded cadence,
// decoupled from the hops: one sync_from_radio() every ~400 ms re-asserts the
// intended mode if an async band revert pulled it away, catching residual
// reverts within a few hops. sync_from_radio is a CmdSync — it never touches
// tunes_in_flight_, so it never gates hop_ready / the hop rate. Quisk's per-band
// memory trains after the first scan pass, so reverts — and thus these
// corrections — taper off.
static constexpr uint32_t MODE_VERIFY_SCAN_CADENCE_MS = 400;

void ALEController::schedule_mode_verify()
{
    if (!radio_) return;
    // While SCANNING the background cadence in tick_mode_verify handles verify;
    // arming the one-shot here would just be superseded by the next 200 ms hop.
    if (sm_.get_state() == ALEState::SCANNING) return;
    mode_verify_checks_left_ = MODE_VERIFY_CHECKS;
    mode_verify_deadline_ms_ = now_ms_ + MODE_VERIFY_DELAYS_MS[0];
}

void ALEController::tick_mode_verify(uint32_t now_ms)
{
    if (sm_.get_state() == ALEState::SCANNING) {
        // Fixed-cadence background verify, decoupled from the per-hop re-arm.
        // Lazily arm on the first scanning tick so the cadence is measured from
        // the start of scanning, not from whatever value was left behind.
        if (mode_verify_scan_deadline_ms_ == 0)
            mode_verify_scan_deadline_ms_ = now_ms + MODE_VERIFY_SCAN_CADENCE_MS;
        if (now_ms >= mode_verify_scan_deadline_ms_) {
            if (radio_) radio_->sync_from_radio();  // backend re-asserts intended mode if reverted
            mode_verify_scan_deadline_ms_ = now_ms + MODE_VERIFY_SCAN_CADENCE_MS;
        }
        return;
    }

    // Non-scanning one-shot ops: deferred multi-check path.
    mode_verify_scan_deadline_ms_ = 0;  // leaving scanning — re-arm cadence on next scan
    if (mode_verify_checks_left_ <= 0 || now_ms < mode_verify_deadline_ms_) return;
    if (radio_) radio_->sync_from_radio();  // backend re-asserts intended mode if reverted
    --mode_verify_checks_left_;
    if (mode_verify_checks_left_ > 0) {
        const int next = MODE_VERIFY_CHECKS - mode_verify_checks_left_;
        mode_verify_deadline_ms_ = now_ms + MODE_VERIFY_DELAYS_MS[next];
    } else {
        mode_verify_deadline_ms_ = 0;
    }
}

void ALEController::tick_ptt_timing(uint32_t now_ms)
{
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
        set_ptt_and_notify(false);
        // tx_only (Direction=TX): keep RX disabled — transmit-only channel.
        if (!rx_inhibited(get_current_frequency()))
            demodulator_.set_enabled(true);
        // The SM stamped its own "waiting for peer" timer (e.g. WAIT_ACK's
        // Twr window) back when it decided to listen, not now that it truly
        // can — replay the delay we just imposed so that timer isn't already
        // running down before this station could physically hear anything.
        sm_.extend_peer_wait_window_for_ptt_release_delay(ptt_tail_armed_delay_ms_);
        ptt_tail_armed_delay_ms_ = 0;
    }
}

void ALEController::tick_sm(uint32_t now_ms)
{
    // rx_only: abort an SM-initiated TX (incoming-call handshake response) that
    // was caught in set_rx_enabled_callback. Drained here to avoid re-entering
    // the SM inside that callback. emergency_manual_control() → IDLE; in
    // HANDSHAKE state this is silent (no TWAS), so the station hears the call
    // but never transmits — exactly "reception only, transmission disabled".
    if (abort_tx_pending_) {
        abort_tx_pending_ = false;
        sm_.emergency_manual_control();
    }
    sm_.update(now_ms);
    maybe_emit_call_alert();
}

void ALEController::tick_frame_settle(uint32_t now_ms)
{
    // Commit a settled received-sounding session to the LQA DB (full address +
    // session-averaged snr/ber). Checked every tick — cheap, and time-sensitive.
    if (sounding_accumulator_.timed_out(now_ms)) {
        if (auto r = sounding_accumulator_.finalize())
            commit_sounding_result(*r);
    }

    // Commit a settled non-sounding RX frame's averaged BER/SNR (A.5.4.1.1) to
    // the LQA DB for the measured peer + channel. Same Tdrw-silence frame-end
    // detection as the sounding path above.
    if (rx_ber_acc_.word_count() > 0 && rx_ber_settle_ms_ > 0
        && (now_ms - rx_ber_settle_ms_) >= ALETimingConstants::Tdrw_ms) {
        commit_rx_ber_sample();
    }

    // Linked AMD RX fallback: if a CMD AMD was seen but no TIS conclusion
    // arrived (corrupted/missed), commit after Tdrw silence.  The TIS path in
    // rx_accumulate_linked_amd() is the primary commit; this is belt-and-suspenders.
    if (linked_amd_collecting_ && linked_amd_settle_ms_ > 0
        && (now_ms - linked_amd_settle_ms_) >= ALETimingConstants::Tdrw_ms) {
        commit_linked_amd();
    }
}

void ALEController::tick_relink(uint32_t now_ms)
{
    // Auto-Relink / Enhanced Frequency-Select: evaluate channel renegotiation.
    // Only while LINKED, LQA enabled, no relink already pending, and EFS IDLE.
    if ((config_.relink_enabled || config_.enhanced_freq_select)
        && op_params_.lqa_enabled
        && sm_.get_state() == ALEState::LINKED
        && pending_relink_addr_.empty()
        && freq_select_.is_idle()) {
        config_.enhanced_freq_select
            ? freq_select_.evaluate(now_ms, rx_ber_settle_ms_, config_.relink_improvement_threshold)
            : evaluate_relink(now_ms);
    }

    freq_select_.tick(now_ms);

    // After TWAS completes and SM is back to IDLE/SCANNING, re-initiate the call
    // to pending_relink_addr_ on the now-best channel.
    if (!pending_relink_addr_.empty()) {
        const ALEState st = sm_.get_state();
        if (st == ALEState::IDLE || st == ALEState::SCANNING) {
            std::string addr = std::move(pending_relink_addr_);
            pending_relink_addr_.clear();
            initiate_call(addr);
        }
    }
}

void ALEController::tick_sounding_sweep(uint32_t now_ms)
{
    // Periodic multi-channel sounding sweep (set_automatic_sounding). Start a
    // sweep over the configured net's channels every interval, gated on IDLE/
    // SCANNING. A running sweep holds the SM in SOUNDING, which blocks re-entry.
    if (!auto_sounding_on_ || auto_sounding_net_.empty() || auto_sounding_interval_ms_ == 0)
        return;

    const ALEState st = sm_.get_state();
    if (st == ALEState::SOUNDING) return; // sweep in flight

    // Compute remaining ms. Use signed arithmetic to detect overdue (<=0).
    const int32_t remaining_ms = static_cast<int32_t>(auto_sounding_interval_ms_)
                                 - static_cast<int32_t>(now_ms - auto_sounding_last_ms_);

    // Pre-sounding warning — IDLE only, within the configured lead window.
    if (st == ALEState::IDLE && sounding_warning_lead_ms_ > 0
        && remaining_ms > 0
        && static_cast<uint32_t>(remaining_ms) <= sounding_warning_lead_ms_
        && !sounding_warning_sent_) {
        sounding_warning_sent_   = true;
        sounding_warning_active_ = true;
        const uint32_t remaining_sec = (static_cast<uint32_t>(remaining_ms) + 999u) / 1000u;
        {
            ale::SoundingWarningData swd{ auto_sounding_net_.c_str(), remaining_sec, "warn" };
            dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
        }
    }

    // Fire when due — from IDLE or SCANNING.
    if (remaining_ms <= 0 && (st == ALEState::IDLE || st == ALEState::SCANNING)) {
        auto channels = resolve_net_sounding_channels(auto_sounding_net_);
        // Sounding uses the same "call width" C as calling (Tsrs = (C+2)·Ta) —
        // take it from the auto-sounding net's calling_length_c.  See
        // resolve_sounding_C() and handle_sounding().
        sm_.set_target_scan_channels(resolve_sounding_C(auto_sounding_net_));
        if (sm_.get_self_address().empty()) {
            pal::log_warn("Ctrl", "Auto-sounding deferred: no callsign configured");
            emit_status("Auto-sounding deferred — configure a callsign first");
            auto_sounding_last_ms_ = now_ms_;
            return;
        }
        const bool started = !channels.empty() && sm_.send_sounding_sweep(channels);
        if (started) {
            emit_status("Auto-sounding sweep on net '" + auto_sounding_net_
                        + "' (" + std::to_string(channels.size()) + " channels)");
            // Defer the timer re-arm to cycle END (on_sm_state_change leaving
            // SOUNDING) so the next interval is the gap AFTER the sounding cycle,
            // not after it started.  Do NOT stamp auto_sounding_last_ms_ here.
            sounding_cycle_active_ = true;
        } else {
            // No soundable channels / sweep rejected — re-arm now to avoid
            // busy-looping on an empty net (no SOUNDING cycle will ever complete).
            auto_sounding_last_ms_ = now_ms_;
        }
        sounding_warning_sent_ = false;
        if (sounding_warning_active_) {
            sounding_warning_active_ = false;
            ale::SoundingWarningData swd{ auto_sounding_net_.c_str(), 0, "fire" };
            dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
            dispatch(pal::EventType::ALE_SOUNDING, auto_sounding_net_);
        }
    }
    // When remaining_ms <= 0 but SM is CALLING/HANDSHAKE/LINKED, leave
    // auto_sounding_last_ms_ unchanged so the sweep fires the moment the SM
    // returns to IDLE/SCANNING.
}

// ── Test-Channel sweep (active per-peer LQA collection) ──────────────────────
// Per-channel no-reply backstop. The SM's own calling timeout
// (compute_calling_timeout_ms) normally aborts a no-reply call first; this is a
// safety net for a stuck SM.
static constexpr uint32_t TEST_CHANNEL_LINK_TIMEOUT_MS = 30000;

std::vector<Channel> ALEController::resolve_net_call_channels(const std::string& net_name) const
{
    std::vector<Channel> out;
    // Callable = enabled && may call (not inhibit_calling) && may TX (not rx_only).
    // A test channel must be able to place a call, so sounding-only filters do not apply.
    auto collect_net = [&](const Net& net) {
        for (const auto& ch_id : net.channel_ids)
            for (const auto& ch : calling_channels_)
                if (ch.id == ch_id && ch.enabled && !ch.inhibit_calling && !ch.rx_only) {
                    out.push_back(ch);
                    break;
                }
    };

    if (!net_name.empty()) {
        if (const Net* net = net_store_.find(net_name)) { collect_net(*net); return out; }
        // named net not found → fall through to active net / all
    }
    if (!active_scan_net_.empty()) {
        if (const Net* net = net_store_.find(active_scan_net_)) { collect_net(*net); return out; }
    }
    for (const auto& ch : calling_channels_)
        if (ch.enabled && !ch.inhibit_calling && !ch.rx_only) out.push_back(ch);
    return out;
}

bool ALEController::start_test_channel(const std::string& target, const std::string& net)
{
    if (test_active_) {
        emit_status("Test-Channel already in progress — stop it first (CMD:TEST_CHANNEL_STOP)");
        return false;
    }
    if (!is_valid_ale_address(target)) {
        emit_status("ERROR: test-channel target '" + target
                    + "' invalid — must be 3–15 Basic-38 characters");
        return false;
    }
    const ALEState st = sm_.get_state();
    if (st != ALEState::IDLE && st != ALEState::SCANNING) {
        emit_status("Test-Channel rejected — only available while IDLE or scanning");
        return false;
    }
    auto chans = resolve_net_call_channels(net);
    if (chans.empty()) {
        emit_status("Test-Channel rejected — no callable channels for net '"
                    + (net.empty() ? active_scan_net_ : net) + "'");
        return false;
    }

    test_target_    = target;
    test_net_       = net;
    test_channels_  = std::move(chans);
    test_idx_       = 0;
    test_results_.assign(test_channels_.size(), TestResult{});
    test_summary_.clear();
    test_was_scanning_ = (st == ALEState::SCANNING);
    // Remember the channel the radio was on at start so DONE/STOP can return to
    // it — without this the radio ends up on the last tested channel.
    {   const Channel orig = get_current_channel();
        test_orig_freq_hz_ = orig.rx_frequency_hz;
        test_orig_mode_    = orig.rx_mode;
    }
    test_active_ = true;
    test_phase_  = TestPhase::TUNE;
    test_phase_start_ms_ = now_ms_;

    emit_status("Test-Channel sweep to " + target + " over "
                + std::to_string(test_channels_.size()) + " channel(s)");
    emit_test_event_("start", nullptr, -1, false);
    return true;
}

void ALEController::stop_test_channel()
{
    if (!test_active_) return;
    // Abort any in-flight call/handshake/link so the SM returns to a quiescent state.
    const ALEState st = sm_.get_state();
    if (st == ALEState::CALLING || st == ALEState::HANDSHAKE || st == ALEState::LINKED)
        sm_.emergency_manual_control();
    test_active_ = false;
    test_phase_  = TestPhase::INACTIVE;
    // Return the radio to the channel it was on when the sweep started, then
    // resume scanning if that was the pre-sweep state.
    restore_test_channel_();
    if (test_was_scanning_) sm_.process_event(ALEEvent::START_SCAN);
    emit_status("Test-Channel sweep stopped");
    emit_test_event_("stop", nullptr, -1, false);
}

void ALEController::emit_test_event_(const char* phase, const Channel* ch,
                                     int score, bool linked)
{
    ale::TestChannelData d{};
    d.peer       = test_target_.c_str();
    d.phase      = phase;
    d.channel_id = ch ? ch->id.c_str() : "";
    d.freq_hz    = ch ? ch->rx_frequency_hz : 0u;
    d.index      = ch ? static_cast<uint32_t>(test_idx_ + 1) : 0u;
    d.total      = static_cast<uint32_t>(test_channels_.size());
    d.score      = score;
    d.linked     = linked;
    d.summary    = (std::string(phase) == "done") ? test_summary_.c_str() : "";
    dispatch(pal::EventType::ALE_TEST_CHANNEL, "", 0, &d, sizeof(d));
}

void ALEController::tick_test_channel(uint32_t now_ms)
{
    if (!test_active_) return;
    const ALEState st = sm_.get_state();

    switch (test_phase_) {
    case TestPhase::TUNE: {
        // Only place a call from a quiescent SM (IDLE/SCANNING). Right after start
        // or a terminate this holds; if not, retry next tick.
        if (st != ALEState::IDLE && st != ALEState::SCANNING) return;
        if (test_idx_ >= test_channels_.size()) { test_phase_ = TestPhase::DONE; return; }
        const Channel& ch = test_channels_[test_idx_];
        // Defensive: resolve_net_call_channels() already filtered these, but a live
        // config change between start and now could have flipped a flag.
        if (ch.inhibit_calling || ch.rx_only) {
            test_results_[test_idx_] = { ch.rx_frequency_hz, ch.id, false, -1 };
            emit_status("Test-Channel: skip " + ch.id + " (not callable)");
            emit_test_event_("failed", &ch, -1, false);
            test_phase_ = TestPhase::NEXT;
            return;
        }
        emit_status("Test-Channel: tuning " + ch.id + " ("
                    + std::to_string(ch.rx_frequency_hz) + " Hz) — calling " + test_target_);
        set_vfo_channel(ch.rx_frequency_hz, ch.rx_mode);
        // initiate_single_channel_call() reads the now-tuned channel and auto-queues
        // the bilateral LQA CMD 'a' when lqa_exchange_enabled && !reporting_inhibited.
        if (!initiate_single_channel_call(test_target_)) {
            test_results_[test_idx_] = { ch.rx_frequency_hz, ch.id, false, -1 };
            emit_test_event_("failed", &ch, -1, false);
            test_phase_ = TestPhase::NEXT;
            return;
        }
        emit_test_event_("tune", &ch, -1, false);
        test_phase_ = TestPhase::CALLING;
        test_phase_start_ms_   = now_ms;
        test_link_deadline_ms_ = now_ms + TEST_CHANNEL_LINK_TIMEOUT_MS;
        return;
    }
    case TestPhase::CALLING: {
        if (st == ALEState::LINKED) {
            // Link up — bilateral/FROM metrics are written by on_operator_event.
            const Channel& ch = test_channels_[test_idx_];
            test_results_[test_idx_] = { ch.rx_frequency_hz, ch.id, true, -1 };
            emit_status("Test-Channel: linked on " + ch.id);
            emit_test_event_("linked", &ch, -1, true);
            test_phase_ = TestPhase::LINKED_SETTLE;
            test_phase_start_ms_ = now_ms;
            return;
        }
        // SM returned to quiescent without linking → no reply / rejected.
        if (st == ALEState::IDLE || st == ALEState::SCANNING) {
            const Channel& ch = test_channels_[test_idx_];
            test_results_[test_idx_] = { ch.rx_frequency_hz, ch.id, false, -1 };
            emit_status("Test-Channel: no link on " + ch.id);
            emit_test_event_("failed", &ch, -1, false);
            test_phase_ = TestPhase::NEXT;
            return;
        }
        // Backstop: a stuck SM. Force-abort and move on.
        if (now_ms >= test_link_deadline_ms_) {
            const Channel& ch = test_channels_[test_idx_];
            emit_status("Test-Channel: timeout on " + ch.id);
            sm_.emergency_manual_control();
            test_results_[test_idx_] = { ch.rx_frequency_hz, ch.id, false, -1 };
            emit_test_event_("failed", &ch, -1, false);
            test_phase_ = TestPhase::NEXT;
            return;
        }
        return;
    }
    case TestPhase::LINKED_SETTLE: {
        const Channel& ch = test_channels_[test_idx_];
        // If the link dropped early, record what we have and advance.
        if (st != ALEState::LINKED) {
            snapshot_test_score_(ch);
            test_phase_ = TestPhase::NEXT;
            return;
        }
        // Hold for the configured dwell (floor: Tdrw = 784 ms so bilateral LQA
        // metrics always have time to commit via tick_frame_settle before snapshot).
        const uint32_t dwell_ms = std::max(
            ALETimingConstants::Tdrw_ms,
            config_.test_channel_link_hold_time * 1000u
        );
        if (now_ms - test_phase_start_ms_ < dwell_ms) return;
        snapshot_test_score_(ch);
        terminate_link();
        emit_test_event_("terminate", &ch, test_results_[test_idx_].score, true);
        test_phase_ = TestPhase::TERMINATING;
        test_phase_start_ms_ = now_ms;
        return;
    }
    case TestPhase::TERMINATING: {
        // TWAS frame drains → SM leaves LINKED (on_sm_state_change fires
        // ALE_LINK_TERMINATED). Once it does, stamp start_ms so NEXT can gate
        // on Tdrw: the peer needs up to one Tdrw after the last TWAS word to
        // detect termination and return to SCANNING. Without the gap the next
        // channel's scanning call starts before the peer is ready to receive it.
        if (st != ALEState::LINKED) {
            test_phase_start_ms_ = now_ms;
            test_phase_ = TestPhase::NEXT;
            return;
        }
        if (now_ms - test_phase_start_ms_ > TEST_CHANNEL_LINK_TIMEOUT_MS) {
            sm_.emergency_manual_control();
            test_phase_start_ms_ = now_ms;
            test_phase_ = TestPhase::NEXT;
            return;
        }
        return;
    }
    case TestPhase::NEXT: {
        if (st != ALEState::IDLE && st != ALEState::SCANNING) return;
        // Inter-channel settle: give the peer Tdrw to detect our TWAS and
        // return to scanning before we place the next call. test_phase_start_ms_
        // is stamped by TERMINATING on exit; for failure paths (CALLING→NEXT,
        // TUNE→NEXT) the elapsed time already exceeds Tdrw so there is no delay.
        if (now_ms - test_phase_start_ms_ < ALETimingConstants::Tdrw_ms) return;
        ++test_idx_;
        if (test_idx_ >= test_channels_.size()) { test_phase_ = TestPhase::DONE; return; }
        test_phase_ = TestPhase::TUNE;
        return;
    }
    case TestPhase::DONE: {
        // Build a ranked summary: linked channels by score desc, then failed.
        std::vector<size_t> order(test_results_.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const bool la = test_results_[a].linked, lb = test_results_[b].linked;
            if (la != lb) return la;                 // linked before failed
            if (la) return test_results_[a].score > test_results_[b].score;
            return false;                            // failed: keep input order
        });
        std::string s = "Test-Channel results for " + test_target_ + ":\n";
        s += "  channel      freq_hz    linked  score\n";
        for (size_t i : order) {
            const auto& r = test_results_[i];
            char line[112];
            std::snprintf(line, sizeof(line), "  %-12s %-11u %-7s %d\n",
                          r.id.c_str(), r.freq_hz, r.linked ? "yes" : "no", r.score);
            s += line;
        }
        test_summary_ = std::move(s);

        emit_status("Test-Channel sweep complete — " + test_target_);
        emit_test_event_("done", nullptr, -1, false);

        test_active_ = false;
        test_phase_  = TestPhase::INACTIVE;
        // Return the radio to the channel it was on when the sweep started, then
        // resume scanning if that was the pre-sweep state.
        restore_test_channel_();
        if (test_was_scanning_) sm_.process_event(ALEEvent::START_SCAN);
        return;
    }
    case TestPhase::INACTIVE: return;
    }
}

void ALEController::snapshot_test_score_(const Channel& ch)
{
    test_results_[test_idx_].freq_hz = ch.rx_frequency_hz;
    test_results_[test_idx_].id     = ch.id;
    if (auto e = lqa_database_.get_entry(ch.rx_frequency_hz, test_target_))
        test_results_[test_idx_].score = static_cast<int>(e->score + 0.5f);
    else
        test_results_[test_idx_].score = -1;
}

void ALEController::restore_test_channel_()
{
    if (test_orig_freq_hz_ == 0) return;
    const Channel* cfg = find_channel_by_freq(test_orig_freq_hz_);
    const std::string& mode = cfg ? cfg->rx_mode : test_orig_mode_;
    set_vfo_channel(test_orig_freq_hz_, mode);
}

void ALEController::tick_offline_completion()
{
    // Offline mode: no audio device, so drive word-completion directly by
    // pulling all pending symbol frames and firing on_word_complete per frame.
    if (!audio_device_) {
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modulator_.pull_symbol_frame(syms))
            sm_.on_word_complete();
    }
}

void ALEController::tick_lqa_update(uint32_t now_ms)
{
    // Throttled to once per second — the database is small but no need to
    // iterate every audio frame.
    if (now_ms - lqa_update_ms_ >= 1000u) {
        lqa_analyzer_.update();
        lqa_update_ms_ = now_ms;
        // Refresh propagation context so solar-elevation is computed against
        // the current clock rather than the time of the last position update.
        update_propagation_context();
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

    // GAP 2 fix (A.5.4.1.1): JOE commits calling-frame FROM measurement NOW
    // (before encode_outgoing reads the DB) so the fresh data is available
    // for CMD 'a'. Reset the accumulator; on_sm_state_change provides the
    // safety-net reset if the alert fires but LINKED is never reached.
    if (op_params_.lqa_enabled && hs_call_acc_.word_count() > 0) {
        if (!caller.empty() && !self_address_store_.matches_self(caller)) {
            // Commit uses hs_call_freq_hz_ (set by the accumulator above via the
            // radio-backed get_current_channel()), so guard on that — not on
            // sm_.get_current_channel(), which is nullptr on no-scan links and would
            // silently drop the calling-frame FROM measurement (cf. 1562ea9).
            if (hs_call_freq_hz_ > 0) {
                lqa_database_.update_entry_extended(hs_call_freq_hz_, caller,
                    hs_call_acc_.snr_avg(),
                    static_cast<float>(hs_call_acc_.ber_score()),
                    hs_call_acc_.sinad_avg(),
                    0.0f, -120.0f, 0, static_cast<int>(hs_call_acc_.word_count()), 0);
                record_lqa_history(hs_call_freq_hz_, caller);
                if (debug_rx_)
                    emit_status("LQA calling-frame FROM: " + caller
                                + " freq=" + std::to_string(hs_call_freq_hz_)
                                + " ber=" + std::to_string(static_cast<int>(hs_call_acc_.ber_score()))
                                + " sinad=" + std::to_string(static_cast<int>(hs_call_acc_.sinad_avg())));
            }
        }
    }
    hs_call_acc_.reset();
    hs_call_freq_hz_ = 0;

    // Block A4 (responder) — queue CMD LQA (KA1=0) for the response frame.
    // encode_outgoing reads the fresh calling-frame measurement committed above.
    // Done here (once, at alert time) so it applies for both auto-accept and manual-accept.
    if (config_.lqa_exchange_enabled) {
        const Channel cur_blka4_ch = get_current_channel();
        // Per-channel inhibit_reporting suppresses the bilateral CMD 'a'
        // exchange (both encode and apply) for this channel.
        if (cur_blka4_ch.rx_frequency_hz > 0
            && !reporting_inhibited(cur_blka4_ch.rx_frequency_hz))
            lqa_exchange_.encode_outgoing(cur_blka4_ch.rx_frequency_hz,
                                          sm_.get_caller_address(), false);
        else if (cur_blka4_ch.rx_frequency_hz > 0
                 && reporting_inhibited(cur_blka4_ch.rx_frequency_hz))
            emit_status("LQA CMD 'a' exchange suppressed — channel "
                        + cur_blka4_ch.id + " is inhibit-reporting");
    }

    // Block A5 — apply bilateral received from caller's CMD 'a'; Block C5 if KA1=true.
    if (config_.lqa_exchange_enabled && !reporting_inhibited(get_current_frequency()))
        lqa_exchange_.apply_pending(caller, true,
                                     [this](const std::string& m){ emit_status(m); });

    dispatch(pal::EventType::ALE_CALL_RECEIVED, caller);
}

void ALEController::commit_sounding_result(const SoundingIdentityAccumulator::Result& r)
{
    // MIL-STD Fig. A-27: LQA matrix is remote-stations only — never store own address.
    if (self_address_store_.matches_self(r.station)) return;
    // Forward the sounding's conclusion type so the LQA entry is flagged
    // available (TIS) / not available (TWAS) for active link establishment.
    lqa_analyzer_.process_sounding(r.station, r.frequency_hz,
                                   r.snr_db, r.ber, r.sinad_db, r.twas_conclusion);
}

void ALEController::commit_rx_ber_sample()
{
    const uint32_t words = rx_ber_acc_.word_count();
    if (words == 0) {
        rx_ber_acc_.reset();
        rx_ber_snr_sum_   = 0.0f;
        rx_ber_sinad_sum_ = 0.0f;
        rx_ber_settle_ms_ = 0;
        return;
    }

    // Resolve the station that sent the measured transmission (the peer): the
    // station we called (to_address) when we initiated, else the caller
    // (caller_address) when we were called — same rule as the LQA-report path.
    // get_to_address()/get_caller_address() already return cleaned addresses.
    const std::string sender = !sm_.get_to_address().empty()
        ? sm_.get_to_address() : sm_.get_caller_address();

    // Skip when no peer is known yet or the address is our own (Fig. A-27: the
    // LQA matrix records remote stations only).
    if (!sender.empty() && !self_address_store_.matches_self(sender)
        && rx_ber_freq_hz_ > 0) {
        const float n        = static_cast<float>(words);
        const float avg_ber  = static_cast<float>(rx_ber_acc_.ber_score());
        const float avg_snr  = rx_ber_snr_sum_  / n;
        const float avg_sinad = rx_ber_sinad_sum_ / n;
        // A.5.4.1.2: write measured SINAD via update_entry_extended.
        lqa_database_.update_entry_extended(rx_ber_freq_hz_, sender,
                                             avg_snr, avg_ber, avg_sinad,
                                             0.0f, -120.0f, 0,
                                             static_cast<int>(words), 0);
        record_lqa_history(rx_ber_freq_hz_, sender);
        if (debug_rx_)
            emit_status("LQA BER: " + sender + " ch=" + std::to_string(rx_ber_freq_hz_)
                        + " ber=" + std::to_string(static_cast<int>(avg_ber))
                        + " sinad=" + std::to_string(static_cast<int>(avg_sinad))
                        + " (" + std::to_string(words) + " words)");
    }

    rx_ber_acc_.reset();
    rx_ber_snr_sum_   = 0.0f;
    rx_ber_sinad_sum_ = 0.0f;
    rx_ber_settle_ms_ = 0;
}

void ALEController::evaluate_relink(uint32_t now_ms) {
    // Hysteresis guard: require at least 4×Tdrw of stable data before evaluating.
    if (rx_ber_settle_ms_ == 0
        || (now_ms - rx_ber_settle_ms_) < ALETimingConstants::Tdrw_ms * 4u)
        return;

    const std::string peer = !sm_.get_to_address().empty()
        ? sm_.get_to_address() : sm_.get_caller_address();
    if (peer.empty() || self_address_store_.matches_self(peer)) return;

    const Channel* ch = sm_.get_current_channel();
    if (!ch || ch->rx_frequency_hz == 0) return;

    // Score of the currently active channel for this peer.
    const auto ranked = lqa_analyzer_.rank_channels_for_station(peer);
    if (ranked.empty()) return;

    float cur_score = 0.0f;
    for (const auto& r : ranked)
        if (r.frequency_hz == ch->rx_frequency_hz) { cur_score = r.score; break; }

    const float best_score = ranked.front().score;
    const uint32_t best_freq = ranked.front().frequency_hz;

    if (best_freq != ch->rx_frequency_hz
        && best_score > cur_score + config_.relink_improvement_threshold) {
        emit_status("Auto-relink: ch " + std::to_string(best_freq)
                    + " Hz score " + std::to_string(static_cast<int>(best_score))
                    + " > cur " + std::to_string(static_cast<int>(cur_score))
                    + " + " + std::to_string(static_cast<int>(config_.relink_improvement_threshold)));
        pending_relink_addr_ = peer;
        sm_.terminate_link();
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
    // LBT occupancy (A.5.4.7.2): only while RX actually listens on the channel —
    // during TX the RX pipeline is disabled and own-signal leakage would poison
    // the noise floor and busy state.
    if (demodulator_.enabled())
        occupancy_.push_samples(samples, count);
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

    // Pre-empt: if a call begins while the idle-sounding countdown popup is open,
    // cancel the popup. Keep sounding_warning_sent_=true so we don't re-warn for
    // this same cycle; the overdue sweep will fire silently once the call ends.
    if (sounding_warning_active_
        && (to == ALEState::CALLING || to == ALEState::HANDSHAKE
            || to == ALEState::LINKED)) {
        sounding_warning_active_ = false;
        ale::SoundingWarningData swd{ auto_sounding_net_.c_str(), 0, "cancel" };
        dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
    }

    // Reset caller tracking when leaving HANDSHAKE
    if (from == ALEState::HANDSHAKE) {
        last_caller_.clear();
        call_alert_fired_ = false;
        // Safety-net: discard any calling-frame accumulation that never fired an alert
        hs_call_acc_.reset();
        hs_call_freq_hz_ = 0;
    }

    // Fresh calling-frame accumulator for each new handshake.
    if (to == ALEState::HANDSHAKE) {
        hs_call_acc_.reset();
        hs_call_freq_hz_ = 0;
        lqa_exchange_.on_handshake_start();
    }

    // Fresh response-frame accumulator for each new outgoing call.
    if (to == ALEState::CALLING) {
        hs_resp_acc_.reset();
        hs_resp_freq_hz_ = 0;
    }

    // Entering SOUNDING = we are about to *transmit* our own sounding. Per
    // A.5.4.1.1/A.5.4.1.2 a transmitted sounding produces no received words and
    // MUST NOT create an LQA entry (only stations that *receive* a sounding
    // perform channel measurements — see commit_sounding_result()). The sounding
    // scheduler is driven by auto_sounding_last_ms_, not the LQA DB, so there is
    // nothing to write here. Block B3 (CMD NOISE attach) is TX command assembly,
    // not an LQA DB write, and remains below.
    if (to == ALEState::SOUNDING) {
        const Channel* ch = sm_.get_current_channel();
        if (ch) {
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

    // Auto-sounding timer re-arm at cycle END.  The SM stays in SOUNDING between
    // sweep channels (it re-arms LBT directly, no transition_to) and only leaves
    // SOUNDING once — at sweep exhaustion (→ IDLE/SCANNING) or on CALL_DETECTED/
    // ERROR — so this fires exactly once per auto-sounding cycle.  Stamping here
    // (not at fire time in tick_sounding_sweep) makes the configured interval the
    // gap AFTER the cycle, matching the spec's "reset to sound-interval when a
    // sound is sent" read as when the sound completes.
    if (from == ALEState::SOUNDING && to != ALEState::SOUNDING
        && sounding_cycle_active_) {
        auto_sounding_last_ms_  = now_ms_;
        sounding_cycle_active_  = false;
        sounding_warning_sent_  = false;
    }

    // Link exited (except via HANDSHAKE_COMPLETE → LINKED)
    if (from == ALEState::LINKED && to != ALEState::LINKED) {
        link_start_ms_ = 0;
        pending_operator_accept_ = false;  // clear any unresolved manual-accept gate
        dispatch(pal::EventType::ALE_LINK_TERMINATED, "Link state exited");
    }
}

void ALEController::on_operator_event(OperatorEvent ev)
{
    switch (ev) {
        case OperatorEvent::LINK_ESTABLISHED:
        {
            // GAP 1 fix: SAM commits response-frame FROM measurement for JOE
            // (= get_to_address() = DF3SR). Only present on the SAM side;
            // JOE committed hs_call in maybe_emit_call_alert() already.
            if (op_params_.lqa_enabled && hs_resp_acc_.word_count() > 0) {
                const std::string peer = sm_.get_to_address();
                if (!peer.empty() && !self_address_store_.matches_self(peer)
                    && hs_resp_freq_hz_ > 0) {
                    lqa_database_.update_entry_extended(hs_resp_freq_hz_, peer,
                        hs_resp_acc_.snr_avg(),
                        static_cast<float>(hs_resp_acc_.ber_score()),
                        hs_resp_acc_.sinad_avg(),
                        0.0f, -120.0f, 0,
                        static_cast<int>(hs_resp_acc_.word_count()), 0);
                    record_lqa_history(hs_resp_freq_hz_, peer);
                    if (debug_rx_)
                        emit_status("LQA response-frame FROM: " + peer
                                    + " freq=" + std::to_string(hs_resp_freq_hz_)
                                    + " ber=" + std::to_string(
                                          static_cast<int>(hs_resp_acc_.ber_score()))
                                    + " sinad=" + std::to_string(
                                          static_cast<int>(hs_resp_acc_.sinad_avg())));
                }
            }
            hs_resp_acc_.reset();
            hs_resp_freq_hz_ = 0;

            // Block A5 — SAM side: if JOE sent CMD 'a' in the response frame, store it.
            // Per-channel inhibit_reporting suppresses the bilateral exchange.
            if (config_.lqa_exchange_enabled && !reporting_inhibited(get_current_frequency())) {
                const std::string peer = sm_.get_to_address();
                lqa_exchange_.apply_pending(peer, false,
                                             [this](const std::string& m){ emit_status(m); });
            }

            // Block A6 — successful call: bilateral data was (or wasn't) received.
            // Flush any pending response-frame word metrics BEFORE marking bilateral
            // attempted: finalizing here stores the TIS:peer FROM-direction
            // quality so mark_bilateral_attempted finds a real entry to annotate
            // instead of creating a zero-score stub (which scores ~6 recency-only).
            if (auto r = sounding_accumulator_.finalize()) commit_sounding_result(*r);
            if (config_.lqa_exchange_enabled)
                lqa_exchange_.on_call_concluded();

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
            dispatch(pal::EventType::ALE_LINK_ESTABLISHED, peer);
            break;
        }
        case OperatorEvent::CALL_REJECTED:
            // GAP 1 fix: JOE's TWAS frame was received in CALLING/LISTENING →
            // commit whatever response-frame quality was measured.
            if (op_params_.lqa_enabled && hs_resp_acc_.word_count() > 0) {
                const std::string peer = sm_.get_to_address();
                if (!peer.empty() && !self_address_store_.matches_self(peer)
                    && hs_resp_freq_hz_ > 0) {
                    lqa_database_.update_entry_extended(hs_resp_freq_hz_, peer,
                        hs_resp_acc_.snr_avg(),
                        static_cast<float>(hs_resp_acc_.ber_score()),
                        hs_resp_acc_.sinad_avg(),
                        0.0f, -120.0f, 0,
                        static_cast<int>(hs_resp_acc_.word_count()), 0);
                    record_lqa_history(hs_resp_freq_hz_, peer);
                }
            }
            hs_resp_acc_.reset();
            hs_resp_freq_hz_ = 0;
            // Block A6 — flush any partial response metrics before marking
            if (auto r = sounding_accumulator_.finalize()) commit_sounding_result(*r);
            if (config_.lqa_exchange_enabled)
                lqa_exchange_.on_call_concluded();
            emit_status("Call rejected by remote station (TWAS)");
            dispatch(pal::EventType::ALE_LINK_TERMINATED, "Call rejected");
            break;
        case OperatorEvent::NO_CHANNELS_LEFT:
            // No JOE response received on any channel → discard response-frame acc.
            hs_resp_acc_.reset();
            hs_resp_freq_hz_ = 0;
            // Block A6 — flush any partial response metrics before marking
            if (auto r = sounding_accumulator_.finalize()) commit_sounding_result(*r);
            if (config_.lqa_exchange_enabled) {
                // A.5.4.5.1: all tried channels failed → deprioritise for next attempt.
                const std::string& tgt = lqa_exchange_.call_target();
                if (!tgt.empty()) {
                    for (const auto& ch : calling_channels_)
                        lqa_database_.record_handshake_fail(ch.rx_frequency_hz, tgt, now_ms_);
                }
                lqa_exchange_.on_call_concluded();
            }
            emit_status("No reply — all calling channels exhausted");
            dispatch(pal::EventType::ALE_LINK_TERMINATED, "No reply");
            break;
        case OperatorEvent::EMERGENCY_ACTIVE:
            emit_status("Emergency manual control is now active");
            dispatch(pal::EventType::SYSTEM_WARNING, "Emergency manual control active");
            break;
    }
}

void ALEController::on_received_word(const ALEWord& word)
{
    rx_log_word(word);
    rx_track_signal_quality(word);
    rx_accumulate_caller_identity(word);
    rx_handle_lqa_exchange(word);
    rx_handle_freq_select(word);
    rx_accumulate_sounding(word);
    rx_accumulate_frame_ber(word);
    rx_accumulate_linked_amd(word);
    rx_accumulate_ack_amd(word);
    sm_.process_received_word(word);
}

void ALEController::rx_log_word(const ALEWord& word)
{
    if (!debug_rx_) return;
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

void ALEController::rx_track_signal_quality(const ALEWord& word)
{
    if (!word.valid) return;

    // Track latest signal-quality stats (get_current_signal_quality).
    constexpr float kMaxVotes = 48.0f;
    last_votes_      = word.unanimous_votes;
    last_fec_errors_ = word.fec_errors;
    last_sinad_db_   = word.sinad_db;                            // actual Goertzel SINAD (A.5.4.1.2)
    last_snr_db_     = (word.unanimous_votes / kMaxVotes) * 31.0f; // votes proxy (internal use only)
    last_ber_        = (word.fec_errors > 0) ? static_cast<float>(word.fec_errors) / 50.0f : 0.0f;

    // Passive monitor tap: neutral decoded-word notification.
    // Fires regardless of local protocol state or address match, in strict
    // on-air arrival order — no reordering, no deferral.
    {
        ale::WordData wd{ WordParser::word_type_name(word.type), word.address,
                          monitor_frame_id_, word.unanimous_votes, word.fec_errors,
                          word.timestamp_ms, get_current_channel().rx_frequency_hz };
        dispatch(pal::EventType::ALE_WORD_DECODED, "", 0, &wd, sizeof(wd));
    }
}

void ALEController::rx_accumulate_caller_identity(const ALEWord& word)
{
    // Capture caller identity as it arrives word-by-word in HANDSHAKE/WAIT_CYCLE_END.
    //
    // Protocol (A.5.2.3.2.1):  TIS:XXX [DATA:YYY [REP:ZZZ [DATA:... [REP:...]]]]
    //   - TIS = anchor word (first 3 chars, possibly @-padded)
    //   - DATA/REP alternates for chars 4-6, 7-9, 10-12, 13-15
    //   - Trailing '@' stuffing is stripped by trim_ale_address()
    //
    // Identity is accumulated here but NOT emitted yet — the single alert fires
    // in tick_sm() once the conclusion has fully settled (WAIT_CYCLE_END exits).
    if (sm_.get_state() != ALEState::HANDSHAKE
        || sm_.get_handshake_phase() != HandshakePhase::WAIT_CYCLE_END)
        return;

    // ── Caller identity (gated on word.valid) ──────────────────────────
    // AMD no longer rides in the calling frame — A.5.7.2.2 places it in the
    // ACK frame, which the responder decodes in rx_accumulate_ack_amd().
    if (word.valid) {
        const std::string chunk = trim_ale_address(word.address);

        if (word.type == PreambleType::TIS && last_caller_.empty()) {
            last_caller_ = chunk;   // conclusion anchor (first 3 chars)
        } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                   && !last_caller_.empty()) {
            last_caller_ += chunk;  // caller-address extension after TIS
        }
    }
}

// ── Linked AMD orderwire RX (A.5.7.2 over an established link) ───────────────
// Reassembles an AMD message that arrives in a linked-orderwire frame
//   TO[peer] (+DATA/REP ext) + CMD AMD + message DATA/REP + TIS[peer]
// We ignore the TO/address-extension prefix (the peer is already known from the
// link state) and collect from the CMD AMD header onward.  A new CMD or the TIS
// conclusion commits the message; Tdrw silence is the fallback (tick_frame_settle).
//
// CMD discrimination: AMD has no fixed identifier (its first 3 chars are message
// content, Expanded-64).  We exclude CMD words owned by other protocols — EFS 'f',
// LQA 'a'/'n'/'r' (handled by rx_handle_lqa_exchange / rx_handle_freq_select) and
// the DTM/DBM identifiers ("DTM"/"DBM", decoded as Basic-38).  This mirrors the
// inherent A.5.7.2.3 ambiguity: an AMD whose text begins with "DTM"/"DBM" is
// misclassified — documented limitation, TX remains correct.
void ALEController::rx_accumulate_linked_amd(const ALEWord& word)
{
    if (sm_.get_state() != ALEState::LINKED) {
        if (linked_amd_collecting_) commit_linked_amd();   // best-effort on exit
        return;
    }

    // Any received word while collecting refreshes the settle deadline.
    if (linked_amd_collecting_)
        linked_amd_settle_ms_ = now_ms_;

    if (word.type == PreambleType::CMD) {
        const uint8_t cc = cmd_char_code(word);
        // CMD 'f'/'a'/'n'/'r' belong to EFS/LQA handlers — they end an in-progress
        // AMD but are not consumed here.
        if (cc == 'f' || cc == 'a' || cc == 'n' || cc == 'r') {
            if (linked_amd_collecting_) commit_linked_amd();
            return;
        }
        // DTM/DBM identifiers (Basic-38) — not AMD.
        char basic38[4] = {};
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::CMD, basic38)
            && (std::string(basic38, 3) == "DTM"
                || std::string(basic38, 3) == "DBM")) {
            if (linked_amd_collecting_) commit_linked_amd();
            return;
        }
        // AMD CMD header — begin a new message (commit any prior first).
        if (linked_amd_collecting_) commit_linked_amd();
        char exp64[4];
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::DATA, exp64)) {
            linked_amd_collecting_ = true;
            linked_amd_acc_        = std::string(exp64, 3);
            linked_amd_peer_       = active_peer();
            linked_amd_settle_ms_  = now_ms_;
        }
        return;
    }

    if (!linked_amd_collecting_)
        return;   // pre-CMD TO/address-extension or stray word — ignore

    if (word.type == PreambleType::DATA || word.type == PreambleType::REP) {
        char exp64[4];
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::DATA, exp64))
            linked_amd_acc_ += std::string(exp64, 3);
        linked_amd_settle_ms_ = now_ms_;
        return;
    }

    // TIS/TWAS conclusion = frame end → commit immediately.
    if (word.type == PreambleType::TIS || word.type == PreambleType::TWAS)
        commit_linked_amd();
}

void ALEController::commit_linked_amd()
{
    if (!linked_amd_collecting_) return;
    linked_amd_collecting_ = false;
    linked_amd_settle_ms_ = 0;
    const auto p = linked_amd_acc_.find_last_not_of(" @");
    if (p != std::string::npos) linked_amd_acc_ = linked_amd_acc_.substr(0, p + 1);
    if (!linked_amd_acc_.empty()) {
        const std::string self = get_primary_self_address();
        ale::AmdData ad{ self.c_str(), linked_amd_peer_.c_str(), linked_amd_acc_.c_str() };
        dispatch(pal::EventType::ALE_AMD_RECEIVED, "", 0, &ad, sizeof(ad));
    }
    linked_amd_acc_.clear();
    linked_amd_peer_.clear();
}

// ── ACK-frame AMD RX (A.5.7.2.2, responder side) ─────────────────────────────
// The caller places AMD in the ACK frame (the third handshake frame):
//   TO[self] ×2 + [CMD 'a'] + [CMD 'r'+DATA…] + [CMD AMD + DATA/REP…] + TIS[caller]
// We receive that frame while in HANDSHAKE/WAIT_ACK.  The leading TO[self] words
// are ignored (the caller identity is already known from WAIT_CYCLE_END); we
// reassemble from the CMD AMD header onward and commit at the TIS conclusion.
// CMD discrimination mirrors rx_accumulate_linked_amd: skip EFS 'f' and LQA
// 'a'/'n'/'r' (handled by rx_handle_lqa_exchange) and DTM/DBM identifiers.
void ALEController::rx_accumulate_ack_amd(const ALEWord& word)
{
    if (sm_.get_state() != ALEState::HANDSHAKE
        || sm_.get_handshake_phase() != HandshakePhase::WAIT_ACK) {
        if (ack_amd_collecting_) commit_ack_amd();   // best-effort on exit
        return;
    }

    if (word.type == PreambleType::CMD) {
        const uint8_t cc = cmd_char_code(word);
        // CMD 'f'/'a'/'n'/'r' belong to EFS/LQA handlers — they end an in-progress
        // AMD but are not consumed here.
        if (cc == 'f' || cc == 'a' || cc == 'n' || cc == 'r') {
            if (ack_amd_collecting_) commit_ack_amd();
            return;
        }
        // DTM/DBM identifiers (Basic-38) — not AMD.
        char basic38[4] = {};
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::CMD, basic38)
            && (std::string(basic38, 3) == "DTM"
                || std::string(basic38, 3) == "DBM")) {
            if (ack_amd_collecting_) commit_ack_amd();
            return;
        }
        // AMD CMD header — begin a new message (commit any prior first).
        if (ack_amd_collecting_) commit_ack_amd();
        char exp64[4];
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::DATA, exp64)) {
            ack_amd_collecting_ = true;
            ack_amd_acc_        = std::string(exp64, 3);
            ack_amd_peer_       = sm_.get_caller_address();
        }
        return;
    }

    if (!ack_amd_collecting_)
        return;   // pre-CMD TO[self]/address-extension or stray word — ignore

    if (word.type == PreambleType::DATA || word.type == PreambleType::REP) {
        char exp64[4];
        if (WordParser::decode_ascii(word.raw_payload, PreambleType::DATA, exp64))
            ack_amd_acc_ += std::string(exp64, 3);
        return;
    }

    // TIS/TWAS conclusion = ACK frame end → commit immediately.
    if (word.type == PreambleType::TIS || word.type == PreambleType::TWAS)
        commit_ack_amd();
}

void ALEController::commit_ack_amd()
{
    if (!ack_amd_collecting_) return;
    ack_amd_collecting_ = false;
    const auto p = ack_amd_acc_.find_last_not_of(" @");
    if (p != std::string::npos) ack_amd_acc_ = ack_amd_acc_.substr(0, p + 1);
    if (!ack_amd_acc_.empty()) {
        const std::string self = get_primary_self_address();
        ale::AmdData ad{ self.c_str(), ack_amd_peer_.c_str(), ack_amd_acc_.c_str() };
        dispatch(pal::EventType::ALE_AMD_RECEIVED, "", 0, &ad, sizeof(ad));
    }
    ack_amd_acc_.clear();
    ack_amd_peer_.clear();
}

void ALEController::rx_handle_lqa_exchange(const ALEWord& word)
{
    if (!config_.lqa_exchange_enabled) return;

    // ── Block A5 — CMD LQA (char 'a') bilateral RX ───────────────────────
    // Captures in HANDSHAKE/WAIT_CYCLE_END (JOE receiving SAM's CMD 'a')
    // and in CALLING/LISTENING (SAM receiving JOE's CMD 'a' in the response).
    // cmd_char_code() reads raw_payload bits directly so CMD detection is
    // independent of the char-set gate and the decoded address[] content;
    // raw_payload is always the authoritative CMD function code.
    {
        const ALEState cur_st = sm_.get_state();
        const bool in_bilateral_window =
            (cur_st == ALEState::HANDSHAKE
                && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END)
            || (cur_st == ALEState::CALLING
                && sm_.get_calling_phase() == CallingPhase::LISTENING);
        if (word.type == PreambleType::CMD && cmd_char_code(word) == 'a'
                && in_bilateral_window) {
            const Channel cur_bilat_ch = get_current_channel();
            // Per-channel inhibit_reporting: do not decode peer's CMD 'a' here.
            if (cur_bilat_ch.rx_frequency_hz > 0
                && !reporting_inhibited(cur_bilat_ch.rx_frequency_hz))
                lqa_exchange_.on_word_lqa_cmd(word.raw_payload,
                                               cur_bilat_ch.rx_frequency_hz);
        }
    }

    // ── Block B4 — CMD NOISE (char 'n') RX ───────────────────────────────
    if (word.type == PreambleType::CMD && cmd_char_code(word) == 'n') {
        if (const Channel* ch = sm_.get_current_channel()) {
            const uint8_t max_db  = (word.raw_payload >> 7) & 0x7Fu;
            const uint8_t mean_db =  word.raw_payload       & 0x7Fu;
            lqa_database_.update_noise_floor(ch->rx_frequency_hz, max_db, mean_db,
                                              word.timestamp_ms);
        }
    }

    // ── Block C5 RX — LQA Report (CMD 'r' header + DATA payloads) ────────
    // Per-channel inhibit_reporting suppresses the bilateral LQA report exchange.
    if (!reporting_inhibited(get_current_frequency())) {
        if (word.type == PreambleType::CMD && cmd_char_code(word) == 'r')
            lqa_exchange_.on_report_cmd(word.raw_payload);
        else if (word.type == PreambleType::DATA) {
            const std::string sender = !sm_.get_to_address().empty()
                ? sm_.get_to_address() : sm_.get_caller_address();
            lqa_exchange_.on_report_data(word.raw_payload, sender,
                                          [this](const std::string& m){ emit_status(m); });
        }
    }
}

void ALEController::rx_handle_freq_select(const ALEWord& word)
{
    // ── EFS: CMD 'f' + DATA 2-word sequence capture (A.5.6.3.2) ─────────
    if (config_.enhanced_freq_select)
        freq_select_.on_word(word, now_ms_, config_.relink_improvement_threshold);
    else
        freq_select_.reset_pending_cmd();
}

void ALEController::rx_accumulate_sounding(const ALEWord& word)
{
    // Accumulate a foreign sounding frame (TIS/TWAS + DATA/REP extension
    // words) received while SCANNING/IDLE (A.5.3.1). Full address committed
    // once the frame settles (Tdrw silence) — see commit_sounding_result()
    // in tick_frame_settle(). Slot-indexed acceptance + cross-cycle voting
    // logic lives in SoundingIdentityAccumulator (see its header for the
    // position-grammar rationale and the TO-field-handshake non-goal note).
    if (!op_params_.lqa_enabled) return;
    const ALEState cur_st = sm_.get_state();
    if (cur_st != ALEState::SCANNING && cur_st != ALEState::IDLE) return;

    // Radio-backed channel (cf. 1562ea9): sm_.get_current_channel()
    // returns nullptr in IDLE state (scan list empty after a
    // single-channel call) so fall back to radio_->get_channel().
    const Channel cur_snd_ch = get_current_channel();
    if (cur_snd_ch.rx_frequency_hz == 0) return;

    if (auto flushed = sounding_accumulator_.on_word(word, cur_snd_ch.rx_frequency_hz, now_ms_))
        commit_sounding_result(*flushed);
}

void ALEController::rx_accumulate_frame_ber(const ALEWord& word)
{
    // A.5.4.1.1 FROM-direction BER for active protocol frames (not soundings):
    //   LINKED            → rx_ber_acc_ (in-link traffic)
    //   CALLING/LISTENING → hs_resp_acc_ (SAM measures JOE's response)
    //   HANDSHAKE/WAIT    → hs_call_acc_ (JOE measures SAM's calling frame)
    if (!op_params_.lqa_enabled) return;
    if (!word.valid && !word.golay_uncorrectable) return;

    const ALEState cur_st = sm_.get_state();

    if (cur_st == ALEState::LINKED) {
        // Radio-backed channel (cf. 1562ea9): sm_.get_current_channel() returns
        // nullptr on no-scan / single-channel links, which would silently drop
        // every LINKED-traffic measurement. get_current_channel() falls back to
        // radio_->get_channel() (cached), which reports the tuned frequency even
        // without a scan list, so the FROM-direction SINAD/BER is accumulated.
        const Channel cur_ch = get_current_channel();
        if (cur_ch.rx_frequency_hz == 0) return;
        constexpr float kMaxVotes = 48.0f;
        const uint8_t non_unanimous = word.golay_uncorrectable
            ? 48u
            : (word.unanimous_votes <= 48u
               ? static_cast<uint8_t>(48u - word.unanimous_votes) : 0u);
        rx_ber_acc_.add_word(non_unanimous, word.golay_uncorrectable);
        rx_ber_snr_sum_   += (word.unanimous_votes / kMaxVotes) * 31.0f;
        rx_ber_sinad_sum_ += word.sinad_db;
        rx_ber_freq_hz_    = cur_ch.rx_frequency_hz;
        rx_ber_settle_ms_  = now_ms_;

    } else if (cur_st == ALEState::CALLING
               && sm_.get_calling_phase() == CallingPhase::LISTENING) {
        // GAP 1 fix: SAM measures JOE's response frame (A.5.4.1.1).
        // Committed to (get_to_address(), freq) at LINK_ESTABLISHED or CALL_REJECTED.
        // A TIS word marks JOE's conclusion → restart so Trs redundancy keeps only
        // the last (best) copy and failed-cycle noise is discarded.
        //
        // Radio-backed channel (cf. 1562ea9): sm_.get_current_channel() is nullptr
        // on no-scan / single-channel links; without this fallback the response-frame
        // FROM measurement is never accumulated and only a bilateral stub survives.
        const Channel cur_ch = get_current_channel();
        if (cur_ch.rx_frequency_hz == 0) return;
        if (word.valid && word.type == PreambleType::TIS)
            hs_resp_acc_.reset();
        // FrameQualityAccumulator defers Golay-uncorrectable words: a trailing
        // post-frame phantom is excluded unless a later valid word flushes it.
        hs_resp_acc_.add_word(word.unanimous_votes,
                              word.golay_uncorrectable, word.sinad_db);
        hs_resp_freq_hz_ = cur_ch.rx_frequency_hz;

    } else if (cur_st == ALEState::HANDSHAKE
               && sm_.get_handshake_phase() == HandshakePhase::WAIT_CYCLE_END) {
        // GAP 2 fix: JOE measures SAM's calling frame (A.5.4.1.1), all word types
        // (TO, TIS, DATA, CMD). Committed in maybe_emit_call_alert() so
        // encode_outgoing() reads fresh data.
        //
        // Radio-backed channel (cf. 1562ea9): sm_.get_current_channel() is nullptr
        // on no-scan / single-channel links; without this fallback the calling-frame
        // FROM measurement (JOE side) is never accumulated.
        const Channel cur_ch = get_current_channel();
        if (cur_ch.rx_frequency_hz == 0) return;
        // FrameQualityAccumulator defers Golay-uncorrectable words so a trailing
        // post-frame phantom is excluded unless a later valid word flushes it.
        hs_call_acc_.add_word(word.unanimous_votes,
                              word.golay_uncorrectable, word.sinad_db);
        hs_call_freq_hz_ = cur_ch.rx_frequency_hz;
    }
}

std::string ALEController::display_state() const
{
    const ALEState st = sm_.get_state();
    if (st == ALEState::HANDSHAKE) {
        // Called station: WAIT_CYCLE_END (listening for the caller's conclusion)
        // and AWAIT_ACCEPT (operator decision gate) are the "incoming" phase —
        // the call has been detected but the response exchange hasn't begun. The
        // response-exchange phases (SLOT_WAIT → CHANNEL_CHECK → SENDING_RESPONSE
        // → WAIT_ACK) are the active "handshake". Splitting here gives the GUI
        // pill the sequence Incoming → Handshake → Linked the operator expects
        // (the call_received alert fires only after WAIT_CYCLE_END, so without
        // this the pill would show Handshake first, then Incoming overwriting
        // it for the rest of the exchange).
        const HandshakePhase hp = sm_.get_handshake_phase();
        if (hp == HandshakePhase::WAIT_CYCLE_END || hp == HandshakePhase::AWAIT_ACCEPT)
            return "INCOMING";
        return "HANDSHAKE";
    }
    if (st == ALEState::CALLING) {
        const CallingPhase cp = sm_.get_calling_phase();
        if (cp == CallingPhase::SENDING_ACK)
            return "HANDSHAKE";
        // LISTENING sub-phase (a) — waiting for "TO SAM" — is not a handshake;
        // only promote to HANDSHAKE once the remote has actually responded.
        if (cp == CallingPhase::LISTENING && sm_.get_response_to_detected())
            return "HANDSHAKE";
    }
    return ALEStateMachine::state_name(st);
}

void ALEController::set_event_handler(pal::IEventHandler* handler)
{
    event_handler_ = handler;
}

void ALEController::dispatch(pal::EventType type, const std::string& msg,
                              int32_t code, const void* data, size_t data_size)
{
    pal::Event ev{};
    ev.type         = type;
    ev.source       = "ALEController";
    ev.message      = msg;
    ev.code         = code;
    ev.timestamp_ms = 0;
    ev.data         = const_cast<void*>(data);
    ev.data_size    = data_size;
    if (event_handler_) event_handler_->emit(ev);
    if (auto* gh = pal::get_event_handler()) gh->emit(ev);
}

void ALEController::emit_status(const std::string& msg)
{
    dispatch(pal::EventType::ALE_STATUS, msg);
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

void ALEController::clear_lqa()
{
    lqa_database_.clear();
}

// ── LQA history (append-only) ────────────────────────────────────────────────

void ALEController::set_lqa_history_config(uint32_t retention_days, bool enabled)
{
    LQAHistoryStore::Config cfg;
    cfg.retention_days = retention_days;
    cfg.enabled        = enabled;
    lqa_history_.set_config(cfg);
}

bool ALEController::load_lqa_history(const std::string& path)
{
    const bool loaded = lqa_history_.load_from_file(path);
    lqa_history_.open_append(path);
    return loaded;
}

std::vector<LQAHistorySample> ALEController::get_lqa_history(uint64_t since_ms,
                                                               const std::string& station,
                                                               uint32_t freq_hz,
                                                               size_t limit) const
{
    return lqa_history_.query(since_ms, station, freq_hz, limit);
}

bool ALEController::clear_lqa_history(const std::string& path)
{
    return lqa_history_.clear_and_truncate(path);
}

void ALEController::record_lqa_history(uint32_t freq_hz, const std::string& station)
{
    auto e = lqa_database_.get_entry(freq_hz, station);
    if (!e) return;
    const uint64_t ts_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    lqa_history_.record({ts_ms, freq_hz, station, e->sinad_db, e->ber, e->score});
}

void ALEController::enable_automatic_sounding(bool on)
{
    AnalyzerConfig cfg = lqa_analyzer_.get_config();
    cfg.enable_automatic_sounding = on;
    lqa_analyzer_.set_config(cfg);
}

void ALEController::set_automatic_sounding(bool on, const std::string& net_name)
{
    // Idempotency guard: the GUI re-asserts SOUND_AUTO on almost every settings
    // save and net (re)selection, even when the on/off + net target hasn't
    // actually changed. Without this, each redundant call re-logged "Periodic
    // multi-channel sounding on/off", burying real ALE events in status spam,
    // and reset the sounding timer on every no-op "on" reassertion. Interval-
    // only changes are picked up separately via refresh_auto_sounding_interval()
    // (NET_UPDATE / net-rename handlers), so skipping here is safe.
    const bool new_on = on && !net_name.empty();
    const std::string new_net = on ? net_name : std::string{};
    if (new_on == auto_sounding_on_ && new_net == auto_sounding_net_) return;

    // Cancel an active warning popup before changing state.
    if (sounding_warning_active_) {
        ale::SoundingWarningData swd{ auto_sounding_net_.c_str(), 0, "cancel" };
        dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
    }
    sounding_warning_sent_   = false;
    sounding_warning_active_ = false;

    auto_sounding_on_  = on && !net_name.empty();
    auto_sounding_net_ = on ? net_name : std::string{};

    if (auto_sounding_on_) {
        // Resolve interval from the net's own policy; fall back to global default.
        const Net* net = net_store_.find(net_name);
        const uint32_t net_sec = (net && net->sounding_interval_sec > 0)
                                 ? net->sounding_interval_sec
                                 : config_.sounding_interval_sec;
        auto_sounding_interval_ms_ = net_sec * 1000u;
        // Arm from now — don't fire immediately (operator may still be configuring).
        auto_sounding_last_ms_ = now_ms_;
        emit_status("Periodic sounding on net '" + net_name + "' every "
                    + std::to_string(net_sec) + " s");
    } else {
        auto_sounding_interval_ms_ = 0u;
        emit_status("Periodic multi-channel sounding off");
    }
}

void ALEController::refresh_auto_sounding_interval()
{
    if (!auto_sounding_on_ || auto_sounding_net_.empty()) return;
    const Net* net = net_store_.find(auto_sounding_net_);
    const uint32_t net_sec = (net && net->sounding_interval_sec > 0)
                             ? net->sounding_interval_sec
                             : config_.sounding_interval_sec;
    auto_sounding_interval_ms_ = net_sec * 1000u;
}

void ALEController::set_sounding_warning_lead_sec(uint32_t sec)
{
    config_.sounding_warning_lead_sec = sec;
    sounding_warning_lead_ms_         = sec * 1000u;
}

void ALEController::interrupt_sounding(const std::string& net)
{
    if (net != auto_sounding_net_) {
        emit_status("Sounding interrupt ignored — net '" + net + "' is not the active sounding net");
        return;
    }
    if (sm_.get_state() == ALEState::SOUNDING) {
        emit_status("Sounding already in progress — too late to interrupt");
        return;
    }
    // Reset timer to full interval (per spec: "set back to its original value").
    auto_sounding_last_ms_   = now_ms_;
    sounding_warning_sent_   = false;
    if (sounding_warning_active_) {
        sounding_warning_active_ = false;
        ale::SoundingWarningData swd{ net.c_str(), 0, "cancel" };
        dispatch(pal::EventType::ALE_SOUNDING_WARNING, "", 0, &swd, sizeof(swd));
    }
    emit_status("Sounding on net '" + net + "' interrupted — timer reset");
}

std::vector<Channel> ALEController::resolve_net_sounding_channels(
    const std::string& net_name) const
{
    std::vector<Channel> out;
    const Net* net = net_store_.find(net_name);
    if (!net) return out;
    for (const auto& ch_id : net->channel_ids) {
        for (const auto& ch : calling_channels_) {
            if (ch.id == ch_id && ch.enabled && !ch.inhibit_sounding && !ch.rx_only) {
                out.push_back(ch);
                break;
            }
        }
    }
    return out;
}

uint32_t ALEController::resolve_sounding_C(const std::string& net_name) const
{
    // Same "call width" C calling uses (Tsc = C·2·Trw), from the net's
    // calling_length_c policy.  Precedence: the named net, then the auto-sounding
    // net, then the active scan net, then the global assumed_scan_channels default.
    auto C_of = [this](const std::string& nm) -> uint32_t {
        if (!nm.empty())
            if (const Net* n = net_store_.find(nm)) return n->calling_length_c;
        return 0;  // 0 = "not found" sentinel; caller falls through.
    };
    uint32_t C = C_of(net_name);
    if (C == 0) C = C_of(auto_sounding_net_);
    if (C == 0) C = C_of(active_scan_net_);
    if (C == 0) C = config_.assumed_scan_channels;
    return C;
}

void ALEController::set_sounding_interval_sec(uint32_t sec)
{
    config_.sounding_interval_sec = sec;
    AnalyzerConfig cfg = lqa_analyzer_.get_config();
    cfg.sounding_interval_ms = sec * 1000u;
    lqa_analyzer_.set_config(cfg);
}

void ALEController::set_sounding_use_twas(bool use_twas)
{
    config_.sounding_use_twas = use_twas;
    sm_.set_sounding_use_twas(use_twas);
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

void ALEController::reset_link_idle_timer()
{
    sm_.reset_link_idle_timer();
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
        if (e.remote_station.empty()) continue;  // internal channel-aggregate; not for GUI
        // Skip penalty-only stubs (record_handshake_fail creates an entry with
        // no FROM measurements and no bilateral data solely for A.5.4.5.1
        // deprioritisation).  They have no displayable quality data and would
        // appear as phantom rows in the GUI heard panel.
        if (e.total_words == 0 && e.bilateral_ber == 31u && e.bilateral_sinad == 31u) continue;
        const uint32_t age_ms = (now > e.last_activity_ms()) ? (now - e.last_activity_ms()) : 0u;
        // Fields: freq|station|snr_db|ber|sinad_db|score|age_ms
        //        |bilateral_sinad|bilateral_ber|bilateral_mp|display_score|available
        // score already incorporates the bilateral fallback (see compute_score),
        // so display_score == score; bilateral_* are shipped so the GUI can show
        // the peer-reported SINAD/BER/MP when no local FROM measurement exists.
        // available: sounding-conclusion availability flag — 1 = TIS (available
        // for active link establishment), 0 = TWAS (not available), -1 = no
        // sounding heard from this station (entry from a contact only).
        const int available = (e.last_sounding_ms > 0)
            ? (e.sounding_twas ? 0 : 1)
            : -1;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "%u|%s|%.1f|%.4f|%.1f|%.1f|%u|%u|%u|%u|%.1f|%d",
                      e.frequency_hz, e.remote_station.c_str(),
                      e.snr_db, e.ber, e.sinad_db, e.score, age_ms,
                      static_cast<unsigned>(e.bilateral_sinad),
                      static_cast<unsigned>(e.bilateral_ber),
                      static_cast<unsigned>(e.bilateral_mp),
                      e.score, available);
        out.push_back(buf);
    }
    return out;
}

bool ALEController::is_link_active() const
{
    return state() == ALEState::LINKED;
}

bool ALEController::is_tx_active() const
{
    return ptt_lead_deadline_ms_ > 0
        || !pending_tx_words_.empty()
        || modulator_.is_transmitting();
}

uint32_t ALEController::get_call_duration_seconds() const
{
    if (link_start_ms_ == 0 || now_ms_ < link_start_ms_) return 0;
    return (now_ms_ - link_start_ms_) / 1000u;
}

ALEController::SignalQuality ALEController::get_current_signal_quality() const
{
    SignalQuality q;
    q.snr_db   = last_snr_db_;
    q.ber      = last_ber_;
    q.votes    = static_cast<int8_t>(last_votes_);
    q.fec_errors = last_fec_errors_;
    q.sinad_db = last_sinad_db_;  // Goertzel SINAD (A.5.4.1.2); upgraded below if LQA entry exists

    const Channel* ch = sm_.get_current_channel();
    if (ch) {
        const std::string peer = !sm_.get_to_address().empty()
            ? sm_.get_to_address() : sm_.get_caller_address();
        if (auto e = lqa_database_.get_entry(ch->rx_frequency_hz, peer)) {
            // Prefer measured Goertzel SINAD (A.5.4.1.2) over votes-based approximation.
            if (e->sinad_db > 0.0f) {
                q.sinad_db     = e->sinad_db;
                q.multipath_ms = e->multipath_score;
            }
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

bool ALEController::sync_radio_state()
{
    return radio_ ? radio_->sync_from_radio() : false;
}

std::string ALEController::get_rig_connection_status() const
{
    if (!radio_) return "not attached";
    return radio_->is_ready() ? "ready" : "not ready";
}

// ── Station position + propagation context ────────────────────────────────────

void ALEController::update_propagation_context() {
    using PS = ALEStationConfig::PositionSource;
    PropagationContext ctx;
    ctx.now_ms      = now_ms_;
    ctx.sfi_current = current_sfi_;

    if (config_.position_source == PS::GPSD || config_.position_source == PS::NMEA_SERIAL) {
        ctx.position_valid = gps_fix_valid_;
        ctx.lat_deg        = gps_lat_;
        ctx.lon_deg        = gps_lon_;
    } else if (config_.position_source != PS::NONE) {
        ctx.position_valid = true;
        ctx.lat_deg        = config_.station_lat_deg;
        ctx.lon_deg        = config_.station_lon_deg;
    }
    lqa_analyzer_.set_propagation_context(ctx);
}

void ALEController::set_station_position_manual(double lat_deg, double lon_deg) {
    config_.station_lat_deg = lat_deg;
    config_.station_lon_deg = lon_deg;
    config_.position_source = ALEStationConfig::PositionSource::MANUAL;
    update_propagation_context();
}

bool ALEController::set_station_position_grid(const std::string& grid) {
    double lat, lon;
    if (!ale::maidenhead_to_latlon(grid, lat, lon)) return false;
    config_.grid_locator    = grid;
    config_.station_lat_deg = lat;
    config_.station_lon_deg = lon;
    config_.position_source = ALEStationConfig::PositionSource::MAIDENHEAD;
    update_propagation_context();
    return true;
}

void ALEController::set_position_source(ALEStationConfig::PositionSource src) {
    config_.position_source = src;
    update_propagation_context();
}

void ALEController::set_gpsd_config(const std::string& host, uint16_t port) {
    config_.gpsd_host = host;
    config_.gpsd_port = port;
}

void ALEController::set_nmea_config(const std::string& port, uint32_t baud) {
    config_.nmea_port = port;
    config_.nmea_baud = baud;
}

void ALEController::set_gps_fix(bool valid, double lat_deg, double lon_deg) {
    gps_fix_valid_ = valid;
    if (valid) { gps_lat_ = lat_deg; gps_lon_ = lon_deg; }
    update_propagation_context();
}

void ALEController::set_current_sfi(float sfi) {
    current_sfi_ = sfi;
    update_propagation_context();
}

bool ALEController::export_settings(const std::string& path)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# ALE settings export\n";
    for (const auto& e : self_address_store_.all())
        f << "self_address=" << e.address << "\n";
    if (!station_file_.empty())
        f << "station_file=" << station_file_ << "\n";
    f << "assumed_scan_channels=" << config_.assumed_scan_channels << "\n";
    f << "golay_mode=" << static_cast<int>(config_.golay_mode) << "\n";
    f << "min_unanimous_votes=" << static_cast<int>(config_.min_unanimous_votes) << "\n";
    f << "adaptive_fec=" << (config_.adaptive_fec ? 1 : 0) << "\n";
    f << "debug_rx=" << (config_.debug_rx ? 1 : 0) << "\n";
    f << "manual_accept_mode=" << (config_.manual_accept_mode ? 1 : 0) << "\n";
    f << "manual_accept_timeout_ms=" << config_.accept_timeout_ms << "\n";
    f << "scan_dwell_ms=" << config_.scan_dwell_ms << "\n";
    f << "scan_squelch_enabled=" << (scan_squelch_enabled() ? 1 : 0) << "\n";
    f << "scan_detect_margin_db=" << scan_detect_margin_db() << "\n";
    f << "sounding_interval_sec=" << config_.sounding_interval_sec << "\n";
    f << "sounding_use_twas=" << (config_.sounding_use_twas ? "1" : "0") << "\n";
    f << "sounding_warning_lead_sec=" << config_.sounding_warning_lead_sec << "\n";
    f << "link_idle_timeout_sec=" << config_.link_idle_timeout_sec << "\n";
    f << "max_tune_time_ms=" << config_.max_tune_time_ms << "\n";
    f << "ptt_lead_ms=" << config_.ptt_lead_ms << "\n";
    f << "ptt_tail_ms=" << config_.ptt_tail_ms << "\n";
    f << "lqa_exchange_enabled=" << (config_.lqa_exchange_enabled ? 1 : 0) << "\n";
    f << "lqa_enabled=" << (lqa_enabled() ? 1 : 0) << "\n";
    f << "relink_enabled=" << (config_.relink_enabled ? 1 : 0) << "\n";
    f << "relink_improvement_threshold=" << config_.relink_improvement_threshold << "\n";
    f << "enhanced_freq_select=" << (config_.enhanced_freq_select ? 1 : 0) << "\n";
    f << "position_source=" << static_cast<int>(config_.position_source) << "\n";
    f << "station_lat_deg=" << config_.station_lat_deg << "\n";
    f << "station_lon_deg=" << config_.station_lon_deg << "\n";
    f << "grid_locator=" << config_.grid_locator << "\n";
    f << "gpsd_host=" << config_.gpsd_host << "\n";
    f << "gpsd_port=" << config_.gpsd_port << "\n";
    f << "nmea_port=" << config_.nmea_port << "\n";
    f << "nmea_baud=" << config_.nmea_baud << "\n";
    f << "sfi_enabled=" << (config_.sfi_enabled ? 1 : 0) << "\n";
    const auto& stations = sm_.get_address_book().all_stations();
    if (!stations.empty()) {
        f << "\n# ALE address export\n";
        for (const auto& [addr, name] : stations)
            f << "station=" << addr << '|' << name << '\n';
    }
    return f.good();
}

bool ALEController::import_settings(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    ALEStationConfig cfg = config_;  // start from current defaults
    bool has_lqa_enabled = false;
    bool lqa_enabled_val = lqa_enabled();
    std::string station_file_to_load;
    bool first_self_addr = true;
    // Stations from ale.conf (# ALE address export section). Applied after
    // load_station_file so entries already in the station file are not duplicated.
    std::vector<std::pair<std::string, std::string>> conf_stations;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "self_address" && !val.empty()) {
            if (first_self_addr) { self_address_store_.clear(); first_self_addr = false; }
            add_self_address(val);
            set_primary_self_address(val);
        } else if (key == "station_file" && !val.empty()) {
            station_file_to_load = val;
        } else if (key == "channel_file" && !val.empty()) {
            station_file_to_load = val;  // legacy alias
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
        } else if (key == "sounding_use_twas") {
            cfg.sounding_use_twas = (val == "1" || val == "true");
        } else if (key == "sounding_warning_lead_sec") {
            cfg.sounding_warning_lead_sec = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "link_idle_timeout_sec") {
            cfg.link_idle_timeout_sec = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "max_tune_time_ms") {
            cfg.max_tune_time_ms = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "ptt_lead_ms") {
            cfg.ptt_lead_ms = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "ptt_tail_ms") {
            cfg.ptt_tail_ms = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "lqa_exchange_enabled") {
            cfg.lqa_exchange_enabled = (val == "1");
        } else if (key == "lqa_enabled") {
            lqa_enabled_val = (val == "1");
            has_lqa_enabled = true;
        } else if (key == "scan_squelch_enabled") {
            set_scan_squelch_enabled(val == "1");   // detector member, not in cfg
        } else if (key == "scan_detect_margin_db") {
            set_scan_detect_margin_db(std::stof(val));
        } else if (key == "relink_enabled") {
            cfg.relink_enabled = (val == "1");
        } else if (key == "relink_improvement_threshold") {
            cfg.relink_improvement_threshold = std::stof(val);
        } else if (key == "enhanced_freq_select") {
            cfg.enhanced_freq_select = (val == "1");
        } else if (key == "position_source") {
            cfg.position_source = static_cast<ALEStationConfig::PositionSource>(std::stoi(val));
        } else if (key == "station_lat_deg") {
            cfg.station_lat_deg = std::stod(val);
        } else if (key == "station_lon_deg") {
            cfg.station_lon_deg = std::stod(val);
        } else if (key == "grid_locator") {
            cfg.grid_locator = val;
        } else if (key == "gpsd_host") {
            cfg.gpsd_host = val;
        } else if (key == "gpsd_port") {
            cfg.gpsd_port = static_cast<uint16_t>(std::stoul(val));
        } else if (key == "nmea_port") {
            cfg.nmea_port = val;
        } else if (key == "nmea_baud") {
            cfg.nmea_baud = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "sfi_enabled") {
            cfg.sfi_enabled = (val == "1");
        } else if (key == "station" && !val.empty()) {
            // station=callsign|name  (new format)
            const auto sep = val.find('|');
            const std::string cs = val.substr(0, sep);
            const std::string nm = (sep != std::string::npos) ? val.substr(sep + 1) : "";
            if (!cs.empty()) conf_stations.push_back({cs, nm});
        } else if (key == "contact" && !val.empty()) {
            // contact=callsign|name|...  (legacy — use only first two fields)
            const auto fields = split_pipe(val);
            if (!fields.empty() && !fields[0].empty())
                conf_stations.push_back({fields[0], fields.size() > 1 ? fields[1] : ""});
        }
        // net= / net_channel=: legacy keys, silently ignored (nets now in station file)
        // audio_in / audio_out: bridge-level, silently ignored here
    }
    apply_config(cfg);
    if (has_lqa_enabled) set_lqa_enabled(lqa_enabled_val);
    if (!station_file_to_load.empty())
        load_station_file(station_file_to_load);  // sets station_file_ on success
    // Populate address book from ale.conf stations (skip callsigns already loaded
    // from the station file to avoid duplicates).
    for (const auto& [cs, name] : conf_stations) {
        if (!sm_.get_address_book().is_known_station(cs))
            sm_.get_address_book().update_station(cs, name);
    }
    update_propagation_context();
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
    if (!station_file_.empty()) save_channels(station_file_);
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
    if (!station_file_.empty()) save_channels(station_file_);
    return true;
}

bool ALEController::rename_channel(const std::string& old_id, const std::string& new_id)
{
    if (old_id == new_id) return true;
    if (new_id.empty()) return false;
    // Reject whitespace in the id (would break the .ale token format).
    if (new_id.find_first_of(" \t\r\n") != std::string::npos) return false;

    auto it = std::find_if(calling_channels_.begin(), calling_channels_.end(),
        [&](const Channel& c){ return c.id == old_id; });
    if (it == calling_channels_.end()) return false;                 // unknown old_id

    // Reject collision with another channel that already has new_id.
    for (const auto& c : calling_channels_)
        if (c.id == new_id) return false;

    it->id = new_id;
    net_store_.rename_channel(old_id, new_id);                       // propagate to nets
    sm_.set_calling_channels(calling_channels_);
    notify_channel_changed_(*it);                 // GUI readout follows rename
    if (!station_file_.empty()) save_channels(station_file_);
    return true;
}

bool ALEController::set_channel_enabled(const std::string& id, bool enabled)
{
    auto it = std::find_if(calling_channels_.begin(), calling_channels_.end(),
        [&](const Channel& c){ return c.id == id; });
    if (it == calling_channels_.end()) return false;
    if (it->enabled == enabled) return true;                          // no-op if unchanged
    it->enabled = enabled;
    sm_.set_calling_channels(calling_channels_);
    if (!station_file_.empty()) save_channels(station_file_);
    return true;
}

bool ALEController::set_channel_mode(const std::string& id, const std::string& mode)
{
    if (mode.empty()) return false;
    auto it = std::find_if(calling_channels_.begin(), calling_channels_.end(),
        [&](const Channel& c){ return c.id == id; });
    if (it == calling_channels_.end()) return false;
    it->rx_mode = mode;
    it->tx_mode = mode;
    sm_.set_calling_channels(calling_channels_);
    if (!station_file_.empty()) save_channels(station_file_);
    // Re-assert on the radio if this is the currently-active channel so the
    // override takes effect immediately (freq-first/mode-last, set_vfo_channel).
    if (radio_) {
        const Channel* cur = sm_.get_current_channel();
        if (cur && cur->rx_frequency_hz == it->rx_frequency_hz)
            set_vfo_channel(it->rx_frequency_hz, mode);
    }
    return true;
}

bool ALEController::load_station_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<Channel> loaded;
    net_store_.clear();
    contact_store_.clear();
    group_call_store_.clear();

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("NET:", 0) == 0) {
            std::istringstream iss(line.substr(4));
            std::string name;
            iss >> name;
            if (name.empty()) continue;
            net_store_.add_net(name);
            Net policy;
            policy.name = name;
            std::string tok;
            while (iss >> tok) {
                const auto eq = tok.find('=');
                if (eq != std::string::npos) {
                    const std::string key = tok.substr(0, eq);
                    const std::string val = tok.substr(eq + 1);
                    try {
                        if      (key == "dwell")  policy.dwell_ms              = static_cast<uint32_t>(std::stoul(val));
                        else if (key == "scan")   policy.scanning_enabled       = (val != "0");
                        else if (key == "sound")  policy.sounding_enabled       = (val != "0");
                        else if (key == "sndint") policy.sounding_interval_sec  = static_cast<uint32_t>(std::stoul(val));
                        else if (key == "c")      policy.calling_length_c       = static_cast<uint32_t>(std::stoul(val));
                    } catch (...) {}
                } else {
                    for (const auto& id : split_csv(tok))
                        net_store_.assign_channel(name, id);
                }
            }
            net_store_.update_net(policy);
            continue;
        }

        if (line.rfind("CONTACT:", 0) == 0) {
            // CONTACT:callsign|name|status|net_members_csv|valid_channels_csv
            const auto fields = split_pipe(line.substr(8));
            if (fields.empty() || fields[0].empty()) continue;
            const std::string cs      = fields[0];
            const std::string name    = fields.size() > 1 ? fields[1] : "";
            const std::string status  = fields.size() > 2 ? fields[2] : "enabled";
            const std::string nets    = fields.size() > 3 ? fields[3] : "";
            const std::string chans   = fields.size() > 4 ? fields[4] : "ALL";
            add_contact(cs, name, status, nets, chans);
            continue;
        }

        if (line.rfind("GROUP:", 0) == 0) {
            // GROUP:name:member1,member2,...
            const std::string rest = line.substr(6);
            const auto colon = rest.find(':');
            if (colon == std::string::npos) continue;
            const std::string roster_name = rest.substr(0, colon);
            if (roster_name.empty()) continue;
            group_call_store_.add_roster(roster_name);
            for (const auto& m : split_csv(rest.substr(colon + 1)))
                group_call_store_.add_member(roster_name, m);
            continue;
        }

        if (line.rfind("ALLCALL:", 0) == 0) {
            // ALLCALL:key=val
            const std::string kv = line.substr(8);
            const auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = kv.substr(0, eq);
            const std::string val = kv.substr(eq + 1);
            if (key == "accept")   all_call_config_.accept   = (val != "0");
            else if (key == "selector" && !val.empty()) all_call_config_.selector = val[0];
            continue;
        }

        auto ch = parse_channel_spec(line);
        if (ch) loaded.push_back(*ch);
    }
    for (auto& ch : loaded)
        if (ch.id.empty()) ch.id = next_free_channel_id(loaded);
    calling_channels_ = std::move(loaded);
    sm_.set_calling_channels(calling_channels_);
    station_file_ = path;
    return true;
}

bool ALEController::save_station_file(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# openALE station file — MIL-STD-188-141B\n";
    f << "# ID:id rx_hz tx_hz mode [flags] [label]   flags=[OFF,RX,IC,IS,IR]\n";
    for (const auto& ch : calling_channels_)
        f << format_channel_line(ch) << '\n';
    if (!net_store_.empty()) {
        f << "# NET:name id,id,...  [dwell=ms] [scan=0|1] [sound=0|1] [sndint=sec] [c=N]\n";
        for (const auto& net : net_store_.all()) {
            f << "NET:" << net.name << ' ' << join_csv(net.channel_ids);
            f << " dwell=" << net.dwell_ms;
            f << " scan="  << (net.scanning_enabled  ? '1' : '0');
            f << " sound=" << (net.sounding_enabled   ? '1' : '0');
            f << " sndint=" << net.sounding_interval_sec;
            f << " c=" << net.calling_length_c;
            f << '\n';
        }
    }
    if (!contact_store_.empty()) {
        f << "# CONTACT:callsign|name|status|net_members|valid_channels\n";
        for (const auto& c : contact_store_.all()) {
            f << "CONTACT:" << c.callsign << '|' << c.name
              << '|' << (c.enabled ? "enabled" : "disabled")
              << '|' << join_csv(c.net_members)
              << '|' << (c.all_channels ? "ALL" : join_csv(c.valid_channels))
              << '\n';
        }
    }
    if (!group_call_store_.empty()) {
        f << "# GROUP:name:member1,member2,...\n";
        for (const auto& r : group_call_store_.all())
            f << "GROUP:" << r.name << ':' << join_csv(r.members) << '\n';
    }
    f << "# ALLCALL:key=val\n";
    f << "ALLCALL:selector=" << all_call_config_.selector << '\n';
    f << "ALLCALL:accept=" << (all_call_config_.accept ? '1' : '0') << '\n';
    return f.good();
}

// ── Nets ─────────────────────────────────────────────────────────────────────

bool ALEController::add_net(const std::string& name)
{
    const bool added = net_store_.add_net(name);
    if (added && !station_file_.empty()) save_channels(station_file_);
    return added;
}

bool ALEController::del_net(const std::string& name)
{
    const bool removed = net_store_.remove_net(name);
    if (removed && !station_file_.empty()) save_channels(station_file_);
    return removed;
}

bool ALEController::assign_channel_to_net(const std::string& net_name, const std::string& channel_id)
{
    const bool ok = net_store_.assign_channel(net_name, channel_id);
    if (ok && !station_file_.empty()) save_channels(station_file_);
    return ok;
}

bool ALEController::unassign_channel_from_net(const std::string& net_name, const std::string& channel_id)
{
    const bool ok = net_store_.unassign_channel(net_name, channel_id);
    if (ok && !station_file_.empty()) save_channels(station_file_);
    return ok;
}

bool ALEController::update_net(const Net& updated)
{
    const bool ok = net_store_.update_net(updated);
    if (ok) {
        if (!station_file_.empty()) save_channels(station_file_);
        // Live-update the sounding timer if the active sounding net was changed.
        if (updated.name == auto_sounding_net_) refresh_auto_sounding_interval();
    }
    return ok;
}

bool ALEController::rename_net(const std::string& old_name, const std::string& new_name)
{
    if (old_name == new_name) return true;
    if (new_name.empty()) return false;
    if (!net_store_.rename_net(old_name, new_name)) return false;

    if (active_scan_net_ == old_name) active_scan_net_ = new_name;
    if (auto_sounding_net_ == old_name) {
        auto_sounding_net_ = new_name;
        refresh_auto_sounding_interval();
    }

    const std::vector<Contact> all_contacts = contact_store_.all();   // snapshot first
    for (const auto& c : all_contacts) {
        if (std::find(c.net_members.begin(), c.net_members.end(), old_name) == c.net_members.end())
            continue;
        Contact updated = c;
        std::replace(updated.net_members.begin(), updated.net_members.end(), old_name, new_name);
        contact_store_.add_or_update(updated);
    }

    if (!station_file_.empty()) save_station_file(station_file_);
    return true;
}

// ── Group-call rosters ────────────────────────────────────────────────────────

bool ALEController::add_group_roster(const std::string& name)
{
    const bool ok = group_call_store_.add_roster(name);
    if (ok && !station_file_.empty()) save_station_file(station_file_);
    return ok;
}

bool ALEController::del_group_roster(const std::string& name)
{
    const bool ok = group_call_store_.remove_roster(name);
    if (ok && !station_file_.empty()) save_station_file(station_file_);
    return ok;
}

bool ALEController::add_group_member(const std::string& roster_name, const std::string& callsign)
{
    const bool ok = group_call_store_.add_member(roster_name, callsign);
    if (ok && !station_file_.empty()) save_station_file(station_file_);
    return ok;
}

bool ALEController::del_group_member(const std::string& roster_name, const std::string& callsign)
{
    const bool ok = group_call_store_.remove_member(roster_name, callsign);
    if (ok && !station_file_.empty()) save_station_file(station_file_);
    return ok;
}

bool ALEController::initiate_group_call(const std::string& roster_name)
{
    const GroupCallRoster* r = group_call_store_.find(roster_name);
    if (!r || r->members.empty()) {
        emit_status("Group roster '" + roster_name + "' not found or empty");
        return false;
    }
    return initiate_group_call(r->members);
}

bool ALEController::initiate_all_call(char selector)
{
    emit_status(std::string("AllCall TX not yet implemented (stub) — selector='") + selector + "'");
    return false;
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
    const bool ok = contact_store_.add_or_update(c);
    if (ok && !station_file_.empty()) save_station_file(station_file_);
    return ok;
}

bool ALEController::remove_contact(const std::string& callsign)
{
    const bool removed = contact_store_.remove(callsign);
    if (removed && selected_contact_ == callsign) selected_contact_.clear();
    if (removed && !station_file_.empty()) save_station_file(station_file_);
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
    if (cmd == "CMD:STOP_SCANNING" || cmd == "CMD:AVAILABLE") {
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
        if (!station_file_.empty()) save_channels(station_file_);
        return "OK: channel list cleared";
    }
    if (cmd.rfind("CMD:SAVE_CHANNELS", 0) == 0) {
        std::string path = cmd_trim(cmd.substr(17));
        if (path.empty()) path = station_file_;
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
    if (cmd.rfind("CMD:RENAME_NET ", 0) == 0) {
        std::istringstream iss(cmd.substr(15));   // "CMD:RENAME_NET ".size()
        std::string old_name, new_name;
        iss >> old_name >> new_name;
        if (old_name.empty() || new_name.empty())
            return "ERROR: CMD:RENAME_NET <old_name> <new_name>";
        if (!rename_net(old_name, new_name))
            return "ERROR: net '" + old_name + "' not found or '" + new_name + "' already exists";
        return "OK: net " + old_name + " renamed to " + new_name;
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
        const std::string args = cmd_trim(cmd.substr(8));
        if (args.empty())
            return "ERROR: CMD:AMD requires message text";
        // When LINKED, the active peer receives the message.
        // Accept (and ignore) any <addr> prefix so the bridge can always call
        // CMD:AMD <addr> <text> regardless of link state.
        if (is_link_active()) {
            const auto sp = args.find(' ');
            if (sp == std::string::npos)
                return "ERROR: CMD:AMD <ADDR> <text> -- message text is empty";
            const std::string text = cmd_trim(args.substr(sp + 1));
            if (text.empty())
                return "ERROR: CMD:AMD <ADDR> <text> -- message text is empty";
            return send_amd(active_peer(), text);
        }
        const auto sp = args.find(' ');
        if (sp == std::string::npos)
            return "ERROR: CMD:AMD <ADDR> <text> -- address required when not linked";
        const std::string addr = args.substr(0, sp);
        const std::string text = cmd_trim(args.substr(sp + 1));
        if (text.empty())
            return "ERROR: CMD:AMD <ADDR> <text> -- message text is empty";
        return send_amd(addr, text);
    }
    // -- Sounding -----------------------------------------------------------------
    if (cmd == "CMD:SOUND") {
        if (!send_sounding())
            return std::string("ERROR: cannot sound in state ")
                   + ALEStateMachine::state_name(state());
        return "OK: sounding";
    }
    if (cmd.rfind("CMD:SOUND_SWEEP ", 0) == 0) {
        const std::string net_name = cmd_trim(cmd.substr(16));
        if (net_name.empty())
            return "ERROR: CMD:SOUND_SWEEP requires a net name";
        const std::vector<Channel> chans = resolve_net_sounding_channels(net_name);
        if (chans.empty())
            return "ERROR: net '" + net_name + "' not found or has no soundable channels";
        // Sounding uses the same "call width" C as calling (Tsrs = (C+2)·Ta) —
        // take it from this net's calling_length_c.  See resolve_sounding_C().
        sm_.set_target_scan_channels(resolve_sounding_C(net_name));
        if (!send_sounding_sweep(chans))
            return std::string("ERROR: cannot sweep in state ")
                   + ALEStateMachine::state_name(state());
        char buf[80];
        std::snprintf(buf, sizeof(buf), "OK: sounding sweep on %zu channel(s)", chans.size());
        return buf;
    }
    if (cmd.rfind("CMD:SOUND_AUTO ", 0) == 0) {
        std::istringstream iss(cmd.substr(15));
        std::string flag, net_name;
        iss >> flag;
        std::getline(iss, net_name);
        net_name = cmd_trim(net_name);
        if (flag != "on" && flag != "off")
            return "ERROR: CMD:SOUND_AUTO on|off [net]";
        set_automatic_sounding(flag == "on", net_name);
        return std::string("OK: auto-sounding ") + flag
               + (net_name.empty() ? "" : " net=" + net_name);
    }
    if (cmd.rfind("CMD:SOUND_INTERRUPT ", 0) == 0) {
        const std::string net_name = cmd_trim(cmd.substr(20));
        if (net_name.empty())
            return "ERROR: CMD:SOUND_INTERRUPT requires a net name";
        interrupt_sounding(net_name);
        return "OK: sounding interrupted for net " + net_name;
    }
    // -- Single-channel / group call ----------------------------------------------
    if (cmd.rfind("CMD:SINGLE_CALL ", 0) == 0) {
        const std::string target = cmd_trim(cmd.substr(16));
        if (target.empty())
            return "ERROR: CMD:SINGLE_CALL requires a target address";
        if (!initiate_single_channel_call(target))
            return std::string("ERROR: cannot call in state ")
                   + ALEStateMachine::state_name(state());
        return "OK: single-channel call to " + target;
    }
    if (cmd.rfind("CMD:TEST_CHANNEL", 0) == 0) {
        // CMD:TEST_CHANNEL_STOP has no args; CMD:TEST_CHANNEL takes <ADDR> [net].
        if (cmd == "CMD:TEST_CHANNEL_STOP") {
            if (!test_channel_active())
                return "ERROR: no test-channel sweep in progress";
            stop_test_channel();
            return "OK: test-channel stopped";
        }
        if (cmd.size() > 16 && cmd[16] == ' ') {
            std::istringstream iss(cmd.substr(17));
            std::string target, net;
            iss >> target;
            std::getline(iss, net);
            net = cmd_trim(net);
            if (target.empty())
                return "ERROR: CMD:TEST_CHANNEL <ADDR> [net]";
            if (!start_test_channel(target, net))
                return std::string("ERROR: cannot start test-channel in state ")
                       + ALEStateMachine::state_name(state());
            return "OK: testing " + target + " over "
                   + std::to_string(test_channels_.size()) + " channel(s)";
        }
        return "ERROR: CMD:TEST_CHANNEL <ADDR> [net]  |  CMD:TEST_CHANNEL_STOP";
    }
    if (cmd.rfind("CMD:GROUP_CALL ", 0) == 0) {
        const std::string roster = cmd_trim(cmd.substr(15));
        if (roster.empty())
            return "ERROR: CMD:GROUP_CALL requires a roster name";
        if (!initiate_group_call(roster))
            return "ERROR: roster '" + roster + "' not found or cannot call in current state";
        return "OK: group call to roster " + roster;
    }
    // -- Link control -------------------------------------------------------------
    if (cmd == "CMD:RESET_IDLE_TIMER") {
        reset_link_idle_timer();
        return "OK: idle timer reset";
    }
    if (cmd == "CMD:EMERGENCY_STOP") {
        emergency_stop();
        return "OK: emergency stop";
    }
    if (cmd.rfind("CMD:SET_PTT ", 0) == 0) {
        const std::string flag = cmd_trim(cmd.substr(12));
        if (flag != "on" && flag != "off")
            return "ERROR: CMD:SET_PTT on|off";
        set_manual_ptt(flag == "on");
        return std::string("OK: PTT ") + flag;
    }
    // -- Scan net -----------------------------------------------------------------
    if (cmd.rfind("CMD:SET_SCAN_NET ", 0) == 0) {
        const std::string name = cmd_trim(cmd.substr(17));
        set_active_scan_net(name);
        return "OK: scan net set to " + (name.empty() ? std::string("(all)") : name);
    }
    // -- Contacts -----------------------------------------------------------------
    if (cmd == "CMD:LIST_CONTACTS") {
        const auto contacts = get_all_contacts();
        if (contacts.empty()) return "CONTACTS: (none)";
        std::string out = "CONTACTS:\n";
        for (const auto& c : contacts) out += "  " + c + "\n";
        return out;
    }
    if (cmd.rfind("CMD:ADD_CONTACT ", 0) == 0) {
        const std::string args = cmd_trim(cmd.substr(16));
        if (args.empty())
            return "ERROR: CMD:ADD_CONTACT <callsign> [name]";
        const auto sp2  = args.find(' ');
        const std::string cs   = (sp2 == std::string::npos) ? args : args.substr(0, sp2);
        const std::string name = (sp2 == std::string::npos) ? "" : cmd_trim(args.substr(sp2 + 1));
        if (!add_contact(cs, name))
            return "ERROR: callsign must not be empty";
        return "OK: contact " + cs + " added";
    }
    if (cmd.rfind("CMD:DEL_CONTACT ", 0) == 0) {
        const std::string cs = cmd_trim(cmd.substr(16));
        if (cs.empty())
            return "ERROR: CMD:DEL_CONTACT requires a callsign";
        if (!remove_contact(cs))
            return "ERROR: contact '" + cs + "' not found";
        return "OK: contact " + cs + " removed";
    }
    if (cmd.rfind("CMD:SELECT_CONTACT ", 0) == 0) {
        const std::string cs = cmd_trim(cmd.substr(19));
        if (cs.empty())
            return "ERROR: CMD:SELECT_CONTACT requires a callsign";
        if (!select_contact(cs))
            return "ERROR: contact '" + cs + "' not found";
        return "OK: " + cs + " selected";
    }
    // -- Self addresses -----------------------------------------------------------
    if (cmd == "CMD:LIST_SELF_ADDRS") {
        const auto addrs = get_all_self_addresses();
        if (addrs.empty()) return "SELF_ADDRS: (none)";
        std::string out = "SELF_ADDRS:\n";
        for (const auto& a : addrs) out += "  " + a + "\n";
        return out;
    }
    if (cmd.rfind("CMD:ADD_SELF_ADDR ", 0) == 0) {
        const std::string addr = cmd_trim(cmd.substr(18));
        if (addr.empty())
            return "ERROR: CMD:ADD_SELF_ADDR requires an address";
        if (!add_self_address(addr))
            return "ERROR: address must not be empty";
        return "OK: self address " + addr + " added";
    }
    if (cmd.rfind("CMD:DEL_SELF_ADDR ", 0) == 0) {
        const std::string addr = cmd_trim(cmd.substr(18));
        if (addr.empty())
            return "ERROR: CMD:DEL_SELF_ADDR requires an address";
        if (!remove_self_address(addr))
            return "ERROR: self address '" + addr + "' not found";
        return "OK: self address " + addr + " removed";
    }
    if (cmd.rfind("CMD:SET_PRIMARY_ADDR ", 0) == 0) {
        const std::string addr = cmd_trim(cmd.substr(21));
        if (addr.empty())
            return "ERROR: CMD:SET_PRIMARY_ADDR requires an address";
        if (!set_primary_self_address(addr))
            return "ERROR: self address '" + addr + "' not found";
        return "OK: primary self address set to " + addr;
    }
    // -- Channel management -------------------------------------------------------
    if (cmd.rfind("CMD:RENAME_CHANNEL ", 0) == 0) {
        std::istringstream iss(cmd.substr(19));
        std::string old_id, new_id;
        iss >> old_id >> new_id;
        if (old_id.empty() || new_id.empty())
            return "ERROR: CMD:RENAME_CHANNEL <old_id> <new_id>";
        if (!rename_channel(old_id, new_id))
            return "ERROR: channel '" + old_id + "' not found";
        return "OK: channel " + old_id + " renamed to " + new_id;
    }
    // -- LQA ----------------------------------------------------------------------
    if (cmd == "CMD:CLEAR_LQA") {
        clear_lqa();
        return "OK: LQA database cleared";
    }
    if (cmd == "CMD:HELP") {
        return
            "Commands:\n"
            "  -- Call/link control --\n"
            "  CMD:CALL <ADDR>                          initiate individual call\n"
            "  CMD:SINGLE_CALL <ADDR>                   force single-channel call (no scanning)\n"
            "  CMD:TEST_CHANNEL <ADDR> [net]             actively test all channels to a peer (LQA sweep)\n"
            "  CMD:TEST_CHANNEL_STOP                    abort an in-progress test-channel sweep\n"
            "  CMD:GROUP_CALL <ROSTER>                  call all members of named roster\n"
            "  CMD:AMD <ADDR> <text>                    send AMD orderwire (to peer if LINKED)\n"
            "  CMD:TERMINATE                            terminate current link\n"
            "  CMD:ACCEPT                               accept incoming call (manual-accept mode)\n"
            "  CMD:REJECT                               reject incoming call (TWAS)\n"
            "  CMD:RESET_IDLE_TIMER                     reset the link idle watchdog\n"
            "  CMD:EMERGENCY_STOP                       abort TX and reset immediately\n"
            "  CMD:SET_PTT on|off                       manual PTT override\n"
            "  -- Scanning --\n"
            "  CMD:START_SCANNING                       start channel scanning (alias: CMD:SCAN)\n"
            "  CMD:STOP_SCANNING                        stop scanning, return to IDLE (alias: CMD:AVAILABLE)\n"
            "  CMD:SET_SCAN_NET <name>                  set active scan net (empty = all)\n"
            "  CMD:STATUS                               print current SM state\n"
            "  -- Sounding --\n"
            "  CMD:SOUND                                send a sounding on current channel\n"
            "  CMD:SOUND_SWEEP <net>                    sounding sweep over a net's channels\n"
            "  CMD:SOUND_AUTO on|off [net]              enable/disable automatic sounding\n"
            "  CMD:SOUND_INTERRUPT <net>                cancel an in-progress sounding sweep\n"
            "  -- Channels --\n"
            "  CMD:ADD_CHANNEL rx_hz[:tx_hz] [mode] [label]  add/update channel\n"
            "  CMD:DEL_CHANNEL rx_hz                    remove channel\n"
            "  CMD:LIST_CHANNELS                        list all channels\n"
            "  CMD:CLEAR_CHANNELS                       remove all channels\n"
            "  CMD:RENAME_CHANNEL <old_id> <new_id>     rename a channel by ID\n"
            "  CMD:SAVE_CHANNELS [path]                 save channel list to file\n"
            "  CMD:LOAD_CHANNELS <path>                 load channel list from file\n"
            "  -- Nets --\n"
            "  CMD:ADD_NET <name>                       add a net\n"
            "  CMD:DEL_NET <name>                       remove a net\n"
            "  CMD:RENAME_NET <old_name> <new_name>     rename a net\n"
            "  CMD:ASSIGN_CHANNEL <net> <id>            assign a channel ID to a net\n"
            "  CMD:UNASSIGN_CHANNEL <net> <id>          remove a channel ID from a net\n"
            "  CMD:LIST_NETS                            list all nets and their channels\n"
            "  -- Contacts --\n"
            "  CMD:LIST_CONTACTS                        list all contacts\n"
            "  CMD:ADD_CONTACT <callsign> [name]        add/update a contact\n"
            "  CMD:DEL_CONTACT <callsign>               remove a contact\n"
            "  CMD:SELECT_CONTACT <callsign>            select contact for next outgoing call\n"
            "  -- Self addresses --\n"
            "  CMD:LIST_SELF_ADDRS                      list all self addresses\n"
            "  CMD:ADD_SELF_ADDR <addr>                 add a self address\n"
            "  CMD:DEL_SELF_ADDR <addr>                 remove a self address\n"
            "  CMD:SET_PRIMARY_ADDR <addr>              set primary self address\n"
            "  -- LQA --\n"
            "  CMD:CLEAR_LQA                            clear LQA database\n"
            "  CMD:HELP                                 print this message";
    }
    return "ERROR: unknown command -- try CMD:HELP";
}

} // namespace ale
