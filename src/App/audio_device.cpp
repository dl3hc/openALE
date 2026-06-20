/**
 * \file App/audio_device.cpp
 * \brief AudioDevice platform implementations.
 *
 * Windows: WASAPI shared-mode with event-driven callbacks.
 *
 *   Architecture (pull model)
 *   ──────────────────────────
 *   A dedicated high-priority audio thread waits on three Win32 events:
 *     render_event_  — WASAPI signals: render buffer has space for more data
 *     capture_event_ — WASAPI signals: capture buffer has new data
 *     stop_event_    — close() signals: audio thread must exit
 *
 *   TX data flow:
 *     audio thread → sym_pull_(out_49) → [modem's pull_symbol_frame()]
 *     audio thread → at_tone_gen_.generate_symbols() → 8 kHz PCM
 *     audio thread → at_tx_resampler_->process()     → device-rate PCM
 *     audio thread → WASAPI render buffer
 *
 *   RX data flow:
 *     WASAPI → audio thread → at_rx_resampler_ → [rx_mtx_ / rx_queue_]
 *     main thread → tick() → rx_out
 *
 *   Frame completion:
 *     Audio thread increments frames_rendered_ (atomic) when the last
 *     device-rate sample of a symbol frame has been handed to WASAPI.
 *     Main thread tick() fires armed callbacks from frame_notify_queue_
 *     when frames_rendered_ reaches each callback's target.
 *
 * Other: NullDevice — compiles but does no I/O; simulates pull consumption
 *        from tick() for offline use.
 */

#include "PAL/audio_driver.h"
#include "App/resampler.h"
#include "FSK/tone_generator.h"
#include "FSK/tx_bandpass.h"
#include "FSK/ale_waveform.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
// ─────────────────────────────────────────────────────────────────────────────

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace ale {

static constexpr uint32_t MODEM_RATE = 8000;

// WASAPI shared-mode buffer duration: 200 ms of headroom.
static constexpr REFERENCE_TIME REQ_BUFFER_HNS = 2000000;  // 200 ms in 100-ns units

// ── UTF-16 → UTF-8 ────────────────────────────────────────────────────────────
static std::string to_utf8(const wchar_t* w)
{
    if (!w) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return {};
    std::string s(static_cast<size_t>(sz - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], sz, nullptr, nullptr);
    return s;
}

// ── Device friendly name ──────────────────────────────────────────────────────
static std::string friendly_name(IMMDevice* dev)
{
    IPropertyStore* props = nullptr;
    std::string name;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.pwszVal)
            name = to_utf8(var.pwszVal);
        PropVariantClear(&var);
        props->Release();
    }
    return name;
}

// ── Disambiguated display names for a device collection ───────────────────────
// Duplicate friendly-names (e.g. two identical USB codecs) are made unique by
// appending " (n)" to each duplicate occurrence in enumeration order; unique
// names are left bare for backward compatibility. The SAME logic feeds both
// list_flow() (what the GUI shows and sends back) and resolve_device() (what
// open() matches), so a selected name always maps to exactly one device.
static std::vector<std::string> annotated_device_names(IMMDeviceCollection* coll)
{
    UINT count = 0;
    coll->GetCount(&count);
    std::vector<std::string> base(count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* d = nullptr;
        if (SUCCEEDED(coll->Item(i, &d))) { base[i] = friendly_name(d); d->Release(); }
    }
    std::vector<std::string> out(count);
    for (UINT i = 0; i < count; ++i) {
        int total = 0, occ = 0;
        for (UINT j = 0; j < count; ++j)
            if (base[j] == base[i]) { ++total; if (j <= i) ++occ; }
        out[i] = (total > 1) ? base[i] + " (" + std::to_string(occ) + ")" : base[i];
    }
    return out;
}

