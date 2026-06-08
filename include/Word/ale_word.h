/**
 * \file ale_word.h
 * \brief ALE word structure and parser
 *
 * Implements MIL-STD-188-141B word structure:
 *  - 24 bits total after majority voting
 *  - 3-bit preamble (word type), bits 23-21
 *  - 21-bit payload (3 × 7-bit characters), bits 20-0
 *
 * Two character sets per MIL-STD-188-141B:
 *  - Basic 38   (A.5.2.4.2): A-Z, 0-9, '@', '?' — routing/addressing only
 *  - Expanded 64 (A.5.7.2.1): all 7-bit codepoints with b7b6 = 01 or 10
 *                              (0x20-0x5F) — DATA/REP orderwire words only
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "FSK/ale_waveform.h"
#include <string>
#include <cstdint>
#include <vector>

namespace ale {

// Word bit-field structure (MIL-STD-188-141B A.5.1.2)
constexpr uint32_t PREAMBLE_BITS = 3;
constexpr uint32_t PAYLOAD_BITS  = 21;
constexpr uint32_t WORD_BITS     = PREAMBLE_BITS + PAYLOAD_BITS;

// Golay (24,12) FEC parameters
constexpr uint32_t GOLAY_CODEWORD_BITS = 24;
constexpr uint32_t GOLAY_INFO_BITS     = 12;
constexpr uint32_t GOLAY_PARITY_BITS   = 12;
constexpr uint32_t MAX_GOLAY_ERRORS    = 3;

/**
 * \enum PreambleType
 * Word preamble types per MIL-STD-188-141B Table A-II
 */
enum class PreambleType : uint8_t {
    DATA = 0,    ///< Data word  — uses Expanded 64 character set
    THRU = 1,    ///< Through word (repeater) — uses Basic 38
    TO   = 2,    ///< To address  — uses Basic 38
    TWAS = 3,    ///< Terminator and identification quitting — uses Basic 38
    FROM = 4,    ///< From address — uses Basic 38
    TIS  = 5,    ///< This Is Self — uses Basic 38
    CMD  = 6,    ///< Command word — uses Basic 38
    REP  = 7,    ///< Repeat (retransmit DATA) — uses Expanded 64
    UNKNOWN = 0xFF
};

/**
 * \struct ALEWord
 * Decoded ALE word with preamble and payload
 */
struct ALEWord {
    PreambleType type;       ///< Preamble type (3 bits)
    char         address[4]; ///< 3 decoded characters + null terminator
    uint32_t     raw_payload;    ///< Raw 21-bit payload
    uint8_t      fec_errors;     ///< Golay errors corrected
    uint8_t      unanimous_votes;///< 2/3-voter unanimous count 0..48 (A.5.2.6.3)
    bool         valid;          ///< Word passed FEC and character validation
    uint32_t     timestamp_ms;   ///< Reception timestamp (ms)

    ALEWord() : type(PreambleType::UNKNOWN), raw_payload(0), fec_errors(0),
                unanimous_votes(0), valid(false), timestamp_ms(0) {
        address[0] = address[1] = address[2] = address[3] = '\0';
    }

    /**
     * Encode this word for transmission (A.5.2.2.2 / A.5.2.2.3).
     * Applies Golay (24,12) to both halves, inverts Coder-B check bits,
     * interleaves A/B channels, and appends S49=0.
     * \return 49-bit transmitted word ready for FSK modulation.
     */
    uint64_t encode() const;
};

/**
 * \class WordParser
 * Parse ALE words from decoded symbols.
 *
 * Character-set selection is automatic: routing preambles (TO, TWAS, FROM,
 * TIS, THRU, CMD) validate against Basic 38; DATA and REP words validate
 * against Expanded 64.
 */
class WordParser {
public:
    WordParser();

