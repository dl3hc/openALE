# Handoff — Bilateral LQA exchange, reverse direction: caller's report in the ACK (KA1 asymmetry)

Date: 2026-08-31
Branch: `develop`, baseline commits `a6b682c` (RxCMD TABLE A-XVI decode logging) and
`5b2f2ef` (issue #5: CALLING budget vs LQA-report responses). Both committed; this
document is the implementation handoff for the *remaining* spec deviation.

## Topic

openALE's bilateral LQA exchange (the "Request LQA" setting, `lqa_exchange_enabled`,
CMD 'a' / Block C5) is **one-directional**. The caller requests and receives the
responder's LQA report, but the responder never requests and the caller never sends
its own report. Per MIL-STD-188-141B A.5.4.2 the CMD 'a' word's KA1 bit is a *request*
either station may set; per MIL-STD-187-721D §5.4.3.1 the full bilateral exchange is a
three-frame handshake in which the caller's report rides the **ACK frame**. The result
in openALE today: the caller ends the handshake with rich bilateral data, the responder
with only the caller's single-channel CMD 'a' measurement — the responder's
`LQAAnalyzer::bilateral_channel_score` stays under-informed.

Concretely, two complementary gaps:

1. **Responder never requests.** At call-alert time the responder builds its CMD 'a'
   with `request_report` hardcoded false: `src/App/ale_controller.cpp:2374`
   (`lqa_exchange_.encode_outgoing(cur_blka4_ch.rx_frequency_hz,
   sm_.get_caller_address(), /*request_report=*/false)` — Block A4, responder role).
2. **Caller never reports.** The caller-side `apply_pending` is called with
   `can_queue_c5=false` (`src/App/ale_controller.cpp:2711`), and
   `ALEStateMachine::build_ack_words()` (`src/Protocol/Control/ale_state_machine.cpp:1927`)
   transmits a bare `TO×2 + TIS/TWAS` with no message section — nothing could carry the
   report even if it were queued.

Working reference path (caller → responder, already implemented, use as the template):
caller queues CMD 'a' KA1=1 at `ale_controller.cpp:1241`; responder captures it in
`rx_handle_lqa_exchange` (`ale_controller.cpp:3250`, capture windows at `:3262-3277`),
and queues its report at `:2384` (`apply_pending(caller, /*can_queue_c5=*/true, …)`);
`build_response_words()` (`ale_state_machine.cpp:1954`) inserts
`CMD 'a' + CMD 'r' + DATA…` before the TIS conclusion and scales its TX-drain deadline.

## What the standards declare

### MIL-STD-188-141B — the word, the bit, the frame hook

