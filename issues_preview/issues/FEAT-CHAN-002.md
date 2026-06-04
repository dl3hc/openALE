# FEAT-CHAN-002 — LQA CMD Reporting

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `CHAN`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- ⬜ `FEAT-CHAN-001` — LQA-Messung (BER, SINAD, MP)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-CHAN-016 — CMD LQA Word ist verpflichtende Funktion
**Spec:** `A.5.4.2 / Absatz 1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Funktion zum Austausch aktueller LQA-Informationen unter ALE-Stationen (CMD LQA) ist verpflichtend.

### REQ-CHAN-017 — CMD LQA Word enthält BER, SINAD und MP
**Spec:** `A.5.4.2 / Absatz 2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das CMD LQA-Wort enthält drei Arten von Analyseinformationen (BER, SINAD und MP), die separat von der ALE-Analyse-Fähigkeit erzeugt werden. Wenn das Control-Bit KA1 auf "1" gesetzt ist, muss die empfangende Station mit einem LQA-Report im Handshake antworten. Wenn KA1 auf "0" gesetzt ist, ist kein Report erforderlich.

### REQ-CHAN-018 — BER-Feld im LQA CMD enthält 5 Bits
**Spec:** `A.5.4.2.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Messung und Meldung von BER ist verpflichtend. Das BER-Feld im LQA CMD enthält fünf Bits. Tabelle A-XIII ist für die zugeordneten Werte heranzuziehen.

### REQ-CHAN-019 — SINAD-Feld im LQA CMD: 5 Bits, 0–30 dB
**Spec:** `A.5.4.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> SINAD wird im CMD LQA Word als 5 Bits dargestellt. Der Messbereich ist 0 bis 30 dB in 1-dB-Schritten. 00000 entspricht 0 dB oder weniger, 11111 bedeutet keine Messung verfügbar.

### REQ-CHAN-020 — MP-Feld im LQA CMD: 3 Bits, 0–6 ms
**Spec:** `A.5.4.2.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Wenn implementiert, werden MP-Messungen im CMD LQA Word in 3 Bits dargestellt. Der gemessene Wert in Millisekunden ist auf die nächste ganze Zahl gerundet zu melden, außer Werte größer als 6 ms, die als 6 zu melden sind. Wenn MP nicht gemessen wird, ist der gemeldete MP-Wert 7.

### REQ-CHAN-021 — Local Noise Report CMD (optional)
**Spec:** `A.5.4.4 / Absatz 1, 2` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD`

> Der Local Noise Report CMD bietet eine Broadcast-Alternative zum Sounding, die es empfangenden Stationen erlaubt, die bilaterale Link-Qualität für den das Report tragenden Kanal grob vorherzusagen. Der CMD meldet die mittlere und maximale Rauschleistung, die auf dem Kanal in den vergangenen 60 Minuten gemessen wurde.

### REQ-CHAN-022 — Local Noise Report: Einheiten und Codierung
**Spec:** `A.5.4.4 / Absatz 2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Einheiten für Max- und Mean-Felder sind dB relativ zu 0,1 µV / 3 kHz Rauschen. Bei einem gemessenen Rauschwert von 0 dB oder weniger ist eine 0 zu senden. Für Messwerte von 0 dB bis +126 dB ist das Verhältnis in dB auf eine ganze Zahl gerundet zu senden. Für Rauschverhältnisse größer als +126 dB ist 126 zu senden. Der Code 127 (alle 1en) ist zu senden, wenn kein Report für ein Feld verfügbar ist.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-CHAN-016 — CMD LQA Word ist verpflichtende Funktion (`A.5.4.2 / Absatz 1`)
- [ ] ⬜ **`AC-CHAN-016-1`** — Alle ALE-Stationen unterstützen die CMD LQA-Funktion.

### REQ-CHAN-017 — CMD LQA Word enthält BER, SINAD und MP (`A.5.4.2 / Absatz 2`)
- [ ] ⬜ **`AC-CHAN-017-1`** — CMD LQA-Wort trägt BER-, SINAD- und MP-Informationen.
- [ ] ⬜ **`AC-CHAN-017-2`** — Bei KA1="1" antwortet die empfangende Station mit einem LQA-Report.
- [ ] ⬜ **`AC-CHAN-017-3`** — Bei KA1="0" ist kein LQA-Report erforderlich.

### REQ-CHAN-018 — BER-Feld im LQA CMD enthält 5 Bits (`A.5.4.2.1`)
- [ ] ⬜ **`AC-CHAN-018-1`** — BER-Messung und -Meldung ist verpflichtend.
- [ ] ⬜ **`AC-CHAN-018-2`** — Das BER-Feld besteht aus 5 Bits.
- [ ] ⬜ **`AC-CHAN-018-3`** — Die BER-Werte entsprechen Tabelle A-XIII.

### REQ-CHAN-019 — SINAD-Feld im LQA CMD: 5 Bits, 0–30 dB (`A.5.4.2.2`)
- [ ] ⬜ **`AC-CHAN-019-1`** — SINAD wird als 5-Bit-Feld dargestellt.
- [ ] ⬜ **`AC-CHAN-019-2`** — Der Bereich erstreckt sich von 0 bis 30 dB in 1-dB-Schritten.
- [ ] ⬜ **`AC-CHAN-019-3`** — Der Code 00000 repräsentiert 0 dB oder weniger.
- [ ] ⬜ **`AC-CHAN-019-4`** — Der Code 11111 signalisiert "keine Messung verfügbar."

### REQ-CHAN-020 — MP-Feld im LQA CMD: 3 Bits, 0–6 ms (`A.5.4.2.3`)
- [ ] ⬜ **`AC-CHAN-020-1`** — MP wird in 3 Bits dargestellt (wenn implementiert).
- [ ] ⬜ **`AC-CHAN-020-2`** — Der Wert in ms ist auf die nächste ganze Zahl gerundet.
- [ ] ⬜ **`AC-CHAN-020-3`** — Werte größer als 6 ms werden als 6 gemeldet.
- [ ] ⬜ **`AC-CHAN-020-4`** — Bei fehlender MP-Messung wird 7 gemeldet.

### REQ-CHAN-021 — Local Noise Report CMD (optional) (`A.5.4.4 / Absatz 1, 2`)
- [ ] ⬜ **`AC-CHAN-021-1`** — Eine Implementierung kann den Local Noise Report CMD unterstützen oder unterlassen.
- [ ] ⬜ **`AC-CHAN-021-2`** — Der Report enthält Mittel- und Maximalrauschleistung der vergangenen 60 Minuten.

### REQ-CHAN-022 — Local Noise Report: Einheiten und Codierung (`A.5.4.4 / Absatz 2`)
- [ ] ⬜ **`AC-CHAN-022-1`** — Einheiten sind dB relativ zu 0,1 µV / 3 kHz.
- [ ] ⬜ **`AC-CHAN-022-2`** — Wert ≤ 0 dB wird als 0 gesendet.
- [ ] ⬜ **`AC-CHAN-022-3`** — Wert 0–126 dB wird als gerundete ganze Zahl gesendet.
- [ ] ⬜ **`AC-CHAN-022-4`** — Wert > 126 dB wird als 126 gesendet.
- [ ] ⬜ **`AC-CHAN-022-5`** — Code 127 signalisiert "kein Report verfügbar."

## 🧪 Tests
- `tests/test_lqa_database.cpp`

## 💡 Implementierungshinweise

CMD-Wort: BER 5-Bit, SINAD 5-Bit (0-30dB), MP 3-Bit (0-6ms), KA1-Polling-Flag. Verpflichtendes Feature.

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
