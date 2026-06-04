# FEAT-LINK-001 — FSK Symbol-Detektor (RX-Pfad)

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `LINK`

## 📁 Module
- `src/ale2gmodem.cpp`
- `include/ale2gmodem.h`

## 🔗 Depends on
- ✅ `FEAT-WAVEFORM-001` — Tone-Symbol-Mapping & Frequenztabelle
- ✅ `FEAT-WAVEFORM-002` — NCO-Tongenerator mit Phasenkontinuität

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-LINK-004 — Signal- und Symbolerkennung
**Spec:** `A.5.1.2 / A.5.2.6.2 · ⚠ Siehe OPEN-08` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das System erkennt aus dem empfangenen Audiosignal die übertragenen Symbole.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-LINK-004 — Signal- und Symbolerkennung (`A.5.1.2 / A.5.2.6.2 · ⚠ Siehe OPEN-08`)
- [ ] ⬜ **`AC-LINK-004-1`** — Ein sauber gesendetes Wort wird fehlerfrei zurückgewonnen (Loopback).

## 🧪 Tests
- `tests/test_symbol_detector.cpp`

## 💡 Implementierungshinweise

Goertzel-Filter für 8 feste Frequenzen. k=round(64*freq/8000). Energiemaximum = Symbol. Aktuell Placeholder.

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
