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
#include <vector>

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

    // Set TX power only (0-100%), without disturbing frequency/mode. Default
    // implementation is a read-modify-write through set_channel(); backends
    // that can set power independently (e.g. hamlib RIG_LEVEL_RFPOWER) override
    // this to send only the power command.
    virtual bool set_power(int pct) {
        Channel c = get_channel();
        c.power = pct;
        return set_channel(c);
    }

    // True if this backend can actually apply set_power()/Channel::power to
    // the hardware (e.g. hamlib backends where rig_has_set_level(RFPOWER) is
    // true for the connected rig). Default false: callers (GUI, bridge) must
    // not present a power control as functional unless this returns true —
    // an RF-safety requirement, since a silently-ignored power command would
    // let the operator believe they reduced power when they did not.
    virtual bool supports_power_control() const { return false; }

    // Query the radio for its actual current frequency and mode and update the
    // internal channel state.  Returns true if anything changed.  The default
    // implementation is a no-op (returns false) for backends that have no
    // independent radio state to read back (e.g. software modems, mocks).
    virtual bool sync_from_radio() { return false; }

    // Block until all previously enqueued commands have been dispatched to the
    // hardware.  The default is a no-op for synchronous backends; asynchronous
    // backends (e.g. HamlibRadio) override this to drain the command queue.
    // Primarily useful in tests that need to verify hardware-side effects of
    // set_channel() / sync_from_radio() without introducing arbitrary sleeps.
    virtual void flush() {}

    // Optional CAT-traffic diagnostics (opt-in, off by default). When enabled,
    // a backend that talks a text CAT protocol (e.g. HamlibRadio/rigctld)
    // records one line per command — command, response/error, elapsed time —
    // and drain_cat_trace() polls and clears them. Backends with no
    // meaningful CAT traffic (mocks, GPIO-only PTT) keep the default no-ops:
    // enabling does nothing and drain always returns empty.
    virtual void set_cat_trace_enabled(bool /*on*/) {}
    virtual std::vector<std::string> drain_cat_trace() { return {}; }

    // True when the radio has finished ("settled on") every tune command
    // (set_channel / set_frequency / set_mode) issued so far — i.e. no tune is
    // still outstanding on the hardware. Synchronous backends (mocks, software
    // modems, blocking serial radios) settle inside the setter itself, so the
    // default is always true and any settle gate becomes a no-op for them.
    // Asynchronous backends (HamlibRadio) return false while a tune is in flight
    // on the I/O worker. The ALE controller relays this to the scanner as its
    // hop-ready predicate: the SM only issues the next hop once the current tune
    // has settled, which both restores the 200 ms dwell cadence (tune latency
    // overlaps the dwell) and upholds "at most one tune in flight".
    virtual bool is_tune_settled() const { return true; }

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
