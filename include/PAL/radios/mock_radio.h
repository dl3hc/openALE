// mock_radio.h — software-only IRadio for testing PTT + channel-hop wiring
#pragma once

#include "PAL/radio.h"
#include <cstdio>
#include <string>

namespace pal {

/**
 * MockRadio — logs every PTT and channel-change call to stdout.
 *
 * No hardware, no serial port, no Hamlib required.
 * Use with: ale_cli --radio mock
 *
 * Also note: Hamlib's own dummy rig (model 1) can be used as an alternative
 * when Hamlib is installed: --radio hamlib:1:none
 */
class MockRadio : public IRadio {
public:
    bool initialize() override { return true; }
    void shutdown()   override { transmitting_ = false; ready_ = false; }
    bool start()      override { ready_ = true;  log("READY"); return true; }
    void stop()       override { ready_ = false; log("STOPPED"); }

    bool set_channel(const Channel& ch) override {
        current_ = ch;
        const uint32_t tx_hz = ch.tx_frequency ? ch.tx_frequency : ch.rx_frequency;
        const uint32_t rx_hz = ch.rx_frequency;
        const char* mode = mode_name(ch.tx_mode);

        if (tx_hz == rx_hz) {
            std::printf("[RADIO] CH %u kHz  %s  pwr=%d%%  ant=%d\n",
                        rx_hz / 1000, mode, ch.power, ch.antenna);
        } else {
            std::printf("[RADIO] CH TX=%u kHz / RX=%u kHz  %s  pwr=%d%%  ant=%d\n",
                        tx_hz / 1000, rx_hz / 1000, mode, ch.power, ch.antenna);
        }
        std::fflush(stdout);
        return true;
    }

    Channel     get_channel()      const override { return current_; }
    bool        is_transmitting()  const override { return transmitting_; }
    bool        is_ready()         const override { return ready_; }
    std::string get_port_config()  const override { return "mock"; }

    void set_ptt(bool tx) override {
        transmitting_ = tx;
        std::printf("[RADIO] PTT %s\n", tx ? "ON  ← TX" : "OFF → RX");
        std::fflush(stdout);
    }

    void register_send_callback(SendCommandCallback) override {}
    void register_ack_callback(AckCallback)          override {}
    void process_response(const uint8_t*, size_t)    override {}

private:
    Channel current_;
    bool    transmitting_ = false;
    bool    ready_        = false;

    static void log(const char* msg) {
        std::printf("[RADIO] %s\n", msg);
        std::fflush(stdout);
    }

    static const char* mode_name(RadioMode m) {
        switch (m) {
            case RadioMode::USB:      return "USB";
            case RadioMode::LSB:      return "LSB";
            case RadioMode::CW:       return "CW";
            case RadioMode::CW_R:     return "CW-R";
            case RadioMode::FM:       return "FM";
            case RadioMode::FMW:      return "FM-W";
            case RadioMode::AM:       return "AM";
            case RadioMode::FSK:      return "FSK";
            case RadioMode::RTTY:     return "RTTY";
            case RadioMode::FSK_R:    return "FSK-R";
            case RadioMode::DATA_USB: return "DATA-USB";
            case RadioMode::DATA_LSB: return "DATA-LSB";
            case RadioMode::DIG:      return "DIG";
            case RadioMode::TUNE:     return "TUNE";
            default:                  return "???";
        }
    }
};

} // namespace pal
