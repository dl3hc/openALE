# FEAT-FRAME-003 — Leading Call (Tlc-Phase)

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-002` — Scanning Call (Tsc-Phase)
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-004 — Leading Call: Zusammensetzung und Adressinhalt
**Spec:** `A.5.2.5.1 / Figure A-15` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Leading Call besteht aus TO- und möglicherweise DATA- und REP-Wörtern, die die vollständige Adresse(n) der gerufenen Station(en) enthalten. Der Leading Call beginnt mit der Einleitung des Leading Call und endet mit dem Beginn des Message-Abschnitts bzw. des Conclusion-Abschnitts. Die Verwendung von REP und DATA ist in A.5.2.4 festgelegt.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-004 — Leading Call: Zusammensetzung und Adressinhalt (`A.5.2.5.1 / Figure A-15`)
- [ ] ⬜ **`AC-FRAME-004-1`** — Der Leading Call enthält die vollständige(n) Zieladresse(n) einschließlich aller Erweiterungswörter (DATA, REP).
- [ ] ⬜ **`AC-FRAME-004-2`** — Der Leading Call beginnt mit dem Einleitungszeitpunkt des Leading Call und endet spätestens mit dem Beginn des Message- oder Conclusion-Abschnitts.
- [ ] ⬜ **`AC-FRAME-004-3`** — Die Verwendung von DATA- und REP-Wörtern im Leading Call entspricht den Regeln aus A.5.2.4.

## 🧪 Tests
- `tests/ale_modem_integration_test.cpp`

## 💡 Implementierungshinweise

Vollständige Zieladresse 2x senden. Pro Trw-Slot 1 Wort. call_cycles_in_phase % seq.size() = aktuelles Wort.

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
