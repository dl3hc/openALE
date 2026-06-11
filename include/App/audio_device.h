/**
 * \file App/audio_device.h
 * \brief Platform audio I/O abstraction for ALE CLI (8 kHz, mono, 16-bit PCM).
 *
 * Usage pattern (main loop):
 *
 *   auto dev = ale::make_audio_device();
 *   dev->open();
 *
 *   // From the TX callback of ALEController:
 *   dev->write_tx(pcm_samples, count);
 *
 *   // In the main loop at ~1 ms intervals:
 *   std::vector<int16_t> rx;
 *   dev->tick(rx);               // drains captured audio, manages buffers
 *   ctrl.feed_audio(rx.data(), rx.size());
 *
 * Platform implementations:
 *   Windows  — WinMM waveIn/waveOut with polling (CALLBACK_NULL)
 *   Other    — NullDevice (no-op; useful for offline / test builds)
 *
 * Audio format: PCM, 1 channel, 8000 Hz, 16-bit signed integers.
 */

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ale {

class AudioDevice {
public:
    static constexpr uint32_t SAMPLE_RATE = 8000;
    static constexpr uint16_t CHANNELS    = 1;
    static constexpr uint16_t BITS        = 16;

    virtual ~AudioDevice() = default;

    /**
     * Open audio input and output.
     *
     * \param in_device   Substring of the waveIn  device name ("" = system default).
     * \param out_device  Substring of the waveOut device name ("" = same as in_device).
     *
     * Single-device shortcut:  open("CABLE-A")       — same device for both directions.
     * Split-device loopback:   open("CABLE-B Output", "CABLE-A Input")
     *                                                 — TX → CABLE-A, RX ← CABLE-B.
     * \return true on success
     */
    virtual bool open(const std::string& in_device  = "",
                      const std::string& out_device = "") = 0;

    /** Stop I/O and release resources. Safe to call even if not open. */
    virtual void close() = 0;

    /**
     * Queue PCM samples for audio output.
     * Thread-safe; may be called from any thread.
     */
    virtual void write_tx(const int16_t* samples, uint32_t count) = 0;

    /**
     * Process done audio buffers and collect captured input.
     * Must be called from the main loop.  Non-blocking.
     * \param rx_out  Captured samples are appended here.
     */
    virtual void tick(std::vector<int16_t>& rx_out) = 0;

    /** True if the device was successfully opened. */
    virtual bool is_open() const = 0;

    /**
     * Enumerate available audio device names.
     * Each entry is a human-readable name that can be passed to open().
     */
    virtual std::vector<std::string> list_devices() const = 0;
};

/** Factory: returns the platform-appropriate AudioDevice. */
std::unique_ptr<AudioDevice> make_audio_device();

} // namespace ale
