# FEAT-FEC-001 — Golay (24,12) Encoder

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FEC`

## 📁 Module
- `extern/PC-ALE/src/fec/golay.cpp`
- `extern/PC-ALE/include/ale/golay.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FEC-004 — Kombinierte Fehlerbehandlungs-Funktionen
**Spec:** `A.5.2.2.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Funktionen Forward Error Correction, Interleaving und Redundanz sind im Sendencoder und Empfangsdecoder durchzuführen.

### REQ-FEC-005 — FEC-Code-Typ
**Spec:** `A.5.2.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das extended Golay (24, 12, 3) Forward Error Correction Code ist vorgeschrieben.

### REQ-FEC-006 — FEC-Generator-Polynom
**Spec:** `A.5.2.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Generator-Polynom für den FEC-Code generator ist festgelegt wie im Standard definiert.

### REQ-FEC-007 — Generator-Matrix-Struktur
**Spec:** `A.5.2.2.2, Figure A-6` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das System muss den Extended Golay (24,12)-Code mit der vom Standard vorgeschriebenen systematischen Codewortstruktur implementieren.

### REQ-FEC-009 — Kodier-Formel
**Spec:** `A.5.2.2.2.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das System muss jedes 12-Bit-Datenwort gemäß dem vorgeschriebenen Golay-Kodierverfahren in ein 24-Bit-Codewort überführen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FEC-004 — Kombinierte Fehlerbehandlungs-Funktionen (`A.5.2.2.1`)
- [ ] ⬜ **`AC-FEC-004-1`** — Der Sendencoder muss FEC, Interleaving und Redundanz unterstützen
- [ ] ⬜ **`AC-FEC-004-2`** — Der Empfangsdecoder muss FEC, Deinterleaving und Redundanzverarbeitung unterstützen

### REQ-FEC-005 — FEC-Code-Typ (`A.5.2.2.2`)
- [ ] ⬜ **`AC-FEC-005-1`** — Der verwendete FEC-Code muss extended Golay (24, 12, 3) sein
- [ ] ⬜ **`AC-FEC-005-2`** — Jeder codierte Block muss 24 Bits umfassen, wovon 12 Datenbits und 12 Paritybits bestehen
- [ ] ⬜ **`AC-FEC-005-3`** — Der minimale Hamming-Abstand des Codes muss 3 betragen

### REQ-FEC-006 — FEC-Generator-Polynom (`A.5.2.2.2`)
- [ ] ⬜ **`AC-FEC-006-1`** — Das Generator-Polynom muss dem im Standard definierten Wert entsprechen

### REQ-FEC-007 — Generator-Matrix-Struktur (`A.5.2.2.2, Figure A-6`)
- [ ] ⬜ **`AC-FEC-007-1`** — Die Codewortstruktur muss der im Standard definierten systematischen Form des Golay (24,12)-Codes entsprechen.

### REQ-FEC-009 — Kodier-Formel (`A.5.2.2.2.1`)
- [ ] ⬜ **`AC-FEC-009-1`** — Jedes 12-Bit-Datenwort muss in ein 24-Bit-Golay-Codewort kodiert werden.
- [ ] ⬜ **`AC-FEC-009-2`** — Das Codewort muss dem vom Standard definierten Kodierverfahren entsprechen.

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

Systematischer Extended Golay (24,12,3). Polynom 0xAE3. Syndromtabelle 4096 Einträge.

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
