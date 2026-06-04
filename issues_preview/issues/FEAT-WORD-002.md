# FEAT-WORD-002 — Adresswörter (TO / TIS / TWAS / THRU / FROM)

**Status:** 🚧 `in-progress` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `WORD`

## 📁 Module
- `src/ale_word.cpp`
- `include/ale_word.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-WORD-003 — TO für direkte Zieladressen
**Spec:** `A.5.2.3.2.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das TO-Wort muss als Routing-Designator die Adresse der aktuell vorgesehenen Zielstation oder Zielstationen angeben, die den Ruf direkt empfangen sollen. TO muss in Einzelrufprotokollen für einzelne Stationen und in Netrufprotokollen für mehrere Netmitgliedsstationen verwendet werden, die über eine einzige Netadresse gerufen werden. Das TO-Wort selbst muss die ersten drei Zeichen einer Adresse enthalten. Erweiterte Adressen müssen in unmittelbar folgenden, abwechselnden DATA- und REP-Wörtern enthalten sein. Die Folge muss TO, DATA, REP, DATA und REP sein und darf nur so lang sein, wie es zur Aufnahme der Adresse nötig ist, höchstens jedoch fünf Adresswörter mit insgesamt 15 Zeichen.

### REQ-WORD-004 — TIS für die sendende Station und den Abschluss
**Spec:** `A.5.2.3.2.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das TIS-Wort muss als Routing-Designator die Adresse der aktuell sendenden oder sendenden Sounding-Station angeben. TIS muss, außer bei Verwendung von TWAS, in allen ALE-Protokollen zur Beendigung des ALE-Frames und der Übertragung verwendet werden. Es muss die Fortsetzung des Protokolls oder Handshakes anzeigen und andere Stationen je nach Protokoll veranlassen, anfordern oder einladen, zu antworten oder zu bestätigen. TIS muss zur Kennzeichnung des Call-Acceptance-Sounds verwendet werden. Das TIS-Wort selbst muss die ersten drei Zeichen der Adresse der rufenden Station enthalten. Erweiterte Adressen müssen in unmittelbar folgenden, abwechselnden DATA- und REP-Wörtern fortgesetzt werden. Die gesamte Adresse und der erforderliche Teil der TIS-, DATA-, REP-, DATA-, REP-Folge dürfen nur im Schlussabschnitt des ALE-Frames verwendet werden oder müssen ein vollständiges Sound bilden. TIS und TWAS dürfen nicht im selben Frame verwendet werden.

### REQ-WORD-005 — TWAS für die beendende Station
**Spec:** `A.5.2.3.2.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das TWAS-Wort muss als Routing-Designator genauso wie TIS verwendet werden, jedoch mit der Wirkung, dass es die Beendigung des ALE-Protokolls oder Handshakes anzeigt und Antworten oder Bestätigungen zurückweist, hemmt oder nicht einlädt. TWAS muss zur Kennzeichnung des Call-Rejection-Sounds verwendet werden. TIS und TWAS dürfen nicht im selben Frame verwendet werden.

