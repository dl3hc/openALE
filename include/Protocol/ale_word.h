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
 * Word preamble types per MIL-STD-188-141B (low-level encoding layer)
 */
enum class PreambleType : uint8_t {
    DATA = 0,
    THRU = 1,
    TO   = 2,
    TWS  = 3,
    FROM = 4,
    TIS  = 5,
    CMD  = 6,
    REP  = 7,
    UNKNOWN = 0xFF
};

/**
 * \struct Word
 * Low-level decoded ALE word with raw bits and FEC metadata
 */
struct Word {
    uint32_t     raw_bits;
    uint32_t     corrected_bits;
    PreambleType preamble;
    uint32_t     payload;
    uint8_t      error_count;
    bool         crc_valid;
    uint32_t     word_index;
};

/**
 * \enum WordType
 * Preamble types per MIL-STD-188-141B Table A-II
 */
enum class WordType : uint8_t {
    DATA = 0,    ///< Data word  — uses Expanded 64 character set
    THRU = 1,    ///< Through word (repeater) — uses Basic 38
    TO   = 2,    ///< To address  — uses Basic 38
    TWS  = 3,    ///< To With Self — uses Basic 38
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
    WordType type;        ///< Preamble type (3 bits)
    char     address[4];  ///< 3 decoded characters + null terminator
    uint32_t raw_payload; ///< Raw 21-bit payload
    uint8_t  fec_errors;  ///< Golay errors corrected
    bool     valid;       ///< Word passed FEC and character validation
    uint32_t timestamp_ms;///< Reception timestamp (ms)

    ALEWord() : type(WordType::UNKNOWN), raw_payload(0), fec_errors(0),
                valid(false), timestamp_ms(0) {
        address[0] = address[1] = address[2] = address[3] = '\0';
    }
};

/**
 * \class WordParser
 * Parse ALE words from decoded symbols.
 *
 * Character-set selection is automatic: routing preambles (TO, TWS, FROM,
 * TIS, THRU, CMD) validate against Basic 38; DATA and REP words validate
 * against Expanded 64.
 */
class WordParser {
public:
    WordParser();

    /**
     * Parse 49 symbols into an ALE word.
     * Applies majority voting, then extracts preamble and payload.
     *
     * \param symbols      Array of 49 detected symbols (0-7)
     * \param output [out] Decoded ALE word
     * \param timestamp_ms Reception timestamp in milliseconds
     * \return true if parsing successful
     */
    bool parse_word(const uint8_t symbols[SYMBOLS_PER_WORD],
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
    static WordType extract_preamble(uint32_t word_bits);

    /**
     * Extract 21-bit payload from 24-bit word (bits 20-0).
     */
    static uint32_t extract_payload(uint32_t word_bits);

    /**
     * Decode a 21-bit payload to 3 characters.
     *
     * The character set used for validation depends on the word type:
     *  - Basic 38   for routing preambles (TO, FROM, TWS, TIS, THRU, CMD)
     *  - Expanded 64 for data words (DATA, REP)
     *
     * \param payload     21-bit payload
     * \param word_type   Preamble type (drives character-set selection)
     * \param output [out] 4-byte buffer (3 chars + null)
     * \return true if all three characters are valid for the given word type
     */
    static bool decode_ascii(uint32_t payload,
                              WordType  word_type,
                              char      output[4]);

    /**
     * Encode 3 characters to a 21-bit payload.
     *
     * \param chars     Exactly 3 characters to encode
     * \param word_type Preamble type (drives character-set selection)
     * \return 21-bit payload, or 0xFFFFFFFF if any character is invalid
     */
    static uint32_t encode_ascii(const char chars[3], WordType word_type);

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
    static bool uses_basic38(WordType type);

    /**
     * Get string name for word type.
     */
    static const char* word_type_name(WordType type);

    /**
     * Convenience factory: build an ALEWord from a preamble type and
     * exactly 3 address characters.  Characters must be valid for the
     * given word type (Basic 38 for routing words, Expanded 64 for DATA/REP).
     *
     * \param type   Word type (drives character-set selection)
     * \param chars  Exactly 3 characters; must be valid for \p type
     * \return Valid ALEWord on success; ALEWord with valid==false on invalid chars
     */
    static ALEWord make_word(WordType type, const char chars[3]);

private:
    uint32_t last_timestamp_ms; ///< Timestamp of most recently parsed word
};

/**
 * \class AddressBook
 * Manage ALE addresses (self, other stations, nets).
 *
 * All addresses are constrained to the Basic 38 ASCII subset and
 * 3-15 characters per MIL-STD-188-141B A.5.2.4.2.
 */
class AddressBook {
public:
    AddressBook();

    /**
     * Set self address (this station's call sign).
     * Must be 3-15 Basic 38 characters.
     * \return true if valid and set
     */
    bool set_self_address(const std::string& address);

    /** Get self address. */
    std::string get_self_address() const { return self_address; }

    /**
     * Add a known station address.
     * \param address Station address (Basic 38, 3-15 chars)
     * \param name    Optional friendly name
     */
    void add_station(const std::string& address, const std::string& name = "");

    /**
     * Add a net (group) address.
     * \param net_address Net address (Basic 38, 3-15 chars)
     * \param description Optional description
     */
    void add_net(const std::string& net_address,
                 const std::string& description = "");

    /** Return true if address matches the self address exactly. */
    bool is_self(const std::string& address) const;

    /** Return true if address is in the known-station list. */
    bool is_known_station(const std::string& address) const;

    /** Return true if address is a known net. */
    bool is_known_net(const std::string& address) const;

    /**
     * Match an address against a pattern that may contain wildcard characters.
     *
     * Per MIL-STD-188-141B A.5.2.4.2:
     *  '@' matches exactly one Basic 38 character at that position.
     *  '?' is reserved for special functions (not a positional wildcard here).
     *
     * Pattern and address must have the same length; each '@' in the
     * pattern accepts any single Basic 38 character in the address.
     *
     * \param pattern Pattern string (Basic 38 + '@')
     * \param address Address to match (Basic 38 only)
     * \return true if pattern matches address
     */
    static bool match_wildcard(const std::string& pattern,
                                const std::string& address);

private:
    std::string self_address;
    std::vector<std::pair<std::string, std::string>> stations; ///< address, name
    std::vector<std::pair<std::string, std::string>> nets;     ///< net, description
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
     * word (TO, TIS, TWS, FROM, CMD).  Conformant receivers ignore calls that
     * use the local address in a THRU word outside the scanning section.
     * \return true if all THRU words appear before the first TO/TIS/TWS/FROM/CMD.
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
     * AC-WORD-008-5: The first CMD in a frame ends the Calling Cycle and
     * marks the beginning of the Message section.
     * \return true if the first CMD word is at the beginning of the frame.
     */
    static bool first_cmd_begins_message_section(const std::vector<ALEWord>& words);

    // ── REQ-WORD-010 (REP sequence rules) ──────────────────────

    /**
     * AC-WORD-010-6: REP must not follow itself, TIS, or TWAS.
     * \return true if REP is not directly preceded by itself, TIS, or TWAS.
     */
    static bool rep_not_followed_by_self_tis_twas(const std::vector<ALEWord>& words);

    /**
     * AC-WORD-010-7: REP must not be used where multiple senders exist.
     * \return true if REP is not used in a multiple sender situation.
     */
    static bool rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words);
};

} // namespace ale