// ── True if the device mix format delivers 32-bit IEEE float samples ──────────
static bool format_is_float(const WAVEFORMATEX* f)
{
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
class WasapiDevice : public pal::IAudioDriver {
public:
    ~WasapiDevice() override { close(); }

    // ── AudioDevice interface ─────────────────────────────────────────────

    bool open(const std::string& in_device  = "",
              const std::string& out_device = "") override;

    void close() override;

    void set_symbol_source(std::function<bool(uint8_t*)> fn) override;
    void arm_frame_complete(std::function<void()> cb) override;

    // Drain accumulated 8 kHz RX samples; fire pending frame completions.
    void tick(std::vector<int16_t>& rx_out) override;

    bool is_open() const override { return open_; }

    std::vector<std::string> list_devices() const override;

private:
    // ── Audio thread ──────────────────────────────────────────────────────
    std::thread          audio_thread_;
    std::atomic<bool>    audio_running_{false};
    HANDLE               render_event_  = nullptr;
    HANDLE               capture_event_ = nullptr;
    HANDLE               stop_event_    = nullptr;

    void audio_loop();
    void service_render();    // called from audio thread on render event
    void service_capture();   // called from audio thread on capture event

    // ── Symbol source (set from main thread, read from audio thread) ──────
    // Protected by sym_src_mtx_: set_symbol_source() may be called after
    // open() while the audio thread is already running.
    std::mutex                       sym_src_mtx_;
    std::function<bool(uint8_t*)>    sym_pull_;

    // ── Audio-thread-only TX state (no locking needed) ────────────────────
    ToneGenerator             at_tone_gen_;
    TxBandpass                at_tx_filter_;        // 750–2500 Hz band-pass (SSB-audio shaping)
    std::unique_ptr<Resampler> at_tx_resampler_;   // 8 kHz → device rate
    std::vector<int16_t>      at_pcm_8k_;          // ToneGenerator output (3136 samples)
    std::vector<int16_t>      at_pcm_filt_;         // band-pass output (8 kHz, fed to resampler)
    std::vector<int16_t>      at_render_buf_;       // device-rate PCM for current word
    size_t                    at_render_pos_  = 0;  // drain position in at_render_buf_
    bool                      at_frame_pending_ = false; // true when at_render_buf_ holds a real frame

    // ── Audio-thread-only RX scratch ─────────────────────────────────────
    std::vector<int16_t> at_cap_scratch_;
    std::vector<int16_t> at_rx_8k_;
    std::unique_ptr<Resampler> rx_resampler_;      // capture rate → 8 kHz (audio thread only)

    // ── Frame completion tracking ─────────────────────────────────────────
    // frames_rendered_: number of symbol frames whose device-rate samples
    //   have all been handed to WASAPI ReleaseBuffer.
    //   Written by audio thread; read by main thread via tick().
    std::atomic<uint64_t>  frames_rendered_{0};

    // frame_notify_queue_ and frames_armed_: main-thread-only.
    struct FrameNotify {
        uint64_t               target;  // fire when frames_rendered_ >= target
        std::function<void()>  cb;
    };
    std::deque<FrameNotify>  frame_notify_queue_;
    uint64_t                 frames_armed_ = 0;

    // ── RX queue: audio thread produces, main thread consumes ─────────────
    std::mutex           rx_mtx_;
    std::deque<int16_t>  rx_queue_;

    // ── COM / WASAPI state ────────────────────────────────────────────────
    bool com_init_ = false;
    bool open_     = false;
    IMMDeviceEnumerator* enum_ = nullptr;

    // Render (output / TX)
    IMMDevice*          r_dev_        = nullptr;
    IAudioClient*       r_client_     = nullptr;
    IAudioRenderClient* r_svc_        = nullptr;
    WAVEFORMATEX*       r_fmt_        = nullptr;
    UINT32              r_buf_frames_ = 0;
    uint32_t            r_rate_       = MODEM_RATE;
    uint16_t            r_ch_         = 1;
    uint16_t            r_bits_       = 16;
    bool                r_float_      = false;
    std::string         r_name_;

    // Capture (input / RX)
    IMMDevice*           c_dev_    = nullptr;
    IAudioClient*        c_client_ = nullptr;
    IAudioCaptureClient* c_svc_    = nullptr;
    WAVEFORMATEX*        c_fmt_    = nullptr;
    uint32_t             c_rate_   = MODEM_RATE;
    uint16_t             c_ch_     = 1;
    uint16_t             c_bits_   = 16;
    bool                 c_float_  = false;
    std::string          c_name_;

    // ── Helpers ───────────────────────────────────────────────────────────
    bool open_render(const std::string& name);
    bool open_capture(const std::string& name);
    IMMDevice* resolve_device(EDataFlow flow, const std::string& sub) const;

    static void list_flow(IMMDeviceEnumerator* en, EDataFlow flow,
                          const char* prefix, std::vector<std::string>& out);

    void write_frame(BYTE* data, UINT32 i, int16_t s) const;
    int16_t read_frame_mono(const BYTE* data, UINT32 i) const;
};

// ── open ─────────────────────────────────────────────────────────────────────

bool WasapiDevice::open(const std::string& in_device, const std::string& out_device)
{
    if (open_) return true;

    const std::string out_name = out_device.empty() ? in_device : out_device;

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
    com_init_ = true;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&enum_))) {
        close(); return false;
    }

    render_event_  = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    capture_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    stop_event_    = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!render_event_ || !capture_event_ || !stop_event_) { close(); return false; }

    if (!open_render(out_name))  { close(); return false; }
    if (!open_capture(in_device)){ close(); return false; }

    // Both resamplers are audio-thread-only — created here (before thread starts)
    // so the thread start provides the happens-before edge.
    at_tx_resampler_ = std::make_unique<Resampler>(MODEM_RATE, r_rate_);
    rx_resampler_    = std::make_unique<Resampler>(c_rate_, MODEM_RATE);

    // Pre-reserve audio-thread scratch buffers to avoid RT allocations.
    at_pcm_8k_.reserve(SYMBOLS_PER_WORD * FFT_SIZE);
    at_pcm_filt_.reserve(SYMBOLS_PER_WORD * FFT_SIZE);
    at_tx_filter_.reset();

    std::fprintf(stderr,
        "[audio] WASAPI render '%s' %u Hz/%uch %s  |  capture '%s' %u Hz/%uch %s  (modem %u Hz)\n",
        r_name_.c_str(), r_rate_, r_ch_, r_float_ ? "f32" : "pcm",
        c_name_.c_str(), c_rate_, c_ch_, c_float_ ? "f32" : "pcm",
        MODEM_RATE);

    r_client_->Start();
    c_client_->Start();

    audio_running_ = true;
    audio_thread_  = std::thread(&WasapiDevice::audio_loop, this);

    open_ = true;
    return true;
}

