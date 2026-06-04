# FEAT-FEC-003 — Interleaving / Deinterleaving

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FEC`

## 📁 Module
- `src/ale_fec_codec.cpp`
- `include/ale_fec_codec.h`

## 🔗 Depends on
- 🔍 `FEAT-FEC-001` — Golay (24,12) Encoder
- 🔍 `FEAT-FEC-002` — Golay (24,12) Decoder mit Fehlerkorrektur

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FEC-012 — Interleaving-Vorgabe
**Spec:** `A.5.2.2.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Datenbits des Worts und die Golay-FEC-Bits müssen vor der Übertragung gemäß dem im Standard definierten Muster interleaved werden. Die obere Hälfte der FEC-Bits muss dabei invertiert werden.

### REQ-FEC-013 — Transmitted-Word-Struktur
**Spec:** `A.5.2.2.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein übertragenes Wort besteht aus 48 interleaved Bits plus einem 49ten Stuff-Bit S₄ (Wert = 0). Die Übertragung erfolgt als A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ mit 16⅓ Symbolen pro Wort. Das 49te Stuff-Bit wird vom Empfänger ignoriert.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FEC-012 — Interleaving-Vorgabe (`A.5.2.2.3`)
- [ ] ⬜ **`AC-FEC-012-1`** — Die Bits W1 bis W24 müssen gemäß dem Standard-Interleaving-Muster umgeordnet werden
- [ ] ⬜ **`AC-FEC-012-2`** — Die Golay FEC-Bits G13 bis G24 müssen vor dem Interleaving invertiert werden
- [ ] ⬜ **`AC-FEC-012-3`** — Das Deinterleaving am Empfänger muss umkehrbar und verlustfrei sein

### REQ-FEC-013 — Transmitted-Word-Struktur (`A.5.2.2.3`)
- [ ] ⬜ **`AC-FEC-013-1`** — Ein übertragenes Wort muss aus 48 Nutzbits und einem Stuff-Bit bestehen
- [ ] ⬜ **`AC-FEC-013-2`** — Das Stuff-Bit S₄₉ muss den Wert 0 haben
- [ ] ⬜ **`AC-FEC-013-3`** — Die Übertragungssequenz muss dem Muster A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ entsprechen
- [ ] ⬜ **`AC-FEC-013-4`** — Das empfangene 49te Stuff-Bit muss ignoriert werden

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

Muster A1B1A2B2...A24B24+S49. G13..G24 invertiert. Coder A=W1..W12, B=W13..W24.

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
