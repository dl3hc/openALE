// hamlib_radio.cpp

#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with explicit null termination below
#endif

#include "PAL/radios/hamlib_radio.h"
#include "PAL/logger.h"
#include <hamlib/rig.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pal {

static rmode_t        to_hamlib_mode(RadioMode mode);
static RadioMode      from_hamlib_mode(rmode_t m);

// assert_mode() re-sends the mode at most this many times when the live readback
// still disagrees. BandFromFreq-style band-memory restores fire once per frequency
// change, so one or two passes converge; the cap only guards a persistently
// rejecting backend from spinning forever.
static constexpr int MODE_ASSERT_MAX_ATTEMPTS = 3;

// Maps the GUI log-level integer (0=Off 1=Error 2=Info 3=Debug 4=Trace) to the
// PAL logger level and forwards it.  Info shows channel/freq transitions +
// assert_mode readback detail; Debug adds sync detail.
void pal::hamlib_set_log_level(int level) {
    auto* logger = pal::get_logger();
    if (!logger) return;
    static const pal::LogLevel kMap[] = {
        static_cast<pal::LogLevel>(5),  // 0 = Off   → FATAL
        static_cast<pal::LogLevel>(4),  // 1 = Error  → ERROR
        static_cast<pal::LogLevel>(2),  // 2 = Info   → INFO
        static_cast<pal::LogLevel>(1),  // 3 = Debug  → DEBUG
        static_cast<pal::LogLevel>(0),  // 4 = Trace  → TRACE
    };
    const int idx = (level >= 0 && level <= 4) ? level : 2;
    logger->set_level(kMap[idx]);
}

// sync_from_radio() re-asserts the intended mode only within this window after
// the last intentional mode command. An SDR front-end (Quisk) applies its
// band-memory mode restore asynchronously — it can land AFTER assert_mode()'s
// bounded readback loop has already returned, silently reverting a one-shot
// channel activation (manual step / net select). The 2 s VFO_GET sync poll is
// the only later observer, so it must correct the revert. The window keeps
// that correction scoped to the aftermath of our own command: a mode the
// operator changes on the rig/SDR minutes later is NOT fought.
static constexpr auto MODE_REASSERT_WINDOW = std::chrono::seconds(5);

HamlibRadio::HamlibRadio(const std::string& model,
                         const std::string& port,
                         int baud,
                         SerialLinePolicy policy)
    : model_(model),
      port_(port),
      baud_(baud),
      policy_(policy) {}

HamlibRadio::~HamlibRadio() {
    shutdown();
}

bool HamlibRadio::initialize() {
    if (ready_) return true;

    rig_set_debug(RIG_DEBUG_ERR);

    // Stellt sicher, dass alle statisch eingebundenen Backends registriert sind.
    // Bei monolithischem libhamlib-4.dll meist redundant, schadet aber nicht.
    rig_load_all_backends();

    rig_ = rig_init(std::stoi(model_));
    if (!rig_) {
        pal::log_error("HamlibRadio", "rig_init(%s) failed", model_.c_str());
        return false;
    }

    if (!configure_port()) {
        shutdown();
        return false;
    }

    // ready_ bleibt false bis start() rig_open() erfolgreich aufgerufen hat.
    pal::log_info("HamlibRadio", "rig_init(%s) -> rig_open pending", model_.c_str());
    return true;
}

void HamlibRadio::shutdown() {
    pal::log_info("HamlibRadio", "shutdown: model=%s port=%s", model_.c_str(), port_.c_str());
    if (rig_) {
        if (ready_) rig_close(rig_);  // only send "q" when rig_open() succeeded
        rig_cleanup(rig_);
        rig_ = nullptr;
    }
    transmitting_ = false;
    ready_ = false;
}

