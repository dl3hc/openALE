# FEAT-WAVEFORM-002 — NCO-Tongenerator mit Phasenkontinuität

**Status:** ✅ `verified` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WAVEFORM`

## 📁 Module
- `extern/PC-ALE/src/fsk/tone_generator.cpp`

## 🔗 Depends on
- ✅ `FEAT-WAVEFORM-001` — Tone-Symbol-Mapping & Frequenztabelle

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WAVEFORM-004 — Codierung und Interleaving der Bits
**Spec:** `A.5.1.2, Bezug auf A.5.2.2 und A.5.2.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die übertragenen Bits müssen codierte und interleaved Datenbits sein, die ein Wort bilden.

### REQ-WAVEFORM-005 — Phasenkontinuität der Tonübergänge
**Spec:** `A.5.1.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Übergänge zwischen Tönen müssen phasenkontinuierlich sein und an den Maxima oder Minima der Welle (slope zero) erfolgen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WAVEFORM-004 — Codierung und Interleaving der Bits (`A.5.1.2, Bezug auf A.5.2.2 und A.5.2.3`)
- [ ] ⬜ **`AC-WAVEFORM-004-1`** — Die Bits müssen codiert und interleaved sein gemäß den in A.5.2.2 und A.5.2.3 definierten Verfahren (siehe OPEN-01, OPEN-02).
- [ ] ⬜ **`AC-WAVEFORM-004-2`** — Jede Gruppe von Bits muss einem vollständigen ALE-Wort entsprechen.

### REQ-WAVEFORM-005 — Phasenkontinuität der Tonübergänge (`A.5.1.2`)
- [x] ✅ **`AC-WAVEFORM-005-1`** `verified` — Tonübergänge müssen phasenkontinuierlich sein
- [ ] 🔍 **`AC-WAVEFORM-005-2`** `validate` — Tonübergänge müssen an einem Punkt mit null Ableitung (Maxima oder Minima) erfolgen

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

32-Bit NCO, Init 0x40000000 (pi/2). Phasenkontinuität durch geteilten Akkumulator.

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
