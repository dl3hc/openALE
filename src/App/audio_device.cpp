/**
 * \file App/audio_device.cpp
 * \brief AudioDevice platform implementations.
 *
 * Windows: WASAPI (shared mode) render + capture, polled from the main loop.
 *
 *   Why WASAPI and not WinMM: WinMM forces a wave format on the device and a
 *   forced 8 kHz (or even 48 kHz) PCM format is frequently refused or silently
 *   dropped by modern endpoints and virtual cables, so nothing is heard.
 *   WASAPI shared mode instead adopts the device's *mix format* (its native
 *   rate / channel count / sample type) as authoritative; the audio engine
 *   does the rest.  The 8 kHz ALE modem is bridged to that native rate by a
 *   polyphase Resampler on each direction:
 *
 *     TX:  8 kHz modem PCM  ──Resampler──▶  render mix-rate  ──▶ device
 *     RX:  device capture   ──Resampler──▶  8 kHz pipeline PCM
 *
 *   The driver is *polled*: tick() drains the capture endpoint and tops up the
 *   render endpoint.  This keeps the single-threaded controller model intact —
 *   no audio runs on a separate real-time thread.
 *
 * Other: NullDevice — compiles but does no I/O.  Useful for offline tests.
 */

#include "App/audio_device.h"
#include "App/resampler.h"
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
#include <memory>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace ale {

// The ALE modem and pipeline run at this fixed rate; the device runs faster.
static constexpr uint32_t MODEM_RATE = AudioDevice::SAMPLE_RATE;   // 8 kHz

// Requested shared-mode buffer duration (REFERENCE_TIME units = 100 ns).
// 200 ms of headroom is ample for ~1 ms main-loop polling.
static constexpr REFERENCE_TIME REQ_BUFFER_HNS = 2000000;          // 200 ms

// ── UTF-16 → UTF-8 ────────────────────────────────────────────────────────────
static std::string to_utf8(const wchar_t* w) {
    if (!w) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return {};
    std::string s(static_cast<size_t>(sz - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], sz, nullptr, nullptr);
    return s;
}

