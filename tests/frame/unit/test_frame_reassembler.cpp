/**
 * \file test_frame_reassembler.cpp
 * \brief OFS Phase 2: FrameReassembler grammar parser + shadow equivalence.
 *
 * docs/FRAMING_STANDARD.md §5: one RX grammar parser assigns roles from parse
 * position (FR-01..05). Phase 2 is shadow mode — the SM feeds the reassembler
 * in parallel and consumes nothing; these tests pin:
 *
 *   A. Structural grammar: boundaries (Tdrw settle + out-of-grammar restart),
 *      redundancy collapse, address accumulation ('@'-trim, ≤5 words),
 *      conclusion exclusivity, spacing gate, payload blocks (FR-11), and
 *      grammar-only frame typing (F_SOUND/F_RESPONSE/…; F-03/04/05 share one
 *      grammar — the §8 context matrix decides in Phase 3).
 *   B. Shadow equivalence: the SM's own reassembler (fed by
 *      ALECallProcessor::process_received_word) must reproduce the identity
 *      facts and extension roles of the pinned RX paths (the Phase 0
 *      characterization scenarios), and its frame boundaries must land on
 *      the same Tdrw settles the SM's handle_*() logic uses.
 *   C. Production feed: frame-level trace lines (FR-10) appear from the SM
 *      wiring — "[FRAME] F_SOUND … from TWAS DC7SU", not just the last word.
 *
 * Deliberate divergence class (documented, resolved by Phase 3): classify()
 * gates some roles on SM STATE (e.g. `collecting`), the reassembler on parse
 * POSITION. Where they disagree the reassembler is the OFS-intended reading;
 * the equivalence assertions below cover the paths both must agree on —
 * identity accumulation and the pinned extension classifications.
 */

#include "Protocol/Frame/frame_reassembler.h"
#include "Protocol/Control/ale_state_machine.h"
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

