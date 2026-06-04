# FEAT-FRAME-005 — Conclusion

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-003` — Leading Call (Tlc-Phase)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-010 — Conclusion: Struktur und Worttypen
**Spec:** `A.5.2.5.3 / Figure A-17` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Conclusion ist der dritte Abschnitt aller Frames. Sie besteht aus entweder TIS- oder TWAS-Wörtern (aber nicht aus beiden) sowie möglicherweise DATA- und REP-Wörtern. Sie beginnt nach dem Ende des Message-Abschnitts bzw. nach dem Ende des Calling Cycle, wenn kein Message-Abschnitt vorhanden ist, und erstreckt sich bis zum Ende des Calls. Sowohl Conclusions als auch Sounds enthalten die vollständige Adresse der sendenden Station.

### REQ-FRAME-011 — Conclusion bei Sounds und Ausnahme-Frames
**Spec:** `A.5.2.5.3 / A.5.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Sounds und Exception-Frames beginnen unmittelbar mit TIS- oder TWAS-Wörtern, ohne vorangehenden Calling Cycle oder Message-Abschnitt, gemäß den Regeln in A.5.3. Ein REP-Wort darf nicht unmittelbar auf ein TIS- oder TWAS-Wort folgen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-010 — Conclusion: Struktur und Worttypen (`A.5.2.5.3 / Figure A-17`)
- [ ] ⬜ **`AC-FRAME-010-1`** — Die Conclusion enthält entweder ausschließlich TIS-Wörter oder ausschließlich TWAS-Wörter als Typ-Anker — niemals beide Typen im selben Frame.
- [ ] ⬜ **`AC-FRAME-010-2`** — Die Conclusion kann zusätzlich DATA- und REP-Wörter enthalten.
- [ ] ⬜ **`AC-FRAME-010-3`** — Die Conclusion enthält die vollständige Adresse der sendenden Station.
- [ ] ⬜ **`AC-FRAME-010-4`** — Liegt kein Message-Abschnitt vor, beginnt die Conclusion unmittelbar nach dem Calling Cycle.
- [ ] ⬜ **`AC-FRAME-010-5`** — Die Conclusion erstreckt sich bis zum Ende des Calls.

### REQ-FRAME-011 — Conclusion bei Sounds und Ausnahme-Frames (`A.5.2.5.3 / A.5.3`)
- [ ] ⬜ **`AC-FRAME-011-1`** — Bei Sounds und Exception-Frames beginnt die Aussendung direkt mit TIS- oder TWAS-Wörtern.
- [ ] ⬜ **`AC-FRAME-011-2`** — Ein REP-Wort folgt niemals unmittelbar auf ein TIS- oder TWAS-Wort.

## 🧪 Tests
- `tests/ale_modem_integration_test.cpp`

## 💡 Implementierungshinweise

TIS (Accept) oder TWS/TWAS (Reject) + vollst. eigene Adresse. RX öffnet am Trw-Grid-Boundary nach letztem Wort.

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
