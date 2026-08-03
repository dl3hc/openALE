/**
 * @file yaesu_cat.h
 * @brief Yaesu CAT protocol encoder/decoder
 * 
 * Yaesu CAT frame format (5-byte commands):
 *   [P1] [P2] [P3] [P4] [CMD]
 * 
 * Supports FT-817, FT-857, FT-897, FT-991, etc.
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include "pal/radio.h"
#include "pal/serial.h"
#include <cstdint>
#include <vector>

namespace pal {

/**
 * @brief Yaesu CAT command codes
 */
enum class YaesuCommand : uint8_t {
    SET_FREQ = 0x01,        // Set frequency
    SPLIT_ON = 0x02,        // Split on
    SPLIT_OFF = 0x82,       // Split off
    READ_FREQ = 0x03,       // Read frequency/mode
    SET_MODE = 0x07,        // Set mode
    PTT_ON = 0x08,          // PTT on
    PTT_OFF = 0x88,         // PTT off
    READ_RX_STATUS = 0xE7,  // Read RX status
    READ_TX_STATUS = 0xF7,  // Read TX status
    LOCK_ON = 0x00,         // Lock on
    LOCK_OFF = 0x80,        // Lock off
    CLAR_ON = 0x05,         // Clarifier on
    CLAR_OFF = 0x85,        // Clarifier off
    VFO_A = 0x81,           // Select VFO A
    VFO_B = 0x81,           // Select VFO B (with param)
    POWER_ON = 0x0F,        // Power on
    POWER_OFF = 0x8F,       // Power off
};

/**
 * @brief Yaesu mode codes
 */
enum class YaesuMode : uint8_t {
    LSB = 0x00,
    USB = 0x01,
    CW = 0x02,
    CW_R = 0x03,
    AM = 0x04,
    FM = 0x08,
    DIG = 0x0A,
    PKT = 0x0C,
    FM_N = 0x88,    // FM Narrow
    DIG_USB = 0x0A, // Digital USB
    DIG_LSB = 0x0A, // Digital LSB (with filter param)
};

/**
 * @brief Yaesu CAT radio implementation
 */
class YaesuCat : public IRadio {
public:
    /**
     * @brief Constructor
     * @param serial Serial port interface (injected)
     */
    explicit YaesuCat(ISerial* serial);
    
    ~YaesuCat() override = default;
    
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
    
private:
    // Build 5-byte CAT command
    std::vector<uint8_t> build_command(YaesuCommand cmd, 
                                        uint8_t p1 = 0, uint8_t p2 = 0, 
                                        uint8_t p3 = 0, uint8_t p4 = 0);
    
    // Send command via serial
    void send_command(const std::vector<uint8_t>& cmd);
    
    // Frequency encoding (packed BCD, MSB first)
    static void freq_to_packed_bcd(uint32_t freq_hz, uint8_t* bcd);
    static uint32_t packed_bcd_to_freq(const uint8_t* bcd);
    
    // Mode conversion
    static YaesuMode radio_mode_to_yaesu(RadioMode mode);
    static RadioMode yaesu_to_radio_mode(YaesuMode mode);
    
    ISerial* serial_;
    
    Channel current_channel_;
    bool transmitting_ = false;
    bool ready_ = false;
    
    SendCommandCallback send_callback_;
    AckCallback ack_callback_;
};

} // namespace pal
