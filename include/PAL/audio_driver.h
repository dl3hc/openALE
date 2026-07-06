/**
 * \file PAL/audio_driver.h
 * \brief Platform audio driver interface — pull model (8 kHz, mono, 16-bit PCM).
 *
 * Architecture
 * ────────────
 * The audio driver is the sole real-time execution boundary between the ALE
 * domain and the OS audio subsystem.  A dedicated audio thread pulls symbol
 * frames on demand, renders PCM via ToneGenerator, resamples to the device
 * rate, and hands the result to the hardware.
 *
 * TX data flow (pull model):
 *
 *   Main thread  : modem.enqueue_word(w)       — encodes word, stores frame
 *   Audio thread : sym_pull(out_49)            — copies 49 symbols into out_49
 *   Audio thread : ToneGenerator.render(syms)  — produces 8 kHz PCM
 *   Audio thread : Resampler → hardware buffer — platform playback
 *
 * Main-loop contract:
 *   set_symbol_source(fn)  — register modem pull callback; call once after open()
 *   arm_frame_complete(cb) — arm one-shot notification after next frame renders
 *   tick(rx_out)           — drain captured audio; fire pending completions
 *
 * Audio format: PCM, 1 channel, 8000 Hz, 16-bit signed integers.
 *
 * Platform implementations: WasapiAudioDriver (Windows), NullAudioDriver (stub).
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pal {

class IAudioDriver {
public:
    virtual ~IAudioDriver() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Open audio input (RX) and output (TX) devices.
     *
     * \param rx_device  Substring of the capture device name, or "" for default.
     * \param tx_device  Substring of the render device name, or "" for default.
     * \return true on success.
     */
    virtual bool open(const std::string& rx_device = "",
                      const std::string& tx_device = "") = 0;

    /** Stop I/O and release all resources.  Safe to call if not open. */
    virtual void close() = 0;

    // ── TX pull model ─────────────────────────────────────────────────────────

    /**
     * Register the symbol pull function.
     *
     * fn(out_49) is called by the audio thread to obtain the next 49-symbol
     * frame.  Returns true and fills out_49 when a frame is available; false
     * when idle (driver renders silence).
     *
     * Must be called from the main thread before TX begins.
     * Thread-safe with respect to the audio thread.
     */
    virtual void set_symbol_source(std::function<bool(uint8_t*)> fn) = 0;

    /**
     * Arm a one-shot frame-completion notification.
     *
     * cb fires from the main-loop thread (inside tick()) after the NEXT symbol
     * frame pulled from the symbol source has been fully written into the
     * hardware render buffer.  Multiple arms are queued FIFO.
     *
     * On the NullAudioDriver the callback fires during the next tick() that
     * drains the pending symbol frame (offline / test mode).
     */
    virtual void arm_frame_complete(std::function<void()> cb) = 0;

    // ── Main-loop driver ──────────────────────────────────────────────────────

    /**
     * Drain captured RX samples and fire pending frame completions.
     *
     * Must be called from the main loop at ~1 ms intervals.  Non-blocking.
     *
     * \param rx_out  Captured 8 kHz mono 16-bit samples are appended here.
     */
    virtual void tick(std::vector<int16_t>& rx_out) = 0;

    // ── TX level ─────────────────────────────────────────────────────────────

    /**
     * Set TX output amplitude (0.0 = mute, 1.0 = 0 dBFS).
     *
     * Thread-safe; effective on the next symbol frame rendered.
     * Default: 0.25 (−12 dBFS) — matches ale::TX_AMPLITUDE.
     */
    virtual void set_tx_volume(float level) { (void)level; }

    // ── Inspection ────────────────────────────────────────────────────────────

    /** True after a successful open(), false after close(). */
    virtual bool is_open() const = 0;

    /** Enumerate available audio device names (IN:/OUT: prefixed). */
    virtual std::vector<std::string> list_devices() const = 0;
};

/** Factory: returns the platform-appropriate IAudioDriver. */
std::unique_ptr<IAudioDriver> create_audio_driver();

} // namespace pal
