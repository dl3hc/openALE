/**
 * \file Protocol/Control/ale_call_processor.h
 * \brief ALE call-processor — owns all received-word processing LOGIC.
 *
 * Architecture (see the "Full-fledged ALE decoder + isolated state machine"
 * plan): the decoder and the state machine are both isolated components.
 * The decoder decodes every unencrypted ALE transmission into an ALEWord;
 * the state machine (ALEStateMachine) does only states + transitions (+ its own
 * time evolution and TX output) and contains NO word-processing logic.
 *
 * Everything between them — interpreting a decoded word (pertinence / AllCall /
 * conclusion / data-extension classification), executing the per-state reaction
 * (accumulate caller identity, arm Tlww, extend addresses, LBT-busy abort,
 * contiguous-error counting), updating LQA, and driving the frame assembler —
 * lives here, in ALECallProcessor.  It is a stateless collection of friend
 * functions: each entry takes the ALEStateMachine& it operates on, reads the SM's
 * state/phase snapshot, mutates the SM's receive-related fields + the SM's
 * MessageAssembler / LQAMetrics / frame-assembled callback (all of which stay SM
 * members so the SM remains copyable), and drives state changes via
 * ALEStateMachine::process_event().
 *
 * The SM's process_received_word() / update_link_quality() are one-line delegate
 * shims that forward to ALECallProcessor::process_received_word(*this, …) /
 * update_lqa(*this, …), preserving the SM's public API (tests, examples, the
 * controller call sm.process_received_word(...) unchanged) while keeping all
 * processing logic out of the SM.
 */

#pragma once

#include "Protocol/Control/ale_state_machine.h"  // ALEStateMachine, ALEState, phases, ALEEvent
#include "Protocol/Message/ale_message.h"        // ALEMessage
#include "Word/ale_word.h"                        // ALEWord, PreambleType
#include "Word/address_encoder.h"                // trim_ale_address
#include "LQA/lqa_metrics.h"                      // LQAMetrics, MetricsSample, LinkQuality
#include <string>

namespace ale {

class ALECallProcessor {
public:
    /// Classification result (replaces the old WordEvent/WordDecodeContext pair).
    /// Public so unit tests can assert classification directly.
    struct WordRole {
        enum Type { NONE, TO_SELF, TIS_CALLER, DATA_EXTENSION, TWAS_WORD, CHANNEL_BUSY, ALLCALL } type = NONE;
        std::string address;  // trimmed address field
    };

    /// Main entry: classify the received word, run the per-state reaction, update
    /// LQA, and feed the frame assembler.  Equivalent to the old
    /// ALEStateMachine::process_received_word().
    static void process_received_word(ALEStateMachine& sm, const ALEWord& word);

    /// Forward an LQA sample to the metrics subsystem + channel manager.
    /// Equivalent to the old ALEStateMachine::update_link_quality().
    static void update_lqa(ALEStateMachine& sm, const LinkQuality& lq);

    /// Classify a decoded word given the current SM state/phase + self address.
    /// Mirrors the old ALEWordDecoder::decode() + the WordDecodeContext assembly
    /// that process_received_word() used to do inline.  Public so the AllCall
    /// unit test can assert pertinence without driving a full state transition.
    static WordRole classify(const ALEStateMachine& sm, const ALEWord& word);

private:
    /// AllCall address recognition (A.5.5.4.4) — moved verbatim from
    /// ale_word_decoder.cpp's file-static is_allcall_address().
    static bool is_allcall_address_(const std::string& addr, const std::string& self);

    // Per-state reactions — moved verbatim (in behavior) from the SM's react_*.
    // Mutate sm.* receive fields (friend access) and call sm.process_event().
    static void react_idle_(ALEStateMachine& sm, const WordRole& r);
    static void react_scanning_(ALEStateMachine& sm, const WordRole& r);
    static void react_calling_(ALEStateMachine& sm, const WordRole& r);
    static void react_handshake_(ALEStateMachine& sm, const WordRole& r, const ALEWord& word);
    // Shared: TO_SELF / ALLCALL detection (old SM detect_incoming_call).
    static void detect_incoming_call_(ALEStateMachine& sm, const WordRole& r);

    /// Invalid-word handling — moved from the SM's handle_invalid_word().
    static void on_invalid_word_(ALEStateMachine& sm);
};

} // namespace ale