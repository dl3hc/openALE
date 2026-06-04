# FEAT-SYNC-002 — Empfangsseitige Wortsynchronisation

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `SYNC`

## 📁 Module
- `src/ale2gmodem.cpp`

## 🔗 Depends on
- 🔍 `FEAT-SYNC-001` — Trw-Grid (Sendeseitiger Wortphasen-Anker)
- ⬜ `FEAT-FEC-005` — Unanimous-Votes-Erfassung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SYNC-005 — Empfangsdemodulator: Signalakquisition, Tracking und Demodulation
**Spec:** `A.5.2.6.2 / Figure A-11` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Empfangsdemodulator akzeptiert Basisband-Audio vom Empfänger, akquiriert, verfolgt und demoduliert ALE-Signale und stellt die zurückgewonnenen digitalen Daten den Decodern bereit. Im Datenblocknachrichten-Modus (DBM-Modus) muss der Empfangsdemodulator zusätzlich in der Lage sein, einzelne Datenbits für das tiefe Deinterleaving und Dekodieren zu lesen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SYNC-005 — Empfangsdemodulator: Signalakquisition, Tracking und Demodulation (`A.5.2.6.2 / Figure A-11`)
- [ ] ⬜ **`AC-SYNC-005-1`** — Der Empfangsdemodulator akquiriert ALE-Signale aus dem Basisband-Audio.
- [ ] ⬜ **`AC-SYNC-005-2`** — Der Empfangsdemodulator verfolgt (trackt) ALE-Signale und demoduliert sie.
- [ ] ⬜ **`AC-SYNC-005-3`** — Der Empfangsdemodulator stellt die zurückgewonnenen digitalen Daten den Decodern bereit.
- [ ] ⬜ **`AC-SYNC-005-4`** — Im DBM-Modus ist der Empfangsdemodulator in der Lage, einzelne Datenbits für das tiefe Deinterleaving und Dekodieren zu lesen.

## 🧪 Tests
- `tests/test_sync.cpp`

## 💡 Implementierungshinweise

Sliding-Window oder Energy-Detector. Hängt von FEAT-LINK-001 ab. Wortphasengrenzen aus Energieübergängen.

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