namespace {

bool all_pass = true;

void check(bool cond, const char* label) {
    std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
    all_pass = all_pass && cond;
}

const char* role_name(ParseRole r) {
    switch (r) {
    case ParseRole::NONE:               return "NONE";
    case ParseRole::SCAN_WORD:          return "SCAN_WORD";
    case ParseRole::ADDRESS_ANCHOR:     return "ADDRESS_ANCHOR";
    case ParseRole::ADDRESS_EXTENSION:  return "ADDRESS_EXTENSION";
    case ParseRole::MESSAGE_CMD:        return "MESSAGE_CMD";
    case ParseRole::MESSAGE_DATA:       return "MESSAGE_DATA";
    }
    return "??";
}

const uint32_t Trw  = ALETimingConstants::Trw_ms;    // 392
const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;   // 784

ALEWord w3(PreambleType t, char a, char b, char c) {
    const char chars[3] = {a, b, c};
    return WordParser::make_word(t, chars);
}

// CMD test word built directly (like ALESequenceBuilder::lqa_cmd/noise_cmd):
// control-CMD address fields ('a', 'n', … + spaces) are display artifacts of
// the raw payload, not Basic-38/Expanded-64 characters, so make_word() would
// reject them. The reassembler reads type + address field only.
ALEWord cmd_word(char a, char b, char c) {
    ALEWord w{};
    w.type        = PreambleType::CMD;
    w.raw_payload = 0x1A2B3u & 0x1FFFFFu;
    w.address[0]  = a; w.address[1] = b; w.address[2] = c; w.address[3] = '\0';
    w.valid       = true;
    return w;
}

// ── Part A: structural grammar (standalone reassembler) ─────────────────────

// A1: sound burst — repeated identical conclusions collapse (FR-02); a
// conclusion-only candidate is F_SOUND by grammar (§9 replay shape).
void a1_sound_collapse() {
    std::cout << "\n[A1] Sound burst: FR-02 collapse + F_SOUND typing\n";
    FrameReassembler r;
    const auto r1 = r.on_word(w3(PreambleType::TIS, 'S','A','M'), 1000);
    check(r1 == ParseRole::ADDRESS_ANCHOR, "first TIS[SAM] anchors");
    for (int i = 0; i < 3; ++i)
        check(r.on_word(w3(PreambleType::TIS, 'S','A','M'), 1000 + (i+1)*Trw)
                  == ParseRole::ADDRESS_ANCHOR,
              "repeated TIS[SAM] collapses (role unchanged)");
    r.tick(1000 + 4*Trw + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame from the repeated burst");
    if (frames.empty()) return;
    check(frames[0].type == FrameType::F_SOUND, "typed F_SOUND");
    check(frames[0].conclusion_identity == "SAM", "identity SAM");
    check(frames[0].complete, "complete (TIS conclusion seen)");
    check(frames[0].word_count == 4 && frames[0].logical_word_count == 1,
          "4 physical words collapse to 1 logical word");
    check(frames[0].mid_frame_acquisition, "no calling cycle → mid-frame");
    check(!frames[0].closed_by_out_of_grammar, "closed by Tdrw settle (FR-01a)");
}

// A2: multi-word conclusion with '@' stuffing — identity completes live
// (FR-06 construct completion), trimmed exactly like the SM's classify().
void a2_multiword_conclusion_trim() {
    std::cout << "\n[A2] Multi-word conclusion: '@'-trim + live identity\n";
    FrameReassembler r;
    r.on_word(w3(PreambleType::TIS, 'D','L','3'), 1000);
    r.on_word(w3(PreambleType::DATA, 'H','C','@'), 1000 + Trw);
    const AssembledFrame* c = r.candidate();
    check(c && c->conclusion_identity == "DL3HC",
          "identity DL3HC BEFORE the boundary (construct completion)");
    r.tick(1000 + Trw + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1 && frames[0].conclusion_identity == "DL3HC",
          "identity DL3HC at settle");
}

// A3: response shape — doubled 1-word TO collapses into one addressee run.
void a3_response_shape() {
    std::cout << "\n[A3] Response shape: TO×2 collapse + F_RESPONSE\n";
    FrameReassembler r;
    check(r.on_word(w3(PreambleType::TO, 'J','O','E'), 1000) == ParseRole::SCAN_WORD,
          "candidate's leading anchor is a scan word");
    check(r.on_word(w3(PreambleType::TO, 'J','O','E'), 1000 + Trw) == ParseRole::SCAN_WORD,
          "doubled TO collapses (FR-02)");
    check(r.on_word(w3(PreambleType::TIS, 'S','A','M'), 1000 + 2*Trw)
              == ParseRole::ADDRESS_ANCHOR, "TIS concludes");
    r.tick(1000 + 2*Trw + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].type == FrameType::F_RESPONSE, "typed F_RESPONSE (shared F-03/04/05 grammar)");
    check(frames[0].addressed_to.size() == 1 && frames[0].addressed_to[0] == "JOE",
          "one addressee run: JOE");
    check(frames[0].conclusion_identity == "SAM", "identity SAM");
}

// A4: doubled 2-word leading call — two complete addressee runs.
void a4_doubled_multiword_leading() {
    std::cout << "\n[A4] Doubled 2-word leading: SAMUEL twice\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TO,   'S','A','M'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'U','E','L'), t); t += Trw;
    check(r.on_word(w3(PreambleType::TO, 'S','A','M'), t) == ParseRole::ADDRESS_ANCHOR,
          "second TO opens the doubled run");
    t += Trw;
    r.on_word(w3(PreambleType::DATA, 'U','E','L'), t); t += Trw;
    r.on_word(w3(PreambleType::TIS,  'J','O','E'), t); t += Trw;
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].addressed_to.size() == 2
          && frames[0].addressed_to[0] == "SAMUEL"
          && frames[0].addressed_to[1] == "SAMUEL",
          "addressed to SAMUEL ×2 (Tlc doubling is transport)");
    check(frames[0].conclusion_identity == "JOE", "identity JOE");
    check(frames[0].type == FrameType::F_RESPONSE, "typed F_RESPONSE");
}

