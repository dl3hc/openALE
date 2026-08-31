/**
 * \file frame_reassembler.cpp
 * \brief OFS FrameReassembler — RX grammar parser implementation (FR-01..05).
 *
 * Spec: MIL-STD-188-141B Appendix A; docs/FRAMING_STANDARD.md §5 (rules),
 * §6 (catalog), §6.1 (payload blocks). Shadow mode in Phase 2 — no consumer
 * beyond trace logging; Phase 3 routes Frame events into the context matrix.
 */

#include "Protocol/Frame/frame_reassembler.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/address_encoder.h"   // trim_ale_address()
#include <cstring>

namespace ale {

namespace {

bool is_anchor(PreambleType t) {
    return t == PreambleType::TO  || t == PreambleType::THRU
        || t == PreambleType::FROM || t == PreambleType::TIS
        || t == PreambleType::TWAS;
}

bool is_conclusion_anchor(PreambleType t) {
    return t == PreambleType::TIS || t == PreambleType::TWAS;
}

// CMD → payload-protocol kind (heuristic, see PayloadBlock doc).
PayloadBlock::Kind block_kind_of(const char chars[4]) {
    if (strncmp(chars, "DTM", 3) == 0) return PayloadBlock::Kind::DTM;
    if (strncmp(chars, "DBM", 3) == 0) return PayloadBlock::Kind::DBM;
    if (chars[0] == 'a' || chars[0] == 'r' || chars[0] == 'n')
        return PayloadBlock::Kind::LQA;
    return PayloadBlock::Kind::AMD;
}

} // namespace

ParseRole FrameReassembler::on_word(const ALEWord& w, uint32_t now_ms) {
    if (!w.valid) {
        last_role_ = ParseRole::NONE;
        return ParseRole::NONE;
    }

    // FR-02: identical (type + payload) to the immediately preceding accepted
    // word — transport repetition (scanning repeats, doubled 1-word leading,
    // repeated sound conclusions). Refresh the silence gate; grammar frozen.
    if (active_ && w.type == last_word_type_ && w.raw_payload == last_word_payload_) {
        candidate_storage_.last_word_ms = now_ms;
        ++candidate_storage_.word_count;
        last_accepted_ms_ = now_ms;
        last_word_type_   = w.type;
        last_word_payload_ = w.raw_payload;
        last_role_         = last_logical_role_;
        return last_role_;
    }

    if (!active_) {
        if (!is_anchor(w.type) && w.type != PreambleType::CMD) {
            // A frame cannot open on DATA/REP (§2: calling cycle or message
            // CMD comes first; a mid-frame payload join has no anchor to
            // attach to). Discard toward no interpretation (FR-08).
            last_role_ = last_logical_role_ = ParseRole::NONE;
            return ParseRole::NONE;
        }
        start_candidate_(w, now_ms);
    }

    const std::string txt = trim_ale_address(w.address);
    ParseRole role = ParseRole::NONE;

    switch (w.type) {
        case PreambleType::TO:
        case PreambleType::THRU:
        case PreambleType::FROM:
        case PreambleType::TIS:
        case PreambleType::TWAS: {
            if (conclusion_seen_) {
                // FR-01(b): the conclusion ends the frame (§2) — any anchor
                // after it (a second, different conclusion per FR-05, or the
                // leading TO of a doubled burst's copy) cannot extend this
                // grammar. Close the candidate and restart at this word.
                close_candidate_(true, now_ms);
                start_candidate_(w, now_ms);
                // fall through: handle the word as the fresh candidate's anchor
            }

            // A block ends at the start of the conclusion (§6.1 boundary).
            block_open_ = false;

            const bool conclusion = is_conclusion_anchor(w.type);
            // A second FROM is out-of-grammar (AC-WORD-007-4): one quick-ID
            // per frame. Close + restart like any other non-extending word.
            if (!conclusion && w.type == PreambleType::FROM
                    && !candidate_storage_.quick_id.empty()) {
                close_candidate_(true, now_ms);
                start_candidate_(w, now_ms);
            }

            if (run_open_) {
                // FR-03: the previous run completes at the next non-extension
                // word. Store its result by anchor kind.
                if (run_.anchor == PreambleType::TIS || run_.anchor == PreambleType::TWAS) {
                    candidate_storage_.conclusion_identity    = run_.text;
                    candidate_storage_.conclusion_is_twas     =
                        (run_.anchor == PreambleType::TWAS);
                } else if (run_.anchor == PreambleType::FROM) {
                    candidate_storage_.quick_id = run_.text;
                } else {
                    candidate_storage_.addressed_to.push_back(run_.text);
                }
                run_open_ = false;
            }

            // Open the new run (FR-03 anchor).
            run_            = Run{};
            run_.anchor     = w.type;
            run_.text       = txt;
            run_.words      = 1;
            run_.is_scanning = candidate_storage_.word_count == 0;
            run_open_       = true;

            if (conclusion) {
                conclusion_seen_ = true;
                candidate_storage_.complete = true;
                candidate_storage_.conclusion_identity = run_.text;
                candidate_storage_.conclusion_is_twas  =
                    (w.type == PreambleType::TWAS);
                role = ParseRole::ADDRESS_ANCHOR;
            } else {
                if (w.type == PreambleType::THRU) seen_thru_ = true;
                role = run_.is_scanning ? ParseRole::SCAN_WORD
                                        : ParseRole::ADDRESS_ANCHOR;
            }
            break;
        }

        case PreambleType::CMD: {
            if (conclusion_seen_) {
                // §2: the message section precedes the conclusion. A CMD
                // after it (e.g. the CMD NOISE word trailing a sound burst)
                // cannot extend this frame — close + restart (FR-01(b)); the
                // word becomes its own mid-frame candidate.
                close_candidate_(true, now_ms);
                start_candidate_(w, now_ms);
                candidate_storage_.mid_frame_acquisition = true;
            }

            if (block_open_) {
                // A new CMD closes the previous block and opens the next
                // (§6.1: each block is opened by its own CMD).
                block_open_ = false;
            }
            if (run_open_) {
                // FR-04: payload words never attach to an address run; the
                // block boundary also completes the run.
                if (run_.anchor == PreambleType::FROM)
                    candidate_storage_.quick_id = run_.text;
                else if (!is_conclusion_anchor(run_.anchor))
                    candidate_storage_.addressed_to.push_back(run_.text);
                run_open_ = false;
            }

            PayloadBlock block;
            block.kind = block_kind_of(w.address);
            memcpy(block.cmd_chars, w.address, 3);
            block.cmd_chars[3] = '\0';
            candidate_storage_.blocks.push_back(std::move(block));
            block_open_ = true;
            role = ParseRole::MESSAGE_CMD;
            break;
        }

        case PreambleType::DATA:
        case PreambleType::REP: {
            if (block_open_) {
                // FR-11: the block's owning CMD consumes payload words; they
                // never reach address accumulation (FR-04).
                candidate_storage_.blocks.back().data.push_back(w);
                role = ParseRole::MESSAGE_DATA;
                break;
            }
            if (!run_open_) {
                // Nothing to extend (candidate joined mid-frame after a
                // conclusion run was logically consumed, or a stray word).
                role = ParseRole::NONE;
                break;
            }
            if (static_cast<uint32_t>(now_ms - last_accepted_ms_)
                    > 2u * ALETimingConstants::Trw_ms) {
                // FR-04: more than Tdrw since the last accumulated word —
                // this word belongs to a later frame. Discard toward no
                // interpretation (FR-08); the candidate still closes on its
                // own silence settle.
                role = ParseRole::NONE;
                break;
            }
            if (w.type == PreambleType::REP && !run_.saw_data
                    && !is_conclusion_anchor(run_.anchor)) {
                // REP acting as a NEW group recipient (A.5.5.4.3.2 — mirror
                // of reconstruct_to_addresses: REP after a TO/THRU anchor
                // starts the next member, it does not extend). Conclusions
                // keep the lenient accumulate-REP behavior of the shipped RX
                // paths (FR-05 note in the header).
                candidate_storage_.addressed_to.push_back(run_.text);
                run_            = Run{};
                run_.anchor     = PreambleType::TO;   // REP carries TO semantics
                run_.text       = txt;
                run_.words      = 1;
                role = ParseRole::ADDRESS_ANCHOR;
                break;
            }
            if (run_.words >= MAX_RUN_WORDS) {
                // 6th word of an address run: out-of-grammar (A.5.2.4.4) —
                // close the candidate at this word (FR-01(b)); DATA/REP
                // cannot start the fresh one, so the word itself is dropped.
                role = ParseRole::NONE;
                close_candidate_(true, now_ms);
                break;
            }
            run_.text += txt;
            ++run_.words;
            if (w.type == PreambleType::DATA) run_.saw_data = true;
            if (is_conclusion_anchor(run_.anchor)) {
                // Live identity: consumers may read the candidate before the
                // boundary (FR-06 construct completion).
                candidate_storage_.conclusion_identity = run_.text;
            }
            role = ParseRole::ADDRESS_EXTENSION;
            break;
        }

        default:
            role = ParseRole::NONE;
            break;
    }

    if (role == ParseRole::NONE) {
        // Dropped word (FR-08): does not extend the grammar and does not
        // refresh the silence gate — it belongs to a later frame (FR-04) or
        // to no frame at all. Counters and collapse state stay untouched so
        // the candidate settles on its own last accepted word, matching the
        // SM's per-conclusion settle anchors.
        last_role_ = ParseRole::NONE;
        return ParseRole::NONE;
    }

    candidate_storage_.last_word_ms = now_ms;
    ++candidate_storage_.word_count;
    ++candidate_storage_.logical_word_count;
    last_accepted_ms_   = now_ms;
    last_word_type_     = w.type;
    last_word_payload_  = w.raw_payload;
    last_logical_role_  = role;
    last_role_          = role;
    return role;
}

void FrameReassembler::tick(uint32_t now_ms) {
    // FR-01(a): Tdrw of silence after the last accepted word.
    if (active_ && (now_ms - last_accepted_ms_) >= ALETimingConstants::Tdrw_ms)
        close_candidate_(false, now_ms);
}

std::vector<AssembledFrame> FrameReassembler::take_completed() {
    std::vector<AssembledFrame> out;
    out.swap(completed_);
    return out;
}

void FrameReassembler::reset() {
    active_ = false;
    candidate_storage_ = AssembledFrame{};
    run_open_ = false;
    block_open_ = false;
    conclusion_seen_ = false;
    seen_thru_ = false;
    last_word_type_ = PreambleType::UNKNOWN;
    last_word_payload_ = 0;
    last_logical_role_ = ParseRole::NONE;
    last_role_ = ParseRole::NONE;
    completed_.clear();
}

void FrameReassembler::start_candidate_(const ALEWord& w, uint32_t now_ms) {
    active_ = true;
    candidate_storage_ = AssembledFrame{};
    candidate_storage_.first_word_ms = now_ms;
    run_open_        = false;
    run_             = Run{};
    conclusion_seen_ = false;
    block_open_      = false;
    seen_thru_       = false;
    // The opening word is processed by the caller's walk (its counters are
    // advanced there); anchor the silence gate here so a start-of-candidate
    // word is never misjudged by the FR-04 spacing check.
    last_accepted_ms_  = now_ms;
    last_word_type_    = PreambleType::UNKNOWN;
    last_word_payload_ = w.raw_payload + 1u;   // never equal to the opening word
    (void)w;
}

void FrameReassembler::close_candidate_(bool out_of_grammar, uint32_t now_ms) {
    if (!active_) return;

    // Finalize the open run (FR-03: completes at frame end).
    if (run_open_) {
        if (run_.anchor == PreambleType::TIS || run_.anchor == PreambleType::TWAS) {
            candidate_storage_.conclusion_identity = run_.text;
            candidate_storage_.conclusion_is_twas  =
                (run_.anchor == PreambleType::TWAS);
        } else if (run_.anchor == PreambleType::FROM) {
            candidate_storage_.quick_id = run_.text;
        } else {
            candidate_storage_.addressed_to.push_back(run_.text);
        }
        run_open_ = false;
    }

    candidate_storage_.mid_frame_acquisition =
        candidate_storage_.addressed_to.empty();
    candidate_storage_.closed_by_out_of_grammar = out_of_grammar;
    if (out_of_grammar) candidate_storage_.last_word_ms = now_ms;

    assign_frame_type_(candidate_storage_);
    push_completed_(std::move(candidate_storage_));

    active_             = false;
    candidate_storage_  = AssembledFrame{};
    run_open_           = false;
    block_open_         = false;
    conclusion_seen_    = false;
}

void FrameReassembler::assign_frame_type_(AssembledFrame& f) const {
    if (!f.complete) {
        f.type = FrameType::UNTAGGED;   // incomplete — consumers discard (FR-08)
        return;
    }
    // Grammar-only typing; F-03/F-04/F-05 vs a C=0 F-01 share one grammar and
    // are disambiguated by the §8 context matrix in Phase 3 (FR-07).
    const auto is_allcall = [](const std::string& a) {
        return a.size() >= 2 && a[0] == '@' && a[1] != '@';
    };
    for (const auto& a : f.addressed_to)
        if (is_allcall(a)) { f.type = FrameType::F_ALLCALL; return; }
    for (const auto& a : f.addressed_to)
        if (a == "?@?") { f.type = FrameType::F_INLINK; return; }
    if (seen_thru_)                        { f.type = FrameType::F_CALL; return; }
    if (!f.addressed_to.empty() && !f.blocks.empty())
                                           { f.type = FrameType::F_ORDERWIRE; return; }
    if (!f.addressed_to.empty())           { f.type = FrameType::F_RESPONSE; return; }
    if (!f.blocks.empty())                 { f.type = FrameType::F_LQA; return; }
    f.type = FrameType::F_SOUND;
}

void FrameReassembler::push_completed_(AssembledFrame&& f) {
    completed_.push_back(std::move(f));
    if (completed_.size() > MAX_COMPLETED_)
        completed_.erase(completed_.begin());
}

} // namespace ale