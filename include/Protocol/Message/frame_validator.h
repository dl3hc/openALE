/**
 * \file Protocol/Message/frame_validator.h
 * \brief ALE frame-structure validators (whole-frame conformance checks).
 *
 * FrameValidator operates on a std::vector<ALEWord> — a complete or partial
 * ALE frame — and applies the MIL-STD-188-141B A.5.2.3.2 structural constraints
 * (THRU / FROM / TIS / TWAS / CMD / DATA / REP placement) that a receiver or
 * transmitter must enforce.  It is a frame-level utility, not a word-level one,
 * so it lives with the message/protocol layer rather than the Word module.
 *
 * Specification: MIL-STD-188-141B Appendix A.
 */

#pragma once

#include "Word/ale_word.h"
#include "Word/frame_catalog.h"
#include <optional>
#include <string>
#include <vector>

namespace ale {

/**
 * \class FrameValidator
 * Word-sequence validators for ALE frame compliance.
 *
 * Per MIL-STD-188-141B A.5.2.3.2, THRU and FROM have structural constraints
 * on where they may appear within a frame word sequence.  These static methods
 * provide the conformance checks that receivers (and transmitters) must apply.
 */
class FrameValidator {
public:
    // ── REQ-WORD-007 (FROM position) ────────────────────────────────────────

