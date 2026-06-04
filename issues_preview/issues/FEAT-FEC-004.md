# FEAT-FEC-004 — 3x Redundanz mit Majority-Vote (RX)

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `FEC`

## 📁 Module
- `src/ale_fec_codec.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FEC-003` — Interleaving / Deinterleaving

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-FEC-014 — Redundanz-Übertragung
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Jedes 49-Bit-Wort muss redundant (dreifach) übertragen werden, um die Auswirkungen von Fading, Interferenz und Rauschen zu reduzieren.

### REQ-FEC-015 — Scanning-Call Redundanz-Dauer
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein individuelles oder Netz-Routing-Wort (TO...), das zum Rufen einer scannenden Station (oder eines Netzes) verwendet wird, muss so lange im Scanning-Call redundant gesendet werden, wie es im Scan erforderlich ist, um den Empfang sicherzustellen.

### REQ-FEC-016 — Group-Call Redundanz-Sequenz
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Bei einem Gruppenruf (unter Verwendung von THRU und REP alternierend) müssen das erste individuelle Routing-Wort (THRU) und alle nachfolgenden individuellen Routing-Wörter (REP, THRU, ...) dreimal unmittelbar nacheinander gesendet werden. Diese Triple-Wörter für individuelle Stationen müssen in Gruppen-Sequenz rotiert werden.

### REQ-FEC-017 — 2/3 Mehrheits-Entscheidung
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Empfänger muss bei Bit-Zeitintervallen das aktuelle Bit und den vergangenen Bit-Stream untersuchen und eine 2/3-Mehrheitsentscheidung auf Bit-Basis über einen Zeitraum von drei Wörtern durchführen.

### REQ-FEC-018 — Majority-Wort-Weitergabe
**Spec:** `A.5.2.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die resultant 48 (ignoriert das 49te Bit) neuesten Majority-Bits müssen das neueste Majority-Wort bilden und an den Deinterleaver und FEC-Decoder übergeben werden.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-FEC-014 — Redundanz-Übertragung (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-014-1`** — Jedes Wort muss exakt dreimal übertragen werden
- [ ] ⬜ **`AC-FEC-014-2`** — Die redundante Übertragung muss dazu dienen, Fading, Interferenz und Rauschen zu reduzieren

### REQ-FEC-015 — Scanning-Call Redundanz-Dauer (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-015-1`** — Ein Scanning-Call-Wort muss solange wiederholt werden, bis der Empfang sichergestellt ist
- [ ] ⬜ **`AC-FEC-015-2`** — Die Wiederholungsdauer muss dem im Scanning-Call definierten Zeitrahmen entsprechen

### REQ-FEC-016 — Group-Call Redundanz-Sequenz (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-016-1`** — Das erste Routing-Wort im Group-Call muss THRU sein und dreimal wiederholt werden
- [ ] ⬜ **`AC-FEC-016-2`** — Alle nachfolgenden Routing-Wörter müssen REP und THRU alternierend und jeweils dreimal wiederholt werden
- [ ] ⬜ **`AC-FEC-016-3`** — Die Triple-Wörter müssen in Gruppen-Sequenz rotiert werden

### REQ-FEC-017 — 2/3 Mehrheits-Entscheidung (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-017-1`** — Eine 2/3-Mehrheitsentscheidung muss auf Bit-Basis über drei Wörter durchgeführt werden
- [ ] ⬜ **`AC-FEC-017-2`** — Die Entscheidung muss sowohl das aktuelle Bit als auch die vergangenen Bits berücksichtigen

### REQ-FEC-018 — Majority-Wort-Weitergabe (`A.5.2.2.4`)
- [ ] ⬜ **`AC-FEC-018-1`** — Das Majority-Wort muss exakt 48 Bits umfassen
- [ ] ⬜ **`AC-FEC-018-2`** — Das Majority-Wort muss an den Deinterleaver übergeben werden
- [ ] ⬜ **`AC-FEC-018-3`** — Das Majority-Wort muss an den FEC-Decoder übergeben werden

## 🧪 Tests
- `tests/test_fec_majority_vote.cpp`

## 💡 Implementierungshinweise

remove_redundancy_3x() aktuell Placeholder (nimmt erste Kopie). Muss 2/3-Voting implementieren. Bit 49 ignorieren.

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
