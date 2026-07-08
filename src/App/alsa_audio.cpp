/**
 * \file App/alsa_audio.cpp
 * \brief ALSA audio driver (Linux).
 *
 *   Architecture (pull model — mirrors WASAPI implementation)
 *   ──────────────────────────────────────────────────────────
 *   A dedicated RT audio thread uses snd_pcm_wait() as its loop clock.
 *   On each wake-up it services render (TX) and then capture (RX).
 *
 *   TX data flow:
 *     audio thread → sym_pull_(out_49) → [modem's pull_symbol_frame()]
 *     audio thread → ToneGenerator → TxBandpass → Resampler → snd_pcm_writei
 *
 *   RX data flow:
 *     snd_pcm_readi → mono-mix → Resampler(device→8kHz) → [rx_mtx_ / rx_queue_]
 *     main thread → tick() → rx_out
 *
 *   Frame completion:
 *     snd_pcm_delay() gives frames still queued in the hardware ring buffer.
 *     played = total_written_ - delay  mirrors the WASAPI padding formula.
 *     When played crosses a word_play_target_, frames_rendered_ is incremented
 *     and tick() fires the armed callback.
 */

#include "PAL/audio_driver.h"
#include "App/resampler.h"
#include "FSK/tone_generator.h"
#include "FSK/tx_bandpass.h"
#include "FSK/ale_waveform.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ale {

// ─────────────────────────────────────────────────────────────────────────────
class AlsaDevice : public pal::IAudioDriver {
public:
    AlsaDevice() = default;
    ~AlsaDevice() override { close(); }

    bool open(const std::string& rx_device = "",
              const std::string& tx_device = "") override;
    void close() override;

    void set_symbol_source(std::function<bool(uint8_t*)> fn) override;
    void set_pcm_source(std::function<size_t(int16_t*, size_t)> fn) override;
    void arm_frame_complete(std::function<void()> cb) override;
    void tick(std::vector<int16_t>& rx_out) override;

    bool is_open() const override { return open_; }