    /**
     * Parse one received word from a triple-copy vote buffer.
     * Applies majority voting, then extracts preamble and payload.
     *
     * \param symbols      WordVoteBuffer with 3 × 49 received symbols
     * \param output [out] Decoded ALE word
     * \param timestamp_ms Reception timestamp in milliseconds
     * \return true if word passed FEC, character validation, and vote quality
     *         threshold (unanimous_votes >= VOTE_THRESHOLD_BAD, A.5.2.6.3)
     */
    bool parse_word(const WordVoteBuffer& symbols,
                    ALEWord& output,
                    uint32_t timestamp_ms = 0);

    /**
     * Parse from a raw 24-bit word (after majority voting).
     *
     * \param word_bits    24-bit decoded word
     * \param output [out] Decoded ALE word
     * \param timestamp_ms Reception timestamp in milliseconds
     * \return true if parsing successful
     */
    bool parse_from_bits(uint32_t word_bits,
                         ALEWord& output,
                         uint32_t timestamp_ms = 0);

    /**
     * Extract preamble type from 24-bit word (bits 23-21).
     */
    static PreambleType extract_preamble(uint32_t word_bits);

    /**
     * Extract 21-bit payload from 24-bit word (bits 20-0).
     */
    static uint32_t extract_payload(uint32_t word_bits);

    /**
     * Decode a 21-bit payload to 3 characters.
     *
     * The character set used for validation depends on the word type:
     *  - Basic 38   for routing preambles (TO, FROM, TWAS, TIS, THRU, CMD)
     *  - Expanded 64 for data words (DATA, REP)
     *
     * \param payload     21-bit payload
     * \param word_type   Preamble type (drives character-set selection)
     * \param output [out] 4-byte buffer (3 chars + null)
     * \return true if all three characters are valid for the given word type
     */
    static bool decode_ascii(uint32_t payload,
                              PreambleType  word_type,
                              char      output[4]);

    /**
     * Encode 3 characters to a 21-bit payload.
     *
     * \param chars     Exactly 3 characters to encode
     * \param word_type Preamble type (drives character-set selection)
     * \return 21-bit payload, or 0xFFFFFFFF if any character is invalid
     */
    static uint32_t encode_ascii(const char chars[3], PreambleType word_type);

    // ------------------------------------------------------------------
    // Character-set predicates (A.5.2.4.2 and A.5.7.2.1)
    // ------------------------------------------------------------------

    /**
     * Validate a character for the Basic 38 ASCII subset (A.5.2.4.2).
     * Valid: A-Z, 0-9, '@', '?'
     * Used for all routing/addressing preambles.
     */
    static bool is_valid_basic38_char(char ch);

    /**
     * Validate a character for the Expanded 64 ASCII subset (A.5.7.2.1).
     * Valid: all 7-bit codepoints with b7=0 and b6=1 or b5=1, i.e. 0x20-0x5F.
     * Per spec: digital discrimination by the two MSBs b7,b6 = 01 or 10.
     * Used for DATA and REP words (orderwire / AMD messages).
     */
    static bool is_valid_expanded64_char(char ch);

    /**
     * Return true if the word type uses the Basic 38 character set.
     * DATA and REP use Expanded 64; all other types use Basic 38.
     */
    static bool uses_basic38(PreambleType type);

    /**
     * Get string name for word type.
     */
    static const char* word_type_name(PreambleType type);

    /**
     * Convenience factory: build an ALEWord from a preamble type and
     * exactly 3 address characters.  Characters must be valid for the
     * given word type (Basic 38 for routing words, Expanded 64 for DATA/REP).
     *
     * \param type   Word type (drives character-set selection)
     * \param chars  Exactly 3 characters; must be valid for \p type
     * \return Valid ALEWord on success; ALEWord with valid==false on invalid chars
     */
    static ALEWord make_word(PreambleType type, const char chars[3]);

private:
    uint32_t last_timestamp_ms; ///< Timestamp of most recently parsed word
};

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

    // ── REQ-WORD-008 (CMD sequence) ────────────────────────────────────

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
};

} // namespace ale