// A5: FR-05 — a second, different conclusion word is out-of-grammar: the
// candidate closes AT that word (FR-01(b)) with its identity settled.
void a5_foreign_conclusion_restart() {
    std::cout << "\n[A5] Second conclusion → out-of-grammar restart (FR-05/FR-01b)\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TO,   'J','O','E'), t); t += Trw;
    r.on_word(w3(PreambleType::TO,   'J','O','E'), t); t += Trw;
    r.on_word(w3(PreambleType::TIS,  'S','A','M'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'U','E','L'), t); t += Trw;
    check(r.on_word(w3(PreambleType::TIS,  'O','H','2'), t) == ParseRole::ADDRESS_ANCHOR,
          "foreign TIS restarts a fresh candidate");
    const auto frames = r.take_completed();   // closed by the restart itself
    check(frames.size() == 1, "frame closed at the foreign TIS (before settle)");
    if (frames.empty()) return;
    check(frames[0].closed_by_out_of_grammar, "closed by FR-01(b)");
    check(frames[0].conclusion_identity == "SAMUEL", "identity SAMUEL (foreign word excluded)");
    check(frames[0].complete, "frame complete");
    r.tick(t + Tdrw);
    const auto rest = r.take_completed();
    check(rest.size() == 1 && rest[0].type == FrameType::F_SOUND
          && rest[0].conclusion_identity == "OH2",
          "foreign candidate settles later as its own mid-frame frame");
}

// A6: FR-04 — a late extension (>Tdrw after the last accepted word) belongs
// to a later frame; it must not extend, and must not refresh the gate.
void a6_spacing_gate() {
    std::cout << "\n[A6] FR-04 spacing gate: late extension dropped\n";
    FrameReassembler r;
    r.on_word(w3(PreambleType::TIS, 'S','L','3'), 1000);
    const ParseRole late = r.on_word(w3(PreambleType::DATA, 'Z','X','B'),
                                     1000 + 3*Trw);   // > 2×Trw
    check(late == ParseRole::NONE, "late DATA does not extend (FR-04)");
    const AssembledFrame* c = r.candidate();
    check(c && c->conclusion_identity == "SL3", "identity still SL3");
    r.tick(1000 + Tdrw);   // gate anchored at the LAST ACCEPTED word
    const auto frames = r.take_completed();
    check(frames.size() == 1 && frames[0].conclusion_identity == "SL3",
          "candidate settles on its own accepted word, not the dropped one");
}

// A7: scanning section carries only the first word; the leading call's
// extension completes the full address in the same run (BOB → BOBCAT).
void a7_scanning_plus_leading() {
    std::cout << "\n[A7] Scanning first-word + leading extension → BOBCAT\n";
    FrameReassembler r;
    uint32_t t = 1000;
    for (int i = 0; i < 6; ++i) {   // C=3 scanning section, all identical
        const ParseRole role = r.on_word(w3(PreambleType::TO, 'B','O','B'), t);
        check(role == ParseRole::SCAN_WORD, "scanning repeats collapse (FR-02)");
        t += Trw;
    }
    check(r.on_word(w3(PreambleType::DATA, 'C','A','T'), t) == ParseRole::ADDRESS_EXTENSION,
          "leading extension attaches to the open run");
    t += Trw;
    r.on_word(w3(PreambleType::TIS, 'S','A','M'), t); t += Trw;
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].addressed_to.size() == 1 && frames[0].addressed_to[0] == "BOBCAT",
          "address completed to BOBCAT (run extended past the scanning anchor)");
    check(frames[0].conclusion_identity == "SAM", "identity SAM");
}

