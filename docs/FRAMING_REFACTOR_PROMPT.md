# OFS Refactoring — Kickoff Prompt

Self-contained handoff prompt for the session that implements the openALE
Framing Standard in code. Paste the fenced block below into a fresh Claude
Code session on branch `develop` (or run `claude "$(cat docs/FRAMING_REFACTOR_PROMPT.md)"`).

---

```markdown
# Task: Refactor openALE's frame TX/RX layers onto the Framing Standard (OFS)

## Context you must load first (read in this order, before any code changes)

1. `docs/FRAMING_STANDARD.md` — the normative standard (OFS). You are making
   the code converge onto it. Its §10 "Current-code mapping" is your migration
   map; §11 is the compliance checklist every phase must satisfy.
2. `docs/LINKED_TWAS_TERMINATION_HANDOFF.md` — the incident + fix that OFS
   generalizes. The shipped LINKED-TWAS termination path is the reference
   implementation of OFS compliance (arm → accumulate → Tdrw settle → exact
   full-address match). It must survive this refactoring byte-for-byte in
   behavior. Do not regress it.
3. Source ground truth for the current mechanisms:
   - TX: `include/Word/ale_sequence.h` + `src/Word/ale_sequence.cpp`
     (ALESequenceBuilder), `src/Word/address_encoder.cpp`, `src/Word/ale_word.cpp`
   - RX: `src/Protocol/Control/ale_call_processor.cpp` (classify() at the top,
     per-state react_*, LINKED branch ~line 426-486),
     `src/Protocol/Control/ale_state_machine.cpp` (handle_linked() settle
     decision ~line 901-911), `include/Protocol/Message/ale_message.h`
     (MessageAssembler/CallTypeDetector), `include/App/sounding_identity_accumulator.h`
   - Tests: `tests/link/unit/test_foreign_twas_link_guard.cpp`
     (ctest `ForeignTwasLinkGuard`, 6 tests). Full suite is 66/66 green on
     develop HEAD (`0986a61`) and must stay green after every phase.

## Goal

Converge the four ad-hoc frame-reassembly mechanisms onto the OFS target
architecture (§4 layer model), in incremental, behavior-preserving phases:

- one FrameReassembler on RX (roles assigned from parse position, per FR-01..05),
  absorbing over time: (a) the `collecting`/`hs_conclusion_rcvd`/`hs_ack_tis_rcvd`
  gates in classify(), (b) `linked_twas_addr_`/`linked_twas_last_ms_` in the SM,
  (c) `MessageAssembler`/`CallTypeDetector`, (d) `SoundingIdentityAccumulator`.
- ALESequenceBuilder promoted to FrameBuilder on TX (§7): catalog-typed
  constructors + a grammar validator (FR-09) that hard-fails illegal word
  lists at build time (address ≤5 words, stuffing only in last word, DATA/REP
  alternation, TIS xor TWAS, first conclusion extension is DATA; payload
  limits per §6.1: AMD ≤30 data words/90 chars, ≤30 CMD words, space-stuffing;
  DTM sizes per Table A-XXXII + mandatory CMD CRC; DBM declared lengths).
- The per-state react_* dispatch reorganized to mirror the §8 context matrix
  (state × frame type), so "which frame types may act in this state" is a
  table, not scattered conditions.

## Hard constraints

- **Behavior-preserving.** No shipped protocol decision may change. Before
  touching RX, write characterization tests that pin current observable
  behavior (standalone SM harness technique: compile
  `src/Protocol/Control/ale_state_machine.cpp` directly with g++/MinGW — no
  CMake, no hamlib — for deterministic protocol tests). The suite must be
  green before and after every phase.
- **The termination semantics are locked** by owner decision: a TWAS anchor
  alone never terminates; only the completed termination frame
  (TWAS[peer] + DATA/REP extensions, `'@'` stuffing trimmed, REP carries new
  chars) terminates, decided at Tdrw settle, exact full-address match vs
  `active_call_to`; a non-matching TWAS disarms; the accumulator clears on
  LINKED entry. The known transient trade-off (while armed ≤Tdrw, DATA words
  feed the accumulator instead of re-arming the AMD-confirm path — fail-safe,
  self-recovers) is accepted; preserve it or flag it explicitly before
  changing it.
- **PAL conventions** (enforced by `scripts/check_conventions.sh` +
  clang-tidy): `pal::get_logger()` only, never printf/fprintf; inter-component
  notifications via the PAL event bus (new `EventType` in
  `include/PAL/events.h` + payload struct in `include/App/ale_event_data.h` +
  `dispatch()` inside `ale_controller.cpp`) — never `std::function on_*`
  fields outside `include/PAL/`, never `dispatch()` from outside ALEController.
- **State-machine hazards** (established project knowledge): SM
  `set_*_callback()` handlers fire synchronously mid-transition — never call
  back into the SM from inside them, defer via `tick_*()`.
  `trigger_linked_orderwire()` auto-appends TIS: callers pass content-only
  words, never a pre-built frame with its own TIS.
- **Terminology** (owner preference): use CALLING / RESPONSE / ACK precisely;
  never write "ack" where "Response" is meant, in code, comments, docs, or
  test names.
- **Comments**: match surrounding density; no speculative prose; restate
  facts plainly. Docs: flat mechanism-first, no rhetorical contrast, no
  unverifiable claims.
- **Scope**: 2G ALE (MIL-STD-188-141B Appendix A) only. AQC-ALE (§A.5.8) is
  explicitly out of scope. DBM deep-interleave demodulation does not exist;
  the FR-01 self-declared-block exception is design text — do not build a DBM
  modem, but do not architect anything that would contradict it.
- **Process**: work on `develop`; tests live only on develop. Check
  `git status` first (concurrent sessions may have touched the tree). One
  commit per phase, each shippable. main / 0.0.4-pre-alpha are cherry-picked
  afterwards only if the phase changes behavior-relevant code — this
  refactor should be develop-only until the owner decides about porting.

## Phased plan (propose adjustments, then implement phase by phase)

**Phase 0 — Characterization.** Standalone-SM harness + characterization
tests pinning: termination full-address semantics (already covered by
ForeignTwasLinkGuard — keep), handshake conclusion accumulation
(`collecting_remote_conclusion`, `hs_conclusion_rcvd`, `hs_ack_tis_rcvd`),
MessageAssembler AMD assembly, sounding accumulation. No production changes.

**Phase 1 — TX FrameBuilder (§7).** Add catalog-typed build entry points and
the grammar validator over ALESequenceBuilder. Every existing caller migrates
to the catalog API; the validator runs on every built sequence. Unit-test the
validator (each illegal shape from the spec flowcharts rejected:
>15-char address, stuffing not in last word, TIS+TWAS in one frame, REP
directly after TIS/TWAS, non-alternating DATA/REP extension, AMD over Tm max).
Tag emitted sequences with their frame type (F-xx) for logging/tests.

**Phase 2 — RX FrameReassembler skeleton.** New component (suggest
`src/Protocol/Frame/`, `include/Protocol/Frame/`) implementing FR-01..FR-05
as a grammar parser: frame boundary (Tdrw silence / out-of-grammar restart /
DBM declared-length exception), redundancy collapse, address accumulation
(anchor + alternating extensions, ≤5 words, '@'-trim, spacing gate 2×Trw),
conclusion identity/exclusivity. It observes the word stream in parallel
(shadow mode) and must produce identical classifications to today's
classify()/react_* paths — assert this in the characterization tests.
Payload blocks (§6.1): CMD-opened blocks, boundaries (next CMD / conclusion /
frame end), FR-11 owner precedence; DBM suspends word grammar per FR-01
exception.

**Phase 3 — Context matrix (§8).** Reorganize the per-state reactions so the
state × frame-type dispatch mirrors the matrix. FrameReassembler output now
drives decisions; WordRole/classify() reduces to reassembler roles. The
LINKED termination path migrates from `linked_twas_addr_` to the
reassembler's conclusion accumulator with identical observable behavior:
ForeignTwasLinkGuard must pass unmodified.

**Phase 4 — Absorb the remaining accumulators.** MessageAssembler /
CallTypeDetector become a Frame view; SoundingIdentityAccumulator consumes
F_SOUND frame events. Update `docs/FRAMING_STANDARD.md` §10 mapping as each
step lands (mark what converged), so the doc stays the status record.

Each phase ends with: full ctest green, `scripts/check_conventions.sh` clean,
conventions/clang-tidy clean, commit on develop.

## Definition of done

- One FrameReassembler exists; the four ad-hoc mechanisms are either absorbed
  or explicitly listed in §10 as intentionally remaining (with reason).
- FrameBuilder is the only construction path for TX word lists in protocol
  code; the grammar validator rejects every illegal shape from the spec
  flowcharts.
- Context matrix governs interpretation; no word-level state-change trigger
  exists anywhere (OF-0 audit: grep the react paths for direct
  single-word→process_event wiring outside the two FR-06 decision points).
- All existing tests pass unmodified (they are the behavior pin);
  characterization tests from Phase 0 pass before AND after.
- New foreign-traffic tests per §11 item 5 for each frame type whose handling
  changed structurally (at minimum: termination and AMD paths).
- `docs/FRAMING_STANDARD.md` §10 updated to reflect landed state;
  RELEASE_NOTES.md untouched unless the owner asks (no user-visible change).

Start by re-reading the two docs listed above, then present a short
implementation plan for Phase 0–1 (files touched, test list, risks) before
writing code.
```