# FEAT-FEC-002 — Golay (24,12) Decoder mit Fehlerkorrektur

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FEC`

## 📁 Module
- `extern/PC-ALE/src/fec/golay.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FEC-001` — Golay (24,12) Encoder

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FEC-010 — Dekodier-Formel und Syndrom-Bildung
**Spec:** `A.5.2.2.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das System muss empfangene 24-Bit-Codewörter dekodieren und dabei Übertragungsfehler bis zur vorgeschriebenen Korrekturkapazität des Golay (24,12)-Codes erkennen und korrigieren.

### REQ-FEC-011 — Fehler-Korrektur-Flag-Logik
**Spec:** `A.5.2.2.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Flags müssen entsprechend der Anzahl der korrigierten Fehler gesetzt werden. Wenn s ungleich 0 ist und e mehr Fehler enthält als vom Dekodiermodus korrigierbar, muss ein erkannter Fehler angezeigt und das entsprechende Flag gesetzt werden.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FEC-010 — Dekodier-Formel und Syndrom-Bildung (`A.5.2.2.2.2`)
- [ ] ⬜ **`AC-FEC-010-1`** — Das Dekodierverfahren muss dem vom Standard vorgeschriebenen Syndrom-basierten Golay-Dekodierverfahren entsprechen.
- [ ] ⬜ **`AC-FEC-010-2`** — Jeder korrigierbare Fehlervektor muss eindeutig einem Syndromwert zugeordnet sein.
- [ ] ⬜ **`AC-FEC-010-3`** — Das dekodierte 12-Bit-Datenwort muss dem gesendeten Original entsprechen, sofern die Fehleranzahl die Korrekturkapazität nicht überschreitet.

### REQ-FEC-011 — Fehler-Korrektur-Flag-Logik (`A.5.2.2.2.2`)
- [ ] ⬜ **`AC-FEC-011-1`** — Ein Flag muss bei erfolgreicher Korrektur gesetzt werden
- [ ] ⬜ **`AC-FEC-011-2`** — Ein Flag muss bei einem nicht korrigierbaren, aber erkennbaren Fehler gesetzt werden

## 🧪 Tests
- `tests/test_fsk_core.cpp`

## 💡 Implementierungshinweise

Syndrom-basiert. Korrigiert <=3 Bit. Gibt 0xFF zurück wenn unkorrektierbar.

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
