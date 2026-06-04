# FEAT-WAVEFORM-001 — Tone-Symbol-Mapping & Frequenztabelle

**Status:** ✅ `done` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WAVEFORM`

## 📁 Module
- `include/ale_types.h`

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WAVEFORM-001 — Modulzweck des ALE-Waveform
**Spec:** `A.5.1.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die ALE-Waveform ist so ausgelegt, dass sie durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden kann. Sie stellt eine robuste, langsame digitale Modem-Kapazität für multiple Zwecke bereit, einschließlich selektives Rufen und Datenübertragung.

### REQ-WAVEFORM-002 — Modulationsart
**Spec:** `A.5.1.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Waveform muss eine 8-ary frequency shift-keying (FSK) Modulation mit acht orthogonalen Tönen verwenden, wobei jeweils ein Ton (Symbol) zu einer Zeit übertragen wird. Jeder Ton repräsentiert drei Bits Daten.

### REQ-WAVEFORM-003 — Tonfrequenzen und Bit-Zuordnung
**Spec:** `A.5.1.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die acht Tonfrequenzen und ihre Zuordnung zu 3-Bit-Werten sind festgelegt wie in der folgenden Tabelle.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WAVEFORM-001 — Modulzweck des ALE-Waveform (`A.5.1.1`)
- [x] ✅ **`AC-WAVEFORM-001-1`** `verified` — Die Waveform muss durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden können
- [x] ✅ **`AC-WAVEFORM-001-2`** `verified` — Die Waveform muss selektives Rufen ermöglichen
- [x] ✅ **`AC-WAVEFORM-001-3`** `verified` — Die Waveform muss Datenübertragung ermöglichen

### REQ-WAVEFORM-002 — Modulationsart (`A.5.1.2`)
- [x] ✅ **`AC-WAVEFORM-002-1`** `verified` — Die Modulation muss FSK mit exakt 8 orthogonalen Tönen sein
- [x] ✅ **`AC-WAVEFORM-002-2`** `verified` — Pro Ton müssen exakt 3 Bits repräsentiert werden
- [x] ✅ **`AC-WAVEFORM-002-3`** `verified` — Es muss immer nur ein Ton zu einer Zeit übertragen werden

### REQ-WAVEFORM-003 — Tonfrequenzen und Bit-Zuordnung (`A.5.1.2`)
- [x] ✅ **`AC-WAVEFORM-003-1`** `verified` — Jede Frequenz muss exakt das zugewiesene 3-Bit-Muster repräsentieren

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

FREQ_TO_SYMBOL[rank] und TONE_FREQS_HZ[rank] in ale_types.h. static_assert prüft Bijektivität.

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
