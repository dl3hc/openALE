// hamlib_radio.cpp

#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with explicit null termination below
#endif

#include "PAL/radios/hamlib_radio.h"
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

// Controlled via hamlib_set_debug_logging() — toggled by the GUI "Debug" log level.
// 0=Off, 1=Error, 2=Info, 3=Debug, 4=Trace — matches the GUI cfgLogLevel values.
// Info shows channel/freq transitions + assert_mode readback detail; Debug adds sync detail.
static int s_log_level = 2;
void pal::hamlib_set_log_level(int level) { s_log_level = level; }

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
        std::fprintf(stderr, "[HamlibRadio] rig_init(%s) failed\n", model_.c_str());
        return false;
    }

    if (!configure_port()) {
        shutdown();
        return false;
    }

    // ready_ bleibt false bis start() rig_open() erfolgreich aufgerufen hat.
    std::fprintf(stderr, "[HamlibRadio] rig_init(%s) → rig_open pending\n", model_.c_str());
    return true;
}

void HamlibRadio::shutdown() {
    std::fprintf(stderr, "[HamlibRadio] model=%s port=%s\n", model_.c_str(), port_.c_str());
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
        std::fprintf(stderr,
            "[HamlibRadio] rig_open failed (model=%s port=%s): %s\n",
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
    std::fprintf(stderr, "[HamlibRadio] model=%s port=%s opened\n", model_.c_str(), port_.c_str());
    return true;
}

void HamlibRadio::stop() {
    std::fprintf(stderr, "[HamlibRadio] model=%s port=%s closed\n", model_.c_str(), port_.c_str());
    if (rig_ && ready_) rig_close(rig_);  // only send "q" when rig_open() succeeded
    transmitting_ = false;
    ready_ = false;
}

bool HamlibRadio::set_channel(const Channel& channel) {
    if (!rig_) return false;

    const bool freq_changed = channel.tx_frequency != current_channel_.tx_frequency;
    const char* mname = rig_strrmode(to_hamlib_mode(channel.tx_mode));
    if (s_log_level >= 2)
        std::fprintf(stderr, "[HamlibRadio] set_channel: %u Hz  mode=%s  [freq: %s]\n",
                     channel.tx_frequency, mname ? mname : "?",
                     freq_changed ? "set" : "skipped");

    // Order: frequency FIRST, mode LAST. Some SDR front-ends (Quisk) restore a
    // per-band saved mode on a frequency change; sending mode last — then having
    // assert_mode() verify via live readback and re-send on mismatch — makes
    // openALE's channel mode authoritative. assert_mode()'s diagnostic prints
    // (level >= Info) provide the natural I/O latency (~5 ms on Windows stderr)
    // that lets the SDR's async band restore complete before the retry fires.
    // No explicit sleep; no scan-rate impact.
    // Mode is sent on EVERY hop (no mode_changed guard): the rig may have been
    // retuned externally between sync_from_radio() polls. VFO = RIG_VFO_CURR;
    // passband = RIG_PASSBAND_NORMAL.
    int freq_ret = RIG_OK;
    if (freq_changed) {
        freq_ret = rig_set_freq(rig_, RIG_VFO_CURR,
                                static_cast<freq_t>(channel.tx_frequency));
        if (freq_ret != RIG_OK)
            std::fprintf(stderr, "[HamlibRadio]   rig_set_freq(%u) → FAILED: %s\n",
                         channel.tx_frequency, rigerror(freq_ret));
        else if (s_log_level >= 2)
            std::fprintf(stderr, "[HamlibRadio]   rig_set_freq: %u Hz → OK\n",
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
        std::fprintf(stderr, "[HamlibRadio]   assert_mode(%s) → FAILED: %s\n",
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
        std::fprintf(stderr, "[HamlibRadio] rig_set_freq(%u) failed: %s\n",
                     hz, rigerror(ret));
    else if (s_log_level >= 2)
        std::fprintf(stderr, "[HamlibRadio] freq=%u Hz (simplex)\n", hz);

    current_channel_.rx_frequency = hz;
    current_channel_.tx_frequency = hz;  // simplex

    const char* saved_mname = rig_strrmode(to_hamlib_mode(saved_mode));
    if (s_log_level >= 3)
        std::fprintf(stderr, "[HamlibRadio]   set_frequency: re-asserting mode %s after freq change\n",
                     saved_mname ? saved_mname : "?");
    const int mode_ret = assert_mode(saved_mode);
    
    return ret == RIG_OK && mode_ret == RIG_OK;
}

bool HamlibRadio::set_mode(RadioMode mode) {
    if (!rig_) {
        std::fprintf(stderr, "[HamlibRadio] set_mode: rig_ is null — not connected\n");
        return false;
    }

    const char* mname = rig_strrmode(to_hamlib_mode(mode));
    if (s_log_level >= 3)
        std::fprintf(stderr, "[HamlibRadio] set_mode: requesting %s\n",
                     mname ? mname : "?");
    const int ret = assert_mode(mode);
    current_channel_.rx_mode = mode;
    current_channel_.tx_mode = mode;
    if (ret != RIG_OK)
        std::fprintf(stderr, "[HamlibRadio] set_mode: %s → FAILED: %s\n",
                     mname ? mname : "?", rigerror(ret));
    return ret == RIG_OK;
}

// Set the mode, then read it back LIVE and re-assert until the rig reports the
// intended mode (bounded, NO delay). This is the authoritative-mode mechanism:
// an SDR front-end (Quisk) may restore a per-band saved mode after a frequency
// change, silently reverting our mode. Because HAMLIB_CACHE_MODE is 0 (see
// start()), rig_get_mode() is a live query and actually observes that override.
// A band-memory restore fires once per frequency change, so re-sending the mode
// wins; the attempt cap only bounds a persistently-rejecting backend.
int HamlibRadio::assert_mode(RadioMode mode) {
    const rmode_t target = to_hamlib_mode(mode);
    const char* mname = rig_strrmode(target);

    last_mode_cmd_ = std::chrono::steady_clock::now();

    int mode_ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
    if (mode_ret != RIG_OK)
        std::fprintf(stderr, "[HamlibRadio]   assert_mode: rig_set_mode(%s) → FAILED: %s\n",
                     mname ? mname : "?", rigerror(mode_ret));
    else if (s_log_level >= 2)
        std::fprintf(stderr, "[HamlibRadio]   assert_mode: rig_set_mode(%s) → sent\n",
                     mname ? mname : "?");

    bool verified = false;
    for (int attempt = 0; attempt < MODE_ASSERT_MAX_ATTEMPTS; ++attempt) {
        rmode_t actual = RIG_MODE_NONE; pbwidth_t bw = 0;
        if (rig_get_mode(rig_, RIG_VFO_CURR, &actual, &bw) != RIG_OK) {
            std::fprintf(stderr, "[HamlibRadio]   assert_mode: rig_get_mode() failed — cannot verify\n");
            break;
        }
        if (actual == target) {
            verified = true;
            if (s_log_level >= 2)
                std::fprintf(stderr, "[HamlibRadio]   assert_mode: %s confirmed (readback #%d)\n",
                             mname ? mname : "?", attempt + 1);
            return mode_ret;
        }
        if (s_log_level >= 2)
            std::fprintf(stderr,
                "[HamlibRadio]   assert_mode: readback #%d: %s ≠ intended %s — re-asserting\n",
                attempt + 1,
                rig_strrmode(actual) ? rig_strrmode(actual) : "?", mname ? mname : "?");
        mode_ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
    }
    if (!verified)
        std::fprintf(stderr,
            "[HamlibRadio]   assert_mode: %s — readback loop exhausted/failed, mode unverified\n",
            mname ? mname : "?");
    return mode_ret;
}

void HamlibRadio::set_ptt(bool transmit) {
    if (!rig_) { transmitting_ = false; return; }

    const ptt_t ptt_mode = transmit ? RIG_PTT_ON : RIG_PTT_OFF;
    if (rig_set_ptt(rig_, RIG_VFO_CURR, ptt_mode) == RIG_OK) {
        transmitting_ = transmit;
        if (transmit)
            std::fprintf(stderr, "[HamlibRadio] PTT ON (transmitting)\n");
        else
            std::fprintf(stderr, "[HamlibRadio] PTT OFF (receiving)\n");
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
        if (s_log_level >= 2)
            std::fprintf(stderr, "[HamlibRadio] sync_from_radio: external retune %u→%u Hz\n",
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
        if (s_log_level >= 3)
            std::fprintf(stderr, "[HamlibRadio] sync_from_radio: mode mismatch — radio=%s  intended=%s\n",
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
            if (s_log_level >= 3)
                std::fprintf(stderr,
                    "[HamlibRadio] sync_from_radio: re-asserting %s → %s\n",
                    intend_mname ? intend_mname : "?",
                    ret == RIG_OK ? "OK" : rigerror(ret));
        } else if (s_log_level >= 3) {
            if (changed)
                std::fprintf(stderr,
                    "[HamlibRadio] sync_from_radio: freq also changed — not fighting external retune\n");
            else
                std::fprintf(stderr,
                    "[HamlibRadio] sync_from_radio: window expired — not re-asserting\n");
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
        std::fprintf(stderr, "[HamlibRadio] network: endpoint=%s\n", endpoint.c_str());
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

    std::fprintf(stderr,
        "[HamlibRadio] configure_port: port=%s baud=%d 8N1 handshake=NONE "
        "DTR=%s RTS=%s stab=%ums\n",
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
        std::fprintf(stderr, "[HamlibRadio] DTR/RTS: AUTO (no action)\n");
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

    std::fprintf(stderr,
        "[HamlibRadio] apply_line_policy: DTR=%s RTS=%s\n",
        policy_.dtr == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.dtr == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.rts == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.rts == SerialLinePolicy::State::OFF ? "OFF" : "AUTO");
}

static int collect_rig_cb(const struct rig_caps *caps, rig_ptr_t data) {
    if (!caps || !caps->macro_name) return 1;
    auto *vec = static_cast<std::vector<RigEntry>*>(data);
    std::string macro = caps->macro_name;
    constexpr std::string_view prefix = "RIG_MODEL_";
    if (macro.size() > prefix.size() && macro.compare(0, prefix.size(), prefix) == 0)
        macro.erase(0, prefix.size());
    vec->push_back({(int)caps->rig_model, caps->mfg_name ? caps->mfg_name : "", std::move(macro)});
    return 1;
}

std::vector<RigEntry> list_rigs() {
    std::vector<RigEntry> rigs;
    rig_list_foreach(collect_rig_cb, &rigs);
    std::sort(rigs.begin(), rigs.end(), [](const RigEntry& a, const RigEntry& b) {
        return a.mfg != b.mfg ? a.mfg < b.mfg : a.macro < b.macro;
    });
    return rigs;
}

} // namespace pal