bool HamlibRadio::start() {
    if (!rig_) return false;

    const int ret = rig_open(rig_);
    if (ret != RIG_OK) {
        pal::log_error("HamlibRadio", "rig_open failed (model=%s port=%s): %s",
                       model_.c_str(), port_.c_str(), rigerror(ret));
        return false;
    }

    // Over TCP/netrigctl: shorten the hamlib cache TTL so that rig_get_freq/
    // rig_get_mode queries made ≥500 ms after the last set go live to the
    // rigctld server instead of returning hamlib's internally cached intended
    // value.  The 2-second VFO_GET poll interval exceeds 500 ms, guaranteeing
    // a live read on every GUI sync cycle.  Serial backends keep their default
    // TTL (no override needed — cache churn on serial CAT is undesirable).
    if (!is_serial_port()) {
        rig_set_cache_timeout_ms(rig_, HAMLIB_CACHE_ALL, 500);
        // Mode reads must be LIVE (no cache). assert_mode() sets the mode, then
        // reads it straight back to verify the rig actually applied it — an SDR
        // front-end (Quisk) may override the mode on a band change. A cached read
        // would just echo the value we set and defeat the verification.
        rig_set_cache_timeout_ms(rig_, HAMLIB_CACHE_MODE, 0);

        // hamlib's rig_set_mode() begins with an UNINITIALIZED `int locked_mode`,
        // fills it via rig_get_lock_mode() (return code ignored) and silently
        // returns RIG_OK WITHOUT transmitting when it is nonzero. Over netrigctl
        // the \get_lock_mode transaction fails against servers that don't
        // implement it (Quisk: "RPRT -4"; sscanf on that buffer writes nothing),
        // so whether ANY mode command reaches the radio depends on stack garbage
        // (hamlib 4.5 rig.c:2218; still present in upstream master rig.c:2812).
        // Nulling the backend hook makes rig_get_lock_mode() fall back to
        // rig->state.lock_mode — a real, zero-initialized field — so the elision
        // path is deterministically dead and every rig_set_mode() transmits.
        // Bonus: removes one wire round-trip per mode set (scan path gets faster).
        rig_->caps->get_lock_mode = nullptr;
        rig_->state.lock_mode = 0;
    }

    // Serielle Schnittstelle: DTR/RTS-Leitungszustand nach Open setzen,
    // dann stabilization_ms warten bevor der erste CAT-Befehl gesendet wird.
    if (is_serial_port()) {
        apply_line_policy();
        if (policy_.stabilization_ms > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(policy_.stabilization_ms));
    }

    ready_ = true;
    pal::log_info("HamlibRadio", "model=%s port=%s opened", model_.c_str(), port_.c_str());
    return true;
}

void HamlibRadio::stop() {
    pal::log_info("HamlibRadio", "model=%s port=%s closed", model_.c_str(), port_.c_str());
    if (rig_ && ready_) rig_close(rig_);  // only send "q" when rig_open() succeeded
    transmitting_ = false;
    ready_ = false;
}

bool HamlibRadio::set_channel(const Channel& channel) {
    if (!rig_) return false;

    const bool freq_changed = channel.tx_frequency != current_channel_.tx_frequency;
    const char* mname = rig_strrmode(to_hamlib_mode(channel.tx_mode));
    pal::log_info("HamlibRadio", "set_channel: %u Hz  mode=%s  [freq: %s]",
                  channel.tx_frequency, mname ? mname : "?",
                  freq_changed ? "set" : "skipped");

    // Order: frequency FIRST, mode LAST. Some SDR front-ends (Quisk) restore a
    // per-band saved mode on a frequency change; sending mode last — then having
    // assert_mode() verify via live readback and re-send on mismatch — makes
    // openALE's channel mode authoritative. If the SDR's band restore lands
    // asynchronously AFTER assert_mode() returned, the deferred sync backstop
    // (ALEController::tick_mode_verify -> sync_from_radio) corrects it — no
    // sleeps here, no scan-rate impact. Both mechanisms only work because
    // start() neutralized hamlib's get_lock_mode probe: otherwise re-sent mode
    // commands may be silently elided inside rig_set_mode() (see start()).
    // Mode is sent on EVERY hop (no mode_changed guard): the rig may have been
    // retuned externally between sync_from_radio() polls. VFO = RIG_VFO_CURR;
    // passband = RIG_PASSBAND_NORMAL.
    int freq_ret = RIG_OK;
    if (freq_changed) {
        freq_ret = rig_set_freq(rig_, RIG_VFO_CURR,
                                static_cast<freq_t>(channel.tx_frequency));
        if (freq_ret != RIG_OK)
            pal::log_error("HamlibRadio", "  rig_set_freq(%u) -> FAILED: %s",
                           channel.tx_frequency, rigerror(freq_ret));
        else
            pal::log_info("HamlibRadio", "  rig_set_freq: %u Hz -> OK",
                          channel.tx_frequency);
    }

    const int mode_ret = assert_mode(channel.tx_mode);

    // Always track the intended state regardless of return codes. Over TCP,
    // hamlib can report failure even when rigctld applied the command (e.g. a
    // slow rig makes the RPRT read time out after rigctld already set it).
    // Leaving current_channel_ stale causes callers that read get_channel()
    // (set_frequency, step_channel) to send the wrong mode on their next call,
    // overriding what the channel hop correctly set on the radio.
    current_channel_ = channel;
    if (mode_ret != RIG_OK)
        pal::log_error("HamlibRadio", "  assert_mode(%s) -> FAILED: %s",
                       mname ? mname : "?", rigerror(mode_ret));
    return freq_ret == RIG_OK && mode_ret == RIG_OK;
}

