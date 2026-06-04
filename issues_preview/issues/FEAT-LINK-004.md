# FEAT-LINK-004 — End-of-Frame-Erkennung

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `LINK`

## 📁 Module
- `src/ale2gmodem.cpp`
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- ⬜ `FEAT-LINK-001` — FSK Symbol-Detektor (RX-Pfad)
- ⬜ `FEAT-FEC-004` — 3x Redundanz mit Majority-Vote (RX)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-LINK-005 — End-of-Frame-Erkennung
**Spec:** `A.5.5.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das Ende einer empfangenen Übertragung wird anhand einer gültigen Conclusion und der konstanten Wort-Phase erkannt.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-LINK-005 — End-of-Frame-Erkennung (`A.5.5.2.4`)
- [ ] ⬜ **`AC-LINK-005-1`** — Nach einer gültigen Conclusion plus definiertem Wartedelay gilt die Übertragung als beendet.

## 🧪 Tests
- `tests/test_link_establishment.cpp`

## 💡 Implementierungshinweise

TIS/TWAS + konstante Wortphase + Tlww=392ms. Max 4 Folgewörter (DATA/REP). Kein weiteres Wort = Frame-Ende.

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
