# FEAT-SOUND-002 — Multi-Channel Sounding

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD` &nbsp;|&nbsp; **Domain:** `SOUND`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- ⬜ `FEAT-SOUND-001` — Single-Channel Sounding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SOUND-005 — Single-Channel Sounding-Protokoll
**Spec:** `A.5.3.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die fundamentale Fähigkeit zum automatischen Sounding auf einem Kanal muss gemäß dem Sounding-Protokoll (siehe Figure A-22) implementiert werden. Als Option können Stationen dieses Protokoll für Single-Channel Sounding, Connectivity Tracking und die Übertragung ihrer Verfügbarkeit für Rufe und Verkehr verwenden. Das Basisprotokoll besteht nur aus einem Teil: dem Sound. Der Sound enthält seine eigene Adresse ("TIS A"). Wenn "A" Anrufer ermutigt und einen empfängt, muss "A" dem Sound das optionale Handshake-Protokoll aus A.5.3.4 folgen. Wenn "A" plant, Anrufer zu ignorieren, muss es TWAS verwenden, was "B" und anderen mitteilt, keine Anrufe zu versuchen, und dann sofort zu normal "available" zurückkehren. In einigen Systemen muss eine Multikanal-Station "A" periodisch zu einem Single-Channel-Netz senden, um ihm mitzuteilen, dass sie auf diesem Kanal aktiv und verfügbar ist. Bei Empfang von "A"s Sound müssen "B" und andere Stationen "A"s Adresse als empfangenen Sound anzeigen und, wenn sie über LQA- und Connectivity-Speicher verfügen, die Konnektivitätsinformation speichern.

### REQ-SOUND-006 — Multichannel Sounding: Kompatibilität mit Scanning
**Spec:** `A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Sounding muss mit dem Scanning-Timing kompatibel sein. Alle Stationen müssen in der Lage sein, die unten beschriebenen Scanning-Sounding-Protokolle durchzuführen, auch wenn sie auf einer festen Frequierung betrieben werden. Diese Protokolle stellen einseitige Konnektivität zwischen Stationen auf jedem verfügbaren, gemeinsam abgetasteten Kanal sicher und bestätigen sie positiv; sie unterstützen auch den Aufbau von Links zwischen Stationen, die auf Kontakt warten. Stationen müssen diese Protokolle für Multichannel Sounding, Connectivity Tracking und die Übertragung ihrer Verfügbarkeit für Rufe und Verkehr verwenden.

### REQ-SOUND-007 — Multichannel Sounding: Timing-Berechnung für Scanning Sound Frame
**Spec:** `A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Alle Timing-Betrachtungen und Berechnungen für individuelles Scanning-Calling gelten auch für Scanning Sounding, einschließlich Sounding-Cycle-Zeiten und (optionaler) Handshake-Zeiten. Der Scanning Sound ist identisch mit dem Single-Channel Sound, außer durch die Erweiterung der redundanten Sounding-Zeit (Trs) durch Hinzufügen von Wörtern zur Scan Sounding Time (Tss), um eine Scanning Redundant Sound Time (Tsrs) zu bilden: Tsrs = Tss + Trs. Die Scan Sounding Time (Tss) hat den gleichen Zweck wie die Scan Calling Time (Tsc) für eine äquivalente Scanning-Situation, verwendet aber nur die Volladresse des Senders. Die Kanal-Scanning-Sequenzen und Auswahlkriterien für individuelles Scanning Calling gelten auch für Scanning Sounding. Die zu sondierenden Kanäle werden als "Sound Set" bezeichnet und sind üblicherweise identisch mit dem "Scan Set", der für Scanning verwendet wird.

### REQ-SOUND-008 — Call-Rejection Scanning Sounding Protocol
**Spec:** `A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Wenn eine Station "A" plant, Anrufer von "B" nach "A"s Sound zu ignorieren, muss das Call-Rejection Scanning Sounding Protocol verwendet werden. In einer für individuelles Scanning Calling identischen Weise landet "A" auf dem ersten Kanal im Scan Set, wartet (Twt), um zu sehen, ob der Kanal frei ist, stimmt den Coupler ab (Tt), geht auf volle Leistung und initiiert den Frame der Scanning Redundant Sound Times (Tsrs). Dieser Scanning Sound muss länger sein als die Scan-Periode (Ts) der Empfänger-Stationen um mindestens Trs, um eine verfügbare Detektionsperiode von mindestens Tdrw = 784 ms sicherzustellen. "A" verwendet auch "TWAS A" redundant, um mitzuteilen, dass keine Anrufe erwartet werden. Nach Abschluss des Scanning Sound Frame überlässt "A" sofort den Kanal und geht zum nächsten Kanal im Sound Set. Dieses Verfahren wiederholt sich, bis alle Kanäle gesendet wurden oder übersprungen wurden, falls besetzt. Wenn der calling ALE-Station alle vorab vereinbarten Sound Set Kanäle erschöpft sind, muss sie automatisch zum normalen "available" receive scan mode zurückkehren.

### REQ-SOUND-009 — Scanning Detection: Mindestens eine Erfassungsmöglichkeit pro Kanal
**Spec:** `A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Wie in der Abbildung dargestellt, wurde das Timing von "A" und "B" vorab vereinbart, um sicherzustellen, dass "B" mindestens eine Gelegenheit pro Kanal hat, "A"s Sound zu erreichen und zu "captures". Spezifisch: "B" kommt an, erkennt Sounds, wartet auf gute Wörter, liest mindestens drei (redundante) "TWAS A" (in 3 bis 4 Tw), speichert die Konnektivitätsinformation und verlässt sofort den Kanal, um das Scanning fortzusetzen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SOUND-005 — Single-Channel Sounding-Protokoll (`A.5.3.2`)
- [ ] ⬜ **`AC-SOUND-005-1`** — Alle Stationen besitzen die fundamentale Fähigkeit zum automatischen Single-Channel Sounding gemäß dem Standard-Protokoll.
- [ ] ⬜ **`AC-SOUND-005-2`** — Stationen können optional das Protokoll für Single-Channel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung verwenden.
- [ ] ⬜ **`AC-SOUND-005-3`** — Das Basis-Sounding-Protokoll besteht nur aus dem Sound.
- [ ] ⬜ **`AC-SOUND-005-4`** — Der Sound enthält die eigene Adresse der sendenden Station.
- [ ] ⬜ **`AC-SOUND-005-5`** — Wenn eine Station Anrufer ermutigt und einen empfängt, folgt sie dem optionalen Handshake-Protokoll aus A.5.3.4.
- [ ] ⬜ **`AC-SOUND-005-6`** — Wenn eine Station Anrufer ignorieren will, verwendet sie TWAS und kehrt sofort zu "available" zurück.
- [ ] ⬜ **`AC-SOUND-005-7`** — Multikanal-Stationen können periodisch zu einem Single-Channel-Netz senden, um Aktivität und Verfügbarkeit zu melden.
- [ ] ⬜ **`AC-SOUND-005-8`** — Bei Empfang eines Sounds müssen Empfänger die Adresse anzeigen und bei Verfügbarkeit Konnektivitätsinformation speichern.