    /**
     * AC-WORD-007-4: FROM appears at most once per ALE frame (A.5.2.3.2.5).
     * \return true if the sequence contains zero or one FROM words.
     */
    static bool from_count_valid(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-007-5/7: Every FROM in the sequence must be immediately
     * followed by CMD (possibly after DATA/REP address extension words).
     * Conformant systems ignore frames where FROM appears elsewhere.
     * \return true if all FROM words lead (via optional DATA/REP) to CMD.
     */
    static bool from_precedes_cmd_only(const std::vector<ALEWord>& words);

    // ── REQ-WORD-004 / REQ-WORD-005 (TIS / TWAS mutual exclusion) ──────────

    /**
     * AC-WORD-004-5 / AC-WORD-005-5: TIS and TWAS must not both appear in
     * the same ALE frame (A.5.2.3.2.2 / A.5.2.3.2.3).
     * \return true if the sequence does not contain both TIS and TWAS.
     */
    static bool tis_twas_mutually_exclusive(const std::vector<ALEWord>& words);

    // ── REQ-WORD-006 (THRU / group-call scanning section) ───────────────────

    /**
     * AC-WORD-006-1/7: THRU must not appear after the scanning section ends.
     * The scanning section is the initial portion before any leading/conclusion
     * word (TO, TIS, TWAS, FROM, CMD).  Conformant receivers ignore calls that
     * use the local address in a THRU word outside the scanning section.
     * \return true if all THRU words appear before the first TO/TIS/TWAS/FROM/CMD.
     */
    static bool thru_in_scanning_section_only(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-006-2: In a group-call scanning section THRU and REP must
     * alternate, starting with THRU, forming complete THRU-REP pairs.
     * \param scanning_words  Words from the scanning section (THRU/REP pairs).
     * \return true if the sequence consists of complete THRU, REP pairs.
     */
    static bool thru_rep_alternates(const std::vector<ALEWord>& scanning_words);

    /**
     * AC-WORD-006-4: A group call contains at most 5 different first address
     * words (distinct THRU targets) per A.5.2.3.2.4.
     * \param scanning_words  Words from the scanning section.
     * \return true if the number of distinct THRU target addresses is <= 5.
     */
    static bool group_call_target_count_valid(const std::vector<ALEWord>& scanning_words);

    // ── REQ-WORD-008 (CMD Sequence) ────────────────────────────────────

    /**
     * AC-WORD-008-1: Every Message section begins with CMD.
     * \return true if all message sections (after CMD) start with CMD.
     */
    static bool message_sections_begin_with_cmd(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-008-3: CMD is only used in the Message section — it must not
     * appear before the address section (TO/FROM/TIS/TWAS) has started.
     * \return true if no CMD precedes the first address-section word.
     */
    static bool cmd_not_before_address_section(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-008-4: A frame containing CMD must have a preceding call
     * (TO/FROM/TIS/TWAS before CMD) and a following conclusion (TIS/TWAS after CMD).
     * \return true if every CMD in the frame satisfies both conditions.
     */
    static bool cmd_has_call_and_conclusion(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-008-5: The first CMD in a frame ends the Calling Cycle and
     * marks the beginning of the Message section.
     * \return true if no calling-section word types (TO, FROM, TIS, THRU) appear
     *         after the first CMD in the frame.
     */
    static bool first_cmd_begins_message_section(const std::vector<ALEWord>& words);

    // ── REQ-WORD-009 (DATA sequence rules) ─────────────────────

    /**
     * AC-WORD-009-1: DATA must not directly follow another DATA word.
     * \return true if no DATA word is immediately preceded by DATA.
     */
    static bool data_not_after_data(const std::vector<ALEWord>& words);

    // ── REQ-WORD-010 (REP sequence rules) ──────────────────────

    /**
     * AC-WORD-010-6: REP must not be directly preceded by itself, TIS, or TWAS.
     * \return true if no REP word is immediately preceded by REP, TIS, or TWAS.
     */
    static bool rep_not_preceded_by_self_tis_twas(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-010-2/3: Consecutive words in a frame must have different preamble
     * types. Any data change requires a preamble change; a preamble change is also
     * required even when the data field is identical.
     * \return true if no two adjacent words share the same preamble type.
     */
    static bool no_consecutive_same_preamble(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-010-7: REP must not be used where multiple senders exist.
     *
     * \note Always returns true. Detecting a "multiple sender situation"
     *       requires cross-station protocol state (net-call membership,
     *       simultaneous transmissions) that is not available to a single-frame
     *       validator. This check must be enforced at the ALEStateMachine level
     *       when net-call support (CallingPhase::NET_CALL_STUB) is implemented.
     *
     * \return true (unconditional — see note above)
     */
    static bool rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words);

    // ── REQ-FRAME-013 (address section size) ───────────────────────────

    /**
     * AC-FRAME-006-002: Each individual/net address section must not exceed
     * 5 words (Ta max = 5 × Trw = 1960 ms, Table A-XII / A.5.2.4.2).
     *
     * An address sequence begins on any anchor word (TO, TIS, TWAS, FROM, THRU)
     * and extends through any immediately following DATA / REP extension words.
     * The validator checks every such sequence within the word list and returns
     * false if any single sequence exceeds TA_MAX_WORDS = 5.
     *
     * \param words  Complete word sequence of the ALE frame.
     * \return true if every address sequence in the frame contains ≤ 5 words.
     */
    static bool address_section_word_count_valid(const std::vector<ALEWord>& words);

    // ── Address reconstruction ──────────────────────────────────

    /**
     * Reconstruct destination addresses from a TO / DATA / REP word sequence.
     *
     * Encoding rules (A.5.2.3.2.1 / A.5.2.3.4):
     *  - TO starts a new recipient address (3 chars).
     *  - DATA after any non-DATA word extends the current address (+3 chars).
     *  - REP directly after TO (no DATA since last TO) → new recipient.
     *  - REP after DATA → extends current address (repeats DATA function).
     *  - '@' padding characters (A.5.2.4.3) are stripped from each result.
     *
     * \return Ordered list of reconstructed address strings.
     */
    static std::vector<std::string> reconstruct_to_addresses(const std::vector<ALEWord>& words);

    // ── OFS catalog-frame validation (docs/FRAMING_STANDARD.md §7, FR-09) ───
    //
    // The per-rule checks above validate isolated structural properties and
    // are applied selectively (some only to scanning sections, some only to
    // logical word lists — several are not redundancy-safe and would reject
    // legal repeated/doubled TX sections). validate_frame() is the TX-side
    // grammar gate: a section-aware single pass over a COMPLETE frame that
    // the FrameBuilder catalog constructors run before encoding (FR-09 —
    // the spec's "INVALID ADDRESS SEQUENCE!" flowchart exits, A.5.2.5.1/3,
    // become a hard failure at build time).

    /**
     * Validate a complete frame of the given catalog type against the OFS
     * grammar rules:
     *
     *   FR-05  TIS and TWAS never occur in one frame (A.5.2.5.3).
     *   FR-03  Every address run (anchor + DATA/REP extensions, outside the
     *          message section) is ≤ 5 words / 15 chars (A.5.2.4.4).
     *   FR-03  Extensions alternate DATA, REP, DATA, REP — DATA directly
     *          after DATA is rejected (A.5.2.4.4.2). REP after REP is legal
     *          in the group leading call (REP = new-recipient marker,
     *          A.5.5.4.3.2); for TIS/TWAS conclusions the first extension is
     *          DATA, never REP (A.5.2.5.3).
     *   FR-03  '@' stuffing appears only in the LAST extension word of an
     *          address run (A.5.2.4.3). Anchor words are exempt — the special
     *          addresses (@?@, @@?, ?@?, @A@, @@A, @@@) legitimately carry
     *          '@' in the anchor itself (A.5.2.4.7-12).
     *   §6.1   Payload blocks: a non-DTM/DBM (i.e. AMD-family) block carries
     *          ≤ 29 DATA/REP words — 90 chars of message text including the
     *          first 3 the CMD word carries (A.5.7.2.3 Tm max; "59 words
     *          counting CMD" = 29 data + 30 CMD); ≤ 30 CMD words per frame.
     *          DTM/DBM blocks are exempt from the data-word cap here — their
     *          size rules live in their own (partially implemented)
     *          protocols per FR-11.
     *
     * Redundancy is transport, not content (FR-02): doubled leading calls,
     * repeated scanning words and repeated sound conclusions all pass. A
     * DATA/REP before any anchor, or an empty word list, is rejected.
     *
     * \return std::nullopt when the frame is legal; otherwise a short error
     *         description naming the violated rule (for the caller to log or
     *         refuse with).
     */
    static std::optional<std::string> validate_frame(FrameType type,
                                                     const std::vector<ALEWord>& words);
};

} // namespace ale