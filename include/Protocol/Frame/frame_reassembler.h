/**
 * \file frame_reassembler.h
 * \brief OFS FrameReassembler — single RX grammar parser (FR-01..FR-05).
 *
 * docs/FRAMING_STANDARD.md §5/§8: roles come from parse position, not from
 * ad-hoc state flags. One instance observes the received word stream and
 * emits complete Frame events; the four ad-hoc accumulators it will absorb
 * (MessageAssembler, the classify() collecting gates, linked_twas_addr_,
 * SoundingIdentityAccumulator) keep running unchanged until Phase 3/4 —
 * Phase 2 is shadow mode: the SM feeds the stream, nothing consumes the
 * output (only frame-level trace logging, FR-10).
 *
 * Pull model, no callbacks: consumers (tests now, the context matrix in
 * Phase 3) query the in-progress candidate and drain completed frames.
 *
 * Grammar rules implemented:
 *   FR-01  Frame boundary: (a) Tdrw of silence after the last accepted word,
 *          (b) the first word that cannot extend the current grammar closes
 *          the candidate and starts a fresh one at that word. Mid-frame
 *          acquisition is legal input (A.5.2.5.1) — a candidate that never
 *          saw a calling cycle is marked mid_frame.
 *          Exception (self-declared blocks): a DBM block declares its own
 *          length; silence inside the declared block would be a fade, not a
 *          boundary. openALE's DBM CMD carries an ASCII "DBM" marker, not
 *          spec BC bits (deep-interleave demod does not exist), so the
 *          accounting hook (Kind::DBM) exists but no timing behavior —
 *          nothing here contradicts the rule for when it does.
 *   FR-02  Redundancy collapse: a word identical (type + payload) to the
 *          immediately preceding accepted word is transport repetition —
 *          it refreshes the silence gate but does not advance the grammar.
 *   FR-03  Address accumulation: an anchor (TO/THRU/FROM/TIS/TWAS) opens a
 *          run; following DATA/REP extend it ('@'/' ' trimmed) until the
 *          next non-extension word or frame end. The run never closes
 *          before frame end — an extension may arrive at any point inside
 *          the frame (the LINKED termination accumulation relies on this).
 *          A 6th word in a run is out-of-grammar (FR-01(b)).
 *   FR-04  Extension attribution: a DATA/REP arriving more than Tdrw
 *          (2×Trw) after the last accepted word does not extend anything —
 *          it belongs to a later frame (drop toward no interpretation).
 *   FR-05  Conclusion: TIS/TWAS carries the sender's whole address; a
 *          second, different conclusion word is out-of-grammar (FR-01(b))
 *          — the candidate up to that word is complete, the new word opens
 *          a fresh one. On RX the first extension after TIS/TWAS is
 *          accepted as REP too (not DATA-only as on TX): the shipped
 *          classify()/react paths accumulate REP identically, and the
 *          fail-safe direction (FR-08) is to accept, not to discard.
 *   §6.1   Payload blocks: a CMD word opens a block (AMD/DTM/DBM/LQA kind
 *          heuristic from the CMD characters — ambiguous first-chars are
 *          documented at the heuristic); block words route to the owning
 *          block only (FR-11) and never to address accumulation (FR-04).
 *          A CMD after the conclusion is out-of-grammar — the message
 *          section precedes the conclusion (§2) — so it closes the
 *          candidate and opens a fresh (mid-frame) one.
 *
 * Frame typing is grammar-only: F_SOUND (conclusion-only, incl. mid-frame —
 * the 2026-08-31 incident's sounding is F-06 BY GRAMMAR), F_ALLCALL ('@'
 * addressee), F_INLINK ('?@?'), F_CALL (THRU seen — group), F_ORDERWIRE
 * (addressee + payload + conclusion; the Ion2G AMD calling frame shares
 * this grammar — the §8 context matrix decides in Phase 3), F_RESPONSE
 * (addressee + conclusion: the shared F-01(C=0)/F-03/F-04/F-05 grammar —
 * reception context is the ONLY disambiguator, OFS §6 note), F_LQA (CMD-only
 * broadcast, e.g. the noise word after a sound). Incomplete candidates
 * close as UNTAGGED — consumers discard (FR-08).
 */

#pragma once

#include "Word/ale_word.h"
#include "Word/frame_catalog.h"
#include <string>
#include <vector>

