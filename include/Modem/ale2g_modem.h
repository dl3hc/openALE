/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — Modulator and Demodulator.
 *
 * namespace ale::ALE2GModem contains:
 *
 *   Modulator        — TX side: word-queue + 49-symbol pull API (no PCM, no timing)
 *   Demodulator      — RX side: streaming PCM → ALEWord via Goertzel + Grid-Lock
 *   SpectrumAnalyser — RX-side waterfall / spectrum analyser (display concern,
 *                      not decode).  See Modem/spectrum_analyser.h.
 *
 * ── Modulator ────────────────────────────────────────────────────────────────
 *
 * Encodes ALEWords into 49 8-FSK symbol values (0–7) for audio-layer pull.
 *
 *   ALEWord → word.encode() → tx49 (49-bit) → ALEEncoder → symbol_buf_[49]
 *
 * On-air word format (AC-WAVEFORM-008-2):
 *   symbol[k] = word_bit(3k%49) as bit2, word_bit((3k+1)%49) as bit1,
 *               word_bit((3k+2)%49) as bit0
 *   49 symbols × 8 ms/symbol = 392 ms = Trw (REQ-WAVEFORM-010).
 *
 * Pull API (thread-safe):
 *   Main thread  — enqueue_word() / enqueue_sequence() / is_transmitting()
 *   Audio thread — pull_symbol_frame(out_49)
 *
 * ── Demodulator ──────────────────────────────────────────────────────────────
 *
 * Accepts 8 kHz / mono / int16 PCM via push_samples().  Every DECODE_STEP_COARSE
 * (16) samples during acquisition, or DECODE_STEP_FINE (4) once grid-locked, it
 * attempts to decode the last 3136 samples (one ALE word block) via
 * Goertzel detection + ALEDecoder (majority vote + Golay FEC).  Accepted words
 * are delivered via word_cb_.
 *
 * Word acquisition / tracking (MIL-STD-188-141B A.5.2.6.3):
 *   Every word — during both initial and continuing sync — must satisfy all
 *   applicable criteria, NOT a zero-error decode.  The 3× symbol redundancy and
 *   Golay(24,12) exist so a receiver can acquire through residual symbol errors
 *   (channel noise, resampling smear, sub-sample phase) that any real or
 *   foreign-encoder signal carries — such words decode as DECODE_CORRECTED, not
 *   DECODE_OK.  The criteria applied here:
 *     - unanimous 2/3-vote count ≥ threshold  (primary BER / phase discriminator)
 *     - successful Golay decode of both A and B halves (DECODE_OK or _CORRECTED)
 *     - valid ASCII for the word's character set      (enforced in ALEDecoder)
 *     - acceptable leading preamble (TO / TWAS / TIS) for INITIAL acquisition
 *     - correct triple-redundant word phase (on-grid once locked)
 *   Silence-gap reset: 100 ms below threshold releases the grid lock so the
 *   next transmission re-acquires at its own sub-symbol phase.
 *
 * Word-boundary refinement:
 *   Golay + triple-redundant voting are shift-tolerant — a decode window up to
 *   ~half a symbol off the true word boundary still ranks every symbol's
 *   majority tone correctly and decodes cleanly.  Accepting the FIRST passing
 *   offset therefore anchors the grid ~28-32 samples early and poisons the
 *   per-symbol SINAD (A.5.4.1.2) whose guard sub-window then straddles the
 *   previous tone.  Instead, a passing decode only opens a refinement window:
 *   candidates are evaluated every DECODE_STEP_FINE samples and the offset with
 *   the maximum summed winning-tone Goertzel energy wins (the energy is a
 *   matched-filter statistic that peaks exactly at the boundary — the same
 *   principle as LinuxALE's per-phase magnitude-accumulator sync and ALELite's
 *   agreeing-sub-block symbol hunt).  After one full symbol has been scanned
 *   past the first candidate, the best candidate is committed: grid anchored
 *   there, word_cb_ fired with the SINAD measured at the refined alignment.
 *   Emission latency is ≤ SAMPLES_PER_SYMBOL (8 ms) — negligible vs Trw.
 *
 * Architecture:
 *   Demodulator = signal extraction (PCM → DecodedCandidate via Goertzel + FEC)
 *   WordGridTracker = grid-lock state machine (A.5.2.6.3 gate + refinement)
 *
 *   The grid-lock layer (WordGridTracker) is independently testable without PCM:
 *   construct DecodedCandidate values and call process_candidate() directly.
 *   See Modem/word_grid_tracker.h and tests/sync/unit/test_word_grid_tracker.cpp.
 */

