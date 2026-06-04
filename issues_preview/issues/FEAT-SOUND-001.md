# FEAT-SOUND-001 — Single-Channel Sounding

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD` &nbsp;|&nbsp; **Domain:** `SOUND`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-FRAME-005` — Conclusion
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SOUND-002 — Sounding: Einseitige, periodische Übertragung auf unbesetzten Kanälen
**Spec:** `A.5.3.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das Sounding ist eine einseitige, einwegige Übertragung, die in periodischen Intervallen auf unbesetzten Kanälen durchgeführt wird. Sounding ist keine interaktive, zweiseitige Technik wie Polling. Allerdings gibt die Identifizierung einer Station durch Hören ihres Sounding-Signals eine hohe Wahrscheinlichkeit (aber keine Garantie) für zweiseitige Konnektivität und kann passiv am Empfänger erfolgen. Sounding verwendet die standardmäßige ALE-Signalisierung; jede Station kann Sounding-Signale empfangen. Als Minimum muss die (Adresse)Information dem Bediener angezeigt werden und für Stationen mit Konnektivitäts- und LQA-Speichern muss die Information gespeichert und später für den Linkaufbau verwendet werden. Wenn eine Station auf einem Kanal, der zum Sounding vorgesehen ist, kürzlich Sendungen hatte, muss es nicht notwendig sein, erneut auf diesem Kanal zu senden, bis das Sounding-Intervall, restartet von diesen letzten Sendungen, abgelaufen ist. Wenn ein Netz (oder eine Gruppe) von Stationen abgehört wird, dienen ihre Antworten als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger. Alle Stationen müssen in der Lage sein, periodisches Sounding auf vorab vereinbarten Kanälen durchzuführen. Die Sounding-Fähigkeit kann vom Bediener oder Controller selektiv aktiviert werden, und der Zeitraum zwischen den Sounds kann vom Bediener oder Controller gemäß den Systemanforderungen einstellbar sein. Wenn verfügbar und nicht anderweitig zugewiesen oder vom Bediener oder Controller gerichtet, müssen alle ALE-Stationen die Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

### REQ-SOUND-003 — Sounding-Struktur: Identisch zum Basic Call, ohne Calling Cycle und Message
**Spec:** `A.5.3.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die Struktur des Sounds ist virtually identisch mit dem Basic Call; jedoch ist der Calling Cycle nicht nötig und es gibt keinen Message-Abschnitt. Es ist nur notwendig, den Conclusion (Terminator) zu senden, der die sendende Station identifiziert. Der Typ des Wortes, entweder TIS oder TWAS (aber nie beide), zeigt an, ob potenzielle Anrufer ermutigt oder ignoriert werden sollen.

### REQ-SOUND-004 — Minimale redundante Sounding-Zeit (Trs)
**Spec:** `A.5.3.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die minimale redundante Sounding-Zeit (Trs) ist gleich der standardmäßigen Single-Word-Address-Calling-Zeit (Tlc), also 784 ms.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SOUND-002 — Sounding: Einseitige, periodische Übertragung auf unbesetzten Kanälen (`A.5.3.1`)
- [ ] ⬜ **`AC-SOUND-002-1`** — Sounding ist eine einseitige, einwegige Übertragung.
- [ ] ⬜ **`AC-SOUND-002-2`** — Sounding wird in periodischen Intervallen auf unbesetzten Kanälen durchgeführt.
- [ ] ⬜ **`AC-SOUND-002-3`** — Sounding ist keine interaktive, zweiseitige Technik.
- [ ] ⬜ **`AC-SOUND-002-4`** — Die Identifizierung durch Hören des Sounding-Signals gibt eine hohe Wahrscheinlichkeit für zweiseitige Konnektivität.
- [ ] ⬜ **`AC-SOUND-002-5`** — Sounding kann passiv am Empfänger erfolgen.
- [ ] ⬜ **`AC-SOUND-002-6`** — Sounding verwendet die standardmäßige ALE-Signalisierung.
- [ ] ⬜ **`AC-SOUND-002-7`** — Jede Station kann Sounding-Signale empfangen.
- [ ] ⬜ **`AC-SOUND-002-8`** — Die Adresse-Information muss dem Bediener als Minimum angezeigt werden.
- [ ] ⬜ **`AC-SOUND-002-9`** — Stationen mit Konnektivitäts- und LQA-Speichern müssen die Information speichern und später für den Linkaufbau verwenden.
- [ ] ⬜ **`AC-SOUND-002-10`** — Wenn eine Station kürzlich auf einem zum Sounding vorgesehenen Kanal gesendet hat, kann das erneute Senden entfallen, bis das Sounding-Intervall seit den letzten Sendungen abgelaufen ist.
- [ ] ⬜ **`AC-SOUND-002-11`** — Antworten eines abgehörten Netzes (oder einer Gruppe) dienen als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger.
- [ ] ⬜ **`AC-SOUND-002-12`** — Alle Stationen müssen periodisches Sounding auf vorab vereinbarten Kanälen durchführen können.
- [ ] ⬜ **`AC-SOUND-002-13`** — Die Sounding-Fähigkeit kann selektiv vom Bediener oder Controller aktiviert werden.
- [ ] ⬜ **`AC-SOUND-002-14`** — Der Zeitraum zwischen den Sounds kann vom Bediener oder Controller einstellbar sein.
- [ ] ⬜ **`AC-SOUND-002-15`** — Verfügbare Stationen müssen Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

### REQ-SOUND-003 — Sounding-Struktur: Identisch zum Basic Call, ohne Calling Cycle und Message (`A.5.3.1`)
- [ ] ⬜ **`AC-SOUND-003-1`** — Die Struktur des Sounds ist identisch mit dem Basic Call, mit Ausnahmen des Calling Cycles und des Message-Abschnitts.
- [ ] ⬜ **`AC-SOUND-003-2`** — Der Conclusion (Terminator) muss gesendet werden, um die sendende Station zu identifizieren.
- [ ] ⬜ **`AC-SOUND-003-3`** — Nur entweder TIS oder TWAS wird verwendet, niemals beide im selben Frame.
- [ ] ⬜ **`AC-SOUND-003-4`** — Der Worttyp TIS zeigt an, dass potenzielle Anrufer ermutigt werden.
- [ ] ⬜ **`AC-SOUND-003-5`** — Der Worttyp TWAS zeigt an, dass potenzielle Anrufer ignoriert werden sollen.

### REQ-SOUND-004 — Minimale redundante Sounding-Zeit (Trs) (`A.5.3.1`)
- [ ] ⬜ **`AC-SOUND-004-1`** — Die minimale redundante Sounding-Zeit (Trs) muss 784 ms betragen.

## 🧪 Tests
- `tests/test_sounding.cpp`

## 💡 Implementierungshinweise

Nur Conclusion senden (TIS=Accept, TWAS=Reject). Kein Calling Cycle, kein Message. Intervall konfigurierbar.

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
