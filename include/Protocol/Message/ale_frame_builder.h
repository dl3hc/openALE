/**
 * \file ale_frame_builder.h
 * \brief OFS FrameBuilder — the TX catalog with the grammar gate (FR-09).
 *
 * docs/FRAMING_STANDARD.md §7: frames are created only through catalog
 * constructors; each built word list passes FrameValidator::validate_frame()
 * before it may reach the modulator — the spec's "INVALID ADDRESS SEQUENCE!
 * … ALERT OPERATOR OR CONTROLLER" flowchart exit (A.5.2.5.1/3) collapsed to
 * build time. A frame that cannot be built legally is a bug by construction.
 *
 * Layering (Architecture guard, AC-GEN-001-001): this class lives in the
 * Protocol layer because the gate needs FrameValidator (Protocol/Message).
 * ALESequenceBuilder (Word layer) remains the section/frame constructor and
 * carries the FrameType tags; the gate composes on top of it (Protocol →
 * Word is the permitted direction).
 *
 * On validation failure a constructor returns an EMPTY ALESequence — nothing
 * illegal goes on air; callers treat empty as "refused" (transmit nothing).
 */

#pragma once

#include "Word/ale_sequence.h"
#include <string>
#include <vector>

namespace ale {

/**
 * \class ALEFrameBuilder
 * Catalog-typed TX frame constructors with the FR-09 grammar gate.
 * One method per catalog row openALE transmits (§6). The F_CALL row is
 * validated by ALEStateMachine::call_frame_is_legal_() (its frame is rendered
 * section-by-section for the word-count-driven phase advance).
 */
class ALEFrameBuilder {
public:
    /** F-03 response (§A.5.5.3.3): TO[caller]×2 + TIS[self], or TWAS[self] reject. */
    static ALESequence response(const std::string& caller_addr,
                                const std::string& self_addr,
                                bool is_reject = false);

    /** F-04 ACK (§A.5.5.3.4): TO[peer]×2 + TIS[self], or TWAS[self] (AMD decline). */
    static ALESequence ack(const std::string& peer_addr,
                           const std::string& self_addr,
                           bool no_link = false);

    /** F-05 termination (§A.5.5.3.5): TO[peer]×2 + TWAS[self]. */
    static ALESequence termination(const std::string& peer_addr,
                                   const std::string& self_addr);

    /**
     * F-06 sound frame (A.5.3.3): whole-address conclusion (TIS inviting /
     * TWAS announce-only) repeated @p reps times — Tsrs = (n+2)·Ta, minimum
     * Trs = 2·Ta for reps = 2.
     *
     * @p noise_cmd_raw24 (from noise_cmd() encoding, 0 = none) appends the
     * CMD NOISE word as a trailing fragment (AC-CHAN-004-002) — it is the
     * sound's own one-word broadcast, not part of the validated frame body.
     */
    static ALESequence sound(const std::string& self, bool use_twas,
                             uint32_t reps, uint32_t noise_cmd_raw24 = 0);

    /**
     * F-07 AllCall broadcast frame (A.5.5.4.4): TO @?@ scanning section +
     * TO @?@ × 2 leading call + FROM[self] quick-ID + AMD payload words +
     * TIS/TWAS[self] conclusion (TWAS unless @p link_after_send).
     * One-way broadcast — receivers never respond to it.
     */
    static ALESequence allcall_broadcast(const std::string& self,
                                         const std::vector<ALEWord>& amd_words,
                                         bool link_after_send,
                                         uint32_t scan_channels);

    /**
     * F-08 linked orderwire burst (A.5.6.3.2 / A.5.7.2): @p content_words
     * (leading address + optional CMD payload — content only, never a
     * pre-built frame with its own TIS) + TIS[self] conclusion. The whole
     * burst is duplicated end-to-end when @p double_burst is set (EFS
     * reliability pattern); AMD passes false so the peer decodes the text
     * exactly once.
     */
    static ALESequence orderwire_burst(std::vector<ALEWord> content_words,
                                       const std::string& self,
                                       bool double_burst);
};

} // namespace ale