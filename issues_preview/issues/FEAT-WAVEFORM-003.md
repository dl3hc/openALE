# FEAT-WAVEFORM-003 — Timing-Konstanten & Wortgrenzen

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WAVEFORM`

## 📁 Module
- `include/ale_types.h`
- `include/ale_state_machine.h`

## 🔗 Depends on
- ✅ `FEAT-WAVEFORM-001` — Tone-Symbol-Mapping & Frequenztabelle

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WAVEFORM-006 — Tonrate und Symbolperiode
**Spec:** `A.5.1.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Töne müssen mit einer Rate von 125 Tönen (Symbolen) pro Sekunde übertragen werden, mit einer resultierenden Periode von 8 ms pro Ton.

### REQ-WAVEFORM-007 — Übertragene Bitrate
**Spec:** `A.5.1.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die übertragene Bitrate muss 375 Bits pro Sekunde betragen.

### REQ-WAVEFORM-008 — Wortgrenzen und redundante Wörter
**Spec:** `A.5.1.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Übergänge zwischen benachbarten redundanten (verdreifachten) übertragenen Wörtern müssen mit den Übergängen zwischen Tönen übereinstimmen, was zu einer integralen 49 Symbolen (Tönen) per redundantem (verdreifachtem) Wort führt.

### REQ-WAVEFORM-009 — Einzelwortperiode (Tw)
**Spec:** `A.5.1.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Einzelwortperiode (Tw) muss 130,66... ms (oder 16,33... Symbole) betragen.

### REQ-WAVEFORM-010 — Verdreifachte-Wort-Periode (3×Tw)
**Spec:** `A.5.1.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die verdreifachte-Wort-Periode Trw (3×Tw) für das grundlegende redundante Format muss 392 ms betragen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WAVEFORM-006 — Tonrate und Symbolperiode (`A.5.1.3`)
- [ ] ⬜ **`AC-WAVEFORM-006-1`** — Die Tonrate muss exakt 125 Symbole/Sekunde sein
- [ ] ⬜ **`AC-WAVEFORM-006-2`** — Die Periode pro Ton muss exakt 8 ms sein

### REQ-WAVEFORM-007 — Übertragene Bitrate (`A.5.1.3`)
- [ ] ⬜ **`AC-WAVEFORM-007-1`** — Die übertragene Bitrate muss exakt 375 b/s sein

### REQ-WAVEFORM-008 — Wortgrenzen und redundante Wörter (`A.5.1.3`)
- [ ] ⬜ **`AC-WAVEFORM-008-1`** — Wortübergänge müssen mit Tonübergängen synchronisiert sein
- [ ] ⬜ **`AC-WAVEFORM-008-2`** — Es müssen exakt 49 Symbole pro redundantem Wort sein

### REQ-WAVEFORM-009 — Einzelwortperiode (Tw) (`A.5.1.3`)
- [ ] ⬜ **`AC-WAVEFORM-009-1`** — Tw muss 130,66... ms betragen
- [ ] ⬜ **`AC-WAVEFORM-009-2`** — Tw muss 16,33... Symbolen entsprechen

### REQ-WAVEFORM-010 — Verdreifachte-Wort-Periode (3×Tw) (`A.5.1.3`)
- [ ] ⬜ **`AC-WAVEFORM-010-1`** — 3×Tw muss exakt 392 ms betragen

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

WORD_DURATION_MS=392, SAMPLES_PER_SYMBOL=64, SYMBOLS_PER_WORD=49 als Konstanten.

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