    void set_tx_volume(float level) override {
        tx_volume_.store(std::clamp(level, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<std::string> list_devices() const override;

private:
    // ── PCM handles ──────────────────────────────────────────────────────────
    snd_pcm_t*  play_pcm_ = nullptr;
    snd_pcm_t*  cap_pcm_  = nullptr;

    // ── Negotiated hw params ─────────────────────────────────────────────────
    snd_pcm_format_t  play_fmt_      = SND_PCM_FORMAT_S16_LE;
    snd_pcm_format_t  cap_fmt_       = SND_PCM_FORMAT_S16_LE;
    unsigned int      play_rate_     = 48000;
    unsigned int      cap_rate_      = 48000;
    unsigned int      play_chans_    = 1;
    unsigned int      cap_chans_     = 1;
    snd_pcm_uframes_t period_frames_ = 2400;   // 50 ms @ 48 kHz

    // ── Symbol source ────────────────────────────────────────────────────────
    std::mutex                    sym_src_mtx_;
    std::function<bool(uint8_t*)> sym_pull_;
    // ── Raw-PCM TX source (transparent-voice passthrough) ───────────────────
    // Same mutex as sym_pull_. When non-null the render thread pulls 8 kHz PCM
    // from pcm_pull_ instead of symbols — see service_render().
    std::function<size_t(int16_t*, size_t)> pcm_pull_;

    // ── TX signal chain (audio-thread-only) ──────────────────────────────────
    ToneGenerator              at_tone_gen_;
    TxBandpass                 at_tx_filter_;
    std::unique_ptr<Resampler> at_tx_resampler_;
    std::vector<int16_t>       at_pcm_8k_;
    std::vector<int16_t>       at_pcm_filt_;
    std::vector<int16_t>       at_render_buf_;
    size_t                     at_render_pos_    = 0;
    bool                       at_frame_pending_ = false;
    std::vector<uint8_t>       at_write_scratch_; // device-format interleaved frames

    // ── Frame completion (mirrors WASAPI) ────────────────────────────────────
    // total_written_     — cumulative device frames handed to snd_pcm_writei
    // word_play_targets_ — write-end position of each word awaiting completion
    // played = total_written_ - snd_pcm_delay() → fire when played >= target
    std::atomic<uint64_t> frames_rendered_{0};
    uint64_t              total_written_ = 0;
    std::deque<uint64_t>  word_play_targets_;

    struct FrameNotify {
        uint64_t              target;
        std::function<void()> cb;
    };
    std::deque<FrameNotify> frame_notify_queue_;
    uint64_t                frames_armed_ = 0;

    // ── RX ───────────────────────────────────────────────────────────────────
    std::unique_ptr<Resampler> rx_resampler_;
    std::vector<int16_t>       at_cap_scratch_;
    std::vector<int16_t>       at_rx_8k_;
    std::mutex                 rx_mtx_;
    std::deque<int16_t>        rx_queue_;

    // ── Audio thread ─────────────────────────────────────────────────────────
    std::thread       audio_thread_;
    std::atomic<bool> audio_running_{false};
    std::atomic<float> tx_volume_{0.25f};      // 0.0–1.0; default = TX_AMPLITUDE
    bool              open_ = false;

    // ── Helpers ──────────────────────────────────────────────────────────────
    bool open_pcm(snd_pcm_t** out, const std::string& dev, snd_pcm_stream_t dir,
                  snd_pcm_format_t& fmt, unsigned int& rate, unsigned int& chans);

    static std::string       resolve_device(const std::string& hint, bool is_output);
    static snd_pcm_format_t  negotiate_format(snd_pcm_t* pcm, snd_pcm_hw_params_t* hw);

    void audio_loop();
    void service_render();
    void service_capture();

    // Per-frame sample conversion helpers
    void    s16_to_alsa(int16_t s, uint8_t* dst) const;
    int16_t alsa_to_s16(const uint8_t* src, unsigned int ch,
                         snd_pcm_format_t fmt, unsigned int chans) const;

    int play_bpf() const {
        return snd_pcm_format_physical_width(play_fmt_) / 8 * (int)play_chans_;
    }
    int cap_bpf() const {
        return snd_pcm_format_physical_width(cap_fmt_) / 8 * (int)cap_chans_;
    }
};

// ── Format negotiation ────────────────────────────────────────────────────────

snd_pcm_format_t AlsaDevice::negotiate_format(snd_pcm_t* pcm, snd_pcm_hw_params_t* hw)
{
    static const snd_pcm_format_t pref[] = {
        SND_PCM_FORMAT_S16_LE,
        SND_PCM_FORMAT_S32_LE,
        SND_PCM_FORMAT_FLOAT_LE,
    };
    for (auto fmt : pref)
        if (snd_pcm_hw_params_test_format(pcm, hw, fmt) == 0)
            return fmt;
    return SND_PCM_FORMAT_S16_LE;
}

// ── Sample conversion ─────────────────────────────────────────────────────────

void AlsaDevice::s16_to_alsa(int16_t s, uint8_t* dst) const
{
    const int bps = snd_pcm_format_physical_width(play_fmt_) / 8;
    for (unsigned int c = 0; c < play_chans_; ++c, dst += bps) {
        switch (play_fmt_) {
        case SND_PCM_FORMAT_S16_LE:
            dst[0] = (uint8_t)(s & 0xFF);
            dst[1] = (uint8_t)((s >> 8) & 0xFF);
            break;
        case SND_PCM_FORMAT_S32_LE: {
            int32_t v = (int32_t)s << 16;
            std::memcpy(dst, &v, 4);
            break;
        }
        case SND_PCM_FORMAT_FLOAT_LE: {
            float f = s / 32768.0f;
            std::memcpy(dst, &f, 4);
            break;
        }
        default:
            std::memset(dst, 0, bps);
        }
    }
}

int16_t AlsaDevice::alsa_to_s16(const uint8_t* src, unsigned int ch,
                                  snd_pcm_format_t fmt, unsigned int chans) const
{
    const int bps = snd_pcm_format_physical_width(fmt) / 8;
    const uint8_t* p = src + ch * bps;
    switch (fmt) {
    case SND_PCM_FORMAT_S16_LE: {
        int16_t v; std::memcpy(&v, p, 2); return v;
    }
    case SND_PCM_FORMAT_S32_LE: {
        int32_t v; std::memcpy(&v, p, 4); return (int16_t)(v >> 16);
    }
    case SND_PCM_FORMAT_FLOAT_LE: {
        float f; std::memcpy(&f, p, 4);
        long v = std::lround(f * 32767.0f);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        return (int16_t)v;
    }
    default: return 0;
    }
}

// ── Device name resolution ────────────────────────────────────────────────────

std::string AlsaDevice::resolve_device(const std::string& hint, bool is_output)
{
    if (hint.empty()) return "default";

    // Strip IN:/OUT: prefix emitted by list_devices()
    std::string name = hint;
    if (name.size() > 4 && name[2] == ':' && (name.substr(0,2) == "IN" ||
                                               name.substr(0,2) == "OT"))
        name = name.substr(4);
    // Strip "OUT: " or "IN:  " (5-char prefix with trailing space)
    if (name.size() > 5 && (name.substr(0,5) == "OUT: " ||
                             name.substr(0,5) == "IN:  "))
        name = name.substr(5);

    snd_pcm_stream_t dir = is_output ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

    // Try direct open first
    snd_pcm_t* probe = nullptr;
    if (snd_pcm_open(&probe, name.c_str(), dir, SND_PCM_NONBLOCK) == 0) {
        snd_pcm_close(probe);
        return name;
    }

    // Fallback: search hint list for substring match in description
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) return "default";

    std::string found;
    for (void** h = hints; *h && found.empty(); ++h) {
        char* n = snd_device_name_get_hint(*h, "NAME");
        char* d = snd_device_name_get_hint(*h, "DESC");
        if (d && std::strstr(d, name.c_str()) && n)
            found = n;
        if (n) free(n);
        if (d) free(d);
    }
    snd_device_name_free_hint(hints);
    return found.empty() ? "default" : found;
}

// ── open_pcm ─────────────────────────────────────────────────────────────────

bool AlsaDevice::open_pcm(snd_pcm_t**      pcm_out,
                           const std::string& dev,
                           snd_pcm_stream_t   dir,
                           snd_pcm_format_t&  fmt,
                           unsigned int&      rate,
                           unsigned int&      chans)
{
    int rc = snd_pcm_open(pcm_out, dev.c_str(), dir, 0);
    if (rc < 0) {
        std::fprintf(stderr, "[alsa] open '%s': %s\n", dev.c_str(), snd_strerror(rc));
        return false;
    }

    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(*pcm_out, hw);

    fmt = negotiate_format(*pcm_out, hw);
    if ((rc = snd_pcm_hw_params_set_format(*pcm_out, hw, fmt)) < 0) {
        std::fprintf(stderr, "[alsa] set_format: %s\n", snd_strerror(rc));
        goto fail;
    }
    if ((rc = snd_pcm_hw_params_set_access(*pcm_out, hw,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        std::fprintf(stderr, "[alsa] set_access: %s\n", snd_strerror(rc));
        goto fail;
    }

    chans = 1;
    snd_pcm_hw_params_set_channels_near(*pcm_out, hw, &chans);

    rate = 48000;
    snd_pcm_hw_params_set_rate_near(*pcm_out, hw, &rate, nullptr);

    {
        snd_pcm_uframes_t buf_fr = (snd_pcm_uframes_t)(rate * 200 / 1000);
        snd_pcm_uframes_t per_fr = (snd_pcm_uframes_t)(rate *  50 / 1000);
        snd_pcm_hw_params_set_buffer_size_near(*pcm_out, hw, &buf_fr);
        snd_pcm_hw_params_set_period_size_near(*pcm_out, hw, &per_fr, nullptr);
    }

    if ((rc = snd_pcm_hw_params(*pcm_out, hw)) < 0) {
        std::fprintf(stderr, "[alsa] hw_params: %s\n", snd_strerror(rc));
        goto fail;
    }

    snd_pcm_hw_params_get_period_size(hw, &period_frames_, nullptr);
    return true;

fail:
    snd_pcm_close(*pcm_out);
    *pcm_out = nullptr;
    return false;
}

// ── open ─────────────────────────────────────────────────────────────────────

bool AlsaDevice::open(const std::string& rx_device, const std::string& tx_device)
{
    if (open_) close();

    const std::string play_dev = resolve_device(tx_device, true);
    const std::string cap_dev  = resolve_device(rx_device, false);

    if (!open_pcm(&play_pcm_, play_dev, SND_PCM_STREAM_PLAYBACK,
                  play_fmt_, play_rate_, play_chans_))
        return false;

    if (!open_pcm(&cap_pcm_, cap_dev, SND_PCM_STREAM_CAPTURE,
                  cap_fmt_, cap_rate_, cap_chans_)) {
        std::fprintf(stderr, "[alsa] capture unavailable — TX only.\n");
    }

    at_tx_resampler_ = std::make_unique<Resampler>(8000, (int)play_rate_);
    if (cap_pcm_)
        rx_resampler_ = std::make_unique<Resampler>((int)cap_rate_, 8000);

    at_pcm_8k_.reserve(SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL);
    at_pcm_filt_.reserve(SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL);
    at_tx_filter_.reset();

    // Worst-case scratch: S32 (4 bytes) × max_chans × period_frames
    const int bpf = snd_pcm_format_physical_width(play_fmt_) / 8 * (int)play_chans_;
    at_write_scratch_.resize((size_t)period_frames_ * 4 * bpf);

    std::fprintf(stderr,
        "[audio] ALSA play '%s' %u Hz/%uch  |  capture '%s' %u Hz/%uch  (modem 8000 Hz)\n",
        play_dev.c_str(), play_rate_, play_chans_,
        cap_pcm_ ? cap_dev.c_str() : "none", cap_rate_, cap_chans_);

    open_          = true;
    audio_running_ = true;
    audio_thread_  = std::thread(&AlsaDevice::audio_loop, this);
    return true;
}

// ── close ─────────────────────────────────────────────────────────────────────

void AlsaDevice::close()
{
    if (!open_) return;
    audio_running_ = false;
    if (audio_thread_.joinable()) audio_thread_.join();

    if (play_pcm_) { snd_pcm_drain(play_pcm_); snd_pcm_close(play_pcm_); play_pcm_ = nullptr; }
    if (cap_pcm_)  { snd_pcm_drop(cap_pcm_);   snd_pcm_close(cap_pcm_);  cap_pcm_  = nullptr; }

    at_tx_resampler_.reset();
    rx_resampler_.reset();

    at_render_buf_.clear();
    at_render_pos_    = 0;
    at_frame_pending_ = false;

    frames_rendered_.store(0, std::memory_order_relaxed);
    frames_armed_  = 0;
    frame_notify_queue_.clear();
    total_written_ = 0;
    word_play_targets_.clear();

    { std::lock_guard<std::mutex> lk(sym_src_mtx_); sym_pull_ = nullptr; pcm_pull_ = nullptr; }
    { std::lock_guard<std::mutex> lk(rx_mtx_);      rx_queue_.clear(); }

    open_ = false;
}

// ── set_symbol_source ─────────────────────────────────────────────────────────

void AlsaDevice::set_symbol_source(std::function<bool(uint8_t*)> fn)
{
    std::lock_guard<std::mutex> lk(sym_src_mtx_);
    sym_pull_ = std::move(fn);
}

// ── set_pcm_source ───────────────────────────────────────────────────────────

void AlsaDevice::set_pcm_source(std::function<size_t(int16_t*, size_t)> fn)
{
    std::lock_guard<std::mutex> lk(sym_src_mtx_);
    pcm_pull_ = std::move(fn);
}

// ── arm_frame_complete ────────────────────────────────────────────────────────

void AlsaDevice::arm_frame_complete(std::function<void()> cb)
{
    ++frames_armed_;
    frame_notify_queue_.push_back({ frames_armed_, std::move(cb) });
}

// ── tick ──────────────────────────────────────────────────────────────────────

void AlsaDevice::tick(std::vector<int16_t>& rx_out)
{
    if (!open_) return;

    {
        std::lock_guard<std::mutex> lk(rx_mtx_);
        if (!rx_queue_.empty()) {
            rx_out.insert(rx_out.end(), rx_queue_.begin(), rx_queue_.end());
            rx_queue_.clear();
        }
    }

    const uint64_t rendered = frames_rendered_.load(std::memory_order_acquire);
    while (!frame_notify_queue_.empty() && rendered >= frame_notify_queue_.front().target) {
        frame_notify_queue_.front().cb();
        frame_notify_queue_.pop_front();
    }
}

// ── list_devices ──────────────────────────────────────────────────────────────

std::vector<std::string> AlsaDevice::list_devices() const
{
    std::vector<std::string> result;
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) return result;

    for (void** h = hints; *h; ++h) {
        char* name = snd_device_name_get_hint(*h, "NAME");
        char* desc = snd_device_name_get_hint(*h, "DESC");
        char* ioid = snd_device_name_get_hint(*h, "IOID");

        if (name) {
            std::string n(name);
            if (n != "null" && n != "default" && n.substr(0,3) != "sys") {
                bool is_in  = !ioid || std::strcmp(ioid, "Input")  == 0;
                bool is_out = !ioid || std::strcmp(ioid, "Output") == 0;
                std::string d = desc ? desc : "";
                for (auto& c : d) if (c == '\n') c = ' ';
                if (is_out) result.push_back("OUT: " + n + " — " + d);
                if (is_in)  result.push_back("IN:  " + n + " — " + d);
            }
        }
        if (name) free(name);
        if (desc) free(desc);
        if (ioid) free(ioid);
    }
    snd_device_name_free_hint(hints);
    return result;
}

// ── audio_loop ────────────────────────────────────────────────────────────────

void AlsaDevice::audio_loop()
{
    struct sched_param sp{};
    sp.sched_priority = 60;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);  // best-effort

    if (cap_pcm_) snd_pcm_start(cap_pcm_);

    while (audio_running_.load(std::memory_order_relaxed)) {
        int rc = snd_pcm_wait(play_pcm_, 10);  // 10 ms timeout = loop clock
        if (rc < 0) {
            rc = snd_pcm_recover(play_pcm_, rc, 1);
            if (rc < 0) {
                std::fprintf(stderr, "[alsa] render unrecoverable: %s\n", snd_strerror(rc));
                break;
            }
        }
        service_render();
        if (cap_pcm_) service_capture();
    }
}

// ── service_render ────────────────────────────────────────────────────────────
//
// Mirrors WASAPI service_render(): drain at_render_buf_ frame-by-frame, pull
// new symbol frames on demand, write device-format samples via snd_pcm_writei.
// Completion targets are pre-computed as (total_written_ + batch_offset + 1)
// before writei so they remain valid after partial writes.

void AlsaDevice::service_render()
{
    // Fire completions for words the DAC has consumed
    snd_pcm_sframes_t delay = 0;
    snd_pcm_delay(play_pcm_, &delay);
    if (delay < 0) delay = 0;

    const uint64_t played = total_written_ - (uint64_t)delay;
    while (!word_play_targets_.empty() && word_play_targets_.front() <= played) {
        word_play_targets_.pop_front();
        frames_rendered_.fetch_add(1, std::memory_order_release);
    }

    snd_pcm_sframes_t avail = snd_pcm_avail_update(play_pcm_);
    if (avail <= 0) return;

    // Limit to avoid oversized allocations when the hardware ring is large
    const snd_pcm_sframes_t max_fill = (snd_pcm_sframes_t)period_frames_ * 4;
    if (avail > max_fill) avail = max_fill;

    const int bpf = play_bpf();
    at_write_scratch_.resize((size_t)avail * bpf);

    // Snapshot sym_pull_ / pcm_pull_ once for this batch
    std::function<bool(uint8_t*)>              pull;
    std::function<size_t(int16_t*, size_t)>    pcm_pull;
    { std::lock_guard<std::mutex> lk(sym_src_mtx_); pull = sym_pull_; pcm_pull = pcm_pull_; }

    uint8_t syms[SYMBOLS_PER_WORD];

    // Fill scratch buffer frame-by-frame, mirroring WASAPI's per-frame loop.
    // word_play_targets_ entries are pre-computed here as total_written_ + (i+1)
    // so they stay correct regardless of whether writei writes all frames at once.
    for (snd_pcm_sframes_t i = 0; i < avail; ) {
        if (at_render_pos_ >= at_render_buf_.size()) {
            if (pcm_pull) {
                // Transparent-voice passthrough: raw 8 kHz PCM → resampler →
                // device, no ToneGenerator/band-pass, no completion accounting.
                constexpr size_t PCM_WANT = 160;  // 20 ms @ 8 kHz
                at_pcm_8k_.resize(PCM_WANT);
                const size_t got = pcm_pull(at_pcm_8k_.data(), PCM_WANT);
                if (got > 0) {
                    at_render_buf_.clear();
                    at_tx_resampler_->process(at_pcm_8k_.data(), got, at_render_buf_);
                    at_render_pos_ = 0;
                }
                if (got == 0 || at_render_buf_.empty()) {
                    std::memset(at_write_scratch_.data() + i * bpf, 0,
                                (size_t)(avail - i) * bpf);
                    i = avail;
                    break;
                }
            } else {
                bool pulled = pull && pull(syms);
                if (pulled) {
                    at_pcm_8k_.resize(SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL);
                    at_tone_gen_.generate_symbols(syms, SYMBOLS_PER_WORD,
                                                  at_pcm_8k_.data(),
                                                  tx_volume_.load(std::memory_order_relaxed));
                    at_pcm_filt_.clear();
                    at_tx_filter_.process(at_pcm_8k_.data(),
                                          SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL,
                                          at_pcm_filt_);
                    at_render_buf_.clear();
                    at_tx_resampler_->process(at_pcm_filt_.data(),
                                              at_pcm_filt_.size(),
                                              at_render_buf_);
                    at_render_pos_    = 0;
                    at_frame_pending_ = true;
                } else {
                    // Silence fill for remainder of batch
                    std::memset(at_write_scratch_.data() + i * bpf, 0,
                                (size_t)(avail - i) * bpf);
                    i = avail;
                    break;
                }
            }
        }

        s16_to_alsa(at_render_buf_[at_render_pos_++],
                    at_write_scratch_.data() + i * bpf);
        ++i;

        if (at_frame_pending_ && at_render_pos_ >= at_render_buf_.size()) {
            // total_written_ + i is the absolute position after this frame is written
            word_play_targets_.push_back(total_written_ + (uint64_t)i);
            at_frame_pending_ = false;
        }
    }

    // Write batch to hardware (retry on partial write / EAGAIN / xrun)
    snd_pcm_sframes_t remaining = avail;
    const uint8_t*    ptr       = at_write_scratch_.data();

    while (remaining > 0) {
        snd_pcm_sframes_t w = snd_pcm_writei(play_pcm_, ptr, (snd_pcm_uframes_t)remaining);
        if (w == -EAGAIN) continue;
        if (w < 0) {
            if (snd_pcm_recover(play_pcm_, (int)w, 1) < 0) return;
            continue;
        }
        total_written_ += (uint64_t)w;
        ptr             += (size_t)w * bpf;
        remaining       -= w;
    }
}

// ── service_capture ───────────────────────────────────────────────────────────

void AlsaDevice::service_capture()
{
    snd_pcm_sframes_t avail = snd_pcm_avail_update(cap_pcm_);
    if (avail <= 0) return;

    const int  bpf    = cap_bpf();
    const int  nfr    = (int)std::min(avail, (snd_pcm_sframes_t)period_frames_ * 4);
    at_cap_scratch_.resize(nfr);

    std::vector<uint8_t> raw((size_t)nfr * bpf);
    snd_pcm_sframes_t rd = snd_pcm_readi(cap_pcm_, raw.data(), nfr);
    if (rd < 0) {
        int rc = snd_pcm_recover(cap_pcm_, (int)rd, 1);
        if (rc == 0) snd_pcm_start(cap_pcm_);
        return;
    }

    // Mono-mix all channels to int16_t
    at_cap_scratch_.resize((size_t)rd);
    for (int i = 0; i < (int)rd; ++i) {
        int32_t sum = 0;
        for (unsigned int c = 0; c < cap_chans_; ++c)
            sum += alsa_to_s16(raw.data() + i * bpf, c, cap_fmt_, cap_chans_);
        sum /= (int32_t)cap_chans_;
        if (sum >  32767) sum =  32767;
        if (sum < -32768) sum = -32768;
        at_cap_scratch_[i] = (int16_t)sum;
    }

    at_rx_8k_.clear();
    rx_resampler_->process(at_cap_scratch_.data(),
                           at_cap_scratch_.size(),
                           at_rx_8k_);

    if (at_rx_8k_.empty()) return;

    std::lock_guard<std::mutex> lk(rx_mtx_);
    rx_queue_.insert(rx_queue_.end(), at_rx_8k_.begin(), at_rx_8k_.end());
}

} // namespace ale

namespace pal {
std::unique_ptr<IAudioDriver> create_audio_driver()
{
    return std::make_unique<ale::AlsaDevice>();
}
} // namespace pal
