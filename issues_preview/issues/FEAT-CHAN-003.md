# FEAT-CHAN-003 — Kanalauswahl-Algorithmen

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `CHAN`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- ⬜ `FEAT-CHAN-001` — LQA-Messung (BER, SINAD, MP)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-CHAN-023 — Single-Station Channel Selection für eine Station
**Spec:** `A.5.4.5` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Alle Stationen müssen in der Lage sein, den (recent) besten Kanal für Calling oder Listening für eine einzelne Station basierend auf den Werten im LQA-Speicher auszuwählen.

### REQ-CHAN-024 — Kanalselektion für Zwei-Wege-Link: beide Richtungen berücksichtigen
**Spec:** `A.5.4.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Bei der Auswahl eines Kanals für einen Zwei-Wege-Link sind die Link-Quality-Messungen für beide Richtungen auf jedem Frequenz zu berücksichtigen. Bilaterale (Handshake-)Scores sind die Summe der beiden LQA-Werte.

### REQ-CHAN-025 — LQA-Score-Semantik: kleinere Werte = besser
**Spec:** `A.5.4.5.1 / Figure A-27 Notes` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> LQA = "0" ist ausgezeichnet, reichend bis "30", was sehr schlecht ist. LQA = "x" bedeutet, nach einem Handshake-Versuch nicht verfügbar. LQA = "-" bedeutet, nicht verfügbar, aber Handshake nicht versucht.

### REQ-CHAN-026 — Kanalselektion für One-Way-Broadcast: TO-Scores gewichten
**Spec:** `A.5.4.5.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Wenn nur eine Ein-Wege-Übertragung an eine Station erforderlich ist, TO-Scores (von der Ziel-Station gemeldet) sind stärker zu gewichten als FROM-Scores (von der Ziel-Station gemessen).

### REQ-CHAN-027 — Kanalselektion für Listening: FROM-Scores gewichten
**Spec:** `A.5.4.5.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Bei der Auswahl eines Kanals zum Abhören einer anderen Station sind die auf Übertragungen von dieser Station gemessenen Scores (FROM) stärker zu gewichten als die von der Ziel-Station gemeldeten Scores.

### REQ-CHAN-028 — Multi-Station Channel Selection
**Spec:** `A.5.4.6` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Eine Station muss ebenfalls in der Lage sein, den (recent) besten Kanal zum Anrufen oder Abhören mehrerer Stationen basierend auf den Werten im LQA-Speicher auszuwählen.

### REQ-CHAN-029 — Broadcast zu mehreren Stationen: TO-Scores priorisieren
**Spec:** `A.5.4.6 / Absatz 4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Bei einem Broadcast an mehrere Stationen werden die TO-Scores priorisiert.

### REQ-CHAN-030 — Listening für mehrere Stationen: FROM-Scores priorisieren
**Spec:** `A.5.4.6 / Absatz 5` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Zur Auswahl von Kanälen zum Abhören mehrerer Stationen werden die FROM-Scores priorisiert.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-CHAN-023 — Single-Station Channel Selection für eine Station (`A.5.4.5`)
- [ ] ⬜ **`AC-CHAN-023-1`** — Alle Stationen unterstützen die Kanalselektion für eine einzelne Station basierend auf LQA.

### REQ-CHAN-024 — Kanalselektion für Zwei-Wege-Link: beide Richtungen berücksichtigen (`A.5.4.5.1`)
- [ ] ⬜ **`AC-CHAN-024-1`** — Für Zwei-Wege-Link-Auswahl werden Messungen in beide Richtungen einbezogen.
- [ ] ⬜ **`AC-CHAN-024-2`** — Der bilaterale Score ist die Summe der LQA-Werte beider Richtungen.

### REQ-CHAN-025 — LQA-Score-Semantik: kleinere Werte = besser (`A.5.4.5.1 / Figure A-27 Notes`)
- [ ] ⬜ **`AC-CHAN-025-1`** — Ein LQA-Wert von 0 repräsentiert ausgezeichnete Qualität.
- [ ] ⬜ **`AC-CHAN-025-2`** — Ein LQA-Wert von 30 repräsentiert sehr schlechte Qualität.
- [ ] ⬜ **`AC-CHAN-025-3`** — Der Wert "x" kennzeichnet einen gescheiterten Handshake-Versuch.
- [ ] ⬜ **`AC-CHAN-025-4`** — Der Wert "-" kennzeichnet fehlende Verfügbarkeit ohne erfolgten Handshake-Versuch.

### REQ-CHAN-026 — Kanalselektion für One-Way-Broadcast: TO-Scores gewichten (`A.5.4.5.2`)
- [ ] ⬜ **`AC-CHAN-026-1`** — Bei One-Way-Broadcast werden TO-Scores stärker gewichtet als FROM-Scores.

### REQ-CHAN-027 — Kanalselektion für Listening: FROM-Scores gewichten (`A.5.4.5.3`)
- [ ] ⬜ **`AC-CHAN-027-1`** — Beim Abhören werden FROM-Scores stärker gewichtet als TO-Scores.

### REQ-CHAN-028 — Multi-Station Channel Selection (`A.5.4.6`)
- [ ] ⬜ **`AC-CHAN-028-1`** — Die Station unterstützt die Kanalselektion für mehrere Stationen.
- [ ] ⬜ **`AC-CHAN-028-2`** — Die Auswahl basiert auf den LQA-Speicherwerten.

### REQ-CHAN-029 — Broadcast zu mehreren Stationen: TO-Scores priorisieren (`A.5.4.6 / Absatz 4`)
- [ ] ⬜ **`AC-CHAN-029-1`** — Für Multi-Station Broadcast werden TO-Scores priorisiert.

### REQ-CHAN-030 — Listening für mehrere Stationen: FROM-Scores priorisieren (`A.5.4.6 / Absatz 5`)
- [ ] ⬜ **`AC-CHAN-030-1`** — Für Multi-Station Listening werden FROM-Scores priorisiert.

## 🧪 Tests
- `tests/test_lqa_analyzer.cpp`

## 💡 Implementierungshinweise

Single+Multi-Station. TO-Scores für Link/Broadcast, FROM-Scores für Listening. Gewichtungsformel noch zu definieren.

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
