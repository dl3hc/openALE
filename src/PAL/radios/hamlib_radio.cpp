// hamlib_radio.cpp

#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with explicit null termination below
#endif

#include "PAL/radios/hamlib_radio.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pal {

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
    return true;
}

void HamlibRadio::shutdown() {
    if (rig_) {
        rig_close(rig_);
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

    // Serielle Schnittstelle: DTR/RTS-Leitungszustand nach Open setzen,
    // dann stabilization_ms warten bevor der erste CAT-Befehl gesendet wird.
    if (is_serial_port()) {
        apply_line_policy();
        if (policy_.stabilization_ms > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(policy_.stabilization_ms));
    }

    ready_ = true;
    return true;
}

void HamlibRadio::stop() {
    if (rig_) rig_close(rig_);
    transmitting_ = false;
    ready_ = false;
}

bool HamlibRadio::set_channel(const Channel& channel) {
    if (!rig_) return false;

    if (rig_set_freq(rig_, RIG_VFO_CURR,
                     static_cast<freq_t>(channel.tx_frequency)) != RIG_OK)
        return false;

    const rmode_t mode = to_hamlib_mode(channel.tx_mode);
    if (rig_set_mode(rig_, RIG_VFO_CURR, mode, RIG_PASSBAND_NORMAL) != RIG_OK)
        return false;

    current_channel_ = channel;
    return true;
}

Channel HamlibRadio::get_channel() const {
    return current_channel_;
}

void HamlibRadio::set_ptt(bool transmit) {
    if (!rig_) { transmitting_ = false; return; }

    const ptt_t ptt_mode = transmit ? RIG_PTT_ON : RIG_PTT_OFF;
    if (rig_set_ptt(rig_, RIG_VFO_CURR, ptt_mode) == RIG_OK)
        transmitting_ = transmit;
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

// ── Private helpers ───────────────────────────────────────────────────────────

bool HamlibRadio::is_serial_port() const {
    // TCP/network Specs beginnen mit "tcp://" oder "rigctld://"
    return port_.rfind("tcp://", 0) != 0 && port_.rfind("rigctld://", 0) != 0;
}

rmode_t HamlibRadio::to_hamlib_mode(RadioMode mode) const {
    switch (mode) {
        case RadioMode::LSB:      return RIG_MODE_LSB;
        case RadioMode::CW:       return RIG_MODE_CW;
        case RadioMode::CW_R:     return RIG_MODE_CWR;
        case RadioMode::FM:
        case RadioMode::FMW:      return RIG_MODE_FM;
        case RadioMode::AM:       return RIG_MODE_AM;
        case RadioMode::FSK:
        case RadioMode::RTTY:     return RIG_MODE_RTTY;
        case RadioMode::FSK_R:    return RIG_MODE_RTTYR;
        case RadioMode::DATA_USB: return RIG_MODE_PKTUSB;
        case RadioMode::DATA_LSB: return RIG_MODE_PKTLSB;
        default:                  return RIG_MODE_USB;
    }
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
        policy_.rts == SerialLinePolicy::State::AUTO)
        return;  // Nichts zu tun

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

} // namespace pal