// A8: group call — REP acts as a new recipient (A.5.5.4.3.2); THRU seen →
// F_CALL.
void a8_group_call() {
    std::cout << "\n[A8] Group call: REP-as-recipient + F_CALL typing\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::THRU, 'B','O','B'), t); t += Trw;
    check(r.on_word(w3(PreambleType::REP,  'J','O','E'), t) == ParseRole::ADDRESS_ANCHOR,
          "REP after THRU anchor = new recipient, not an extension");
    t += Trw;
    r.on_word(w3(PreambleType::THRU, 'A','N','N'), t); t += Trw;
    r.on_word(w3(PreambleType::REP,  'B','O','B'), t); t += Trw;
    r.on_word(w3(PreambleType::TO,   'S','A','M'), t); t += Trw;
    r.on_word(w3(PreambleType::TIS,  'S','A','M'), t); t += Trw;
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].type == FrameType::F_CALL, "THRU seen → typed F_CALL");
    check(frames[0].conclusion_identity == "SAM", "identity SAM");
}

// A9: payload blocks — each CMD opens a block; block words never attach to
// addresses (FR-11/FR-04); block kind heuristic.
void a9_payload_blocks() {
    std::cout << "\n[A9] Payload blocks: CMD boundaries + FR-11 routing\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TO,   'J','O','E'), t); t += Trw;
    r.on_word(w3(PreambleType::TO,   'J','O','E'), t); t += Trw;
    check(r.on_word(w3(PreambleType::CMD,  'H','E','L'), t) == ParseRole::MESSAGE_CMD,
          "CMD opens a block");
    t += Trw;
    check(r.on_word(w3(PreambleType::DATA, 'L','O',' '), t) == ParseRole::MESSAGE_DATA,
          "payload DATA routes to the block");
    t += Trw;
    r.on_word(w3(PreambleType::REP,  'W','O','R'), t); t += Trw;
    check(r.on_word(cmd_word('a',' ',' '), t) == ParseRole::MESSAGE_CMD,
          "next CMD closes the previous block");
    t += Trw;
    r.on_word(w3(PreambleType::TIS,  'S','A','M'), t); t += Trw;
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].blocks.size() == 2, "two blocks (AMD + LQA)");
    if (frames[0].blocks.size() >= 2) {
        check(frames[0].blocks[0].kind == PayloadBlock::Kind::AMD
              && frames[0].blocks[0].data.size() == 2,
              "block 0: AMD, 2 payload words");
        check(frames[0].blocks[1].kind == PayloadBlock::Kind::LQA
              && frames[0].blocks[1].data.empty(),
              "block 1: LQA (CMD 'a'), no payload");
    }
    check(frames[0].addressed_to.size() == 1 && frames[0].addressed_to[0] == "JOE",
          "payload words never polluted the addressee");
    check(frames[0].type == FrameType::F_ORDERWIRE,
          "addressee + payload + conclusion → F_ORDERWIRE");
}

// A10: CMD after the conclusion is out-of-grammar (§2: message precedes
// conclusion) — the sound frame closes, the noise word becomes its own
// mid-frame F_LQA candidate.
void a10_noise_after_sound() {
    std::cout << "\n[A10] CMD NOISE after sound: out-of-grammar restart → F_LQA\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TIS, 'S','A','M'), t); t += Trw;
    r.on_word(w3(PreambleType::TIS, 'S','A','M'), t); t += Trw;
    check(r.on_word(cmd_word('n',' ',' '), t) == ParseRole::MESSAGE_CMD,
          "CMD after conclusion restarts");
    const auto frames = r.take_completed();
    check(frames.size() == 1 && frames[0].type == FrameType::F_SOUND
          && frames[0].conclusion_identity == "SAM",
          "sound frame closed complete at the CMD word");
    r.tick(t + Tdrw);
    const auto rest = r.take_completed();
    // The bare noise CMD has no conclusion — grammatically it can never form
    // a complete frame, so it settles as an incomplete UNTAGGED fragment with
    // its LQA block recorded (FR-08: discardable). F_LQA typing is reserved
    // for a conclusion-bearing payload frame (mid-frame join of a message
    // section + conclusion).
    check(rest.size() == 1 && rest[0].type == FrameType::UNTAGGED
          && !rest[0].complete && rest[0].mid_frame_acquisition,
          "noise word settles as an incomplete mid-frame fragment");
    check(rest.size() == 1 && rest[0].blocks.size() == 1
          && rest[0].blocks[0].kind == PayloadBlock::Kind::LQA,
          "noise fragment recorded as an LQA payload block");
}

