# Voice Audio Routing & Dynamic Audio Path Management

**Status:** Design + implementation plan (analog voice). Digital-voice extension point
defined, not yet implemented.
**Date:** 2026-07-08
**Scope:** Simultaneous ALE operation and operator voice communication over separate audio
paths, with the radio (VAC) audio path ownership switched automatically by ALE link state.

---

## 1. Problem

OpenALE today uses a **single audio device** — a Virtual Audio Cable (Windows) or loopback
(Linux) — as the exclusive bridge between the ALE modem and the radio:

- RX: `audio->tick(rx_buf)` → `ALEController::feed_audio()` → demodulator.
- TX: modulator → `IAudioDriver::set_symbol_source()` pull callback → ToneGenerator →
  TxBandpass → resampler → device → radio.
- PTT is radio CAT only (`pal::IRadio::set_ptt`). The operator's manual-PTT button keys the
  radio with **modem output**. There is **no operator-voice / microphone path.**

The requirement adds two things:

1. **Simultaneous ALE + voice** via separate audio paths — the modem keeps the VAC; the operator
   uses a physical mic/speaker, **or the mic/speaker of a connected smartphone/tablet when using
   the mobile GUI**.
2. **Dynamic ownership** of the radio (VAC) audio path, switched **automatically by ALE link
   state**: modem-exclusive while not linked; transparent bidirectional voice passthrough while
   LINKED; back to modem on link termination. PTT controls the half-duplex direction. Must
   support analog voice now and digital voice later without architectural change.

## 2. Key architectural decision

The smartphone/tablet mic/speaker is reachable **only** from the mobile GUI running in a browser
on that device. The bridge (openALE) runs on the PC and cannot access a phone's audio hardware
directly. Therefore the operator-side voice path **must be browser-mediated**: the GUI captures
mic via `getUserMedia` and plays RX via Web Audio, streaming PCM over the existing WebSocket as
binary frames. This single path unifies the desktop case (PC mic/speaker through the desktop
browser) and the mobile case (phone mic/speaker through the mobile browser).

A PC-local second WASAPI/ALSA device on the bridge side is a possible future low-latency option
for desktop, but it does **not** satisfy the smartphone requirement and is out of scope.

## 3. Constraint honoured

The modem/decoder signal-processing logic (ToneGenerator, modulator, demodulator, Goertzel,
Golay, WordGridTracker) is **not modified**. The only audio-adjacent change is in the
**transport layer** (`pal::IAudioDriver`): an optional raw-PCM TX source so the VAC device can
carry arbitrary PCM during passthrough. The decoder is simply *not fed* during passthrough — its
code is untouched.

## 4. Architecture

### 4.1 Two audio domains

| Domain | Device | Owner when not linked | Owner when LINKED |
|---|---|---|---|
| Radio side | modem `AudioDevice` = VAC (`BridgeCtx.audio`) | modem (symbol RX/TX) | transparent pipe (browser ↔ radio) |
| Operator side | **browser Web Audio** (mic `getUserMedia`, speaker `AudioWorklet`) | idle / not captured | voice PCM over WebSocket |

No second native audio device is opened by the bridge. **The browser is the operator-side
device.**

### 4.2 Audio-path ownership state machine (single authority)

Driven **solely** by ALE link state (`on_link_established` / `on_link_terminated`, already wired
in `apps/ale_bridge.cpp`).

```
                 link_established (voice armed)
  ALE_EXCLUSIVE ─────────────────────────────────▶ VOICE_PASSTHROUGH
       ▲                                                │  PTT off: radio RX → browser speaker
       │                                                │  PTT on : browser mic → radio TX (speaker muted)
       │  link_terminated (any reason)                  │
       └────────────────────────────────────────────────┘
```

- **ALE_EXCLUSIVE** (IDLE / SCANNING / CALLING / HANDSHAKE / SOUNDING, or LINKED-but-voice-not-armed):
  today's behaviour, byte-for-byte unchanged. Modem owns the VAC; `feed_audio`→demodulator;
  TX = symbol source.
