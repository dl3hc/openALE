/**
 * @file serial.h
 * @brief Platform-agnostic serial port interface
 * 
 * Abstracts serial I/O so radio protocol code doesn't know
 * if it's talking to termios, Win32 COM, or anything else.
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <functional>

namespace pal {

/**
 * @brief Parity settings
 */
enum class Parity {
    NONE,
    ODD,
    EVEN
};

/**
 * @brief Stop bits
 */
enum class StopBits {
    ONE,
    TWO
};

/**
 * @brief Serial port configuration
 */
struct SerialConfig {
    uint32_t baud_rate = 9600;
    uint8_t data_bits = 8;
    Parity parity = Parity::NONE;
    StopBits stop_bits = StopBits::ONE;
    bool rts_cts = false;           ///< Hardware flow control
    uint32_t timeout_ms = 1000;     ///< Read timeout
};

/**
 * @brief Serial port interface
 * 
 * Platform implementations provide the actual I/O:
 * - Linux: termios
 * - Windows: Win32 COM API
 */
class ISerial {
public:
    virtual ~ISerial() = default;
    
    /**
     * @brief Callback for received data
     */
    using ReceiveCallback = std::function<void(const uint8_t* data, size_t length)>;
    
    // Lifecycle
    virtual bool open(const std::string& port, const SerialConfig& config) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    
    // I/O
    virtual size_t write(const uint8_t* data, size_t length) = 0;
    virtual size_t read(uint8_t* buffer, size_t max_length) = 0;
    
    // Async receive (optional - platform may implement polling instead)
    virtual void set_receive_callback(ReceiveCallback callback) = 0;
    
    // Line control (for PTT via RTS/DTR)
    virtual void set_rts(bool state) = 0;
    virtual void set_dtr(bool state) = 0;
    virtual bool get_cts() const = 0;
    virtual bool get_dsr() const = 0;
    
    // Buffer control
    virtual void flush() = 0;
    virtual size_t available() const = 0;
};

/**
 * @brief Factory function - implemented by platform
 */
std::unique_ptr<ISerial> create_serial();

/**
 * @brief Parse port config string "9600,n,8,1" to SerialConfig
 */
SerialConfig parse_port_string(const std::string& config);

} // namespace pal