// A11: AllCall broadcast shape → F_ALLCALL.
void a11_allcall() {
    std::cout << "\n[A11] AllCall broadcast → F_ALLCALL\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TO,   '@','?','@'), t); t += Trw;
    r.on_word(w3(PreambleType::TO,   '@','?','@'), t); t += Trw;
    r.on_word(w3(PreambleType::FROM, 'D','L','3'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'H','C','@'), t); t += Trw;
    r.on_word(w3(PreambleType::CMD,  'P','O','S'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'I','T','I'), t); t += Trw;
    r.on_word(w3(PreambleType::REP,  'O','N',' '), t); t += Trw;
    r.on_word(w3(PreambleType::TWAS, 'D','L','3'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'H','C','@'), t); t += Trw;
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1, "one frame");
    if (frames.empty()) return;
    check(frames[0].type == FrameType::F_ALLCALL, "typed F_ALLCALL");
    check(frames[0].quick_id == "DL3HC", "FROM quick-ID DL3HC");
    check(frames[0].conclusion_identity == "DL3HC", "TWAS identity DL3HC");
    check(frames[0].blocks.size() == 1 && frames[0].blocks[0].data.size() == 2,
          "AMD payload block intact");
}

// A12: in-link keep-alive address ?@? → F_INLINK (structural; openALE does
// not transmit this frame today).
void a12_inlink() {
    std::cout << "\n[A12] In-link ?@? → F_INLINK\n";
    FrameReassembler r;
    r.on_word(w3(PreambleType::TO,  '?','@','?'), 1000);
    r.on_word(w3(PreambleType::TO,  '?','@','?'), 1000 + Trw);
    r.on_word(w3(PreambleType::TIS, 'S','A','M'), 1000 + 2*Trw);
    r.tick(1000 + 2*Trw + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1 && frames[0].type == FrameType::F_INLINK,
          "typed F_INLINK");
}

// A13: incomplete candidate (no conclusion) closes UNTAGGED — consumers
// discard (FR-08 fail-safe).
void a13_incomplete() {
    std::cout << "\n[A13] Incomplete candidate → UNTAGGED\n";
    FrameReassembler r;
    r.on_word(w3(PreambleType::TO, 'J','O','E'), 1000);
    r.on_word(w3(PreambleType::TO, 'J','O','E'), 1000 + Trw);
    r.tick(1000 + Trw + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 1 && !frames[0].complete
          && frames[0].type == FrameType::UNTAGGED,
          "no conclusion → UNTAGGED, discardable");
}

// A14: a 6th word in an address run is out-of-grammar (A.5.2.4.4) — the
// candidate closes, the word cannot start the next one (dropped).
void a14_run_overflow() {
    std::cout << "\n[A14] Address run > 5 words → out-of-grammar close\n";
    FrameReassembler r;
    uint32_t t = 1000;
    r.on_word(w3(PreambleType::TO,   'A','B','C'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'D','E','F'), t); t += Trw;
    r.on_word(w3(PreambleType::REP,  'G','H','I'), t); t += Trw;
    r.on_word(w3(PreambleType::DATA, 'J','K','L'), t); t += Trw;
    r.on_word(w3(PreambleType::REP,  'M','N','O'), t); t += Trw;
    check(r.on_word(w3(PreambleType::DATA, 'P','Q','R'), t) == ParseRole::NONE,
          "6th word of the run is dropped");
    const auto frames = r.take_completed();
    check(frames.size() == 1 && !frames[0].complete,
          "run overflow closed the candidate (incomplete)");
    check(r.candidate() == nullptr, "no candidate opened by the dropped word");
}

