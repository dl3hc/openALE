/**
 * \file Modem/word_grid_tracker.h
 * \brief A.5.2.6.3 grid-lock state machine extracted from ALE2GModem::Demodulator.
 *
 * WordGridTracker accepts decoded word candidates (DecodedCandidate) and runs
 * the grid-lock, word-boundary refinement, and adaptive FEC logic defined in
 * MIL-STD-188-141B A.5.2.6.3.  It owns all grid state and fires the word callback
 * when a word is committed.
 *
 * The seam type is DecodedCandidate — the output of the Goertzel + FEC stage.
 * Because the tracker takes candidates as plain data, it is independently testable
 * without PCM generation: construct DecodedCandidate values directly and call
 * process_candidate().  The Demodulator owns one tracker instance and calls it
 * from push_samples() after producing each candidate via try_decode().
 */

#pragma once

#include "FEC/golay.h"
#include "FSK/ale_waveform.h"
#include "Word/ale_word.h"
#include <functional>
#include <cstdint>

namespace ale {
namespace ALE2GModem {

// ── Seam type ────────────────────────────────────────────────────────────────

/// Output of the Goertzel + FEC stage (Demodulator::try_decode_()).
/// Passed to WordGridTracker::process_candidate() for grid-lock filtering.
struct DecodedCandidate {
    ALEWord             word;
    Golay::DecodeResult fec;
    uint8_t             unanimous_votes = 0;
    float               word_energy     = 0.0f;
    bool                decoded_ok      = false;
};

// ── AdaptiveFec ───────────────────────────────────────────────────────────────

/**
 * A.5.2.6.3 "DO" — EWMA quality tracker that maps observed unanimous-vote
 * counts to a Golay correction mode and a unanimous-vote threshold.
 * Used by WordGridTracker::OperatingPoint; also directly testable.
 */
class AdaptiveFec {
public:
    void reset() { quality_ = INIT_QUALITY; }
    void observe(uint8_t votes) {
        quality_ += ALPHA * (static_cast<float>(votes) - quality_);
    }
    float     quality()   const { return quality_; }
    GolayMode mode()      const {
        if (quality_ >= CLEAN_HI) return GolayMode::Mode1_6;
        if (quality_ >= CLEAN_LO) return GolayMode::Mode2_5;
        return GolayMode::Mode3_4;
    }
    uint8_t   threshold() const {
        float t = quality_ - MARGIN;
        if (t < MIN_THRESHOLD) t = MIN_THRESHOLD;
        if (t > MAX_THRESHOLD) t = MAX_THRESHOLD;
        return static_cast<uint8_t>(t + 0.5f);
    }
private:
    static constexpr float   ALPHA         = 0.125f;
    static constexpr float   MARGIN        = 8.0f;
    static constexpr float   CLEAN_LO      = 45.0f;
    static constexpr float   CLEAN_HI      = 48.0f;
    static constexpr float   INIT_QUALITY  = 40.0f;
    static constexpr uint8_t MIN_THRESHOLD = 30;
    static constexpr uint8_t MAX_THRESHOLD = 42;
    float quality_ = INIT_QUALITY;
};

// ── WordGridTracker ───────────────────────────────────────────────────────────

/**
 * A.5.2.6.3 grid-lock state machine.
 *
 * Accepts DecodedCandidate values from the Goertzel/FEC stage and implements:
 *   - initial-acquisition preamble filter (TO/TIS/TWAS/FROM only cold-lock)
 *   - unanimous-vote threshold gate
 *   - min-spacing gate (no duplicate decodes of the same physical word)
 *   - word-boundary refinement (argmax winning-tone energy within one symbol)
 *   - continuing-sync on-grid phase check for FEC-corrected words
 *   - adaptive FEC operating-point update (A.5.2.6.3 "DO")
 *   - silence-gap grid reset
 *   - uncorrectable-word surfacing (valid=false) for downstream BER accounting
 *
 * All state lives here; the Demodulator provides the write-pointer (absolute
 * sample position) as an explicit parameter to every mutating call.
 */
class WordGridTracker {
public:
    using WordCallback = std::function<void(const ALEWord&)>;

