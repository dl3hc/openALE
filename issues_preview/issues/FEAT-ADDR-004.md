# FEAT-ADDR-004 — Wildcard-Matching

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `ADDR`

## 📁 Module
- `src/ale_word.cpp`

## 🔗 Depends on
- 🔍 `FEAT-ADDR-001` — Basic-38-Zeichensatz & Adressvalidierung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-ADDR-012 — Wildcard-Zeichen und gleichlange Adresslängen
**Spec:** `A.5.2.4.9 / Table A-XI` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein Wildcard-Zeichen ist ein Sonderzeichen, das der Anrufer verwendet, um Mehrstationsadressen mit einer einzigen Rufadresse anzusprechen. Empfänger müssen das Wildcard-Zeichen als Ersatz für jedes alphanumerische Zeichen in ihren eigenen Adressen an derselben Position oder an denselben Positionen akzeptieren. Jedes Wildcard-Zeichen muss für eines von 36 Zeichen des Basic-38-Satzes stehen. Die Länge der rufenden Wildcard-Adresse und der gerufenen Adresse muss gleich sein. Das spezielle Wildcard-Zeichen muss „?“ sein.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-ADDR-012 — Wildcard-Zeichen und gleichlange Adresslängen (`A.5.2.4.9 / Table A-XI`)
- [ ] ⬜ **`AC-ADDR-012-1`** — Ein Wildcard-Zeichen kann Mehrstationsadressen mit einer einzigen Rufadresse ansprechen.
- [ ] ⬜ **`AC-ADDR-012-2`** — Empfänger akzeptieren das Wildcard-Zeichen als Ersatz für jedes alphanumerische Zeichen an derselben Position.
- [ ] ⬜ **`AC-ADDR-012-3`** — Jedes Wildcard-Zeichen steht für eines von 36 Zeichen des Basic-38-Satzes.
- [ ] ⬜ **`AC-ADDR-012-4`** — Die Länge der rufenden und der gerufenen Adresse ist gleich.
- [ ] ⬜ **`AC-ADDR-012-5`** — Das spezielle Wildcard-Zeichen ist „?“.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

match_wildcard(): '?' = ein beliebiges alphanumerisches Zeichen. Muster- und Adresslänge müssen gleich sein.

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
