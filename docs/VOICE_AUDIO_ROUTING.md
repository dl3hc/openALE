# Voice Audio Routing

**Status:** Implemented and tested (`ctest` 57/57).
**Scope:** Simultaneous ALE operation + operator voice over the radio (VAC) audio path.

---

## 1. Problem

A single VAC bridges the ALE modem to the radio. The old `VoicePathManager`
implementation handed the VAC to the voice path exclusively during a phone-patch link,
which had two critical defects:

1. **Remote termination impossible.** The ALE decoder (`feed_audio`) was gated behind an
   `else`—never called during voice passthrough. When the remote station sent `TWAS` to
   end the link, it was forwarded to the browser speaker and silently discarded.
2. **Local termination hack.** The bridge forced `voice->on_link_state(false)` before
   forwarding `CMD:TERMINATE`, temporarily restoring the modem symbol path to allow
   the termination burst to go out. This was fragile and relied on the arbiter not
   noticing the path had been illegally released.

Both defects share the same root cause: exclusive VAC ownership prevents the ALE decoder
from running in parallel with voice passthrough. RX is not a contended resource—only TX is.

---

## 2. Architecture

### 2.1 AudioTransport — central hub

`include/App/audio_transport.h` / `src/App/audio_transport.cpp`

The transport permanently owns the VAC and drives all audio movement each tick:

```
                  ┌─────────────────────────────────────┐
       VAC RX ───▶│  tick()  [bridge main loop]         │──▶ ctrl.feed_audio()  [always]
                  │                                      │
                  │  RX fan-out:                         │──▶ on_speaker_pcm()  [when registered
                  │    decoder_sink_  (permanent)        │     + not TX]
                  │    rx_sinks_[]    (dynamic)          │
                  │                                      │
                  │  TX arbiter: arbitrate_tx_()         │──▶ VAC set_pcm_source()
                  └─────────────────────────────────────┘
```

**RX fan-out (the core fix):**
- `decoder_sink_` (bound to `ctrl.feed_audio`) is called **every tick, in every state**.
  Voice passthrough no longer mutes the decoder. A remote `TWAS` arriving during a QSO
  is decoded normally and triggers `on_link_terminated`.
- `rx_sinks_[]` is a list of dynamic `RxSink*` entries, called each tick **when not
  transmitting** (both protocol and media TX suppress them — half-duplex). Dynamic sinks
  self-register / self-unregister; the transport does not know their lifecycle.

**TX arbitration by priority (runs every tick):**

| Priority | Condition | pcm_source installed |
|---|---|---|
| 1. Protocol | `ctrl.is_tx_active()` | `nullptr` → modem symbol path |
| 2. Media | `vpm.media_tx_wanted()` (PTT + passthrough) | mic ring pull |
| 3. Idle | `vpm.passthrough_active()` | silence |
| 4. ALE exclusive | (default) | `nullptr` → modem symbol path |

`set_pcm_source` is only called on state **change** (`last_source_` tracking), never
redundantly. The modem symbol source stays registered for the life of the device.

### 2.2 VoicePathManager — media producer / RxSink

`include/App/voice_path_manager.h` / `src/App/voice_path_manager.cpp`

VPM implements the `RxSink` interface and self-manages its registration:

- **`enter_passthrough_()`**: calls `transport_->add_rx_sink(*this)` — VPM joins the
  dynamic sink list and will receive speaker audio each tick.
- **`exit_passthrough_()`**: calls `transport_->remove_rx_sink(*this)` — VPM leaves
  the list before releasing PTT and resetting mode.
- **`on_rx_audio(buf, n)`**: calls `on_speaker_pcm(buf, n)` — the bridge-provided
  callback that sends the tagged binary WS frame (0x01) to the browser.

VPM **never calls `set_pcm_source()`** — TX source arbitration belongs entirely to the
transport. VPM exposes `media_tx_wanted()` (PTT + passthrough) and `pull_mic_pcm()`
(SPSC ring consumer) so the transport can select and pull the mic source.

### 2.3 State machine

```
              link_established (voice armed)
ALE_EXCLUSIVE ──────────────────────────────▶ VOICE_PASSTHROUGH
     ▲          enter_passthrough_():              │  PTT off: decoder + speaker
     │            add_rx_sink(*this)               │  PTT on : decoder only (mic TX)
     │                                             │
     │  link_terminated (any reason)               │
     └─────────────────────────────────────────────┘
       exit_passthrough_():
         remove_rx_sink(*this)
```

