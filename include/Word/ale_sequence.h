/**
 * \file ale_sequence.h
 * \brief ALE protocol word sequences (MIL-STD-188-141B Appendix A, §A.5.2.5)
 *
 * Terminology alignment with the spec (§A.5.2.5, §A.5.2.5.3 flowchart):
 *
 *   "BASIC FRAME IS CONSTRUCTED" — ALESequenceBuilder builds the word list.
 *   "TRIPLE EACH WORD FOR REDUNDANCY" — ALEEncoder::encode_tx49() handles
 *       this downstream; the 49-bit tx49 is mapped to 49 8-FSK symbols
 *       (147 bits = 3× physical copies) via the modulo-49 mapping.
 *   "LEADING CALL HAS BEEN DOUBLED" — pre-doubled by leading_call() /
 *       leading_call_group() (Tlc = 2×Tc, §A.5.5.3.1).
 *   "SEND IT" — Modulator::enqueue_sequence() queues the tx49 stream.
 *
 * The spec's "frame" is the complete transmission (calling cycle + optional
 * message + conclusion).  ALESequence models one section or a complete
 * response frame — an ordered list of ALEWords ready to be enqueued.
 *
 * encode() converts each ALEWord to its 49-bit tx49 representation
 * (Golay + A/B-interleaving, §A.5.2.2.2/3).  The physical 3×-redundancy
 * is applied by the encoder/modulator layer and is invisible here.
 *
 * Bit-convention of tx49 words (§A.5.2.2.3, MSB-first):
 *   bit  0 = A1  (W1, first transmitted bit)
 *   bit 48 = S49 (Stuff Bit = 0, last transmitted bit)
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Word/ale_word.h"
#include <string>
#include <vector>
#include <cstdint>

namespace ale {

/**
 * An ordered sequence of ALEWords forming one transmit-able protocol unit.
 *
 * Corresponds to the "basic frame" word list from the spec's construction
 * flowchart (§A.5.2.5.3): the word sequence BEFORE the physical tripling
 * that ALEEncoder::encode_tx49() applies per word.
 *
 * Used for individual sections (scanning call, leading call, conclusion)
 * and for complete response frames (response, ACK, termination) whose
 * sections are combined into one contiguous sequence.
 */
class ALESequence {
public:
    ALESequence() = default;
    explicit ALESequence(std::vector<ALEWord> words) : words_(std::move(words)) {}

    /**
     * Encode all words to their 49-bit tx49 representations.
     *
     * For each ALEWord:
     *   1. 24-bit word [preamble(3) | payload(21)] from type and raw_payload
     *   2. Golay (24,12) on upper half W1..W12 (Coder A) and
     *      lower half W13..W24 (Coder B, check bits inverted) — §A.5.2.2.2
     *   3. A/B-interleaving [A1,B1, A2,B2, …, A24,B24] + S49=0 — §A.5.2.2.3
     *
     * \return Vector of tx49 values; each is a 49-bit word
     *         (bit 0 = A1 = first transmitted bit, bit 48 = S49 = 0).
     *         The physical 3×-redundancy (§A.5.2.2.4) is applied by
     *         ALEEncoder::encode_tx49() in the modulator, not here.
     */
    std::vector<uint64_t> encode() const;

    const std::vector<ALEWord>& words() const { return words_; }
    size_t size()  const { return words_.size(); }
    bool   empty() const { return words_.empty(); }

private:
    std::vector<ALEWord> words_;
};

/**
 * Builds ALESequence objects for the protocol sections and response frames
 * defined by MIL-STD-188-141B §A.5.5.3 (individual/net, Figure A-29) and
 * §A.5.5.4.3 (star group calling, Figures A-34/A-35).
 *
 * Figure mapping:
 *   (a) 1-ch nonscan, 1-word, individual:  leading_call  + conclusion
 *   (b) N-ch scanning, 1-word, individual: scanning_call + leading_call + conclusion
 *   (c) 1-ch nonscan, 2-word, individual:  leading_call  + conclusion
 *   (d) N-ch scanning, 2-word, individual: scanning_call + leading_call + conclusion
 *   §A.5.5.4.3 N-member star group call:   scanning_call_group + leading_call_group + conclusion
 *
 * The caller assembles sections into a complete call frame:
 *   scanning_call(dest, C) + leading_call(dest) + conclusion(self)
 *
 * Timing durations (Tsc, Tlc, Tcc, Tx) → ale_timing.h.
 * 1-word vs. 2-word addressing is automatic from the address length.
 */
