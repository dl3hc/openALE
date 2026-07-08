# Handoff: True-SINAD DSP rework of `symbol_from_block`

**Status:** open follow-up (agreed 2026-07-02). The LQA *rating/ranking* was already
made BER-led (see `openALE LQA compute_score fix` in memory / `from_direction_quality()`),
so the operator no longer sees "Poor" on a clean link. **This task is only about making
the reported SINAD dB *number* meaningful** — a clean link should read a high SINAD
(≈20–30 dB), a noisy link a low one, and the value should roughly track injected SNR.

**Blast radius is small:** SINAD now feeds only (a) the operator display and (b) the
*secondary* 0.3 term of `from_direction_quality()`. It no longer dominates ranking, so
changing the measurement cannot re-break channel selection.

---

## 1. Current implementation

`src/Modem/ale2g_modem.cpp`:

```cpp
// symbol_from_block() — per 64-sample symbol block
float total = 0.0f, best = -1.0f;
uint8_t rank = 0;
for (uint8_t r = 0; r < NUM_TONES; ++r) {
    float p = goertzel_power(block, TONE_FREQS_HZ[r]);  // |X(k)|^2 over the 64-sample window
    total += p;
    if (p > best) { best = p; rank = r; }
}
const float nd = total - best;                          // sum of the 7 non-winning tone bins
sinad_db_out = (nd > 0.0f) ? 10.0f*log10f(total/nd) : 30.0f;
return FREQ_TO_SYMBOL[rank];
```

`try_decode()` averages this over the 49 symbols: `out.sinad_db = sinad_sum / SYMBOLS_PER_WORD;`

**Flow of the value:** `out.sinad_db` → `MetricsSample.sinad_db` → `LQAMetrics::sinad_to_lqa_code()`
/ `update_entry_extended()` → `LQAEntry.sinad_db` → GUI display + secondary score term.

### Relevant constants (`include/FSK/ale_waveform.h`)
- `SAMPLE_RATE_HZ = 8000`, `SYMBOL_RATE_BAUD = 125`, `SAMPLES_PER_SYMBOL = 64`
- `NUM_TONES = 8`, `TONE_SPACING_HZ = 250`, tones `750..2500 Hz`
- `SYMBOLS_PER_WORD = 49`, `WORD_SAMPLES = 3136`
- TX tones are exact **integer cycles per 64-sample symbol** (750 Hz = 6 cyc … 2500 Hz = 20 cyc),
  so the 8 Goertzel bins are mutually **orthogonal** over a full symbol, and TX has
  slope-zero symbol boundaries (`REQ-WAVEFORM-005`).
- `goertzel_power()` returns `|X(k)|^2` (squared DFT magnitude), **rectangular window**.

---

## 2. Why it reads ~7 dB on a clean Virtual-Audio-Cable loopback

This is **not** inherent to a clean tone. On a *perfectly aligned* clean symbol (pure
integer-cycle tone) the other 7 Goertzel bins are exactly 0 → `nd=0` → SINAD clamps to 30.
The ~7 dB comes from **real off-fundamental energy inside the measured window**, from two
receiver-side artifacts (not channel impairment):

1. **Window straddle.** The 64-sample window is anchored at `grid_anchor_`, which can sit
   ±`DECODE_STEP_FINE`/2 = ±2 samples off the true symbol boundary. A straddle pulls in
   part of the *neighbouring* symbol (a different tone) → energy lands on other tone bins.
2. **Resampling ISI.** WASAPI double-resamples 8k↔48k↔8k; the polyphase FIR rings at
   every symbol transition, spreading energy across frequency. (`resampler.h` Q was already
   reduced 64→16 to shorten the ringing; `DECODE_STEP` was made dynamic. Residual remains.)

Both put ~25 % of the winning-tone energy off-fundamental at the window edges, giving
`10·log10(1.25/0.25) ≈ 7 dB`.

### What will NOT fix it (important — avoids a dead end)
A naïve "full-spectrum Parseval SINAD" — `signal = winning tone`, `N+D = total_time_power − signal`
— **makes the number *lower*, not higher**, because full-spectrum `N+D ≥` the 8-bin `N+D`.
The off-fundamental energy is genuinely present in the windowed samples; measuring it more
completely can only *decrease* the dB. The fix must **reduce the artifact energy captured**,
not just re-measure it.

---

## 3. Recommended approach

**Decouple the SINAD sub-measurement from the symbol decision, and measure SINAD over a
centered guard-limited window that excludes the transition-ringing edges.**

