/**
 * \file Modem/word_grid_tracker.cpp
 * \brief A.5.2.6.3 grid-lock state machine — WordGridTracker implementation.
 *
 * All logic is extracted verbatim from ALE2GModem::Demodulator.  The only
 * mechanical changes are:
 *   - write_pos is passed as an explicit parameter (was write_pos_ member)
 *   - gate_word_() takes DecodedCandidate instead of three separate out-params
 *   - try_emit_decoded_() / try_emit_uncorrectable_() receive write_pos explicitly
 */

#include "Modem/word_grid_tracker.h"
#include "Word/ale_word.h"

namespace ale {
namespace ALE2GModem {

void WordGridTracker::reset()
{
    grid_locked_   = false;
    grid_anchor_   = 0;
    uncorr_anchor_ = 0;
    ref_.reset();
    op_.restore_base();
}

void WordGridTracker::process_candidate(const DecodedCandidate& c, uint32_t write_pos)
{
    // Boundary refinement: one full symbol past the first passing candidate
    // the energy peak has necessarily been bracketed — commit the best candidate.
    if (ref_.active && (write_pos - ref_.first_pos) >= SAMPLES_PER_SYMBOL)
        commit_refined_word_();

    if (c.decoded_ok && gate_word_(c, write_pos))
        try_emit_decoded_(c.word, c.word_energy, c.unanimous_votes, write_pos);
    else if (!c.decoded_ok && !ref_.active && grid_locked_
             && c.unanimous_votes >= op_.min_unanimous
             && (c.word.golay_uncorrectable || !c.word.valid))
        try_emit_uncorrectable_(c.word, write_pos);
}

void WordGridTracker::on_silence_gap()
{
    // Transmission ended: flush any pending boundary refinement (the best
    // candidate is a fully gated word — do not lose it), then release the
    // grid and return to the base (acquisition) operating point so the next
    // transmission re-acquires with full tolerance.
    if (ref_.active) commit_refined_word_();
    if (grid_locked_) op_.restore_base();
    grid_locked_ = false;
}

bool WordGridTracker::gate_word_(const DecodedCandidate& c, uint32_t write_pos) const
{
    // A.5.2.6.3 criterion 1: unanimous-vote threshold
    if (c.unanimous_votes < op_.min_unanimous)
        return false;

    const bool clean_decode = (c.fec.flag == Golay::DECODE_OK);

    // Initial acquisition: only address-opening preambles cold-lock the grid.
    // DATA/REP are extension words — a DATA cold-lock offsets the grid by one Trw.
    // CMD has no char-set gate and is an unreliable anchor under noise.
    // THRU only appears mid-rotation in group scanning calls.
    if (!grid_locked_)
        return c.word.type == PreambleType::TO
            || c.word.type == PreambleType::TIS
            || c.word.type == PreambleType::TWAS
            || c.word.type == PreambleType::FROM;

    // Continuing sync: ignore candidates closer than (almost) one full word
    // to the previous committed word boundary.
    const uint32_t samples_since = write_pos - grid_anchor_;
    const uint32_t min_spacing   = WORD_SAMPLES - SAMPLES_PER_SYMBOL;
    if (samples_since < min_spacing)
        return false;

    if (clean_decode)
        return true;

    // FEC-corrected decode: accept only when on the word grid
    // (within one symbol of an expected boundary — A.5.2.6.3 criterion 9).
    const uint32_t phase = samples_since % WORD_SAMPLES;
    return (phase <= SAMPLES_PER_SYMBOL) ||
           (phase >= WORD_SAMPLES - SAMPLES_PER_SYMBOL);
}

void WordGridTracker::try_emit_decoded_(const ALEWord& word, float word_energy,
                                        uint8_t unanimous_votes, uint32_t write_pos)
{
    // Do NOT accept the first passing offset: the decode pipeline is
    // shift-tolerant up to ~half a symbol, so the earliest passing offset lies
    // before the true word boundary and would poison the SINAD measurement.
    // Track the candidate whose summed winning-tone energy peaks at the true
    // boundary; commit_refined_word_() emits the best one after one symbol.
    if (!ref_.active) {
        ref_.active      = true;
        ref_.first_pos   = write_pos;
        ref_.best_energy = -1.0f;
    }
    if (word_energy > ref_.best_energy) {
        ref_.best_energy = word_energy;
        ref_.best_pos    = write_pos;
        ref_.best_votes  = unanimous_votes;
        ref_.best_word   = word;
    }
}

void WordGridTracker::try_emit_uncorrectable_(const ALEWord& word, uint32_t write_pos)
{
    // Grid-locked, quality-thresholded NON-decoded word: report via word_cb_
    // with word.valid=false so downstream sees every on-grid word.
    // Fire only at phase==0 (exact grid slot) — straddles at phase 4 /
    // WORD_SAMPLES-4 are Golay-uncorrectable mashes of two adjacent words, not
    // real words, and would inflate BER by 48 per spurious fire (A.5.4.1.1).
    const uint32_t samples_since = write_pos - grid_anchor_;
    const uint32_t min_sp        = WORD_SAMPLES - SAMPLES_PER_SYMBOL;
    if (samples_since < min_sp) return;
    const uint32_t phase = samples_since % WORD_SAMPLES;
    if ((phase == 0u) && (write_pos - uncorr_anchor_ >= min_sp)) {
        uncorr_anchor_ = write_pos;
        if (word_cb_) word_cb_(word);
    }
}

void WordGridTracker::commit_refined_word_()
{
    // Anchor the grid at the best-aligned candidate so the next word's decode
    // window and per-symbol SINAD line up with the true symbol boundaries.
    grid_locked_ = true;
    grid_anchor_ = ref_.best_pos;
    ref_.active  = false;

    // A.5.2.6.3 "DO": fold the committed word's quality into the adaptive
    // estimate and update the operating point.
    if (op_.adaptive) {
        op_.afec.observe(ref_.best_votes);
        op_.mode          = op_.afec.mode();
        op_.min_unanimous = op_.afec.threshold();
    }
    if (word_cb_) word_cb_(ref_.best_word);
}

} // namespace ALE2GModem
} // namespace ale
