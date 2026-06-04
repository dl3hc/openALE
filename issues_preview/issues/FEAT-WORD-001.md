# FEAT-WORD-001 — word24 Bit-Layout Encoding/Decoding

**Status:** ✅ `done` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WORD`

## 📁 Module
- `src/ale_word.cpp`
- `include/ale_word.h`

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WORD-001 — Grundstruktur des ALE-Worts
**Spec:** `A.5.2.3.1 / Figure A-12` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein grundlegendes ALE-Wort muss 24 Bit Informationsinhalt umfassen und aus W1 als höchstwertigem Bit bis W24 als niederwertigem Bit bestehen. Das Wort muss in einen 3-Bit-Präambelteil und einen 21-Bit-Datenfeldteil unterteilt sein. Das höchstwertige Bit muss zuerst übertragen werden. Vor der Übertragung muss das Wort in zwei 12-Bit-Hälften für die FEC-Kodierung aufgeteilt werden. Das Datenfeld kann drei 7-Bit-ASCII-Zeichen pro Wort enthalten.

### REQ-WORD-002 — Präambelbits und Worttypen
**Spec:** `A.5.2.3.1.2 / A.5.2.3.1.3 / Table A-VIII` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die führenden drei Bits eines ALE-Worts müssen die Präambelbits P3 bis P1 bilden. Diese Präambelbits müssen einen von acht möglichen Worttypen identifizieren. Die Worttypen und ihre Bedeutung müssen den im Standard definierten Codes und Funktionen entsprechen. Optional definierte AQC-ALE-Preambles sind in A.5.8.1.2 festgelegt.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WORD-001 — Grundstruktur des ALE-Worts (`A.5.2.3.1 / Figure A-12`)
- [x] ✅ **`AC-WORD-001-1`** — Ein grundlegendes ALE-Wort enthält genau 24 Bit.
- [x] ✅ **`AC-WORD-001-2`** — Das Wort ist in genau einen 3-Bit-Präambelteil und einen 21-Bit-Datenfeldteil unterteilt.
- [x] ✅ **`AC-WORD-001-3`** — Das höchstwertige Bit wird zuerst übertragen.
- [x] ✅ **`AC-WORD-001-4`** — Vor der Übertragung wird das Wort in zwei 12-Bit-Hälften für die FEC-Kodierung aufgeteilt.
- [x] ✅ **`AC-WORD-001-5`** — Das Datenfeld kann drei 7-Bit-ASCII-Zeichen pro Wort enthalten.

### REQ-WORD-002 — Präambelbits und Worttypen (`A.5.2.3.1.2 / A.5.2.3.1.3 / Table A-VIII`)
- [x] ✅ **`AC-WORD-002-1`** — Die führenden drei Bits eines ALE-Worts identifizieren genau einen von acht Worttypen.
- [x] ✅ **`AC-WORD-002-2`** — Die Präambelbits werden als P3, P2 und P1 geführt.
- [x] ✅ **`AC-WORD-002-3`** — Die Worttypen THRU, TO, CMD, FROM, TIS, TWAS, DATA und REP werden gemäß Standard unterstützt.
- [x] ✅ **`AC-WORD-002-4`** — Optional definierte AQC-ALE-Preambles werden nur gemäß der dafür vorgesehenen Spezifikation verwendet.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

W1=bit23 (MSB). Preamble [23:21], Char1 [20:14], Char2 [13:7], Char3 [6:0]. Raw 7-bit ASCII.

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
