/**
 * \file App/null_audio.cpp
 * \brief NullAudioDriver — stub for platforms without a real-time audio backend.
 *
 * Simulates pull consumption from tick() so the modem and state machine can
 * run offline (test mode, macOS, other POSIX).  No actual I/O is performed.
 */

#include "PAL/audio_driver.h"
#include "PAL/logger.h"
#include "FSK/ale_waveform.h"
#include <queue>

namespace ale {

class NullDevice : public pal::IAudioDriver {
    bool open_ = false;
    std::function<bool(uint8_t*)>              sym_pull_;
    std::function<size_t(int16_t*, size_t)>    pcm_pull_;
    std::queue<std::function<void()>>          pending_completions_;

public:
    bool open(const std::string& = "", const std::string& = "") override {
        pal::log_info("audio", "NullDevice — no real-time audio on this platform.");
        open_ = true;
        return true;
    }

    void close() override {
        open_      = false;
        sym_pull_  = nullptr;
        pcm_pull_  = nullptr;
        while (!pending_completions_.empty()) pending_completions_.pop();
    }

    void set_symbol_source(std::function<bool(uint8_t*)> fn) override {
        sym_pull_ = std::move(fn);
    }

    void set_pcm_source(std::function<size_t(int16_t*, size_t)> fn) override {
        pcm_pull_ = std::move(fn);
    }

    void arm_frame_complete(std::function<void()> cb) override {
        if (cb) pending_completions_.push(std::move(cb));
    }

    void tick(std::vector<int16_t>& /*rx_out*/) override {
        // PCM passthrough path takes precedence when set: drain whatever the
        // source offers so the pull model is exercised offline (test mode).
        if (pcm_pull_) {
            int16_t buf[160];
            while (pcm_pull_(buf, 160) > 0) { /* consumed */ }
            return;
        }
        if (!sym_pull_) return;
        uint8_t syms[SYMBOLS_PER_WORD];
        while (sym_pull_(syms)) {
            if (!pending_completions_.empty()) {
                pending_completions_.front()();
                pending_completions_.pop();
            }
        }
    }

    bool is_open() const override { return open_; }
    std::vector<std::string> list_devices() const override { return {}; }
};

} // namespace ale

namespace pal {
std::unique_ptr<IAudioDriver> create_audio_driver()
{
    return std::make_unique<ale::NullDevice>();
}
} // namespace pal