// ── close ────────────────────────────────────────────────────────────────────

void WasapiDevice::close()
{
    if (audio_running_) {
        audio_running_ = false;
        if (stop_event_) SetEvent(stop_event_);
        if (audio_thread_.joinable()) audio_thread_.join();
    }

    if (r_client_) r_client_->Stop();
    if (c_client_) c_client_->Stop();

    if (r_svc_)     { r_svc_->Release();     r_svc_     = nullptr; }
    if (c_svc_)     { c_svc_->Release();     c_svc_     = nullptr; }
    if (r_client_)  { r_client_->Release();  r_client_  = nullptr; }
    if (c_client_)  { c_client_->Release();  c_client_  = nullptr; }
    if (r_dev_)     { r_dev_->Release();     r_dev_     = nullptr; }
    if (c_dev_)     { c_dev_->Release();     c_dev_     = nullptr; }
    if (r_fmt_)     { CoTaskMemFree(r_fmt_); r_fmt_     = nullptr; }
    if (c_fmt_)     { CoTaskMemFree(c_fmt_); c_fmt_     = nullptr; }
    if (enum_)      { enum_->Release();      enum_      = nullptr; }

    if (render_event_)  { CloseHandle(render_event_);  render_event_  = nullptr; }
    if (capture_event_) { CloseHandle(capture_event_); capture_event_ = nullptr; }
    if (stop_event_)    { CloseHandle(stop_event_);    stop_event_    = nullptr; }

    at_tx_resampler_.reset();
    rx_resampler_.reset();

    at_render_buf_.clear();
    at_render_pos_    = 0;
    at_frame_pending_ = false;

    frames_rendered_.store(0, std::memory_order_relaxed);
    frames_armed_ = 0;
    frame_notify_queue_.clear();

    { std::lock_guard<std::mutex> lk(sym_src_mtx_); sym_pull_ = nullptr; }
    { std::lock_guard<std::mutex> lk(rx_mtx_);      rx_queue_.clear(); }

    if (com_init_) { CoUninitialize(); com_init_ = false; }
    open_ = false;
}

// ── set_symbol_source (main thread) ──────────────────────────────────────────

void WasapiDevice::set_symbol_source(std::function<bool(uint8_t*)> fn)
{
    std::lock_guard<std::mutex> lk(sym_src_mtx_);
    sym_pull_ = std::move(fn);
}

// ── arm_frame_complete (main thread) ─────────────────────────────────────────

void WasapiDevice::arm_frame_complete(std::function<void()> cb)
{
    ++frames_armed_;
    frame_notify_queue_.push_back({ frames_armed_, std::move(cb) });
}

