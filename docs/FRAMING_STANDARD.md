# openALE Framing Standard (OFS)

Normative internal standard for how openALE constructs, parses, and interprets
ALE transmissions. Derived from MIL-STD-188-141B Appendix A (§A.5.2.4 – A.5.3)
and from the LINKED-TWAS link-loss incident (see
[LINKED_TWAS_TERMINATION_HANDOFF.md](LINKED_TWAS_TERMINATION_HANDOFF.md)).

Status: proposed standard. The LINKED-TWAS termination path already complies
(2026-08-31, develop `5c96743`); the rest of this document generalizes that
pattern so the compliance becomes structural rather than per-case.

---

## 1. Why this standard exists

On 2026-08-31 a third station's sounding (`TWAS[SL3]` + extension) tore down a
live LINK to DC7SU. Two prior fixes both failed for the same underlying reason:

- 2026-08-27 guard: compared the TWAS **anchor word** (first ≤3 chars) against
  the peer → a foreign DC7XY still killed a DC7SU link, because the anchor
  cannot discriminate addresses that share their first word.
- Root cause (original bug): a **single word** was given frame-level meaning —
  "TWAS received in LINKED" fired LINK_TERMINATED regardless of sender.

The bug class is not "TWAS was mishandled". It is: **assigning frame-level
semantics to word-level events.** A.5.2.6.3 itself names the acceptance
criteria for a word — "History, status, expectations, and protocol" — because
the standard's word set is small (8 preambles) and deliberately overloaded.
The same preamble means different things in different frames and states:

| Word on air | Possible meanings |
|---|---|
| `TWAS[DC7]` | sounding conclusion (A.5.3) · call rejection (A.5.3.2) · link-termination conclusion (A.5.5.3.5) |
| `DATA[SU@]` | address extension (A.5.2.4.4.2) · AMD text payload (A.5.7) · LQA report field · DBM block |
| `TIS[XYZ]`  | sound inviting calls (A.5.3) · response/ACK conclusion (A.5.5.3.3/4) · in-link identification |
| `TO[ABC]`   | scanning call (A.5.2.5.1) · leading call (A.5.2.5.1) · response/ACK/termination addressee |

Only the frame — the complete word sequence in its reception context —
determines the meaning. This standard makes that rule explicit and enforces it
on both the transmit and receive side.

## 2. Frame anatomy (per the standard)

From §A.5.2.5, every ALE transmission is a **frame** with three sections:

```
┌ calling cycle (Tcc) ┬ Tsc: scanning call   — first word(s) only, repeated to exceed Ts
│                    └ Tlc: leading call     — whole address(es), sent twice
├ message (optional) — quick-ID (FROM…) then CMD (+ REP/DATA extension)
└ conclusion         — TIS or TWAS (never both) + whole transmitter address
```

- **Word** (A.5.1.2): 24 bits = 3-bit preamble + 3 × 7-bit characters.
  Eight preambles (Table A-II): DATA, THRU, TO, TWAS, FROM, TIS, CMD, REP.
  Character set is preamble-bound: routing words take Basic 38 (A.5.2.4.2);
  DATA/REP take Expanded 64 (A.5.7.2.1).
- **Address** (A.5.2.4.4): an anchor word (TO/THRU/FROM/TIS/TWAS) plus
  extension words alternating DATA, REP, DATA, REP — max 5 words / 15 chars.
  `'@'` stuffing only in the last word (A.5.2.4.3); each extension carries
  *new* characters, REP is not a repeat of DATA.
- **Redundancy** (A.5.2.5.3 flowchart): every word is sent 3× (Trw = 392 ms
  per logical word); the scanning section is repeated to exceed the receivers'
  scan period Ts; the leading call is doubled. Repetition is transport, not
  content.
- **No sync word** (A.5.2.5.1): a receiver may acquire at any point in a
  frame. A parse that starts mid-frame is legal input and must be handled.
- **Conclusion identity rule** (A.5.2.5.3): the conclusion carries the
  **transmitting** station's whole address. A TWAS/TIS address is always the
  sender's identity, never a callee. REP never immediately follows TIS/TWAS
  (first extension is DATA).
- **Sounds** (A.5.3): conclusion-only frames. TIS = calls invited,
  TWAS = calls not invited. Whole address only.
