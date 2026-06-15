/**
 * @file yaesu_cat.cpp
 * @brief Yaesu CAT protocol implementation
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#include "pal/radios/yaesu_cat.h"

namespace pal {

YaesuCat::YaesuCat(ISerial* serial)
    : serial_(serial)
{
}

bool YaesuCat::initialize() {
    ready_ = true;
    return true;
}

void YaesuCat::shutdown() {
    ready_ = false;
}

bool YaesuCat::start() {
    return ready_;
}

void YaesuCat::stop() {
    // Nothing to do
}

bool YaesuCat::set_channel(const Channel& channel) {
    if (!ready_ || !serial_) return false;
    
    // Set frequency
    // Yaesu format: 4 bytes packed BCD (10 Hz resolution) + command
    // 14.250.00 MHz -> 01 42 50 00 01
    uint8_t freq_bcd[4];
    freq_to_packed_bcd(channel.rx_frequency, freq_bcd);
    auto cmd = build_command(YaesuCommand::SET_FREQ, 
                             freq_bcd[0], freq_bcd[1], 
                             freq_bcd[2], freq_bcd[3]);
    send_command(cmd);
    
    // Set mode
    cmd = build_command(YaesuCommand::SET_MODE, 
                        static_cast<uint8_t>(radio_mode_to_yaesu(channel.rx_mode)),
                        0, 0, 0);
    send_command(cmd);
    
    current_channel_ = channel;
    return true;
}

Channel YaesuCat::get_channel() const {
    return current_channel_;
}

void YaesuCat::set_ptt(bool transmit) {
    if (!ready_ || !serial_) return;
    
    auto cmd = build_command(transmit ? YaesuCommand::PTT_ON : YaesuCommand::PTT_OFF);
    send_command(cmd);
    
    transmitting_ = transmit;
}

bool YaesuCat::is_transmitting() const {
    return transmitting_;
}

bool YaesuCat::is_ready() const {
    return ready_;
}

std::string YaesuCat::get_port_config() const {
    // Yaesu default: 9600 or 38400 depending on model
    return "9600,n,8,2";  // 2 stop bits common for Yaesu
}

void YaesuCat::register_send_callback(SendCommandCallback callback) {
    send_callback_ = callback;
}

void YaesuCat::register_ack_callback(AckCallback callback) {
    ack_callback_ = callback;
}

void YaesuCat::process_response(const uint8_t* data, size_t length) {
    // Yaesu returns status bytes
    // For now, just acknowledge receipt
    if (length > 0 && ack_callback_) {
        ack_callback_();
    }
}

std::vector<uint8_t> YaesuCat::build_command(YaesuCommand cmd, 
                                              uint8_t p1, uint8_t p2, 
                                              uint8_t p3, uint8_t p4) {
    std::vector<uint8_t> frame(5);
    frame[0] = p1;
    frame[1] = p2;
    frame[2] = p3;
    frame[3] = p4;
    frame[4] = static_cast<uint8_t>(cmd);
    return frame;
}

void YaesuCat::send_command(const std::vector<uint8_t>& cmd) {
    if (send_callback_) {
        send_callback_(cmd.data(), cmd.size());
    } else if (serial_) {
        serial_->write(cmd.data(), cmd.size());
    }
}

void YaesuCat::freq_to_packed_bcd(uint32_t freq_hz, uint8_t* bcd) {
    // Yaesu uses packed BCD, MSB first, 10 Hz resolution
    // 14.250.000 Hz -> 14250000 / 10 = 1425000 -> 01 42 50 00
    uint32_t freq_10hz = freq_hz / 10;
    
    // Pack into 4 bytes, MSB first
    bcd[0] = ((freq_10hz / 10000000) % 10) << 4 | ((freq_10hz / 1000000) % 10);
    bcd[1] = ((freq_10hz / 100000) % 10) << 4 | ((freq_10hz / 10000) % 10);
    bcd[2] = ((freq_10hz / 1000) % 10) << 4 | ((freq_10hz / 100) % 10);
    bcd[3] = ((freq_10hz / 10) % 10) << 4 | (freq_10hz % 10);
}

uint32_t YaesuCat::packed_bcd_to_freq(const uint8_t* bcd) {
    uint32_t freq = 0;
    
    freq += ((bcd[0] >> 4) & 0x0F) * 10000000;
    freq += (bcd[0] & 0x0F) * 1000000;
    freq += ((bcd[1] >> 4) & 0x0F) * 100000;
    freq += (bcd[1] & 0x0F) * 10000;
    freq += ((bcd[2] >> 4) & 0x0F) * 1000;
    freq += (bcd[2] & 0x0F) * 100;
    freq += ((bcd[3] >> 4) & 0x0F) * 10;
    freq += (bcd[3] & 0x0F);
    
    return freq * 10;  // Convert back to Hz
}

YaesuMode YaesuCat::radio_mode_to_yaesu(RadioMode mode) {
    switch (mode) {
        case RadioMode::LSB:      return YaesuMode::LSB;
        case RadioMode::USB:      return YaesuMode::USB;
        case RadioMode::CW:       return YaesuMode::CW;
        case RadioMode::CW_R:     return YaesuMode::CW_R;
        case RadioMode::AM:       return YaesuMode::AM;
        case RadioMode::FM:       return YaesuMode::FM;
        case RadioMode::DIG:      return YaesuMode::DIG;
        case RadioMode::FSK:      return YaesuMode::DIG;
        case RadioMode::DATA_USB: return YaesuMode::DIG_USB;
        case RadioMode::DATA_LSB: return YaesuMode::DIG_LSB;
        default:                  return YaesuMode::USB;
    }
}

RadioMode YaesuCat::yaesu_to_radio_mode(YaesuMode mode) {
    switch (mode) {
        case YaesuMode::LSB:    return RadioMode::LSB;
        case YaesuMode::USB:    return RadioMode::USB;
        case YaesuMode::CW:     return RadioMode::CW;
        case YaesuMode::CW_R:   return RadioMode::CW_R;
        case YaesuMode::AM:     return RadioMode::AM;
        case YaesuMode::FM:     return RadioMode::FM;
        case YaesuMode::DIG:    return RadioMode::DIG;
        case YaesuMode::PKT:    return RadioMode::FSK;
        default:                return RadioMode::USB;
    }
}

} // namespace pal
