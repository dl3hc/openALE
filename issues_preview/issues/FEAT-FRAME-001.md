# FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FRAME`

## 📁 Module
- `src/ale_state_machine.cpp`
- `include/ale_state_machine.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding
- 🔍 `FEAT-FEC-003` — Interleaving / Deinterleaving

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FRAME-001 — Frame-Grundstruktur und Wortbasis
**Spec:** `A.5.2.5` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Alle ALE-Übertragungen basieren auf den Tönen, dem Timing sowie den Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3. Jede Aussendung ist als „Frame" aufgebaut, der aus fortlaufenden redundanten Wörtern in gültigen Sequenzen besteht. Ein Frame setzt sich aus genau drei grundlegenden Abschnitten zusammen: Calling Cycle, Message und Conclusion. Die zulässigen Formate und Abschnittsgrenzen sind in Figure A-14, Table A-VII und A.5.5 festgelegt.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FRAME-001 — Frame-Grundstruktur und Wortbasis (`A.5.2.5`)
- [ ] ⬜ **`AC-FRAME-001-1`** — Jede ALE-Aussendung verwendet ausschließlich Töne, Timing sowie Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3.
- [ ] ⬜ **`AC-FRAME-001-2`** — Jeder Frame besteht ausschließlich aus fortlaufenden redundanten Wörtern in gültigen Sequenzen.
- [ ] ⬜ **`AC-FRAME-001-3`** — Jeder Frame enthält die drei Abschnitte Calling Cycle, Message und Conclusion in genau dieser Reihenfolge (wobei Message optional ist).
- [ ] ⬜ **`AC-FRAME-001-4`** — Die Konstruktion eines Frames entspricht den Vorgaben aus Figure A-14, Table A-VII und den Format-Beschreibungen aus A.5.5.

## 🧪 Tests
- `tests/ale_modem_integration_test.cpp`

## 💡 Implementierungshinweise

Frame = Tcc + [Message] + Conclusion. Tc = wpa*Trw, Tlc = 2*Tc, Tsc = C*2*Trw.

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