- **Payload protocols** (§A.5.7): user data never free-floats; it rides in the
  message section, opened by a CMD word. AMD (mandatory, A.5.7.2): CMD word
  carries the *first three message characters*, followed by alternating
  DATA/REP; last word space-stuffed (`0100000`); the block ends at the start
  of the conclusion or at the next CMD. Tm max = 30 data words (90 chars),
  59 words counting CMD words, ≤30 CMD words per message section (A.5.7.2.3).
  DTM (optional, A.5.7.3): CMD 'd' word (KD1–KD4 control bits + 10 DC size
  bits), BASIC/EXTENDED/NULL/ARQ modes, data block in DATA/REP words closed by
  a mandatory CMD CRC; extends the Tm limit. DBM (optional, A.5.7.4):
  deep-interleaved bit blocks. During a DTM exchange frame conclusions stay
  TIS until all blocks are transferred (and ACKed under ARQ); TWAS only for
  one-way broadcast or forced abandon (A.5.7.3).
- **Frame boundary**: there is no delimiter word. A frame ends when the
  channel goes silent: Tdrw = 2×Trw = 784 ms without an accepted word.

## 3. The Axiom

> **OF-0 — Words are syntax; frames are semantics.**
> A received word is evidence that someone transmitted a 24-bit pattern.
> No protocol state may change as a direct reaction to a bare word. State
> changes happen only at the decision points defined in FR-06.