// ── Device friendly name ──────────────────────────────────────────────────────
static std::string friendly_name(IMMDevice* dev) {
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

// ── True if the device mix format delivers 32-bit IEEE float samples ──────────
static bool format_is_float(const WAVEFORMATEX* f) {
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
class WasapiDevice : public AudioDevice {
public:
    ~WasapiDevice() override { close(); }

    // ── AudioDevice interface ─────────────────────────────────────────────

    bool open(const std::string& in_device  = "",
              const std::string& out_device = "") override {
        if (open_) return true;

        // When out_device is empty, use the same substring for both directions.
        const std::string out_name = out_device.empty() ? in_device : out_device;

        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
        com_init_ = true;

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&enum_))) {
            close();
            return false;
        }

        if (!open_render(out_name))  { close(); return false; }   // TX / output
        if (!open_capture(in_device)){ close(); return false; }   // RX / input

        tx_resampler_ = std::make_unique<Resampler>(MODEM_RATE, r_rate_);
        rx_resampler_ = std::make_unique<Resampler>(c_rate_, MODEM_RATE);

        // Prime the render buffer with silence, then start both endpoints.
        refill_render();
        r_client_->Start();
        c_client_->Start();

        std::fprintf(stderr,
            "[audio] WASAPI render '%s' %u Hz/%uch %s  |  capture '%s' %u Hz/%uch %s  (modem %u Hz)\n",
            r_name_.c_str(), r_rate_, r_ch_, r_float_ ? "f32" : "pcm",
            c_name_.c_str(), c_rate_, c_ch_, c_float_ ? "f32" : "pcm",
            MODEM_RATE);

        open_ = true;
        return true;
    }

    void close() override {
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

        tx_resampler_.reset();
        rx_resampler_.reset();
        out_queue_.clear();

        if (com_init_) { CoUninitialize(); com_init_ = false; }
        open_ = false;
    }

    void write_tx(const int16_t* samples, uint32_t count) override {
        // Modem PCM is 8 kHz; resample up to the render rate before queueing.
        std::lock_guard<std::mutex> lock(out_mtx_);
        tx_scratch_.clear();
        tx_resampler_->process(samples, count, tx_scratch_);
        out_queue_.insert(out_queue_.end(), tx_scratch_.begin(), tx_scratch_.end());
    }

    void tick(std::vector<int16_t>& rx_out) override {
        if (!open_) return;
        drain_capture(rx_out);
        refill_render();
    }

    bool is_open() const override { return open_; }

    std::vector<std::string> list_devices() const override {
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

private:
    // ── Render (output) endpoint setup ────────────────────────────────────
    bool open_render(const std::string& name) {
        r_dev_ = resolve_device(eRender, name);
        if (!r_dev_) {
            std::fprintf(stderr, "[audio] render device not found: '%s'\n", name.c_str());
            return false;
        }
        r_name_ = friendly_name(r_dev_);

        if (FAILED(r_dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    (void**)&r_client_)))                 return false;
        if (FAILED(r_client_->GetMixFormat(&r_fmt_)))                     return false;

        r_rate_  = r_fmt_->nSamplesPerSec;
        r_ch_    = r_fmt_->nChannels;
        r_bits_  = r_fmt_->wBitsPerSample;
        r_float_ = format_is_float(r_fmt_);

        if (FAILED(r_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                         REQ_BUFFER_HNS, 0, r_fmt_, nullptr))) return false;
        if (FAILED(r_client_->GetBufferSize(&r_buf_frames_)))             return false;
        if (FAILED(r_client_->GetService(__uuidof(IAudioRenderClient),
                                         (void**)&r_svc_)))               return false;
        return true;
    }

    // ── Capture (input) endpoint setup ────────────────────────────────────
    bool open_capture(const std::string& name) {
        c_dev_ = resolve_device(eCapture, name);
        if (!c_dev_) {
            std::fprintf(stderr, "[audio] capture device not found: '%s'\n", name.c_str());
            return false;
        }
        c_name_ = friendly_name(c_dev_);

        if (FAILED(c_dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    (void**)&c_client_)))                 return false;
        if (FAILED(c_client_->GetMixFormat(&c_fmt_)))                     return false;

        c_rate_  = c_fmt_->nSamplesPerSec;
        c_ch_    = c_fmt_->nChannels;
        c_bits_  = c_fmt_->wBitsPerSample;
        c_float_ = format_is_float(c_fmt_);

        if (FAILED(c_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                         REQ_BUFFER_HNS, 0, c_fmt_, nullptr))) return false;
        if (FAILED(c_client_->GetService(__uuidof(IAudioCaptureClient),
                                         (void**)&c_svc_)))               return false;
        return true;
    }

    // ── Resolve an endpoint by friendly-name substring ("" = default) ─────
    IMMDevice* resolve_device(EDataFlow flow, const std::string& sub) const {
        if (sub.empty()) {
            IMMDevice* d = nullptr;
            if (FAILED(enum_->GetDefaultAudioEndpoint(flow, eConsole, &d))) return nullptr;
            return d;
        }
        IMMDeviceCollection* coll = nullptr;
        if (FAILED(enum_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
            return nullptr;

        UINT count = 0;
        coll->GetCount(&count);
        IMMDevice* found = nullptr;
        for (UINT i = 0; i < count && !found; ++i) {
            IMMDevice* cand = nullptr;
            if (FAILED(coll->Item(i, &cand))) continue;
            if (friendly_name(cand).find(sub) != std::string::npos) {
                found = cand;
                found->AddRef();
            }
            cand->Release();
        }
        coll->Release();
        return found;
    }

    static void list_flow(IMMDeviceEnumerator* en, EDataFlow flow,
                          const char* prefix, std::vector<std::string>& out) {
        IMMDeviceCollection* coll = nullptr;
        if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) return;
        UINT count = 0;
        coll->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(coll->Item(i, &dev))) {
                out.push_back(std::string(prefix) + friendly_name(dev));
                dev->Release();
            }
        }
        coll->Release();
    }

    // ── Output: write one buffer's worth from the TX queue (silence-padded) ─
    void refill_render() {
        UINT32 padding = 0;
        if (FAILED(r_client_->GetCurrentPadding(&padding))) return;

        const UINT32 avail = r_buf_frames_ - padding;
        if (avail == 0) return;

        BYTE* data = nullptr;
        if (FAILED(r_svc_->GetBuffer(avail, &data)) || !data) return;

        {
            std::lock_guard<std::mutex> lock(out_mtx_);
            for (UINT32 i = 0; i < avail; ++i) {
                int16_t s = 0;
                if (!out_queue_.empty()) { s = out_queue_.front(); out_queue_.pop_front(); }
                write_frame(data, i, s);
            }
        }
        r_svc_->ReleaseBuffer(avail, 0);
    }

    // Write one mono sample into device-format frame i (all channels).
    void write_frame(BYTE* data, UINT32 i, int16_t s) const {
        if (r_float_) {
            float* dst = reinterpret_cast<float*>(data) + i * r_ch_;
            const float f = s / 32768.0f;
            for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = f;
        } else if (r_bits_ == 32) {
            int32_t* dst = reinterpret_cast<int32_t*>(data) + i * r_ch_;
            const int32_t v = static_cast<int32_t>(s) << 16;
            for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = v;
        } else { // 16-bit PCM
            int16_t* dst = reinterpret_cast<int16_t*>(data) + i * r_ch_;
            for (uint16_t ch = 0; ch < r_ch_; ++ch) dst[ch] = s;
        }
    }

    // ── Input: drain all queued capture packets, downmix to 8 kHz mono ────
    void drain_capture(std::vector<int16_t>& rx_out) {
        UINT32 packet = 0;
        if (FAILED(c_svc_->GetNextPacketSize(&packet))) return;

        cap_scratch_.clear();
        while (packet > 0) {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(c_svc_->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for (UINT32 i = 0; i < frames; ++i)
                cap_scratch_.push_back((silent || !data) ? int16_t(0)
                                                         : read_frame_mono(data, i));

            c_svc_->ReleaseBuffer(frames);
            if (FAILED(c_svc_->GetNextPacketSize(&packet))) break;
        }

        if (!cap_scratch_.empty())
            rx_resampler_->process(cap_scratch_.data(), cap_scratch_.size(), rx_out);
    }

    // Read device-format frame i, average channels, return mono int16.
    int16_t read_frame_mono(const BYTE* data, UINT32 i) const {
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
        } else { // 16-bit PCM
            const int16_t* src = reinterpret_cast<const int16_t*>(data) + i * c_ch_;
            int acc = 0;
            for (uint16_t ch = 0; ch < c_ch_; ++ch) acc += src[ch];
            return static_cast<int16_t>(acc / c_ch_);
        }
    }

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

    // Resamplers across the 8 kHz ↔ device-rate boundary
    std::unique_ptr<Resampler> tx_resampler_;   // 8 kHz   → render rate
    std::unique_ptr<Resampler> rx_resampler_;   // capture → 8 kHz

    std::mutex           out_mtx_;
    std::deque<int16_t>  out_queue_;     // render-rate mono samples awaiting output
    std::vector<int16_t> tx_scratch_;
    std::vector<int16_t> cap_scratch_;   // capture-rate mono samples
};

std::unique_ptr<AudioDevice> make_audio_device() {
    return std::make_unique<WasapiDevice>();
}

} // namespace ale

// ─────────────────────────────────────────────────────────────────────────────
#else  // non-Windows stub
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <memory>

namespace ale {

class NullDevice : public AudioDevice {
    bool open_ = false;
public:
    bool open(const std::string& = "", const std::string& = "") override {
        std::fprintf(stderr, "[audio] NullDevice — no real-time audio on this platform.\n");
        open_ = true;
        return true;
    }
    void close() override               { open_ = false; }
    void write_tx(const int16_t*, uint32_t) override {}
    void tick(std::vector<int16_t>&) override {}
    bool is_open() const override        { return open_; }
    std::vector<std::string> list_devices() const override { return {}; }
};

std::unique_ptr<AudioDevice> make_audio_device() {
    return std::make_unique<NullDevice>();
}

} // namespace ale

#endif
