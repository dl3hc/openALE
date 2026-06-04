# FEAT-ADDR-005 — Self, Null, In-Link Adressen

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `ADDR`

## 📁 Module
- `src/ale_word.cpp`
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-ADDR-013 — Selbstadressierung mit eigenen Adressen
**Spec:** `A.5.2.4.10` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Für Selbsttest, Wartung und andere Zwecke müssen Stationen in der Lage sein, ihre eigenen Adressen in Rufen zu verwenden. Wenn eine Selbstadressierungsfunktion erforderlich ist, müssen die folgenden Selbstadressierungsstrukturen und -protokolle verwendet werden. Alle im Standard zulässigen Rufstrukturen und -protokolle, die einen spezifisch adressierten Calling Cycle enthalten, müssen akzeptabel sein, sofern es sich nicht um AllCall oder AnyCall handelt. Die Station darf dabei eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

### REQ-ADDR-014 — Null-Adresse ohne Ziel, Antwort oder Annahme
**Spec:** `A.5.2.4.11` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Für Test, Wartung, Pufferzeiten und andere Zwecke muss die Station eine Null-Adresse verwenden, die von keiner Station adressiert, angenommen oder beantwortet wird. Wenn eine Null-Adressfunktion erforderlich ist, muss das Standardprotokoll für die Null-Adresse verwendet werden. Die spezielle Null-Adress-Form muss „TO @@@“ oder „REP @@@“ sein, wenn sie unmittelbar nach einem anderen TO steht. Die Null-Adresse darf nur TO oder REP verwenden und nur im Calling Cycle verwendet werden. In Gruppenrufen darf sie nur im führenden Ruf erscheinen und nicht im Scanning Call. Null-Adressen dürfen niemals in der Schlusssequenz verwendet werden. Wenn eine Null-Adresse in einem Gruppenruf erscheint, wird kein Platz in der zugehörigen Antwortslot-Zeile für eine Station belegt; der Slot bleibt leer und kann als Puffer genutzt werden.

### REQ-ADDR-015 — In-Link-Adresse für alle Mitglieder eines Links
**Spec:** `A.5.2.4.12` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die In-Link-Adressfunktion muss dazu dienen, dass alle Mitglieder des etablierten Links auf die Informationen reagieren, die in dem Frame enthalten sind, der die In-Link-Adresse trägt. Die In-Link-Adresse muss „?@?“ sein. Wenn ein Funkgerät in den verknüpften Zustand mit einer oder mehreren Stationen eintritt, muss es die Menge der als eigene Adressen erkannten Adressen um die In-Link-Adresse erweitern. Wenn ein Frame von einem Mitglied des Links unter Verwendung der In-Link-Adresse übertragen wird, sind alle Mitglieder öffentlich adressiert und müssen die Frame-Information verwenden. Wenn ein Mitglied einen Frame mit TWAS-Präambel sendet, müssen alle Mitglieder erkennen, dass die sendende Station den Link verlassen hat.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-ADDR-013 — Selbstadressierung mit eigenen Adressen (`A.5.2.4.10`)
- [ ] ⬜ **`AC-ADDR-013-1`** — Stationen können ihre eigenen Adressen in Rufen verwenden.
- [ ] ⬜ **`AC-ADDR-013-2`** — Selbstadressierung wird für Selbsttest, Wartung und andere Zwecke unterstützt.
- [ ] ⬜ **`AC-ADDR-013-3`** — Spezifisch adressierte Calling Cycles sind zulässig, sofern sie kein AllCall und kein AnyCall sind.
- [ ] ⬜ **`AC-ADDR-013-4`** — Eine Station darf eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

### REQ-ADDR-014 — Null-Adresse ohne Ziel, Antwort oder Annahme (`A.5.2.4.11`)
- [ ] ⬜ **`AC-ADDR-014-1`** — Eine Null-Adresse ist weder adressiert noch angenommen noch beantwortet.
- [ ] ⬜ **`AC-ADDR-014-2`** — Die spezielle Null-Adress-Form ist „TO @@@“ oder „REP @@@“ unter der angegebenen Folgebedingung.
- [ ] ⬜ **`AC-ADDR-014-3`** — Die Null-Adresse verwendet nur TO oder REP.
- [ ] ⬜ **`AC-ADDR-014-4`** — Die Null-Adresse wird nur im Calling Cycle verwendet.
- [ ] ⬜ **`AC-ADDR-014-5`** — In Gruppenrufen erscheint die Null-Adresse nur im führenden Ruf und nicht im Scanning Call.
- [ ] ⬜ **`AC-ADDR-014-6`** — Null-Adressen werden niemals in der Schlusssequenz verwendet.
- [ ] ⬜ **`AC-ADDR-014-7`** — Ein gruppenrufbezogener Slot bleibt leer, wenn eine Null-Adresse dort verwendet wird.

### REQ-ADDR-015 — In-Link-Adresse für alle Mitglieder eines Links (`A.5.2.4.12`)
- [ ] ⬜ **`AC-ADDR-015-1`** — Die In-Link-Adresse gilt für alle Mitglieder des etablierten Links.
- [ ] ⬜ **`AC-ADDR-015-2`** — Die In-Link-Adresse ist „?@?“.
- [ ] ⬜ **`AC-ADDR-015-3`** — Ein Funkgerät erweitert im verknüpften Zustand seine als eigene Adressen erkannten Adressen um die In-Link-Adresse.
- [ ] ⬜ **`AC-ADDR-015-4`** — Ein Frame mit In-Link-Adresse adressiert alle Mitglieder öffentlich.
- [ ] ⬜ **`AC-ADDR-015-5`** — Ein Frame mit TWAS-Präambel signalisiert allen Mitgliedern, dass die sendende Station den Link verlassen hat.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

Null='@@@' (nur TO/REP im Calling Cycle). In-Link='?@?' (alle Link-Mitglieder). Self: mehrere Adressen möglich.

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
