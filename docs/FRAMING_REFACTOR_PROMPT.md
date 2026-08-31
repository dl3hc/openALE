# OFS Refactoring — Handoff Prompt (Phase 3)

Self-contained handoff prompt for the session implementing the next phase of
the openALE Framing Standard in code. Paste the fenced block below into a
fresh Claude Code session on branch `develop` (or run
`claude "$(cat docs/FRAMING_REFACTOR_PROMPT.md)"`).

Phases 0–2 are DONE (see the status block inside). This prompt targets
**Phase 3 — the context matrix**.

---

```markdown
# Task: OFS Phase 3 — converge RX decisions onto the context matrix

## Completed so far — do NOT redo (all on develop, suite 69/69 green at HEAD)

- `17a0a44` — docs: FRAMING_STANDARD.md (the normative standard, OF-0, FR-01..11,
  catalog F-01..10 + payload P-1..4, §8 context matrix, §10 mapping, §11
  checklist) + this handoff file.
- `f85d493` — Phase 0: characterization pins,
  `tests/link/unit/test_rx_characterization.cpp` (ctest `RxCharacterization`,
  7 tests). Every multi-word conclusion accumulation path is pinned — existing
  tests only ever used 3-char addresses.
- `a6d1b32` — Phase 1: TX FrameBuilder (FR-09). `Word/frame_catalog.h`
  (FrameType F-01..F-10), FrameType tags on ALESequence,
  `FrameValidator::validate_frame()` (section-aware grammar gate — build-time
  "INVALID ADDRESS SEQUENCE" hard failure, empty sequence = refuse),
  `ALEFrameBuilder` (src/Protocol/Message/ale_frame_builder.cpp) as the
  catalog/gate layer (Architecture ctest forbids Word→Protocol includes, so
  the gate composes above the Word-layer ALESequenceBuilder),
  `ALEStateMachine::call_frame_is_legal_()` validates the complete F_CALL
  frame at initiate time. All SM/controller TX paths build through the gate.
  Tests: `tests/word/unit/test_frame_builder.cpp` (ctest `FrameBuilder`).
- `064edee` — Phase 2: RX FrameReassembler, SHADOW MODE.
  `include/Protocol/Frame/frame_reassembler.h` +
  `src/Protocol/Frame/frame_reassembler.cpp` implement FR-01..05 as a
  single-candidate grammar parser + §6.1 payload blocks (CMD-opened, FR-11
  routing, kind heuristic). The SM feeds it shadow-style
  (ALECallProcessor::process_received_word feeds every valid word;
  ALEStateMachine::update() ticks it and drains completed frames into
  frame-level SM_TRACE lines, FR-10: "[FRAME] F_SOUND … from TWAS DC7SU").
  NOTHING consumes its output yet — that is Phase 3's job.
  Tests: `tests/frame/unit/test_frame_reassembler.cpp` (ctest
  `FrameReassembler`, 15 structural + 4 shadow-equivalence tests over the
  Phase-0 scenarios).

## Context you must load first (read in this order, before any code changes)

1. `docs/FRAMING_STANDARD.md` — the normative standard. §8 context matrix is
   the Phase 3 spec; §10 records exactly what has converged; §11 is the
   compliance checklist. §9 (incident replay under OFS) is the shape of the
   final LINKED path you must produce.
2. `docs/LINKED_TWAS_TERMINATION_HANDOFF.md` — the incident + the LOCKED
   termination semantics (see Hard constraints below).
3. Source ground truth:
   - The shadow parser: `include/Protocol/Frame/frame_reassembler.h`
     (read its header doc — the design decisions are documented there) and
     `src/Protocol/Frame/frame_reassembler.cpp`.
   - The paths you are migrating:
     `src/Protocol/Control/ale_call_processor.cpp` (classify() at the top,
     per-state react_*, the LINKED branch arming `linked_twas_addr_`),
     `src/Protocol/Control/ale_state_machine.cpp` (handle_handshake() settle
     ~line 680-725, handle_linked() TWAS settle ~line 900-925, the update()
     shadow tick/drain near the top).
   - The tests that gate you (all must pass UNMODIFIED except where this
     prompt says to re-pin deliberately):
     `tests/link/unit/test_foreign_twas_link_guard.cpp` (ForeignTwasLinkGuard,
     6 tests — LOCKED), `tests/link/unit/test_rx_characterization.cpp`
     (RxCharacterization, 7 tests), `tests/frame/unit/
     test_frame_reassembler.cpp` (FrameReassembler, 19),
     `tests/link/unit/test_state_machine.cpp`, `tests/monitor/unit/
     test_message_assembler.cpp`.
4. Persistent memory: the session-start memory recall will surface the
   "openALE Framing Standard (OFS)" entity with every design decision and
   gotcha from Phases 0–2 — read it before planning.

## Phase-2 design decisions Phase 3 inherits (do not re-litigate)

- **Grammar-only typing is deliberately ambiguous**: the reassembler types a
  TO×2-addr + conclusion frame F_RESPONSE — the shared grammar of
  F-01(C=0)/F-03/F-04/F-05. YOUR §8 matrix is the only disambiguator
  (FR-07). The reassembler also types a conclusion-only mid-frame candidate
  F_SOUND — the 2026-08-31 sounding. The F-05 termination decision lives in
  the matrix cell, never in the parser.
- Incomplete candidates close as UNTAGGED (discard, FR-08). Dropped words
  (FR-04 spacing, out-of-grammar) do NOT refresh the silence gate — frames
  settle on their own last accepted word, same as the SM's settle anchors.
- RX accepts REP as a first conclusion extension (the DATA-only rule is
  TX-side; the shipped RX paths accumulate REP identically — pinned).
- The frame boundary lands on the SAME Tdrw settle handle_handshake()/
  handle_linked() use (pinned by the equivalence tests) — so migrating the
  settle decisions to consume completed frames changes NO timing.
- classify()'s state-gates (the `collecting` OR) vs the reassembler's
  parse-position roles diverge in places; the divergences are documented in
  test_frame_reassembler.cpp's header. The reassembler is the OFS-intended
  reading; classify() migrates toward it, not the reverse.

## Goal (Phase 3)

FR-07 as code: the state × frame-type dispatch becomes a table; Frame events
from the reassembler drive the decisions at frame boundaries; word-level
role/classify() reduces to reassembler roles where the gates are parse-position
facts.

1. **The matrix** (§8): a data structure (frame type × SM state →
   ACT / OBSERVE / IGNORE) + a query function, initially written to mirror
   current behavior exactly. It lives with the reassembler or the call
   processor — propose the placement.
2. **Boundary decisions consume Frame events**: update()'s drain (currently
   trace-only) becomes the decision feed; per-state handlers consult the
   matrix. Two FR-06 decision points only: construct completion (learning —
   already live in the candidate) and frame boundary (acting).
3. **classify() reduction**: the `collecting`/`hs_conclusion_rcvd`/
   `hs_ack_tis_rcvd` gates are parse-position facts — migrate them onto
   reassembler roles so classify() stops maintaining parallel state.
   Migrate incrementally; every step keeps the full suite green.
4. **LINKED termination migration** (the centerpiece): `linked_twas_addr_`/
   `linked_twas_last_ms_` retire; handle_linked()'s Tdrw settle reads the
   completed frame from the reassembler (grammar-typed F_SOUND, mid-frame)
   and the matrix cell LINKED × conclusion-frame applies the F-05 decision:
   exact full-address match vs `active_call_to` → LINK_TERMINATED, anything
   else → discard (FR-08). ForeignTwasLinkGuard must pass UNMODIFIED.

## Hard constraints

- **ForeignTwasLinkGuard and RxCharacterization pass unmodified** — they are
  the behavior pins. If a Phase-3 step genuinely requires changing a pinned
  observable, STOP and present the change for approval first; do not
  silently re-pin.
- **The termination semantics are LOCKED** (owner decision,
  LINKED_TWAS_TERMINATION_HANDOFF.md): a TWAS anchor alone never terminates;
  only the completed termination frame (TWAS[peer] + DATA/REP extensions,
  `'@'` stuffing trimmed) terminates, decided at Tdrw settle, exact
  full-address match vs `active_call_to`. The known transient trade-off
  (while armed ≤Tdrw, DATA words feed the accumulator instead of re-arming
  the AMD-confirm path — fail-safe, self-recovers) is accepted: preserve it
  or flag it explicitly before changing it. The accumulator-equivalent
  (reassembler candidate) must clear on LINKED entry exactly like
  `linked_twas_addr_` does.
- **Behavior-preserving otherwise.** Known word-level triggers that are
  candidates to REMAIN word-level — decide each explicitly, document in
  §10 "intentionally remaining":
    - `react_calling_` TWAS rejection → CALL_REJECTED + LINK_TIMEOUT at the
      WORD (RxCharacterization TEST 7 pins the state change immediately
      after the word). Under strict OF-0 this should defer to the boundary;
      migrating it changes pinned observable timing — keep it word-level
      unless the owner approves the re-pin. Note: TWAS[addr] in a rejection
      frame is also just a conclusion word — the frame view types it; only
      the DECISION timing is in question.
    - LBT channel-busy + scanning dwell freeze: channel-occupancy facts,
      not protocol semantics (§4 note) — remain word-level by design.
    - `detect_incoming_call_` (TO_SELF → CALL_DETECTED): arming at the
      anchor is FR-06.1 (construct-completion arm), not a word-level
      shortcut — legal, but the matrix row should name it.
- **PAL conventions** (scripts/check_conventions.sh + clang-tidy):
  pal::get_logger() only, never printf/fprintf in src/; inter-component
  notifications via the PAL event bus (new EventType in
  include/PAL/events.h + payload struct in include/App/ale_event_data.h +
  dispatch() inside ale_controller.cpp) — never std::function on_* fields
  outside include/PAL/, never dispatch() from outside ALEController. The
  reassembler is pull-model on purpose — keep it that way (its consumer is
  the SM/controller, not callbacks).
- **State-machine hazards**: set_*_callback() handlers fire synchronously
  mid-transition — never call back into the SM from inside them, defer via
  tick_*(). trigger_linked_orderwire() auto-appends TIS: callers pass
  content-only words. SM-internal single-thread contract holds.
- **Test-harness traps (hit twice already)**: drain loops MUST capture
  get_words_pending() BEFORE looping (it decrements per on_word_complete —
  `i < get_words_pending()` converges at half). build_ack_words() is
  deferred one update() tick (unlike the synchronous response build).
  MSVC debug crashes (0x80000003) swallow buffered stdout — put
  `std::cout << std::unitbuf;` at the top of any new test main() when
  diagnosing.
- **Terminology** (owner preference): CALLING / RESPONSE / ACK precisely;
  never "ack" where "Response" is meant — code, comments, docs, test names.
- **Comments**: match surrounding density; no speculative prose. Docs: flat
  mechanism-first.
- **Scope**: 2G ALE (MIL-STD-188-141B Appendix A) only; AQC-ALE out of
  scope; no DBM modem (the reassembler's DBM accounting hook is inert).
- **Process**: work on `develop`; check `git status` first (concurrent
  sessions may have touched the tree). One commit per step, each shippable,
  full ctest green + conventions clean before each commit. main /
  0.0.4-pre-alpha are cherry-picked only if the owner decides — until then
  develop-only. RELEASE_NOTES.md untouched (no user-visible change) unless
  the owner asks.

## Suggested Phase-3 steps (propose adjustments, then implement stepwise)

- **3a — Matrix as data.** Introduce the §8 matrix (frame type × state →
  ACT/OBSERVE/IGNORE) + query function, no behavior change: write it to
  mirror today's behavior, add tests asserting the table matches the
  current react_* gating (a characterization of the matrix itself).
- **3b — Termination migration.** LINKED branch: retire linked_twas_addr_/
  linked_twas_last_ms_; handle_linked() reads the completed frame at the
  same settle. ForeignTwasLinkGuard green unmodified. This is the highest-
  risk step — do it alone, commit alone.
- **3c — classify() reduction.** Migrate the collecting gates onto
  reassembler roles, one gate at a time (hs_conclusion_rcvd, hs_ack_tis_rcvd,
  collecting_remote_conclusion, linked arming if anything remains).
  RxCharacterization + FrameReassembler equivalence green after each.
- **3d — Dispatch reorganization.** The per-state react_* structure mirrors
  the matrix (state × frame-type rows); word-level remainders from the
  constraints list get their §10 "intentionally remaining" entries.
- Update docs/FRAMING_STANDARD.md §10 after every step (it is the status
  record).

## Definition of done (Phase 3)

- The §8 matrix exists as a table with a query function and governs frame
  interpretation; the LINKED termination decision runs through it.
- linked_twas_addr_/linked_twas_last_ms_ are gone; the termination path
  reads reassembler Frame events with identical observable behavior
  (ForeignTwasLinkGuard 6/6 unmodified).
- classify()'s state-gates are reduced to reassembler parse-position roles
  wherever they were parse-position facts; the rest are listed in §10 with
  reasons.
- OF-0 audit: grep the react paths for direct single-word→process_event
  wiring outside the FR-06 decision points; every survivor is listed in §10
  as intentionally remaining, with the reason.
- Full suite green (69+ tests), conventions clean, one-or-more shippable
  commits on develop, §10 updated, memory written back (the OFS entity in
  the persistent graph carries the phase history).

Start by re-reading FRAMING_STANDARD.md §8/§10 and the reassembler header,
then present a short implementation plan for 3a–3d (files touched, test
list, risks) before writing code.
```