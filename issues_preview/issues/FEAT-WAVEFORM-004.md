# FEAT-WAVEFORM-004 — Genauigkeits-Verifikation (Frequenz, Amplitude, Timing)

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WAVEFORM`

## 📁 Module
- `tests/test_tone_accuracy.cpp`

## 🔗 Depends on
- ✅ `FEAT-WAVEFORM-002` — NCO-Tongenerator mit Phasenkontinuität
- 🔍 `FEAT-WAVEFORM-003` — Timing-Konstanten & Wortgrenzen

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WAVEFORM-011 — Tonfrequenzgenauigkeit am Baseband
**Spec:** `A.5.1.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Am Baseband-Audio müssen die erzeugten Töne innerhalb von ±1,0 Hz liegen.

### REQ-WAVEFORM-012 — Sendeleistung der Töne im HF-Bereich
**Spec:** `A.5.1.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Im HF-Bereich müssen alle gesendeten Töne innerhalb der Amplitude von 2,0 dB liegen.

### REQ-WAVEFORM-013 — Symbol-Timing-Genauigkeit
**Spec:** `A.5.1.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das gesendete Symbol-Timing, und daher die Bit- und Wortraten, müssen innerhalb von 10 Teilen pro Million liegen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WAVEFORM-011 — Tonfrequenzgenauigkeit am Baseband (`A.5.1.4`)
- [ ] ⬜ **`AC-WAVEFORM-011-1`** — Jeder erzeugte Ton muss innerhalb von ±1,0 Hz seines Sollwerts liegen

### REQ-WAVEFORM-012 — Sendeleistung der Töne im HF-Bereich (`A.5.1.4`)
- [ ] ⬜ **`AC-WAVEFORM-012-1`** — Alle übertragenen Töne müssen innerhalb von 2,0 dB Amplitude im HF-Bereich sein

### REQ-WAVEFORM-013 — Symbol-Timing-Genauigkeit (`A.5.1.4`)
- [ ] ⬜ **`AC-WAVEFORM-013-1`** — Symbol-Timing muss innerhalb von 10 ppm sein
- [ ] ⬜ **`AC-WAVEFORM-013-2`** — Bit-Rate muss innerhalb von 10 ppm sein
- [ ] ⬜ **`AC-WAVEFORM-013-3`** — Wort-Rate muss innerhalb von 10 ppm sein

## 🧪 Tests
- `tests/test_tone_accuracy.cpp`

## 💡 Implementierungshinweise

Goertzel-basierte Frequenzmessung +-1Hz; RMS-Vergleich aller 8 Symbole <=2dB; Timing 10ppm.

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
