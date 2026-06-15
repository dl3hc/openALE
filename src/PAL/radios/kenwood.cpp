/**
 * @file kenwood.cpp
 * @brief Kenwood CAT protocol implementation
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#include "pal/radios/kenwood.h"
#include <cstdio>

namespace pal {

Kenwood::Kenwood(ISerial* serial)
    : serial_(serial)
{
}

bool Kenwood::initialize() {
    rx_buffer_.clear();
    rx_buffer_.reserve(64);
    ready_ = true;
    return true;
}

void Kenwood::shutdown() {
    ready_ = false;
}

bool Kenwood::start() {
    return ready_;
}

void Kenwood::stop() {
    // Nothing to do
}

bool Kenwood::set_channel(const Channel& channel) {
    if (!ready_ || !serial_) return false;
    
    // Set frequency on VFO A
    send_command(build_freq_command('A', channel.rx_frequency));
    
    // Set mode
    send_command(build_mode_command(channel.rx_mode));
    
    current_channel_ = channel;
    return true;
}

Channel Kenwood::get_channel() const {
    return current_channel_;
}

void Kenwood::set_ptt(bool transmit) {
    if (!ready_ || !serial_) return;
    
    send_command(transmit ? "TX;" : "RX;");
    transmitting_ = transmit;
}

bool Kenwood::is_transmitting() const {
    return transmitting_;
}

bool Kenwood::is_ready() const {
    return ready_;
}

std::string Kenwood::get_port_config() const {
    // Kenwood default
    return "9600,n,8,1";
}

void Kenwood::register_send_callback(SendCommandCallback callback) {
    send_callback_ = callback;
}

void Kenwood::register_ack_callback(AckCallback callback) {
    ack_callback_ = callback;
}

void Kenwood::process_response(const uint8_t* data, size_t length) {
    // Accumulate until semicolon
    for (size_t i = 0; i < length; i++) {
        char c = static_cast<char>(data[i]);
        rx_buffer_ += c;
        
        if (c == ';') {
            // Complete response received
            // Parse if needed, for now just acknowledge
            if (ack_callback_) {
                ack_callback_();
            }
            rx_buffer_.clear();
        }
        
        // Prevent overflow
        if (rx_buffer_.size() > 256) {
            rx_buffer_.clear();
        }
    }
}

void Kenwood::send_command(const std::string& cmd) {
    if (send_callback_) {
        send_callback_(reinterpret_cast<const uint8_t*>(cmd.c_str()), cmd.size());
    } else if (serial_) {
        serial_->write(reinterpret_cast<const uint8_t*>(cmd.c_str()), cmd.size());
    }
}

std::string Kenwood::build_freq_command(char vfo, uint32_t freq_hz) {
    // Format: FA00014250000; (11 digits, Hz)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "F%c%011u;", vfo, freq_hz);
    return std::string(buf);
}

std::string Kenwood::build_mode_command(RadioMode mode) {
    // Format: MD2; (mode number)
    char buf[8];
    std::snprintf(buf, sizeof(buf), "MD%d;", static_cast<int>(radio_mode_to_kenwood(mode)));
    return std::string(buf);
}

KenwoodMode Kenwood::radio_mode_to_kenwood(RadioMode mode) {
    switch (mode) {
        case RadioMode::LSB:      return KenwoodMode::LSB;
        case RadioMode::USB:      return KenwoodMode::USB;
        case RadioMode::CW:       return KenwoodMode::CW;
        case RadioMode::FM:       return KenwoodMode::FM;
        case RadioMode::AM:       return KenwoodMode::AM;
        case RadioMode::FSK:      return KenwoodMode::FSK;
        case RadioMode::RTTY:     return KenwoodMode::FSK;
        case RadioMode::CW_R:     return KenwoodMode::CW_R;
        case RadioMode::FSK_R:    return KenwoodMode::FSK_R;
        case RadioMode::DATA_USB: return KenwoodMode::USB;
        case RadioMode::DATA_LSB: return KenwoodMode::LSB;
        default:                  return KenwoodMode::USB;
    }
}

RadioMode Kenwood::kenwood_to_radio_mode(KenwoodMode mode) {
    switch (mode) {
        case KenwoodMode::LSB:   return RadioMode::LSB;
        case KenwoodMode::USB:   return RadioMode::USB;
        case KenwoodMode::CW:    return RadioMode::CW;
        case KenwoodMode::FM:    return RadioMode::FM;
        case KenwoodMode::AM:    return RadioMode::AM;
        case KenwoodMode::FSK:   return RadioMode::FSK;
        case KenwoodMode::CW_R:  return RadioMode::CW_R;
        case KenwoodMode::FSK_R: return RadioMode::FSK_R;
        default:                 return RadioMode::USB;
    }
}

} // namespace pal