namespace ale {

/// Grammar role of one received word, from parse position (FR-01..05).
enum class ParseRole {
    NONE,               ///< Cannot extend any open structure — dropped (FR-08)
    SCAN_WORD,          ///< Repetition of the candidate's leading anchor (FR-02 transport)
    ADDRESS_ANCHOR,     ///< Anchor word, or REP acting as a new group recipient
    ADDRESS_EXTENSION,  ///< DATA/REP extending the open address run (FR-03)
    MESSAGE_CMD,        ///< CMD word opening a payload block (FR-11)
    MESSAGE_DATA,       ///< DATA/REP inside an open payload block (FR-04)
};

/// One CMD-opened payload block (§6.1 P-rows). Kind is a heuristic from the
/// CMD word's three characters: "DTM"/"DBM" identify their protocols (and
/// DBM marks the self-declared-block accounting hook); a leading 'a'/'r'/'n'
/// is openALE's LQA family; anything else is treated as AMD. An AMD message
/// whose first characters collide with one of these markers is typed by the
/// wrong row — payload routing is refined by reception context in Phase 3
/// (FR-11 gives the block's owning CMD precedence either way).
struct PayloadBlock {
    enum class Kind { AMD, DTM, DBM, LQA, OTHER };
    Kind                kind = Kind::AMD;
    char                cmd_chars[4] = {'\0','\0','\0','\0'};
    std::vector<ALEWord> data;      ///< Block payload words (CMD excluded)
};

/// A complete frame event — the candidate's accumulated state at a boundary.
struct AssembledFrame {
    FrameType type = FrameType::UNTAGGED;  ///< Grammar classification (see header)
    bool      complete = false;            ///< A conclusion (TIS or TWAS) was seen
    bool      mid_frame_acquisition = false; ///< No calling cycle observed (A.5.2.5.1)
    std::vector<std::string> addressed_to; ///< Completed TO/THRU/REP-recipient runs.
                                           ///< TO anchors are addressees grammatically —
                                           ///  scan-vs-leading distinction is timing, not
                                           ///  identity, so a collapsed scanning+leading
                                           ///  merge lands here too.
    std::string quick_id;                 ///< FROM identity, if present
    std::string conclusion_identity;      ///< Sender's whole address (FR-03/05)
    bool        conclusion_is_twas = false;
    std::vector<PayloadBlock> blocks;     ///< §6.1 payload blocks in order
    uint32_t    first_word_ms = 0;
    uint32_t    last_word_ms  = 0;
    size_t      word_count = 0;           ///< Physical words accepted into the frame
    size_t      logical_word_count = 0;   ///< Post-collapse (FR-02)
    bool        closed_by_out_of_grammar = false; ///< FR-01(b) vs Tdrw settle
};

/**
 * \class FrameReassembler
 * Single-pass RX grammar parser producing Frame events. Stateless between
 * frames except for the one in-progress candidate; no callbacks, no logging
 * — consumers pull via candidate()/take_completed().
 */
class FrameReassembler {
public:
    /**
     * Feed one accepted word at reception time @p now_ms.
     * Only valid words belong on this stream (the SM's classify() sees the
     * same stream; invalid words are FEC events, not grammar input).
     * \return The grammar role assigned to the word.
     */
    ParseRole on_word(const ALEWord& w, uint32_t now_ms);

    /**
     * Advance time: closes the candidate at Tdrw of silence (FR-01(a)).
     * Call from the same clock that drives the SM's handle_*() settlers.
     */
    void tick(uint32_t now_ms);

    /// In-progress candidate (nullptr before the first word / after a close).
    const AssembledFrame* candidate() const { return active_ ? &candidate_storage_ : nullptr; }

    /// Drain frames completed at boundaries, oldest first (shadow mode:
    /// unconsumed frames are capped, the oldest dropped).
    std::vector<AssembledFrame> take_completed();

    /// Role assigned to the most recently fed word (test/equivalence use).
    ParseRole last_role() const { return last_role_; }

    /// Drop all state (e.g. on emergency reset). Phase 3 adds channel-hop
    /// boundaries; FR-01(b) already restarts on out-of-grammar collisions.
    void reset();

private:
    static constexpr size_t MAX_RUN_WORDS  = 5;    // A.5.2.4.4 (15 chars)
    static constexpr size_t MAX_COMPLETED_ = 32;   // shadow-mode cap

    struct Run {
        PreambleType anchor = PreambleType::UNKNOWN; ///< Anchor preamble (REP-as-TO keeps TO semantics)
        std::string  text;                 ///< Accumulated identity, '@'/' ' trimmed
        size_t       words = 0;            ///< Anchor + extensions
        bool         saw_data = false;     ///< A DATA extension arrived (REP then extends)
        bool         is_scanning = false;  ///< Opened while still in the scanning section
    };

    // Candidate state (see AssembledFrame for the accumulated results)
    bool          active_ = false;
    AssembledFrame candidate_storage_;
    Run           run_;                    ///< Open address run (may be "closed" logically
                                           ///  but extensions still attach until frame end)
    bool          run_open_ = false;
    bool          conclusion_seen_ = false;
    bool          block_open_ = false;
    bool          seen_thru_ = false;
    PreambleType last_word_type_ = PreambleType::UNKNOWN;
    uint32_t      last_word_payload_ = 0;
    uint32_t      last_accepted_ms_ = 0;
    ParseRole     last_logical_role_ = ParseRole::NONE;  ///< Role of the last non-repetition word
    ParseRole     last_role_ = ParseRole::NONE;

    std::vector<AssembledFrame> completed_;

    void start_candidate_(const ALEWord& w, uint32_t now_ms);
    void close_candidate_(bool out_of_grammar, uint32_t now_ms);
    void assign_frame_type_(AssembledFrame& f) const;
    void push_completed_(AssembledFrame&& f);
};

} // namespace ale