This is the generalization of the owner decision from the handoff ("no
word-level shortcuts — termination is recognized by the defined frame") to
every frame type openALE handles.

## 4. Layer model

```
TX                                     RX
────                                   ────
Frame intent (catalog call, §6)        bits → WordParser (syntax, FEC, char set)
        │                                       │ ALEWord
        ▼                                       ▼
FrameBuilder (§7) — validates         FrameReassembler (§5) — accumulates,
  grammar, emits ALESequence            assigns roles from parse position,
        │                               emits complete Frame events
        ▼                                       ▼
encoder/modulator (tripling)          SM + controller — act on Frame events
                                        gated by context matrix (§8)
```

Rules of the layers:

- **TX:** nothing below FrameBuilder may construct a frame; nothing above it
  may concatenate raw sections. See FR-09.
- **RX:** nothing above the FrameReassembler sees bare words for protocol
  purposes. (LQA sampling, word logging, and LBT channel-busy detection are
  word-level by nature and stay below/at the reassembler — they are signal
  quality and channel-occupancy facts, not protocol semantics.)

## 5. The FrameReassembler grammar rules

The receive side keeps one parser whose state is the parse position inside the
frame grammar. Roles come from position, not from ad-hoc state flags.

**FR-01 — Frame boundary.** A frame ends at (a) Tdrw of silence after the last
accepted word, or (b) the first word that cannot extend the current grammar,
which starts a fresh candidate frame at that word. Two transmissions from the
same station never merge; two stations colliding produce out-of-grammar
sequences and both candidates are dropped. Per A.5.7.3/4 the receiver keeps
reading a colliding, significantly stronger ALE signal in parallel without
confusing it with the in-progress payload block — a second candidate frame
opens under (b).
**Exception — self-declared blocks:** a DBM block carries its own length in
its CMD word (BC bits); the boundary is at the declared end (ID × 64 ms).
Silence inside the declared block is a fade, not a frame end — FR-01(a) is
suspended until the declared end.

**FR-02 — Redundancy collapse.** Consecutive accepted words with identical
(type, payload) collapse to one logical word. Which repeats are legal is part
of the grammar (Tsc repetition, Tlc doubling, 3× tripling); a repeat pattern
the grammar does not allow is treated as FR-01(b).

**FR-03 — Address accumulation.** An anchor (TO/THRU/FROM/TIS/TWAS) opens an
address accumulator; following DATA/REP words extend it (alternation
DATA, REP, DATA, REP; ≤5 words; trailing `'@'` trimmed). The address
**completes** at the next non-extension word or at frame end. An anchor alone
is never an identity — this is the general form of "`TWAS[DC7]` alone must not
terminate."

**FR-04 — Extension attribution.** An extension word belongs to the anchor it
follows within the same frame, gated on word spacing: a DATA/REP arriving more
than Tdrw (2×Trw) after the last accumulated word starts a new frame, it does
not extend the address. Extensions in the message section belong to the
message block; they never attach to a calling-cycle or conclusion address.
A payload block opened by a CMD ends only at the next CMD, at the start of
the conclusion, or at the frame boundary (A.5.7.2.2) — those are the block
terminators.

**FR-05 — Conclusion exclusivity and identity.** TIS and TWAS never occur in
one frame. The conclusion's completed address is the sender's identity. First
extension after TIS/TWAS is DATA, never REP (A.5.2.5.3).

**FR-06 — Decision points.** Protocol decisions are permitted at exactly two
kinds of points:
1. **Construct completion** — an address (or message block) completed inside a
   frame, e.g. the caller's whole address identified during a handshake
   conclusion. Use for *learning* (bind `caller_address`, heard-list, LQA).
2. **Frame boundary** — FR-01(a) settle. Use for *acting* (state
   transitions, termination, delivery confirmations).

   A rule that wants to act earlier must be expressed as arm → accumulate →
   decide at the boundary. Any "fire on word" shortcut is a violation of OF-0.

**FR-07 — Context gating.** Interpretation of a frame type is attempted only
in the SM states listed for it in the catalog (§6). In every other state the
frame is observed (LQA, heard-list, logging) but causes no state change. This
implements A.5.2.6.3's "history, status, expectations, and protocol" as a
table instead of scattered conditions.

**FR-08 — Foreign-traffic safety.** In committed states (LINKED, HANDSHAKE)
any frame whose completed addresses reference neither self nor the active peer
is observation-only. When a parse is ambiguous, discard toward **no state
change**: a missed genuine termination costs at most one Tdrw and a retry; a
false termination destroys a working link. Fail-safe direction is always
"keep the current state".

**FR-09 — TX legality.** Frames are created only through FrameBuilder catalog
functions (§7). Each constructor validates the grammar before encoding. Raw
`ALEWord` list construction outside the builder is forbidden in protocol code.
The set of transmittable frames equals the catalog; anything else is a bug by
construction.

**FR-10 — Frame-level logging.** RX diagnostics name the frame type and the
completed addresses (`F_TERMINATION from DC7SU to JOE`), not just the last
word. Word-level logs remain for modem/FEC diagnosis. (Precedent: the "TWS"
vs "TWAS" label divergence — same bits, different label — cost a verification
round-trip that frame-level labels would have avoided.)

**FR-11 — Payload protocol precedence.** The message section's interpretation
belongs to the CMD word that opened it. Words that happen to match another
payload protocol's format inside that block are consumed by the owning
protocol and ignored by the other payload functions (A.5.7.2.3: DTM takes
precedence over AMD within its block). The reassembler routes payload words
to the block owner — never to address accumulation (FR-03/04) and never to a
second protocol.

## 6. Frame catalog

Every frame openALE transmits or interprets is one of these types. Grammar
notation: `A` = anchor, `[...]` optional, `...` repetition, `addr(n)` =
anchor + up to n−1 extensions. Special addresses per Table A-IX / A.5.2.4.7–12:
AllCall `@?@`, selective AllCall `@A@`, AnyCall `@@?`, selective AnyCall
`@@A`, null `@@@`, in-link `?@?`.

| # | Frame type | Grammar (logical words) | Sender → Receiver | Receiver decision (at FR-06 point) |
|---|---|---|---|---|
| F-01 | `F_CALL` individual/net | `TO(addr) [DATA/REP...] ×n` (Tsc) `+ TO×2 addr` (Tlc) `[FROM quick-ID] [CMD...]` `+ TIS/TWAS self` | caller → callee | anchor addressed to self → arm; whole callee address → bind `active_call_to`; conclusion address (construct) → bind `caller_address`; at boundary → accept (TIS) / reject (TWAS) |
| F-02 | `F_CALL` group | Tsc: `THRU/REP` first words alternating; Tlc: all whole addresses ×2 | caller → members | per member: same as F-01; star protocol per A.5.5.4.3 |
| F-03 | `F_RESPONSE` | `TO×2 caller + TIS self` | callee → caller | only in HANDSHAKE awaiting response; conclusion address must equal expected caller (construct) → LINKED at boundary |
| F-04 | `F_ACK` | `TO×2 peer + TIS/TWAS self` | caller → callee | same grammar as F-03; **disambiguated by state only** (who called whom). TIS → link established; TWAS → AMD decline |
| F-05 | `F_TERMINATION` | `TO×2 peer + TWAS self` | either peer | only in LINKED; at boundary, completed sender identity == `active_call_to` (exact full-address match) → LINK_TERMINATED; mismatch → discard (FR-08) |
| F-06 | `F_SOUND` | `TIS/TWAS self` (repeated; scanning variant repeats to exceed Ts) | any station | never a state change. TIS/TWAS → availability flag; address → heard-list + LQA. In LINKED this includes peer's keep-alive behavior via F-09 |
| F-07 | `F_ALLCALL` | `TO @?@ ...` (or selective `@A@`) `+ TIS/TWAS self` | any station | freeze + collect if uncommitted (A.5.5.4.4); observation-only in committed states; GPR/position payload per AMD_ORDERWIRE.md |
| F-08 | `F_ORDERWIRE` (in-link exchange) | `TO×2 [FROM sender] CMD+payload... + TIS/TWAS` | linked station | usage pattern, not the only AMD carrier: per A.5.7.2.2 payload blocks ride in **any** frame (see P-table below); in LINKED, payload is message content and the conclusion address identifies the sender for delivery confirmation (construct) |
| F-09 | `F_INLINK` (keep-alive) | `TO ?@? (×2) + TIS member` (A.5.2.4.12) | linked member | only in LINKED; extends Twa idle timer; no state change |
| F-10 | `F_LQA` | `CMD('a'/'r'/'n') [+ DATA/REP fields]` | any / in link | LQA + channel metrics only; never a state change (P-1 payload) |

Notes on the catalog:

- **F-03 / F-04 / F-05 share one grammar** (`TO×2 addr + conclusion`). The
  word stream cannot distinguish them; reception context (who initiated, which
  handshake phase) does. This is the clearest proof of OF-0: a parser that
  maps grammar → meaning alone must misclassify, and FR-07's context matrix is
  the only sound disambiguator.
- **F-06 is never actionable.** A sound's TWAS is the same on-air construct as
  F-05's conclusion. The incident of 2026-08-31 was, in catalog terms, F-06
  being read as F-05. The fix — and this standard — makes that path impossible
  by table, not by vigilance.
- New frame types start here: a catalog row (grammar, sender, decision rule,
  action states) *before* any code. A behavior that cannot be written as a row
  violates OF-0.

### 6.1 Payload protocols (§A.5.7 — message-section constructs)

Payload protocols are **not frame types**. A.5.7.2.2 requires the receiving
station to accept an AMD message "contained in **any** ALE frame, including
calls, responses, and acknowledgments" — so P-rows ride inside the message
section of F-01/F-03/F-04/F-08 (and F-07 for broadcasts), and the *carrying
frame's* context-matrix cell governs whether the payload is acted on. This is
also what openALE's AMD/link decoupling depends on: AMD in the calling frame,
frame 3 decides the link.

| # | Payload | Grammar (inside message section) | Block boundary (FR-04) | Constraints |
|---|---|---|---|---|
| P-1 | `P_AMD` (A.5.7.2, mandatory) | `CMD` (carries the **first 3 message chars**) `+ DATA/REP alternating` | next CMD, start of conclusion, or frame end | Expanded 64 chars; space-stuffing (`0100000`) in last word; Tm max 30 data words / 90 chars; ≤30 CMD words per message section; display verbatim, substitute error block on detected loss |
| P-2 | `P_DTM` (A.5.7.3, optional — partially implemented, `ale_orderwire_protocols.h`) | `CMD 'd'` (KD1–KD4 control bits + 10 DC size/mode bits) `+ DATA/REP block + CMD CRC` | CMD CRC closes the block; next CMD or conclusion closes the exchange | BASIC (≤651 bits/93 chars, exact size), EXTENDED (1–351 words), NULL (interrogate/break/terminate), ARQ (ACK/NAK/flow); one block per frame under ARQ, exact duplicates resent on NAK (≥7 tries); DTM **extends** Tm; conclusions stay TIS during the exchange, TWAS only for broadcast/forced abandon; KD1 alternates per block, KD2 marks same-message segments |
| P-3 | `P_DBM` (A.5.7.4, optional — partially implemented) | `CMD 'b'` (KB1–KB4 control bits + 10 BC mode/size/interleaver-depth bits) `+ deeply interleaved raw-bit block` — **not** DATA/REP words: Golay rows read out by matrix columns, 3 bits/symbol | declared in the BC bits: block time = interleaver depth ID × 64 ms; word grammar resumes at the declared block end | BASIC: fixed depth 49, ≤572 bits/81 chars; EXTENDED: ID multiple of 49, up to ~262 kbits (23.26 min); CRC-16 occupies the last 16 data-field bits; DEL/'1' stuffing; transparent binary or 7-bit ASCII (chars never split across blocks); extends Tm; under ARQ one block per frame, exact duplicates on NAK (≥7), NULL interrogates capability; conclusions stay TIS until all blocks are done, TWAS only for broadcast/forced abandon; redundant word phase preserved (expansion is a multiple of the Trw grid) |
| P-4 | `P_LQA` (CMD 'a'/'r'/'n', §A.5.6 / 187-721D) | `CMD [+ DATA/REP fields]` | next CMD, conclusion, or frame end | metrics only, never a state change |

Payload rules (already covered by FR rules, listed here for the checklist):

- FR-11 precedence: the block's owning CMD wins; overlapping-format words
  inside a block are ignored by other payload functions (A.5.7.2.3).
- FR-04: payload DATA/REP never attach to an address accumulator.
- A frame may carry multiple payload blocks, each opened by its own CMD
  (A.5.7.2.2) — the reassembler emits one Frame event with a list of blocks.
- Inside a DBM block the word grammar is suspended: FR-02 collapse and word
  acceptance criteria do not apply to the interleaved bit stream; the block is
  delimited by its declared length, and word sync resumes on the Trw grid
  afterwards (guaranteed — the expansion is a multiple of the Trw grid,
  A.5.7.4).

## 7. TX side — FrameBuilder

`ALESequenceBuilder` (include/Word/ale_sequence.h) is promoted to the
FrameBuilder layer of FR-09. Its current section functions
(`scanning_call`, `leading_call`, `conclusion`, `response`, `ack`,
`termination`, `from_id`, …) remain as the internals; the additions are:

1. **One constructor per catalog row** (`build(F_TERMINATION, peer, self)` in
   spirit), so the catalog is the API surface, sections are never hand-wired
   by callers.
2. **A grammar validator** run on every built word list before encode: legal
   preamble sequence, address limits (≤5 words, stuffing only in last word),
   DATA/REP alternation, TIS/TWAS exclusivity, conclusion-first-extension-is-
   DATA. Payload limits per §6.1: AMD ≤30 data words (90 chars), ≤30 CMD
   words per message section, space-stuffing in the last AMD word (A.5.7.2.3);
   DTM block sizes per Table A-XXXII, CMD CRC mandatory after a DTM block,
   one DTM block per frame under ARQ (A.5.7.3). The spec's own flowchart
   gates ("INVALID ADDRESS SEQUENCE! … ALERT OPERATOR OR CONTROLLER",
   A.5.2.5.1/3) become a hard failure at build time.
3. **Catalog tagging on TX**: each emitted sequence records its frame type for
   logging and for test assertions.

## 8. RX side — context matrix

FR-07 as a table. ✓ = frame type may act in that state; ○ = observation only
(LQA, heard-list, logging); ✗ = not interpreted.

| State ↓ / Frame → | F-01/02 call | F-03 response | F-04 ack | F-05 term. | F-06 sound | F-07 allcall | F-08 orderwire* | F-09 inlink | F-10 LQA |
|---|---|---|---|---|---|---|---|---|---|
| IDLE / SCANNING | ✓ (arm on TO self) | ✗ | ✗ | ○ | ○ | ✓ | ✓* (AMD inside F-01) | ✗ | ○ |
| CALLING | ○ | ✓ | ✗ | ✓ (peer rejection) | ○ | ○ | ○ | ✗ | ○ |
| HANDSHAKE | ✓ (handshake legs) | ✓ | ✓ | ○ | ○ | ○ | ✓* (AMD in F-03/F-04 legs) | ✗ | ○ |
| LINKED | ○ | ✗ | ✗ | ✓ (full-address only) | ○ | ○ | ✓ | ✓ | ○ |
| SOUNDING | ○ | ✗ | ✗ | ○ | ○ | ○ | ○ | ✗ | ○ |

\* A.5.7.2.2: AMD (and other P-payloads) are receivable inside **any** frame —
the F-08 cells marked ✓* are payloads of the carrying frame's row, not
independent frame detections. The state change (if any) is the carrying
frame's (F-01 call → HANDSHAKE, F-03 response → LINKED); the payload itself
only delivers content.

Read the incident against the LINKED row: F-06 is ○. A sound — from any
station, sharing any prefix with anyone — cannot reach a state transition
because no row in its column intersects LINKED with ✓.

The matrix replaces, over time, the ad-hoc gates currently scattered in
`classify()` (`collecting`, `hs_conclusion_rcvd`, `linked_twas_addr_`-arming)
— those are all parse-position facts and belong in the single reassembler.

## 9. Incident replay under OFS

SL3ZXB sounds on the shared channel while DC7SU ↔ openALE are LINKED and
exchanging AMD:

1. Words `TWAS[SL3]`, `DATA[ZXB]` arrive. FrameReassembler: no calling cycle
   seen (mid-frame acquisition is legal, A.5.2.5.1) → candidate frame is
   conclusion-only → **F-06** by grammar, sender identity `SL3ZXB` completed
   at FR-03.
2. Frame boundary at Tdrw settle → Frame event `F_SOUND from SL3ZXB`.
3. Context matrix: state LINKED × frame F-06 → ○. Heard-list/LQA update only.
4. No rule anywhere in the path contains "TWAS → terminate"; the word-level
   trigger no longer exists. The shared-prefix hole (DC7XY vs DC7SU) cannot
   open either, because F-06 never compares against the peer at all — and a
   genuine F_TERMINATION from DC7XY still fails the exact full-address match
   at the F-05 boundary decision.

The shipped 2026-08-31 fix is exactly rows 1–4 specialized to TWAS:
arm at anchor (FR-03 open), extend under spacing gate (FR-04), decide at
Tdrw settle with full-address compare (FR-06.2 + F-05 row). OFS does not
change that behavior; it names it and applies it to everything else.

## 10. Current-code mapping

| OFS concept | Current code | Gap |
|---|---|---|
| Word syntax layer | `WordParser` (src/Word/ale_word.cpp) | compliant |
| TX catalog + sections | `ALESequenceBuilder` (src/Word/ale_sequence.cpp) + `ALEFrameBuilder` (src/Protocol/Message/ale_frame_builder.cpp) | **converged (2026-08-31, FR-09)**: catalog constructors gate every TX frame through `FrameValidator::validate_frame()` (build-time "INVALID ADDRESS SEQUENCE" hard failure — empty sequence = refuse), sections/frames carry `FrameType` tags (`Word/frame_catalog.h`); F_CALL validated by `ALEStateMachine::call_frame_is_legal_()` at initiate time. Payload encoders (`encode_amd/dtm/dbm`, freq-select, version-caps) remain message-section internals by design (§6.1 P-rows, FR-11) |
| Address accumulation | `AddressEncoder` (TX); hand-rolled per case (RX): `collecting_remote_conclusion`, `hs_conclusion_rcvd`, `linked_twas_addr_`+`linked_twas_last_ms_` (src/Protocol/Control/ale_call_processor.cpp:54-64,444-467) | one accumulator, driven by FR-03/04, replaces the four ad-hoc ones |
| Role assignment | `classify()` + `WordRole` (ale_call_processor.cpp:37-114) | roles from parse position instead of state-flag ORs |
| Orderwire/message frames | `MessageAssembler` → `ALEMessage`, `CallTypeDetector` (include/Protocol/Message/ale_message.h) | becomes a Frame view; `CallTypeDetector` is the grammar classifier to fold into the reassembler |
| Boundary decision | `handle_linked()` settle (ale_state_machine.cpp:901-911); `tick_frame_settle()` (App); `SoundingIdentityAccumulator` | one settle scheduler feeds all Frame consumers |
| Context gating | per-state `react_*` switch (ale_call_processor.cpp:426-480) | converges to the §8 matrix |

Migration is incremental: each ad-hoc accumulator is already a partial
reassembler; the standard defines the target shape so future frame work
extends the grammar instead of adding a fifth parallel mechanism. No
behavioral change to shipped protocol decisions is required to comply —
FR-06's two decision points are where they already decide.

## 11. Compliance checklist (for every future protocol feature)

1. Named frame type added to §6 catalog — or payload row added to §6.1 —
   (grammar, sender, decision rule, action states) — or an explicit
   justification why existing rows cover it.
2. TX path goes through a catalog constructor; grammar validator passes.
3. RX path: decisions only at FR-06 points; no new word-level trigger.
4. Context-matrix row updated; committed-state entries default to ○.
5. Foreign-traffic test: traffic from an unrelated station with a shared
   address prefix must not change state (extend
   tests/link/unit/test_foreign_twas_link_guard.cpp pattern per frame type).
6. Logs identify frames, not just words (FR-10).

---

*Sources: MIL-STD-188-141B Appendix A §A.5.2.4.1–A.5.2.6.3 (addresses, frame
structure, valid sequences, synchronization), §A.5.3 (sounding),
§A.5.7–A.5.7.3 (AMD, DTM payload protocols; DBM per A.5.7.4);
docs/standards/ALE_standard_188_141B_extracted.json;
LINKED_TWAS_TERMINATION_HANDOFF.md (incident + required semantics);
implementation: src/Word/, src/Protocol/Control/ale_call_processor.cpp,
src/Protocol/Control/ale_state_machine.cpp.*