class ALESequenceBuilder {
public:
    /**
     * Scanning call section — individual/net call (Figures b, d) §A.5.2.5.1.
     *
     * Transmits the first address word of dest exactly scan_channels × 2 times
     * (Tsc = scan_channels × 2 × Trw), covering the target's full scan period.
     * Only the first 3 characters are used (DATA/REP extension words are
     * forbidden in the scanning section — §A.5.2.5.1).
     *
     * scan_channels = 0: returns an empty sequence (skip scanning, §AC-LINK-017-3).
     * scan_channels = 1: minimum — 2 words (one Tsc slot pair).
     *
     * Examples (scan_channels = 3):
     *   "BOB"    → [TO:BOB, TO:BOB, TO:BOB, TO:BOB, TO:BOB, TO:BOB]  (6 words)
     *   "SAMUEL" → [TO:SAM, TO:SAM, TO:SAM, TO:SAM, TO:SAM, TO:SAM]  (6 words, first 3 chars)
     */
    static ALESequence scanning_call(const std::string& dest,
                                     uint32_t scan_channels = 1);

    /**
     * Scanning call section — star group call §A.5.5.4.3.1.
     *
     * Collects the first word (first 3 chars) of every member address,
     * drops duplicates ("sent only once during Tsc"), and caps the result
     * at 5 unique words (A.5.5.4.3 / AC-WORD-006-4). The survivors rotate
     * through the section alternating THRU/REP by word position, for the
     * same total airtime as an individual scanning call (scan_channels × 2
     * words). If only one unique first word survives, this degenerates to
     * a plain individual/net scanning call (TO) — see scanning_call().
     *
     * scan_channels = 0 or members empty: returns an empty sequence.
     *
     * Example (scan_channels = 3, members = {"BOB","EDGAR","SAM"}):
     *   [THRU:BOB, REP:EDG, THRU:SAM, REP:BOB, THRU:EDG, REP:SAM]  (6 words)
     */
    static ALESequence scanning_call_group(const std::vector<std::string>& members,
                                           uint32_t scan_channels = 1);

    /**
     * Leading call section — individual/net call (Figures a–d) §A.5.5.3.1.
     *
     * Full address, sent twice (Tlc = 2×Tc):
     *   1-word: [TO:dest, TO:dest]
     *   2-word: [TO:dest1, DATA:dest2, TO:dest1, DATA:dest2]
     */
    static ALESequence leading_call(const std::string& dest);

    /**
     * Leading call section — star group call §A.5.5.4.3.2.
     *
     * Complete addresses of every prospective group member, sent twice
     * (Tlc = 2×Tc), always anchored on TO ("as usual" — THRU never appears
     * outside the scanning section, AC-WORD-006-1/7). Address boundaries
     * use AddressEncoder::encode_group()'s TO/REP rule: a TO following
     * another TO becomes REP; a TO following DATA remains TO.
     * Up to 12 address words total across all members (A.5.5.4.3).
     *
     * Example (members = {"BOB","EDGAR","SAMUEL"}):
     *   [TO:BOB, REP:EDG, DATA:AR@, TO:SAM, DATA:UEL,
     *    TO:BOB, REP:EDG, DATA:AR@, TO:SAM, DATA:UEL]
     */
    static ALESequence leading_call_group(const std::vector<std::string>& members);

    /**
     * Conclusion section — all figures (a–f) §A.5.2.5.3.
     *
     *   is_reject = false (default): [TIS:self]  — call accepted / sounding
     *   is_reject = true:            [TWAS:self] — call rejected (§A.5.2.3.2.2)
     *
     * Multiple words when the callsign exceeds 3 characters (DATA/REP extension).
     */
    static ALESequence conclusion(const std::string& self,
                                  bool is_reject = false);

    /**
     * Complete response frame — called station (JOE) side §A.5.5.3.3 / Figure A-30.
     *
     *   is_reject = false: TO [caller] × 2 + TIS [self]   (accept)
     *   is_reject = true:  TWAS [self]                    (reject, §AC-FRAME-010-1)
     */
    static ALESequence response(const std::string& caller_addr,
                                const std::string& self_addr,
                                bool is_reject = false);

    /**
     * Complete ACK frame — calling station (SAM) side §A.5.5.3.4 / Figure A-31.
     *
     *   TO [peer] × 2 + TIS [self]
     */
    static ALESequence ack(const std::string& peer_addr,
                           const std::string& self_addr);

    /**
     * Complete termination frame §A.5.5.3.5 / T-07.
     *
     *   TO [peer] × 2 + TWAS [self]
     *
     * Sent by terminate_link() before the link is torn down.
     */
    static ALESequence termination(const std::string& peer_addr,
                                   const std::string& self_addr);
};

} // namespace ale