// A15: the 2026-08-31 incident replay (§9) — 8 repeated SL3ZXB sounding
// bursts reassemble into 8 separate F_SOUND frames, identity SL3ZXB each;
// no frame ever merges two stations' words (FR-01).
void a15_incident_replay() {
    std::cout << "\n[A15] Incident replay: 8 sounding bursts → 8 F_SOUND frames\n";
    FrameReassembler r;
    uint32_t t = 1000;
    for (int burst = 0; burst < 8; ++burst) {
        r.on_word(w3(PreambleType::TWAS, 'S','L','3'), t); t += Trw;
        r.on_word(w3(PreambleType::DATA, 'Z','X','B'), t); t += Trw;
    }
    r.tick(t + Tdrw);
    const auto frames = r.take_completed();
    check(frames.size() == 8, "8 bursts → 8 frames (never merged)");
    bool all_ok = !frames.empty();
    for (const auto& f : frames)
        all_ok = all_ok && f.type == FrameType::F_SOUND
                     && f.conclusion_identity == "SL3ZXB"
                     && f.conclusion_is_twas && f.mid_frame_acquisition;
    check(all_ok, "every frame: F_SOUND from TWAS SL3ZXB, mid-frame");
}

// ── Part B: shadow equivalence against the pinned RX paths ──────────────────
//
// The SM's own shadow reassembler is driven by process_received_word(); the
// trace callback captures the FR-10 lines update() emits per completed frame.

struct ShadowHarness {
    ALEStateMachine sm;
    std::vector<ALEWord> sent;
    std::vector<std::string> trace;
    std::vector<ParseRole> roles;      // reassembler role per fed word

    explicit ShadowHarness(const char* self) {
        sm.set_self_address(self);
        sm.set_target_scan_channels(1);
        sm.set_transmit_callback([&](const ALEWord& w) { sent.push_back(w); });
        sm.set_state_callback([](ALEState, ALEState) {});
        sm.set_channel_callback([](const Channel&) {});
        sm.set_rx_enabled_callback([](bool) {});
        sm.set_operator_callback([](OperatorEvent) {});
        sm.set_trace_callback([&](const std::string& s) { trace.push_back(s); });
    }

    void rx(PreambleType ty, const char a3[3], uint32_t t) {
        sm.update(t);
        sm.process_received_word(WordParser::make_word(ty, a3));
        roles.push_back(sm.frame_reassembler().last_role());
    }

    std::vector<std::string> frame_lines() const {
        std::vector<std::string> out;
        for (const auto& s : trace)
            if (s.find("[FRAME] ") == 0) out.push_back(s);
        return out;
    }
};