- Keep the **symbol decision** (`rank`) on the full 64-sample block — do not weaken decode.
- Compute **SINAD** on the central `M` samples of the symbol (e.g. `M = 32`, dropping 16 at
  each edge where FIR ringing / straddle live).
- Over that centered window use a **true wideband SINAD** so real channel noise still counts:
  - `S = ` power of the winning tone (Goertzel at the decided rank over the centered window)
  - `total = ` time-domain average power over the same centered window
  - `SINAD = 10·log10( total / (total − S) )`, clamp to `[0, 30]`.

Why this works:
- On clean audio the centered window is a near-pure integer-cycle tone → `total ≈ S` →
  high SINAD (excludes the receiver's own edge ringing — which is a processing artifact,
  not link quality).
- On a real noisy channel, AWGN is present throughout the window (not just at edges) → it
  correctly lowers `total − S` ratio → SINAD tracks SNR.

### Orthogonality check for a centered 32-sample window
250 Hz spacing over 32 samples @ 8 kHz = exactly 1 DFT-bin spacing; each tone is an integer
number of cycles (750 Hz = 3 cyc … 2500 Hz = 10 cyc). **Bins stay orthogonal** → a clean
tone still leaks ~0 into the others. If you pick a different `M`, re-verify all 8 tones are
integer-cycle (M must be a multiple of `SAMPLE_RATE_HZ / TONE_SPACING_HZ = 32`).

### Normalization note (get the scales consistent)
`goertzel_power` returns `|X(k)|^2`. For a real tone over `M` samples the average power is
`P = 2·|X(k)|^2 / M^2`. Time-domain average power is `(1/M)·Σ block[n]^2`. Convert the
Goertzel result to the same average-power scale before forming `total − S`. (Alternatively
compute both `S` and `total` in the DFT domain via Parseval — but the time-domain `total` is
cheaper and avoids per-bin summation.)

Consider guarding against `total < S` (numerical) → clamp `nd = max(total − S, ε)`.

---

## 4. Files to change
- `src/Modem/ale2g_modem.cpp` — `symbol_from_block()` (SINAD block), possibly `try_decode()`
  if you split the symbol-decision window from the SINAD window.
- `include/Modem/ale2g_modem.h` — only if a signature/const changes.
- Nothing else: downstream (`LQAMetrics`, DB, GUI) consumes `sinad_db` unchanged.

---

## 5. Verification plan

There is no audio-loopback in the current unit tests, so add a small harness (model it on
`tests/waveform/e2e/test_roundtrip.cpp`). Building blocks:
- **Synthesize clean PCM** from a known symbol stream via `src/FSK/tone_generator.cpp`
  (symbol→sine, uses `SYMBOL_TO_FREQ`). A round-trip through `Modulator` symbol frames +
  `ToneGenerator` gives a reference clean signal.
- **Feed** `Demodulator::push_samples()` and read back `out.sinad_db` (expose via the word
  callback or a test hook).
- Sanity sweeps:
  1. **Clean loopback** → expect SINAD ≥ ~20 dB (currently ~6–7).
  2. **AWGN sweep** (add gaussian noise at known SNR) → SINAD should decrease monotonically
     and land within a few dB of the injected SNR across ~5–25 dB.
  3. **BER unaffected** — decode still succeeds; `unanimous_votes` unchanged (you did not
     touch the symbol decision).
- Related existing references: `tests/waveform/integration/test_resampler_path.cpp`,
  `tests/waveform/unit/test_tone_thd.cpp`, `tests/waveform/unit/test_tone_accuracy.cpp`,
  `apps/tx_wav_dump.cpp`.

### Acceptance criteria
- [ ] Clean VAC loopback reports SINAD ≈ 20–30 dB (operator sees "Excellent/Good", not "+6 dB").
- [ ] SINAD decreases monotonically with added AWGN and roughly tracks injected SNR (±~3 dB).
- [ ] Decode BER / unanimous-vote counts unchanged vs. before (symbol decision untouched).
- [ ] All LQA + waveform suites green.

---

## 6. Context pointers
- Memory (Neo4j): entity **`openALE LQA compute_score fix`** — the BER-led decision + this
  follow-up.
- Memory (file): `project_lqa_ber_led_scoring.md`, `reference_lqa_spec_conventions.md`,
  `project_tx_tone_quality.md`.
- Spec: `ALE_standard_188_141B_extracted.json` — A.5.4.1.2 (SINAD = (S+N+D)/(N+D) averaged
  over each received ALE signal), A.5.4.2.2 (on-air SINAD code 0–30 dB; **leave that encoding
  untouched**), A.5.4.1.4 (display in dB).
