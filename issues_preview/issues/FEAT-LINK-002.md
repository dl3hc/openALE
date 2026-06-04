# FEAT-LINK-002 — Individual & Net Call TX

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `LINK`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-005` — Conclusion
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-LINK-001 — Individual Call
**Spec:** `A.5.5.3.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein Ruf an eine einzelne Station durchläuft Scanning-Call, Leading-Call, Conclusion und Empfangsfenster.

### REQ-LINK-002 — Net Call
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Ein Netzruf verwendet denselben Ablauf wie der Individual Call; der Unterschied liegt in der Adressklassifikation.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-LINK-001 — Individual Call (`A.5.5.3.1`)
- [ ] ⬜ **`AC-LINK-001-1`** — Die gesendete Wortfolge entspricht: Scanning (TO-Erstwort), Leading (vollständige Adresse ×2), Conclusion (eigene Adresse).

## 🧪 Tests
- `tests/ale_modem_integration_test.cpp`

## 💡 Implementierungshinweise

Gleicher Ablauf SCANNING→LEADING→CONCLUSION→LISTENING. active_call_type unterscheidet nur intern.

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
