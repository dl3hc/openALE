# FEAT-CHAN-001 — LQA-Messung (BER, SINAD, MP)

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `CHAN`

## 📁 Module
- `src/ale_state_machine.cpp`
- `include/ale_state_machine.h`

## 🔗 Depends on
- ⬜ `FEAT-FEC-005` — Unanimous-Votes-Erfassung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-CHAN-001 — Channel-Selection-System: Grundprinzip
**Spec:** `A.5.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das ALE-System muss ein Channel-Selection-System implementieren, das es einer Station erlaubt, aus einem vorab vereinbarten Satz von Kanälen automatisch den besten verfügbaren Kanal für Calling und Kommunikation auszuwählen. Die Auswahl basiert auf gespeicherten Link-Quality-Daten und aktuellen Kanalbelegungsinformationen.

### REQ-CHAN-002 — Link Quality Analysis (LQA): Grundfunktion
**Spec:** `A.5.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das ALE-System muss eine Link Quality Analysis (LQA) durchführen, die Qualitätsmessungen aus empfangenen ALE-Signalen ableitet und im Link Quality Memory speichert. Die LQA-Daten bilden die Grundlage für die Kanalauswahl.

### REQ-CHAN-003 — LQA zur Kanalauswertung und -auswahl
**Spec:** `A.5.4.1 / Absatz 1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> LQA-Daten sind zur Bewertung der Kanäle und zur Unterstützung der Auswahl eines "besten" (oder akzeptablen) Kanals für Calling und Kommunikation zu verwenden.

### REQ-CHAN-004 — Kontinuierliche LQA-Überwachung während der Kommunikation
**Spec:** `A.5.4.1 / Absatz 2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> LQA ist zur kontinuierlichen Überwachung der Link-Qualität während der mit ALE-Signalisierung führenden Kommunikation zu verwenden.

### REQ-CHAN-005 — Verfügbarkeit und Übermittlung der gespeicherten LQA-Werte
**Spec:** `A.5.4.1 / Absatz 3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die gespeicherten LQA-Werte sind auf Anfrage oder nach Anweisung des Netzwerkmanagers übertragbar zu machen.

### REQ-CHAN-006 — Automatische Einfügung des CMD LQA-Words
**Spec:** `A.5.4.1 / Absatz 4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Alle ALE-Stationen sind automatisch verpflichtet, das CMD LQA-Word in die Message-Sections ihrer Signale und Handshakes einzufügen, wenn dazu von der handshaking Station angefragt, in einem Netzwerk vereinbart oder vom Protokoll vorgeschrieben, es sei denn, der Operator oder Controller weist ausdrücklich etwas anderes an.

### REQ-CHAN-007 — LQA-Anfrage durch Control-Bit KA1 (polling-capable Station)
**Spec:** `A.5.4.1 / Absatz 5` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Eine ALE-Station, die LQA-Informationen erfordert und verwenden kann (polling-fähig), kann die Daten von einer anderen Station anfordern, indem das Control-Bit KA1 im CMD LQA-Wort auf "1" gesetzt wird.

### REQ-CHAN-008 — KA1 auf "0" setzen bei nicht-polling-fähigen Stationen
**Spec:** `A.5.4.1 / Absatz 6` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Eine ALE-Station, die CMD LQA sendet, aber LQA-Informationen nicht verwenden kann (nicht polling-fähig), muss das Control-Bit KA1 auf "0" setzen.

### REQ-CHAN-009 — Aktiv/Passiv-Entscheidung für LQA als Netzwerkmanagement-Entscheidung
**Spec:** `A.5.4.1 / Absatz 7` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Ob LQA aktiv oder passiv ist, ist eine Netzwerkmanagement-Entscheidung.

### REQ-CHAN-010 — LQA-Scores für Operator-Anzeige: höhere Zahlen = bessere Kanäle
**Spec:** `A.5.4.1 / Absatz 8` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Für menschliche Bedienung sollten LQA-Scores, die dem Operator angezeigt werden, höhere (Zahl-)Werte für bessere Kanäle haben.

