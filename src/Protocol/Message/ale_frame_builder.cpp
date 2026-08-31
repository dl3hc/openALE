/**
 * \file ale_frame_builder.cpp
 * \brief OFS FrameBuilder implementation — catalog constructors + grammar gate.
 *
 * Spec: MIL-STD-188-141B Appendix A; docs/FRAMING_STANDARD.md §7 (FR-09).
 */

#include "Protocol/Message/ale_frame_builder.h"
#include "Protocol/Message/frame_validator.h"

namespace ale {

namespace {

// The FR-09 gate: validate and pass through, or refuse (empty sequence —
// nothing illegal goes on air).
ALESequence gate(FrameType type, std::vector<ALEWord> words) {
    if (FrameValidator::validate_frame(type, words))
        return ALESequence{};
    return ALESequence(std::move(words), type);
}

} // namespace

ALESequence ALEFrameBuilder::response(const std::string& caller_addr,
                                       const std::string& self_addr,
                                       bool is_reject) {
    return gate(FrameType::F_RESPONSE,
                ALESequenceBuilder::response(caller_addr, self_addr, is_reject).words());
}

ALESequence ALEFrameBuilder::ack(const std::string& peer_addr,
                                 const std::string& self_addr,
                                 bool no_link) {
    return gate(FrameType::F_ACK,
                ALESequenceBuilder::ack(peer_addr, self_addr, no_link).words());
}

ALESequence ALEFrameBuilder::termination(const std::string& peer_addr,
                                         const std::string& self_addr) {
    return gate(FrameType::F_TERMINATION,
                ALESequenceBuilder::termination(peer_addr, self_addr).words());
}

ALESequence ALEFrameBuilder::sound(const std::string& self, bool use_twas,
                                   uint32_t reps, uint32_t noise_cmd_raw24) {
    // A.5.3.3: the (scanning) sound is the whole-address conclusion repeated
    // for Tsrs. Repeated conclusions are separate sound frames on air
    // (Tdrw-separated) but the burst is transmitted contiguously, so the
    // conclusion body is validated as one list before the noise fragment is
    // appended.
    const ALESequence conc = ALESequenceBuilder::conclusion(self, use_twas);
    const auto& cw = conc.words();
    if (cw.empty() || reps == 0)
        return ALESequence{};

    std::vector<ALEWord> words;
    words.reserve(cw.size() * reps + (noise_cmd_raw24 ? 1u : 0u));
    for (uint32_t i = 0; i < reps; ++i)
        words.insert(words.end(), cw.begin(), cw.end());

    const ALESequence gated = gate(FrameType::F_SOUND, std::move(words));
    if (gated.empty())
        return ALESequence{};

    // CMD NOISE trailing fragment (AC-CHAN-004-002): built from the same
    // raw24 the pending-noise path uses (the address field matches
    // noise_cmd() encoding).
    std::vector<ALEWord> out = gated.words();
    if (noise_cmd_raw24) {
        ALEWord nw{};
        nw.type        = PreambleType::CMD;
        nw.raw_payload = noise_cmd_raw24 & 0x1FFFFFu;
        nw.address[0]  = 'n'; nw.address[1] = ' ';
        nw.address[2]  = ' '; nw.address[3] = '\0';
        nw.valid       = true;
        out.push_back(nw);
    }
    return ALESequence(std::move(out), FrameType::F_SOUND);
}

ALESequence ALEFrameBuilder::allcall_broadcast(const std::string& self,
                                              const std::vector<ALEWord>& amd_words,
                                              bool link_after_send,
                                              uint32_t scan_channels) {
    // A.5.2.4.7: the global AllCall address is the literal 3-char "@?@" —
    // valid Basic-38, no special-casing needed in the address path. The
    // scanning call must precede the leading call (A.5.2.5.1) so a station
    // mid-hop elsewhere still locks on before the leading call starts.
    static constexpr const char* kGlobalAllCallAddr = "@?@";
    std::vector<ALEWord> words =
        ALESequenceBuilder::scanning_call(kGlobalAllCallAddr, scan_channels).words();
    const auto leading = ALESequenceBuilder::leading_call(kGlobalAllCallAddr).words();
    const auto from    = ALESequenceBuilder::from_id(self).words();
    const auto conc    = ALESequenceBuilder::conclusion(self, /*is_reject=*/!link_after_send).words();
    words.insert(words.end(), leading.begin(), leading.end());
    words.insert(words.end(), from.begin(), from.end());
    words.insert(words.end(), amd_words.begin(), amd_words.end());
    words.insert(words.end(), conc.begin(), conc.end());

    return gate(FrameType::F_ALLCALL, std::move(words));
}

ALESequence ALEFrameBuilder::orderwire_burst(std::vector<ALEWord> content_words,
                                            const std::string& self,
                                            bool double_burst) {
    // Content-only contract: the caller supplies leading address + optional
    // CMD payload; the TIS:SELF conclusion is appended here (a pre-built
    // frame with its own TIS would double it on air).
    const ALESequence conc = ALESequenceBuilder::conclusion(self);
    for (const auto& w : conc.words())
        content_words.push_back(w);
    // EFS doubles the whole burst for reliability; AMD passes double_burst
    // = false so the peer decodes the text exactly once.
    if (double_burst) {
        const auto once = content_words;
        content_words.insert(content_words.end(), once.begin(), once.end());
    }

    return gate(FrameType::F_ORDERWIRE, std::move(content_words));
}

} // namespace ale