/**
 * \file context_matrix.h
 * \brief OFS FR-07 as a table — docs/FRAMING_STANDARD.md §8.
 *
 * Interpretation of a frame type is attempted only in the SM states listed
 * for it in the §6 catalog; everywhere else the frame is observed (LQA,
 * heard-list, logging) but causes no state change. This is that table,
 * transcribed cell-for-cell from §8, plus its query function.
 *
 * Placement: Protocol/Control, not Protocol/Frame — the matrix keys on
 * ALEState (Protocol/Control/ale_state_machine.h), and ale_link (the library
 * that owns ALEState) already depends on ale_protocol (the library that owns
 * FrameReassembler/FrameType); putting the matrix under Protocol/Frame would
 * invert that dependency.
 *
 * The F-08 (F_ORDERWIRE) column carries the §8 footnote: A.5.7.2.2 payloads
 * are receivable inside *any* carrying frame, so a §8 cell marked ACT for
 * F_ORDERWIRE in IDLE/SCANNING/HANDSHAKE is the *carrying* frame's action
 * (F-01 call / F-03/F-04 handshake legs) applied to a frame the reassembler
 * grammar-types as F_ORDERWIRE (addressee + payload block + conclusion) —
 * not an independent F-08 detection in those states. LINKED's F_ORDERWIRE
 * ACT is the real in-link exchange (F-08 proper).
 *
 * LINKED's F_RESPONSE cell is OBSERVE, not the conceptual table's literal
 * IGNORE (§8 footnote †): F_RESPONSE is the RX type shared by F-03/F-04/F-05
 * (§6 note), so a genuine peer termination whose leading TO×2 was caught
 * types F_RESPONSE, never a distinct F_TERMINATION (RX never produces that
 * type — found during Phase 3b). OBSERVE lets ALEStateMachine::
 * handle_completed_frame_()'s identity check see it; it is never an
 * unconditional ACT by type alone — the exact full-address compare decides.
 *
 * UNTAGGED (incomplete candidate, FR-08 discard) is IGNORE in every state.
 * ALEState::ERROR has no §8 row; it defaults to OBSERVE everywhere (FR-08's
 * fail-safe direction — never act from an undefined state).
 */

#pragma once

#include "Protocol/Control/ale_state_machine.h"
#include "Word/frame_catalog.h"

namespace ale {

/// FR-07 disambiguation outcome for one (state, frame type) cell.
enum class MatrixAction {
    ACT,      ///< ✓ — the state's per-state reaction may act on this frame
    OBSERVE,  ///< ○ — LQA/heard-list/logging only, no state change
    IGNORE,   ///< ✗ — not interpreted at all in this state
};

/// Catalog name for logging/tests ("ACT", "OBSERVE", "IGNORE").
inline const char* matrix_action_name(MatrixAction a) {
    switch (a) {
    case MatrixAction::ACT:     return "ACT";
    case MatrixAction::OBSERVE: return "OBSERVE";
    case MatrixAction::IGNORE:  return "IGNORE";
    }
    return "??";
}

/// FR-07 as a table (§8): may this frame type act (drive a state transition)
/// while the SM is in this state? Written to mirror current behavior exactly
/// (Phase 3a — no behavior change); Phase 3b/3c route decisions through it.
MatrixAction context_matrix_lookup(ALEState state, FrameType type);

} // namespace ale