The modem's `ctrl.update()` continues running in both states so the SM's Twa idle-timeout
can fire and terminate the link — which is exactly what calls `on_link_terminated` and
drives the state machine back to `ALE_EXCLUSIVE`.

### 2.4 Observable session sub-states (Phase 4)

`AudioTransport` exposes three read-only flags, polled by the bridge after each tick:

| Method | Meaning |
|---|---|
| `receiving_voice()` | Passthrough active, radio in RX, speaker forwarded |
| `transmitting_voice()` | Media (mic) TX won arbitration this tick |
| `protocol_pending()` | Protocol burst preempting active voice session |

The bridge pushes a `voice_session {state: "receiving"|"transmitting"|"protocol"|""}` event
on every sub-state transition. `""` = not in a voice session.

---

## 3. Wire protocol

Binary WS frames carry a 1-byte stream tag:

| Tag | Direction | Content |
|---|---|---|
| `0x00` | bridge → browser | Spectrum (float32 LE FFT bins) |
| `0x01` | bridge → browser | Voice RX PCM (int16 LE, 8 kHz mono) |
| `0x01` | browser → bridge | Mic TX PCM (int16 LE, 8 kHz mono) |

---

## 4. Threading model

```
bridge main loop:
  ctrl.feed_audio()  ──────────────────────────────────────▶ decoder
  on_speaker_pcm()   ──▶ ws.send_binary(0x01) ──▶ browser speaker

browser → WS ──▶ main loop: push_mic_pcm() ──▶ mic_ring_ [SPSC]
                                                     │
                                          audio render thread:
                                          pull_mic_pcm() → resampler → VAC TX
```

- The mic ring is SPSC lock-free: main loop produces, render thread consumes.
- All other calls (decoder, speaker, arbitration) run on the main loop.
- `set_pcm_source` is called from the main loop; the render thread reads the snapshot
  atomically per the existing driver guarantee (`sym_src_mtx_`).

---

## 5. Bridge setup (summary)

```cpp
// Transport (declared before VoicePathManager — destroyed after it)
AudioTransport   transport;
VoicePathManager voice_mgr;

// Wire up
transport.set_decoder_sink([&](const int16_t* buf, size_t n) {
    ctrl.feed_audio(buf, n);          // permanent, every tick
});
voice_mgr.on_speaker_pcm = [&](const int16_t* buf, size_t n) {
    ws.send_binary(tagged_frame(0x01, buf, n));  // called by VPM's on_rx_audio
};
voice_mgr.set_transport(&transport);  // enables self-registration
transport.set_media_producer(&voice_mgr);
transport.set_protocol_tx_query([&]() { return ctrl.is_tx_active(); });

// Each main-loop tick:
voice_mgr.attach(audio.get(), radio.get()); // no-op when unchanged
transport.attach(audio.get());              // no-op when unchanged
transport.tick();                           // capture → fan-out → arbitrate
```

---

## 6. Future digital-voice extension

The `RxSink` interface and `IVoiceCodec` hook points are the seams for digital voice:

```cpp
class IVoiceCodec {
public:
    virtual ~IVoiceCodec() = default;
    virtual size_t encode(const int16_t* in, size_t n, uint8_t* out, size_t cap) = 0;
    virtual size_t decode(const uint8_t* in, size_t n, int16_t* out, size_t cap) = 0;
};
```

A `PassthroughCodec` (raw PCM, today's default) satisfies the interface without
modification. A future codec (M17, FreeDV, etc.) replaces only the encode/decode step;
the routing state machine, TX arbiter, and mic ring are codec-agnostic.

---

## 7. Verification

1. `cmake --build build` — clean build, 0 warnings.
2. `ctest` — 57/57 green (includes `test_audio_transport` AT-1 + AT-2, `test_voice_path_manager` 1–8).
3. **Manual E2E**: arm voice, establish link → `receiving_voice()` fires, browser speaker
   plays. Remote sends `TWAS` → `on_link_terminated` fires, ALE resumes (critical fix).
4. **Local End Link**: `CMD:TERMINATE` → `TWAS` goes out via protocol TX arbiter, no path
   release hack needed.
5. **Voice-disarmed regression**: ALE-only operation byte-identical to pre-refactor.
