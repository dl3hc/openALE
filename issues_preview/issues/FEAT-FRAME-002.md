# FEAT-FRAME-002 — Scanning Call (Tsc-Phase)

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-001` — Frame-Grundstruktur & Wortbasis
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-002 — Calling Cycle: Gliederung und Bestandteile
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der initiale Abschnitt aller Frames — mit Ausnahme von Sounds — ist der Calling Cycle (Tcc). Dieser besteht aus zwei Teilen: dem Scanning Call (Tsc) und dem Leading Call (Tlc). Der Calling Cycle endet mit dem Beginn des Message-Abschnitts oder, sofern kein Message-Abschnitt vorhanden, mit dem Beginn des Conclusion-Abschnitts.

### REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Scanning Call setzt sich bei Individual- oder Net-Calls aus TO-Wörtern zusammen, die ausschließlich das erste Wort der Adresse der gerufenen Station(en) oder des Netzes enthalten. Bei einem Group Call besteht der Scanning Call abwechselnd aus THRU- und REP-Wörtern mit demselben eingeschränkten Adressinhalt. Der Satz unterschiedlicher Adress-Erstworte (Tcl) darf zur Abdeckung der Suchlaufperiode (Ts) so oft wie nötig wiederholt werden.

### REQ-FRAME-005 — Frame-Synchronisation: Kein dediziertes Sync-Word
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein ALE-Frame besitzt kein dediziertes „Flag Word" oder „Sync Word" für die Frame-Synchronisation. Empfangsstationen können den ALE-Signalempfang und die Dekodierung an jedem beliebigen Punkt nach dem Beginn der Übertragung aufnehmen.

### REQ-FRAME-006 — Sender-Hochlaufzeit: 90-Prozent-Leistung
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Sender muss innerhalb von 2,5 ms nach der ersten Tonübertragung nach Rufinitiierung mindestens 90 Prozent der gewählten HF-Sendeleistung erreicht haben. Die zulässige Verzögerung von 2,5 ms gilt zusätzlich zu der in 5.3.5.1 festgelegten Attackzeit. Nichterfüllung der 90-%-Bedingung beeinträchtigt die Verlinkungswahrscheinlichkeit; die Bedingung gilt als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-002 — Calling Cycle: Gliederung und Bestandteile (`A.5.2.5.1`)
- [ ] ⬜ **`AC-FRAME-002-1`** — Jeder Frame (außer Sounds) beginnt mit einem Calling Cycle.
- [ ] ⬜ **`AC-FRAME-002-2`** — Der Calling Cycle besteht aus genau zwei Teilen: Scanning Call und Leading Call.
- [ ] ⬜ **`AC-FRAME-002-3`** — Der Calling Cycle endet mit dem Beginn des Message- oder, bei fehlendem Message-Abschnitt, des Conclusion-Abschnitts.

### REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt (`A.5.2.5.1`)
- [ ] ⬜ **`AC-FRAME-003-1`** — Bei Individual- und Net-Calls enthält der Scanning Call ausschließlich TO-Wörter mit dem ersten Adresswort der gerufenen Station(en).
- [ ] ⬜ **`AC-FRAME-003-2`** — Bei Group Calls enthält der Scanning Call abwechselnd THRU- und REP-Wörter.
- [ ] ⬜ **`AC-FRAME-003-3`** — Der Scanning Call enthält in beiden Fällen nur das jeweils erste Wort der Zieladresse, keine vollständige Adresse.
- [ ] ⬜ **`AC-FRAME-003-4`** — Der Satz der Adress-Erstworte (Tcl) wird so oft wiederholt, bis die Suchlaufperiode (Ts) überschritten wird.
- [ ] ⬜ **`AC-FRAME-003-5`** — Die Dauer des Scanning Call (Tsc) ist ≥ Ts.

### REQ-FRAME-005 — Frame-Synchronisation: Kein dediziertes Sync-Word (`A.5.2.5.1`)
- [ ] ⬜ **`AC-FRAME-005-1`** — Der Sender fügt kein dediziertes Flag- oder Sync-Wort zur Frame-Synchronisation ein.
- [ ] ⬜ **`AC-FRAME-005-2`** — Eine Empfangsstation ist in der Lage, das ALE-Signal an einem beliebigen Punkt nach Übertragungsbeginn zu synchronisieren und zu lesen.

### REQ-FRAME-006 — Sender-Hochlaufzeit: 90-Prozent-Leistung (`A.5.2.5.1`)
- [ ] ⬜ **`AC-FRAME-006-1`** — Die Sendeleistung beträgt spätestens 2,5 ms nach dem ersten ALE-Ton mindestens 90 % der gewählten HF-Leistung.
- [ ] ⬜ **`AC-FRAME-006-2`** — Die zulässige 2,5-ms-Verzögerung ist additiv zur Attackzeit aus 5.3.5.1.
- [ ] ⬜ **`AC-FRAME-006-3`** — Die Einhaltung gilt auch dann als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist (Compliance-Äquivalenz).

## 🧪 Tests
- `tests/ale_modem_integration_test.cpp`

## 💡 Implementierungshinweise

INDIVIDUAL/NET: TO + erste 3 Chars, kein DATA/REP. GROUP: THRU/REP alternierend. tsc_slots = C*2.

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
