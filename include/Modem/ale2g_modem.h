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
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>

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
    ///
    /// Only sets hop_pending_ (atomic).  push_samples() — which always runs on the
    /// audio thread — applies the actual tracker reset and ring zero when it observes
    /// the flag.  This avoids a data race: all mutable demodulator state (tracker_,
    /// ring_, hop_offset_, etc.) is exclusively owned by the audio thread; the
    /// controller thread only signals intent via this atomic flag.
    void mark_channel_hop() {
        hop_pending_.store(true, std::memory_order_release);
    }

    /// Register the §A.5.3.3 stage-1 callback: fired once per channel when the
    /// tone-diversity detector confirms ALE traffic (≥ MIN_DISTINCT_TONES distinct
    /// tones each seen as a per-symbol triple within DIVERSITY_WINDOW).  nullptr disables.
    void set_ale_energy_callback(std::function<void()> cb) {
        ale_energy_cb_ = std::move(cb);
    }

    // ── §A.5.3.3 stage-1 operator squelch (optional, default OFF) ─────────────
    // The detector above is level-invariant, so it works untouched on any audio
    // path.  This is a SEPARATE operator control: a global, audio-path-calibrated
    // noise floor + a dB margin that lets the operator require a signal to be at
    // least `margin_db` above the measured noise before scanning stops on it (i.e.
    // "ignore weak traffic").  Default OFF → detector behaviour is unchanged.
    void  set_scan_squelch_enabled(bool on) { scan_squelch_enabled_ = on; }
    bool  scan_squelch_enabled() const      { return scan_squelch_enabled_; }
    /// Minimum signal-above-floor to trigger. 3 dB ≈ 0 dB SINAD, 6 dB ≈ 4 dB, 10 dB ≈ 8 dB.
    void  set_scan_detect_margin_db(float db) {
        scan_detect_margin_lin_ = std::pow(10.0f, (db < 0.0f ? 0.0f : db) / 10.0f);
    }
    float scan_detect_margin_db() const {
        return 10.0f * std::log10(scan_detect_margin_lin_);
    }
    /// Learned global in-band noise floor, in dB (10·log10 of the tracked tonal power);
    /// 0 until the first non-tonal sub-block trains it.  For GUI display / calibration.
    float scan_floor_db() const {
        return scan_floor_valid_ ? 10.0f * std::log10(scan_floor_ + 1.0f) : 0.0f;
    }
    /// Operator "calibrate" — snapshot the current live floor as the persisted baseline.
    /// Returns the snapshotted floor in dB (0 if not yet trained).
    float calibrate_scan_detector() {
        scan_floor_baseline_ = scan_floor_;
        return scan_floor_db();
    }
    /// Last operator-calibrated floor snapshot, in dB (0 until calibrate is run).
    float scan_floor_baseline_db() const {
        return scan_floor_baseline_ > 0.0f ? 10.0f * std::log10(scan_floor_baseline_ + 1.0f) : 0.0f;
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
    // Level-invariant per-symbol-triple + tone-diversity detector (sim-locked
    // 2026-07-16).  Faithful to ALELite's per-symbol confidence, with a gr-ale
    // "> 2 distinct tones" guard added to reject a steady carrier that ALELite
    // would false-stop on.
    //
    // Every SUBBLOCK_STEP (16 = 2ms, OVERLAPPED) we take an 8-tone Goertzel over the
    // most recent ANALYSIS_SAMPLES (32 = 4ms).  A sub-block's winning tone counts only
    // if the block is *tonal*:
    //   peakiness = 2*(best + strongest_neighbour_bin)/ANA² / block_mean_square ≥ STAGE1_PEAKINESS
    // The +neighbour term absorbs 125 Hz scalloping (worst-case mistuning = half the
    // 250 Hz tone spacing); the ratio is level-invariant, so no absolute audio-path
    // calibration is needed here (that is a separate operator-facing squelch, not this
    // path).  A per-symbol TRIPLE = TRIPLE_LEN consecutive same-winner sub-blocks
    // (3×2ms = 6ms < the 8ms symbol, so it provably fits inside one symbol — unlike the
    // previous 4ms non-overlapped blocks whose 12ms "triple" exceeded a symbol and only
    // fired on lucky adjacent same-tone symbols).  On each triple we timestamp that
    // tone; ale_energy_cb_ fires once when ≥ MIN_DISTINCT_TONES tones have a triple within
    // DIVERSITY_WINDOW.  This fires on any 8-FSK data (~30ms) yet rejects noise/voice
    // (few triples) and a steady carrier (only one active tone).
    //
    // HOP_GUARD_SAMPLES: stage-1 is suppressed for this many samples after a hop, only
    // long enough to refill the ring after mark_channel_hop() zeroes it.  Async-tune
    // latency is handled upstream by the controller, which drops the callback while
    // !radio_->is_tune_settled(); the old 640-sample (80ms) guard duplicated that and
    // ate most of the 200ms dwell.  64 samples (8ms = one symbol) is enough here.
    static constexpr uint32_t ANALYSIS_SAMPLES   = SAMPLES_PER_SYMBOL / 2;  // 32 = 4ms Goertzel window
    static constexpr uint32_t SUBBLOCK_STEP      = SAMPLES_PER_SYMBOL / 4;  // 16 = 2ms overlap step
    static constexpr uint32_t TRIPLE_LEN         = 3;                       // per-symbol triple (6ms < 8ms)
    static constexpr float    STAGE1_PEAKINESS   = 0.50f;                   // tonality gate (sim-locked)
    static constexpr uint32_t DIVERSITY_WINDOW   = 800;                     // 100ms freshness window
    static constexpr uint32_t MIN_DISTINCT_TONES = 3;                       // gr-ale "> 2 tones" carrier guard
    static constexpr uint32_t HOP_GUARD_SAMPLES  = SAMPLES_PER_SYMBOL;      // 64 = 8ms ring-refill guard
    std::function<void()>     ale_energy_cb_;
    std::atomic<bool>         hop_pending_ {false};  // controller signals hop; audio thread applies it
    uint32_t                  hop_offset_    = 0;
    bool                      energy_fired_  = false;
    uint32_t                  subblk_accum_  = 0;
    uint8_t                   subblk_tone_[TRIPLE_LEN] = {0xFF, 0xFF, 0xFF};   // last TRIPLE_LEN sub-block winners
    int64_t                   last_triple_pos_[NUM_TONES];                     // write_pos_ of each tone's last triple

    // ── §A.5.3.3 stage-1 operator squelch state (default OFF) ─────────────────
    // Global in-band noise floor: asymmetric EWMA of the winner+neighbour tonal
    // power over NON-tonal sub-blocks.  Learned across every empty channel a scan
    // visits and NOT reset by mark_channel_hop() — it is the audio-path floor, not a
    // per-channel value, so it is never poisoned by landing mid-transmission.
    static constexpr float SCAN_FLOOR_ALPHA_UP   = 0.05f;  // slow: noise blocks pull floor up
    static constexpr float SCAN_FLOOR_ALPHA_DOWN  = 0.30f;  // fast: quiet restores the floor
    bool   scan_squelch_enabled_   = false;                 // operator opt-in; OFF ⇒ pure level-invariant
    float  scan_detect_margin_lin_ = 1.9953f;               // 3 dB default (10^0.3); min ≈ 0 dB SINAD
    float  scan_floor_            = 0.0f;                    // tracked global in-band noise floor (tonal power)
    float  scan_floor_baseline_   = 0.0f;                    // last operator-calibrated snapshot
    bool   scan_floor_valid_      = false;

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

    // Stage-1 sub-block tonality test over the last ANALYSIS_SAMPLES in the ring.
    // Returns true when the block is tonal (peakiness ≥ STAGE1_PEAKINESS) and sets
    // winner to the winning ALE tone rank (0–7); returns false otherwise.  Always sets
    // tonal_pow to the winner+neighbour mean-square power (used by the operator squelch
    // floor, whether or not the block is tonal).
    bool subblock_tonal_(uint8_t& winner, float& tonal_pow) const;

    void check_silence_reset_(int16_t s);
};

} // namespace ALE2GModem
} // namespace ale
