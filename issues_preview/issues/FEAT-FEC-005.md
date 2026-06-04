# FEAT-FEC-005 — Unanimous-Votes-Erfassung

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FEC`

## 📁 Module
- `src/ale_fec_codec.cpp`
- `include/ale_fec_codec.h`

## 🔗 Depends on
- ⬜ `FEAT-FEC-004` — 3x Redundanz mit Majority-Vote (RX)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FEC-019 — Unanimous-Votes-Erfassung
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Anzahl der einstimmigen Stimmen der 48 möglichen Stimmen, die mit diesem Majority-Wort verbunden sind, müssen temporär für die Verwendung gemäß A.5.2.6 zurückgehalten werden.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FEC-019 — Unanimous-Votes-Erfassung (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-019-1`** — Die Anzahl der einstimmigen Stimmen muss für die 48 Bits erfasst werden
- [ ] ⬜ **`AC-FEC-019-2`** — Die erfassten Werte müssen temporär gespeichert werden

## 🧪 Tests
- `tests/test_fec_majority_vote.cpp`

## 💡 Implementierungshinweise

Zählt einstimmige Votes (alle 3 gleich) über 48 Bits. Ergebnis temporär für FEAT-SYNC-003 speichern.

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