#pragma once

#include "Codec/ale_decoder.h"
#include "Codec/ale_encoder.h"
#include "FSK/ale_waveform.h"
#include "FEC/golay.h"
#include "Modem/spectrum_analyser.h"
#include "Modem/word_grid_tracker.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include "Word/ale_sequence.h"
#include <array>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstring>

namespace ale {
namespace ALE2GModem {

// ── Modulator ─────────────────────────────────────────────────────────────────

class Modulator {
public:
    Modulator();

    /**
     * Enqueue one logical word for transmission (AC-WAVEFORM-008-2).
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Enqueue all words of an ALESequence for sequential transmission.
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_sequence(const ALESequence& seq);

    /**
     * Pull the next pending symbol frame (49 symbol values, 0–7) into out_49.
     * Returns false when idle. Thread-safe: may be called concurrently with enqueue_word().
     */
    bool pull_symbol_frame(uint8_t* out_49);

    /** True if at least one symbol frame is pending. Thread-safe. */
    bool is_transmitting() const;

    /** Immediately discard all pending TX frames. Thread-safe. */
    void abort();

private:
    mutable std::mutex   mtx_;
    uint64_t             pending_tx49_  = 0;
    bool                 word_enqueued_ = false;
    std::queue<uint64_t> tx49_queue_;
    SymbolFrame          symbol_buf_;

    void enqueue_tx49_(uint64_t tx49);
    void advance_queue_();
};

// ── Demodulator ───────────────────────────────────────────────────────────────

class Demodulator {
public:
    using WordCallback = std::function<void(const ALEWord&)>;

    /**
     * Spectrum callback — fired ~10 times/second from the audio thread.
     * bins[k] = power spectral density in dBFS at k * hz_per_bin Hz (1025 bins, ≈3.9 Hz each).
     * ALE channel (750–2500 Hz) maps to bins ≈ 768–2560.
     * Typical range: noise floor ≈ −90 dBFS, strong tone ≈ −20 dBFS.
     * Called from the audio capture thread — keep it short or hand off to a queue.
     */
    using SpectrumCallback = SpectrumAnalyser::SpectrumCallback;

    Demodulator();

    void set_word_callback(WordCallback cb) { tracker_.set_word_callback(std::move(cb)); }

    /** Register a callback for spectrum/waterfall data.  Pass nullptr to disable. */
    void set_spectrum_callback(SpectrumCallback cb) { spectrum_.set_callback(std::move(cb)); }

    /**
     * Enable / disable decoding.  Disabling resets the ring buffer and
     * word-grid lock so stale signal cannot leak after re-enable.
     */
    void set_enabled(bool en) {
        if (!en && enabled_) reset();
        enabled_ = en;
    }
    bool enabled() const { return enabled_; }

    /** Feed PCM samples (8 kHz, mono, 16-bit). May invoke word_cb_. */
    void push_samples(const int16_t* samples, uint32_t count);

    void reset();

    // ── FEC / sync configuration (A.5.2.6.3, manufacturer-tunable) ──────────
    // These set the BASE operating point used for word acquisition.  Default is
    // the most tolerant point (full Golay correction, MIN_UNANIMOUS_VOTES) so any
    // spec-compliant signal can be acquired.

    /// Select the Golay correction power applied during decode (A.5.2.6.3).
    void      set_golay_mode(GolayMode m) { tracker_.set_golay_mode(m); }
    GolayMode golay_mode() const          { return tracker_.golay_mode(); }

    /// Set the minimum unanimous 2/3-vote count required to accept a word (0..49).
    void    set_min_unanimous_votes(uint8_t v) { tracker_.set_min_unanimous_votes(v); }
    uint8_t min_unanimous_votes() const        { return tracker_.min_unanimous_votes(); }

