/**
 * AudioWorklet processors for the Operator Audio Interface's RX/TX PCM path.
 * See docs/VOICE_AUDIO_ROUTING.md. Wire format is unchanged: 8 kHz mono int16
 * LE PCM, WS binary tag 0x01. Runs on the dedicated audio-render thread (not
 * the main thread) for low, consistent latency; app.js falls back to
 * ScriptProcessorNode when AudioWorklet is unavailable (insecure context —
 * it requires HTTPS or localhost).
 */

const SRC_HZ    = 8000;
const BIN_TAG_VOICE = 0x01;

// Cubic Hermite (Catmull-Rom) interpolation — 4-tap, meaningfully less
// imaging distortion than linear (2-tap) when upsampling narrowband 8 kHz
// voice to the device's native rate.
function cubicAt(ring, cap, pos) {
  const i1  = Math.floor(pos);
  const t   = pos - i1;
  const i0  = (i1 - 1 + cap) % cap;
  const i1m = i1 % cap;
  const i2  = (i1 + 1) % cap;
  const i3  = (i1 + 2) % cap;
  const p0 = ring[i0], p1 = ring[i1m], p2 = ring[i2], p3 = ring[i3];
  const a0 = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
  const a1 =        p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
  const a2 = -0.5 * p0            + 0.5 * p2;
  const a3 =        p1;
  return ((a0 * t + a1) * t + a2) * t + a3;
}

class SpeakerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.cap   = 8192;               // ~1.0 s @ 8 kHz — generous jitter buffer
    this.ring  = new Float32Array(this.cap);
    this.write = 0;                  // next write index
    this.avail = 0;                  // samples buffered, ready to read
    this.read  = 0.0;                // fractional read position (8 kHz domain)
    this.muted = false;

    this.port.onmessage = (e) => {
      const msg = e.data;
      if (msg instanceof ArrayBuffer) {
        const i16 = new Int16Array(msg);
        for (let k = 0; k < i16.length; ++k) {
          this.ring[this.write] = i16[k] / 32768;
          this.write = (this.write + 1) % this.cap;
          if (this.avail < this.cap) this.avail++;
          else this.read = (this.read + 1) % this.cap;  // overrun: drop oldest
        }
      } else if (msg && msg.type === 'mute') {
        this.muted = !!msg.on;
      }
    };
  }

  process(_inputs, outputs) {
    const out = outputs[0] && outputs[0][0];
    if (!out) return true;
    if (this.muted || this.avail <= 0) { out.fill(0); return true; }

    // Upsample 8 kHz ring → context rate. Same buffering algorithm as the
    // ScriptProcessorNode fallback (voiceSpkProcess in app.js), just with
    // cubic Hermite instead of linear interpolation for the actual sample.
    const ratio    = sampleRate / SRC_HZ;   // `sampleRate` is a worklet global
    const invRatio = 1 / ratio;
    const nFill = Math.min(out.length, Math.floor(this.avail * ratio));
    // Clamp: cubic Hermite is not a convex combination, so it can overshoot
    // past the input's own peak on transients close to full scale — audible
    // as hard clipping at the WebAudio destination even though the source
    // PCM itself never left [-1, 1].
    let pos = this.read;
    for (let i = 0; i < nFill; ++i) {
      const v = cubicAt(this.ring, this.cap, pos);
      out[i] = v > 1 ? 1 : (v < -1 ? -1 : v);
      pos += invRatio;
    }
    for (let i = nFill; i < out.length; ++i) out[i] = 0;  // underrun → silence
    const consumed = Math.ceil(nFill / ratio);
    this.read  = (this.read + consumed) % this.cap;
    this.avail = Math.max(0, this.avail - consumed);
    return true;
  }
}

// Two-pass box average (≈ triangular/Bartlett window) — meaningfully better
// alias suppression before decimation than a single boxcar pass, at the same
// order of cost. Fixed-factor first pass (halve the rate) + a second pass
// that finishes the decimation to 8 kHz; supports fractional ratios exactly
// like the original single-pass implementation did.
function boxDecimate(input, ratio) {
  const nOut = Math.floor(input.length / ratio);
  const out = new Float32Array(nOut);
  for (let i = 0; i < nOut; ++i) {
    const start = Math.floor(i * ratio), end = Math.floor((i + 1) * ratio);
    let sum = 0;
    for (let j = start; j < end; ++j) sum += input[j];
    out[i] = sum / Math.max(1, end - start);
  }
  return out;
}

class MicProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    // Accumulate device-rate samples across render quanta (128 samples,
    // ~2.7 ms @ 48 kHz — too small to send individually) until there's
    // enough for a ~20 ms chunk, then downsample and post it in one shot.
    // Small enough for low latency, large enough to amortize postMessage.
    this.chunkMs = 20;
    this.buf = new Float32Array(0);
  }

  process(inputs) {
    const inp = inputs[0] && inputs[0][0];
    if (!inp || inp.length === 0) return true;

    const merged = new Float32Array(this.buf.length + inp.length);
    merged.set(this.buf, 0);
    merged.set(inp, this.buf.length);
    this.buf = merged;

    const target = Math.round(sampleRate * this.chunkMs / 1000);
    if (this.buf.length < target) return true;

    const chunk = this.buf.subarray(0, target);
    this.buf = this.buf.slice(target);  // keep remainder for next quanta

    const ratio  = sampleRate / SRC_HZ;
    const stage1 = boxDecimate(chunk, 2);
    const stage2 = boxDecimate(stage1, ratio / 2);

    const i16 = new Int16Array(stage2.length);
    for (let i = 0; i < stage2.length; ++i) {
      let v = Math.round(stage2[i] * 32767);
      if (v > 32767) v = 32767; if (v < -32768) v = -32768;
      i16[i] = v;
    }

    // Frame it here (tag byte + int16 LE PCM) so the main thread's port
    // handler is a pure relay to the WebSocket — no per-sample work left
    // on the main thread. (Int16Array can't start at a 1-byte offset into
    // the framed buffer — its byteOffset must be a multiple of 2 — so the
    // tag byte is prepended via a Uint8Array copy instead.)
    const framed = new Uint8Array(1 + i16.byteLength);
    framed[0] = BIN_TAG_VOICE;
    framed.set(new Uint8Array(i16.buffer), 1);
    this.port.postMessage(framed.buffer, [framed.buffer]);
    return true;
  }
}

registerProcessor('speaker-processor', SpeakerProcessor);
registerProcessor('mic-processor', MicProcessor);
