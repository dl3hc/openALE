/**
 * @file icom_civ.cpp
 * @brief Icom CI-V protocol implementation
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#include "pal/radios/icom_civ.h"
#include <cstring>

namespace pal {

IcomCiv::IcomCiv(ISerial* serial, uint8_t radio_addr)
    : serial_(serial)
    , radio_addr_(radio_addr)
{
}

bool IcomCiv::initialize() {
    rx_buffer_.clear();
    rx_buffer_.reserve(64);
    ready_ = true;
    return true;
}

void IcomCiv::shutdown() {
    ready_ = false;
}

bool IcomCiv::start() {
    return ready_;
}

void IcomCiv::stop() {
    // Nothing to do
}

bool IcomCiv::set_channel(const Channel& channel) {
    if (!ready_ || !serial_) return false;
    
    // Set frequency (5 bytes BCD, 10 digits, 1 Hz resolution)
    uint8_t freq_bcd[5];
    freq_to_bcd(channel.rx_frequency, freq_bcd, 5);
    auto frame = build_frame(CivCommand::SET_FREQ, freq_bcd, 5);
    send_frame(frame);
    
    // Set mode
    uint8_t mode_data[1] = { static_cast<uint8_t>(radio_mode_to_civ(channel.rx_mode)) };
    frame = build_frame(CivCommand::SET_MODE, mode_data, 1);
    send_frame(frame);
    
    current_channel_ = channel;
    return true;
}

Channel IcomCiv::get_channel() const {
    return current_channel_;
}

void IcomCiv::set_ptt(bool transmit) {
    if (!ready_ || !serial_) return;
    
    // CI-V PTT: command 0x1C, subcommand 0x00, data 0x01 (TX) or 0x00 (RX)
    uint8_t ptt_data[1] = { transmit ? uint8_t(0x01) : uint8_t(0x00) };
    auto frame = build_frame(CivCommand::PTT, 0x00, ptt_data, 1);
    send_frame(frame);
    
    transmitting_ = transmit;
}

bool IcomCiv::is_transmitting() const {
    return transmitting_;
}

bool IcomCiv::is_ready() const {
    return ready_;
}

std::string IcomCiv::get_port_config() const {
    // Icom default: 9600,n,8,1 (newer radios)
    // Older radios: 1200 or 4800
    return "9600,n,8,1";
}

void IcomCiv::register_send_callback(SendCommandCallback callback) {
    send_callback_ = callback;
}

void IcomCiv::register_ack_callback(AckCallback callback) {
    ack_callback_ = callback;
}

void IcomCiv::process_response(const uint8_t* data, size_t length) {
    // Look for valid CI-V frame: FE FE ... FD
    for (size_t i = 0; i < length; i++) {
        rx_buffer_.push_back(data[i]);
        
        if (data[i] == CIV_EOM) {
            // Frame complete, parse it
            if (rx_buffer_.size() >= 6 && 
                rx_buffer_[0] == CIV_PREAMBLE && 
                rx_buffer_[1] == CIV_PREAMBLE) {
                
                // Check if it's ACK or NAK
                // ACK: FE FE E0 radio_addr FB FD
                // NAK: FE FE E0 radio_addr FA FD
                if (rx_buffer_.size() >= 6) {
                    uint8_t cmd = rx_buffer_[4];
                    if (cmd == CIV_ACK) {
                        if (ack_callback_) ack_callback_();
                    }
                    // NAK handling would go here
                }
            }
            rx_buffer_.clear();
        }
        
        // Prevent buffer overflow
        if (rx_buffer_.size() > 256) {
            rx_buffer_.clear();
        }
    }
}

std::vector<uint8_t> IcomCiv::build_frame(CivCommand cmd, const uint8_t* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.reserve(6 + len);
    
    frame.push_back(CIV_PREAMBLE);      // FE
    frame.push_back(CIV_PREAMBLE);      // FE
    frame.push_back(radio_addr_);       // Radio address
    frame.push_back(controller_addr_);  // Controller address (E0)
    frame.push_back(static_cast<uint8_t>(cmd));
    
    if (data && len > 0) {
        frame.insert(frame.end(), data, data + len);
    }
    
    frame.push_back(CIV_EOM);           // FD
    
    return frame;
}

std::vector<uint8_t> IcomCiv::build_frame(CivCommand cmd, uint8_t subcmd, const uint8_t* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.reserve(7 + len);
    
    frame.push_back(CIV_PREAMBLE);      // FE
    frame.push_back(CIV_PREAMBLE);      // FE
    frame.push_back(radio_addr_);       // Radio address
    frame.push_back(controller_addr_);  // Controller address (E0)
    frame.push_back(static_cast<uint8_t>(cmd));
    frame.push_back(subcmd);
    
    if (data && len > 0) {
        frame.insert(frame.end(), data, data + len);
    }
    
    frame.push_back(CIV_EOM);           // FD
    
    return frame;
}

void IcomCiv::send_frame(const std::vector<uint8_t>& frame) {
    if (send_callback_) {
        send_callback_(frame.data(), frame.size());
    } else if (serial_) {
        serial_->write(frame.data(), frame.size());
    }
}

void IcomCiv::freq_to_bcd(uint32_t freq_hz, uint8_t* bcd, size_t len) {
    // CI-V uses BCD encoding, LSB first
    // 14.250.000 Hz -> 00 00 25 14 00 (5 bytes)
    for (size_t i = 0; i < len; i++) {
        uint8_t lo = freq_hz % 10;
        freq_hz /= 10;
        uint8_t hi = freq_hz % 10;
        freq_hz /= 10;
        bcd[i] = (hi << 4) | lo;
    }
}

uint32_t IcomCiv::bcd_to_freq(const uint8_t* bcd, size_t len) {
    uint32_t freq = 0;
    uint32_t mult = 1;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t lo = bcd[i] & 0x0F;
        uint8_t hi = (bcd[i] >> 4) & 0x0F;
        freq += lo * mult;
        mult *= 10;
        freq += hi * mult;
        mult *= 10;
    }
    
    return freq;
}

CivMode IcomCiv::radio_mode_to_civ(RadioMode mode) {
    switch (mode) {
        case RadioMode::LSB:      return CivMode::LSB;
        case RadioMode::USB:      return CivMode::USB;
        case RadioMode::AM:       return CivMode::AM;
        case RadioMode::CW:       return CivMode::CW;
        case RadioMode::RTTY:     return CivMode::RTTY;
        case RadioMode::FM:       return CivMode::FM;
        case RadioMode::CW_R:     return CivMode::CW_R;
        case RadioMode::FSK:      return CivMode::RTTY;
        case RadioMode::FSK_R:    return CivMode::RTTY_R;
        case RadioMode::DATA_LSB: return CivMode::LSB;  // Some radios have DATA modes
        case RadioMode::DATA_USB: return CivMode::USB;
        default:                  return CivMode::USB;
    }
}

RadioMode IcomCiv::civ_to_radio_mode(CivMode mode) {
    switch (mode) {
        case CivMode::LSB:    return RadioMode::LSB;
        case CivMode::USB:    return RadioMode::USB;
        case CivMode::AM:     return RadioMode::AM;
        case CivMode::CW:     return RadioMode::CW;
        case CivMode::RTTY:   return RadioMode::RTTY;
        case CivMode::FM:     return RadioMode::FM;
        case CivMode::CW_R:   return RadioMode::CW_R;
        case CivMode::RTTY_R: return RadioMode::FSK_R;
        default:              return RadioMode::USB;
    }
}

} // namespace pal