// B1: the Phase-0 TEST-1 stream — multi-word caller conclusion. The
// reassembler's identity must equal the SM's caller_address at the same
// settle, and the extension word must carry the extension role that
// classify() pinned as DATA_EXTENSION.
void b1_handshake_identity_equivalence() {
    std::cout << "\n[B1] WAIT_CYCLE_END SAMUEL: identity + role equivalence\n";
    ShadowHarness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const char joe[3] = {'J','O','E'};
    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    uint32_t t = 1000;
    h.rx(PreambleType::TO,  joe, t); t += Trw;
    h.rx(PreambleType::TO,  joe, t); t += Trw;
    h.rx(PreambleType::TO,  joe, t); t += Trw;
    h.rx(PreambleType::TIS, sam, t); t += Trw;
    h.rx(PreambleType::DATA, uel, t); t += Trw;

    check(h.roles.size() == 5
          && h.roles[3] == ParseRole::ADDRESS_ANCHOR
          && h.roles[4] == ParseRole::ADDRESS_EXTENSION,
          "TIS[SAM] anchors, DATA[UEL] extends (classify: DATA_EXTENSION)");
    check(h.sm.get_caller_address() == "SAMUEL", "SM identity SAMUEL");

    const AssembledFrame* c = h.sm.frame_reassembler().candidate();
    check(c && c->conclusion_identity == "SAMUEL",
          "reassembler identity == SM identity BEFORE settle");

    h.sm.update(t + Tdrw);   // the SM's settle update — also the frame boundary
    const auto lines = h.frame_lines();
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SAMUEL") != std::string::npos && l.find("JOE") != std::string::npos)
            found = true;
    check(found, "frame trace line: to JOE from SAMUEL at the same settle");
}

// B2: foreign TIS inside the settle window (Phase-0 TEST-3) — the frame
// closes AT the foreign word with the real caller's identity; the SM's
// identity is unchanged by the foreign word.
void b2_foreign_tis_equivalence() {
    std::cout << "\n[B2] Foreign TIS: frame closes at the foreign word\n";
    ShadowHarness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const char joe[3] = {'J','O','E'};
    const char sam[3] = {'S','A','M'};
    const char uel[3] = {'U','E','L'};
    const char oh2[3] = {'O','H','2'};
    uint32_t t = 1000;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TIS,  sam, t); t += Trw;
    h.rx(PreambleType::DATA, uel, t); t += Trw;
    h.rx(PreambleType::TIS,  oh2, t); t += Trw;

    check(h.sm.get_caller_address() == "SAMUEL",
          "SM identity unpolluted (pinned by RxCharacterization)");
    check(h.roles.back() == ParseRole::ADDRESS_ANCHOR,
          "foreign TIS anchors the fresh candidate");
    const AssembledFrame* c = h.sm.frame_reassembler().candidate();
    check(c && c->conclusion_identity == "OH2",
          "fresh candidate carries the foreign identity (context discards, Phase 3)");
}

// B3: the WAIT_ACK multi-word ACK conclusion (Phase-0 TEST-5) — full 3-way
// handshake; the ACK frame's reassembled identity equals the SM's.
void b3_wait_ack_equivalence() {
    std::cout << "\n[B3] WAIT_ACK SL3ZXB: full-handshake identity equivalence\n";
    ShadowHarness h("JOE");
    h.sm.process_event(ALEEvent::START_SCAN);

    const char joe[3] = {'J','O','E'};
    const char sl3[3] = {'S','L','3'};
    const char zxb[3] = {'Z','X','B'};
    uint32_t t = 1000;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TO,   joe, t); t += Trw;
    h.rx(PreambleType::TIS,  sl3, t); t += Trw;
    h.rx(PreambleType::DATA, zxb, t); t += Trw;
    const uint32_t settle = t + Tdrw;
    h.sm.update(settle);                    // → SLOT_WAIT
    h.sm.update(settle + 1);                // → CHANNEL_CHECK
    h.sm.update(settle + 1 + Tdrw);        // → SENDING_RESPONSE (5 words)
    const uint32_t resp_words = h.sm.get_words_pending();
    for (uint32_t i = 0; i < resp_words; ++i)
        h.sm.on_word_complete();           // drain → WAIT_ACK (count captured first)

    // Caller's ACK: TO[JOE]×2 + TIS[SL3] + DATA[ZXB].
    uint32_t ack = settle + 1 + Tdrw + 500;
    h.rx(PreambleType::TO,  joe, ack); ack += Trw;
    h.rx(PreambleType::TO,  joe, ack); ack += Trw;
    h.rx(PreambleType::TIS, sl3, ack); ack += Trw;
    h.rx(PreambleType::DATA, zxb, ack);

    check(h.sm.get_state() == ALEState::HANDSHAKE, "SM still accumulating (pinned)");
    check(h.sm.get_caller_address() == "SL3ZXB", "SM identity SL3ZXB");
    const AssembledFrame* c = h.sm.frame_reassembler().candidate();
    check(c && c->conclusion_identity == "SL3ZXB",
          "reassembler identity == SM identity");

    h.sm.update(ack + Tdrw + 1);          // the settle both paths wait for
    check(h.sm.get_state() == ALEState::LINKED, "SM LINKED at settle (pinned)");
    const auto lines = h.frame_lines();
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SL3ZXB") != std::string::npos && l.find("JOE") != std::string::npos)
            found = true;
    check(found, "ACK frame traced: to JOE from SL3ZXB");
}

