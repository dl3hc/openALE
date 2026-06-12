/**
 * \file App/audio_device.h
 * \brief Platform audio I/O abstraction for ALE CLI (8 kHz, mono, 16-bit PCM).
 *
 * Architecture
 * ────────────
 * The audio device is the ONLY real-time execution boundary.  A dedicated audio
 * thread (WASAPI event-driven on Windows, or a tick-simulated NullDevice on other
 * platforms) pulls symbol frames from a registered source, renders PCM via an
 * internal ToneGenerator, and feeds the hardware.
 *
 * TX data flow (pull model):
 *
 *   Main thread: modem.enqueue_word(w)        — builds symbol frame, stores it
 *   Audio thread: sym_pull(out_49)            — copies 49 symbols into out_49
 *   Audio thread: ToneGenerator.render(syms)  — produces 8 kHz PCM
 *   Audio thread: Resampler → WASAPI buffer   — hardware playback
 *
 * Main-loop contract:
 *   set_symbol_source(fn)  — register modem pull function; call once after open()
 *   arm_frame_complete(cb) — arm one-shot callback fired after next frame renders
 *   tick(rx_out)           — drain captured audio; fires armed completions (main-loop)
 *
 * Forbidden:
 *   write_tx() / push_symbol_frame() — push-based TX is not supported.
 *
 * Audio format: PCM, 1 channel, 8000 Hz, 16-bit signed integers.
 */

#pragma once
#include <cstdint>
#include <functional>
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
     * \return true on success
     */
    virtual bool open(const std::string& in_device  = "",
                      const std::string& out_device = "") = 0;

    /** Stop I/O and release resources. Safe to call even if not open. */
    virtual void close() = 0;

    /**
     * Register the symbol pull function.
     *
     * fn(out_49) is called by the audio thread to obtain the next 49-symbol frame.
     * Returns true and fills out_49 when a frame is available; false for silence.
     *
     * Must be called from the main thread, before TX begins.
     * Thread-safe with respect to the audio thread.
     */
    virtual void set_symbol_source(std::function<bool(uint8_t*)> fn) = 0;

    /**
     * Arm a one-shot frame-completion notification.
     *
     * cb fires (from the main-loop thread, via tick()) after the NEXT symbol frame
     * pulled from the symbol source has been fully written into the hardware render
     * buffer.  Calls are FIFO: completions fire in the order they are armed.
     *
     * On NullDevice the callback is deferred to the next tick() call that drains
     * the pending symbol frame.
     */
    virtual void arm_frame_complete(std::function<void()> cb) = 0;

    /**
     * Process done audio buffers and collect captured input.
     * Must be called from the main loop at ~1 ms intervals.  Non-blocking.
     * Also fires armed frame-completion callbacks when their frames have rendered.
     * \param rx_out  Captured 8 kHz samples are appended here.
     */
    virtual void tick(std::vector<int16_t>& rx_out) = 0;

    /** True if the device was successfully opened. */
    virtual bool is_open() const = 0;

    /** Enumerate available audio device names. */
    virtual std::vector<std::string> list_devices() const = 0;
};

/** Factory: returns the platform-appropriate AudioDevice. */
std::unique_ptr<AudioDevice> make_audio_device();

} // namespace ale