- **VOICE_PASSTHROUGH** (LINKED **and** voice armed): the modem *releases* the VAC. RX from VAC
  (radio) → browser speaker. Browser mic → VAC → radio TX, **gated by PTT**. Demodulator not fed.
  `ALEController::update()` keeps running so the SM's Twa idle-timeout can terminate the link —
  which is exactly what returns ownership to the modem (the spec's link-termination rule).

**Exclusive-ownership invariant:** at every instant exactly one consumer owns the VAC. Entering
passthrough = stop `feed_audio` + set `pcm_source` (mic). Leaving = clear `pcm_source` (restores
symbol source, already registered) + resume `feed_audio`. The modem device is **never
closed/reopened** across the transition (avoids re-acquisition latency and device-loss races).

### 4.3 PTT (half-duplex direction)

HF voice is simplex. Inside VOICE_PASSTHROUGH:

| PTT | Radio | Mic → VAC TX | VAC RX → speaker |
|---|---|---|---|
| off (receive) | RX | ignored (mic not pulled) | played |
| on (transmit) | TX (CAT PTT asserted) | streamed to radio | muted |

The existing mobile PTT button (`#pttBtnMob` / `#pttBtn`) already sends `SET_PTT`; the bridge
**context-routes** it: `voice->set_ptt()` when passthrough active, `ctrl.set_manual_ptt()`
otherwise (legacy ALE manual PTT, unchanged). Voice PTT does **not** invoke ALE
`ptt_lead`/`ptt_tail` (those are ALE-modem word-framing concerns). Voice PTT activity resets the
link idle timer (Twa) so a QSO does not time out.

## 5. Components

### 5.1 `pal::IAudioDriver` — raw-PCM TX source (transport layer, additive)

`include/PAL/audio_driver.h` + `wasapi_audio.cpp` / `alsa_audio.cpp` / `null_audio.cpp`.

```cpp
// New virtual, default no-op (existing symbol path untouched when null).
// When set: the audio render thread pulls raw 8 kHz mono int16 PCM from fn
// (skipping ToneGenerator + TxBandpass), resamples to the device rate, writes to
// the render buffer. Returns samples filled; 0 ⇒ silence. Thread-safe w.r.t. the
// main thread. Passing nullptr restores the symbol-source render path.
virtual void set_pcm_source(std::function<size_t(int16_t* out, size_t want)> fn) { (void)fn; }
```

- WASAPI/ALSA: in `service_render()`, branch on `pcm_pull_` vs `sym_pull_`. The PCM path reuses
  the existing TX resampler (`at_tx_resampler_`) — **no new DSP**. It does **not** apply
  `tx_volume_` (that is a modem-level setting; voice level is controlled browser-side) and does
  **not** pass through `TxBandpass` (the 750–2500 Hz ALE bandpass is too narrow for voice; voice
  is full-band baseband for the radio's SSB modulator).
- `frames_rendered_` / word-completion accounting stays on the symbol path only (completion is an
  ALE-modem concept; the PCM path renders continuously and never arms frame completions).
- NullAudioDriver: stores `pcm_pull_` so `VoicePathManager` tests can exercise the pull path.

This is the **only** signal-adjacent change and it lives in the driver, not the modem/decoder.

### 5.2 `VoicePathManager` — new, bridge-owned

`include/App/voice_path_manager.h` + `src/App/voice_path_manager.cpp`. Added to the `ale_app_core`
CMake target. Bridge-owned, mirroring the established rule that audio/radio lifecycle is owned by
the bridge caller, not the controller.

Responsibilities:
- Hold a pointer to the modem `AudioDevice` (VAC) and to `pal::IRadio` (for PTT).
- One SPSC lock-free ring `mic_ring_` (browser mic → modem render thread). The speaker direction
  needs no ring: the main loop has `rx_buf` in hand and sends it straight to the browser.
- Mode + PTT state, enforcing the exclusive-ownership invariant.
- API:
  - `void attach(AudioDevice* vac, pal::IRadio* radio);`
  - `void arm(bool on);` — enable/disable voice capability (off ⇒ ALE_EXCLUSIVE always = legacy).
  - `void on_link_state(bool linked);` — linked && armed ⇒ VOICE_PASSTHROUGH; !linked ⇒ ALE_EXCLUSIVE.
  - `void set_ptt(bool on);` — voice PTT: `radio->set_ptt(on)`; on ⇒ `vac->set_pcm_source(pull mic_ring)`; off ⇒ `vac->set_pcm_source(silence)`. Mute speaker while on.
  - `void push_mic_pcm(const int16_t*, size_t);` — from WS voice frames (main loop). No-op unless passthrough + PTT.
  - `bool passthrough_active() const;` / `bool ptt() const;` — for the main-loop routing decision.
  - `std::function<void()> on_ptt_activity;` — bridge wires to `ctrl.reset_link_idle_timer()`.
- Entering passthrough: stop `feed_audio` (bridge-side gate), set `vac` `pcm_source`. Leaving:
  clear `pcm_source` (symbol source restored), resume `feed_audio`.

### 5.3 Bridge changes (`apps/ale_bridge.cpp`)

- `BridgeCtx`: add `VoicePathManager* voice; bool voice_armed;`.
- Wire `on_link_established` / `on_link_terminated` to also call `voice->on_link_state(...)` and
  push a new `voice_path` event `{mode:"ale"|"voice", reason}`.
- **Binary WS protocol tag.** The existing spectrum path uses raw float binary. Voice coexists on
  the same socket via a **1-byte stream tag** on binary frames: `0x00` = spectrum, `0x01` = voice
  int16 PCM. Mic-up frames (browser → bridge) are tagged `0x01`.
- New / changed commands:
  - `VOICE_ARM {on}` — arm/disarm voice capability.
  - `VOICE_GET` → `{armed, mode, ptt}`.
  - `SET_PTT` is **context-routed** (see §4.3).
- Main-loop routing (`apps/ale_bridge.cpp`):
  ```cpp
  audio->tick(rx_buf);
  if (voice->passthrough_active()) {
      if (!rx_buf.empty() && !voice->ptt())
          ws.send_binary(voice_frame(rx_buf));   // radio RX → browser speaker
      // do NOT feed_audio (decoder idle during voice)
  } else if (!rx_buf.empty()) {
      ctrl.feed_audio(rx_buf.data(), (uint32_t)rx_buf.size());  // unchanged
  }
  ctrl.update(t);                               // still drives SM (Twa → terminate)
  // drain WS messages; voice mic binary frames → voice->push_mic_pcm(...)
  ```
- Settings export/import: add `voice_armed=` line (mirrors `audio_in=`/`audio_out=`).

### 5.4 `ALEController` — essentially unchanged

No new ownership. One optional hook: the bridge wires
`voice->on_ptt_activity = [&]{ if (ctrl.is_link_active()) ctrl.reset_link_idle_timer(); };`
so voice keeps the link alive. `ctrl.update()` continues during passthrough; the SM's Twa
idle-timeout is the natural link-termination path.

### 5.5 GUI — browser Web Audio (`apps/gui/mobile/{app.js,index.html,styles.css}` and `apps/gui/`)

- **Mic capture**: `getUserMedia({audio:{echoCancellation,noiseSuppression,autoGainControl}})` →
  an `AudioWorklet` (or `ScriptProcessor` fallback) that resamples the device rate to **8000 Hz
  mono int16** and emits ~20 ms frames. Each frame is sent as a binary WS message tagged `0x01`,
  only while PTT is held.
- **Speaker playback**: `AudioContext` + ring-buffer `AudioWorklet` that consumes incoming `0x01`
  int16 frames and plays them (resampled to the output device rate). Muted while PTT held.
- **Device selection**: `enumerateDevices()` for mic (`audioinput`) and speaker (`audiooutput`)
  in a new **Voice** settings section (browser devices, distinct from the bridge-side VAC devices
  in the existing Audio section).
- **PTT button** (existing `#pttBtnMob` / `#pttBtn`): pointer-down → start mic streaming +
  `SET_PTT{on:true}` + mute speaker; pointer-up → stop mic + `SET_PTT{on:false}` + unmute.
- **`voice_path` event** → show a "VOICE" badge in the call panel, swap the PTT label to voice
  PTT. End Link (`TERMINATE`) returns to ALE_EXCLUSIVE (driven by `link_terminated`).
- Desktop GUI (`apps/gui/`) gets the same Web Audio plumbing (same browser context).

## 6. Threading model

```
browser mic ──WS binary 0x01──▶ bridge main loop ──push_mic_pcm──▶ mic_ring_ (SPSC)
                                                                     │
                                                          modem render thread pulls
                                                          via vac->set_pcm_source(fn)
                                                                     │
                                                          resampler → VAC render → radio TX

radio RX ──VAC capture──▶ modem tick (main loop) ──rx_buf──▶ ws.send_binary(0x01) ──▶ browser speaker
```

- `mic_ring_`: written by the main loop (WS receive), read by the modem audio render thread →
  single-producer/single-consumer, lock-free.
- Speaker direction: no ring — the main loop already holds `rx_buf` and sends it directly.
- `set_pcm_source` / `set_symbol_source` are mutually exclusive and protected by the driver's
  existing `sym_src_mtx_`. The render thread snapshots the active source once per batch.

Buffer sizing: ~20–50 ms rings for low end-to-end latency. Browser round-trip latency
(~150–300 ms) is acceptable for half-duplex HF voice.

## 7. Configuration

- `ale.conf` / settings file: new `voice_armed=` key (persisted alongside `audio_in=`/`audio_out=`).
- Browser-side mic/speaker device choice is a GUI preference (not persisted in `ale.conf` — it is
  per-browser/per-device).
- Voice routing engages on LINKED **iff** voice is armed **and** the browser has mic permission.
  Otherwise the modem keeps the VAC (legacy behaviour).

## 8. Future digital-voice extension

A pluggable `IVoiceCodec` strategy:

```cpp
class IVoiceCodec {
public:
    virtual ~IVoiceCodec() = default;
    // mic PCM (8 kHz int16) → radio-bound bytes/frames
    virtual size_t encode(const int16_t* in, size_t n, uint8_t* out, size_t cap) = 0;
    // radio RX bytes/frames → speaker PCM (8 kHz int16)
    virtual size_t decode(const uint8_t* in, size_t n, int16_t* out, size_t cap) = 0;
};
```

`PassthroughCodec` (raw PCM, analog) is the default. A future codec (e.g. M17, FreeDV) replaces
only the encode/decode step. **The ownership/routing state machine is codec-agnostic** —
`VoicePathManager` calls the codec at the browser↔radio boundary; everything else (link-state
switching, PTT, exclusive ownership) is unchanged. Analog-only is implemented now.

## 9. Verification

1. **Build**: `cmake --build build` green; existing suites green (`ctest` 54/54 baseline).
2. **Unit tests**: `test_voice_path_manager` (NullAudioDriver + MockRadio) asserts the
   exclusive-ownership invariant and PTT routing.
3. **Manual E2E (desktop, VAC + browser)**: arm voice, grant mic, establish a link → VOICE badge,
   VAC switches to passthrough; hold PTT → peer hears voice; release → peer's voice plays; link
   ends → VAC returns to modem, ALE resumes automatically.
4. **Mobile E2E**: open the mobile GUI on a phone → phone mic/speaker used (the spec's central
   requirement).
5. **Regression**: voice disarmed ⇒ ALE-only operation byte-for-byte identical to today.