### REQ-WORD-006 — THRU für Gruppenrufe im Scanning Call
**Spec:** `A.5.2.3.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das THRU-Wort muss im Scanning-Call-Abschnitt des Calling Cycles ausschließlich mit Gruppenrufprotokollen verwendet werden. Es muss abwechselnd mit REP als Routing-Designator eingesetzt werden, um das erste Adresswort von Stationen zu kennzeichnen, die direkt gerufen werden sollen. Jedes solche Adresswort darf nur ein grundlegendes Adresswort mit drei Zeichen Länge sein. In einem Gruppenruf sind höchstens fünf verschiedene erste Adresswörter zulässig. Die Folge darf nur aus der Abfolge THRU, REP bestehen. THRU darf nicht für erweiterte Adressen verwendet werden und darf nicht innerhalb des Leading-Call-Abschnitts verwendet werden. Sobald der Leading Call im Gruppenruf beginnt, muss die gesamte Gruppe mit vollständigen Adressen gerufen werden, die mit TO-Präambeln und den zugehörigen Strukturen gesendet werden.

### REQ-WORD-007 — FROM als optionale Kennung der sendenden Station
**Spec:** `A.5.2.3.2.5` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Das FROM-Wort ist ein optionaler Designator zur Identifikation der sendenden Station, ohne eine ALE-Frame-Beendigung wie TIS oder TWAS zu verwenden. Es muss die vollständige Adresse der sendenden Station enthalten; falls erforderlich, sind dafür zusätzlich DATA- und REP-Wörter zu verwenden, genau wie bei der TO-Adressstruktur. FROM sollte nur einmal pro ALE-Frame verwendet werden und nur unmittelbar vor einem CMD-Wort im Message-Abschnitt verwendet werden. Unter Vorgabe durch Operator oder Controller sollte FROM zur Bereitstellung einer schnellen Identifikation der sendenden Station eingesetzt werden, wenn die normale Schlusssequenz verzögert sein kann, etwa bei einem langen Message-Abschnitt.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-WORD-003 — TO für direkte Zieladressen (`A.5.2.3.2.1`)
- [x] ✅ **`AC-WORD-003-1`** — TO wird für Einzelrufe und Netrufe gemäß Standard verwendet.
- [x] ✅ **`AC-WORD-003-2`** — Ein TO-Wort enthält die ersten drei Zeichen der Adresse.
- [x] ✅ **`AC-WORD-003-3`** — Erweiterte Adressen werden unmittelbar durch abwechselnde DATA- und REP-Wörter fortgesetzt.
- [x] ✅ **`AC-WORD-003-4`** — Die zulässige Adressfolge endet spätestens bei fünf Adresswörtern beziehungsweise 15 Zeichen.

### REQ-WORD-004 — TIS für die sendende Station und den Abschluss (`A.5.2.3.2.2`)
- [x] ✅ **`AC-WORD-004-1`** — TIS kennzeichnet die aktuell sendende Station.
- [x] ✅ **`AC-WORD-004-2`** — TIS wird zur Beendigung des ALE-Frames und der Übertragung verwendet, außer wenn TWAS verwendet wird.
- [x] ✅ **`AC-WORD-004-3`** — TIS kann Antworten oder Bestätigungen anfordern, veranlassen oder einladen.
- [x] ✅ **`AC-WORD-004-4`** — TIS kennzeichnet den Call-Acceptance-Sound.
- [x] ✅ **`AC-WORD-004-5`** — Ein TIS-Wort enthält die ersten drei Zeichen der Adresse der rufenden Station.
- [x] ✅ **`AC-WORD-004-6`** — Erweiterte Adressen folgen unmittelbar in abwechselnden DATA- und REP-Wörtern.
- [x] ✅ **`AC-WORD-004-7`** — Die gesamte Adresse wird nur im Schlussabschnitt des ALE-Frames verwendet oder bildet ein vollständiges Sound.
- [x] ✅ **`AC-WORD-004-8`** — TIS und TWAS werden nicht im selben Frame verwendet.

### REQ-WORD-005 — TWAS für die beendende Station (`A.5.2.3.2.3`)
- [x] ✅ **`AC-WORD-005-1`** — TWAS kennzeichnet die Beendigung des ALE-Protokolls oder Handshakes.
- [x] ✅ **`AC-WORD-005-2`** — TWAS weist Antworten oder Bestätigungen zurück, hemmt sie oder lädt sie nicht ein.
- [x] ✅ **`AC-WORD-005-3`** — TWAS kennzeichnet den Call-Rejection-Sound.
- [x] ✅ **`AC-WORD-005-4`** — TIS und TWAS werden nicht im selben Frame verwendet.

### REQ-WORD-006 — THRU für Gruppenrufe im Scanning Call (`A.5.2.3.2.4`)
- [ ] ⬜ **`AC-WORD-006-1`** — THRU wird nur im Scanning Call von Gruppenrufprotokollen verwendet.
- [ ] ⬜ **`AC-WORD-006-2`** — THRU und REP alternieren in der vorgegebenen Sequenz.
- [x] ✅ **`AC-WORD-006-3`** — Jedes mit THRU referenzierte erste Adresswort ist auf drei Zeichen begrenzt.
- [ ] ⬜ **`AC-WORD-006-4`** — Ein Gruppenruf enthält höchstens fünf verschiedene erste Adresswörter.
- [x] ✅ **`AC-WORD-006-5`** — THRU wird nicht für erweiterte Adressen verwendet.
- [x] ✅ **`AC-WORD-006-6`** — Im Leading Call eines Gruppenrufs werden vollständige Adressen mit TO verwendet.
- [ ] ⬜ **`AC-WORD-006-7`** — Stationskonforme Systeme ignorieren Calls, die ihre Adresse in einem THRU-Wort außerhalb des Scanning Calls verwenden.
- [ ] ⬜ **`AC-WORD-006-8`** — Der THRU-Präambelwert ist für zukünftige indirekte und Relais-Protokolle reserviert.
- [ ] ⬜ **`AC-WORD-006-9`** — Der THRU-Präambelwert ist auch für AQC-ALE reserviert.

### REQ-WORD-007 — FROM als optionale Kennung der sendenden Station (`A.5.2.3.2.5`)
- [x] ✅ **`AC-WORD-007-1`** — FROM identifiziert die sendende Station ohne Verwendung einer Frame-Beendigung wie TIS oder TWAS.
- [x] ✅ **`AC-WORD-007-2`** — FROM enthält die vollständige Adresse der sendenden Station.
- [x] ✅ **`AC-WORD-007-3`** — Falls erforderlich, werden für eine erweiterte FROM-Adresse DATA- und REP-Wörter verwendet.
- [ ] ⬜ **`AC-WORD-007-4`** — FROM erscheint höchstens einmal pro ALE-Frame.
- [ ] ⬜ **`AC-WORD-007-5`** — FROM erscheint nur unmittelbar vor CMD im Message-Abschnitt.
- [x] ✅ **`AC-WORD-007-6`** — FROM kann zur schnellen Identifikation bei verzögerter normaler Schlusssequenz verwendet werden.
- [ ] ⬜ **`AC-WORD-007-7`** — Stationskonforme Systeme ignorieren Abschnitte von Calls, die FROM-Wörter in anderer als der unmittelbar vor CMD stehenden Sequenz verwenden.
- [ ] ⬜ **`AC-WORD-007-8`** — Der FROM-Präambelwert ist für zukünftige indirekte und Relais-Protokolle reserviert.
- [ ] ⬜ **`AC-WORD-007-9`** — Der FROM-Präambelwert ist auch für AQC-ALE reserviert.

## 🧪 Tests
- `tests/test_protocol.cpp`
- `tests/test_ale_calling.cpp` — 13 per-AC tests, alle grün (`ALECalling` in ctest)

## 💡 Implementierungshinweise

Alle 5 Adress-Preambles implementiert. TWS/TWAS = Wert 3 (gleicher Bit-Wert).

**Offene Punkte (blockieren Done-Kriterium):**
- AC-WORD-006-1/2/4/7: Gruppenruf-Protokoll (`CallingPhase::GROUP_CALL`) nicht implementiert — `NET_CALL_STUB` deckt nur Einzel- und Netruf ab.
- AC-WORD-007-4/5/7: FROM-Positionsvalidierung im Frame-Assembler nicht durchgesetzt.
- AC-WORD-006-8/9, AC-WORD-007-8/9: Reservierungs-Semantik für AQC/Relay gilt durch Spec-Konformität; keine Laufzeit-Prüfung vorgesehen.

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