// ── tick (main thread) ────────────────────────────────────────────────────────

void WasapiDevice::tick(std::vector<int16_t>& rx_out)
{
    if (!open_) return;

    {
        std::lock_guard<std::mutex> lk(rx_mtx_);
        if (!rx_queue_.empty()) {
            rx_out.insert(rx_out.end(), rx_queue_.begin(), rx_queue_.end());
            rx_queue_.clear();
        }
    }

    // Fire frame-completion callbacks whose symbol frames have been rendered.
    const uint64_t rendered = frames_rendered_.load(std::memory_order_acquire);
    while (!frame_notify_queue_.empty() && rendered >= frame_notify_queue_.front().target) {
        frame_notify_queue_.front().cb();
        frame_notify_queue_.pop_front();
    }
}

// ── audio_loop (audio thread) ─────────────────────────────────────────────────

void WasapiDevice::audio_loop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    const HANDLE events[3] = { render_event_, capture_event_, stop_event_ };

    while (true) {
        const DWORD res = WaitForMultipleObjects(3, events, FALSE, INFINITE);

        if (res == WAIT_FAILED)            break;
        if (res == WAIT_OBJECT_0 + 2)      break;  // stop_event_
        if (!audio_running_)               break;

        if (res == WAIT_OBJECT_0)     service_render();
        if (res == WAIT_OBJECT_0 + 1) service_capture();
    }

    CoUninitialize();
}

// ── service_render (audio thread) ────────────────────────────────────────────
//
// Pull model: for each device-rate frame needed, drain the current render
// buffer (one resampled symbol frame worth of samples).  When the buffer
// empties, pull the next symbol frame via sym_pull_, render it with
// ToneGenerator, and resample.  Signal frame completion (frames_rendered_)
// the moment the last device-rate sample of a word is handed to WASAPI.

void WasapiDevice::service_render()
{
    UINT32 padding = 0;
    if (FAILED(r_client_->GetCurrentPadding(&padding))) return;

    const UINT32 avail = r_buf_frames_ - padding;
    if (avail == 0) return;

    BYTE* data = nullptr;
    if (FAILED(r_svc_->GetBuffer(avail, &data)) || !data) return;

    uint8_t syms[SYMBOLS_PER_WORD];

    for (UINT32 i = 0; i < avail; ) {
        // Refill render buffer when current word is exhausted
        if (at_render_pos_ >= at_render_buf_.size()) {
            bool pulled = false;
            {
                std::lock_guard<std::mutex> lk(sym_src_mtx_);
                if (sym_pull_) pulled = sym_pull_(syms);
            }

            if (pulled) {
                at_pcm_8k_.resize(SYMBOLS_PER_WORD * FFT_SIZE);
                at_tone_gen_.generate_symbols(syms, SYMBOLS_PER_WORD,
                                              at_pcm_8k_.data(), TX_AMPLITUDE);
                // SSB-audio band-pass (750–2500 Hz): strips the sub-300 Hz keying
                // skirt and out-of-band content without touching the 8-FSK keying.
                at_pcm_filt_.clear();
                at_tx_filter_.process(at_pcm_8k_.data(),
                                      SYMBOLS_PER_WORD * FFT_SIZE,
                                      at_pcm_filt_);
                at_render_buf_.clear();
                at_tx_resampler_->process(at_pcm_filt_.data(),
                                          at_pcm_filt_.size(),
                                          at_render_buf_);
                at_render_pos_    = 0;
                at_frame_pending_ = true;
            } else {
                // No symbols pending — fill remaining frames with silence
                for (; i < avail; ++i)
                    write_frame(data, i, 0);
                break;
            }
        }

        write_frame(data, i++, at_render_buf_[at_render_pos_++]);

        // When the last device-rate sample of a word has been handed to WASAPI,
        // signal completion so the main-loop tick() can fire on_word_complete().
        if (at_frame_pending_ && at_render_pos_ >= at_render_buf_.size()) {
            frames_rendered_.fetch_add(1, std::memory_order_release);
            at_frame_pending_ = false;
        }
    }

    r_svc_->ReleaseBuffer(avail, 0);
}

// ── service_capture (audio thread) ───────────────────────────────────────────