Channel HamlibRadio::get_channel() const {
    return current_channel_;
}

bool HamlibRadio::set_frequency(uint32_t hz) {
    if (!rig_ || hz == 0) return false;

    // Store intended mode to re-assert after frequency change.
    // An SDR front-end (Quisk) restores a per-band saved mode on freq change;
    // openALE's mode must be authoritative — always assert it.
    const RadioMode saved_mode = current_channel_.tx_mode;

    // One CAT command, no read-modify-write: a manual frequency change must not
    // re-send the mode (and must not depend on current_channel_ being accurate
    // for the mode). See set_channel() for the netrigctl-flush rationale.
    const int ret = rig_set_freq(rig_, RIG_VFO_CURR, static_cast<freq_t>(hz));
    if (ret != RIG_OK)
        pal::log_error("HamlibRadio", "rig_set_freq(%u) failed: %s", hz, rigerror(ret));
    else
        pal::log_info("HamlibRadio", "freq=%u Hz (simplex)", hz);

    current_channel_.rx_frequency = hz;
    current_channel_.tx_frequency = hz;  // simplex

    const char* saved_mname = rig_strrmode(to_hamlib_mode(saved_mode));
    pal::log_debug("HamlibRadio", "  set_frequency: re-asserting mode %s after freq change",
                   saved_mname ? saved_mname : "?");
    const int mode_ret = assert_mode(saved_mode);
    
    return ret == RIG_OK && mode_ret == RIG_OK;
}

bool HamlibRadio::set_mode(RadioMode mode) {
    if (!rig_) {
        pal::log_error("HamlibRadio", "set_mode: rig_ is null — not connected");
        return false;
    }

    const char* mname = rig_strrmode(to_hamlib_mode(mode));
    pal::log_debug("HamlibRadio", "set_mode: requesting %s", mname ? mname : "?");
    const int ret = assert_mode(mode);
    current_channel_.rx_mode = mode;
    current_channel_.tx_mode = mode;
    if (ret != RIG_OK)
        pal::log_error("HamlibRadio", "set_mode: %s -> FAILED: %s",
                       mname ? mname : "?", rigerror(ret));
    return ret == RIG_OK;
}