// B4: the 2026-08-31 LINKED termination stream (ForeignTwasLinkGuard TEST 3)
// — TWAS[DC7] + DATA[SU@]. The reassembler's grammar types it F_SOUND
// (conclusion-only, mid-frame — §9): the F-05-vs-F-06 decision belongs to
// the context matrix (Phase 3). What both must agree on here: the completed
// FULL ADDRESS DC7SU at the same Tdrw settle the SM terminates on.
void b4_linked_termination_identity() {
    std::cout << "\n[B4] LINKED termination stream: full-address equivalence\n";
    ShadowHarness h("DL3HC");
    h.sm.initiate_call("DC7SU");
    h.sm.process_event(ALEEvent::HANDSHAKE_COMPLETE);
    check(h.sm.get_state() == ALEState::LINKED, "reached LINKED (guard-test pattern)");

    const uint32_t t0 = 10'000u;
    h.sm.update(t0);
    const char dc7[3] = {'D','C','7'};
    const char sua[3] = {'S','U','@'};
    h.rx(PreambleType::TWAS, dc7, t0 + Trw);
    h.rx(PreambleType::DATA, sua, t0 + 2*Trw);

    const AssembledFrame* c = h.sm.frame_reassembler().candidate();
    check(c && c->conclusion_identity == "DC7SU",
          "reassembler accumulates the full address live ('@'-trimmed)");

    h.sm.update(t0 + 2*Trw + Tdrw + 1);   // the settle the guard test terminates on
    check(h.sm.get_state() != ALEState::LINKED,
          "SM terminates at settle (pinned by ForeignTwasLinkGuard)");
    bool found = false;
    for (const auto& l : h.frame_lines())
        if (l.find("F_SOUND") != std::string::npos
            && l.find("DC7SU") != std::string::npos)
            found = true;
    check(found,
          "frame traced as F_SOUND from DC7SU — grammar types the construct, "
          "the context matrix (Phase 3) holds the F-05 decision");
}

// ── Part C: production feed proof ────────────────────────────────────────────
// (folded into B: every B test reads the SM's own reassembler and its
//  trace lines — the ALECallProcessor feed and update() drain are the
//  production path, no test-only wiring exists.)

} // namespace

int main() {
    std::cout << std::unitbuf;   // flush per write — crash diagnostics survive
    std::cout << "==========================================================\n";
    std::cout << "FrameReassembler — OFS Phase 2 shadow grammar parser\n";
    std::cout << "==========================================================\n";

    a1_sound_collapse();
    a2_multiword_conclusion_trim();
    a3_response_shape();
    a4_doubled_multiword_leading();
    a5_foreign_conclusion_restart();
    a6_spacing_gate();
    a7_scanning_plus_leading();
    a8_group_call();
    a9_payload_blocks();
    a10_noise_after_sound();
    a11_allcall();
    a12_inlink();
    a13_incomplete();
    a14_run_overflow();
    a15_incident_replay();

    b1_handshake_identity_equivalence();
    b2_foreign_tis_equivalence();
    b3_wait_ack_equivalence();
    b4_linked_termination_identity();

    if (all_pass) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}