    /// A.5.2.6.3 "DO": enable automatic adjustment of Golay mode + unanimous-vote
    /// threshold from observed signal quality.  Off by default → fixed operating
    /// point.  Adaptation runs only while the word grid is locked (tracking);
    /// acquisition always uses the configured base point so a degraded signal can
    /// still be acquired.
    void set_adaptive_fec(bool on) { tracker_.set_adaptive_fec(on); }
    bool adaptive_fec() const      { return tracker_.adaptive_fec(); }

    // ── §A.5.3.3 stage-1 scanning detection ──────────────────────────────────

    /// Call on every channel hop to arm the new-channel guard (§A.5.3.3 stage 1).
    /// Resets the write-position anchor and one-shot flag so stage-1 can fire
    /// exactly once on the new channel after WORD_SAMPLES of new audio.
    void mark_channel_hop() { hop_offset_ = write_pos_; energy_fired_ = false; }

    /// Register the §A.5.3.3 stage-1 callback: fired once per channel after
    /// unanimous_votes ≥ ALE_STAGE1_MIN_VOTES is observed and the decode window
    /// is entirely new-channel audio.  Pass nullptr to disable.
    void set_ale_energy_callback(std::function<void()> cb) {
        ale_energy_cb_ = std::move(cb);
    }

private:
    static constexpr uint32_t WORD_SAMPLES          = SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL; // 3136
    static constexpr uint32_t DECODE_STEP_COARSE    = SAMPLES_PER_SYMBOL / 4;   // 16 — acquisition
    static constexpr uint32_t DECODE_STEP_FINE      = SAMPLES_PER_SYMBOL / 16;  //  4 — grid-locked
    static constexpr uint32_t SILENCE_RESET_SAMPLES = 800;  // 100 ms at 8 kHz
    static constexpr int16_t  SILENCE_THRESHOLD     = 200;

    // Ring buffer holds exactly the decode window (one word + one symbol of slack
    // for boundary refinement).
    static constexpr uint32_t BUF_CAP = WORD_SAMPLES + SAMPLES_PER_SYMBOL;  // 3200

    // ── §A.5.3.3 stage-1 scanning detection ──────────────────────────────────
    // Lower vote gate for "detect sounds" — fires before the full Golay+ASCII gate.
    static constexpr uint8_t ALE_STAGE1_MIN_VOTES = 20;
    std::function<void()>    ale_energy_cb_;
    uint32_t                 hop_offset_    = 0;    // write_pos_ at last channel hop / reset
    bool                     energy_fired_  = false; // one-shot per channel; reset by mark_channel_hop()

    // ── Signal-extraction working set ──────────────────────────────────────
    bool                 enabled_       = true;
    std::vector<int16_t> ring_;
    uint32_t             write_pos_     = 0;
    uint32_t             step_accum_    = 0;
    uint32_t             silence_count_ = 0;

    // ── Grid-lock state machine ─────────────────────────────────────────────
    // All gate / refinement / adaptive-FEC logic lives in WordGridTracker.
    WordGridTracker tracker_;

    // ── Spectrum analyser (waterfall data source) ────────────────────────────
    SpectrumAnalyser spectrum_;

    // ── Signal-extraction helpers ────────────────────────────────────────────
    // Goertzel single-bin power |X(k)|^2 over M samples (default: one full
    // 64-sample symbol).
    static float   goertzel_power(const int16_t* block, float freq_hz,
                                  uint32_t M = SAMPLES_PER_SYMBOL);
    static uint8_t symbol_from_block(const int16_t* block, float& sinad_db_out,
                                     float& win_power_out);
    int16_t        ring_at(uint32_t abs_pos) const { return ring_[abs_pos % BUF_CAP]; }

    // Produce a DecodedCandidate from the current ring buffer position.
    // Uses tracker_.golay_mode() for the FEC operating point.
    DecodedCandidate try_decode_() const;

    void check_silence_reset_(int16_t s);
};

} // namespace ALE2GModem
} // namespace ale
