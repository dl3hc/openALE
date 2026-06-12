/**
 * @file icom_civ.h
 * @brief Icom CI-V protocol encoder/decoder
 * 
 * CI-V frame format:
 *   FE FE [radio_addr] [ctrl_addr] [cmd] [subcmd] [data...] FD
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
#include <memory>

namespace pal {

/**
 * @brief CI-V protocol constants
 */
constexpr uint8_t CIV_PREAMBLE = 0xFE;
constexpr uint8_t CIV_EOM = 0xFD;           // End of message
constexpr uint8_t CIV_CONTROLLER = 0xE0;    // Default controller address
constexpr uint8_t CIV_ACK = 0xFB;
constexpr uint8_t CIV_NAK = 0xFA;

/**
 * @brief CI-V command codes
 */
enum class CivCommand : uint8_t {
    SET_FREQ = 0x05,        // Set frequency (BCD)
    SET_MODE = 0x06,        // Set mode
    SET_VFO = 0x07,         // Select VFO
    SET_MEM = 0x08,         // Select memory channel
    READ_FREQ = 0x03,       // Read frequency
    READ_MODE = 0x04,       // Read mode
    PTT = 0x1C,             // PTT control (subcommand 0x00)
    SPLIT = 0x0F,           // Split operation
    VFO_EQUAL = 0x07,       // VFO A=B (subcommand 0xA0)
};

/**
 * @brief CI-V mode codes
 */
enum class CivMode : uint8_t {
    LSB = 0x00,
    USB = 0x01,
    AM = 0x02,
    CW = 0x03,
    RTTY = 0x04,
    FM = 0x05,
    CW_R = 0x07,
    RTTY_R = 0x08,
    DV = 0x17,
};

/**
 * @brief Known Icom radio addresses
 */
struct IcomRadioAddress {
    static constexpr uint8_t IC_735 = 0x04;
    static constexpr uint8_t IC_706 = 0x48;
    static constexpr uint8_t IC_706MKII = 0x4E;
    static constexpr uint8_t IC_706MKIIG = 0x58;
    static constexpr uint8_t IC_718 = 0x5E;
    static constexpr uint8_t IC_746 = 0x56;
    static constexpr uint8_t IC_756 = 0x50;
    static constexpr uint8_t IC_756PRO = 0x5C;
    static constexpr uint8_t IC_7000 = 0x70;
    static constexpr uint8_t IC_7100 = 0x88;
    static constexpr uint8_t IC_7200 = 0x76;
    static constexpr uint8_t IC_7300 = 0x94;
    static constexpr uint8_t IC_7600 = 0x7A;
    static constexpr uint8_t IC_7610 = 0x98;
    static constexpr uint8_t IC_7700 = 0x74;
    static constexpr uint8_t IC_7800 = 0x6A;
    static constexpr uint8_t IC_7850 = 0x8E;
    static constexpr uint8_t IC_7851 = 0x8E;
    static constexpr uint8_t IC_9700 = 0xA2;
};

/**
 * @brief Icom CI-V radio implementation
 */
class IcomCiv : public IRadio {
public:
    /**
     * @brief Constructor
     * @param serial Serial port interface (injected)
     * @param radio_addr CI-V address of the radio
     */
    IcomCiv(ISerial* serial, uint8_t radio_addr);
    
    ~IcomCiv() override = default;
    
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
    
    // CI-V specific
    void set_radio_address(uint8_t addr) { radio_addr_ = addr; }
    uint8_t get_radio_address() const { return radio_addr_; }
    
private:
    // Build CI-V frame
    std::vector<uint8_t> build_frame(CivCommand cmd, const uint8_t* data = nullptr, size_t len = 0);
    std::vector<uint8_t> build_frame(CivCommand cmd, uint8_t subcmd, const uint8_t* data = nullptr, size_t len = 0);
    
    // Send frame via serial
    void send_frame(const std::vector<uint8_t>& frame);
    
    // Frequency encoding (BCD)
    static void freq_to_bcd(uint32_t freq_hz, uint8_t* bcd, size_t len);
    static uint32_t bcd_to_freq(const uint8_t* bcd, size_t len);
    
    // Mode conversion
    static CivMode radio_mode_to_civ(RadioMode mode);
    static RadioMode civ_to_radio_mode(CivMode mode);
    
    ISerial* serial_;
    uint8_t radio_addr_;
    uint8_t controller_addr_ = CIV_CONTROLLER;
    
    Channel current_channel_;
    bool transmitting_ = false;
    bool ready_ = false;
    
    SendCommandCallback send_callback_;
    AckCallback ack_callback_;
    
    // Response buffer for parsing
    std::vector<uint8_t> rx_buffer_;
};

} // namespace pal
