# FEAT-LINK-003 — Group Call TX

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD` &nbsp;|&nbsp; **Domain:** `LINK`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-LINK-002` — Individual & Net Call TX
- ⬜ `FEAT-ADDR-003` — Net, Group, AllCall, AnyCall Adressen

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-LINK-003 — Group Call
**Spec:** `A.5.2.5.1 / A.5.2.3.4.2` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Ein Gruppenruf adressiert mehrere Stationen; der Scanning-Call verwendet alternierende Routing-Wörter, der Leading-Call die vollständigen Adressen aller Ziele.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-LINK-003 — Group Call (`A.5.2.5.1 / A.5.2.3.4.2`)
- [ ] ⬜ **`AC-LINK-003-1`** — Jede Zieladresse erscheint im Leading-Call vollständig.
- [ ] ⬜ **`AC-LINK-003-2`** — Die Scanning-Sequenz endet nicht mit einem Wiederholungswort.

## 🧪 Tests
- `tests/test_link_establishment.cpp`

## 💡 Implementierungshinweise

group_targets statt active_call_to. Scanning: THRU/REP. Leading: geflattenet, zweites Ziel mit REP nicht TO.

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