// Set the mode, then read it back LIVE and re-assert until the rig reports the
// intended mode (bounded, NO delay). This is the authoritative-mode mechanism:
// an SDR front-end (Quisk) may restore a per-band saved mode after a frequency
// change, silently reverting our mode. Because HAMLIB_CACHE_MODE is 0 (see
// start()), rig_get_mode() is a live query and actually observes that override.
// The re-sends only reach the wire because start() nulled the backend's
// get_lock_mode hook — hamlib's rig_set_mode() otherwise consults an
// uninitialized lock flag and may elide the command while returning RIG_OK.
// A band-memory restore fires once per frequency change, so re-sending the mode
// wins; the attempt cap only bounds a persistently-rejecting backend. If the
// restore lands after this loop already returned, the loop legitimately reports
// success on the pre-revert mode — the deferred tick_mode_verify checks
// (+300/700/1500 ms -> sync_from_radio) catch and correct that case.
int HamlibRadio::assert_mode(RadioMode mode) {
    const rmode_t target = to_hamlib_mode(mode);
    const char* mname = rig_strrmode(target);

    last_mode_cmd_ = std::chrono::steady_clock::now();

    int mode_ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
    if (mode_ret != RIG_OK)
        pal::log_error("HamlibRadio", "  assert_mode: rig_set_mode(%s) -> FAILED: %s",
                       mname ? mname : "?", rigerror(mode_ret));
    else
        pal::log_debug("HamlibRadio", "  assert_mode: rig_set_mode(%s) -> sent",
                      mname ? mname : "?");

    bool verified = false;
    for (int attempt = 0; attempt < MODE_ASSERT_MAX_ATTEMPTS; ++attempt) {
        rmode_t actual = RIG_MODE_NONE; pbwidth_t bw = 0;
        if (rig_get_mode(rig_, RIG_VFO_CURR, &actual, &bw) != RIG_OK) {
            pal::log_error("HamlibRadio", "  assert_mode: rig_get_mode() failed — cannot verify");
            break;
        }
        if (actual == target) {
            verified = true;
            pal::log_debug("HamlibRadio", "  assert_mode: %s confirmed (readback #%d)",
                          mname ? mname : "?", attempt + 1);
            return mode_ret;
        }
        pal::log_info("HamlibRadio", "  assert_mode: readback #%d: %s != intended %s — re-asserting",
                      attempt + 1,
                      rig_strrmode(actual) ? rig_strrmode(actual) : "?", mname ? mname : "?");
        mode_ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
    }
    if (!verified)
        pal::log_warn("HamlibRadio", "  assert_mode: %s — readback loop exhausted/failed, mode unverified",
                      mname ? mname : "?");
    return mode_ret;
}

void HamlibRadio::set_ptt(bool transmit) {
    if (!rig_) { transmitting_ = false; return; }

    const ptt_t ptt_mode = transmit ? RIG_PTT_ON : RIG_PTT_OFF;
    if (rig_set_ptt(rig_, RIG_VFO_CURR, ptt_mode) == RIG_OK) {
        transmitting_ = transmit;
        pal::log_info("HamlibRadio", transmit ? "PTT ON (transmitting)" : "PTT OFF (receiving)");
    }
}

bool HamlibRadio::is_transmitting() const { return transmitting_; }
bool HamlibRadio::is_ready()        const { return ready_; }

std::string HamlibRadio::get_port_config() const { return port_; }

void HamlibRadio::register_send_callback(SendCommandCallback callback) {
    send_callback_ = std::move(callback);
}

void HamlibRadio::register_ack_callback(AckCallback callback) {
    ack_callback_ = std::move(callback);
}

void HamlibRadio::process_response(const uint8_t*, size_t) {}

// ── File-static helpers (hamlib types stay out of the header) ─────────────────

static rmode_t to_hamlib_mode(pal::RadioMode mode) {
    switch (mode) {
        case pal::RadioMode::USB:      return RIG_MODE_USB;
        case pal::RadioMode::LSB:      return RIG_MODE_LSB;
        case pal::RadioMode::CW:       return RIG_MODE_CW;
        case pal::RadioMode::CW_R:     return RIG_MODE_CWR;
        case pal::RadioMode::FM:
        case pal::RadioMode::FMW:      return RIG_MODE_FM;
        case pal::RadioMode::AM:       return RIG_MODE_AM;
        case pal::RadioMode::FSK:
        case pal::RadioMode::RTTY:     return RIG_MODE_RTTY;
        case pal::RadioMode::FSK_R:    return RIG_MODE_RTTYR;
        case pal::RadioMode::DATA_USB: return RIG_MODE_PKTUSB;
        case pal::RadioMode::DATA_LSB: return RIG_MODE_PKTLSB;
        default:                       return RIG_MODE_USB;
    }
}

