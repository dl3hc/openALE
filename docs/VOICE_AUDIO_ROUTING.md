# Voice Audio Routing

**Status:** Implemented and tested (`ctest` 57/57).
**Scope:** Simultaneous ALE operation + operator voice over the radio (VAC) audio path.

---

## 0. Terminology: Modem Audio Interface vs. Operator Audio Interface

openALE has two distinct audio interfaces, configured on two potentially different machines:

- **Modem Audio Interface** — the transceiver's VAC (`PAL::IAudioDriver`, `AudioDevice`),
  opened server-side via `AUDIO_OPEN`/`AUDIO_DEVICES` (Settings ▸ Modem ▸ Audio Devices). This
  is *always* configured on the machine running the controller/bridge — the device generating
  and demodulating ALE waveforms at a fixed 8 kHz mono.
- **Operator Audio Interface** — the browser mic/speaker path (`apps/gui/app.js` `Voice`
  object, WS binary tag `0x01`), configured on *whichever device the GUI happens to be open
  on* (Settings ▸ Operator Audio). Device selection is stored in that browser's `localStorage`
  and never sent to the bridge — a mic/speaker `deviceId` is meaningless on any other machine.
  The controller and the GUI may or may not be the same physical machine.

The Operator Audio Interface is the shared foundation for everything the operator hears or
speaks into from their own device: analog voice passthrough (§2), the link-independent channel
monitor (§2.5), notification sounds (ring on incoming call, chime on message arrival — client
side only, no backend involvement), and the future digital-voice codec seam (§6).

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

### 2.5 AudioMonitor — link-independent RX tap ("listen in")

`include/App/audio_monitor.h` / `src/App/audio_monitor.cpp`

Voice Passthrough (§2.2–2.3) only forwards RX audio while a voice link is up. The channel
monitor lets the operator "listen in" on the transceiver's RX audio from the Operator Audio
Interface **regardless of link state** — e.g. while scanning or idle — via a header toggle in
the GUI (`#monitorBtn`, `toggleMonitor()`) backed by the `AUDIO_MONITOR {on}` /
`AUDIO_MONITOR_GET` bridge commands.

`AudioMonitor` is a second, much simpler `RxSink` than `VoicePathManager`: no mode machine, no
mic ring, no PTT. `arm(true)` self-registers via `transport_->add_rx_sink(*this)`, `arm(false)`
unregisters — the same self-registration idiom VPM uses for passthrough entry/exit.

**Suppression, not exclusion.** Both `VoicePathManager` (during an active voice link) and
`AudioMonitor` (while armed) can be registered with the transport at the same time — they are
independent toggles. To avoid sending the same tick's PCM to the browser twice,
`AudioMonitor::on_rx_audio()` checks `voice_->passthrough_active()` and skips forwarding
whenever VoicePathManager is already streaming. The monitor is a superset of passthrough's RX
behavior (if you're on a voice link, you're already "listening in"), so this is a no-op
skip, not a feature loss.

Session-only: `AUDIO_MONITOR` is never persisted to `station.state` — it always defaults off on
bridge start/restart, like PTT.

---

## 3. Wire protocol

Binary WS frames carry a 1-byte stream tag:

| Tag | Direction | Content |
|---|---|---|
| `0x00` | bridge → browser | Spectrum (float32 LE FFT bins) |
| `0x01` | bridge → browser | RX PCM (int16 LE, 8 kHz mono) — voice passthrough or channel monitor |
| `0x01` | browser → bridge | Mic TX PCM (int16 LE, 8 kHz mono) |

Both `VoicePathManager` (passthrough) and `AudioMonitor` (channel monitor, §2.5) frame their RX
PCM identically and send it via the same tag — the browser plays whatever `0x01` frames arrive
without needing to know which server-side sink produced them.

### 3.1 Browser-side pipeline: AudioWorklet with ScriptProcessorNode fallback

`apps/gui/audio-worklets.js` (mirrored at `apps/gui/mobile/audio-worklets.js`) implements the
mic/speaker resampling as two `AudioWorkletProcessor`s (`speaker-processor`, `mic-processor`),
registered via `audioContext.audioWorklet.addModule(...)` and driven from `app.js`
(`voiceInitSpeaker()`/`voiceMicStart()`). This runs the resampling on the dedicated real-time
audio thread with a fixed 128-sample quantum, instead of the main JS thread — the previous
`ScriptProcessorNode` implementation was subject to main-thread jank and only ran in coarse
1024–2048-sample callbacks, which was the dominant source of both latency and jitter. The
resamplers were upgraded at the same time: cubic Hermite interpolation (RX upsample, replacing
linear) and a two-pass box average (TX downsample, replacing a single boxcar pass) — both cheap,
real-time-safe upgrades that reduce imaging/aliasing distortion versus the originals. The wire
format is unchanged (still raw 8 kHz PCM, no codec) — the quality loss being fixed here was
entirely in the device-rate ↔ 8 kHz resampling step, not the (nonexistent) encoding.

`AudioWorklet` requires a secure context (HTTPS, or `localhost`/`127.0.0.1`, which browsers
special-case as secure). Since the bridge serves plain HTTP and `--remote` (0.0.0.0 bind) exists
specifically to let the GUI be opened from another device on the LAN — not a secure context —
`app.js` feature-detects `audioContext.audioWorklet` and falls back to the original
`ScriptProcessorNode` implementation (`voiceSpkProcess`/`voiceMicProcess`, still present
unchanged) whenever it's unavailable. Same-machine/localhost access gets the AudioWorklet path;
remote-LAN-IP access keeps the pre-existing behavior.

The same secure-context requirement blocks `getUserMedia`/`enumerateDevices` even harder —
those have no non-secure-context fallback at all (unlike AudioWorklet's ScriptProcessorNode
path), so mic capture and device *listing* simply don't work over plain-HTTP LAN access. `--tls`
(see `docs/TLS_SETUP.md`) is the fix: it makes `https://<lan-ip>:port/` a genuine secure
context, unlocking the full Operator Audio Interface — mic, device listing, and the
AudioWorklet path — for remote access, not just localhost.

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
// Transport (declared before VoicePathManager/AudioMonitor — destroyed after them)
AudioTransport   transport;
VoicePathManager voice_mgr;
AudioMonitor     audio_monitor;

// Wire up
transport.set_decoder_sink([&](const int16_t* buf, size_t n) {
    ctrl.feed_audio(buf, n);          // permanent, every tick
});
auto send_rx_pcm_frame = [&](const int16_t* buf, size_t n) {
    ws.send_binary(tagged_frame(0x01, buf, n));  // shared framing, see §3
};
voice_mgr.on_speaker_pcm = send_rx_pcm_frame;     // called by VPM's on_rx_audio
voice_mgr.set_transport(&transport);              // enables self-registration
transport.set_media_producer(&voice_mgr);
transport.set_protocol_tx_query([&]() { return ctrl.is_tx_active(); });

audio_monitor.attach(&transport);                 // enables self-registration
audio_monitor.set_voice_manager(&voice_mgr);       // suppression guard, see §2.5
audio_monitor.on_pcm = send_rx_pcm_frame;

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
