/**
 * \file address_encoder.h
 * \brief AddressEncoder — single source of truth for address-string → ALEWord conversion.
 *
 * ## Why this class exists
 *
 * MIL-STD-188-141B A.5.2.3.2.1 and A.5.2.4.3 define a fixed encoding rule
 * for ALE address fields:
 *
 *   1. The address is split into 3-character chunks.
 *      The last chunk is right-padded with the utility symbol '@' (0x40)
 *      if it contains fewer than 3 characters.
 *   2. The first chunk is placed in a word whose preamble type is the
 *      caller-specified "anchor type" (e.g. TO, TIS, TWAS, FROM …).
 *   3. Each subsequent chunk alternates between DATA and REP preambles.
 *
 * The same rule applies to every place in the protocol that transmits
 * an address: scanning call, leading call, conclusion, ACK, response,
 * sounding.  **The only difference between these sites is:**
 *
 *   - **Scanning call**  → always exactly the *first* word of the address.
 *     Per A.5.2.5.1, the scanning section carries only the first 3 chars
 *     regardless of the full address length.
 *   - **All other sites** → the *complete* address (1–5 words, 1–15 chars).
 *
 * This class provides both operations as named methods so the distinction
 * is always explicit at the call site:
 *
 * \code
 * // Scanning call — ALWAYS one word, regardless of address length:
 * transmit( AddressEncoder::encode_first("W1AWJ", WordType::TO) );
 * //  → TO:W1A   (only first 3 chars)
 *
 * // Leading call, conclusion, ACK, response, sounding — full address:
 * for (auto& w : AddressEncoder::encode("W1AWJ", WordType::TO))
 *     transmit(w);
 * //  → TO:W1A, DATA:WJ@   (all chunks)
 * \endcode
 *
 * ## Inverse relationship
 *
 * AddressEncoder::encode() is the exact inverse of
 * FrameValidator::reconstruct_to_addresses().  A roundtrip
 *   encode(addr, TO)  →  reconstruct_to_addresses()  →  {addr}
 * must always hold for any valid Basic-38 address.
 *
 * ## Group / Net calls
 *
 * encode_group() encodes multiple destination addresses into a single
 * interleaved word sequence using the same DATA/REP scheme:
 *
 * \code
 * encode_group({"BOB","SAM"},    TO) → [TO:BOB, REP:SAM]
 * encode_group({"BOB","SAMUEL"}, TO) → [TO:BOB, REP:SAM, DATA:UEL]
 * encode_group({"ROBERT","SAM"}, TO) → [TO:ROB, DATA:ERT, TO:SAM]
 * \endcode
 *
 * The "new recipient" marker is REP (when following a single-word address)
 * or a fresh anchor-type word (when following a multi-word address).
 * This matches the REP-interpretation logic in reconstruct_to_addresses().
 */

#pragma once

#include "Word/ale_word.h"
#include <string>
#include <vector>

namespace ale {

class AddressEncoder {
public:
    // -------------------------------------------------------------------------
    // Primary API — use these at every TX site in the protocol
    // -------------------------------------------------------------------------

    /**
     * Encode a complete address into a word sequence (1–5 words).
     *
     * Use this for: leading call, conclusion, ACK frame, response frame,
     * sounding, and any other site that must transmit the full address.
     *
     * \param addr            Address string (1–15 chars; truncated if longer).
     * \param first_word_type Preamble of the first word (TO, TIS, TWAS, FROM …).
     *
     * Examples with first_word_type = TO:
     *   "W1A"             →  [TO:W1A]
     *   "W1AW"            →  [TO:W1A, DATA:W@@]
     *   "EDWARD"          →  [TO:EDW, DATA:ARD]
     *   "CALLSIGNX"       →  [TO:CAL, DATA:LSI, REP:GNX]
     *   "VERYLONGCALLSIG" →  [TO:VER, DATA:YLO, REP:NGC, DATA:ALL, REP:SIG]
     */
    static std::vector<ALEWord> encode(const std::string& addr,
                                       WordType           first_word_type);

    /**
     * Return only the first word of the address encoding.
     *
     * Use this exclusively for the SCANNING_CALL phase (A.5.2.5.1).
     * The scanning section carries only the first 3 characters of the address —
     * DATA / REP extension words must not appear here regardless of address length.
     *
     * This is NOT a separate algorithm: it returns encode(addr, type).front().
     * Using this named method instead of encode().front() makes the intent
     * visible and prevents accidentally sending the full address in a scanning slot.
     *
     * \param addr            Address string (any length; only first 3 chars used).
     * \param first_word_type Preamble of the word (typically TO).
     *
     * Examples:
     *   "W1A"    → TO:W1A   (same as encode() — 1-word address)
     *   "W1AWJ"  → TO:W1A   (first 3 chars only; "WJ" is dropped)
     *   "EDWARD" → TO:EDW   (first 3 chars only; "ARD" is dropped)
     */
    static ALEWord encode_first(const std::string& addr,
                                WordType           first_word_type);

    /**
     * Encode multiple addresses into a single interleaved word sequence.
     *
     * Use this for group calls / net calls where multiple destination addresses
     * must be packed into the leading call section.
     *
     * Encoding strategy (exact inverse of reconstruct_to_addresses):
     *   - First address starts with first_word_type (anchor, e.g. TO).
     *   - If the previous address was a single-word address (last word type
     *     was the anchor): next address begins with REP (which reconstruct
     *     interprets as "new recipient" when last_non_rep == TO).
     *   - If the previous address was multi-word (last extension was DATA):
     *     next address begins with a fresh anchor-type word (TO), which
     *     reconstruct always starts a new recipient.
     *   - Extension words within each address always alternate DATA/REP.
     *
     * \param addrs           Ordered list of destination address strings.
     * \param first_word_type Preamble for the first word of each new "anchor"
     *                        address (typically TO).
     *
     * Examples:
     *   {"BOB","SAM"}    → [TO:BOB, REP:SAM]
     *   {"BOB","SAMUEL"} → [TO:BOB, REP:SAM, DATA:UEL]
     *   {"ROBERT","SAM"} → [TO:ROB, DATA:ERT, TO:SAM]
     */
    static std::vector<ALEWord> encode_group(const std::vector<std::string>& addrs,
                                              WordType                        first_word_type);

private:
    /**
     * Split addr into 3-char chunks with trailing '@' padding on the last chunk.
     * Per A.5.2.4.3: empty positions are stuffed with the utility symbol '@' (0x40).
     * Maximum 5 chunks (15 characters per A.5.2.4.2).
     * Returns ["@@@"] for an empty address.
     */
    static std::vector<std::string> chunk(const std::string& addr);

    /**
     * Construct one ALEWord from a preamble type and a 3-char chunk.
     * chunk3 must be exactly 3 characters (guaranteed by chunk()).
     */
    static ALEWord make(WordType type, const std::string& chunk3);
};

} // namespace ale
