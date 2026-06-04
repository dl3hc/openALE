# FEAT-SYNC-001 — Trw-Grid (Sendeseitiger Wortphasen-Anker)

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `SYNC`

## 📁 Module
- `src/ale_state_machine.cpp`
- `include/ale_state_machine.h`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-001` — Frame-Grundstruktur & Wortbasis

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SYNC-001 — Asynchroner Systembetrieb und Frame-interne Word-Sync
**Spec:** `A.5.2.6` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das ALE-System ist inhärent asynchron und erfordert keine systemweite Synchronisation, ist jedoch mit solchen Verfahren kompatibel. Die innerhalb eines Frames eingebettete Timing- und Strukturinformation stellt die notwendigen Ankerpunkte bereit, um die Wortsynchronisation (Word Sync) während Linking-, Orderwire- und Anti-Interferenz-Funktionen zu erzielen und aufrechtzuerhalten.

### REQ-SYNC-002 — Konstante Wortphasenbeziehung im Sendemodulator
**Spec:** `A.5.2.6.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der ALE-Sendemodulator akzeptiert digitale Daten vom Encoder und gibt moduliertes Basisband-Audio an den Sender weiter. Nach dem Beginn der ersten Übertragung einer Station muss der ALE-Sendemodulator eine konstante Phasenbeziehung — innerhalb der vorgeschriebenen Timing-Genauigkeit — zwischen allen gesendeten dreifach-redundanten Wörtern zu jedem Zeitpunkt aufrechterhalten, bis der letzte Frame der Übertragung beendet ist. Es gilt:

### REQ-SYNC-003 — Word-Phase-Tracking nur innerhalb einer Übertragung
**Spec:** `A.5.2.6.1 (NOTE)` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das Word-Phase-Tracking wird ausschließlich innerhalb einer Übertragung durchgeführt und nicht zwischen separaten Übertragungen.

### REQ-SYNC-004 — Unabhängigkeit des Sendemodulators vom Empfänger
**Spec:** `A.5.2.6.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die interne Wortphasenreferenz des Sendemodulators muss vom Empfänger unabhängig sein und muss innerhalb der geforderten Genauigkeit selbsttaktend (self-timed) arbeiten. Auf A.5.1.4 wird verwiesen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SYNC-001 — Asynchroner Systembetrieb und Frame-interne Word-Sync (`A.5.2.6`)
- [ ] ⬜ **`AC-SYNC-001-1`** — Das System erfordert keine systemweite externe Synchronisation und muss auch ohne diese korrekt arbeiten.
- [ ] ⬜ **`AC-SYNC-001-2`** — Das System ist mit Verfahren zur systemweiten Synchronisation kompatibel, sofern solche eingesetzt werden.
- [ ] ⬜ **`AC-SYNC-001-3`** — Die innerhalb eines Frames eingebettete Timing- und Strukturinformation wird zur Erzielung und Aufrechterhaltung der Word-Sync während Linking-, Orderwire- und Anti-Interferenz-Funktionen genutzt.

### REQ-SYNC-002 — Konstante Wortphasenbeziehung im Sendemodulator (`A.5.2.6.1`)
- [ ] ⬜ **`AC-SYNC-002-1`** — Nach Beginn der ersten Übertragung hält der Sendemodulator die konstante Phasenbeziehung zwischen allen gesendeten dreifach-redundanten Wörtern aufrecht.
- [ ] ⬜ **`AC-SYNC-002-2`** — Die Differenz der Ereigniszeitpunkte zweier dreifach-redundanter Wörter innerhalb einer Übertragung ist stets ein ganzzahliges Vielfaches von Trw = 392 ms.
- [ ] ⬜ **`AC-SYNC-002-3`** — Die Phasenbeziehung wird bis zum Ende des letzten Frames der Übertragung gehalten.
- [ ] ⬜ **`AC-SYNC-002-4`** — Die Phasenbeziehung wird innerhalb der in A.5.1.4 vorgeschriebenen Timing-Genauigkeit eingehalten.

### REQ-SYNC-003 — Word-Phase-Tracking nur innerhalb einer Übertragung (`A.5.2.6.1 (NOTE)`)
- [ ] ⬜ **`AC-SYNC-003-1`** — Das Word-Phase-Tracking ist auf die Dauer einer einzelnen Übertragung begrenzt.
- [ ] ⬜ **`AC-SYNC-003-2`** — Zwischen zwei getrennten Übertragungen wird keine fortgesetzte Wortphasenbeziehung aufrechterhalten.

### REQ-SYNC-004 — Unabhängigkeit des Sendemodulators vom Empfänger (`A.5.2.6.1`)
- [ ] ⬜ **`AC-SYNC-004-1`** — Die Wortphasenreferenz des Sendemodulators ist zu keinem Zeitpunkt von der Empfängeraktivität abhängig.
- [ ] ⬜ **`AC-SYNC-004-2`** — Der Sendemodulator arbeitet selbsttaktend innerhalb der in A.5.1.4 vorgeschriebenen Genauigkeit.

## 🧪 Tests
- `tests/test_sync.cpp`

## 💡 Implementierungshinweise

next_slot_ms = first_call_tx_ms + call_cycle_count * WORD_DURATION_MS. first_call_tx_ms nie ändern. Selbsttaktend.

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