void WasapiDevice::service_capture()
{
    UINT32 packet = 0;
    if (FAILED(c_svc_->GetNextPacketSize(&packet))) return;

    at_cap_scratch_.clear();

    while (packet > 0) {
        BYTE*  data   = nullptr;
        UINT32 frames = 0;
        DWORD  flags  = 0;
        if (FAILED(c_svc_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;

        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        for (UINT32 i = 0; i < frames; ++i)
            at_cap_scratch_.push_back((silent || !data) ? int16_t(0)
                                                        : read_frame_mono(data, i));

        c_svc_->ReleaseBuffer(frames);
        if (FAILED(c_svc_->GetNextPacketSize(&packet))) break;
    }

    if (at_cap_scratch_.empty()) return;

    at_rx_8k_.clear();
    rx_resampler_->process(at_cap_scratch_.data(),
                           static_cast<uint32_t>(at_cap_scratch_.size()),
                           at_rx_8k_);

    if (at_rx_8k_.empty()) return;

    std::lock_guard<std::mutex> lk(rx_mtx_);
    rx_queue_.insert(rx_queue_.end(), at_rx_8k_.begin(), at_rx_8k_.end());
}

// ── open_render ───────────────────────────────────────────────────────────────

bool WasapiDevice::open_render(const std::string& name)
{
    r_dev_ = resolve_device(eRender, name);
    if (!r_dev_) {
        std::fprintf(stderr, "[audio] render device not found: '%s'\n", name.c_str());
        return false;
    }
    r_name_ = friendly_name(r_dev_);

    if (FAILED(r_dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                 nullptr, (void**)&r_client_)))        return false;
    if (FAILED(r_client_->GetMixFormat(&r_fmt_)))                      return false;

    r_rate_  = r_fmt_->nSamplesPerSec;
    r_ch_    = r_fmt_->nChannels;
    r_bits_  = r_fmt_->wBitsPerSample;
    r_float_ = format_is_float(r_fmt_);

    if (FAILED(r_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     REQ_BUFFER_HNS, 0, r_fmt_, nullptr))) return false;
    if (FAILED(r_client_->GetBufferSize(&r_buf_frames_)))               return false;
    if (FAILED(r_client_->SetEventHandle(render_event_)))               return false;
    if (FAILED(r_client_->GetService(__uuidof(IAudioRenderClient),
                                     (void**)&r_svc_)))                 return false;
    return true;
}

// ── open_capture ──────────────────────────────────────────────────────────────

bool WasapiDevice::open_capture(const std::string& name)
{
    c_dev_ = resolve_device(eCapture, name);
    if (!c_dev_) {
        std::fprintf(stderr, "[audio] capture device not found: '%s'\n", name.c_str());
        return false;
    }
    c_name_ = friendly_name(c_dev_);

    if (FAILED(c_dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                 nullptr, (void**)&c_client_)))         return false;
    if (FAILED(c_client_->GetMixFormat(&c_fmt_)))                       return false;

    c_rate_  = c_fmt_->nSamplesPerSec;
    c_ch_    = c_fmt_->nChannels;
    c_bits_  = c_fmt_->wBitsPerSample;
    c_float_ = format_is_float(c_fmt_);

    if (FAILED(c_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     REQ_BUFFER_HNS, 0, c_fmt_, nullptr))) return false;
    if (FAILED(c_client_->SetEventHandle(capture_event_)))               return false;
    if (FAILED(c_client_->GetService(__uuidof(IAudioCaptureClient),
                                     (void**)&c_svc_)))                  return false;
    return true;
}

// ── resolve_device ────────────────────────────────────────────────────────────

IMMDevice* WasapiDevice::resolve_device(EDataFlow flow, const std::string& sub) const
{
    if (sub.empty()) {
        IMMDevice* d = nullptr;
        if (FAILED(enum_->GetDefaultAudioEndpoint(flow, eConsole, &d))) return nullptr;
        return d;
    }
    IMMDeviceCollection* coll = nullptr;
    if (FAILED(enum_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) return nullptr;

    // Match against the same disambiguated names list_flow() exposes. Prefer an
    // exact match on the annotated name the GUI sent; fall back to the first
    // device whose name *contains* the substring (keeps bare-name selections
    // and CLI --in-device/--out-device matching working).
    const std::vector<std::string> names = annotated_device_names(coll);
    UINT count = 0;
    coll->GetCount(&count);

    int exact_idx = -1, substr_idx = -1;
    for (UINT i = 0; i < count; ++i) {
        if (names[i] == sub) { exact_idx = static_cast<int>(i); break; }
        if (substr_idx < 0 && names[i].find(sub) != std::string::npos)
            substr_idx = static_cast<int>(i);
    }
    const int pick = exact_idx >= 0 ? exact_idx : substr_idx;

    IMMDevice* found = nullptr;
    if (pick >= 0) coll->Item(static_cast<UINT>(pick), &found);  // Item() AddRefs
    coll->Release();
    return found;
}

// ── list_devices ──────────────────────────────────────────────────────────────

std::vector<std::string> WasapiDevice::list_devices() const
{
    std::vector<std::string> result;
    const bool did_init = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&en))) {
        list_flow(en, eCapture, "IN:  ", result);
        list_flow(en, eRender,  "OUT: ", result);
        en->Release();
    }
    if (did_init) CoUninitialize();
    return result;
}