### REQ-CHAN-011 — BER-Messung durch Zählen nicht-einstimmiger Abstimmungen
**Spec:** `A.5.4.1.1 / Absatz 1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die BER-Messung erfolgt durch Zählen der Anzahl der nicht-einstimmigen (2/3) Abstimmungen (out of 48) im Majority-Vote-Decoder. Der Messbereich erstreckt sich von 0 bis 48.

### REQ-CHAN-012 — BER-Berechnung nach erreichtem Word-Sync
**Spec:** `A.5.4.1.1 / Absatz 2, 3, 4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Nach Erreichen des Word-Syncs sind alle empfangenen Words in einem Frame zu messen und ein linearer Durchschnitt BER/LQA wie folgt zu berechnen: Wenn der Golay-Decoder keine unkorrigierbaren Fehler in beiden Hälften des ALE-Words meldet, ist die Anzahl der nicht-einstimmigen Abstimmungen zum Gesamtsummenwert hinzuzufügen. Wenn mindestens eine Hälfte unkorrigierbare Fehler enthielt, sind die nicht-einstimmigen Abstimmungen zu verwerfen und 48 (der Maximalwert) zum Gesamtsummenwert hinzuzufügen. Am Ende der Übertragung ist der Gesamtsummenwert durch die Anzahl der empfangenen Words zu teilen und im Link Quality Memory als aktueller BER-Code für die sendende Station und den tragenden Kanal zu speichern.

### REQ-CHAN-013 — SINAD-Messung als (S+N+D)/(N+D)-Verhältnis
**Spec:** `A.5.4.1.2 / Absatz 1, 2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die SINAD-Messung ist ein (S+N+D)/(N+D)-Verhältnis, gemittelt über die Dauer jedes empfangenen ALE-Signals. SINAD-Werte sind auf allen ALE-Signalen zu messen.

### REQ-CHAN-014 — MP-Messung ist optional
**Spec:** `A.5.4.1.3` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD`

> Die Messung von MP (Modulation Performance) mittels empfangener ALE-Signale ist optional.

### REQ-CHAN-015 — SINAD-Anzeige in dB
**Spec:** `A.5.4.1.4` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Die Anzeige von SINAD-Werten ist in dB durchzuführen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-CHAN-001 — Channel-Selection-System: Grundprinzip (`A.5.4`)
- [ ] ⬜ **`AC-CHAN-001-1`** — Das System implementiert eine automatische Kanalauswahl aus einem vorab vereinbarten Kanalsatz.
- [ ] ⬜ **`AC-CHAN-001-2`** — Die Kanalauswahl berücksichtigt gespeicherte Link-Quality-Daten.
- [ ] ⬜ **`AC-CHAN-001-3`** — Die Kanalauswahl berücksichtigt die aktuelle Kanalbelegung.

### REQ-CHAN-002 — Link Quality Analysis (LQA): Grundfunktion (`A.5.4`)
- [ ] ⬜ **`AC-CHAN-002-1`** — Das System leitet Qualitätsmessungen aus empfangenen ALE-Signalen ab.
- [ ] ⬜ **`AC-CHAN-002-2`** — Die Messwerte werden im Link Quality Memory gespeichert.
- [ ] ⬜ **`AC-CHAN-002-3`** — Die gespeicherten LQA-Daten bilden die Grundlage für die Kanalauswahl.

### REQ-CHAN-003 — LQA zur Kanalauswertung und -auswahl (`A.5.4.1 / Absatz 1`)
- [ ] ⬜ **`AC-CHAN-003-1`** — LQA-Werte beeinflussen die Kanalauswahl für Calling und Kommunikation.

### REQ-CHAN-004 — Kontinuierliche LQA-Überwachung während der Kommunikation (`A.5.4.1 / Absatz 2`)
- [ ] ⬜ **`AC-CHAN-004-1`** — Die Link-Qualität wird während der gesamten Kommunikation fortlaufend mit LQA überwacht.

### REQ-CHAN-005 — Verfügbarkeit und Übermittlung der gespeicherten LQA-Werte (`A.5.4.1 / Absatz 3`)
- [ ] ⬜ **`AC-CHAN-005-1`** — LQA-Werte sind auf Anfrage übertragbar.
- [ ] ⬜ **`AC-CHAN-005-2`** — LQA-Werte sind nach Anweisung des Netzwerkmanagers übertragbar.

### REQ-CHAN-006 — Automatische Einfügung des CMD LQA-Words (`A.5.4.1 / Absatz 4`)
- [ ] ⬜ **`AC-CHAN-006-1`** — CMD LQA-Wort wird auf Anfrage der handshaking Station eingefügt.
- [ ] ⬜ **`AC-CHAN-006-2`** — CMD LQA-Wort wird bei vorheriger Netzvereinbarung eingefügt.
- [ ] ⬜ **`AC-CHAN-006-3`** — CMD LQA-Wort wird bei protokollarischer Vorgabe eingefügt.
- [ ] ⬜ **`AC-CHAN-006-4`** — Eine explizite Anweisung des Operators oder Controllers hebt diese Pflicht auf.

