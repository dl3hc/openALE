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
    uint8_t      fec_errors;          ///< Golay errors corrected (0 on uncorrectable)
    uint8_t      unanimous_votes;     ///< 2/3-voter unanimous count 0..48 (A.5.2.6.3)
    float        sinad_db;            ///< Goertzel SINAD averaged over 49 symbols (dB); A.5.4.1.2
    bool         golay_uncorrectable; ///< true if Golay could not correct either half (A.5.4.1.1)
    bool         valid;               ///< Word passed FEC and character validation
    uint32_t     timestamp_ms;        ///< Reception timestamp (ms)

    ALEWord() : type(PreambleType::UNKNOWN), raw_payload(0), fec_errors(0),
                unanimous_votes(0), sinad_db(0.0f), golay_uncorrectable(false),
                valid(false), timestamp_ms(0) {
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
     * DATA and REP use Expanded 64; CMD carries A.5.6 command payloads
     * (function code + binary argument fields) with no applicable char set,
     * accepted unconditionally in decode_ascii() and interpreted by the
     * protocol layer; all other routing types use Basic 38.
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

} // namespace ale