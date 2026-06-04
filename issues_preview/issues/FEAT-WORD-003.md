# FEAT-WORD-003 — Message & Extension Words (CMD / DATA / REP)

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WORD`

## 📁 Module
- `src/ale_word.cpp`
- `include/ale_word.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WORD-008 — CMD als Sonder-Designator für Message-Wörter
**Spec:** `A.5.2.3.3 / A.5.2.3.3.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Alle Message-Wörter müssen mit einem CMD-Präambelwort beginnen. CMD muss als Sonder-Designator für systemweite Koordination, Kommandierung, Kontrolle, Status, Information, Interoperation und andere Sonderzwecke verwendet werden. CMD muss für beliebige Kombinationen zwischen ALE-Stationen und Operatoren verwendbar sein. CMD ist nur innerhalb des Message-Abschnitts des ALE-Frames zulässig und muss innerhalb eines Frames einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz haben, damit die vorgesehenen Empfänger und der Sender eindeutig bestimmt werden. Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts. Die Orderwire-Funktionen werden mit CMD selbst oder in Kombination mit REP- und DATA-Message-Wörtern und den zugehörigen Funktionen ausgelöst.

### REQ-WORD-009 — DATA als Erweiterungs- und Informationswort
**Spec:** `A.5.2.3.4.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das DATA-Wort muss verwendet werden, um das Datenfeld eines vorherigen Worttyps zu erweitern, sofern dieses Wort nicht selbst DATA ist, oder um Informationen in einer Nachricht zu übertragen. In Verbindung mit TO, FROM, TIS oder TWAS muss DATA die Adresserweiterung von den ersten drei Zeichen auf sechs, neun oder mehr Zeichen in Vielfachen von drei ermöglichen, wenn es abwechselnd mit REP verwendet wird. Die ausgewählte Grenze für die Adresserweiterung beträgt insgesamt 15 Zeichen. In Verbindung mit CMD ist die Funktion von DATA durch den Standard für Message-Wörter und -Funktionen vorgegeben.

### REQ-WORD-010 — REP als Wiederholungs- und Erweiterungswort
**Spec:** `A.5.2.3.4.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das REP-Wort muss verwendet werden, um eine vorherige Präambelfunktion oder Wortbedeutung zu duplizieren, während der Inhalt des Datenfelds geändert wird. Jede Änderung von Wörtern oder Datenfeldbits erfordert eine Änderung der Präambelbits, um Unklarheit und Fehler zu vermeiden. Wenn sich ein Wort ändert, muss die Präambel auch dann geändert werden, wenn das Datenfeld identisch zum vorherigen Wort ist. In Verbindung mit TO muss REP eine Adresserweiterung ermöglichen, sodass mehr als eine Adresse spezifiziert werden kann. In Verbindung mit DATA darf REP zur Erweiterung und Vergrößerung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden. REP darf direkt nach jedem anderen Worttyp folgen, außer nach sich selbst und außer nach TIS oder TWAS.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WORD-008 — CMD als Sonder-Designator für Message-Wörter (`A.5.2.3.3 / A.5.2.3.3.1`)
- [ ] ⬜ **`AC-WORD-008-1`** — Jeder Message-Wort-Abschnitt beginnt mit CMD.
- [ ] ⬜ **`AC-WORD-008-2`** — CMD unterstützt die im Standard genannten systemweiten Sonderzwecke.
- [ ] ⬜ **`AC-WORD-008-3`** — CMD wird nur im Message-Abschnitt verwendet.
- [ ] ⬜ **`AC-WORD-008-4`** — Ein CMD im Frame hat einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz.
- [ ] ⬜ **`AC-WORD-008-5`** — Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts.
- [ ] ⬜ **`AC-WORD-008-6`** — Orderwire-Funktionen können über CMD allein oder in Kombination mit REP und DATA ausgelöst werden.

### REQ-WORD-009 — DATA als Erweiterungs- und Informationswort (`A.5.2.3.4.1`)
- [ ] ⬜ **`AC-WORD-009-1`** — DATA erweitert ein vorheriges Wortfeld, sofern das vorherige Wort nicht DATA ist.
- [ ] ⬜ **`AC-WORD-009-2`** — DATA kann Informationen in einer Nachricht transportieren.
- [ ] ⬜ **`AC-WORD-009-3`** — Mit TO, FROM, TIS oder TWAS erweitert DATA Adressen in Vielfachen von drei Zeichen.
- [ ] ⬜ **`AC-WORD-009-4`** — Die Adresserweiterung endet spätestens bei 15 Zeichen.
- [ ] ⬜ **`AC-WORD-009-5`** — Mit CMD gilt die standarddefinierte Funktion für Message-Wörter.

### REQ-WORD-010 — REP als Wiederholungs- und Erweiterungswort (`A.5.2.3.4.2`)
- [ ] ⬜ **`AC-WORD-010-1`** — REP dupliziert die vorherige Präambelfunktion oder Wortbedeutung bei verändertem Datenfeldinhalt.
- [ ] ⬜ **`AC-WORD-010-2`** — Jede Änderung von Wort oder Datenfeldbit erfordert eine Änderung der Präambel.
- [ ] ⬜ **`AC-WORD-010-3`** — Ein Wortwechsel wird auch dann durch eine geänderte Präambel angezeigt, wenn das Datenfeld unverändert bleibt.
- [ ] ⬜ **`AC-WORD-010-4`** — REP ermöglicht in Verbindung mit TO die Spezifikation mehrerer Adressen.
- [ ] ⬜ **`AC-WORD-010-5`** — REP darf zur Erweiterung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden.
- [ ] ⬜ **`AC-WORD-010-6`** — REP folgt nicht auf sich selbst sowie nicht auf TIS oder TWAS.
- [ ] ⬜ **`AC-WORD-010-7`** — REP darf nur dort eingesetzt werden, wo keine Mehrfachsender-Situation entsteht.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

REP darf nicht direkt auf TIS/TWAS folgen. Sequenzregel: TO, DATA, REP, DATA, REP.

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