| Where | Declares |
|---|---|
| **A.5.4.1** (p. 115) | LQA stored values "shall be available to be transmitted upon request"; stations insert CMD LQA in the message section of their signals and handshakes "when requested by the handshaking **station(s)**" — plural, symmetric intent. |
| **A.5.4.2** (p. 116) + **TABLE A-XIV** (p. 118) | CMD 'a' word format; control bit **KA1 at W11**. Body text (symmetric): "when KA1 is set to '1,' the **receiving station** shall respond with an LQA report in the handshake. If KA1 is set to '0,' the report is not required." Table note 2 (directional): "KA1 requests an LQA within the handshake **from the called station**." |
| **A.5.4.3** (p. 118) | "Historical LQA report: **See MIL-STD-187-721**." — the multi-channel report (openALE's Block C5, CMD 'r' + DATA) is *not defined in 141B*. |
| **A.5.5.3.3** (pp. 129-130) | Caller's response-frame abort criteria: conclusion "starting within **Tlc (plus Tm max, if message included)**" — the rule behind `response_conclusion_window_ms_()` (`ale_state_machine.cpp:1197`). |
| **A.5.5.3.4** (pp. 130-131) | ACK frame (fig. A-31: `TO JOE / TIS SAM`). The ACK-waiting station's abort criteria also read "**(plus Tm max, if message included)**" — the standard explicitly anticipates a **message-bearing ACK frame**. NOTE 2: a typical one-to-one three-way handshake takes 9-14 s. |
| **A.5.8.4** (p. 220) | Tm max basic = 30×Trw = 11.76 s; incl. AMD = 29+30 = **59×Trw = 23.128 s**; incl. DTM = 382×Trw; incl. DBM = 3589×Trw. (Constants: `Tm_max_ms`, `Tm_max_amd_ms` in `include/Protocol/Control/ale_timing.h`.) |
| **TABLE A-XVI** (p. 141) | CMD function summary — `'r'` = LQA report; `'a'` = LQA. Decoded and logged on RX by module `RxCMD` (commit `a6b682c`). |

### MIL-STD-187-721D — the actual bilateral protocol (141B defers here)

| Where | Declares |
|---|---|
| **§4.4 / §4.4.2** (pp. 13-14) | Passive LQA is *unilateral*; "bilateral LQA data may be obtained by using one of the active LQA techniques such as **polling or LQA reporting**." |
| **§5.4.3.1 Individual poll: two stations, one channel** (p. 17) | **The key sentence:** "A two-station poll may be performed using a 3-way handshake with **an LQA request in the call, an LQA report with a request in the response, and an LQA report in the acknowledgment (which may also terminate the link)**." Frame 1 = CMD 'a' KA1=1; frame 2 = responder's report + its own KA1=1 request; frame 3 = **caller's report in the ACK**, which may conclude TWAS instead of TIS to end the link after the exchange. |
| **§5.4.3.2.1.c** (pp. 17-18, hub net poll) | "If any responding station requests LQA from the hub in its response, the hub **shall** insert an LQA report in its acknowledgment" — mandatory on request; use all-1s "no report" fields where needed. Figure 3 note: "*Indicates an LQA request. Its absence indicates data only." — the same CMD 'a' doubles as request vs. data-only. |
| **§5.4.3.2.2** (pp. 18-19, full net poll) | ACK message-section pattern to copy: "the message section in the acknowledgment **shall start with a 'no report' LQA CMD, followed by LQA reports embedded in DATA words**" (slot-ordered; non-responders filled with "no report" words). |
| **§5.4.4 LQA report protocol** (pp. 22-26) | Report CMD 'r' format (fig. 6); **TABLE V** control bits (p. 24): KR5 = DTM vs DBM packing, KR4-2 = report format — data-only 16-bit (Age 3 + MP 3 + SINAD 5 + BER 5), +channel 23-bit, **+frequency 36-bit** (openALE's 36-bits-per-entry packing matches this); fig. 7 report formats (p. 25); **fig. 8 LQA report request CMD** (p. 26) with control bits selecting requested format + **Age field** per **TABLE VI** (p. 26; 110 = report all measured channels, 111 = all common channels incl. no-data). The fig.-8 request word is richer than 141B's single KA1 bit — optional future work, NOT required for this fix. |

## Where to find the standards

- `docs/standards/ALE_standard_188_141B_extracted.json` — 141B text extraction, pages
  41-236, `elements[]` of paragraph/section_header/table/figure objects with page
  numbers. Relevant clusters: A.5.4.x ≈ elements 1323-1400 (pp. 115-119),
  A.5.5.3.x ≈ 1524-1562 (pp. 128-131), TABLE A-XVI ≈ 1700 (p. 141),
  A.5.8.4 ≈ 2801-2865 (p. 220). Parse with python, `PYTHONIOENCODING=utf-8`
  (cp1252 console chokes on '∼'). Raw PDFs also in `docs/standards/` (gitignored dir).
- `docs/standards/MIL-STD-187-721D.PDF` — 67 pages; extract with
  `pip install pypdf` → `PdfReader(path).pages[i].extract_text()`. Printed page
  numbers are PDF index − 11. Relevant printed pages: 13-14 (§4.4.x), 17-19
  (§5.4.3.x, figures 3/4), 22-26 (§5.4.4, tables V/VI, figures 6/7/8).

## Required semantics

1. Responder's CMD 'a' in the response carries **KA1=1** (it requests the caller's
   report), per §5.4.3.1 "an LQA report **with a request** in the response".
2. Caller, on decoding a KA1=1 CMD 'a' in the response, queues **its own Block C5
   report** (same builder as the responder's: `LqaExchangeManager::apply_pending` with
   `can_queue_c5=true` → `ALESequenceBuilder::lqa_report`).
3. ACK frame becomes `TO[peer]×2 + [CMD 'r' + DATA…] + TIS[self]` when a report is
   pending; **byte-identical to today** (`TO×2 + TIS`) when none — a plain call must
   not change on air.
4. Responder decodes the ACK's report: the C5 RX path
   (`on_report_cmd` / `on_report_data`, `ale_controller.cpp:3294+`) is already
   state-independent — no new RX code needed; verify sender resolution falls back to
   `get_caller_address()` (it does) and that the report lands in the LQA DB via
   `update_bilateral`.

## Implementation plan

**Step 1 — responder sets KA1.** `src/App/ale_controller.cpp:2374`: flip
`request_report` to `true`. ⚠️ Side effect to check: `encode_outgoing()`
(`src/LQA/lqa_exchange.cpp:58-75`) sets `sent_ka1_`/`last_call_target_` when
`request_report` is true, and `on_call_concluded()` (`lqa_exchange.cpp:161-168`) then
calls `db_.mark_bilateral_attempted()` on failure paths — now also on the responder
side. Verify that flag's consumers tolerate that, or split the request-bookkeeping
from the KA1 bit (open decision D3).

**Step 2 — caller queues its report.** In `rx_handle_lqa_exchange`'s CALLING/LISTENING
capture branch (`ale_controller.cpp:3262-3277`), after `on_word_lqa_cmd()`, if the
captured payload has KA1=1, call `apply_pending(<dialed target>, /*can_queue_c5=*/true,
emit)`. Peer = the station we dialed (the response's TO is *our* address; the peer is
known from the call context — `sm_.get_to_address()` is only set at TIS, so resolve
from the call target). This runs ~10+ s before the ACK is built — safely before
`build_ack_words()` consumes `pending_lqa_report_seq_`. (No controller hook exists at
TIS; capture-time is the natural point.)

**Step 3 — ACK carries the report.** `build_ack_words()`
(`ale_state_machine.cpp:1927`): consume `pending_lqa_report_seq_` exactly like
`build_response_words()` does (`:1954-2015`): insert `CMD 'r' + DATA…` before the
TIS conclusion, and scale `tx_drain_deadline_ms_` to `words_pending`
(mirror `:2009-2014`). Only on the accept (TIS) path — see D2.

**Step 4 — caller budget covers the longer ACK.** ⚠️ The issue-#5 fix
(`5b2f2ef`) made `check_link_timeout()` CALLING use the response window
(`ale_state_machine.cpp:1784-1801`) *through SENDING_ACK*. A report-carrying ACK adds
up to 30 message words (~11.8 s); worst case (16-entry report response + 16-entry
report ACK) the elapsed time since `response_rx_start_ms` can exceed the current
window (~26.7 s for 3-char addresses) and would fire LINK_TIMEOUT *mid-ACK* — the
mirror image of issue #5. Fix: gate the response-window branch on
`calling_phase == LISTENING` only, and let SENDING_ACK be bounded by its scaled
TX-drain deadline (armed in Step 3); or extend `response_conclusion_window_ms_()`
by a Tm-max-basic term. Recommend the former (the drain deadline is the correct
liveness bound for a TX phase). Add/adjust the test in
`tests/link/unit/test_calling_lqa_response_budget.cpp` accordingly.

**Step 5 — tests.**
- SM level (extend `tests/link/unit/test_calling_lqa_response_budget.cpp`): response
  with CMD 'a' KA1=1 → captured ACK frame (WordCapture) contains `CMD 'r' + DATA`
  before TIS; ACK drains → LINKED; responder WAIT_ACK receives report-bearing ACK →
  LINKED (harness pattern: virtual clock on the Trw grid, one `update(t)` +
  `process_received_word`/`on_word_complete` per word — note the SM's non-monotonic
  clock guard silently holds backward-timestamped updates).
- Controller level (extend the existing `test_bilateral_lqa` / `test_lqa_bilateral_rx`
  suites): KA1=1 capture → report queued; responder-side decode → `update_bilateral`
  written.
- Regression: KA1=0 response → ACK **byte-identical** to today's bare `TO×2 + TIS`.

**Step 6 — live validation.** Both stations "Request LQA" on; the `RxCMD` log module
(commit `a6b682c`) shows every on-air CMD word — expect `CMD 'a'` (KA1) in call *and*
response, `CMD 'r'` in response *and* ACK. Then consider re-enabling the
"Request LQA" default (currently off, `dfc1f78`, the issue-#5-era workaround) and
closing https://github.com/dl3hc/openALE/issues/5.

## Constraints (do not regress)

- `kMaxReportEntries = 16` (`include/LQA/lqa_exchange.h`, with `static_assert`) keeps
  one report's message section within Tm max basic = 30 words. It applies to the ACK
  report too — same cap, same `apply_pending` path.
- Keep commits separate and conventional (`fix(link): …`); follow
  `scripts/check_conventions.sh` (no printf/fprintf outside the allowlist, pal logger
  for diagnostics).
- Per-channel `reporting_inhibited` gating stays on both new paths.

## Open decisions (owner)

- **D1:** responder always KA1=1, or configurable/conditional (e.g. only when its LQA
  matrix lacks data for the caller)?
- **D2:** include the report in the TWAS-concluded ACK (AMD-decline path)? Spec allows
  ("may also terminate the link", §5.4.3.1); recommend TIS-only for the first cut.
- **D3:** accept `mark_bilateral_attempted` on the responder side, or split the
  `sent_ka1_` bookkeeping from the KA1 bit?
- **D4:** Step 4 budget approach — LISTENING-gated window (recommended) vs. extended
  window constant.