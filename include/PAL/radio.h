/**
 * @file radio.h
 * @brief Platform-agnostic radio interface
 * 
 * Based on original radio.dll interface specification.
 * Includes frequency, mode, PTT, power, and antenna control.
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

namespace pal {

/**
 * @brief Radio operating modes
 */
enum class RadioMode {
    LSB = 0,        ///< Lower Side Band
    USB = 1,        ///< Upper Side Band
    CW = 2,         ///< Continuous Wave
    FM = 3,         ///< Frequency Modulation
    FMW = 4,        ///< FM Wide
    AM = 5,         ///< Amplitude Modulation
    FSK = 6,        ///< Frequency Shift Keying
    RTTY = 7,       ///< Radio Teletype
    CW_R = 8,       ///< CW Reverse
    TUNE = 9,       ///< Tune mode
    FSK_R = 10,     ///< FSK Reverse
    DIG = 11,       ///< Digital
    DATA_LSB = 12,  ///< Data LSB
    DATA_USB = 13,  ///< Data USB
    UNKNOWN = 14    ///< Unknown mode
};

/**
 * @brief Channel configuration
 */
struct Channel {
    uint8_t id = 0;                 ///< Channel ID
    uint32_t tx_frequency = 0;      ///< TX frequency in Hz
    uint32_t rx_frequency = 0;      ///< RX frequency in Hz
    RadioMode tx_mode = RadioMode::USB;  ///< TX mode
    RadioMode rx_mode = RadioMode::USB;  ///< RX mode
    int antenna = 1;                ///< Antenna selection (1-4)
    int power = 100;                ///< Power level (0-100%)
    int attenuation = 0;            ///< RX attenuation dB
    bool in_use = false;            ///< Channel in use flag
};

/**
 * @brief Radio interface - abstracts all radio control
 * 
 * Implementations handle the details:
 * - CAT protocol (CI-V, Yaesu, Kenwood, etc.)
 * - PTT method (GPIO, serial RTS/DTR, CAT command)
 * - Serial/network communication
 */
class IRadio {
public:
    virtual ~IRadio() = default;
    
    /**
     * @brief Callback for sending raw commands to radio
     */
    using SendCommandCallback = std::function<void(const uint8_t* data, size_t length)>;
    
    /**
     * @brief Callback for command acknowledgment
     */
    using AckCallback = std::function<void()>;
    
    // Lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    
    // Channel control
    virtual bool set_channel(const Channel& channel) = 0;
    virtual Channel get_channel() const = 0;

    // Single-attribute control — set only frequency or only mode without
    // disturbing the other. The default implementation does a read-modify-write
    // through set_channel(); backends that can set one attribute independently
    // (e.g. hamlib over TCP, where freq and mode are separate CAT commands)
    // override these to send only what changed and avoid touching the other.
    virtual bool set_frequency(uint32_t hz) {
        Channel c = get_channel();
        c.rx_frequency = hz;
        c.tx_frequency = hz;  // simplex
        return set_channel(c);
    }
    virtual bool set_mode(RadioMode mode) {
        Channel c = get_channel();
        c.rx_mode = c.tx_mode = mode;
        return set_channel(c);
    }

    // Query the radio for its actual current frequency and mode and update the
    // internal channel state.  Returns true if anything changed.  The default
    // implementation is a no-op (returns false) for backends that have no
    // independent radio state to read back (e.g. software modems, mocks).
    virtual bool sync_from_radio() { return false; }

    // PTT control (part of radio, not separate)
    virtual void set_ptt(bool transmit) = 0;
    virtual bool is_transmitting() const = 0;
    
    // Status
    virtual bool is_ready() const = 0;
    virtual std::string get_port_config() const = 0;  ///< e.g., "9600,n,8,1"
    
    // Callbacks for serial communication
    virtual void register_send_callback(SendCommandCallback callback) = 0;
    virtual void register_ack_callback(AckCallback callback) = 0;
    
    // Process response from radio
    virtual void process_response(const uint8_t* data, size_t length) = 0;
};

/**
 * @brief Factory function - implemented per platform/radio type
 * 
 * @param config Configuration string (implementation-specific)
 *               Examples:
 *               - "draws:gpio12" (DRAWS hat, GPIO PTT)
 *               - "icom:ci-v:9600" (Icom CI-V)
 *               - "yaesu:cat:38400" (Yaesu CAT)
 *               - "serial:rts:/dev/ttyUSB0" (Serial PTT only)
 */
std::unique_ptr<IRadio> create_radio(const std::string& config);

} // namespace pal
