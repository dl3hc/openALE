# Handoff — LINKED-state TWAS termination (full-address frame recognition)

Date: 2026-08-31
Branches: develop `5c96743` (+ test `304a44f`) · main `1f0fb89` · 0.0.4-pre-alpha `6a8396f`

## Problem

While LINKED to **DC7SU** and exchanging AMD, a third station (**SL3ZXB**) started
sounding on the same channel. A sounding concludes with TWAS — and TWAS is also the
link-termination word. The sounding station's `TWAS[SL3]` tore down the link
(`LINKED → IDLE`). Root cause at incident time: any received TWAS in LINKED fired
`LINK_TERMINATED` unconditionally, regardless of sender.

**History:** a first guard (2026-08-27, all branches) required a prefix match vs the
linked peer — but it compared only the **anchor word** (first ≤3 address chars).
That left a real hole: a foreign station sharing the peer's first 3 chars (a
**DC7XY** sounding while linked to **DC7SU** — realistic with German callsign
blocks) would still kill the link, on the anchor word, before its distinguishing
extension ever arrived.

## Required semantics (owner decision)

`TWAS[DC7]` alone **must not** terminate. Only the **whole peer address** in the
standardized termination frame may: linked to DC7SU ⇒ only `TWAS[DC7], DATA[SU@]`
terminates. No word-level shortcuts — termination is recognized by the defined
frame, not by an early per-word decision.

## Implementation

- **Termination frame** (unchanged construct): `TO[peer]×2` + conclusion
  `TWAS[peer] + DATA/REP` address extensions (AddressEncoder alternates DATA/REP;
  each extension carries *new* address chars, `'@'` stuffing trimmed — REP is not
  a repeat).
- **`src/Protocol/Control/ale_call_processor.cpp`, LINKED branch:** a TWAS
  prefix-matching `sm.active_call_to` **arms** `linked_twas_addr_`; following
  DATA/REP words append (spacing-gated at 2×Trw, on-air cadence); a non-matching
  TWAS **disarms**. No event fires on any single word.
- **`src/Protocol/Control/ale_state_machine.cpp`, `handle_linked()`:** after the
  **Tdrw settle** (same window the handshake uses for multi-word conclusions),
  terminate **only on exact full-address match** vs `active_call_to`; any mismatch
  is discarded. Accumulator cleared on LINKED entry (no stale arming across links).
- **`classify()`:** DATA/REP count as address extensions in LINKED *only while a
  TWAS conclusion is accumulating* — scoped like the CALLING/HANDSHAKE gates, so
  AMD payload words are unaffected. Known transient trade-off: while armed
  (≤ Tdrw), DATA words are consumed by the accumulator instead of the AMD-confirm
  re-arm — fail-safe direction, self-recovers.
- **Timing:** genuine termination is delayed by one Tdrw (~784 ms) — harmless, the
  peer is gone either way. 3-char peers (anchor = full address) terminate after
  settle as before.
- New public getter `ALEStateMachine::get_active_call_to()` (mirrors
  `get_caller_address()`).

## Regression pin

`tests/link/unit/test_foreign_twas_link_guard.cpp` (ctest `ForeignTwasLinkGuard`,
6 tests; full suite 66/66):

1. Incident replay — LINKED to DC7SU, 8 repeated SL3ZXB sounding bursts → link
   survives, no termination TX, peer binding unchanged
2. Unrelated foreign callsign (OH2) ignored
3. `TWAS[DC7]` anchor **alone** does NOT terminate; the full conclusion
   `TWAS[DC7]+DATA[SU@]` terminates at settle
4. AMD exchange with foreign sounding interleaved → survives
5. Shared-prefix foreign station DC7XY (`TWAS[DC7]+DATA[XY]`), single and
   repeated → survives the full-address compare
6. 3-char peer (JOE): anchor IS the full address → terminates at settle

The test only exists on develop (main / 0.0.4-pre-alpha have no tests/ tree).

## Open items

- **Rebuild the radio-PC binary** — the deployed one predates even the 2026-08-27
  guard; both the original bug and the shared-prefix hole are only closed in
  binaries built from ≥ these commits.
- The 2026-08-27 prefix-only guard is superseded; no migration needed (same event,
  same state flow externally).