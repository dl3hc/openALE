/**
 * \file App/sounding_identity_accumulator.h
 * \brief A.5.3.1 sounding-conclusion identity reassembly — extracted from
 *        ALEController::rx_accumulate_sounding()/commit_sounding_sample().
 *
 * A received sounding transmission repeats its self-address conclusion
 * (TIS or TWAS anchor word + up to 4 DATA/REP extension words, e.g.
 * "SL3ZXB" = TIS:SL3 + DATA:ZXB) several times over ~10-15s for redundancy
 * (A.5.3.1). Two things make naive "concatenate arrival-order, last-anchor-
 * wins" reassembly wrong:
 *
 *   1. Extension words are POSITIONAL, not just typed: for one address,
 *      chunks strictly alternate DATA,REP,DATA,REP by chunk index (the exact
 *      inverse of AddressEncoder::encode(), see Word/address_encoder.h). A
 *      dropped word mid-sequence must not silently shift later characters
 *      into the wrong slot.
 *   2. Repeats are redundant copies of the SAME identity, not independent
 *      transmissions: a later repeat's lost extension word must not discard
 *      an earlier repeat's fully-assembled address.
 *
 * This class fixes both: per-cycle slot-indexed acceptance (a word is only
 * assigned to slot N if its type matches slot N's required type; on the
 * first mismatch — wrong type, golay_uncorrectable, or valid==false for any
 * other reason — no further words are slot-assigned in *this* cycle; no
 * lookahead or guessing is performed, cross-cycle redundancy recovers the
 * gap instead) plus cross-cycle per-slot vote maps (each slot's winning
 * content is decided by summed ALEWord::unanimous_votes across every repeat
 * in the session, so one clean repeat recovers a slot another repeat
 * dropped).
 *
 * The sole inactivity signal is ALETimingConstants::Tdrw_ms (784ms, unchanged
 * value already used elsewhere for the identical "words have stopped"
 * purpose) — refreshed by ANY word-level event while a session is open
 * (valid, invalid-but-attempted, or golay_uncorrectable), not just cleanly
 * parsed content words. That refresh is what closes the original bug: in a
 * continuous burst words arrive every ~392ms (TRW_MS), so two consecutive
 * non-refreshing invalid words previously looked like 784ms of silence even
 * though RF was still keyed, triggering a premature commit of a partial
 * address. No second/longer timeout and no hard duration cap are used —
 * bursts are self-terminating (bounded TX repeat count + PTT release) and
 * the caller simply stops feeding words once the SM leaves SCANNING/IDLE.
 *
 * Scope: this position grammar applies to TIS/TWAS sounding conclusions
 * only. AddressEncoder::encode_group() — where REP can instead mean "new
 * recipient" in a multi-address group/net call — is only ever invoked with
 * PreambleType::TO (Word/ale_sequence.cpp), never TIS/TWAS, so that
 * ambiguity never reaches this class. NON-GOAL: ALECallProcessor's TO-field
 * handshake reassembly (caller_address/to_address) DOES have that ambiguity
 * and is not covered here — it would need to reuse
 * FrameValidator::reconstruct_to_addresses()'s last_non_rep state machine
 * (Protocol/Message/frame_validator.cpp) if ever fixed; that is a separate,
 * not-in-scope follow-up.
 *
 * The seam type is ALEWord — the class takes no PCM, no controller, no SM,
 * so it is testable by direct ALEWord injection with zero mocking (mirrors
 * WordGridTracker's DecodedCandidate seam, Modem/word_grid_tracker.h).
 *
 * ALEController usage (single-threaded, called from on_received_word() and
 * tick_frame_settle()):
 *   // on_received_word() / rx_accumulate_sounding():
 *   if (auto flushed = sounding_accumulator_.on_word(word, freq_hz, now_ms_))
 *       commit_sounding_result(*flushed);
 *
 *   // tick_frame_settle():
 *   if (sounding_accumulator_.timed_out(now_ms))
 *       if (auto r = sounding_accumulator_.finalize())
 *           commit_sounding_result(*r);
 */

#pragma once

#include "Word/ale_word.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ale {

class SoundingIdentityAccumulator {
public:
    /// A fully reassembled, session-averaged sounding observation, ready to
    /// commit to the LQA DB.
    struct Result {
        std::string station;
        uint32_t    frequency_hz    = 0;
        float       snr_db          = 0.0f;
        float       ber             = 0.0f;
        float       sinad_db        = 0.0f;
        bool        twas_conclusion = false; ///< true=TWAS (not available), false=TIS (available)
    };

    /**
     * Feed one received word (already filtered by the caller to lqa_enabled
     * + SCANNING/IDLE state + a resolved rx frequency; see
     * ALEController::rx_accumulate_sounding()).
     *
     * \param word          Decoded word from on_received_word().
     * \param frequency_hz  Current channel's rx frequency (a session's
     *                      freq_hz is captured from this at session-open
     *                      time only).
     * \param now_ms        Current monotonic time.
     * \return A Result if processing this word caused an in-progress session
     *         for a DIFFERENT station to be flushed (TIS/TWAS anchor
     *         mismatch) — caller must commit it via commit_sounding_result().
     *         Empty otherwise (including the common case of the word being
     *         folded into the still-open session).
     */
    std::optional<Result> on_word(const ALEWord& word, uint32_t frequency_hz, uint32_t now_ms);

    /// True if a session is open and has been silent for >= Tdrw_ms
    /// (ALETimingConstants::Tdrw_ms, 784ms — the sole inactivity signal;
    /// no separate/longer timeout is used).
    bool timed_out(uint32_t now_ms) const;

    /**
     * Finalize and clear the current session. Caller must have checked
     * timed_out() (or be handling the on_word() anchor-mismatch flush
     * return) first.
     * \return The reassembled Result, or empty if no session was open (or,
     *         defensively, if the session's anchor content was empty).
     */
    std::optional<Result> finalize();

private:
    // MIL-STD-188-141B caps addresses at 5 words / 15 chars: anchor + up to
    // 4 extension words (DATA,REP,DATA,REP by position).
    static constexpr int kMaxExtSlots = 4;
    static const uint32_t kTdrwMs; // = ALETimingConstants::Tdrw_ms (defined in .cpp to keep ale_timing.h out of this header)

    struct Session {
        std::string anchor;                          ///< session-open TIS/TWAS content; fixed for the session's life
        uint32_t    freq_hz      = 0;
        bool        twas         = false;
        std::map<std::string, uint32_t> slot_votes[kMaxExtSlots]; ///< per-slot content -> cumulative unanimous_votes weight
        int         next_slot    = 1;                 ///< 1-based in-cycle position counter (odd=DATA, even=REP)
        bool        cycle_open   = true;               ///< false once this cycle has seen a slot mismatch/invalid word
        float       snr_sum      = 0.0f;
        float       ber_sum      = 0.0f;
        float       sinad_sum    = 0.0f;
        uint32_t    word_count   = 0;
        uint32_t    last_word_ms = 0;
    };

    std::optional<Session> session_;

    static PreambleType expected_type_for_slot(int slot_1based);
    static void fold_linear(Session& s, const ALEWord& word);
    void open_session(const ALEWord& word, const std::string& anchor_content,
                       uint32_t frequency_hz, uint32_t now_ms);
    static Result build_result(const Session& s);
};

} // namespace ale