### REQ-SOUND-006 — Multichannel Sounding: Kompatibilität mit Scanning (`A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-006-1`** — Sounding ist mit dem Scanning-Timing kompatibel.
- [ ] ⬜ **`AC-SOUND-006-2`** — Alle Stationen können Scanning-Sounding-Protokolle durchführen, auch auf fester Frequenz.
- [ ] ⬜ **`AC-SOUND-006-3`** — Die Protokolle stellen einseitige Konnektivität auf jedem verfügbaren, gemeinsam abgetasteten Kanal sicher und bestätigen sie.
- [ ] ⬜ **`AC-SOUND-006-4`** — Stationen verwenden die Protokolle für Multichannel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung.

### REQ-SOUND-007 — Multichannel Sounding: Timing-Berechnung für Scanning Sound Frame (`A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-007-1`** — Alle Timing-Betrachtungen und Berechnungen für individuelles Scanning Calling gelten auch für Scanning Sounding.
- [ ] ⬜ **`AC-SOUND-007-2`** — Der Scanning Sound unterscheidet sich vom Single-Channel Sound durch die Erweiterung von Trs durch Tss zu Tsrs.
- [ ] ⬜ **`AC-SOUND-007-3`** — Die Beziehung muss gelten: Tsrs = Tss + Trs.
- [ ] ⬜ **`AC-SOUND-007-4`** — Tss hat den gleichen Zweck wie Tsc für eine äquivalente Scanning-Situation.
- [ ] ⬜ **`AC-SOUND-007-5`** — Kanal-Scanning-Sequenzen und Auswahlkriterien für Scanning Calling gelten auch für Scanning Sounding.
- [ ] ⬜ **`AC-SOUND-007-6`** — Das "Sound Set" für sondierende Kanäle ist üblicherweise identisch mit dem "Scan Set".

### REQ-SOUND-008 — Call-Rejection Scanning Sounding Protocol (`A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-008-1`** — Bei geplantem Ignorieren von Anrufern muss das Call-Rejection Scanning Sounding Protocol verwendet werden.
- [ ] ⬜ **`AC-SOUND-008-2`** — Die Station landet auf dem ersten Kanal im Scan Set und wartet (Twt), um die Kanalbelegung zu prüfen.
- [ ] ⬜ **`AC-SOUND-008-3`** — Nach dem Abstimmen (Tt) geht die Station auf volle Leistung und initiiert den Scanning Sound Frame.
- [ ] ⬜ **`AC-SOUND-008-4`** — Der Scanning Sound muss den Scan-Perioden der Empfänger-Stationen um mindestens Trs überschreiten, um Tdrw ≥ 784 ms sicherzustellen.
- [ ] ⬜ **`AC-SOUND-008-5`** — "TWAS A" wird redundant verwendet, um mitzuteilen, dass keine Anrufe erwartet werden.
- [ ] ⬜ **`AC-SOUND-008-6`** — Nach dem Scanning Sound Frame verlässt die Station sofort den Kanal und geht zum nächsten Kanal im Sound Set.
- [ ] ⬜ **`AC-SOUND-008-7`** — Das Verfahren wiederholt sich, bis alle Kanäle gesendet oder übersprungen (bei Belegung) wurden.
- [ ] ⬜ **`AC-SOUND-008-8`** — Nach Erschöpfung aller Sound Set Kanäle kehrt die Station automatisch zum "available" receive scan mode zurück.

### REQ-SOUND-009 — Scanning Detection: Mindestens eine Erfassungsmöglichkeit pro Kanal (`A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-009-1`** — Das Timing von Sounding- und Scanning-Stationen ist vorab vereinbart, um mindestens eine Erfassungsmöglichkeit pro Kanal sicherzustellen.
- [ ] ⬜ **`AC-SOUND-009-2`** — Die scannende Station muss mindestens drei redundante Wörter des Sounding-Senders lesen können.
- [ ] ⬜ **`AC-SOUND-009-3`** — Die scannende Station speichert die Konnektivitätsinformation und setzt das Scanning fort.

## 🧪 Tests
- `tests/test_sounding.cpp`

## 💡 Implementierungshinweise

Sounding kompatibel mit Scanning-Receivern. Trs und Timing aus A.5.3.3. Design noch offen.

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
