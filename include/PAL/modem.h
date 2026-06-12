#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class IModem {
public:
    virtual ~IModem() = default;

    // =========================================================================
    // Identity
    // =========================================================================
    virtual const char* id()           const = 0;
    virtual const char* display_name() const = 0;
    virtual const char* description()  const = 0;
    virtual uint32_t    version()      const { return 1; }

    // =========================================================================
    // Capabilities
    // =========================================================================
    struct Capabilities {
        bool supports_tx                   = true;
        bool supports_rx                   = false;
        bool supports_streaming            = true;
        bool supports_variable_sample_rate = false;
    };
    virtual Capabilities capabilities() const = 0;

    // =========================================================================
    // Configuration
    // =========================================================================
    virtual bool configure(const std::string& json_config) { return true; }

    // =========================================================================
    // TX Path — generisch, protokollunabhängig
    //
    // Contract:
    //   transmit()        → rohe Nutzdaten einreihen, non-blocking
    //                       was "data" bedeutet ist modemspezifisch
    //   get_next_sample() → per Sample vom Audio-Callback aufgerufen
    //   is_transmitting() → true solange Samples anstehen
    //
    // =========================================================================
    virtual bool  transmit(const uint8_t* data, size_t size) = 0;
    virtual float get_next_sample()       = 0;
    virtual bool  is_transmitting() const = 0;

    // =========================================================================
    // RX Path — generisch, protokollunabhängig
    //
    // Contract:
    //   push_sample()        → per Sample vom Audio-Callback aufgerufen
    //                          gibt true zurück wenn ein Frame komplett ist
    //   get_received_data()  → liefert dekodierten Frame als rohe Bytes
    //
    // =========================================================================
    virtual bool push_sample(float sample)                              { return false; }
    virtual bool get_received_data(std::vector<uint8_t>& out_data)     { return false; }

    // =========================================================================
    // Control
    // =========================================================================
    virtual void   reset()                {}
    virtual size_t preferred_frame_size() const { return 0; }
};