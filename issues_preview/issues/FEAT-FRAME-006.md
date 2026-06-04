# FEAT-FRAME-006 — Gültige Sequenzen & Frame-Limits

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-001` — Frame-Grundstruktur & Wortbasis

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-012 — Gültige Wortsequenzen im Frame
**Spec:** `A.5.2.5.4 / Figure A-18 / Figure A-19 / Figure A-20` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die acht ALE-Worttypen dürfen zur Konstruktion von Frames und Messages nur in den Sequenzen verwendet werden, die in Figure A-18, Figure A-19 und Figure A-20 erlaubt sind.

### REQ-FRAME-013 — Frame-Größen- und Zeitgrenzen (Table A-XII)
**Spec:** `A.5.2.5.4 / Table A-XII` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Größe und Dauer von ALE-Frames und ihren Abschnitten sind durch die in Table A-XII definierten Grenzwerte begrenzt. Diese Grenzen gelten vorbehaltlich etwaiger Erweiterungen durch AMD-Extension, DTM oder DBM.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-012 — Gültige Wortsequenzen im Frame (`A.5.2.5.4 / Figure A-18 / Figure A-19 / Figure A-20`)
- [ ] ⬜ **`AC-FRAME-012-1`** — Jede gesendete Wortsequenz in einem Frame oder einer Message entspricht den erlaubten Sequenzen aus Figure A-18, Figure A-19 und Figure A-20.
- [ ] ⬜ **`AC-FRAME-012-2`** — Jede empfangene Wortsequenz wird gegen die erlaubten Sequenzen validiert; unzulässige Sequenzen werden verworfen.

### REQ-FRAME-013 — Frame-Größen- und Zeitgrenzen (Table A-XII) (`A.5.2.5.4 / Table A-XII`)
- [ ] ⬜ **`AC-FRAME-013-1`** — Die Adressgröße überschreitet nicht 5 Wörter (Ta max = 1960 ms).
- [ ] ⬜ **`AC-FRAME-013-2`** — Die Call-Zeit (Tc, als halbe Tlc) überschreitet nicht 12 Wörter (4704 ms).
- [ ] ⬜ **`AC-FRAME-013-3`** — Die maximale Suchlaufperiode (Ts max) überschreitet nicht 50 s.
- [ ] ⬜ **`AC-FRAME-013-4`** — Die Basiszeit des Message-Abschnitts (Tm max basic) überschreitet nicht 11,76 s, sofern keine AMD-Extension, DTM oder DBM angewendet wird.
- [ ] ⬜ **`AC-FRAME-013-5`** — Bei Verwendung von AMD (90 Zeichen) überschreitet die Message-Zeit nicht 11,76 s.
- [ ] ⬜ **`AC-FRAME-013-6`** — Bei Verwendung von DTM (1053 Zeichen) überschreitet die Message-Zeit nicht 2,29 min (gesamter Datenblock).
- [ ] ⬜ **`AC-FRAME-013-7`** — Bei Verwendung von DBM (37377 Zeichen) überschreitet die Message-Zeit nicht 23,26 min (gesamter tief-interleaved Block).

## 🧪 Tests
- `tests/test_frame_limits.cpp`

## 💡 Implementierungshinweise

Ta max=1960ms, Tc max=4704ms (12 Wörter), Ts max=50s. Guard: call_cycles_in_phase>12 → Error-Log.

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
