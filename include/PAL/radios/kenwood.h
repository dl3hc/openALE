/**
 * @file kenwood.h
 * @brief Kenwood CAT protocol encoder/decoder
 * 
 * Kenwood uses ASCII commands terminated with semicolon:
 *   FA00014250000;  (Set VFO A to 14.250 MHz)
 *   MD2;            (Set USB mode)
 *   TX;             (Transmit)
 *   RX;             (Receive)
 * 
 * Supports TS-480, TS-590, TS-890, TS-990, etc.
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include "pal/radio.h"
#include "pal/serial.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pal {

/**
 * @brief Kenwood mode codes
 */
enum class KenwoodMode : uint8_t {
    LSB = 1,
    USB = 2,
    CW = 3,
    FM = 4,
    AM = 5,
    FSK = 6,
    CW_R = 7,
    FSK_R = 9,
};

/**
 * @brief Kenwood CAT radio implementation
 */
class Kenwood : public IRadio {
public:
    /**
     * @brief Constructor
     * @param serial Serial port interface (injected)
     */
    explicit Kenwood(ISerial* serial);
    
    ~Kenwood() override = default;
    
    // IRadio interface
    bool initialize() override;
    void shutdown() override;
    bool start() override;
    void stop() override;
    
    bool set_channel(const Channel& channel) override;
    Channel get_channel() const override;
    
    void set_ptt(bool transmit) override;
    bool is_transmitting() const override;
    
    bool is_ready() const override;
    std::string get_port_config() const override;
    
    void register_send_callback(SendCommandCallback callback) override;
    void register_ack_callback(AckCallback callback) override;
    void process_response(const uint8_t* data, size_t length) override;
    
protected:
    // Send ASCII command with semicolon terminator
    void send_command(const std::string& cmd);
    
private:
    
    // Build frequency command: FA00014250000;
    std::string build_freq_command(char vfo, uint32_t freq_hz);
    
    // Build mode command: MD2;
    std::string build_mode_command(RadioMode mode);
    
    // Mode conversion
    static KenwoodMode radio_mode_to_kenwood(RadioMode mode);
    static RadioMode kenwood_to_radio_mode(KenwoodMode mode);
    
    ISerial* serial_;
    
    Channel current_channel_;
    bool transmitting_ = false;
    bool ready_ = false;
    
    SendCommandCallback send_callback_;
    AckCallback ack_callback_;
    
    // Response buffer for parsing
    std::string rx_buffer_;
};

} // namespace pal