### REQ-CHAN-007 — LQA-Anfrage durch Control-Bit KA1 (polling-capable Station) (`A.5.4.1 / Absatz 5`)
- [ ] ⬜ **`AC-CHAN-007-1`** — Eine polling-fähige Station kann durch Setzen von KA1="1" einen LQA-Report anfordern.

### REQ-CHAN-008 — KA1 auf "0" setzen bei nicht-polling-fähigen Stationen (`A.5.4.1 / Absatz 6`)
- [ ] ⬜ **`AC-CHAN-008-1`** — Nicht-polling-fähige Stationen setzen KA1="0" in gesendetem CMD LQA.

### REQ-CHAN-009 — Aktiv/Passiv-Entscheidung für LQA als Netzwerkmanagement-Entscheidung (`A.5.4.1 / Absatz 7`)
- [ ] ⬜ **`AC-CHAN-009-1`** — Der aktive/ passive Modus von LQA wird durch Netzwerkmanagement gesteuert.

### REQ-CHAN-010 — LQA-Scores für Operator-Anzeige: höhere Zahlen = bessere Kanäle (`A.5.4.1 / Absatz 8`)
- [ ] ⬜ **`AC-CHAN-010-1`** — Die Operator-Anzeige zeigt höhere Zahlenwerte für bessere Kanäle.

### REQ-CHAN-011 — BER-Messung durch Zählen nicht-einstimmiger Abstimmungen (`A.5.4.1.1 / Absatz 1`)
- [ ] ⬜ **`AC-CHAN-011-1`** — BER wird durch Zählen nicht-einstimmiger Majority-Vote-Abstimmungen gemessen.
- [ ] ⬜ **`AC-CHAN-011-2`** — Der Messbereich beträgt 0 bis 48.

### REQ-CHAN-012 — BER-Berechnung nach erreichtem Word-Sync (`A.5.4.1.1 / Absatz 2, 3, 4`)
- [ ] ⬜ **`AC-CHAN-012-1`** — Bei fehlenden unkorrigierbaren Fehlern in beiden Hälften: nicht-einstimmige Abstimmungen zur Gesamtsumme addieren.
- [ ] ⬜ **`AC-CHAN-012-2`** — Bei unkorrigierbaren Fehlern in mindestens einer Hälfte: 48 zur Gesamtsumme addieren (nicht-einstimmige Abstimmungen verwerfen).
- [ ] ⬜ **`AC-CHAN-012-3`** — Der Durchschnittswert (Gesamtsumme / Anzahl empfangener Words) wird im Link Quality Memory gespeichert.
- [ ] ⬜ **`AC-CHAN-012-4`** — Der gespeicherte Wert wird der sendenden Station und dem tragenden Kanal zugeordnet.

### REQ-CHAN-013 — SINAD-Messung als (S+N+D)/(N+D)-Verhältnis (`A.5.4.1.2 / Absatz 1, 2`)
- [ ] ⬜ **`AC-CHAN-013-1`** — SINAD wird als (S+N+D)/(N+D)-Verhältnis gemessen.
- [ ] ⬜ **`AC-CHAN-013-2`** — Die Messung erfolgt über die volle Dauer des empfangenen ALE-Signals.
- [ ] ⬜ **`AC-CHAN-013-3`** — SINAD wird auf allen empfangenen ALE-Signalen gemessen.

### REQ-CHAN-014 — MP-Messung ist optional (`A.5.4.1.3`)
- [ ] ⬜ **`AC-CHAN-014-1`** — Eine Implementierung kann MP-Messung unterstützen oder unterlassen.

### REQ-CHAN-015 — SINAD-Anzeige in dB (`A.5.4.1.4`)
- [ ] ⬜ **`AC-CHAN-015-1`** — SINAD-Werte werden dem Operator in dB angezeigt.

## 🧪 Tests
- `tests/test_lqa_analyzer.cpp`

## 💡 Implementierungshinweise

BER = (48 - unanimous_count)/48. SINAD = (S+N+D)/(N+D) in dB. MP optional. Scores: höher = besser für Operator-Anzeige.

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
