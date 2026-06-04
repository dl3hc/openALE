# FEAT-CHAN-004 — Listen Before Transmit

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `CHAN`

## 📁 Module
- `src/ale_state_machine.cpp`
- `src/ale2gmodem.cpp`

## 🔗 Depends on
- ⬜ `FEAT-CHAN-001` — LQA-Messung (BER, SINAD, MP)

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-CHAN-031 — Listen before Transmit: Verpflichtende Pause vor Call/Sound
**Spec:** `A.5.4.7` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Bevor ein Calling oder ein Sound auf einem Kanal eingeleitet wird, muss eine ALE-Station für eine programmierbare Zeit (Twt) auf andere Verkehrstätigkeit auf dem Kanal lauschen und darf auf diesem Kanal nicht senden, wenn Verkehr erkannt wird. Normalerweise ist ein aufgrund erkannten Verkehrs abgebrochener Sound wieder zu planen, während für einen Call ein anderer Kanal zu wählen ist.

### REQ-CHAN-032 — Listen-Before-Transmit-Dauer: programmierbar
**Spec:** `A.5.4.7.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Dauer der Listen-Before-Transmit-Pause ist vom Netzwerkmanager programmierbar. Wenn der ausgewählte Kanal nur für ALE-Übertragungen genutzt wird, muss die Pause nicht länger als 2 × Trw sein. Für andere Kanäle sind mindestens 2 Sekunden zu verwenden. Wenn die ALE-Station auf dem für die Übertragung ausgewählten Kanal bereits hörte, kann die auf dem Kanal verbrachte Hörzeit in die Listen-Before-Transmit-Zeit eingerechnet werden.

### REQ-CHAN-033 — Zu detektierende Modulationen bei Listen Before Transmit
**Spec:** `A.5.4.7.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Listen-Before-Transmit-Funktion muss Verkehr auf einem Kanal gemäß A.4.2.2 erkennen.

### REQ-CHAN-034 — Listen-Before-Transmit Override
**Spec:** `A.5.4.7.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Operator ist berechtigt, sowohl die Listen-Before-Transmit-Pause als auch den Transmit-Lockout zu überschreiben (für Notfallzwecke).


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-CHAN-031 — Listen before Transmit: Verpflichtende Pause vor Call/Sound (`A.5.4.7`)
- [ ] ⬜ **`AC-CHAN-031-1`** — Vor jedem Call oder Sound wird für die Dauer Twt auf dem Kanal gelauscht.
- [ ] ⬜ **`AC-CHAN-031-2`** — Bei erkannten Verkehr wird das Senden unterlassen.
- [ ] ⬜ **`AC-CHAN-031-3`** — Ein aufgrund von Verkehr abgebrochener Sound wird erneut geplant.
- [ ] ⬜ **`AC-CHAN-031-4`** — Ein aufgrund von Verkehr abgebrochener Call wählt einen anderen Kanal.

### REQ-CHAN-032 — Listen-Before-Transmit-Dauer: programmierbar (`A.5.4.7.1`)
- [ ] ⬜ **`AC-CHAN-032-1`** — Die Listen-Before-Transmit-Dauer ist programmierbar.
- [ ] ⬜ **`AC-CHAN-032-2`** — Bei ALE-nur Kanälen: Pause ≥ 2 × Trw.
- [ ] ⬜ **`AC-CHAN-032-3`** — Bei anderen Kanälen: Pause ≥ 2 Sekunden.
- [ ] ⬜ **`AC-CHAN-032-4`** — Bereits verbrachte Hörzeit kann angerechnet werden.

### REQ-CHAN-033 — Zu detektierende Modulationen bei Listen Before Transmit (`A.5.4.7.2`)
- [ ] ⬜ **`AC-CHAN-033-1`** — Verkehrserkennung erfolgt gemäß A.4.2.2.

### REQ-CHAN-034 — Listen-Before-Transmit Override (`A.5.4.7.3`)
- [ ] ⬜ **`AC-CHAN-034-1`** — Der Operator kann die Listen-Before-Transmit-Pause überschreiben.
- [ ] ⬜ **`AC-CHAN-034-2`** — Der Operator kann den Transmit-Lockout überschreiben.

## 🧪 Tests
- `tests/test_channel_selection.cpp`

## 💡 Implementierungshinweise

Pflichtpause vor TX. Dauer programmierbar. Erkennt ALE, DSB, AM, FSK, CW, SSB. Override möglich.

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
