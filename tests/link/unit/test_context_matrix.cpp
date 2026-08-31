/**
 * \file test_context_matrix.cpp
 * \brief OFS Phase 3a: FR-07 context matrix (§8) — every cell pinned.
 *
 * docs/FRAMING_STANDARD.md §8 is the normative table; this test transcribes
 * it exactly (including the F-08 ✓* footnote cells, which are the carrying
 * frame's action — see context_matrix.h) so the matrix can never silently
 * drift from the standard. No production code consults the matrix yet
 * (Phase 3b wires the LINKED termination decision through it); this file
 * characterizes the table itself.
 */

#include "Protocol/Control/context_matrix.h"
#include <iostream>

using namespace ale;

namespace {

bool all_pass = true;

void check(bool cond, const char* label) {
    std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
    all_pass = all_pass && cond;
}

void expect(ALEState state, FrameType type, MatrixAction want, const char* label) {
    const MatrixAction got = context_matrix_lookup(state, type);
    std::cout << "  " << label << " = " << matrix_action_name(got)
               << " (want " << matrix_action_name(want) << "): "
               << (got == want ? "PASS" : "FAIL") << "\n";
    all_pass = all_pass && (got == want);
}

// ── IDLE / SCANNING row ──────────────────────────────────────────────────
void idle_scanning_row() {
    std::cout << "\n[IDLE/SCANNING] arm on TO self; AllCall + orderwire(=AMD-in-F-01) act\n";
    for (ALEState s : {ALEState::IDLE, ALEState::SCANNING}) {
        expect(s, FrameType::F_CALL,        MatrixAction::ACT,     "F_CALL");
        expect(s, FrameType::F_RESPONSE,    MatrixAction::IGNORE,  "F_RESPONSE");
        expect(s, FrameType::F_ACK,         MatrixAction::IGNORE,  "F_ACK");
        expect(s, FrameType::F_TERMINATION, MatrixAction::OBSERVE, "F_TERMINATION");
        expect(s, FrameType::F_SOUND,       MatrixAction::OBSERVE, "F_SOUND");
        expect(s, FrameType::F_ALLCALL,     MatrixAction::ACT,     "F_ALLCALL");
        expect(s, FrameType::F_ORDERWIRE,   MatrixAction::ACT,     "F_ORDERWIRE (AMD inside F-01)");
        expect(s, FrameType::F_INLINK,      MatrixAction::IGNORE,  "F_INLINK");
        expect(s, FrameType::F_LQA,         MatrixAction::OBSERVE, "F_LQA");
    }
}

// ── CALLING row ───────────────────────────────────────────────────────────
void calling_row() {
    std::cout << "\n[CALLING] response acts; termination = peer rejection (word-level, §10)\n";
    const ALEState s = ALEState::CALLING;
    expect(s, FrameType::F_CALL,        MatrixAction::OBSERVE, "F_CALL");
    expect(s, FrameType::F_RESPONSE,    MatrixAction::ACT,     "F_RESPONSE");
    expect(s, FrameType::F_ACK,         MatrixAction::IGNORE,  "F_ACK");
    expect(s, FrameType::F_TERMINATION, MatrixAction::ACT,     "F_TERMINATION (peer rejection)");
    expect(s, FrameType::F_SOUND,       MatrixAction::OBSERVE, "F_SOUND");
    expect(s, FrameType::F_ALLCALL,     MatrixAction::OBSERVE, "F_ALLCALL");
    expect(s, FrameType::F_ORDERWIRE,   MatrixAction::OBSERVE, "F_ORDERWIRE");
    expect(s, FrameType::F_INLINK,      MatrixAction::IGNORE,  "F_INLINK");
    expect(s, FrameType::F_LQA,         MatrixAction::OBSERVE, "F_LQA");
}

// ── HANDSHAKE row ─────────────────────────────────────────────────────────
void handshake_row() {
    std::cout << "\n[HANDSHAKE] call/response/ack legs + orderwire(=AMD-in-legs) act\n";
    const ALEState s = ALEState::HANDSHAKE;
    expect(s, FrameType::F_CALL,        MatrixAction::ACT,     "F_CALL (handshake legs)");
    expect(s, FrameType::F_RESPONSE,    MatrixAction::ACT,     "F_RESPONSE");
    expect(s, FrameType::F_ACK,         MatrixAction::ACT,     "F_ACK");
    expect(s, FrameType::F_TERMINATION, MatrixAction::OBSERVE, "F_TERMINATION");
    expect(s, FrameType::F_SOUND,       MatrixAction::OBSERVE, "F_SOUND");
    expect(s, FrameType::F_ALLCALL,     MatrixAction::OBSERVE, "F_ALLCALL");
    expect(s, FrameType::F_ORDERWIRE,   MatrixAction::ACT,     "F_ORDERWIRE (AMD in F-03/F-04 legs)");
    expect(s, FrameType::F_INLINK,      MatrixAction::IGNORE,  "F_INLINK");
    expect(s, FrameType::F_LQA,         MatrixAction::OBSERVE, "F_LQA");
}

// ── LINKED row — the incident row ───────────────────────────────────────
void linked_row() {
    std::cout << "\n[LINKED] only full-address termination + orderwire/inlink act; sound observes\n";
    const ALEState s = ALEState::LINKED;
    expect(s, FrameType::F_CALL,        MatrixAction::OBSERVE, "F_CALL");
    expect(s, FrameType::F_RESPONSE,    MatrixAction::IGNORE,  "F_RESPONSE");
    expect(s, FrameType::F_ACK,         MatrixAction::IGNORE,  "F_ACK");
    expect(s, FrameType::F_TERMINATION, MatrixAction::ACT,     "F_TERMINATION (full-address only)");
    expect(s, FrameType::F_SOUND,       MatrixAction::OBSERVE, "F_SOUND — the 2026-08-31 incident's row/column");
    expect(s, FrameType::F_ALLCALL,     MatrixAction::OBSERVE, "F_ALLCALL");
    expect(s, FrameType::F_ORDERWIRE,   MatrixAction::ACT,     "F_ORDERWIRE");
    expect(s, FrameType::F_INLINK,      MatrixAction::ACT,     "F_INLINK");
    expect(s, FrameType::F_LQA,         MatrixAction::OBSERVE, "F_LQA");
}

// ── SOUNDING row ──────────────────────────────────────────────────────────
void sounding_row() {
    std::cout << "\n[SOUNDING] everything observes; nothing acts\n";
    const ALEState s = ALEState::SOUNDING;
    expect(s, FrameType::F_CALL,        MatrixAction::OBSERVE, "F_CALL");
    expect(s, FrameType::F_RESPONSE,    MatrixAction::IGNORE,  "F_RESPONSE");
    expect(s, FrameType::F_ACK,         MatrixAction::IGNORE,  "F_ACK");
    expect(s, FrameType::F_TERMINATION, MatrixAction::OBSERVE, "F_TERMINATION");
    expect(s, FrameType::F_SOUND,       MatrixAction::OBSERVE, "F_SOUND");
    expect(s, FrameType::F_ALLCALL,     MatrixAction::OBSERVE, "F_ALLCALL");
    expect(s, FrameType::F_ORDERWIRE,   MatrixAction::OBSERVE, "F_ORDERWIRE");
    expect(s, FrameType::F_INLINK,      MatrixAction::IGNORE,  "F_INLINK");
    expect(s, FrameType::F_LQA,         MatrixAction::OBSERVE, "F_LQA");
}

// ── UNTAGGED + ERROR — outside the §8 table ────────────────────────────────
void untagged_and_error() {
    std::cout << "\n[UNTAGGED / ERROR] FR-08 fail-safe defaults\n";
    for (ALEState s : {ALEState::IDLE, ALEState::SCANNING, ALEState::CALLING,
                        ALEState::HANDSHAKE, ALEState::LINKED, ALEState::SOUNDING,
                        ALEState::ERROR}) {
        expect(s, FrameType::UNTAGGED, MatrixAction::IGNORE,
               "UNTAGGED always IGNORE (incomplete candidate, FR-08 discard)");
    }
    expect(ALEState::ERROR, FrameType::F_TERMINATION, MatrixAction::OBSERVE,
           "ERROR has no §8 row — defaults OBSERVE (never act from undefined state)");
    expect(ALEState::ERROR, FrameType::F_CALL, MatrixAction::OBSERVE,
           "ERROR defaults OBSERVE for every frame type");
}

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Context matrix (FR-07, docs/FRAMING_STANDARD.md §8)\n";
    std::cout << "==========================================================\n";

    idle_scanning_row();
    calling_row();
    handshake_row();
    linked_row();
    sounding_row();
    untagged_and_error();

    if (all_pass) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}