    void set_word_callback(WordCallback cb) { word_cb_ = std::move(cb); }

    /// Feed one decoded candidate at write-pointer position write_pos.
    /// May fire word_cb_ (via commit_refined_word_ or try_emit_uncorrectable_).
    void process_candidate(const DecodedCandidate& c, uint32_t write_pos);

    /// Call when a silence gap (>100 ms below threshold) has been detected.
    /// Commits any pending refinement word, releases the grid lock, and
    /// restores the base FEC operating point.
    void on_silence_gap();

    bool is_grid_locked() const { return grid_locked_; }

    /// True when the demodulator should use the fine (4-sample) decode step.
    /// Matches the old (grid_locked_ || ref_.active) condition in push_samples().
    bool use_fine_step() const { return grid_locked_ || ref_.active; }

    void reset();

    // ── FEC / sync operating-point configuration (A.5.2.6.3) ────────────────
    void      set_golay_mode(GolayMode m)        { op_.base_mode = m; op_.mode = m; }
    GolayMode golay_mode()      const            { return op_.mode; }
    void      set_min_unanimous_votes(uint8_t v) { op_.base_min_unanimous = v; op_.min_unanimous = v; }
    uint8_t   min_unanimous_votes() const        { return op_.min_unanimous; }
    void      set_adaptive_fec(bool on)          { op_.adaptive = on; if (!on) op_.restore_base(); }
    bool      adaptive_fec()    const            { return op_.adaptive; }

private:
    static constexpr uint32_t WORD_SAMPLES  = SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL;
    static constexpr uint8_t  MIN_UNANIMOUS = 33;  // ~67 % of 49, A.5.2.6.3 criterion 1

    // Grid-loss timeout: release the lock if no new word has anchored the grid
    // for this many word periods. Backstops on_silence_gap(), whose raw-PCM
    // amplitude gate (±SILENCE_THRESHOLD for 100 ms) never fires on real
    // receive audio carrying any noise/hiss between transmissions — without
    // this, grid_locked_ (and the GUI's "Sync" indicator) stays true
    // indefinitely once a transmission ends, until the channel is changed.
    static constexpr uint32_t GRID_TIMEOUT_SAMPLES = 3 * WORD_SAMPLES;

    // ── FEC / sync operating point ────────────────────────────────────────────
    struct OperatingPoint {
        GolayMode   base_mode          = GolayMode::Mode3_4;
        uint8_t     base_min_unanimous = MIN_UNANIMOUS;
        GolayMode   mode               = GolayMode::Mode3_4;
        uint8_t     min_unanimous      = MIN_UNANIMOUS;
        bool        adaptive           = false;
        AdaptiveFec afec;
        void restore_base() {
            mode          = base_mode;
            min_unanimous = base_min_unanimous;
            afec.reset();
        }
    };

    // ── Word-boundary refinement state ────────────────────────────────────────
    struct Refinement {
        bool     active      = false;
        uint32_t first_pos   = 0;     ///< write_pos of first passing candidate
        uint32_t best_pos    = 0;     ///< write_pos of highest-energy candidate so far
        float    best_energy = 0.0f;
        uint8_t  best_votes  = 0;
        ALEWord  best_word{};
        void reset() { active = false; best_energy = 0.0f; best_word = ALEWord{}; }
    };

    // ── Grid state ────────────────────────────────────────────────────────────
    WordCallback word_cb_;
    bool         grid_locked_   = false;
    uint32_t     grid_anchor_   = 0;
    uint32_t     uncorr_anchor_ = 0;
    Refinement   ref_;
    OperatingPoint op_;

    // ── Internal helpers (logic unchanged from Demodulator) ──────────────────
    bool gate_word_(const DecodedCandidate& c, uint32_t write_pos) const;
    void try_emit_decoded_(const ALEWord& word, float energy,
                           uint8_t votes, uint32_t write_pos);
    void try_emit_uncorrectable_(const ALEWord& word, uint32_t write_pos);
    void commit_refined_word_();
};

} // namespace ALE2GModem
} // namespace ale