void WasapiDevice::list_flow(IMMDeviceEnumerator* en, EDataFlow flow,
                              const char* prefix, std::vector<std::string>& out)
{
    // ACTIVE only by design: resolve_device()/open() can only open active
    // endpoints, so listing DISABLED/UNPLUGGED ones would just offer entries
    // the user cannot connect. Duplicate friendly-names are disambiguated with
    // a " (n)" suffix (annotated_device_names) so identically-named devices
    // stay distinguishable and selectable.
    IMMDeviceCollection* coll = nullptr;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) return;
    for (const auto& name : annotated_device_names(coll))
        out.push_back(std::string(prefix) + name);
    coll->Release();
}

// ── write_frame / read_frame_mono ─────────────────────────────────────────────

void WasapiDevice::write_frame(BYTE* data, UINT32 i, int16_t s) const
{
    if (r_float_) {
        float* dst = reinterpret_cast<float*>(data) + i * r_ch_;
        const float f = s / 32768.0f;
        for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = f;
    } else if (r_bits_ == 32) {
        int32_t* dst = reinterpret_cast<int32_t*>(data) + i * r_ch_;
        const int32_t v = static_cast<int32_t>(s) << 16;
        for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = v;
    } else {
        int16_t* dst = reinterpret_cast<int16_t*>(data) + i * r_ch_;
        for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = s;
    }
}

int16_t WasapiDevice::read_frame_mono(const BYTE* data, UINT32 i) const
{
    if (c_float_) {
        const float* src = reinterpret_cast<const float*>(data) + i * c_ch_;
        float acc = 0.0f;
        for (uint16_t ch = 0; ch < c_ch_; ++ch) acc += src[ch];
        acc /= c_ch_;
        acc = std::clamp(acc, -1.0f, 1.0f);
        return static_cast<int16_t>(acc * 32767.0f);
    } else if (c_bits_ == 32) {
        const int32_t* src = reinterpret_cast<const int32_t*>(data) + i * c_ch_;
        long long acc = 0;
        for (uint16_t ch = 0; ch < c_ch_; ++ch) acc += src[ch];
        acc /= c_ch_;
        return static_cast<int16_t>(acc >> 16);
    } else {
        const int16_t* src = reinterpret_cast<const int16_t*>(data) + i * c_ch_;
        int acc = 0;
        for (uint16_t ch = 0; ch < c_ch_; ++ch) acc += src[ch];
        return static_cast<int16_t>(acc / c_ch_);
    }
}

} // namespace ale

namespace pal {
std::unique_ptr<IAudioDriver> create_audio_driver()
{
    return std::make_unique<ale::WasapiDevice>();
}
} // namespace pal

// ─────────────────────────────────────────────────────────────────────────────
#else  // non-Windows: NullAudioDriver
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <queue>

namespace ale {

class NullDevice : public pal::IAudioDriver {
    bool open_ = false;
    std::function<bool(uint8_t*)>      sym_pull_;
    std::queue<std::function<void()>>  pending_completions_;

public:
    bool open(const std::string& = "", const std::string& = "") override {
        std::fprintf(stderr, "[audio] NullDevice — no real-time audio on this platform.\n");
        open_ = true;
        return true;
    }
    void close() override {
        open_ = false;
        sym_pull_ = nullptr;
        while (!pending_completions_.empty()) pending_completions_.pop();
    }

    void set_symbol_source(std::function<bool(uint8_t*)> fn) override {
        sym_pull_ = std::move(fn);
    }

    void arm_frame_complete(std::function<void()> cb) override {
        if (cb) pending_completions_.push(std::move(cb));
    }

    void tick(std::vector<int16_t>& /*rx_out*/) override {
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

#endif
