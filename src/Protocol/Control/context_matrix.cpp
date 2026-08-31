/**
 * \file context_matrix.cpp
 * \brief FR-07 context matrix (§8) — table transcription + query.
 */

#include "Protocol/Control/context_matrix.h"

namespace ale {

namespace {

// One row of §8, in FrameType declaration order (UNTAGGED excluded — handled
// by the caller before reaching a row lookup).
struct Row {
    MatrixAction f_call;
    MatrixAction f_response;
    MatrixAction f_ack;
    MatrixAction f_termination;
    MatrixAction f_sound;
    MatrixAction f_allcall;
    MatrixAction f_orderwire;  ///< see header doc: ACT here may be the carrying frame's action
    MatrixAction f_inlink;
    MatrixAction f_lqa;
};

using MA = MatrixAction;

// §8 table, row per SM state. IDLE and SCANNING share one row in the
// standard; ALEState::ERROR has no row and defaults to all-OBSERVE
// (FR-08 fail-safe: never act from an undefined state).
constexpr Row kIdleScanning  { MA::ACT,     MA::IGNORE,  MA::IGNORE,  MA::OBSERVE, MA::OBSERVE, MA::ACT,     MA::ACT,     MA::IGNORE,  MA::OBSERVE };
constexpr Row kCalling       { MA::OBSERVE, MA::ACT,     MA::IGNORE,  MA::ACT,     MA::OBSERVE, MA::OBSERVE, MA::OBSERVE, MA::IGNORE,  MA::OBSERVE };
constexpr Row kHandshake     { MA::ACT,     MA::ACT,     MA::ACT,     MA::OBSERVE, MA::OBSERVE, MA::OBSERVE, MA::ACT,     MA::IGNORE,  MA::OBSERVE };
// F_RESPONSE is OBSERVE, not the conceptual table's literal IGNORE (§8
// footnote †): FrameReassembler::assign_frame_type_() types the shared F-03/
// F-04/F-05 grammar F_RESPONSE, so a genuine termination whose leading TO×2
// was caught arrives typed F_RESPONSE, not a distinct F_TERMINATION (which
// RX never produces). OBSERVE lets handle_completed_frame_()'s identity
// check see it; it is never an unconditional ACT by type alone.
constexpr Row kLinked        { MA::OBSERVE, MA::OBSERVE, MA::IGNORE,  MA::ACT,     MA::OBSERVE, MA::OBSERVE, MA::ACT,     MA::ACT,     MA::OBSERVE };
constexpr Row kSounding      { MA::OBSERVE, MA::IGNORE,  MA::IGNORE,  MA::OBSERVE, MA::OBSERVE, MA::OBSERVE, MA::OBSERVE, MA::IGNORE,  MA::OBSERVE };

MatrixAction row_lookup(const Row& row, FrameType type) {
    switch (type) {
    case FrameType::UNTAGGED:      return MatrixAction::IGNORE;  // never reached (caller short-circuits); kept for completeness
    case FrameType::F_CALL:        return row.f_call;
    case FrameType::F_RESPONSE:    return row.f_response;
    case FrameType::F_ACK:         return row.f_ack;
    case FrameType::F_TERMINATION: return row.f_termination;
    case FrameType::F_SOUND:       return row.f_sound;
    case FrameType::F_ALLCALL:     return row.f_allcall;
    case FrameType::F_ORDERWIRE:   return row.f_orderwire;
    case FrameType::F_INLINK:      return row.f_inlink;
    case FrameType::F_LQA:         return row.f_lqa;
    }
    return MatrixAction::OBSERVE;
}

} // namespace

MatrixAction context_matrix_lookup(ALEState state, FrameType type) {
    // FR-08: an incomplete candidate (no conclusion seen) is never a frame —
    // discard toward no interpretation regardless of state.
    if (type == FrameType::UNTAGGED) return MatrixAction::IGNORE;

    switch (state) {
    case ALEState::IDLE:
    case ALEState::SCANNING:  return row_lookup(kIdleScanning, type);
    case ALEState::CALLING:   return row_lookup(kCalling,      type);
    case ALEState::HANDSHAKE: return row_lookup(kHandshake,    type);
    case ALEState::LINKED:    return row_lookup(kLinked,       type);
    case ALEState::SOUNDING:  return row_lookup(kSounding,     type);
    case ALEState::ERROR:     return MatrixAction::OBSERVE;   // no §8 row — fail-safe
    }
    return MatrixAction::OBSERVE;
}

} // namespace ale
