# FEAT-FRAME-004 — Message-Abschnitt

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-003` — Leading Call (Tlc-Phase)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-007 — Quick-ID: Optionaler Transmitter-Identifier am Beginn des Message-Abschnitts
**Spec:** `A.5.2.5.1 / A.5.2.5.2` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Der Übergang vom Calling Cycle zum Message-Abschnitt kann optional durch eine Quick-ID markiert werden. Die Quick-ID belegt die ersten Wörter des Message-Abschnitts, nach dem Leading Call und vor dem restlichen Message-Inhalt (oder dem Conclusion-Abschnitt, sofern kein weiterer Message-Inhalt folgt). Die Quick-ID besteht aus FROM- und möglicherweise REP- und DATA-Wörtern, die die vollständige Adresse des Senders enthalten. Die Quick-ID darf nur einmalig am Beginn der CMD-Message-Sequenzen verwendet werden und niemals ohne nachfolgende CMD-Message(s).

### REQ-FRAME-008 — Message-Abschnitt: Struktur und Zusammensetzung
**Spec:** `A.5.2.5.2 / Figure A-16` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Message-Abschnitt ist der zweite und optionale Abschnitt aller Frames (außer Sounds). Er besteht — abgesehen von der optionalen Quick-ID — aus CMD- und möglicherweise REP- und DATA-Wörtern, vom Ende des Calling Cycle bis zum Beginn des Conclusion-Abschnitts. Der Message-Abschnitt beginnt stets mit dem ersten CMD-Wort (oder, bei verwendeter Quick-ID, mit dem FROM-Wort, gefolgt von CMD-Wörtern). Der Message-Abschnitt wird innerhalb eines Calls nicht wiederholt, auch wenn Nachrichten oder Informationen innerhalb des Message-Abschnitts selbst wiederholt sein können. Die Verwendung von REP und DATA ist in A.5.7.3 festgelegt.

### REQ-FRAME-009 — Message-Abschnitt: AQC-ALE-Sonderregel
**Spec:** `A.5.2.5.2 / A.5.8.2.3` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD`

> Im Rahmen von AQC-ALE ist der Message-Abschnitt verfügbar, wenn sich das System im Link-Zustand befindet. Das dritte Bein (Acknowledgement Leg) eines Calls kann dabei als Inlink-Entry-Bedingung genutzt werden.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-007 — Quick-ID: Optionaler Transmitter-Identifier am Beginn des Message-Abschnitts (`A.5.2.5.1 / A.5.2.5.2`)
- [ ] ⬜ **`AC-FRAME-007-1`** — Eine Quick-ID, sofern verwendet, steht ausschließlich an der ersten Position des Message-Abschnitts, nach dem Leading Call.
- [ ] ⬜ **`AC-FRAME-007-2`** — Die Quick-ID besteht aus FROM- und möglicherweise REP- und DATA-Wörtern mit der vollständigen Senderadresse.
- [ ] ⬜ **`AC-FRAME-007-3`** — Die Quick-ID wird höchstens einmal pro Frame verwendet, am Beginn der CMD-Message-Sequenz.
- [ ] ⬜ **`AC-FRAME-007-4`** — Eine Quick-ID wird niemals ohne mindestens eine nachfolgende CMD-Message gesendet.
- [ ] ⬜ **`AC-FRAME-007-5`** — Wird keine Quick-ID verwendet, beginnt der Message-Abschnitt direkt mit dem ersten CMD-Wort.

### REQ-FRAME-008 — Message-Abschnitt: Struktur und Zusammensetzung (`A.5.2.5.2 / Figure A-16`)
- [ ] ⬜ **`AC-FRAME-008-1`** — Der Message-Abschnitt enthält — abgesehen von der optionalen Quick-ID — ausschließlich CMD-, REP- und DATA-Wörter.
- [ ] ⬜ **`AC-FRAME-008-2`** — Der Message-Abschnitt beginnt immer mit dem ersten CMD-Wort oder, bei Quick-ID, mit dem FROM-Wort.
- [ ] ⬜ **`AC-FRAME-008-3`** — Der Message-Abschnitt wird innerhalb eines Calls genau einmal gesendet (keine Wiederholung des gesamten Abschnitts).
- [ ] ⬜ **`AC-FRAME-008-4`** — Der Message-Abschnitt erstreckt sich vom Ende des Calling Cycle bis zum Beginn des Conclusion-Abschnitts.

### REQ-FRAME-009 — Message-Abschnitt: AQC-ALE-Sonderregel (`A.5.2.5.2 / A.5.8.2.3`)
- [ ] ⬜ **`AC-FRAME-009-1`** — Bei AQC-ALE steht der Message-Abschnitt nur im Link-Zustand zur Verfügung.
- [ ] ⬜ **`AC-FRAME-009-2`** — Das Acknowledgement Leg (drittes Bein) eines Calls kann als Inlink-Entry-Bedingung verwendet werden.

## 🧪 Tests
- `tests/test_message_section.cpp`

## 💡 Implementierungshinweise

Optional nach Leading Call. Quick-ID, dann Wortsequenz. Tm max basic = 11.76s. AQC-ALE-Sonderregel beachten.

---
## 🤖 Agent-Workflow

```
1. Dieses Issue auf status: in-progress setzen
2. Alle module-Pfade lesen
3. Für jede Spec-Referenz: MIL-STD-188-141B Appendix A, Abschnitt X lesen
4. Nur implementieren, was in den Acceptance Criteria steht
5. Für jedes AC mindestens einen Test schreiben
6. ctest ausführen — alle Tests müssen grün sein
7. Alle AC-Checkboxen abhaken → status: done
```
