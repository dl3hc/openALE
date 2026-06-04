# FEAT-SYNC-003 — Synchronisationskriterien & Schwellwerte

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `SYNC`

## 📁 Module
- `src/ale2gmodem.cpp`
- `src/ale_fec_codec.cpp`

## 🔗 Depends on
- ⬜ `FEAT-SYNC-002` — Empfangsseitige Wortsynchronisation
- ⬜ `FEAT-FEC-005` — Unanimous-Votes-Erfassung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SYNC-006 — Synchronisationskriterien für jedes ALE-Wort
**Spec:** `A.5.2.6.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Decoder akzeptiert digitale Daten vom Empfangsdemodulator und führt Deinterleaving, Dekodierung, FEC und Datenprüfung durch. Zur anfänglichen und fortlaufenden Synchronisation sollten alle der folgenden Kriterien verwendet werden, um jedes ALE-Wort zu diskriminieren und zu lesen:

### REQ-SYNC-007 — Herstelleroptimierbare Parameter: Unanimous-Vote-Schwellwert und Golay-Modus
**Spec:** `A.5.2.6.3` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Die Anzahl der einstimmigen Stimmen stellt einen einfach anpassbaren Qualitäts-Diskriminator dar; der Schwellwert sollte vom Hersteller zur Leistungsoptimierung gewählt werden. Die Korrekturkapazität (Modus) des Golay-Codes sollte vom Hersteller zur Leistungsoptimierung gewählt werden und kann einen der vier Modi verwenden: (3/4, 2/5, 1/6, 0/7), wobei n/m bedeutet, dass bis zu „n" Fehler erkannt und korrigiert werden oder bis zu „m" Fehler erkannt, aber nicht korrigierbar sind. Als Designziel (DO) sollte eine automatische Anpassung des unanimous-vote-Schwellwerts und des Golay-Modus vorgesehen werden, um die Leistung unter wechselnden Bedingungen zu optimieren.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SYNC-006 — Synchronisationskriterien für jedes ALE-Wort (`A.5.2.6.3`)
- [ ] ⬜ **`AC-SYNC-006-1`** — Alle neun der oben genannten Kriterien werden gemeinsam zur Diskriminierung und zum Lesen jedes ALE-Worts angewendet.
- [ ] ⬜ **`AC-SYNC-006-2`** — Ein Wort wird nur dann akzeptiert, wenn sämtliche angewendeten Kriterien erfüllt sind.
- [ ] ⬜ **`AC-SYNC-006-3`** — Der unanimous-vote-Schwellwert wird als leicht anpassbares BER-Signal-Qualitäts-Diskriminierungsmittel verwendet.
- [ ] ⬜ **`AC-SYNC-006-4`** — Eine erfolgreiche Golay-Dekodierung zeigt an, dass alle erkannten Bitfehler innerhalb der Korrekturkapazität des FEC-Codes lagen und der Unkorrigierbar-Fehler-Flag nicht aufgetreten ist.
- [ ] ⬜ **`AC-SYNC-006-5`** — Akzeptable Präambeln sind solche, die innerhalb der Grenzen dieses Standards liegen, wie in A.5.2.3.1.3 definiert.
- [ ] ⬜ **`AC-SYNC-006-6`** — Akzeptable Zeichen bedeuten, dass jedes Zeichen innerhalb des zugehörigen ASCII-Subsets liegt.
- [ ] ⬜ **`AC-SYNC-006-7`** — Alle drei Zeichen müssen innerhalb des Basic-38-ASCII-Subsets liegen, wenn eine Routing-Präambel (z. B. TO) dekodiert wurde.
- [ ] ⬜ **`AC-SYNC-006-8`** — Bei einem initialen REP ist eine beliebige Bitkombination bedingt akzeptabel; ohne die notwendige Kenntnis des vorherigen Worts wird sie in den meisten Fällen als irrelevant betrachtet und verworfen.

### REQ-SYNC-007 — Herstelleroptimierbare Parameter: Unanimous-Vote-Schwellwert und Golay-Modus (`A.5.2.6.3`)
- [ ] ⬜ **`AC-SYNC-007-1`** — Das System bietet die Möglichkeit, den unanimous-vote-Schwellwert zur Leistungsoptimierung einzustellen.
- [ ] ⬜ **`AC-SYNC-007-2`** — Das System unterstützt alle vier Golay-Korrekturmodi: (3/4), (2/5), (1/6), (0/7).
- [ ] ⬜ **`AC-SYNC-007-3`** — Als Designziel (DO) wird eine automatische Anpassung von unanimous-vote-Schwellwert und Golay-Modus unter wechselnden Bedingungen bereitgestellt.

### Weitere Acceptance Criteria
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
- [ ] ⬜ **`AC-SOUND-003-1`** — Die Struktur des Sounds ist identisch mit dem Basic Call, mit Ausnahmen des Calling Cycles und des Message-Abschnitts.
- [ ] ⬜ **`AC-SOUND-003-2`** — Der Conclusion (Terminator) muss gesendet werden, um die sendende Station zu identifizieren.
- [ ] ⬜ **`AC-SOUND-003-3`** — Nur entweder TIS oder TWAS wird verwendet, niemals beide im selben Frame.
- [ ] ⬜ **`AC-SOUND-003-4`** — Der Worttyp TIS zeigt an, dass potenzielle Anrufer ermutigt werden.
- [ ] ⬜ **`AC-SOUND-003-5`** — Der Worttyp TWAS zeigt an, dass potenzielle Anrufer ignoriert werden sollen.
- [ ] ⬜ **`AC-SOUND-004-1`** — Die minimale redundante Sounding-Zeit (Trs) muss 784 ms betragen.
- [ ] ⬜ **`AC-SOUND-005-1`** — Alle Stationen besitzen die fundamentale Fähigkeit zum automatischen Single-Channel Sounding gemäß dem Standard-Protokoll.
- [ ] ⬜ **`AC-SOUND-005-2`** — Stationen können optional das Protokoll für Single-Channel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung verwenden.
- [ ] ⬜ **`AC-SOUND-005-3`** — Das Basis-Sounding-Protokoll besteht nur aus dem Sound.
- [ ] ⬜ **`AC-SOUND-005-4`** — Der Sound enthält die eigene Adresse der sendenden Station.
- [ ] ⬜ **`AC-SOUND-005-5`** — Wenn eine Station Anrufer ermutigt und einen empfängt, folgt sie dem optionalen Handshake-Protokoll aus A.5.3.4.
- [ ] ⬜ **`AC-SOUND-005-6`** — Wenn eine Station Anrufer ignorieren will, verwendet sie TWAS und kehrt sofort zu "available" zurück.
- [ ] ⬜ **`AC-SOUND-005-7`** — Multikanal-Stationen können periodisch zu einem Single-Channel-Netz senden, um Aktivität und Verfügbarkeit zu melden.
- [ ] ⬜ **`AC-SOUND-005-8`** — Bei Empfang eines Sounds müssen Empfänger die Adresse anzeigen und bei Verfügbarkeit Konnektivitätsinformation speichern.
- [ ] ⬜ **`AC-SOUND-006-1`** — Sounding ist mit dem Scanning-Timing kompatibel.
- [ ] ⬜ **`AC-SOUND-006-2`** — Alle Stationen können Scanning-Sounding-Protokolle durchführen, auch auf fester Frequenz.
- [ ] ⬜ **`AC-SOUND-006-3`** — Die Protokolle stellen einseitige Konnektivität auf jedem verfügbaren, gemeinsam abgetasteten Kanal sicher und bestätigen sie.
- [ ] ⬜ **`AC-SOUND-006-4`** — Stationen verwenden die Protokolle für Multichannel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung.
- [ ] ⬜ **`AC-SOUND-007-1`** — Alle Timing-Betrachtungen und Berechnungen für individuelles Scanning Calling gelten auch für Scanning Sounding.
- [ ] ⬜ **`AC-SOUND-007-2`** — Der Scanning Sound unterscheidet sich vom Single-Channel Sound durch die Erweiterung von Trs durch Tss zu Tsrs.
- [ ] ⬜ **`AC-SOUND-007-3`** — Die Beziehung muss gelten: Tsrs = Tss + Trs.
- [ ] ⬜ **`AC-SOUND-007-4`** — Tss hat den gleichen Zweck wie Tsc für eine äquivalente Scanning-Situation.
- [ ] ⬜ **`AC-SOUND-007-5`** — Kanal-Scanning-Sequenzen und Auswahlkriterien für Scanning Calling gelten auch für Scanning Sounding.
- [ ] ⬜ **`AC-SOUND-007-6`** — Das "Sound Set" für sondierende Kanäle ist üblicherweise identisch mit dem "Scan Set".
- [ ] ⬜ **`AC-SOUND-008-1`** — Bei geplantem Ignorieren von Anrufern muss das Call-Rejection Scanning Sounding Protocol verwendet werden.
- [ ] ⬜ **`AC-SOUND-008-2`** — Die Station landet auf dem ersten Kanal im Scan Set und wartet (Twt), um die Kanalbelegung zu prüfen.
- [ ] ⬜ **`AC-SOUND-008-3`** — Nach dem Abstimmen (Tt) geht die Station auf volle Leistung und initiiert den Scanning Sound Frame.
- [ ] ⬜ **`AC-SOUND-008-4`** — Der Scanning Sound muss den Scan-Perioden der Empfänger-Stationen um mindestens Trs überschreiten, um Tdrw ≥ 784 ms sicherzustellen.
- [ ] ⬜ **`AC-SOUND-008-5`** — "TWAS A" wird redundant verwendet, um mitzuteilen, dass keine Anrufe erwartet werden.
- [ ] ⬜ **`AC-SOUND-008-6`** — Nach dem Scanning Sound Frame verlässt die Station sofort den Kanal und geht zum nächsten Kanal im Sound Set.
- [ ] ⬜ **`AC-SOUND-008-7`** — Das Verfahren wiederholt sich, bis alle Kanäle gesendet oder übersprungen (bei Belegung) wurden.
- [ ] ⬜ **`AC-SOUND-008-8`** — Nach Erschöpfung aller Sound Set Kanäle kehrt die Station automatisch zum "available" receive scan mode zurück.
- [ ] ⬜ **`AC-SOUND-009-1`** — Das Timing von Sounding- und Scanning-Stationen ist vorab vereinbart, um mindestens eine Erfassungsmöglichkeit pro Kanal sicherzustellen.
- [ ] ⬜ **`AC-SOUND-009-2`** — Die scannende Station muss mindestens drei redundante Wörter des Sounding-Senders lesen können.
- [ ] ⬜ **`AC-SOUND-009-3`** — Die scannende Station speichert die Konnektivitätsinformation und setzt das Scanning fort.
- [ ] ⬜ **`AC-SOUND-010-1`** — Bei geplantem Begrüßen von Anrufern verwendet Station "A" seine normale Scanning Dwell Time (Td) oder Twt, whichever länger ist, zum Hören vor dem Sounding.
- [ ] ⬜ **`AC-SOUND-010-2`** — Bei freiem Kanal initiiert "A" den Scanning Sound mit "TIS A".
- [ ] ⬜ **`AC-SOUND-010-3`** — "A" soundet für die gleiche Zeitdauer wie beim Call-Rejection Protocol.
- [ ] ⬜ **`AC-SOUND-010-4`** — Am Ende des Sounding Frame wartet "A" für Anrufe gemäß Twrt (z. B. 6 Tw für schnell-abstimmende Stationen).
- [ ] ⬜ **`AC-SOUND-010-5`** — Während des Wartens hört "A" auf assoziierte und nicht-assozierte Anrufe sowie auf andere Sounds.
- [ ] ⬜ **`AC-SOUND-010-6`** — Gehörte andere Sounds werden als Konnektivitätsinformation gespeichert, wenn die Station polling-fähig ist.
- [ ] ⬜ **`AC-SOUND-010-7`** — Wenn keine Anrufe empfangen wurden, verlässt "A" den Kanal.
- [ ] ⬜ **`AC-SOUND-011-1`** — Ein optionales Handshake kann unmittelbar nach einem Sounding mit einer gerufenen Station durchgeführt werden.
- [ ] ⬜ **`AC-SOUND-011-2`** — Das Handshake-Protokoll ist identisch mit dem Single-Channel Individual Call Protocol, außer dem Trigger-Mechanismus.
- [ ] ⬜ **`AC-SOUND-011-3`** — Das Handshake kann manuell oder automatisch (Bediener oder Controller) durch den Erwerb der Konnektivität vom Sounding getriggert werden.
- [ ] ⬜ **`AC-SOUND-011-4`** — Wenn Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt erforderlich ist, sollte das optionale Handshake verwendet werden.
- [ ] ⬜ **`AC-SOUND-011-5`** — Die rufende Station sollte unverzüglich den Call initiieren, nachdem die gerufene Station ihre Übertragung beendet hat.
- [ ] ⬜ **`AC-SOUND-011-6`** — Eine Wait-Before-Transmit Time ist für das Handshake nach Sounding nicht erforderlich.
- [ ] ⬜ **`AC-SOUND-011-7`** — Ein Empfänger, der den Sound einer gesuchten Station hört, kann unverzüglich mit dem einfachen Single-Channel Call antworten.
- [ ] ⬜ **`AC-SOUND-012-1`** — Sounding verwendet die standardmäßige ALE-Signalisierung.
- [ ] ⬜ **`AC-SOUND-012-2`** — Sounding verwendet die gleichen Frequenzen wie das Scanning.
- [ ] ⬜ **`AC-SOUND-012-3`** — Keine neuen Frequenzen oder speziellen Sounding-Kanäle werden benötigt.
- [ ] ⬜ **`AC-SOUND-012-4`** — Das "Sound Set" ist üblicherweise identisch mit dem "Scan Set".

## 🧪 Tests
- `tests/test_sync.cpp`

## 💡 Implementierungshinweise

Unanimous-Vote-Schwellwert konfigurierbar. Golay-Modus 3/4,2/5,1/6,0/7 wählbar. Präambel+ASCII-Check kombiniert.

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
