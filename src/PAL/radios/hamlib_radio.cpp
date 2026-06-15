// hamlib_radio.cpp

#include "pal/radios/hamlib_radio.h"
#include <cstring>
#include <utility>

namespace pal {

HamlibRadio::HamlibRadio(const std::string& model, const std::string& port)
    : model_(model),
      port_(port) {}

HamlibRadio::~HamlibRadio() {
    shutdown();
}

bool HamlibRadio::initialize() {
    if (ready_) {
        return true;
    }

    rig_set_debug(RIG_DEBUG_NONE);

    rig_ = rig_init(std::stoi(model_));
    if (!rig_) {
        ready_ = false;
        return false;
    }

    if (!configure_port()) {
        shutdown();
        return false;
    }

    ready_ = true;
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
    if (!rig_) {
        return false;
    }

    return rig_open(rig_) == RIG_OK;
}

void HamlibRadio::stop() {
    if (rig_) {
        rig_close(rig_);
    }

    transmitting_ = false;
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
    if (!rig_) {
        transmitting_ = false;
        return;
    }

    const ptt_t ptt_mode = transmit ? RIG_PTT_ON : RIG_PTT_OFF;
    if (rig_set_ptt(rig_, RIG_VFO_CURR, ptt_mode) == RIG_OK) {
        transmitting_ = transmit;
    }
}

bool HamlibRadio::is_transmitting() const {
    return transmitting_;
}

bool HamlibRadio::is_ready() const {
    return ready_;
}

std::string HamlibRadio::get_port_config() const {
    return port_;
}

void HamlibRadio::register_send_callback(SendCommandCallback callback) {
    send_callback_ = std::move(callback);
}

void HamlibRadio::register_ack_callback(AckCallback callback) {
    ack_callback_ = std::move(callback);
}

void HamlibRadio::process_response(const uint8_t* /*data*/, size_t /*length*/) {
    // Hamlib-Backends sind hier direkt gekoppelt; rohe Transportantworten
    // werden von diesem Adapter nicht weiterverarbeitet.
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
    if (!rig_) {
        return false;
    }

    // Ziel als serielle Schnittstelle oder TCP-Ziel interpretieren.
    // Hamlib akzeptiert network:port-Ziele; rigctld arbeitet selbst über TCP.
    // Der konkrete Backend-Transport wird hier über den Porttyp festgelegt.
    if (port_.rfind("tcp://", 0) == 0 || port_.rfind("rigctld://", 0) == 0) {
        rig_->state.rigport.type.rig = RIG_PORT_NETWORK;
        std::string endpoint = port_;

        if (endpoint.rfind("tcp://", 0) == 0) {
            endpoint.erase(0, 6);
        } else if (endpoint.rfind("rigctld://", 0) == 0) {
            endpoint.erase(0, 10);
        }

        std::strncpy(rig_->state.rigport.pathname, endpoint.c_str(), HAMLIB_FILPATHLEN);
        rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
        return true;
    }

    rig_->state.rigport.type.rig = RIG_PORT_SERIAL;
    std::strncpy(rig_->state.rigport.pathname, port_.c_str(), HAMLIB_FILPATHLEN);
    rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
    rig_->state.rigport.parm.serial.rate = 19200;

    return true;
}

} // namespace pal