static pal::RadioMode from_hamlib_mode(rmode_t m) {
    switch (m) {
        case RIG_MODE_USB:    return pal::RadioMode::USB;
        case RIG_MODE_LSB:    return pal::RadioMode::LSB;
        case RIG_MODE_CW:     return pal::RadioMode::CW;
        case RIG_MODE_CWR:    return pal::RadioMode::CW_R;
        case RIG_MODE_FM:     return pal::RadioMode::FM;
        case RIG_MODE_WFM:    return pal::RadioMode::FMW;
        case RIG_MODE_AM:     return pal::RadioMode::AM;
        case RIG_MODE_RTTY:   return pal::RadioMode::RTTY;
        case RIG_MODE_RTTYR:  return pal::RadioMode::FSK_R;
        case RIG_MODE_PKTUSB: return pal::RadioMode::DATA_USB;
        case RIG_MODE_PKTLSB: return pal::RadioMode::DATA_LSB;
        default:              return pal::RadioMode::USB;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

bool HamlibRadio::is_serial_port() const {
    // TCP/network Specs beginnen mit "tcp://" oder "rigctld://"
    return port_.rfind("tcp://", 0) != 0 && port_.rfind("rigctld://", 0) != 0;
}

bool HamlibRadio::sync_from_radio() {
    if (!rig_ || !ready_) return false;

    freq_t freq = 0;
    if (rig_get_freq(rig_, RIG_VFO_CURR, &freq) != RIG_OK) return false;

    rmode_t mode = RIG_MODE_NONE;
    pbwidth_t bw  = 0;
    if (rig_get_mode(rig_, RIG_VFO_CURR, &mode, &bw) != RIG_OK) return false;

    const auto new_freq = static_cast<uint32_t>(freq);
    bool changed = false;
    if (new_freq != current_channel_.tx_frequency) {
        pal::log_info("HamlibRadio", "sync_from_radio: external retune %u->%u Hz",
                      current_channel_.tx_frequency, new_freq);
        current_channel_.rx_frequency = current_channel_.tx_frequency = new_freq;
        changed = true;
    }
    // NOTE: Do NOT update current_channel_.tx_mode/rx_mode with the radio's actual mode.
    // The current_channel_ represents openALE's intended state, not what the radio reports.
    const RadioMode new_mode = from_hamlib_mode(mode);
    if (new_mode != current_channel_.tx_mode) {
        const char* actual_mname = rig_strrmode(mode);
        const char* intend_mname = rig_strrmode(to_hamlib_mode(current_channel_.tx_mode));
        pal::log_debug("HamlibRadio", "sync_from_radio: mode mismatch — radio=%s  intended=%s",
                       actual_mname ? actual_mname : "?", intend_mname ? intend_mname : "?");
        // Backstop correction: Quisk's per-band saved-mode restore fires
        // ASYNCHRONOUSLY after a frequency change and can land after both
        // assert_mode()'s readback loop AND the immediate set_mode() re-assertion
        // in step_channel()/set_vfo_channel() have already returned. Re-send the
        // intended mode here, but only when:
        //  (a) the radio is still on the intended frequency (`!changed` — an
        //      external retune means the operator took over; don't fight), and
        //  (b) we commanded a mode recently (window) — a mode the operator
        //      deliberately changes on the rig later stays untouched.
        // Single fire-and-forget send, no readback loop: the next sync tick
        // (2 s) re-checks, and the window bounds any pathological ping-pong.
        // last_mode_cmd_ is deliberately NOT re-stamped here, so the window
        // cannot be extended indefinitely by our own corrections.
        if (!changed &&
            std::chrono::steady_clock::now() - last_mode_cmd_ < MODE_REASSERT_WINDOW) {
            const rmode_t target = to_hamlib_mode(current_channel_.tx_mode);
            const int ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
            pal::log_debug("HamlibRadio", "sync_from_radio: re-asserting %s -> %s",
                           intend_mname ? intend_mname : "?",
                           ret == RIG_OK ? "OK" : rigerror(ret));
        } else {
            pal::log_debug("HamlibRadio", changed
                ? "sync_from_radio: freq also changed — not fighting external retune"
                : "sync_from_radio: window expired — not re-asserting");
        }
    }
    return changed;
}

bool HamlibRadio::configure_port() {
    if (!rig_) return false;

// ── Netzwerk-Pfad (rigctld via TCP) ──────────────────────────────────
    // NET_RIGCTL's netrigctl_open() / network_open() erwartet "host:port" —
    // das tcp:// bzw. rigctld:// Präfix muss vor der Übergabe entfernt werden.
    if (port_.rfind("tcp://", 0) == 0 || port_.rfind("rigctld://", 0) == 0) {
        rig_->state.rigport.type.rig = RIG_PORT_NETWORK;
        std::string endpoint = port_;
        if (endpoint.rfind("tcp://", 0) == 0)         endpoint.erase(0, 6);
        else if (endpoint.rfind("rigctld://", 0) == 0) endpoint.erase(0, 10);
        std::strncpy(rig_->state.rigport.pathname, endpoint.c_str(), HAMLIB_FILPATHLEN);
        rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
        pal::log_info("HamlibRadio", "network: endpoint=%s", endpoint.c_str());
        return true;
    }

    // ── Kein Port (Dummy / USB / Audio-Backends) ─────────────────────────
    // Empty port: leave the backend's declared port type (the rig_caps default)
    // intact. Forcing RIG_PORT_SERIAL with no device would make rig_open() fail
    // for backends that don't use a port at all (Dummy, USB, Audio). The unified
    // GUI sends no device for "other" port-type models, so this is the path they
    // take; real serial rigs always carry a non-empty device string.
    if (port_.empty()) {
        pal::log_info("HamlibRadio", "configure_port: empty port — using backend default port type");
        return true;
    }

    // ── Serieller Pfad ────────────────────────────────────────────────────
    rig_->state.rigport.type.rig = RIG_PORT_SERIAL;

    std::strncpy(rig_->state.rigport.pathname, port_.c_str(), HAMLIB_FILPATHLEN);
    rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';

    // Baud-Rate: 0 → Backend-Default (nicht überschreiben)
    if (baud_ > 0)
        rig_->state.rigport.parm.serial.rate = baud_;

    // Datenformat: 8N1, kein Flow-Control-Handshake.
    rig_->state.rigport.parm.serial.data_bits = 8;
    rig_->state.rigport.parm.serial.stop_bits = 1;
    rig_->state.rigport.parm.serial.parity    = RIG_PARITY_NONE;
    rig_->state.rigport.parm.serial.handshake = RIG_HANDSHAKE_NONE;

    // DTR/RTS VOR rig_open() als conf-Token setzen (KRITISCH für TS-480 und
    // ähnliche USB-CAT-Adapter):  hamlib liest diese Werte in rig_open() beim
    // DCB-Setup und öffnet den Port mit den richtigen Leitungszuständen.
    // apply_line_policy() setzt sie zusätzlich noch einmal NACH rig_open()
    // als Absicherung (Windows-HANDLE-Fallback).
    if (policy_.dtr != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.dtr == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "dtr_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }
    if (policy_.rts != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.rts == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "rts_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }

    pal::log_info("HamlibRadio",
        "configure_port: port=%s baud=%d 8N1 handshake=NONE DTR=%s RTS=%s stab=%ums",
        port_.c_str(), baud_,
        policy_.dtr == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.dtr == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.rts == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.rts == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.stabilization_ms);

    return true;
}

void HamlibRadio::apply_line_policy() {
    // ── Versuch 1: Hamlib-Token-API (rig_set_conf nach rig_open) ─────────
    // Funktioniert für Backends die "dtr_state"/"rts_state" implementieren.
    // Muss nach rig_open() aufgerufen werden, da der Port sonst noch zu ist.
    if (policy_.dtr != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.dtr == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "dtr_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }
    if (policy_.rts != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.rts == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "rts_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }

#ifdef _WIN32
    // ── Versuch 2 (Windows-Fallback): direkt über hamlibs internen HANDLE ─
    // hamlib speichert den HANDLE in rigport.fd als (int)(intptr_t)HANDLE.
    // Wir lesen ihn mit demselben Cast zurück — kein zweites CreateFile nötig,
    // da hamlib den Port bereits exklusiv geöffnet hat.
    if (policy_.dtr == SerialLinePolicy::State::AUTO &&
        policy_.rts == SerialLinePolicy::State::AUTO) {
        pal::log_debug("HamlibRadio", "DTR/RTS: AUTO (no action)");
        return;  // Nichts zu tun
    }

    const HANDLE h = (HANDLE)(intptr_t)rig_->state.rigport.fd;
    if (h && h != INVALID_HANDLE_VALUE) {
        if (policy_.dtr != SerialLinePolicy::State::AUTO)
            EscapeCommFunction(h,
                policy_.dtr == SerialLinePolicy::State::ON ? SETDTR : CLRDTR);
        if (policy_.rts != SerialLinePolicy::State::AUTO)
            EscapeCommFunction(h,
                policy_.rts == SerialLinePolicy::State::ON ? SETRTS : CLRRTS);
    }
#endif

    pal::log_info("HamlibRadio", "apply_line_policy: DTR=%s RTS=%s",
        policy_.dtr == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.dtr == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.rts == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.rts == SerialLinePolicy::State::OFF ? "OFF" : "AUTO");
}

// Coarse connection category from a rig's declared port type. This is the
// single source of truth for whether a model connects over the network or a
// serial device — both the GUI (field visibility) and the bridge (spec grammar)
// derive from it. Everything that is neither network nor serial (Dummy, USB,
// Audio, None, …) collapses to "other" (no connection fields).
static std::string port_type_str(rig_port_t t) {
    switch (t) {
        case RIG_PORT_NETWORK: return "network";
        case RIG_PORT_SERIAL:  return "serial";
        default:               return "other";
    }
}

// rig_list_foreach only iterates backends that have been registered, and
// rig_load_all_backends() is otherwise only called from HamlibRadio::initialize()
// (i.e. when a rig is actually opened). RIG_LIST / rig_port_type fire from the GUI
// before any rig is connected, so we must register the backends ourselves — once
// per process — or the dropdown stays empty on a fresh bridge session.
static void ensure_backends_loaded() {
    static bool backends_loaded = false;
    if (!backends_loaded) {
        rig_set_debug(RIG_DEBUG_ERR);
        rig_load_all_backends();
        backends_loaded = true;
    }
}

static int collect_rig_cb(const struct rig_caps *caps, rig_ptr_t data) {
    if (!caps) return 1;
    // macro_name is null in some Hamlib builds; fall back to model_name then rig_model number.
    const char* raw = caps->macro_name ? caps->macro_name
                    : caps->model_name ? caps->model_name
                    : nullptr;
    if (!raw) return 1;
    auto *vec = static_cast<std::vector<RigEntry>*>(data);
    std::string macro = raw;
    constexpr std::string_view prefix = "RIG_MODEL_";
    if (macro.size() > prefix.size() && macro.compare(0, prefix.size(), prefix) == 0)
        macro.erase(0, prefix.size());
    vec->push_back({(int)caps->rig_model,
                    caps->mfg_name ? caps->mfg_name : "",
                    std::move(macro),
                    port_type_str(caps->port_type)});
    return 1;
}

std::vector<RigEntry> list_rigs() {
    ensure_backends_loaded();
    std::vector<RigEntry> rigs;
    rig_list_foreach(collect_rig_cb, &rigs);
    std::sort(rigs.begin(), rigs.end(), [](const RigEntry& a, const RigEntry& b) {
        return a.mfg != b.mfg ? a.mfg < b.mfg : a.macro < b.macro;
    });
    pal::log_info("HamlibRadio", "RIG_LIST: %zu rigs found", rigs.size());
    return rigs;
}

// Finder callback for rig_port_type(): stops at the first rig whose model
// number matches the target, capturing its port type.
struct PortTypeFinder {
    int         target;
    std::string result;
};
static int find_port_type_cb(const struct rig_caps *caps, rig_ptr_t data) {
    if (!caps) return 1;
    if ((int)caps->rig_model == static_cast<PortTypeFinder*>(data)->target) {
        static_cast<PortTypeFinder*>(data)->result = port_type_str(caps->port_type);
        return 0;  // stop iterating
    }
    return 1;
}

std::string rig_port_type(int model) {
    ensure_backends_loaded();
    PortTypeFinder f{model, "other"};
    rig_list_foreach(find_port_type_cb, &f);
    return f.result;
}

} // namespace pal
