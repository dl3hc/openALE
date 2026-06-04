# FEAT-LINK-005 — Adress-Präfix-Matching

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `LINK`

## 📁 Module
- `src/ale_word.cpp`
- `include/ale_word.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-LINK-006 — Adress-Erkennung im Scanning-Call
**Spec:** `A.5.2.5.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Da der Scanning-Call nur das erste Adresswort überträgt, erkennt die Station einen an sie gerichteten Ruf bereits am ersten Wort ihrer eigenen Adresse.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-LINK-006 — Adress-Erkennung im Scanning-Call (`A.5.2.5.1`)
- [ ] ⬜ **`AC-LINK-006-1`** — Eine Station mit mehr als drei Adresszeichen erkennt einen Scanning-Call, der nur ihr erstes Adresswort enthält.

### Weitere Acceptance Criteria
- [ ] ⬜ **`AC-LINK-007-1`** — Der ALE-Controller unterstützt Notfallsteuerung durch den Operator.
- [ ] ⬜ **`AC-LINK-007-2`** — Jeder ALE-Controller bietet manuelle Steuerungsfähigkeit für den Operator.
- [ ] ⬜ **`AC-LINK-007-3`** — In Notfällen kann der Operator direkt das Basis-SSB-Radio steuern.
- [ ] ⬜ **`AC-LINK-007-4`** — Während normaler Betrieb wird das Radio automatisch gesteuert.
- [ ] ⬜ **`AC-LINK-007-5`** — Der Operator betreibt das Radio über seinen zugehörigen Controller.
- [ ] ⬜ **`AC-LINK-007-6`** — Die „always listening“-Fähigkeit des Controllers wird nicht beeinträchtigt.
- [ ] ⬜ **`AC-LINK-007-7`** — Die Anforderung zur manuellen PTT-Bedienung gemäß 4.2.2 bleibt unberührt.
- [ ] ⬜ **`AC-LINK-008-1`** — Das dreifache Handshake-Protokoll ist ausreichend für eine Verbindung.
- [ ] ⬜ **`AC-LINK-008-2`** — Slotted Responses ermöglichen die Verbindung einer Station mit mehreren Stationen.
- [ ] ⬜ **`AC-LINK-008-3`** — Die Handshake-Protokolle sind identisch für Einzel- und Mehr-Station-Verbindungen.
- [ ] ⬜ **`AC-LINK-009-1`** — Das System implementiert alle Timing-Funktionen gemäß Tabelle A-XV.
- [ ] ⬜ **`AC-LINK-009-2`** — Alle Standard-Timing-Werte (Trw = 392 ms, Tw = 130,66… ms, Ts max = 50 s, etc.) werden eingehalten.
- [ ] ⬜ **`AC-LINK-009-3`** — Ein verlängerter Message-Abschnitt (AMD/DTM/DBM) beginnt nicht vor dem Start des 30. Worts (11,368 s).
- [ ] ⬜ **`AC-LINK-009-4`** — Der Message-Abschnitt endet am Ende des Orderwire-Protokolls ohne weitere Verlängerung.
- [ ] ⬜ **`AC-LINK-009-5`** — Die Conclusion beginnt unmittelbar am Ende des Message-Abschnitts.
- [ ] ⬜ **`AC-LINK-010-1`** — Der Controller kann sich in einem von drei Zuständen befinden.
- [ ] ⬜ **`AC-LINK-010-2`** — Die Zustände sind konzeptionell definiert.
- [ ] ⬜ **`AC-LINK-010-3`** — Die Abbildung A-28 zeigt die Zustände.
- [ ] ⬜ **`AC-LINK-011-1`** — Die rufende Station sendet Aufrufe auf gescannten Kanälen in der durch den Kanal-Auswahl-Algorithmus bestimmten Reihenfolge.
- [ ] ⬜ **`AC-LINK-011-2`** — Der Link wird auf dem ersten Kanal hergestellt, der einen erfolgreichen Handshake unterstützt.
- [ ] ⬜ **`AC-LINK-012-1`** — Der Link wird beendet, wenn ein Kanal abgelehnt wird.
- [ ] ⬜ **`AC-LINK-012-2`** — Die LQA-Daten werden aktualisiert.
- [ ] ⬜ **`AC-LINK-012-3`** — Die Messungen stammen aus dem Verknüpfungsprozess.
- [ ] ⬜ **`AC-LINK-013-1`** — Besetzte Kanäle werden übersprungen.
- [ ] ⬜ **`AC-LINK-013-2`** — Störungen werden verhindert.
- [ ] ⬜ **`AC-LINK-013-3`** — Zuvor besetzte Kanäle werden erneut durchsucht.
- [ ] ⬜ **`AC-LINK-013-4`** — Aufruf wird gestartet, wenn Kanäle frei sind.
- [ ] ⬜ **`AC-LINK-014-1`** — Der Link-Versuch wird beendet, wenn alle Kanäle erschöpft sind.
- [ ] ⬜ **`AC-LINK-014-2`** — Der Zustand wechselt zum verfügbaren Scan-Modus.
- [ ] ⬜ **`AC-LINK-014-3`** — Der Operator wird benachrichtigt.
- [ ] ⬜ **`AC-LINK-014-4`** — Der Netzwerk-Controller wird benachrichtigt, falls vorhanden.
- [ ] ⬜ **`AC-LINK-015-1`** — Das Ende eines empfangenen ALE-Signals wird durch Suche nach einer gültigen Conclusion (TIS oder TWAS, gefolgt von max. 5 Wörtern DATA/REP, oder Tx max) identifiziert.
- [ ] ⬜ **`AC-LINK-015-2`** — Die Conclusion muss konstante redundante Wortphase innerhalb sich selbst (bei Sound) und gegenüber den vorangegangenen Wörtern aufrechterhalten.
- [ ] ⬜ **`AC-LINK-015-3`** — Der Controller prüft jede aufeinanderfolgende Trw-Phase nach TIS/TWAS auf das erste (von bis zu vier) nicht lesbaren oder ungültigen Wort(e).
- [ ] ⬜ **`AC-LINK-015-4`** — Fehlendes gültiges Wort, erkanntes ungültiges Wort oder Erkennung des letzten REP, plus Last-Word-Wait-Delay (Tlww = Trw = 392 ms), zeigt das Übertragungsende an.
- [ ] ⬜ **`AC-LINK-015-5`** — Die maximal akzeptable Abschlussfolge ist: TIS (oder TWAS), DATA, REP, DATA, REP.
- [ ] ⬜ **`AC-LINK-016-1`** — Das Protokoll zum Verbindungsaufbau besteht aus genau drei ALE-Frames: Aufruf (Call), Antwort (Response) und Bestätigung (Acknowledgment).
- [ ] ⬜ **`AC-LINK-016-2`** — Kein Link gilt als hergestellt, bevor alle drei Frames erfolgreich ausgetauscht wurden.
- [ ] ⬜ **`AC-LINK-016-3`** — Alle in A.5.5.3.1–A.5.5.3.4 definierten Zeitüberschreitungen (Twr, Twce, Tlww, Tmmax, Txmax) werden eingehalten.
- [ ] ⬜ **`AC-LINK-017-1`** — Die rufende Station hört auf dem Kanal, bevor sie sendet (Listen-Before-Transmit).
- [ ] ⬜ **`AC-LINK-017-2`** — Die rufende Station stimmt den Kanal ab, bevor sie sendet.
- [ ] ⬜ **`AC-LINK-017-3`** — Bei bekanntem Einzelkanal-Hörer: Aufruf enthält nur Leading Call und Conclusion.
- [ ] ⬜ **`AC-LINK-017-4`** — Bei scannender Gegenstation: dem Leading Call ist ein Scanning Call vorangestellt.
- [ ] ⬜ **`AC-LINK-017-5`** — Die Dauer des Scanning Calls beträgt 2 Trw für jeden gescannten Kanal der gerufenen Station.
- [ ] ⬜ **`AC-LINK-017-6`** — Der Scanning-Call-Abschnitt enthält ausschließlich das erste Adresswort der gerufenen Station (TO-Präambel).
- [ ] ⬜ **`AC-LINK-017-7`** — Die vollständige Zieladresse wird im Leading Call zweimal übertragen.
- [ ] ⬜ **`AC-LINK-017-8`** — Bei ausbleibender Antwort innerhalb Twr (Einzelkanal) oder Twrt (Mehrkanal) wird der nächste Kanal versucht oder der Versuch abgebrochen.
- [ ] ⬜ **`AC-LINK-018-1`** — Erkennt die Station ALE-Signale und erreicht Word-Sync, prüft sie das empfangene Wort zur Aktionsbestimmung.
- [ ] ⬜ **`AC-LINK-018-2`** — Bei Empfang von „To JOE“ (oder akzeptabler Entsprechung): Scan stoppen, Linking-Zustand eintreten, ALE-Wörter weiterlesen, auf Ablauf von Twce warten.
- [ ] ⬜ **`AC-LINK-018-3`** — Bei potenziellem Sound oder anderem Protokoll: Wort gemäß diesem Protokoll verarbeiten.
- [ ] ⬜ **`AC-LINK-018-4`** — Andernfalls: in vorherigen Zustand zurückehren (verfügbar oder gelinkt).
- [ ] ⬜ **`AC-LINK-018-5`** — Abbruchbedingung 1: Kein Start von Quick-ID, Message oder Conclusion innerhalb Twce; oder kein Conclusionstart innerhalb Tmmax nach Beginn des Message-Abschnitts.
- [ ] ⬜ **`AC-LINK-018-6`** — Abbruchbedingung 2: Ungültige Wortpräambel-Sequenz empfangen (Ausnahme: bis zu drei aufeinanderfolgende Wörter mit unkorrigierbaren Fehlern im Scanning Call werden toleriert).
- [ ] ⬜ **`AC-LINK-018-7`** — Abbruchbedingung 3: Ende der Conclusion nicht innerhalb Tlww (plus etwaige Trw-Vielfache bei erweiterter Adresse) nach dem ersten Conclusionwort erkannt.
- [ ] ⬜ **`AC-LINK-018-8`** — Bei erfolgreich empfangener TIS-Conclusion: Last-Word-Wait-Timeout Tlww = Trw starten und auf weitere Adresswörter sowie Frameende warten; dann Response auslösen.
- [ ] ⬜ **`AC-LINK-018-9`** — Bei empfangener TWAS-Conclusion: nicht antworten, sofort in vorherigen Zustand zurückehren.
- [ ] ⬜ **`AC-LINK-019-1`** — Die gerufene Station prüft die Kanalbelegung, bevor sie antwortet.
- [ ] ⬜ **`AC-LINK-019-2`** — Bei freiem Kanal: abstimmen, Antwort senden (Figure A-30), Timer Twr starten.
- [ ] ⬜ **`AC-LINK-019-3`** — Bei belegtem Kanal: Aufruf ignorieren, in vorherigen Zustand zurückkehren.
- [ ] ⬜ **`AC-LINK-019-4`** — Twr gilt für Einzelkanal; Twrt gilt, wenn Bestätigung auf anderem Kanal erwartet wird.
- [ ] ⬜ **`AC-LINK-019-5`** — SAM verarbeitet die Response mit denselben Prüfungen und Timeouts wie beim Empfang des Calls.
- [ ] ⬜ **`AC-LINK-019-6`** — SAM-Abbruchbedingung 1: kein „TO SAM" innerhalb Twr/Twrt.
- [ ] ⬜ **`AC-LINK-019-7`** — SAM-Abbruchbedingung 2: ungültige Wortpräambel-Sequenz.
- [ ] ⬜ **`AC-LINK-019-8`** — SAM-Abbruchbedingung 3: keine Conclusion „TIS JOE" innerhalb Tlc (+Tm max).
- [ ] ⬜ **`AC-LINK-019-9`** — SAM-Abbruchbedingung 4: Ende der Conclusion nicht innerhalb Tlww erkannt.
- [ ] ⬜ **`AC-LINK-019-10`** — Bei „TWAS JOE": SAM bricht Verbindungsaufbau ab und informiert den Operator.
- [ ] ⬜ **`AC-LINK-020-1`** — SAM sendet ACK, wechselt in den Linked-Zustand mit JOE, schaltet den Lautsprecher ein und startet Timer Twa (Standard: 30 s).
- [ ] ⬜ **`AC-LINK-020-2`** — SAM-Abbruchbedingung 1: kein passender ACK-Calling-Cycle „To JOE“ innerhalb Twr.
- [ ] ⬜ **`AC-LINK-020-3`** — SAM-Abbruchbedingung 2: ungültige Wortpräambel-Sequenz.
- [ ] ⬜ **`AC-LINK-020-4`** — SAM-Abbruchbedingung 3: keine Conclusion innerhalb Tlc (+Tm max) nach Framestart.
- [ ] ⬜ **`AC-LINK-020-5`** — SAM-Abbruchbedingung 4: Ende der Conclusion nicht innerhalb Tlww erkannt.
- [ ] ⬜ **`AC-LINK-020-6`** — JOE liest ACK mit denselben Prüfungen wie beim Call; bei TIS SAM: Linked-Zustand eintreten, Operator und Network-Controller informieren, Lautsprecher einschalten, Twa starten.
- [ ] ⬜ **`AC-LINK-020-7`** — Bei TWAS SAM im ACK: JOE kehrt in den Pre-Linking-Zustand zurück.
- [ ] ⬜ **`AC-LINK-020-8`** — Kommt SAMs ACK nach Ablauf von JOEs Twr, behandelt JOE es als neuen Einzelruf (nicht als Duplikat).
- [ ] ⬜ **`AC-LINK-021-1`** — Der Link wird durch TWAS beendet.
- [ ] ⬜ **`AC-LINK-021-2`** — Alle verknüpften Stationen werden benachrichtigt.
- [ ] ⬜ **`AC-LINK-021-3`** — Der Lautsprecher wird ausgeschaltet.
- [ ] ⬜ **`AC-LINK-021-4`** — Die Station kehrt in den verfügbaren Zustand zurück.
- [ ] ⬜ **`AC-LINK-021-5`** — Verknüpfungen mit anderen Stationen bleiben erhalten.
- [ ] ⬜ **`AC-LINK-022-1`** — Der Operator kann manuell eine Station zurücksetzen.
- [ ] ⬜ **`AC-LINK-022-2`** — Der Lautsprecher wird stummgeschaltet.
- [ ] ⬜ **`AC-LINK-022-3`** — Der Controller kehrt in den verfügbaren Zustand zurück.
- [ ] ⬜ **`AC-LINK-022-4`** — TWAS wird gesendet.
- [ ] ⬜ **`AC-LINK-022-5`** — Andere Links bleiben erhalten.
- [ ] ⬜ **`AC-LINK-023-1`** — Der Timer Twa wird gestartet.
- [ ] ⬜ **`AC-LINK-023-2`** — Bei Aktivitätszeitüberschreitung wird der Link beendet.
- [ ] ⬜ **`AC-LINK-023-3`** — Der Lautsprecher wird ausgeschaltet.
- [ ] ⬜ **`AC-LINK-023-4`** — Die Station kehrt in den verfügbaren Zustand zurück.
- [ ] ⬜ **`AC-LINK-023-5`** — Der Timer kann vom Operator deaktiviert werden.
- [ ] ⬜ **`AC-LINK-023-6`** — Es wird empfohlen, TWAS zu senden.
- [ ] ⬜ **`AC-LINK-023-7`** — Die Station kehrt in den verfügbaren Zustand zurück.
- [ ] ⬜ **`AC-LINK-024-1`** — Die Kontinuität des Signals kann verloren gehen.
- [ ] ⬜ **`AC-LINK-024-2`** — Unkorrigierbare Fehler werden erkannt.
- [ ] ⬜ **`AC-LINK-024-3`** — Das Wort-Sync wird wiederhergestellt.
- [ ] ⬜ **`AC-LINK-024-4`** — Bei neuer Wortphase wird eine Kollision angezeigt.
- [ ] ⬜ **`AC-LINK-024-5`** — Das unterbrochene Frame wird verworfen.
- [ ] ⬜ **`AC-LINK-024-6`** — Das unterbrechende Signal wird als neuer Frame verarbeitet.
- [ ] ⬜ **`AC-LINK-025-1`** — Eine Station kann gleichzeitig mit mehreren Stationen verknüpfen.
- [ ] ⬜ **`AC-LINK-025-2`** — Die Protokolle sind beschrieben.
- [ ] ⬜ **`AC-LINK-026-1`** — Das Handshake-Protokoll kann nicht für Mehr-Wege-Aufrufe verwendet werden.
- [ ] ⬜ **`AC-LINK-026-2`** — Ein TDMA-Schema wird verwendet.
- [ ] ⬜ **`AC-LINK-026-3`** — Jede Station sendet ihre Antwort in einem Zeitfenster.
- [ ] ⬜ **`AC-LINK-026-4`** — Die WRTT wird gesetzt.
- [ ] ⬜ **`AC-LINK-026-5`** — Die WRTTs der gerufenen Stationen werden begrenzt.
- [ ] ⬜ **`AC-LINK-026-6`** — Die Wartezeit wird auf Twan = Twrn + 2 Trw gesetzt.
- [ ] ⬜ **`AC-LINK-026-7`** — Jede Station setzt einen Slot-Warte-Timer.
- [ ] ⬜ **`AC-LINK-026-8`** — Die Stationen tunen während des Slots 0.
- [ ] ⬜ **`AC-LINK-026-9`** — Bei Ablauf des Slot-Warte-Timers wird die Antwort gesendet.
- [ ] ⬜ **`AC-LINK-026-10`** — Bei Ablauf des WRTT-Timers wird der Verbindungsaufbau abgebrochen.
- [ ] ⬜ **`AC-LINK-027-1`** — Slotted Response Frames sind identisch mit Einzelruf-Antworten.
- [ ] ⬜ **`AC-LINK-027-2`** — Die Frames enthalten führenden Aufruf, optionalen Nachrichtenabschnitt und Frame-Schlussfolge.
- [ ] ⬜ **`AC-LINK-027-3`** — Die Antwort wird mit TIS oder TWAS abgeschlossen.
- [ ] ⬜ **`AC-LINK-027-4`** — Bei einwortiger Adresse sind die Slots 14 Tw.
- [ ] ⬜ **`AC-LINK-028-1`** — Alle Slots sind 14 Tw lang, sofern nicht anders angegeben.
- [ ] ⬜ **`AC-LINK-028-2`** — Die Slots ermöglichen Durchquerung der Welt.
- [ ] ⬜ **`AC-LINK-028-3`** — Erweiterte Slots werden entsprechend verzögert.
- [ ] ⬜ **`AC-LINK-028-4`** — Die aufrufende Stationsadresse erweitert Slots.
- [ ] ⬜ **`AC-LINK-028-5`** — Die gerufene Stationsadresse erweitert Slots.
- [ ] ⬜ **`AC-LINK-028-6`** — Nachrichtenabschnitt erweitert Slots.
- [ ] ⬜ **`AC-LINK-029-1`** — Die Formel zur Bestimmung der Zeit für slotted Responses ist definiert.
- [ ] ⬜ **`AC-LINK-029-2`** — Die Slotnummer SN wird berücksichtigt.
- [ ] ⬜ **`AC-LINK-029-3`** — Die Formel berücksichtigt die Adresslänge der aufrufenden Station.
- [ ] ⬜ **`AC-LINK-029-4`** — Die Formel berücksichtigt den optionalen Nachrichtenabschnitt.
- [ ] ⬜ **`AC-LINK-029-5`** — Die Formel berücksichtigt die Adresslänge der gerufenen Station.
- [ ] ⬜ **`AC-LINK-029-6`** — Die Formel für Twrn ist definiert.
- [ ] ⬜ **`AC-LINK-029-7`** — Die Formel für Twan ist definiert.
- [ ] ⬜ **`AC-LINK-030-1`** — Das Beispiel ist in Abb. A-33 gezeigt.
- [ ] ⬜ **`AC-LINK-030-2`** — Die Beispieldarstellung ist korrekt.
- [ ] ⬜ **`AC-LINK-031-1`** — Eine Net-Adresse wird einem Satz von Net-Mitglied-Stationen zugewiesen.
- [ ] ⬜ **`AC-LINK-031-2`** — Die Slotnummer und Adresse sind vorab bekannt.
- [ ] ⬜ **`AC-LINK-031-3`** — Die Informationen sind für alle Net-Mitglieder verfügbar.
- [ ] ⬜ **`AC-LINK-032-1`** — Ein Star-Net-Aufruf ist identisch mit einem Einzelruf.
- [ ] ⬜ **`AC-LINK-032-2`** — Die gerufene Stationsadresse ist eine Net-Adresse.
- [ ] ⬜ **`AC-LINK-032-3`** — Die aufrufende Stationsadresse ist individuell.
- [ ] ⬜ **`AC-LINK-033-1`** — Die Station empfängt einen Aufruf an eine Net-Adresse.
- [ ] ⬜ **`AC-LINK-033-2`** — Die Station verarbeitet den Aufruf mit Einzelruf-Prüfungen.
- [ ] ⬜ **`AC-LINK-033-3`** — Die Station antwortet gemäß A.5.5.4.1.
- [ ] ⬜ **`AC-LINK-033-4`** — Die Antwort verwendet die zugewiesene Net-Mitglied-Adresse und Slotnummer.
- [ ] ⬜ **`AC-LINK-034-1`** — Das Bestätigungsprotokoll ist identisch mit Einzelruf-Bestätigung.
- [ ] ⬜ **`AC-LINK-034-2`** — Die gerufene Stationsadresse ist eine Net-Adresse.
- [ ] ⬜ **`AC-LINK-034-3`** — Der Wartezeit-Timer Twan wird verwendet.
- [ ] ⬜ **`AC-LINK-034-4`** — TWAS kehrt die Station in ihren vorherigen Zustand zurück.
- [ ] ⬜ **`AC-LINK-034-5`** — TIS wechselt in den verknüpften Zustand.
- [ ] ⬜ **`AC-LINK-034-6`** — Der Operator wird benachrichtigt.
- [ ] ⬜ **`AC-LINK-034-7`** — Der Lautsprecher wird eingeschaltet.
- [ ] ⬜ **`AC-LINK-034-8`** — Der Timer Twa wird gesetzt.
- [ ] ⬜ **`AC-LINK-035-1`** — Das Gruppenrufprotokoll erweitert Mehr-Wege-Rufen.
- [ ] ⬜ **`AC-LINK-035-2`** — Die Gruppe ist ad-hoc.
- [ ] ⬜ **`AC-LINK-035-3`** — Es muss nichts über die Stationen außer ihren Adressen und Frequenzen bekannt sein.
- [ ] ⬜ **`AC-LINK-035-4`** — Gruppenmitgliedschaft ist begrenzt.
- [ ] ⬜ **`AC-LINK-035-5`** — Die Gesamtlänge darf 12 Wörter nicht überschreiten.
- [ ] ⬜ **`AC-LINK-035-6`** — Die Menge der einzigartigen ersten Adresswörter darf fünf Wörter nicht überschreiten.
- [ ] ⬜ **`AC-LINK-036-1`** — Eine Gruppenadresse wird durch Kombination individueller Adressen erzeugt.
- [ ] ⬜ **`AC-LINK-036-2`** — Beim Scanning-Aufruf werden nur die ersten Wörter gesendet.
- [ ] ⬜ **`AC-LINK-036-3`** — Die ersten Adresswörter werden in Rotation gesendet.
- [ ] ⬜ **`AC-LINK-036-4`** — Die Adresswörter wechseln zwischen THRU und REP.
- [ ] ⬜ **`AC-LINK-036-5`** — Die Adresswörter sind in Abb. A-35 gezeigt.
- [ ] ⬜ **`AC-LINK-037-1`** — Die vollständigen Adressen werden gesendet.
- [ ] ⬜ **`AC-LINK-037-2`** — Die Adressen werden mit TO-Preambles gesendet.
- [ ] ⬜ **`AC-LINK-037-3`** — Bis zu 12 Adresswörter sind erlaubt.
- [ ] ⬜ **`AC-LINK-037-4`** — Tlc dauert bis zu 24 Trw.
- [ ] ⬜ **`AC-LINK-037-5`** — TO-Wörter werden korrekt behandelt.
- [ ] ⬜ **`AC-LINK-038-1`** — Der Nachrichtenabschnitt ist optional.
- [ ] ⬜ **`AC-LINK-038-2`** — Die Schlussfolge folgt A.5.2.5.3.
- [ ] ⬜ **`AC-LINK-038-3`** — Die Vorgaben sind korrekt.
- [ ] ⬜ **`AC-LINK-039-1`** — Slots werden abgeleitet.
- [ ] ⬜ **`AC-LINK-039-2`** — Die Station liest THRU- oder REP-Preambles.
- [ ] ⬜ **`AC-LINK-039-3`** — Die Station prüft auf Übereinstimmung.
- [ ] ⬜ **`AC-LINK-039-4`** — Die Station kehrt in den verfügbaren Zustand zurück.
- [ ] ⬜ **`AC-LINK-039-5`** — Tlc wird geprüft.
- [ ] ⬜ **`AC-LINK-039-6`** — Der Slot-Zähler wird gesetzt und inkrementiert.
- [ ] ⬜ **`AC-LINK-039-7`** — Die Nachrichtenabschnitt und Schlussfolge werden verarbeitet.
- [ ] ⬜ **`AC-LINK-039-8`** — Bei spätem Eintreffen wird ein Standardwert verwendet.
- [ ] ⬜ **`AC-LINK-040-1`** — Slotted Responses werden gemäß A.5.5.4.1 gesendet.
- [ ] ⬜ **`AC-LINK-040-2`** — Slotted Responses werden überprüft.
- [ ] ⬜ **`AC-LINK-040-3`** — Die abgeleiteten Slot-Nummern werden verwendet.
- [ ] ⬜ **`AC-LINK-040-4`** — Die eigene Adresse wird verwendet.
- [ ] ⬜ **`AC-LINK-041-1`** — Die Bestätigung ist an Subset gerichtet.
- [ ] ⬜ **`AC-LINK-041-2`** — Die führende Aufruf-Adresse wird zweimal gesendet.
- [ ] ⬜ **`AC-LINK-041-3`** — Die Bestätigung wird verarbeitet gemäß A.5.5.3.4.
- [ ] ⬜ **`AC-LINK-041-4`** — Der Twan-Wert wird verwendet.
- [ ] ⬜ **`AC-LINK-041-5`** — Selbstadressen werden durchsucht.
- [ ] ⬜ **`AC-LINK-041-6`** — TWAS kehrt die Station in ihren vorherigen Zustand zurück.
- [ ] ⬜ **`AC-LINK-041-7`** — TIS wechselt in den verknüpften Zustand.
- [ ] ⬜ **`AC-LINK-041-8`** — Der Operator wird benachrichtigt.
- [ ] ⬜ **`AC-LINK-041-9`** — Der Lautsprecher wird eingeschaltet.
- [ ] ⬜ **`AC-LINK-041-10`** — Der Timer Twa wird gesetzt.
- [ ] ⬜ **`AC-LINK-042-1`** — Die Beispielgruppe ist in Abb. A-35 gezeigt.
- [ ] ⬜ **`AC-LINK-042-2`** — Die Slot-Zeiten sind korrekt.
- [ ] ⬜ **`AC-LINK-042-3`** — Die Bestätigung wird korrekt gesendet.
- [ ] ⬜ **`AC-LINK-043-1`** — Eine Station antwortet auf mehrere Adressen.
- [ ] ⬜ **`AC-LINK-043-2`** — Die Tatsache, dass die Station mehrere Adressen hat, ist dem Aufrufer nicht bekannt.
- [ ] ⬜ **`AC-LINK-043-3`** — Konflikte können nach erfolgreicher Verknüpfung aufgelöst werden.
- [ ] ⬜ **`AC-LINK-044-1`** — Ein AllCall fordert Stationen auf, zu hören.
- [ ] ⬜ **`AC-LINK-044-2`** — Ein AllCall fordert keine Antworten an.
- [ ] ⬜ **`AC-LINK-044-3`** — Die AllCall-Adresse wird nur in TO-Wörtern verwendet.
- [ ] ⬜ **`AC-LINK-044-4`** — Selektive AllCalls verwenden Gruppenadressierung.
- [ ] ⬜ **`AC-LINK-044-5`** — Die Station wird identifiziert.
- [ ] ⬜ **`AC-LINK-044-6`** — Die Pause wird gestartet.
- [ ] ⬜ **`AC-LINK-044-7`** — Bei Quick-ID wird die Pause erweitert.
- [ ] ⬜ **`AC-LINK-044-8`** — Bei Nachricht wird eine Zeit für das Lesen verwendet.
- [ ] ⬜ **`AC-LINK-044-9`** — Bei Schlussfolge wird eine Zeit für das Lesen verwendet.
- [ ] ⬜ **`AC-LINK-044-10`** — Bei TIS wird der Zustand gewechselt.
- [ ] ⬜ **`AC-LINK-044-11`** — Bei TWAS kehrt die Station zum Scanning zurück.
- [ ] ⬜ **`AC-LINK-044-12`** — Der Operator kann einen Handshake initiieren.
- [ ] ⬜ **`AC-LINK-044-13`** — AllCall-Adresse wird nicht verwendet.
- [ ] ⬜ **`AC-LINK-044-14`** — Controller können AllCalls ignorieren.
- [ ] ⬜ **`AC-LINK-044-15`** — AllCall-Verarbeitung sollte aktiviert sein.
- [ ] ⬜ **`AC-LINK-045-1`** — Ein AnyCall fordert Antworten an.
- [ ] ⬜ **`AC-LINK-045-2`** — Die Verwendung der AnyCall-Adresse ist identisch mit AllCall.
- [ ] ⬜ **`AC-LINK-045-3`** — Die Station prüft den Aufruf wie bei AllCall.
- [ ] ⬜ **`AC-LINK-045-4`** — Bei TIS wird ein Link-Zustand gewechselt.
- [ ] ⬜ **`AC-LINK-045-5`** — Die Station generiert eine slotted Antwort.
- [ ] ⬜ **`AC-LINK-045-6`** — Die Station wählt zufällig eine Slot-Nummer.
- [ ] ⬜ **`AC-LINK-045-7`** — Die Slot-Breite wird berechnet.
- [ ] ⬜ **`AC-LINK-045-8`** — Slot 0 wird für das Tunen verwendet.
- [ ] ⬜ **`AC-LINK-045-9`** — Die Antwort wird gesendet.
- [ ] ⬜ **`AC-LINK-045-10`** — Kollisionen werden erwartet und toleriert.
- [ ] ⬜ **`AC-LINK-045-11`** — Die aufrufende Station liest die beste Antwort.
- [ ] ⬜ **`AC-LINK-045-12`** — Die aufrufende Station sendet eine ACK.
- [ ] ⬜ **`AC-LINK-045-13`** — Die AnyCall-Adresse wird nicht verwendet.
- [ ] ⬜ **`AC-LINK-045-14`** — Controller können AnyCalls ignorieren.
- [ ] ⬜ **`AC-LINK-045-15`** — AnyCall-Verarbeitung sollte aktiviert sein.
- [ ] ⬜ **`AC-LINK-046-1`** — Wildcard-Adressen sind die einzigen Mitglieder des Aufrufs.
- [ ] ⬜ **`AC-LINK-046-2`** — Wildcard-Adressen werden nicht in anderen Adressfeldern verwendet.
- [ ] ⬜ **`AC-LINK-046-3`** — Der Umfang der Wildcards ist begrenzt.
- [ ] ⬜ **`AC-LINK-046-4`** — Aufrufe mit TWAS werden wie AllCall verarbeitet.
- [ ] ⬜ **`AC-LINK-046-5`** — Antworten mit TIS werden in pseudozufälligen Slots gesendet.
- [ ] ⬜ **`AC-LINK-046-6`** — Die Verarbeitung kann ignoriert werden.
- [ ] ⬜ **`AC-LINK-046-7`** — Wildcard-Aufruf-Verarbeitung sollte aktiviert sein.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

matches_address_prefix(): trailing '@' abstreifen, dann startswith(prefix). W1AWJ erkennt W1A.

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
