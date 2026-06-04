# PC-ALE Requirements Specification

**Basis-Standard:** MIL-STD-188-141B, Appendix A
**Projekt:** PC-ALE / PC-ALE-Win
**Status:** Entwurf
**Letzte Änderung:** _(Datum eintragen)_

---

## Über dieses Dokument

Dieses Dokument beschreibt **was** das System leisten muss und **warum** — ausdrücklich **nicht wie**. Es ist lösungsneutral: keine Datentypen, keine Bit-Offsets, keine Funktionsnamen, keine Sprachkonstrukte. Diese gehören in die nachgelagerten Ebenen (Features, Tasks, Code), die über IDs auf die Anforderungen zurückverweisen.

### Die vier Ebenen und ihre Trennung

```
EPIC          große fachliche Klammer (z. B. "Outbound Calling")
  │
  ▼
USER STORY    fachlicher Bedarf aus Nutzer-/Systemsicht
  │           "Als rufende Station will ich …, damit …"
  ▼
REQUIREMENT   prüfbare, lösungsneutrale Einzelanforderung (REQ-xxx)
  │           verweist auf Spec-Referenz
  ▼
ACCEPTANCE    testbares Kriterium (AC) — Grundlage der Test-Cases
```

Implementierungsdetails (z. B. „Preamble liegt in den drei höchstwertigen Bits eines 24-Bit-Worts") gehören **nicht hierher**. Sie stehen im **Feature-/Design-Dokument** und tragen einen Rückverweis wie `implements: REQ-WORD-001`.

### Was hier NICHT steht

| Gehört nicht ins Requirement | Gehört in … |
|---|---|
| Datentypen (uint32, array) | Feature/Design-Dokument |
| Bit-Offsets, Speicherlayout | Feature/Design-Dokument |
| Funktions-/Klassennamen | Code + Code-Kommentare |
| Algorithmus-Wahl (FFT vs. Goertzel) | Feature/Design-Dokument |
| Konkrete Schwellwerte ohne Spec-Grundlage | Feature/Design-Dokument |

### Was hier sehr wohl steht

Konkrete Werte, die **der Standard selbst** vorgibt (z. B. „Trw = 392 ms", „maximal 15 Adresszeichen", „Tlc = 2 × Tc"), sind **fachliche Anforderungen**, keine Implementierungsdetails. Sie stehen hier.

### ID-Schema und Traceability

- **Epics:** `EPIC-<Bereich>` (z. B. `EPIC-LINK`)
- **User Stories:** `US-<Bereich>-<nnn>`
- **Requirements:** `REQ-<Bereich>-<nnn>`
- **Acceptance Criteria:** `AC-<Bereich>-<nnn>-<n>` (an die Requirement gebunden)

| Bereich | Thema | Spec-Abschnitt |
|---|---|---|
| `GEN` | Allgemeine Anforderungen (General Requirements) | A.4 |
| `WAVEFORM` | ALE modem waveform (Töne, Timing, Genauigkeit) | A.5.1 |
| `FEC` | Golay, Interleaving, Redundant words | A.5.2.2 |
| `WORD` | Word structures, Preambles, Word types | A.5.2.3 |
| `ADDR` | Addressing (Basic 38, Stuffing, Adresstypen, Wildcards) | A.5.2.4 |
| `FRAME` | Frame structure (Calling cycle, Message, Conclusion) | A.5.2.5 |
| `SYNC` | Synchronization (Word phase, Receiver sync) | A.5.2.6 |
| `SOUND` | Sounding | A.5.3 |
| `CHAN` | Channel selection, LQA, Listen before transmit | A.5.4 |
| `LINK` | Link establishment protocols | A.5.5 |
| `CMD` | ALE control functions (CMDs) | A.5.6 |
| `MSG` | ALE message protocols (AMD, DTM, DBM) | A.5.7 |
| `AQC` | AQC (optional) | A.5.8 |

**Rückverfolgbarkeit:** Jede Requirement nennt ihre Spec-Referenz. Jede User Story listet ihre Requirements. Jedes Feature (separates Dokument) nennt die Requirement(s), die es umsetzt. Jeder Test-Case nennt das Acceptance Criterion, das er prüft. Damit ist die Kette **Spec → Requirement → Feature → Code → Test** lückenlos.

### Priorisierung (MoSCoW)

`MUST` · `SHOULD` · `COULD` · `WON'T (this release)`

### Status

`offen` · `in Arbeit` · `umgesetzt` · `verifiziert`

---

## Inhaltsverzeichnis

1. [Epic-Übersicht](#1-epic-übersicht)
2. [General Requirements — A.4](#2-general-requirements--a4)
   1. [ALE Introduction — A.4.1](#21-ale-introduction--a41)
      - REQ-GEN-001 — ALE Data Link: Schichtstruktur und Sublayer
      - REQ-GEN-002 — ALE-Adressen: Adressierungsstruktur
      - REQ-GEN-003 — Scanning: Wiederholtes Durchlaufen gespeicherter Kanäle
      - REQ-GEN-004 — Calling: Ausführung des Rufprotokolls auf Anforderung
      - REQ-GEN-005 — Kanalbewertung und Kanalqualitätsanzeige
   2. [System Performance Requirements — A.4.2](#22-system-performance-requirements--a42)
      - REQ-GEN-006 — Scan-Rate: Selektierbare Werte 2 und 5 Kanäle/s
      - REQ-GEN-007 — AQC-ALE Scan-Rate: Variable Verweilzeiten (NT)
      - REQ-GEN-008 — AQC-ALE Rückwärtskompatibilität: Scan-Raten 2 und 5 Kanäle/s
      - REQ-GEN-009 — Belegtheitserkennung (NT): Mindestwahrscheinlichkeiten gemäß Tabelle A-I
      - REQ-GEN-010 — Linking-Wahrscheinlichkeit: Mindestanforderungen gemäß Tabelle A-II
      - REQ-GEN-011 — AQC-ALE Linking-Wahrscheinlichkeit (NT)
      - REQ-GEN-012 — AQC-ALE Linking-Leistung bei LP-Level 1 und 2
   3. [Required Data Structures — A.4.3](#23-required-data-structures--a43)
      - REQ-GEN-013 — Kanalspeicher: Mindestens 100 Kanaleinträge, nichtflüchtig
      - REQ-GEN-014 — Selbstadressspeicher: Mindestens 20 Einträge, nichtflüchtig
      - REQ-GEN-015 — Fremdstations-Tabelle: Mindestens 100 Einträge
      - REQ-GEN-016 — Fremdstations-Adressspeicher: Individuelle Einträge und Netzinformationen, nichtflüchtig
      - REQ-GEN-017 — LQA-Speicher: Mindestens 4000 Einträge, 1 Stunde Pufferung
      - REQ-GEN-018 — Fremdstations-Einstellungen: Nichtflüchtige Speicherung (DO)
      - REQ-GEN-019 — Betriebsparameter: Programmierbarkeit durch Operator oder Controller
      - REQ-GEN-020 — Nachrichtenspeicher: Mindestens 12 Nachrichten / 1000 Zeichen, 1 Stunde Pufferung
   4. [ALE Operational Rules — A.4.4](#24-ale-operational-rules--a44)
      - REQ-GEN-021 — ALE-Betriebsregeln gemäß Tabelle A-V (Prioritätsreihenfolge)
   5. [Alternate Quick Call ALE (AQC-ALE) — A.4.5](#25-alternate-quick-call-ale-aqc-ale--a45)
      - REQ-GEN-022 — AQC-ALE: Einführung und Grundprinzip (NT)
      - REQ-GEN-023 — AQC-ALE: Allgemeine Signalisierungsstrategien
      - REQ-GEN-024 — AQC-ALE: Unterstützte Funktionen
      - REQ-GEN-025 — AQC-ALE: Nicht unterstützte Funktionen
3. [ALE Modem Waveform — A.5.1](#3-ale-modem-waveform--a51)
4. [Word Structures — A.5.2.3](#4-word-structures--a523)
   1. [ALE word format — A.5.2.3.1](#31-ale-word-format--a5231)
   3. [Word types and preambles — A.5.2.3.1.2 / A.5.2.3.1.3](#32-word-types-and-preambles--a52312--a52313)
   4. [Address words — A.5.2.3.2](#33-address-words--a5232)
   5. [Message words — A.5.2.3.3](#34-message-words--a5233)
   6. [Extension words — A.5.2.3.4](#35-extension-words--a5234)
5. [Fehlerkorrektur (FEC) — A.5.2.2](#5-fehlerkorrektur-fec--a522)
6. [Frame-Struktur — A.5.2.5](#6-frame-struktur--a525)
   1. [Frame-Grundstruktur — A.5.2.5](#51-frame-grundstruktur--a525)
   3. [Calling Cycle — A.5.2.5.1](#52-calling-cycle--a5251)
      - REQ-FRAME-002 — Calling Cycle: Gliederung und Bestandteile
      - REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt
      - REQ-FRAME-004 — Leading Call: Zusammensetzung und Adressinhalt
      - REQ-FRAME-005 — Frame-Synchronisation: Kein dediziertes Sync-Word
      - REQ-FRAME-006 — Sender-Hochlaufzeit: 90-Prozent-Leistung
   4. [Message-Abschnitt — A.5.2.5.2](#53-message-abschnitt--a5252)
      - REQ-FRAME-007 — Quick-ID: Optionaler Transmitter-Identifier
      - REQ-FRAME-008 — Message-Abschnitt: Struktur und Zusammensetzung
      - REQ-FRAME-009 — Message-Abschnitt: AQC-ALE-Sonderregel
   5. [Conclusion — A.5.2.5.3](#54-conclusion--a5253)
      - REQ-FRAME-010 — Conclusion: Struktur und Worttypen
      - REQ-FRAME-011 — Conclusion bei Sounds und Ausnahme-Frames
   6. [Gültige Sequenzen und Frame-Limits — A.5.2.5.4](#55-gültige-sequenzen-und-frame-limits--a5254)
      - REQ-FRAME-012 — Gültige Wortsequenzen im Frame
      - REQ-FRAME-013 — Frame-Größen- und Zeitgrenzen (Table A-XII)
7. [Synchronization — A.5.2.6](#7-synchronization--a526)
   1. [Systemcharakter der Synchronisation — A.5.2.6](#61-systemcharakter-der-synchronisation--a526)
      - REQ-SYNC-001 — Asynchroner Systembetrieb und Frame-interne Word-Sync
   3. [Sendeseitige Wortphase — A.5.2.6.1](#62-sendeseitige-wortphase--a5261)
      - REQ-SYNC-002 — Konstante Wortphasenbeziehung im Sendemodulator
      - REQ-SYNC-003 — Word-Phase-Tracking nur innerhalb einer Übertragung
      - REQ-SYNC-004 — Unabhängigkeit des Sendemodulators vom Empfänger
   4. [Empfangsseitige Wortsynchronisation — A.5.2.6.2](#63-empfangsseitige-wortsynchronisation--a5262)
      - REQ-SYNC-005 — Empfangsdemodulator: Signalakquisition, Tracking und Demodulation
   5. [Synchronisationskriterien — A.5.2.6.3](#64-synchronisationskriterien--a5263)
      - REQ-SYNC-006 — Synchronisationskriterien für jedes ALE-Wort
      - REQ-SYNC-007 — Herstelleroptimierbare Parameter: Unanimous-Vote-Schwellwert und Golay-Modus
8. [Sounding — A.5.3](#8-sounding--a53)
   1. [Sounding: Einseitige, periodische Übertragung auf unbesetzten Kanälen — A.5.3.1](#71-sounding-einseitige-periodische-übertragung-auf-unbesetzten-kanälen--a531)
   3. [Sounding-Struktur: Identisch zum Basic Call, ohne Calling Cycle und Message — A.5.3.1](#72-sounding-struktur-identisch-zum-basic-call-ohne-calling-cycle-und-message--a531)
   4. [Minimale redundante Sounding-Zeit (Trs) — A.5.3.1](#73-minimale-redundante-sounding-zeit-trs--a531)
   5. [Single-Channel Sounding-Protokoll — A.5.3.2](#74-single-channel-sounding-protokoll--a532)
   6. [Multichannel Sounding: Kompatibilität mit Scanning — A.5.3.3](#75-multichannel-sounding-kompatibilität-mit-scanning--a533)
   7. [Multichannel Sounding: Timing-Berechnung für Scanning Sound Frame — A.5.3.3](#76-multichannel-sounding-timing-berechnung-für-scanning-sound-frame--a533)
   8. [Call-Rejection Scanning Sounding Protocol — A.5.3.3](#77-call-rejection-scanning-sounding-protocol--a533)
   9. [Scanning Detection: Mindestens eine Erfassungsmöglichkeit pro Kanal — A.5.3.3](#78-scanning-detection-mindestens-eine-erfassungsmöglichkeit-pro-kanal--a533)
   10. [Call-Acceptance Scanning Sounding Protocol — A.5.3.3](#79-call-acceptance-scanning-sounding-protocol--a533)
   11. [Optionales Handshake: Getriggert durch Konnektivität vom Sounding — A.5.3.4](#710-optionales-handshake-getriggert-durch-konnektivität-vom-sounding--a534)
   12. [Sounding: Keine neuen Frequenz- oder Hardware-Erfordernisse — A.5.3.1 / A.5.3.3](#711-sounding-keine-neuen-frequenz-oder-hardware-erfordernisse--a531--a533)
9. [Channel Selection — A.5.4](#9-channel-selection--a54)
   0. [Channel Selection: Grundprinzip und LQA-Basis — A.5.4](#80-channel-selection-grundprinzip-und-lqa-basis--a54)
   1. [LQA — A.5.4.1](#81-lqa--a541)
   3. [BER — A.5.4.1.1](#82-ber--a5411)
   4. [SINAD — A.5.4.1.2](#83-sinad--a5412)
   5. [MP (optional) — A.5.4.1.3](#84-mp-optional--a5413)
   6. [Operator display — A.5.4.1.4](#85-operator-display--a5414)
   7. [Current channel quality report (LQA CMD) — A.5.4.2](#86-current-channel-quality-report-lqa-cmd--a542)
   8. [BER field in LQA CMD — A.5.4.2.1](#87-ber-field-in-lqa-cmd--a5421)
   9. [SINAD field in LQA CMD — A.5.4.2.2](#88-sinad-field-in-lqa-cmd--a5422)
   10. [MP field in LQA CMD — A.5.4.2.3](#89-mp-field-in-lqa-cmd--a5423)
   11. [Local Noise Report CMD (optional) — A.5.4.4](#810-local-noise-report-cmd-optional--a544)
   12. [Single-station channel selection — A.5.4.5](#811-single-station-channel-selection--a545)
   13. [Single-station: Link establishment — A.5.4.5.1](#812-single-station-link-establishment--a5451)
   14. [Single-station: One-way broadcast — A.5.4.5.2](#813-single-station-one-way-broadcast--a5452)
   15. [Single-station: Listening — A.5.4.5.3](#814-single-station-listening--a5453)
   15. [Multiple-station channel selection — A.5.4.6](#815-multiple-station-channel-selection--a546)
   16. [Listen before transmit — A.5.4.7](#816-listen-before-transmit--a547)
   17. [Listen-Before-Transmit duration — A.5.4.7.1](#817-listen-before-transmit-duration--a5471)
   18. [Modulations to be detected — A.5.4.7.2](#818-modulations-to-be-detected--a5472)
   19. [Listen before transmit override — A.5.4.7.3](#819-listen-before-transmit-override--a5473)
10. [Link Establishment — A.5.5](#10-link-establishment--a55)
   1. [Individual Call — A.5.5.3.1](#91-individual-call--a5531)
   3. [Net Call — A.5.5](#92-net-call--a55)
   4. [Group Call — A.5.5](#93-group-call--a55)
   5. [Empfang und Dekodierung — A.5.5.2 / A.5.5.3](#94-empfang-und-dekodierung--a552--a553)
   6. [Manual Operation — A.5.5.1](#95-manual-operation--a551)
   7. [ALE Grundlagen — A.5.5.2](#96-ale-grundlagen--a552)
   8. [Timing — A.5.5.2.1](#97-timing--a5521)
   9. [ALE States — A.5.5.2.2](#98-ale-states--a5522)
   10. [Channel Selection — A.5.5.2.3](#99-channel-selection--a5523)
   11. [Channel Rejection — A.5.5.2.3.1](#910-channel-selection--a55231)
   12. [Busy Channel — A.5.5.2.3.2](#911-channel-selection--a55232)
   13. [Exhausted Channel List — A.5.5.2.3.3](#912-channel-selection--a55233)
   14. [End of Frame Detection — A.5.5.2.4](#913-end-of-frame-detection--a5524)
   15. [One-to-One Calling — A.5.5.3](#914-one-to-one-calling--a553)
   15. [Sending an Individual Call — A.5.5.3.1](#915-one-to-one-calling--a5531)
   16. [Receiving an Individual Call — A.5.5.3.2](#916-one-to-one-calling--a5532)
   17. [Response — A.5.5.3.3](#917-one-to-one-calling--a5533)
   18. [Acknowledgment — A.5.5.3.4](#918-one-to-one-calling--a5534)
   19. [Link Termination — A.5.5.3.5](#919-one-to-one-calling--a5535)
   20. [Manual Termination — A.5.5.3.5.1](#920-one-to-one-calling--a55351)
   21. [Automatic Termination — A.5.5.3.5.2](#921-one-to-one-calling--a55352)
   22. [Collision Detection — A.5.5.3.6](#922-one-to-one-calling--a5536)
   23. [One-to-Many Calling — A.5.5.4](#923-one-to-many-calling--a554)
   24. [Slotted Responses — A.5.5.4.1](#924-one-to-many-calling--a5541)
   25. [Slotted Response Frames — A.5.5.4.1.1](#925-one-to-many-calling--a55411)
   26. [Slot Widths — A.5.5.4.1.2](#926-one-to-many-calling--a55412)
   27. [Slot Wait Time Formula — A.5.5.4.1.3](#927-one-to-many-calling--a55413)
   28. [Slotted Response Example — A.5.5.4.1.4](#928-one-to-many-calling--a55414)
   29. [Star Net Calling Protocol — A.5.5.4.2](#929-one-to-many-calling--a5542)
   30. [Star Net Call — A.5.5.4.2.1](#930-one-to-many-calling--a55421)
   31. [Star Net Response — A.5.5.4.2.2](#931-one-to-many-calling--a55422)
   32. [Star Net Acknowledgment — A.5.5.4.2.3](#932-one-to-many-calling--a55423)
   33. [Star Group Calling Protocol — A.5.5.4.3](#933-one-to-many-calling--a5543)
   34. [Star Group Scanning Call — A.5.5.4.3.1](#934-one-to-many-calling--a55431)
   35. [Star Group Leading Call — A.5.5.4.3.2](#935-one-to-many-calling--a55432)
   36. [Star Group Call Conclusion — A.5.5.4.3.3](#936-one-to-many-calling--a55433)
   37. [Receiving a Star Group Call — A.5.5.4.3.4](#937-one-to-many-calling--a55434)
   38. [Star Group Slotted Responses — A.5.5.4.3.5](#938-one-to-many-calling--a55435)
   39. [Star Group Acknowledgment — A.5.5.4.3.6](#939-one-to-many-calling--a55436)
   40. [Star Group Call Example — A.5.5.4.3.7](#940-one-to-many-calling--a55437)
   41. [Multiple Self Addresses in Group Call — A.5.5.4.3.8](#941-one-to-many-calling--a55438)
   42. [AllCall Protocol — A.5.5.4.4](#942-one-to-many-calling--a5544)
   43. [AnyCall Protocol — A.5.5.4.5](#943-one-to-many-calling--a5545)
   44. [Wildcard Calling Protocol — A.5.5.4.6](#944-one-to-many-calling--a5546)
11. [Adressierung — A.5.2.4](#11-adressierung--a524)
    1. [Einführung — A.5.2.4.1](#101-einführung--a5241)
    3. [Basic 38 ASCII subset — A.5.2.4.2](#102-basic-38-ascii-subset--a5242)
    4. [Stuffing — A.5.2.4.3](#103-stuffing--a5243)
    5. [Individual addresses — A.5.2.4.4](#104-individual-addresses--a5244)
    6. [Address patterns and special calls — Table A-IX](#105-address-patterns-and-special-calls--table-a-ix)
    7. [Basic size — A.5.2.4.4.1](#106-basic-size--a52441)
    8. [Extended size — A.5.2.4.4.2](#107-extended-size--a52442)
    9. [Net addresses — A.5.2.4.5](#108-net-addresses--a5245)
    10. [Group addresses — A.5.2.4.6](#109-group-addresses--a5246)
    11. [AllCall addresses — A.5.2.4.7](#1010-allcall-addresses--a5247)
    12. [AnyCalls — A.5.2.4.8](#1011-anycalls--a5248)
    13. [Wildcards — A.5.2.4.9](#1012-wildcards--a5249)
    14. [Self addresses — A.5.2.4.10](#1013-self-addresses--a52410)
    15. [Null address — A.5.2.4.11](#1014-null-address--a52411)
    15. [In-link address — A.5.2.4.12](#1015-in-link-address--a52412)
12. [Nachrichten — A.5.7](#12-nachrichten--a57)
13. [Offene Punkte und Annahmen](#13-offene-punkte-und-annahmen)
14. [Traceability-Matrix](#14-traceability-matrix)
15. [Anhang A — Glossar](#15-anhang-a--glossar)

---

## 1. Epic-Übersicht

| Epic-ID | Titel | Beschreibung | Priorität | Status |
|---|---|---|---|---|
| EPIC-GEN | General Requirements (A.4) | Allgemeine ALE-Betriebsanforderungen, Systemleistung, Datenstrukturen, Betriebsregeln, AQC-ALE | MUST | |
| EPIC-WAVEFORM | ALE Modem Waveform | 8-FSK-Modulation senden und empfangen | MUST | |
| EPIC-WORD | Wortverarbeitung | ALE-Wörter kodieren und dekodieren | MUST | |
| EPIC-FEC | Fehlerkorrektur | Übertragungsfehler erkennen und korrigieren | MUST | |
| EPIC-FRAME | Frame-Struktur | Frame-Aufbau, Calling Cycle, Message, Conclusion | MUST | |
| EPIC-LINK | Link Establishment | Outbound/Inbound Calling-Protokoll und Empfang | MUST | |
| EPIC-SYNC | Synchronization | Wortphasen-Kontrolle und Synchronisationskriterien | MUST | |
| EPIC-SOUND | Sounding | Kanalbewertung durch Stationskennung | COULD | |
| EPIC-CHAN | Kanalauswahl | Kanalauswahl basierend auf LQA und Channel Quality | MUST | |
| EPIC-ADDR | Adressierung | Basic-38-Adressen, Adresstypen, Wildcards, Sonderrufe | MUST | |
| EPIC-MSG | Nachrichtenübertragung | AMD/CMD/DTM/DBM | WON'T | |

---

---

## 2. General Requirements — A.4

> **Spec-Stellen:** A.4.1 (ALE Introduction), A.4.1.1 (ALE Addresses), A.4.1.2 (Scanning), A.4.1.3 (Calling), A.4.1.4 (Channel Evaluation), A.4.1.5 (Channel Quality Display), A.4.2 (System Performance), A.4.2.1 (Scanning Rate), A.4.2.1.1 (AQC Scan Rate), A.4.2.1.2 (AQC Backward Compatibility), A.4.2.2 (Occupancy Detection), A.4.2.3 (Linking Probability), A.4.2.3.1 (AQC Linking Probability), A.4.2.3.2 (AQC Linking Performance), A.4.3 (Required Data Structures), A.4.3.1 (Channel Memory), A.4.3.2 (Self Address Memory), A.4.3.3 (Other Station Table), A.4.3.3.1 (Other Station Address Storage), A.4.3.3.2 (Link Quality Memory), A.4.3.3.3 (Other Station Settings Storage), A.4.3.4 (Operating Parameters), A.4.3.5 (Message Memory), A.4.4 (ALE Operational Rules), A.4.5 (AQC-ALE Introduction), A.4.5.1 (AQC Introduction), A.4.5.2 (AQC Signaling Strategies), A.4.5.3 (AQC Features Supported), A.4.5.4 (AQC Features Not Provided).

### EPIC-GEN · General Requirements (A.4)

#### US-GEN-001

> Als ALE-System will ich alle allgemeinen Betriebsanforderungen gemäß A.4 erfüllen, damit Interoperabilität mit MIL-STD-188-141B-konformen Stationen gewährleistet ist.

**Erfüllt durch:** REQ-GEN-001 bis REQ-GEN-005

#### US-GEN-002

> Als ALE-System will ich die Systemleistungsanforderungen (Scan-Rate, Belegtheitserkennung, Linking-Wahrscheinlichkeit) gemäß A.4.2 einhalten, damit Zuverlässigkeit und Interoperabilität im Betrieb sichergestellt sind.

**Erfüllt durch:** REQ-GEN-006 bis REQ-GEN-014

#### US-GEN-003

> Als ALE-System will ich die erforderlichen Datenstrukturen (Kanalspeicher, Selbstadressspeicher, Fremdstations-Tabelle, Betriebsparameter, Nachrichtenspeicher) gemäß A.4.3 bereitstellen, damit alle Adress-, Kanal- und LQA-Informationen nichtflüchtig verwaltet werden können.

**Erfüllt durch:** REQ-GEN-015 bis REQ-GEN-025

#### US-GEN-004

> Als ALE-System will ich die grundlegenden Betriebsregeln gemäß A.4.4 einhalten (immer empfangen, immer antworten, nicht stören, LQA austauschen usw.), damit zuverlässiger ALE-Betrieb im Netz möglich ist.

**Erfüllt durch:** REQ-GEN-026

#### US-GEN-005

> Als ALE-System will ich optional das AQC-ALE-Protokoll gemäß A.4.5 unterstützen, damit deutlich schnellere Verbindungsaufbauten als mit dem Basis-ALE möglich sind.

**Erfüllt durch:** REQ-GEN-027 bis REQ-GEN-031

---

### 2.1 ALE Introduction — A.4.1

#### REQ-GEN-001 — ALE Data Link: Schichtstruktur und Sublayer

**Spec-Referenz:** A.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine digitale ALE-Datenverbindung bilden, die aus einem robusten Modem und einer Vorwärtsfehlerkorrektur-Codierung besteht. Der ALE-Datenlinkschicht enthält drei Sublayer: den FEC-Sublayer (unterer Sublayer für Fehlerkorrektur und -erkennung), den LP-Sublayer (Linking-Protection-Sublayer in der Mitte) sowie den ALE-Sublayer (oberer Sublayer mit dem ALE-Protokoll für Link-Aufbau, Datenkommunikation und rudimentäres LQA).

**Akzeptanzkriterien:**
- `AC-GEN-001-1` — Das System muss einen FEC-Sublayer bereitstellen, der Redundanz, Majority-Voting, Interleaving und Golay-Codierung auf 24-Bit-ALE-Wörter anwendet.
- `AC-GEN-001-2` — Das System muss einen LP-Sublayer (Linking Protection) zwischen FEC- und ALE-Sublayer bereitstellen.
- `AC-GEN-001-3` — Das System muss einen ALE-Sublayer bereitstellen, der Protokolle für Link-Aufbau, Datenkommunikation und rudimentäres LQA enthält.

---

#### REQ-GEN-002 — ALE-Adressen: Adressierungsstruktur

**Spec-Referenz:** A.4.1.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Stationen müssen die in A.5.2.4 spezifizierte Adressierungsstruktur verwenden, um einzelne Stationen sowie Gruppen von Stationen (Netze und Gruppen) zu identifizieren.

**Akzeptanzkriterien:**
- `AC-GEN-002-1` — Das System muss individuelle Stationsadressen gemäß A.5.2.4 unterstützen.
- `AC-GEN-002-2` — Das System muss Netz- und Gruppenadressierung gemäß A.5.2.4 unterstützen.

---

#### REQ-GEN-003 — Scanning: Wiederholtes Durchlaufen gespeicherter Kanäle

**Spec-Referenz:** A.4.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss in der Lage sein, ausgewählte, im Speicher abgelegte Kanäle wiederholt zu scannen — sowohl unter manueller Steuerung als auch unter Führung eines angeschlossenen automatisierten Controllers. Das System muss den Scan stoppen und auf dem zuletzt besuchten Kanal verharren, wenn eines der folgenden wählbaren Ereignisse eintritt: automatische Controller-Entscheidung zum Stopp, manueller Stopp-Scan-Eingang, oder Aktivierung einer externen Stop-Scan-Leitung (sofern vorhanden). Zu scannende Kanäle sollen nach Gruppen (Scan-Listen) und innerhalb der Gruppen individuell auswählbar sein.

**Akzeptanzkriterien:**
- `AC-GEN-003-1` — Das System muss gespeicherte Kanäle wiederholt scannen können (unter manueller und automatisierter Steuerung).
- `AC-GEN-003-2` — Der Scan muss stoppen und auf dem letzten Kanal verharren, wenn die Controller-Entscheidung, ein manueller Eingang oder eine externe Stop-Scan-Leitung aktiv werden.
- `AC-GEN-003-3` — Kanäle müssen gruppenweise und einzeln innerhalb von Gruppen (Scan-Listen) auswählbar sein.

---

#### REQ-GEN-004 — Calling: Ausführung des Rufprotokolls auf Anforderung

**Spec-Referenz:** A.4.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss auf Anforderung des Operators oder eines externen automatisierten Controllers das in A.5.5 spezifizierte Rufprotokoll ausführen.

**Akzeptanzkriterien:**
- `AC-GEN-004-1` — Das System muss das Rufprotokoll gemäß A.5.5 auf Operator- oder Controller-Anforderung ausführen.

---

#### REQ-GEN-005 — Kanalbewertung und Kanalqualitätsanzeige

**Spec-Referenz:** A.4.1.4, A.4.1.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss in der Lage sein, automatisch ALE-Sounding-Übertragungen gemäß A.5.3 auszusenden und die Signalqualität von ALE-Empfängen automatisch gemäß A.5.4.1 zu messen. Sofern eine Bedienerdisplay-Funktion bereitgestellt wird, muss die Anzeige eine einheitliche Skala von 0 bis 30 (mit 31 = unbekannt) verwenden, die auf SINAD (Signal-plus-Rauschen-plus-Verzerrung zu Rauschen-plus-Verzerrung) basiert.

**Akzeptanzkriterien:**
- `AC-GEN-005-1` — Das System muss automatisch ALE-Sounding-Übertragungen gemäß A.5.3 senden.
- `AC-GEN-005-2` — Das System muss die Signalqualität von ALE-Empfängen automatisch gemäß A.5.4.1 messen.
- `AC-GEN-005-3` — Falls eine Anzeigeeinheit vorhanden ist, muss die Kanalqualitäts-Skala einheitlich 0–30 (31 = unbekannt) auf SINAD-Basis sein.

---

### 2.2 System Performance Requirements — A.4.2

#### REQ-GEN-006 — Scan-Rate: Selektierbare Werte 2 und 5 Kanäle/s

**Spec-Referenz:** A.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Stationen müssen selektierbare Scan-Raten von zwei und fünf Kanälen pro Sekunde unterstützen. Andere Scan-Raten (Design Objective: 10 Kanäle/s) können zusätzlich implementiert werden.

**Akzeptanzkriterien:**
- `AC-GEN-006-1` — Das System muss eine Scan-Rate von 2 Kanälen/s unterstützen.
- `AC-GEN-006-2` — Das System muss eine Scan-Rate von 5 Kanälen/s unterstützen.

---

#### REQ-GEN-007 — AQC-ALE Scan-Rate: Variable Verweilzeiten (NT)

**Spec-Referenz:** A.4.2.1.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Im optionalen AQC-ALE-Protokoll muss das System variable Verweilzeiten während des Scannens unterstützen, sodass Datenverkehr gemäß Tabelle A-II (Linking-Wahrscheinlichkeit) erkannt werden kann.

**Akzeptanzkriterien:**
- `AC-GEN-007-1` — Das System muss im AQC-ALE-Modus variable Verweilzeiten unterstützen.
- `AC-GEN-007-2` — Die Erkennungswahrscheinlichkeit für Datenverkehr muss den Werten aus Tabelle A-II entsprechen.

---

#### REQ-GEN-008 — AQC-ALE Rückwärtskompatibilität: Scan-Raten 2 und 5 Kanäle/s

**Spec-Referenz:** A.4.2.1.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Funkgeräte mit dem optionalen AQC-ALE sollen Scan-Raten von 2 und 5 Kanälen pro Sekunde für Rückwärtskompatibilität mit Nicht-AQC-ALE-Netzen bereitstellen.

**Akzeptanzkriterien:**
- `AC-GEN-008-1` — AQC-ALE-fähige Geräte sollen Scan-Raten von 2 und 5 Kanälen/s unterstützen.

---

#### REQ-GEN-009 — Belegtheitserkennung (NT): Mindestwahrscheinlichkeiten gemäß Tabelle A-I

**Spec-Referenz:** A.4.2.2, Tabelle A-I
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Stationen müssen mindestens die folgenden Erkennungswahrscheinlichkeiten für die angegebenen Wellenformen unter den angegebenen Bedingungen erreichen, bei einer Falschalarmrate von höchstens 1 Prozent (AWGN ohne Fading oder Mehrwegausbreitung, gemessen in 3-kHz-Bandbreite):

| Wellenform | SNR (dB in 3 kHz) | Verweilzeit (s) | Erkennungswahrscheinlichkeit |
|---|---|---|---|
| ALE | 0 | 2,0 | 0,80 |
| ALE | 6 | 2,0 | 0,99 |
| SSB Voice | 6 | 2,0 | 0,80 |
| SSB Voice | 9 | 2,0 | 0,99 |
| MIL-STD-188-110 Serial Tone PSK | 0 | 2,0 | 0,80 |
| MIL-STD-188-110 Serial Tone PSK | 6 | 2,0 | 0,99 |
| STANAG 4529 | 0 | 2,0 | 0,80 |
| STANAG 4529 | 6 | 2,0 | 0,99 |
| STANAG 4285 | 0 | 2,0 | 0,80 |
| STANAG 4285 | 6 | 2,0 | 0,99 |

**Akzeptanzkriterien:**
- `AC-GEN-009-1` — Die Belegtheitserkennung muss für alle Wellenformen aus Tabelle A-I die dort angegebenen Mindestwahrscheinlichkeiten erreichen.
- `AC-GEN-009-2` — Die Falschalarmrate darf 1 Prozent nicht überschreiten.
- `AC-GEN-009-3` — Der Kanalsimulatoren muss AWGN ohne Fading oder Mehrwegausbreitung bereitstellen.

---

#### REQ-GEN-010 — Linking-Wahrscheinlichkeit: Mindestanforderungen gemäß Tabelle A-II

**Spec-Referenz:** A.4.2.3, Tabelle A-II
**Priorität:** MUST · **Status:** offen

**Anforderung:** Linking-Versuche mit einer gemäß diesem Anhang erstellten ALE-Signal-Testaufbau müssen die folgende Linking-Wahrscheinlichkeit (Pl) erreichen:

| SNR (dB in 3 kHz) | Pl (Gaußsches Rauschen) | SNR-Korrektur CCIR Good | SNR-Korrektur CCIR Poor |
|---|---|---|---|
| −2,5 | ≥ 25 % | +0,5 dB | +1,0 dB |
| −1,5 | ≥ 50 % | +2,5 dB | +3,0 dB |
| −0,5 | ≥ 85 % | +5,5 dB | +6,0 dB |
| 0,0 | ≥ 95 % | +8,5 dB | +11,0 dB |

Der modifizierte CCIR Good Channel hat 0,52 ms Mehrwegverzögerung und 0,1 Hz Fading-Bandbreite (zwei Sigma). Der modifizierte CCIR Poor Channel hat 2,2 ms Mehrwegverzögerung und 1,0 Hz Fading-Bandbreite. Doppler-Verschiebungen von +60 Hz dürfen die Leistung um nicht mehr als 1,0 dB gegenüber Tabelle A-II verschlechtern. Jeder SNR-Wert wird in einer nominellen 3-kHz-Bandbreite gemessen. Tests sind gemäß ITU-R F.520-2 durchzuführen.

Zusätzliche Testkriterien:
- Protokoll: Individual Scanning Calling Protocol (nur TO und TIS-Präambeln)
- Adressen: alphanumerisch, ein Wort (drei Zeichen), aus dem 38-Zeichen-Basic-ASCII-Subset
- Prüflinge scannen 10 Kanäle bei 2 Kanälen/s (und wiederholt bei 5 Kanälen/s)
- Rufinitiierung mit gestopptem und auf die Ruffrequenz abgestimmtem Sender
- Maximale Zeit von Rufinitiierung bis Linkaufbau: 14,000 s zuzüglich Simulator-Verzögerung
- Der Ruf darf nicht mehr als 23 redundante Wörter, die Antwort nicht mehr als 3 und die Bestätigung nicht mehr als 3 redundante Wörter umfassen

**Akzeptanzkriterien:**
- `AC-GEN-010-1` — Das System muss bei den in Tabelle A-II angegebenen SNR-Werten die jeweils geforderten Linking-Wahrscheinlichkeiten erreichen.
- `AC-GEN-010-2` — Doppler-Verschiebungen von +60 Hz dürfen die Leistung um höchstens 1,0 dB verschlechtern.
- `AC-GEN-010-3` — Die maximale Zeit vom Rufbeginn bis zum Linkaufbau darf 14,000 s (zuzüglich Simulator-Verzögerung) nicht überschreiten.
- `AC-GEN-010-4` — Der Ruf darf maximal 23, Antwort und Bestätigung je maximal 3 redundante Wörter umfassen.

---

#### REQ-GEN-011 — AQC-ALE Linking-Wahrscheinlichkeit (NT)

**Spec-Referenz:** A.4.2.3.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Bei implementiertem optionalen AQC-ALE-Protokoll muss die Linking-Wahrscheinlichkeit den Werten aus Tabelle A-II entsprechen, mit folgenden zusätzlichen Kriterien: Protokoll ist das AQC-Individual-Calling-Protokoll ohne Nachrichtenübertragung; Adressen haben 1 bis 6 Zeichen aus dem 38-Zeichen-Basic-ASCII-Subset; gerufene Einheiten scannen 10 Kanäle; Rufinitiierung mit gestopptem und auf die Ruffrequenz abgestimmtem Sender; der initiale Call-Probe darf nicht mehr als 10 Trw, die Call Response nicht mehr als 4 Trw und die Bestätigung nicht mehr als 2 Trw umfassen.

**Akzeptanzkriterien:**
- `AC-GEN-011-1` — Die AQC-ALE-Linking-Wahrscheinlichkeit muss den Tabelle-A-II-Werten entsprechen.
- `AC-GEN-011-2` — Call-Probe ≤ 10 Trw, Response ≤ 4 Trw, Bestätigung ≤ 2 Trw.

---

#### REQ-GEN-012 — AQC-ALE Linking-Leistung bei LP-Level 1 und 2

**Spec-Referenz:** A.4.2.3.2
**Priorität:** COULD · **Status:** offen

**Anforderung:** Die AQC-ALE Linking-Leistung darf durch LP-Level 1 oder 2 nicht verschlechtert werden. Scan-Raten von 2 oder 5 Kanälen/s können die Leistung verschlechtern, da während des Call-Probes möglicherweise nicht genügend redundante Wörter ausgestrahlt werden.

**Akzeptanzkriterien:**
- `AC-GEN-012-1` — Die AQC-ALE-Leistung darf bei LP-Level 1 oder 2 nicht abnehmen.

---

### 2.3 Required Data Structures — A.4.3

#### REQ-GEN-013 — Kanalspeicher: Mindestens 100 Kanaleinträge, nichtflüchtig

**Spec-Referenz:** A.4.3.1, Tabelle A-III
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gerät muss mindestens 100 verschiedene Kanalinformationssätze speichern, abrufen und verwenden können. Jeder Eintrag umfasst Sende- und Empfangsfrequenzen sowie zugehörige Modusinformationen (Sendeleistungspegel, Kanalnutzung, Sounding-Daten, Modulationstyp, Sende-/Empfangsmodus; optional: Filterbandbreite, AGC-Einstellung, Antennenport-Auswahl, Informationsport-Auswahl, Noise-Blanker-Einstellung, Sicherheitseinstellung, Sounding-Selbstadressen). Der Kanalspeicher muss nichtflüchtig sein. Jeder Kanal muss manuell oder per Controller abrufbar sein und darf nach dem Abruf ohne Veränderung des ursprünglich gespeicherten Eintrags modifiziert werden.

**Akzeptanzkriterien:**
- `AC-GEN-013-1` — Das Gerät muss mindestens 100 Kanaleinträge speichern können.
- `AC-GEN-013-2` — Der Kanalspeicher muss nichtflüchtig sein.
- `AC-GEN-013-3` — Jeder Kanal muss manuell oder per Controller abrufbar sein.
- `AC-GEN-013-4` — Nach dem Abruf darf der ursprüngliche gespeicherte Eintrag nicht verändert werden, auch wenn der abgerufene Eintrag modifiziert wird.

---

#### REQ-GEN-014 — Selbstadressspeicher: Mindestens 20 Einträge, nichtflüchtig

**Spec-Referenz:** A.4.3.2, Tabelle A-IV
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss mindestens 20 verschiedene Selbstadressinformationssätze speichern, abrufen und verwenden können. Der Speicher muss nichtflüchtig sein. Jeder Satz umfasst die eigene Adresse, gültige zugehörige Kanäle und Netzadressierungsinformationen. Die Netzadressierungsinformation umfasst für jede Netz-Member-Selbstadresse: Netzadresse und zugehörige Slot-Wartezeit (in Vielfachen von Tw) gemäß Formel Tswt(SN) = Tsw × SN. Stationen, die per Netzrufadresse gerufen werden, antworten mit ihrer Netz-Member-Adresse nach der vorgeschriebenen Verzögerung. Stationen, die einzeln per einer ihrer Selbstadressen gerufen werden, antworten sofort. Stationen, die per Netzadresse ohne zugehörige Netz-Member-Adresse gerufen werden, pausieren und hören zu, antworten jedoch nicht, treten aber in den verlinkten Zustand ein.

**Akzeptanzkriterien:**
- `AC-GEN-014-1` — Das System muss mindestens 20 Selbstadresssätze speichern können.
- `AC-GEN-014-2` — Der Selbstadressspeicher muss nichtflüchtig sein.
- `AC-GEN-014-3` — Netz-Member-Stationen müssen nach der durch Tswt(SN) = Tsw × SN berechneten Verzögerung mit ihrer Netz-Member-Adresse antworten.
- `AC-GEN-014-4` — Einzeln gerufene Stationen müssen sofort antworten.
- `AC-GEN-014-5` — Stationen ohne Netz-Member-Adresse zu einer Netzrufadresse müssen in den verlinkten Zustand eintreten, ohne zu antworten.

---

#### REQ-GEN-015 — Fremdstations-Tabelle: Mindestens 100 Einträge

**Spec-Referenz:** A.4.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss mindestens 100 verschiedene Informationssätze zu Adressen anderer Stationen und Netze, Kanalqualitätsdaten zu diesen Stationen (Messungen oder Prognosen) und gerätespezifische Einstellungen für Links zu jeder Station oder jedem Netz speichern, abrufen und verwenden können. Design Objective: Überschusskapazität, die nicht mit vorgeplanteren Fremdstations-Informationen belegt ist, soll automatisch mit auf gescannten oder überwachten Kanälen gehörten Adressen gefüllt werden; wenn die Kapazität erschöpft ist, sollen die ältesten gehörten Adressen durch die neuesten ersetzt werden. Diese Informationen sollen für Rufinitiierung und Aktivitätsbewertung verwendet werden.

**Akzeptanzkriterien:**
- `AC-GEN-015-1` — Das System muss mindestens 100 Fremdstations-Einträge speichern können.
- `AC-GEN-015-2` — Jeder Eintrag muss Adresse, Kanalqualitätsdaten und gerätespezifische Link-Einstellungen enthalten können.

---

#### REQ-GEN-016 — Fremdstations-Adressspeicher: Individuelle Einträge und Netzinformationen, nichtflüchtig

**Spec-Referenz:** A.4.3.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Individuelle Stationsadressen müssen in separaten Tabelleneinträgen gespeichert werden und können mit einer spezifischen Wartezeit für Antworten (Twr) verknüpft sein, sofern dieser vom Standardwert abweicht. Netzinformationen müssen eigene Netz- und Netz-Member-Zuordnungen, relative Slot-Sequenzen und eigene Netz-Wartezeiten für Antworten (Twrn) für Rufinitiierungen enthalten. Der Adress- und Einstellungsspeicher muss nichtflüchtig sein.

**Akzeptanzkriterien:**
- `AC-GEN-016-1` — Individuelle Stationsadressen müssen in separaten Einträgen gespeichert sein.
- `AC-GEN-016-2` — Jeder Eintrag kann eine spezifische Twr-Wartezeit enthalten.
- `AC-GEN-016-3` — Netzinformationen müssen Netz-Member-Zuordnungen, Slot-Sequenzen und Twrn enthalten.
- `AC-GEN-016-4` — Der Speicher muss nichtflüchtig sein.

---

#### REQ-GEN-017 — LQA-Speicher: Mindestens 4000 Einträge, 1 Stunde Pufferung

**Spec-Referenz:** A.4.3.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gerät muss mindestens 4000 (Design Objective: 10.000) Konnektivitäts- und LQA-Informationssätze, die Kanälen und Fremdadressen zugeordnet sind, in einem LQA-Speicher halten können. Der LQA-Speicher muss bei Stromausfall oder Verlust der Primärversorgung mindestens eine Stunde lang erhalten bleiben. Jede Adress-/Kanalzelle muss mindestens bilaterale SINAD-Werte für empfangene Signale an der eigenen Station und von der eigenen Station empfangene und von der Fremdstation gemeldete Signale enthalten. Sie muss außerdem entweder eine Altersanzeige der Information oder einen Algorithmus zur automatischen Gewichtsreduzierung älterer Daten beinhalten. Design Objective: Zellen sollen auch bilaterale BER- und bilaterale Mehrweg-Informationen von geeignet ausgerüsteten Einheiten enthalten.

**Akzeptanzkriterien:**
- `AC-GEN-017-1` — Das System muss mindestens 4000 LQA-Einträge halten können.
- `AC-GEN-017-2` — Der LQA-Speicher muss bei Stromausfall mindestens 1 Stunde lang erhalten bleiben.
- `AC-GEN-017-3` — Jede Zelle muss bilaterale SINAD-Werte enthalten.
- `AC-GEN-017-4` — Jede Zelle muss eine Altersanzeige oder einen Gewichtsreduzierungsalgorithmus für ältere Daten enthalten.

---

#### REQ-GEN-018 — Fremdstations-Einstellungen: Nichtflüchtige Speicherung (DO)

**Spec-Referenz:** A.4.3.3.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Design Objective: Gerätespezifische Einstellungen für Links mit bestimmten Stationen oder Netzen sollen in nichtflüchtigem Speicher abgelegt werden. Solche Einstellungen können Antennenauswahl und -azimut, für diese Station oder dieses Netz autorisierte Kanäle, Leistungsgrenzen für das betreffende Netz usw. umfassen.

**Akzeptanzkriterien:**
- `AC-GEN-018-1` — Das System soll station- und netzspezifische Einstellungen in nichtflüchtigem Speicher ablegen können.

---

#### REQ-GEN-019 — Betriebsparameter: Programmierbarkeit durch Operator oder Controller

**Spec-Referenz:** A.4.3.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die folgenden ALE-Betriebsparameter müssen durch den Operator oder einen externen automatisierten Controller programmierbar sein (vollständige Definitionen in Anhang H):

`ScanRate`, `RequestLQA`, `OtherAddr`, `LqaStatus`, `MaxScanChan`, `AutoPowerAdj`, `OtherAddrStatus`, `LqaAge`, `MaxTuneTime`, `SelfAddrTable`, `TurnAroundTime`, `SelfAddrEntry`, `ActivityTimeout`, `SelfAddr`, `ListenTime`, `OtherAddrNetMembers`, `LqaMultipath`, `OtherAddrValidChannels`, `LqaSINAD`, `OtherAddrAnt`, `LqaBER`, `SelfAddrStatus`, `OtherAddrAntAzimuth`, `ScanSet`, `AcceptAnyCall`, `NetAddr`, `OtherAddrPower`, `AcceptAllcall`, `SlotWaitTime`, `LqaMatrix`, `AcceptAMD`, `SelfAddrValidChannels`, `LqaEntry`, `AcceptDTM`, `AcceptDBM`, `OtherAddrTable`, `LqaAddr`, `OtherAddrEntry`, `LqaChannel`, `ConnectionTable`, `ConnectionEntry`, `ConnectedAddr`, `ConnectionStatus`

**Akzeptanzkriterien:**
- `AC-GEN-019-1` — Alle aufgeführten Parameter müssen durch den Operator oder externen Controller programmierbar sein.

---

#### REQ-GEN-020 — Nachrichtenspeicher: Mindestens 12 Nachrichten / 1000 Zeichen, 1 Stunde Pufferung

**Spec-Referenz:** A.4.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Im Gerät muss Speicher für vorprogrammierte, vom Operator eingegebene und eingehende Nachrichten bereitgestellt werden. Dieser Speicher muss bei Stromausfall oder Verlust der Primärversorgung mindestens eine Stunde lang erhalten bleiben. Es müssen mindestens 12 Nachrichten (Design Objective: 100) bei einer Gesamtkapazität von mindestens 1000 Zeichen (Design Objective: 10.000 Zeichen) gespeichert werden können.

**Akzeptanzkriterien:**
- `AC-GEN-020-1` — Das System muss mindestens 12 Nachrichten speichern können.
- `AC-GEN-020-2` — Die Gesamtkapazität muss mindestens 1000 Zeichen betragen.
- `AC-GEN-020-3` — Der Nachrichtenspeicher muss bei Stromausfall mindestens 1 Stunde lang erhalten bleiben.

---

### 2.4 ALE Operational Rules — A.4.4

#### REQ-GEN-021 — ALE-Betriebsregeln gemäß Tabelle A-V (Prioritätsreihenfolge)

**Spec-Referenz:** A.4.4, Tabelle A-V
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss die folgenden grundlegenden Betriebsregeln in der angegebenen Prioritätsreihenfolge einhalten (einige Regeln können in bestimmten Anwendungen nicht anwendbar sein, z. B. ist "immer hörend" beim Senden mit einem Transceiver oder bei gemeinsamer Antenne nicht möglich):

1. Unabhängige ALE-Empfangsfähigkeit (parallel zu anderen Modems und ähnlichen Audio-Empfängern) — kritisch
2. Immer hören (für ALE-Signale) — kritisch
3. Immer antworten (außer bei bewusster Unterdrückung)
4. Immer scannen (wenn nicht anderweitig in Benutzung)
5. Aktiven Kanal mit erkennbarem Datenverkehr gemäß Tabelle A-I nicht stören (außer wenn die Listen-Call-Funktion vom Operator oder einem anderen Controller übersteuert wird)
6. LQA immer mit anderen Stationen austauschen (außer bei Unterdrückung) und die Signalqualität anderer immer messen
7. Im geeigneten Zeitschlitz auf Rufe antworten, die Schlitz-Antworten erfordern
8. Immer die eigene Konnektivität zu anderen suchen (außer bei Unterdrückung) und verfolgen
9. Verlinkende ALE-Stationen setzen die höchste beidseitig unterstützte Fähigkeitsebene ein
10. Sende- und Empfangszeit auf dem Kanal minimieren
11. Verwendete Sendeleistung automatisch minimieren (sofern fähig)

**Akzeptanzkriterien:**
- `AC-GEN-021-1` — Das System muss eine unabhängige ALE-Empfangsfähigkeit parallel zu anderen Modems bereitstellen.
- `AC-GEN-021-2` — Das System muss immer auf ALE-Signale hören (soweit physikalisch möglich).
- `AC-GEN-021-3` — Das System muss immer antworten, sofern nicht explizit unterdrückt.
- `AC-GEN-021-4` — Das System muss immer scannen, wenn es nicht anderweitig genutzt wird.
- `AC-GEN-021-5` — Das System darf aktiven Kanal mit erkennbarem Datenverkehr (gemäß Tabelle A-I) nicht stören.
- `AC-GEN-021-6` — Das System muss LQA immer austauschen und die Signalqualität anderer immer messen.
- `AC-GEN-021-7` — Das System muss im richtigen Zeitschlitz auf Rufe mit Schlitz-Antwort-Anforderung antworten.
- `AC-GEN-021-8` — Das System muss die eigene Konnektivität zu anderen Stationen suchen und verfolgen.
- `AC-GEN-021-9` — Verlinkende Stationen müssen die höchste beidseitig unterstützte Fähigkeitsebene einsetzen.
- `AC-GEN-021-10` — Das System muss Sende- und Empfangszeit auf dem Kanal minimieren.
- `AC-GEN-021-11` — Das System muss die verwendete Sendeleistung automatisch minimieren, sofern es dazu in der Lage ist.

---

### 2.5 Alternate Quick Call ALE (AQC-ALE) — A.4.5

#### REQ-GEN-022 — AQC-ALE: Einführung und Grundprinzip (NT)

**Spec-Referenz:** A.4.5.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE kann zusätzlich zur grundlegenden ALE-Funktionalität implementiert werden. Es stellt eine Linkaufbautechnik bereit, die deutlich weniger Zeit für den Linkaufbau benötigt als das Basis-ALE-System, indem zusätzliche Technologien eingesetzt und weniger genutzte Funktionen des Basissystems gegen einen schnelleren Verbindungsaufbau eingetauscht werden. Das AQC-ALE muss immer auf den Basis-ALE-Ruf hören und automatisch in diesem Modus antworten und operieren, wenn es per Basis-ALE gerufen wird.

**Akzeptanzkriterien:**
- `AC-GEN-022-1` — Das AQC-ALE-System muss immer auf Basis-ALE-Rufe hören.
- `AC-GEN-022-2` — Das AQC-ALE-System muss automatisch im Basis-ALE-Modus antworten, wenn es per Basis-ALE gerufen wird.

---

#### REQ-GEN-023 — AQC-ALE: Allgemeine Signalisierungsstrategien

**Spec-Referenz:** A.4.5.2
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE-Format muss folgende Eigenschaften aufweisen: drei Adresszeichen (21 Bit) werden in einen 16-Bit-Wert gepackt; Adressen werden von maximal 15 auf 6 Zeichen reduziert; in jeder Transaktion werden sechs Adresszeichen gesendet; die Präambeln FROM (wird PART2, zeigt das 2. Adresswort an) und THRU (wird INLINK, zeigt eine verlinkte Transaktion an) werden ersetzt; Stationsadressen werden vom Nachrichtenanteil der Signalisierungsstruktur isoliert (TO, TIS, TWAS, INLINK, PART2 für Adressierung; CMD, DATA und REP für Nachrichtenübertragung); ein festes Bit in jedem Adresswort verhindert, dass gültige AQC-ALE-Adressen auch gültige Basis-ALE-Adressen sind; mindestens acht Informationsbits werden pro Übertragung bereitgestellt.

**Akzeptanzkriterien:**
- `AC-GEN-023-1` — Das Format muss drei Adresszeichen in 16 Bit packen.
- `AC-GEN-023-2` — Adressen dürfen maximal 6 Zeichen haben.
- `AC-GEN-023-3` — Pro Transaktion müssen sechs Adresszeichen gesendet werden.
- `AC-GEN-023-4` — FROM-Präambel muss durch PART2 ersetzt werden, THRU-Präambel durch INLINK.
- `AC-GEN-023-5` — Adressierungs- und Nachrichtenpräambeln müssen voneinander getrennt sein.
- `AC-GEN-023-6` — AQC-ALE-Adressen dürfen keine gültigen Basis-ALE-Adressen sein.
- `AC-GEN-023-7` — Mindestens 8 Informationsbits müssen pro Übertragung bereitgestellt werden.

---

#### REQ-GEN-024 — AQC-ALE: Unterstützte Funktionen

**Spec-Referenz:** A.4.5.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE-Protokoll muss folgende Basis-ALE-Funktionen vollständig implementieren (eine Station im AQC-ALE-Modus kann auf alle Ruftypen antworten; eine Station nur mit Basis-ALE zweiter Generation antwortet nicht auf AQC-ALE-Protokollformen):
- Linking-Protection-Level 0, 1, 2, 3
- Einzelrufe (Unit Calls)
- Star-Net-Rufe
- AllCalls
- AnyCalls
- LQA-Austausch als Teil des Ruf-Handshakes
- Orderwire- und Relay-Funktionen während eines Links: AMD (Automatic Message Display), DTM (Data Text Message) oder DBM; User Unique Functions (UUF); Call-Relay-Funktionen; Tageszeit- und Netzmanagement
- Soundings: auf Scan-Zeit + 50 % verkürzt; können ein PSK-Signal zur Verbesserung der LQA-Daten enthalten

**Akzeptanzkriterien:**
- `AC-GEN-024-1` — Das AQC-ALE muss LP-Level 0, 1, 2 und 3 unterstützen.
- `AC-GEN-024-2` — Das AQC-ALE muss Einzelrufe, Star-Net-Rufe, AllCalls und AnyCalls unterstützen.
- `AC-GEN-024-3` — Das AQC-ALE muss LQA-Austausch als Teil des Ruf-Handshakes unterstützen.
- `AC-GEN-024-4` — Das AQC-ALE muss Orderwire (AMD, DTM, DBM) und Relay-Funktionen während eines Links unterstützen.
- `AC-GEN-024-5` — Soundings im AQC-ALE müssen auf Scan-Zeit + 50 % verkürzt sein.

---

#### REQ-GEN-025 — AQC-ALE: Nicht unterstützte Funktionen

**Spec-Referenz:** A.4.5.4
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE-Protokoll stellt folgende Funktionen nicht bereit: Gruppenruf (als Alternative kann ein Controller nacheinander weitere Mitglieder über das Rufprotokoll hinzufügen); AMD, DTM und DBM während des Linkaufbaus (der Fokus liegt auf schnellstmöglichem Linkaufbau; nach dem Linkaufbau kann Information ausgetauscht werden); frühe Identifikation der Adresse des Senders während des Orderwire-Datenverkehrs oder zusätzliche Adressierungsidentifikation für Relay-Adressen (der vereinfachte Linkaufbau macht dies überflüssig; Orderwire-Nachrichten sind während des Linkaufbaus nicht zulässig).

**Akzeptanzkriterien:**
- `AC-GEN-025-1` — Das AQC-ALE darf keinen Gruppenruf bereitstellen (Alternative: sequenzielles Hinzufügen über Rufprotokoll).
- `AC-GEN-025-2` — AMD, DTM und DBM dürfen im AQC-ALE nicht während des Linkaufbaus verwendet werden.
- `AC-GEN-025-3` — Orderwire-Nachrichten sind während des AQC-ALE-Linkaufbaus nicht zulässig.

---

## 3. ALE Modem Waveform — A.5.1

> **Spec-Stellen sammeln:** A.5.1.1 (Introduction), A.5.1.2 (Tones), A.5.1.3 (Timing), A.5.1.4 (Accuracy).

### EPIC-WAVEFORM · ALE Modem Waveform

#### US-WAVEFORM-001

> Als Funksystem will ich Daten als 8-FSK-Töne im Audioband senden und empfangen, damit eine HF-Funkstrecke ALE-Signale überträgt.

**Erfüllt durch:** REQ-WAVEFORM-001, REQ-WAVEFORM-002, REQ-WAVEFORM-003, REQ-WAVEFORM-004, REQ-WAVEFORM-005, REQ-WAVEFORM-006, REQ-WAVEFORM-007, REQ-WAVEFORM-008, REQ-WAVEFORM-009, REQ-WAVEFORM-010, REQ-WAVEFORM-011, REQ-WAVEFORM-012, REQ-WAVEFORM-013

#### REQ-WAVEFORM-001 — Modulzweck des ALE-Waveform

**Spec-Referenz:** A.5.1.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die ALE-Waveform ist so ausgelegt, dass sie durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden kann. Sie stellt eine robuste, langsame digitale Modem-Kapazität für multiple Zwecke bereit, einschließlich selektives Rufen und Datenübertragung.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-001-1` — Die Waveform muss durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden können
- `AC-WAVEFORM-001-2` — Die Waveform muss selektives Rufen ermöglichen
- `AC-WAVEFORM-001-3` — Die Waveform muss Datenübertragung ermöglichen

---

#### REQ-WAVEFORM-002 — Modulationsart

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Waveform muss eine 8-ary frequency shift-keying (FSK) Modulation mit acht orthogonalen Tönen verwenden, wobei jeweils ein Ton (Symbol) zu einer Zeit übertragen wird. Jeder Ton repräsentiert drei Bits Daten.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-002-1` — Die Modulation muss FSK mit exakt 8 orthogonalen Tönen sein
- `AC-WAVEFORM-002-2` — Pro Ton müssen exakt 3 Bits repräsentiert werden
- `AC-WAVEFORM-002-3` — Es muss immer nur ein Ton zu einer Zeit übertragen werden

---

#### REQ-WAVEFORM-003 — Tonfrequenzen und Bit-Zuordnung

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die acht Tonfrequenzen und ihre Zuordnung zu 3-Bit-Werten sind festgelegt wie in der folgenden Tabelle.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-003-1` — Jede Frequenz muss exakt das zugewiesene 3-Bit-Muster repräsentieren

**Vom-Standard-vorgegebene Werte:**

| Tonfrequenz | 3-Bit-Wert (MSB→LSB) | Cycles-per-Symbol
|---|---|---|
| 750 Hz | 000 | 6
| 1000 Hz | 001 | 8
| 1250 Hz | 011 | 10
| 1500 Hz | 010 | 12
| 1750 Hz | 110 | 14
| 2000 Hz | 111 | 16
| 2250 Hz | 101 | 18
| 2500 Hz | 100 | 20

---

#### REQ-WAVEFORM-004 — Codierung und Interleaving der Bits

**Spec-Referenz:** A.5.1.2, Bezug auf A.5.2.2 und A.5.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die übertragenen Bits müssen codierte und interleaved Datenbits sein, die ein Wort bilden.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-004-1` — Die Bits müssen codiert und interleaved sein gemäß den in A.5.2.2 und A.5.2.3 definierten Verfahren (siehe OPEN-01, OPEN-02).
- `AC-WAVEFORM-004-2` — Jede Gruppe von Bits muss einem vollständigen ALE-Wort entsprechen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-01 — Der Verweis auf A.5.2.2 (FEC/Golay) und A.5.2.3 (Word structures) ist enthalten, aber der Inhalt dieser Abschnitte wurde nicht geliefert. Codierungsdetail und Interleaving-Regeln sind hier nicht spezifiziert.

---

#### REQ-WAVEFORM-005 — Phasenkontinuität der Tonübergänge

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Übergänge zwischen Tönen müssen phasenkontinuierlich sein und an den Maxima oder Minima der Welle (slope zero) erfolgen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-005-1` — Tonübergänge müssen phasenkontinuierlich sein
- `AC-WAVEFORM-005-2` — Tonübergänge müssen an einem Punkt mit null Ableitung (Maxima oder Minima) erfolgen

---

#### REQ-WAVEFORM-006 — Tonrate und Symbolperiode

**Spec-Referenz:** A.5.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Töne müssen mit einer Rate von 125 Tönen (Symbolen) pro Sekunde übertragen werden, mit einer resultierenden Periode von 8 ms pro Ton.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-006-1` — Die Tonrate muss exakt 125 Symbole/Sekunde sein
- `AC-WAVEFORM-006-2` — Die Periode pro Ton muss exakt 8 ms sein

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Tonrate | 125 | Symbole/Sekunde | A.5.1.3 |
| Ton-Periode (Tc) | 8 | ms | A.5.1.3 |

---

#### REQ-WAVEFORM-007 — Übertragene Bitrate

**Spec-Referenz:** A.5.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die übertragene Bitrate muss 375 Bits pro Sekunde betragen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-007-1` — Die übertragene Bitrate muss exakt 375 b/s sein

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Bitrate | 375 | b/s | A.5.1.3 |

---

#### REQ-WAVEFORM-008 — Wortgrenzen und redundante Wörter

**Spec-Referenz:** A.5.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Übergänge zwischen benachbarten redundanten (verdreifachten) übertragenen Wörtern müssen mit den Übergängen zwischen Tönen übereinstimmen, was zu einer integralen 49 Symbolen (Tönen) per redundantem (verdreifachtem) Wort führt.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-008-1` — Wortübergänge müssen mit Tonübergängen synchronisiert sein
- `AC-WAVEFORM-008-2` — Es müssen exakt 49 Symbole pro redundantem Wort sein

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Symbole pro redundantem Wort | 49 | Symbole | A.5.1.3 |

---

#### REQ-WAVEFORM-009 — Einzelwortperiode (Tw)

**Spec-Referenz:** A.5.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Einzelwortperiode (Tw) muss 130,66... ms (oder 16,33... Symbole) betragen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-009-1` — Tw muss 130,66... ms betragen
- `AC-WAVEFORM-009-2` — Tw muss 16,33... Symbolen entsprechen

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Einzelwortperiode (Tw) | 130,66... | ms | A.5.1.3 |
| Einzelwortperiode (Tw) | 16,33... | Symbole | A.5.1.3 |

---

#### REQ-WAVEFORM-010 — Verdreifachte-Wort-Periode (3×Tw)

**Spec-Referenz:** A.5.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die verdreifachte-Wort-Periode (3×Tw) für das grundlegende redundante Format muss 392 ms betragen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-010-1` — 3×Tw muss exakt 392 ms betragen

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Verdreifachte-Wort-Periode (3×Tw) | 392 | ms | A.5.1.3 |

---

#### REQ-WAVEFORM-011 — Tonfrequenzgenauigkeit am Baseband

**Spec-Referenz:** A.5.1.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Am Baseband-Audio müssen die erzeugten Töne innerhalb von ±1,0 Hz liegen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-011-1` — Jeder erzeugte Ton muss innerhalb von ±1,0 Hz seines Sollwerts liegen

**Vom-Standard-vorgegebene Werte:**

| Parameter | Toleranz | Einheit | Spec-Referenz |
|---|---|---|---|
| Tonfrequenz-Abweichung (Baseband) | ±1,0 | Hz | A.5.1.4 |

---

#### REQ-WAVEFORM-012 — Sendeleistung der Töne im HF-Bereich

**Spec-Referenz:** A.5.1.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Im HF-Bereich müssen alle gesendeten Töne innerhalb der Amplitude von 2,0 dB liegen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-012-1` — Alle übertragenen Töne müssen innerhalb von 2,0 dB Amplitude im HF-Bereich sein

**Vom-Standard-vorgegebene Werte:**

| Parameter | Toleranz | Einheit | Spec-Referenz |
|---|---|---|---|
| HF-Amplituden-Toleranz | 2,0 | dB | A.5.1.4 |

---

#### REQ-WAVEFORM-013 — Symbol-Timing-Genauigkeit

**Spec-Referenz:** A.5.1.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das gesendete Symbol-Timing, und daher die Bit- und Wortraten, müssen innerhalb von 10 Teilen pro Million liegen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-013-1` — Symbol-Timing muss innerhalb von 10 ppm sein
- `AC-WAVEFORM-013-2` — Bit-Rate muss innerhalb von 10 ppm sein
- `AC-WAVEFORM-013-3` — Wort-Rate muss innerhalb von 10 ppm sein

**Vom-Standard-vorgegebene Werte:**

| Parameter | Toleranz | Einheit | Spec-Referenz |
|---|---|---|---|
| Timing-Genauigkeit | 10 | ppm | A.5.1.4 |

---

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- **Bit-Zuordnungstabelle (A.5.1.2)** — Frequenz-zu-Bit-Mapping als Lookup-Tabelle oder switch-case. Vorschlag Rückverweis: implements REQ-WAVEFORM-003
- **Phasenkontinuierliche Erzeugung (A.5.1.2)** — Phase-state tracking für übergangsfreie Tonwechsel. Vorschlag Rückverweis: implements REQ-WAVEFORM-005
- **Baseband-Tonfrequenz-Genauigkeit (A.5.1.4)** —±1 Hz Präzision bei der Tone-Generation. Vorschlag Rückverweis: implements REQ-WAVEFORM-011
- **HF-Amplituden-Messung (A.5.1.4)** — Amplitudenmessung im HF-Bereich, 2 dB Toleranz. Vorschlag Rückverweis: implements REQ-WAVEFORM-012
- **Symbol-Timing-Genauigkeit (A.5.1.4)** — 10 ppm Timing-Stabilität erforderlich. Vorschlag Rückverweis: implements REQ-WAVEFORM-013
- **Tone/Word Scheduler-Abgrenzung** — Das 3×-Scheduling gehört nicht in die Tonzuordnung, sondern in die Protokoll-/State-Machine-Ebene; die Waveform bleibt auf Symbolerzeugung und Tonmapping beschränkt. Vorschlag Rückverweis: implements REQ-WAVEFORM-008, REQ-WAVEFORM-013


---


## 4. Word Structures — A.5.2.3

> **Spec-Stellen sammeln:** A.5.2.3.1 (ALE word format), A.5.2.3.1.2 (Word types), A.5.2.3.1.3 (Preambles), A.5.2.3.2 (Address words), A.5.2.3.3 (Message words), A.5.2.3.4 (Extension words), Figure A-12, Table A-VIII.

### EPIC-WORD · Wortverarbeitung

#### US-WORD-001

> Als ALE-Station will ich Adress- und Steuerinformation in standardkonforme Wörter kodieren, damit Gegenstellen sie interpretieren können.

**Erfüllt durch:** REQ-WORD-001, REQ-WORD-002, REQ-WORD-003, REQ-WORD-004, REQ-WORD-005, REQ-WORD-006, REQ-WORD-007, REQ-WORD-008, REQ-WORD-009, REQ-WORD-010

### 3.1 ALE word format — A.5.2.3.1

#### REQ-WORD-001 — Grundstruktur des ALE-Worts

**Spec-Referenz:** A.5.2.3.1 / Figure A-12
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein grundlegendes ALE-Wort muss 24 Bit Informationsinhalt umfassen und aus W1 als höchstwertigem Bit bis W24 als niederwertigem Bit bestehen. Das Wort muss in einen 3-Bit-Präambelteil und einen 21-Bit-Datenfeldteil unterteilt sein. Das höchstwertige Bit muss zuerst übertragen werden. Vor der Übertragung muss das Wort in zwei 12-Bit-Hälften für die FEC-Kodierung aufgeteilt werden. Das Datenfeld kann drei 7-Bit-ASCII-Zeichen pro Wort enthalten.

**Akzeptanzkriterien:**
- `AC-WORD-001-1` — Ein grundlegendes ALE-Wort enthält genau 24 Bit.
- `AC-WORD-001-2` — Das Wort ist in genau einen 3-Bit-Präambelteil und einen 21-Bit-Datenfeldteil unterteilt.
- `AC-WORD-001-3` — Das höchstwertige Bit wird zuerst übertragen.
- `AC-WORD-001-4` — Vor der Übertragung wird das Wort in zwei 12-Bit-Hälften für die FEC-Kodierung aufgeteilt.
- `AC-WORD-001-5` — Das Datenfeld kann drei 7-Bit-ASCII-Zeichen pro Wort enthalten.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Wortlänge | 24 | Bit | A.5.2.3.1 |
| Präambellänge | 3 | Bit | A.5.2.3.1 |
| Datenfeldlänge | 21 | Bit | A.5.2.3.1 |
| Teilwortlänge für FEC | 12 | Bit | A.5.2.3.1 |
| Anzahl Teilwörter für FEC | 2 | Stück | A.5.2.3.1 |
| ASCII-Zeichen pro Datenfeld | 3 | Zeichen | Figure A-12 |

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Konkrete Bit-Reihenfolge W1 bis W24, einschließlich W1 als MSB und W24 als LSB — Vorschlag Rückverweis: implements REQ-WORD-001

### 3.2 Word types and preambles — A.5.2.3.1.2 / A.5.2.3.1.3

#### REQ-WORD-002 — Präambelbits und Worttypen

**Spec-Referenz:** A.5.2.3.1.2 / A.5.2.3.1.3 / Table A-VIII
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die führenden drei Bits eines ALE-Worts müssen die Präambelbits P3 bis P1 bilden. Diese Präambelbits müssen einen von acht möglichen Worttypen identifizieren. Die Worttypen und ihre Bedeutung müssen den im Standard definierten Codes und Funktionen entsprechen. Optional definierte AQC-ALE-Preambles sind in A.5.8.1.2 festgelegt.

**Akzeptanzkriterien:**
- `AC-WORD-002-1` — Die führenden drei Bits eines ALE-Worts identifizieren genau einen von acht Worttypen.
- `AC-WORD-002-2` — Die Präambelbits werden als P3, P2 und P1 geführt.
- `AC-WORD-002-3` — Die Worttypen THRU, TO, CMD, FROM, TIS, TWAS, DATA und REP werden gemäß Standard unterstützt.
- `AC-WORD-002-4` — Optional definierte AQC-ALE-Preambles werden nur gemäß der dafür vorgesehenen Spezifikation verwendet.

**Vom-Standard-vorgegebene Werte:**

| Worttyp | Code Bits | Funktion | Bedeutung | Spec-Referenz |
|---|---|---|---|---|
| THRU | 001 | multiple (and indirect routing) | present multiple direct destinations for group calls (and future indirect relays, reserved) | Table A-VIII |
| TO | 010 | direct routing | present direct destination for individual and net calls | Table A-VIII |
| CMD | 110 | orderwire control and status | ALE system-wide station (and operator) orderwire for coordination, control, status, and special functions | Table A-VIII |
| FROM | 100 | identification (and indirect routing) | identification of present transmitter without termination (and past originator and relayers, reserved) | Table A-VIII |
| TIS | 101 | terminator and identification continuing | identification of present transmitter, signal terminations, protocol continuation | Table A-VIII |
| TWAS | 011 | terminator and identification quitting | identification of present transmitter, signal and protocol termination | Table A-VIII |
| DATA | 000 | extension and information | extension of data field of the previous ALE word, or information defined by the previous CMD | Table A-VIII |
| REP | 111 | duplication and information | duplication of the previous preamble, or information defined by the previous CMD | Table A-VIII |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-01 — Die in A.5.8.1.2 referenzierten optionalen AQC-ALE-Preambles sind im gelieferten Text nicht enthalten.
- OPEN-02 — Die in A.5.8.1.1 referenzierte AQC-ALE Address Word Structure ist im gelieferten Text nicht enthalten.

### 3.3 Address words — A.5.2.3.2

#### REQ-WORD-003 — TO für direkte Zieladressen

**Spec-Referenz:** A.5.2.3.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das TO-Wort muss als Routing-Designator die Adresse der aktuell vorgesehenen Zielstation oder Zielstationen angeben, die den Ruf direkt empfangen sollen. TO muss in Einzelrufprotokollen für einzelne Stationen und in Netrufprotokollen für mehrere Netmitgliedsstationen verwendet werden, die über eine einzige Netadresse gerufen werden. Das TO-Wort selbst muss die ersten drei Zeichen einer Adresse enthalten. Erweiterte Adressen müssen in unmittelbar folgenden, abwechselnden DATA- und REP-Wörtern enthalten sein. Die Folge muss TO, DATA, REP, DATA und REP sein und darf nur so lang sein, wie es zur Aufnahme der Adresse nötig ist, höchstens jedoch fünf Adresswörter mit insgesamt 15 Zeichen.

**Akzeptanzkriterien:**
- `AC-WORD-003-1` — TO wird für Einzelrufe und Netrufe gemäß Standard verwendet.
- `AC-WORD-003-2` — Ein TO-Wort enthält die ersten drei Zeichen der Adresse.
- `AC-WORD-003-3` — Erweiterte Adressen werden unmittelbar durch abwechselnde DATA- und REP-Wörter fortgesetzt.
- `AC-WORD-003-4` — Die zulässige Adressfolge endet spätestens bei fünf Adresswörtern beziehungsweise 15 Zeichen.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Maximale Anzahl Adresswörter | 5 | Wörter | A.5.2.3.2.1 |
| Maximale Adresslänge | 15 | Zeichen | A.5.2.3.2.1 |
| Grundlänge eines Adressworts | 3 | Zeichen | A.5.2.3.2.1 |

#### REQ-WORD-004 — TIS für die sendende Station und den Abschluss

**Spec-Referenz:** A.5.2.3.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das TIS-Wort muss als Routing-Designator die Adresse der aktuell sendenden oder sendenden Sounding-Station angeben. TIS muss, außer bei Verwendung von TWAS, in allen ALE-Protokollen zur Beendigung des ALE-Frames und der Übertragung verwendet werden. Es muss die Fortsetzung des Protokolls oder Handshakes anzeigen und andere Stationen je nach Protokoll veranlassen, anfordern oder einladen, zu antworten oder zu bestätigen. TIS muss zur Kennzeichnung des Call-Acceptance-Sounds verwendet werden. Das TIS-Wort selbst muss die ersten drei Zeichen der Adresse der rufenden Station enthalten. Erweiterte Adressen müssen in unmittelbar folgenden, abwechselnden DATA- und REP-Wörtern fortgesetzt werden. Die gesamte Adresse und der erforderliche Teil der TIS-, DATA-, REP-, DATA-, REP-Folge dürfen nur im Schlussabschnitt des ALE-Frames verwendet werden oder müssen ein vollständiges Sound bilden. TIS und TWAS dürfen nicht im selben Frame verwendet werden.

**Akzeptanzkriterien:**
- `AC-WORD-004-1` — TIS kennzeichnet die aktuell sendende Station.
- `AC-WORD-004-2` — TIS wird zur Beendigung des ALE-Frames und der Übertragung verwendet, außer wenn TWAS verwendet wird.
- `AC-WORD-004-3` — TIS kann Antworten oder Bestätigungen anfordern, veranlassen oder einladen.
- `AC-WORD-004-4` — TIS kennzeichnet den Call-Acceptance-Sound.
- `AC-WORD-004-5` — Ein TIS-Wort enthält die ersten drei Zeichen der Adresse der rufenden Station.
- `AC-WORD-004-6` — Erweiterte Adressen folgen unmittelbar in abwechselnden DATA- und REP-Wörtern.
- `AC-WORD-004-7` — Die gesamte Adresse wird nur im Schlussabschnitt des ALE-Frames verwendet oder bildet ein vollständiges Sound.
- `AC-WORD-004-8` — TIS und TWAS werden nicht im selben Frame verwendet.

#### REQ-WORD-005 — TWAS für die beendende Station

**Spec-Referenz:** A.5.2.3.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das TWAS-Wort muss als Routing-Designator genauso wie TIS verwendet werden, jedoch mit der Wirkung, dass es die Beendigung des ALE-Protokolls oder Handshakes anzeigt und Antworten oder Bestätigungen zurückweist, hemmt oder nicht einlädt. TWAS muss zur Kennzeichnung des Call-Rejection-Sounds verwendet werden. TIS und TWAS dürfen nicht im selben Frame verwendet werden.

**Akzeptanzkriterien:**
- `AC-WORD-005-1` — TWAS kennzeichnet die Beendigung des ALE-Protokolls oder Handshakes.
- `AC-WORD-005-2` — TWAS weist Antworten oder Bestätigungen zurück, hemmt sie oder lädt sie nicht ein.
- `AC-WORD-005-3` — TWAS kennzeichnet den Call-Rejection-Sound.
- `AC-WORD-005-4` — TIS und TWAS werden nicht im selben Frame verwendet.

#### REQ-WORD-006 — THRU für Gruppenrufe im Scanning Call

**Spec-Referenz:** A.5.2.3.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das THRU-Wort muss im Scanning-Call-Abschnitt des Calling Cycles ausschließlich mit Gruppenrufprotokollen verwendet werden. Es muss abwechselnd mit REP als Routing-Designator eingesetzt werden, um das erste Adresswort von Stationen zu kennzeichnen, die direkt gerufen werden sollen. Jedes solche Adresswort darf nur ein grundlegendes Adresswort mit drei Zeichen Länge sein. In einem Gruppenruf sind höchstens fünf verschiedene erste Adresswörter zulässig. Die Folge darf nur aus der Abfolge THRU, REP bestehen. THRU darf nicht für erweiterte Adressen verwendet werden und darf nicht innerhalb des Leading-Call-Abschnitts verwendet werden. Sobald der Leading Call im Gruppenruf beginnt, muss die gesamte Gruppe mit vollständigen Adressen gerufen werden, die mit TO-Präambeln und den zugehörigen Strukturen gesendet werden.

**Akzeptanzkriterien:**
- `AC-WORD-006-1` — THRU wird nur im Scanning Call von Gruppenrufprotokollen verwendet.
- `AC-WORD-006-2` — THRU und REP alternieren in der vorgegebenen Sequenz.
- `AC-WORD-006-3` — Jedes mit THRU referenzierte erste Adresswort ist auf drei Zeichen begrenzt.
- `AC-WORD-006-4` — Ein Gruppenruf enthält höchstens fünf verschiedene erste Adresswörter.
- `AC-WORD-006-5` — THRU wird nicht für erweiterte Adressen verwendet.
- `AC-WORD-006-6` — Im Leading Call eines Gruppenrufs werden vollständige Adressen mit TO verwendet.
- `AC-WORD-006-7` — Stationskonforme Systeme ignorieren Calls, die ihre Adresse in einem THRU-Wort außerhalb des Scanning Calls verwenden.
- `AC-WORD-006-8` — Der THRU-Präambelwert ist für zukünftige indirekte und Relais-Protokolle reserviert.
- `AC-WORD-006-9` — Der THRU-Präambelwert ist auch für AQC-ALE reserviert.

#### REQ-WORD-007 — FROM als optionale Kennung der sendenden Station

**Spec-Referenz:** A.5.2.3.2.5
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Das FROM-Wort ist ein optionaler Designator zur Identifikation der sendenden Station, ohne eine ALE-Frame-Beendigung wie TIS oder TWAS zu verwenden. Es muss die vollständige Adresse der sendenden Station enthalten; falls erforderlich, sind dafür zusätzlich DATA- und REP-Wörter zu verwenden, genau wie bei der TO-Adressstruktur. FROM sollte nur einmal pro ALE-Frame verwendet werden und nur unmittelbar vor einem CMD-Wort im Message-Abschnitt verwendet werden. Unter Vorgabe durch Operator oder Controller sollte FROM zur Bereitstellung einer schnellen Identifikation der sendenden Station eingesetzt werden, wenn die normale Schlusssequenz verzögert sein kann, etwa bei einem langen Message-Abschnitt.

**Akzeptanzkriterien:**
- `AC-WORD-007-1` — FROM identifiziert die sendende Station ohne Verwendung einer Frame-Beendigung wie TIS oder TWAS.
- `AC-WORD-007-2` — FROM enthält die vollständige Adresse der sendenden Station.
- `AC-WORD-007-3` — Falls erforderlich, werden für eine erweiterte FROM-Adresse DATA- und REP-Wörter verwendet.
- `AC-WORD-007-4` — FROM erscheint höchstens einmal pro ALE-Frame.
- `AC-WORD-007-5` — FROM erscheint nur unmittelbar vor CMD im Message-Abschnitt.
- `AC-WORD-007-6` — FROM kann zur schnellen Identifikation bei verzögerter normaler Schlusssequenz verwendet werden.
- `AC-WORD-007-7` — Stationskonforme Systeme ignorieren Abschnitte von Calls, die FROM-Wörter in anderer als der unmittelbar vor CMD stehenden Sequenz verwenden.
- `AC-WORD-007-8` — Der FROM-Präambelwert ist für zukünftige indirekte und Relais-Protokolle reserviert.
- `AC-WORD-007-9` — Der FROM-Präambelwert ist auch für AQC-ALE reserviert.

**Prioritäts-Begründung (1 Satz, falls nicht MUST):**
- FROM ist im Standard ausdrücklich als optionaler Designator beschrieben.

### 3.4 Message words — A.5.2.3.3

#### REQ-WORD-008 — CMD als Sonder-Designator für Message-Wörter

**Spec-Referenz:** A.5.2.3.3 / A.5.2.3.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle Message-Wörter müssen mit einem CMD-Präambelwort beginnen. CMD muss als Sonder-Designator für systemweite Koordination, Kommandierung, Kontrolle, Status, Information, Interoperation und andere Sonderzwecke verwendet werden. CMD muss für beliebige Kombinationen zwischen ALE-Stationen und Operatoren verwendbar sein. CMD ist nur innerhalb des Message-Abschnitts des ALE-Frames zulässig und muss innerhalb eines Frames einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz haben, damit die vorgesehenen Empfänger und der Sender eindeutig bestimmt werden. Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts. Die Orderwire-Funktionen werden mit CMD selbst oder in Kombination mit REP- und DATA-Message-Wörtern und den zugehörigen Funktionen ausgelöst.

**Akzeptanzkriterien:**
- `AC-WORD-008-1` — Jeder Message-Wort-Abschnitt beginnt mit CMD.
- `AC-WORD-008-2` — CMD unterstützt die im Standard genannten systemweiten Sonderzwecke.
- `AC-WORD-008-3` — CMD wird nur im Message-Abschnitt verwendet.
- `AC-WORD-008-4` — Ein CMD im Frame hat einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz.
- `AC-WORD-008-5` — Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts.
- `AC-WORD-008-6` — Orderwire-Funktionen können über CMD allein oder in Kombination mit REP und DATA ausgelöst werden.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-03 — Die in A.5.6 referenzierten Details zu CMD-Orderwire-Funktionen sind im gelieferten Text nicht enthalten.

### 3.5 Extension words — A.5.2.3.4

#### REQ-WORD-009 — DATA als Erweiterungs- und Informationswort

**Spec-Referenz:** A.5.2.3.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das DATA-Wort muss verwendet werden, um das Datenfeld eines vorherigen Worttyps zu erweitern, sofern dieses Wort nicht selbst DATA ist, oder um Informationen in einer Nachricht zu übertragen. In Verbindung mit TO, FROM, TIS oder TWAS muss DATA die Adresserweiterung von den ersten drei Zeichen auf sechs, neun oder mehr Zeichen in Vielfachen von drei ermöglichen, wenn es abwechselnd mit REP verwendet wird. Die ausgewählte Grenze für die Adresserweiterung beträgt insgesamt 15 Zeichen. In Verbindung mit CMD ist die Funktion von DATA durch den Standard für Message-Wörter und -Funktionen vorgegeben.

**Akzeptanzkriterien:**
- `AC-WORD-009-1` — DATA erweitert ein vorheriges Wortfeld, sofern das vorherige Wort nicht DATA ist.
- `AC-WORD-009-2` — DATA kann Informationen in einer Nachricht transportieren.
- `AC-WORD-009-3` — Mit TO, FROM, TIS oder TWAS erweitert DATA Adressen in Vielfachen von drei Zeichen.
- `AC-WORD-009-4` — Die Adresserweiterung endet spätestens bei 15 Zeichen.
- `AC-WORD-009-5` — Mit CMD gilt die standarddefinierte Funktion für Message-Wörter.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Maximale Adresslänge bei Erweiterung | 15 | Zeichen | A.5.2.3.4.1 |
| Erweiterungsschritt | 3 | Zeichen | A.5.2.3.4.1 |

#### REQ-WORD-010 — REP als Wiederholungs- und Erweiterungswort

**Spec-Referenz:** A.5.2.3.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das REP-Wort muss verwendet werden, um eine vorherige Präambelfunktion oder Wortbedeutung zu duplizieren, während der Inhalt des Datenfelds geändert wird. Jede Änderung von Wörtern oder Datenfeldbits erfordert eine Änderung der Präambelbits, um Unklarheit und Fehler zu vermeiden. Wenn sich ein Wort ändert, muss die Präambel auch dann geändert werden, wenn das Datenfeld identisch zum vorherigen Wort ist. In Verbindung mit TO muss REP eine Adresserweiterung ermöglichen, sodass mehr als eine Adresse spezifiziert werden kann. In Verbindung mit DATA darf REP zur Erweiterung und Vergrößerung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden. REP darf direkt nach jedem anderen Worttyp folgen, außer nach sich selbst und außer nach TIS oder TWAS.

**Akzeptanzkriterien:**
- `AC-WORD-010-1` — REP dupliziert die vorherige Präambelfunktion oder Wortbedeutung bei verändertem Datenfeldinhalt.
- `AC-WORD-010-2` — Jede Änderung von Wort oder Datenfeldbit erfordert eine Änderung der Präambel.
- `AC-WORD-010-3` — Ein Wortwechsel wird auch dann durch eine geänderte Präambel angezeigt, wenn das Datenfeld unverändert bleibt.
- `AC-WORD-010-4` — REP ermöglicht in Verbindung mit TO die Spezifikation mehrerer Adressen.
- `AC-WORD-010-5` — REP darf zur Erweiterung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden.
- `AC-WORD-010-6` — REP folgt nicht auf sich selbst sowie nicht auf TIS oder TWAS.
- `AC-WORD-010-7` — REP darf nur dort eingesetzt werden, wo keine Mehrfachsender-Situation entsteht.

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Konkrete Bit-Reihenfolge und Feldzuordnung gemäß Figure A-12 — Vorschlag Rückverweis: implements REQ-WORD-001
- Adress-Word-Formatter (TO/FROM/TIS/TWAS/DATA/REP) — Wortbildung und Wortwechsel auf Protokollebene; die Segmentierung in 3-Zeichen-Blöcke wird vor der Wortausgabe durchgeführt. Vorschlag Rückverweis: implements REQ-WORD-003, REQ-WORD-004, REQ-WORD-005, REQ-WORD-006, REQ-WORD-007, REQ-WORD-009, REQ-WORD-010
- Adress-Normalisierung aus Self-/Persistent-Memory — Geladene oder selbst referenzierte Adressen werden vor der Wortbildung in die standardkonforme Wortfolge überführt und bei Bedarf mit @ aufgefüllt. Vorschlag Rückverweis: implements REQ-ADDR-013, REQ-ADDR-014
- Präambelbit-Mapping P3/P2/P1 zu den Worttypen gemäß Table A-VIII — Vorschlag Rückverweis: implements REQ-WORD-002

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-04 — Die in A.5.8.1.1 und A.5.8.1.2 referenzierten AQC-ALE-Details sind im gelieferten Text nicht enthalten.

## 5. Fehlerkorrektur (FEC) — A.5.2.2

> **Spec-Stellen sammeln:** A.5.2.2.1 (General), A.5.2.2.2 (Golay coding), A.5.2.2.3 (Interleaving), A.5.2.2.4 (Redundant words).

### EPIC-FEC · Fehlerkorrektur

#### US-FEC-001

> Als ALE-Station will ich übertragene Wörter gegen Funkstörungen absichern, damit der Empfänger auch bei Bitfehlern den Originalinhalt rekonstruiert.

> **Hinweis zur Nummerierung:** REQ-FEC-001 bis REQ-FEC-003 aus dem ursprünglichen Template wurden durch die spec-basierten Anforderungen REQ-FEC-004 bis REQ-FEC-019 ersetzt. Die IDs 001–003 bleiben reserviert.

**Erfüllt durch:** REQ-FEC-004, REQ-FEC-005, REQ-FEC-006, REQ-FEC-007, REQ-FEC-008, REQ-FEC-009, REQ-FEC-010, REQ-FEC-011, REQ-FEC-012, REQ-FEC-013, REQ-FEC-014, REQ-FEC-015, REQ-FEC-016, REQ-FEC-017, REQ-FEC-018, REQ-FEC-019

---

#### REQ-FEC-004 — Kombinierte Fehlerbehandlungs-Funktionen

**Spec-Referenz:** A.5.2.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Funktionen Forward Error Correction, Interleaving und Redundanz sind im Sendencoder und Empfangsdecoder durchzuführen.

**Akzeptanzkriterien:**
- `AC-FEC-004-1` — Der Sendencoder muss FEC, Interleaving und Redundanz unterstützen
- `AC-FEC-004-2` — Der Empfangsdecoder muss FEC, Deinterleaving und Redundanzverarbeitung unterstützen

---

#### REQ-FEC-005 — FEC-Code-Typ

**Spec-Referenz:** A.5.2.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das extended Golay (24, 12, 3) Forward Error Correction Code ist vorgeschrieben.

**Akzeptanzkriterien:**
- `AC-FEC-005-1` — Der verwendete FEC-Code muss extended Golay (24, 12, 3) sein
- `AC-FEC-005-2` — Jeder codierte Block muss 24 Bits umfassen, wovon 12 Datenbits und 12 Paritybits bestehen
- `AC-FEC-005-3` — Der minimale Hamming-Abstand des Codes muss 3 betragen

---

#### REQ-FEC-006 — FEC-Generator-Polynom

**Spec-Referenz:** A.5.2.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Generator-Polynom für den FEC-Code generator ist festgelegt wie im Standard definiert.

**Akzeptanzkriterien:**
- `AC-FEC-006-1` — Das Generator-Polynom muss dem im Standard definierten Wert entsprechen

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Generator-Polynom g(x) | x¹¹ + x⁹ + x⁷ + x⁶ + x⁵ + x + 1 | — | A.5.2.2.2 |

---

#### REQ-FEC-007 — Generator-Matrix-Struktur

**Spec-Referenz:** A.5.2.2.2, Figure A-6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System muss den Extended Golay (24,12)-Code mit der vom Standard vorgeschriebenen systematischen Codewortstruktur implementieren.

**Akzeptanzkriterien:**
- `AC-FEC-007-1` — Die Codewortstruktur muss der im Standard definierten systematischen Form des Golay (24,12)-Codes entsprechen.

**→ FÜR FEATURE-DOKUMENT:** Generator-Matrix G mit I₁₂|P-Struktur (Figure A-6). Rückverweis: implements REQ-FEC-007

---

#### REQ-FEC-008 — Parity-Check-Matrix-Struktur

**Spec-Referenz:** A.5.2.2.2, Figure A-7
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System muss Übertragungsfehler anhand des vom Standard vorgeschriebenen Prüfverfahrens des Golay (24,12)-Codes erkennen.

**Akzeptanzkriterien:**
- `AC-FEC-008-1` — Das Fehlerprüfverfahren muss dem im Standard definierten Golay-Prüfverfahren entsprechen.

**→ FÜR FEATURE-DOKUMENT:** Parity-Check-Matrix H mit pᵀ|I₁₂-Struktur (Figure A-7). Rückverweis: implements REQ-FEC-008

---

#### REQ-FEC-009 — Kodier-Formel

**Spec-Referenz:** A.5.2.2.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System muss jedes 12-Bit-Datenwort gemäß dem vorgeschriebenen Golay-Kodierverfahren in ein 24-Bit-Codewort überführen.

**Akzeptanzkriterien:**
- `AC-FEC-009-1` — Jedes 12-Bit-Datenwort muss in ein 24-Bit-Golay-Codewort kodiert werden.
- `AC-FEC-009-2` — Das Codewort muss dem vom Standard definierten Kodierverfahren entsprechen.

**→ FÜR FEATURE-DOKUMENT:** Formel x = uG; Kodierung als modulo-2-Zeilen-Summe der G-Matrix. Rückverweis: implements REQ-FEC-009

---

#### REQ-FEC-010 — Dekodier-Formel und Syndrom-Bildung

**Spec-Referenz:** A.5.2.2.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System muss empfangene 24-Bit-Codewörter dekodieren und dabei Übertragungsfehler bis zur vorgeschriebenen Korrekturkapazität des Golay (24,12)-Codes erkennen und korrigieren.

**Akzeptanzkriterien:**
- `AC-FEC-010-1` — Das Dekodierverfahren muss dem vom Standard vorgeschriebenen Syndrom-basierten Golay-Dekodierverfahren entsprechen.
- `AC-FEC-010-2` — Jeder korrigierbare Fehlervektor muss eindeutig einem Syndromwert zugeordnet sein.
- `AC-FEC-010-3` — Das dekodierte 12-Bit-Datenwort muss dem gesendeten Original entsprechen, sofern die Fehleranzahl die Korrekturkapazität nicht überschreitet.

**→ FÜR FEATURE-DOKUMENT:** Formel s = yHᵀ; Syndrom als Index für Fehlervektor-Lookup-Tabelle. Rückverweis: implements REQ-FEC-010

---

#### REQ-FEC-011 — Fehler-Korrektur-Flag-Logik

**Spec-Referenz:** A.5.2.2.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Flags müssen entsprechend der Anzahl der korrigierten Fehler gesetzt werden. Wenn s ungleich 0 ist und e mehr Fehler enthält als vom Dekodiermodus korrigierbar, muss ein erkannter Fehler angezeigt und das entsprechende Flag gesetzt werden.

**Akzeptanzkriterien:**
- `AC-FEC-011-1` — Ein Flag muss bei erfolgreicher Korrektur gesetzt werden
- `AC-FEC-011-2` — Ein Flag muss bei einem nicht korrigierbaren, aber erkennbaren Fehler gesetzt werden

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-04 — Der Verweis auf "A.5.2.6" für die Flag-Verwendung ist enthalten, aber der Inhalt dieses Abschnitts wurde nicht geliefert. Die genaue Flag-Benennung und -Semantik ist hier nicht spezifiziert.
- ANNAHME: Das Präfix "FEC" wurde gewählt, da der Text aus Abschnitt A.5.2.2 (FEC) stammt.

---

#### REQ-FEC-012 — Interleaving-Vorgabe

**Spec-Referenz:** A.5.2.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Datenbits des Worts und die Golay-FEC-Bits müssen vor der Übertragung gemäß dem im Standard definierten Muster interleaved werden. Die obere Hälfte der FEC-Bits muss dabei invertiert werden.

**Akzeptanzkriterien:**
- `AC-FEC-012-1` — Die Bits W1 bis W24 müssen gemäß dem Standard-Interleaving-Muster umgeordnet werden
- `AC-FEC-012-2` — Die Golay FEC-Bits G13 bis G24 müssen vor dem Interleaving invertiert werden
- `AC-FEC-012-3` — Das Deinterleaving am Empfänger muss umkehrbar und verlustfrei sein

---

#### REQ-FEC-013 — Transmitted-Word-Struktur

**Spec-Referenz:** A.5.2.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein übertragenes Wort besteht aus 48 interleaved Bits plus einem 49ten Stuff-Bit S₄ (Wert = 0). Die Übertragung erfolgt als A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ mit 16⅓ Symbolen pro Wort. Das 49te Stuff-Bit wird vom Empfänger ignoriert.

**Akzeptanzkriterien:**
- `AC-FEC-013-1` — Ein übertragenes Wort muss aus 48 Nutzbits und einem Stuff-Bit bestehen
- `AC-FEC-013-2` — Das Stuff-Bit S₄₉ muss den Wert 0 haben
- `AC-FEC-013-3` — Die Übertragungssequenz muss dem Muster A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ entsprechen
- `AC-FEC-013-4` — Das empfangene 49te Stuff-Bit muss ignoriert werden

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Bits pro Wort | 48 | Bits | A.5.2.2.3 |
| Stuff-Bit (S₄₉) | 0 | — | A.5.2.2.3 |
| Symbole pro Wort | 16⅓ | Symbole | A.5.2.2.3 / A.5.1.3 |
| Übertragungssequenz | A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ | — | A.5.2.2.3 |

---

#### REQ-FEC-014 — Redundanz-Übertragung

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Jedes 49-Bit-Wort muss redundant (dreifach) übertragen werden, um die Auswirkungen von Fading, Interferenz und Rauschen zu reduzieren.

**Akzeptanzkriterien:**
- `AC-FEC-014-1` — Jedes Wort muss exakt dreimal übertragen werden
- `AC-FEC-014-2` — Die redundante Übertragung muss dazu dienen, Fading, Interferenz und Rauschen zu reduzieren

---

#### REQ-FEC-015 — Scanning-Call Redundanz-Dauer

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein individuelles oder Netz-Routing-Wort (TO...), das zum Rufen einer scannenden Station (oder eines Netzes) verwendet wird, muss so lange im Scanning-Call redundant gesendet werden, wie es im Scan erforderlich ist, um den Empfang sicherzustellen.

**Akzeptanzkriterien:**
- `AC-FEC-015-1` — Ein Scanning-Call-Wort muss solange wiederholt werden, bis der Empfang sichergestellt ist
- `AC-FEC-015-2` — Die Wiederholungsdauer muss dem im Scanning-Call definierten Zeitrahmen entsprechen

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-05 — Der Verweis auf "A.5.5.2" für die Scan-Call-Dauer ist enthalten, aber der Inhalt dieses Abschnitts wurde nicht geliefert. Die exakte Wiederholungsdauer ist hier nicht spezifiziert.

---

#### REQ-FEC-016 — Group-Call Redundanz-Sequenz

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei einem Gruppenruf (unter Verwendung von THRU und REP alternierend) müssen das erste individuelle Routing-Wort (THRU) und alle nachfolgenden individuellen Routing-Wörter (REP, THRU, ...) dreimal unmittelbar nacheinander gesendet werden. Diese Triple-Wörter für individuelle Stationen müssen in Gruppen-Sequenz rotiert werden.

**Akzeptanzkriterien:**
- `AC-FEC-016-1` — Das erste Routing-Wort im Group-Call muss THRU sein und dreimal wiederholt werden
- `AC-FEC-016-2` — Alle nachfolgenden Routing-Wörter müssen REP und THRU alternierend und jeweils dreimal wiederholt werden
- `AC-FEC-016-3` — Die Triple-Wörter müssen in Gruppen-Sequenz rotiert werden


**ZUORDNUNG DER ZUSTÄNDIGKEITEN:**
- Die dreifache, adjazente Aussendung ist eine Aufgabe der Transmit State Machine.
- Die Codierung des Wortinhalts bleibt Aufgabe des Wort-/FEC-Codecs.
- Die Mehrheitsbildung auf Empfangsseite bleibt Aufgabe des RX-Front-Ends vor Deinterleaving/FEC.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-06 — Der Verweis auf "A.5.5.3" für die Rotations-Sequenz ist enthalten, aber der Inhalt dieses Abschnitts wurde nicht geliefert. Die genaue Rotationslogik ist hier nicht spezifiziert.

---

#### REQ-FEC-017 — 2/3 Mehrheits-Entscheidung

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Empfänger muss bei Bit-Zeitintervallen das aktuelle Bit und den vergangenen Bit-Stream untersuchen und eine 2/3-Mehrheitsentscheidung auf Bit-Basis über einen Zeitraum von drei Wörtern durchführen.

**Akzeptanzkriterien:**
- `AC-FEC-017-1` — Eine 2/3-Mehrheitsentscheidung muss auf Bit-Basis über drei Wörter durchgeführt werden
- `AC-FEC-017-2` — Die Entscheidung muss sowohl das aktuelle Bit als auch die vergangenen Bits berücksichtigen

---

#### REQ-FEC-018 — Majority-Wort-Weitergabe

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die resultant 48 (ignoriert das 49te Bit) neuesten Majority-Bits müssen das neueste Majority-Wort bilden und an den Deinterleaver und FEC-Decoder übergeben werden.

**Akzeptanzkriterien:**
- `AC-FEC-018-1` — Das Majority-Wort muss exakt 48 Bits umfassen
- `AC-FEC-018-2` — Das Majority-Wort muss an den Deinterleaver übergeben werden
- `AC-FEC-018-3` — Das Majority-Wort muss an den FEC-Decoder übergeben werden

---

#### REQ-FEC-019 — Unanimous-Votes-Erfassung

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Anzahl der einstimmigen Stimmen der 48 möglichen Stimmen, die mit diesem Majority-Wort verbunden sind, müssen temporär für die Verwendung gemäß A.5.2.6 zurückgehalten werden.

**Akzeptanzkriterien:**
- `AC-FEC-019-1` — Die Anzahl der einstimmigen Stimmen muss für die 48 Bits erfasst werden
- `AC-FEC-019-2` — Die erfassten Werte müssen temporär gespeichert werden

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-07 — Der Verweis auf "A.5.2.6" für die Verwendung der unanimous votes ist enthalten, aber der Inhalt dieses Abschnitts wurde nicht geliefert. Die genaue Verwendung ist hier nicht spezifiziert.
- ANNAHME: Das Präfix "FEC" wurde gewählt, da der Text aus Abschnitt A.5.2.2 stammt.

---

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- **Generator-Matrix G (A.5.2.2.2, Fig. A-6)** — I₁₂/P-Struktur als Lookup/Tabelle. Vorschlag Rückverweis: implements REQ-FEC-007
- **Parity-Check-Matrix H (A.5.2.2.2, Fig. A-7)** — pᵀ/I₁₂-Struktur als Lookup/Tabelle. Vorschlag Rückverweis: implements REQ-FEC-008
- **Encoding x = uG (A.5.2.2.2.1)** — Matrix-Multiplikation als modulo-2-Zeilen-Summe. Vorschlag Rückverweis: implements REQ-FEC-009
- **Decoding s = yHᵀ (A.5.2.2.2.2)** — Syndrom-Bildung + Look-up-Tabelle für Fehlervektoren. Vorschlag Rückverweis: implements REQ-FEC-010
- **Interleaving-Pattern (A.5.2.2.3, Fig. A-10)** — Bit-Umordnung als Lookup/Tabelle. Vorschlag Rückverweis: implements REQ-FEC-012
- **Word-Layout A₁,B₁,...,S₄₉ (A.5.2.2.3)** — 49-Bit-Übertragungsframe. Vorschlag Rückverweis: implements REQ-FEC-013
- **Redundant-Word Scheduler / Tx State Machine (A.5.2.2.4, A.5.2.6.1)** — Emitiert jedes logische 49-Bit-Wort dreifach, adjazent und phasenkonstant; steuert Word-Order und Calling-Cycle-Reihenfolge. Vorschlag Rückverweis: implements REQ-FEC-014, REQ-FEC-015, REQ-FEC-016, REQ-SYNC-002, REQ-SYNC-003, REQ-SYNC-004
- **RX Majority-Voter Front-End (A.5.2.2.4)** — Führt die 2/3-Mehrheitsentscheidung auf Bit-Basis vor Deinterleaving/FEC aus. Vorschlag Rückverweis: implements REQ-FEC-017, REQ-FEC-018, REQ-FEC-019
- **Word-Codec (A.5.2.2.2 / A.5.2.2.3)** — Golay-Kodierung, Interleaving und 49-Bit-Wortaufbau ohne Scheduling der dreifachen Aussendung. Vorschlag Rückverweis: implements REQ-FEC-005, REQ-FEC-009, REQ-FEC-010, REQ-FEC-012, REQ-FEC-013
- **Unanimous Votes Counter (A.5.2.2.4)** — Zähler für einstimmige Stimmen. Vorschlag Rückverweis: implements REQ-FEC-019


## 6. Frame-Struktur — A.5.2.5

> **Spec-Stellen:** A.5.2.5 (Grundstruktur), A.5.2.5.1 (Calling cycle), A.5.2.5.2 (Message section), A.5.2.5.3 (Conclusion), A.5.2.5.4 (Valid sequences / Table A-XII), A.5.2.5.5 (Examples).

### EPIC-FRAME · Frame-Struktur

#### US-FRAME-001

> Als rufende Station will ich einen vollständigen ALE-Frame senden und empfangen, damit standardkonforme Rufe an scannende und nicht-scannende Gegenstellen möglich sind.

**Erfüllt durch:** REQ-FRAME-001, REQ-FRAME-002, REQ-FRAME-003, REQ-FRAME-004, REQ-FRAME-005, REQ-FRAME-006, REQ-FRAME-007, REQ-FRAME-008, REQ-FRAME-009, REQ-FRAME-010, REQ-FRAME-011, REQ-FRAME-012, REQ-FRAME-013

---

### 5.1 Frame-Grundstruktur — A.5.2.5

#### REQ-FRAME-001 — Frame-Grundstruktur und Wortbasis

**Spec-Referenz:** A.5.2.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle ALE-Übertragungen basieren auf den Tönen, dem Timing sowie den Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3. Jede Aussendung ist als „Frame" aufgebaut, der aus fortlaufenden redundanten Wörtern in gültigen Sequenzen besteht. Ein Frame setzt sich aus genau drei grundlegenden Abschnitten zusammen: Calling Cycle, Message und Conclusion. Die zulässigen Formate und Abschnittsgrenzen sind in Figure A-14, Table A-VII und A.5.5 festgelegt.

**Akzeptanzkriterien:**
- `AC-FRAME-001-1` — Jede ALE-Aussendung verwendet ausschließlich Töne, Timing sowie Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3.
- `AC-FRAME-001-2` — Jeder Frame besteht ausschließlich aus fortlaufenden redundanten Wörtern in gültigen Sequenzen.
- `AC-FRAME-001-3` — Jeder Frame enthält die drei Abschnitte Calling Cycle, Message und Conclusion in genau dieser Reihenfolge (wobei Message optional ist).
- `AC-FRAME-001-4` — Die Konstruktion eines Frames entspricht den Vorgaben aus Figure A-14, Table A-VII und den Format-Beschreibungen aus A.5.5.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-16 — Figure A-14 und Table A-VII wurden nicht geliefert; die darin enthaltenen gültigen Sequenz- und Größenregeln müssen bei Vorlage dieser Abschnitte ergänzt werden.
- OPEN-17 — A.5.5 (Frame-Formate) wurde nur teilweise geliefert; vollständige Format-Vorgaben sind dort zu verifizieren.

---

### 5.2 Calling Cycle — A.5.2.5.1

#### REQ-FRAME-002 — Calling Cycle: Gliederung und Bestandteile

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der initiale Abschnitt aller Frames — mit Ausnahme von Sounds — ist der Calling Cycle (Tcc). Dieser besteht aus zwei Teilen: dem Scanning Call (Tsc) und dem Leading Call (Tlc). Der Calling Cycle endet mit dem Beginn des Message-Abschnitts oder, sofern kein Message-Abschnitt vorhanden, mit dem Beginn des Conclusion-Abschnitts.

**Akzeptanzkriterien:**
- `AC-FRAME-002-1` — Jeder Frame (außer Sounds) beginnt mit einem Calling Cycle.
- `AC-FRAME-002-2` — Der Calling Cycle besteht aus genau zwei Teilen: Scanning Call und Leading Call.
- `AC-FRAME-002-3` — Der Calling Cycle endet mit dem Beginn des Message- oder, bei fehlendem Message-Abschnitt, des Conclusion-Abschnitts.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-18 — Die genaue Definition von „Sounds" als Frame-Ausnahme verweist auf A.5.3; Inhalt dort noch nicht vollständig extrahiert (vgl. REQ-SOUND-001).

---

#### REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Scanning Call setzt sich bei Individual- oder Net-Calls aus TO-Wörtern zusammen, die ausschließlich das erste Wort der Adresse der gerufenen Station(en) oder des Netzes enthalten. Bei einem Group Call besteht der Scanning Call abwechselnd aus THRU- und REP-Wörtern mit demselben eingeschränkten Adressinhalt. Der Satz unterschiedlicher Adress-Erstworte (Tcl) darf zur Abdeckung der Suchlaufperiode (Ts) so oft wie nötig wiederholt werden.

**Akzeptanzkriterien:**
- `AC-FRAME-003-1` — Bei Individual- und Net-Calls enthält der Scanning Call ausschließlich TO-Wörter mit dem ersten Adresswort der gerufenen Station(en).
- `AC-FRAME-003-2` — Bei Group Calls enthält der Scanning Call abwechselnd THRU- und REP-Wörter.
- `AC-FRAME-003-3` — Der Scanning Call enthält in beiden Fällen nur das jeweils erste Wort der Zieladresse, keine vollständige Adresse.
- `AC-FRAME-003-4` — Der Satz der Adress-Erstworte (Tcl) wird so oft wiederholt, bis die Suchlaufperiode (Ts) überschritten wird.
- `AC-FRAME-003-5` — Die Dauer des Scanning Call (Tsc) ist ≥ Ts.

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Konkrete Wiederholungszählung der Tcl-Blöcke und Abbruchbedingung ≥ Ts — Vorschlag Rückverweis: implements REQ-FRAME-003

---

#### REQ-FRAME-004 — Leading Call: Zusammensetzung und Adressinhalt

**Spec-Referenz:** A.5.2.5.1 / Figure A-15
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Leading Call besteht aus TO- und möglicherweise DATA- und REP-Wörtern, die die vollständige Adresse(n) der gerufenen Station(en) enthalten. Der Leading Call beginnt mit der Einleitung des Leading Call und endet mit dem Beginn des Message-Abschnitts bzw. des Conclusion-Abschnitts. Die Verwendung von REP und DATA ist in A.5.2.4 festgelegt.

**Akzeptanzkriterien:**
- `AC-FRAME-004-1` — Der Leading Call enthält die vollständige(n) Zieladresse(n) einschließlich aller Erweiterungswörter (DATA, REP).
- `AC-FRAME-004-2` — Der Leading Call beginnt mit dem Einleitungszeitpunkt des Leading Call und endet spätestens mit dem Beginn des Message- oder Conclusion-Abschnitts.
- `AC-FRAME-004-3` — Die Verwendung von DATA- und REP-Wörtern im Leading Call entspricht den Regeln aus A.5.2.4.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-19 — Figure A-15 wurde nicht geliefert; die visuelle Darstellung des Leading Call kann nicht verifiziert werden.

---

#### REQ-FRAME-005 — Frame-Synchronisation: Kein dediziertes Sync-Word

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein ALE-Frame besitzt kein dediziertes „Flag Word" oder „Sync Word" für die Frame-Synchronisation. Empfangsstationen können den ALE-Signalempfang und die Dekodierung an jedem beliebigen Punkt nach dem Beginn der Übertragung aufnehmen.

**Akzeptanzkriterien:**
- `AC-FRAME-005-1` — Der Sender fügt kein dediziertes Flag- oder Sync-Wort zur Frame-Synchronisation ein.
- `AC-FRAME-005-2` — Eine Empfangsstation ist in der Lage, das ALE-Signal an einem beliebigen Punkt nach Übertragungsbeginn zu synchronisieren und zu lesen.

---

#### REQ-FRAME-006 — Sender-Hochlaufzeit: 90-Prozent-Leistung

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Sender muss innerhalb von 2,5 ms nach der ersten Tonübertragung nach Rufinitiierung mindestens 90 Prozent der gewählten HF-Sendeleistung erreicht haben. Die zulässige Verzögerung von 2,5 ms gilt zusätzlich zu der in 5.3.5.1 festgelegten Attackzeit. Nichterfüllung der 90-%-Bedingung beeinträchtigt die Verlinkungswahrscheinlichkeit; die Bedingung gilt als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist.

**Akzeptanzkriterien:**
- `AC-FRAME-006-1` — Die Sendeleistung beträgt spätestens 2,5 ms nach dem ersten ALE-Ton mindestens 90 % der gewählten HF-Leistung.
- `AC-FRAME-006-2` — Die zulässige 2,5-ms-Verzögerung ist additiv zur Attackzeit aus 5.3.5.1.
- `AC-FRAME-006-3` — Die Einhaltung gilt auch dann als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist (Compliance-Äquivalenz).

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Permissible power delay after first tone | 2,5 | ms | A.5.2.5.1 Note 2 |
| Mindest-Leistungsanteil bei Erstem Ton | 90 | % | A.5.2.5.1 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-20 — Der referenzierte Abschnitt 5.3.5.1 (allowable attack time) wurde nicht geliefert; die zusätzlich erlaubte Attackzeit ist dort zu verifizieren.
- OPEN-21 — Table A-I (Verlinkungswahrscheinlichkeitskriterium) wurde nicht geliefert; die Compliance-Äquivalenz kann nicht vollständig geprüft werden.

---

### 5.3 Message-Abschnitt — A.5.2.5.2

#### REQ-FRAME-007 — Quick-ID: Optionaler Transmitter-Identifier am Beginn des Message-Abschnitts

**Spec-Referenz:** A.5.2.5.1 / A.5.2.5.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Der Übergang vom Calling Cycle zum Message-Abschnitt kann optional durch eine Quick-ID markiert werden. Die Quick-ID belegt die ersten Wörter des Message-Abschnitts, nach dem Leading Call und vor dem restlichen Message-Inhalt (oder dem Conclusion-Abschnitt, sofern kein weiterer Message-Inhalt folgt). Die Quick-ID besteht aus FROM- und möglicherweise REP- und DATA-Wörtern, die die vollständige Adresse des Senders enthalten. Die Quick-ID darf nur einmalig am Beginn der CMD-Message-Sequenzen verwendet werden und niemals ohne nachfolgende CMD-Message(s).

**Akzeptanzkriterien:**
- `AC-FRAME-007-1` — Eine Quick-ID, sofern verwendet, steht ausschließlich an der ersten Position des Message-Abschnitts, nach dem Leading Call.
- `AC-FRAME-007-2` — Die Quick-ID besteht aus FROM- und möglicherweise REP- und DATA-Wörtern mit der vollständigen Senderadresse.
- `AC-FRAME-007-3` — Die Quick-ID wird höchstens einmal pro Frame verwendet, am Beginn der CMD-Message-Sequenz.
- `AC-FRAME-007-4` — Eine Quick-ID wird niemals ohne mindestens eine nachfolgende CMD-Message gesendet.
- `AC-FRAME-007-5` — Wird keine Quick-ID verwendet, beginnt der Message-Abschnitt direkt mit dem ersten CMD-Wort.

**PRIORITÄTS-BEGRÜNDUNG:**
- Der Standard beschreibt die Quick-ID als optional; sie wird empfohlen, wenn die Länge des Message-Abschnitts ein Anliegen ist.

---

#### REQ-FRAME-008 — Message-Abschnitt: Struktur und Zusammensetzung

**Spec-Referenz:** A.5.2.5.2 / Figure A-16
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Message-Abschnitt ist der zweite und optionale Abschnitt aller Frames (außer Sounds). Er besteht — abgesehen von der optionalen Quick-ID — aus CMD- und möglicherweise REP- und DATA-Wörtern, vom Ende des Calling Cycle bis zum Beginn des Conclusion-Abschnitts. Der Message-Abschnitt beginnt stets mit dem ersten CMD-Wort (oder, bei verwendeter Quick-ID, mit dem FROM-Wort, gefolgt von CMD-Wörtern). Der Message-Abschnitt wird innerhalb eines Calls nicht wiederholt, auch wenn Nachrichten oder Informationen innerhalb des Message-Abschnitts selbst wiederholt sein können. Die Verwendung von REP und DATA ist in A.5.7.3 festgelegt.

**Akzeptanzkriterien:**
- `AC-FRAME-008-1` — Der Message-Abschnitt enthält — abgesehen von der optionalen Quick-ID — ausschließlich CMD-, REP- und DATA-Wörter.
- `AC-FRAME-008-2` — Der Message-Abschnitt beginnt immer mit dem ersten CMD-Wort oder, bei Quick-ID, mit dem FROM-Wort.
- `AC-FRAME-008-3` — Der Message-Abschnitt wird innerhalb eines Calls genau einmal gesendet (keine Wiederholung des gesamten Abschnitts).
- `AC-FRAME-008-4` — Der Message-Abschnitt erstreckt sich vom Ende des Calling Cycle bis zum Beginn des Conclusion-Abschnitts.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-22 — Figure A-16 wurde nicht geliefert; die visuelle Darstellung des Message-Abschnitts kann nicht verifiziert werden.
- OPEN-23 — A.5.7.3 (Verwendung von REP und DATA im Message-Abschnitt) wurde nicht vollständig geliefert; Detailregeln sind dort zu verifizieren.

---

#### REQ-FRAME-009 — Message-Abschnitt: AQC-ALE-Sonderregel

**Spec-Referenz:** A.5.2.5.2 / A.5.8.2.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Im Rahmen von AQC-ALE ist der Message-Abschnitt verfügbar, wenn sich das System im Link-Zustand befindet. Das dritte Bein (Acknowledgement Leg) eines Calls kann dabei als Inlink-Entry-Bedingung genutzt werden.

**Akzeptanzkriterien:**
- `AC-FRAME-009-1` — Bei AQC-ALE steht der Message-Abschnitt nur im Link-Zustand zur Verfügung.
- `AC-FRAME-009-2` — Das Acknowledgement Leg (drittes Bein) eines Calls kann als Inlink-Entry-Bedingung verwendet werden.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-24 — A.5.8.2.3 wurde nicht vollständig geliefert; die genauen Regeln für die AQC-ALE-Inlink-Entry-Bedingung sind dort zu verifizieren.

**PRIORITÄTS-BEGRÜNDUNG:**
- AQC-ALE ist als optional im Standard beschrieben (vgl. EPIC-AQC, aktuell nicht im Scope).

---

### 5.4 Conclusion — A.5.2.5.3

#### REQ-FRAME-010 — Conclusion: Struktur und Worttypen

**Spec-Referenz:** A.5.2.5.3 / Figure A-17
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Conclusion ist der dritte Abschnitt aller Frames. Sie besteht aus entweder TIS- oder TWAS-Wörtern (aber nicht aus beiden) sowie möglicherweise DATA- und REP-Wörtern. Sie beginnt nach dem Ende des Message-Abschnitts bzw. nach dem Ende des Calling Cycle, wenn kein Message-Abschnitt vorhanden ist, und erstreckt sich bis zum Ende des Calls. Sowohl Conclusions als auch Sounds enthalten die vollständige Adresse der sendenden Station.

**Akzeptanzkriterien:**
- `AC-FRAME-010-1` — Die Conclusion enthält entweder ausschließlich TIS-Wörter oder ausschließlich TWAS-Wörter als Typ-Anker — niemals beide Typen im selben Frame.
- `AC-FRAME-010-2` — Die Conclusion kann zusätzlich DATA- und REP-Wörter enthalten.
- `AC-FRAME-010-3` — Die Conclusion enthält die vollständige Adresse der sendenden Station.
- `AC-FRAME-010-4` — Liegt kein Message-Abschnitt vor, beginnt die Conclusion unmittelbar nach dem Calling Cycle.
- `AC-FRAME-010-5` — Die Conclusion erstreckt sich bis zum Ende des Calls.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-25 — Figure A-17 wurde nicht geliefert; die visuelle Darstellung der Conclusion kann nicht verifiziert werden.

---

#### REQ-FRAME-011 — Conclusion bei Sounds und Ausnahme-Frames

**Spec-Referenz:** A.5.2.5.3 / A.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sounds und Exception-Frames beginnen unmittelbar mit TIS- oder TWAS-Wörtern, ohne vorangehenden Calling Cycle oder Message-Abschnitt, gemäß den Regeln in A.5.3. Ein REP-Wort darf nicht unmittelbar auf ein TIS- oder TWAS-Wort folgen.

**Akzeptanzkriterien:**
- `AC-FRAME-011-1` — Bei Sounds und Exception-Frames beginnt die Aussendung direkt mit TIS- oder TWAS-Wörtern.
- `AC-FRAME-011-2` — Ein REP-Wort folgt niemals unmittelbar auf ein TIS- oder TWAS-Wort.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-26 — A.5.3 (Sounds) wurde noch nicht vollständig extrahiert; die vollständigen Regeln für den Sonderfall Sound-Frame sind dort zu vervollständigen (vgl. REQ-SOUND-001).

---

### 5.5 Gültige Sequenzen und Frame-Limits — A.5.2.5.4

#### REQ-FRAME-012 — Gültige Wortsequenzen im Frame

**Spec-Referenz:** A.5.2.5.4 / Figure A-18 / Figure A-19 / Figure A-20
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die acht ALE-Worttypen dürfen zur Konstruktion von Frames und Messages nur in den Sequenzen verwendet werden, die in Figure A-18, Figure A-19 und Figure A-20 erlaubt sind.

**Akzeptanzkriterien:**
- `AC-FRAME-012-1` — Jede gesendete Wortsequenz in einem Frame oder einer Message entspricht den erlaubten Sequenzen aus Figure A-18, Figure A-19 und Figure A-20.
- `AC-FRAME-012-2` — Jede empfangene Wortsequenz wird gegen die erlaubten Sequenzen validiert; unzulässige Sequenzen werden verworfen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-27 — Figure A-18, Figure A-19 und Figure A-20 wurden nicht geliefert; die vollständigen Sequenz-Regeln können nicht extrahiert werden. Dieser REQ bleibt bis zur Lieferung dieser Figures unvollständig.

---

#### REQ-FRAME-013 — Frame-Größen- und Zeitgrenzen (Table A-XII)

**Spec-Referenz:** A.5.2.5.4 / Table A-XII
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Größe und Dauer von ALE-Frames und ihren Abschnitten sind durch die in Table A-XII definierten Grenzwerte begrenzt. Diese Grenzen gelten vorbehaltlich etwaiger Erweiterungen durch AMD-Extension, DTM oder DBM.

**Akzeptanzkriterien:**
- `AC-FRAME-013-1` — Die Adressgröße überschreitet nicht 5 Wörter (Ta max = 1960 ms).
- `AC-FRAME-013-2` — Die Call-Zeit (Tc, als halbe Tlc) überschreitet nicht 12 Wörter (4704 ms).
- `AC-FRAME-013-3` — Die maximale Suchlaufperiode (Ts max) überschreitet nicht 50 s.
- `AC-FRAME-013-4` — Die Basiszeit des Message-Abschnitts (Tm max basic) überschreitet nicht 11,76 s, sofern keine AMD-Extension, DTM oder DBM angewendet wird.
- `AC-FRAME-013-5` — Bei Verwendung von AMD (90 Zeichen) überschreitet die Message-Zeit nicht 11,76 s.
- `AC-FRAME-013-6` — Bei Verwendung von DTM (1053 Zeichen) überschreitet die Message-Zeit nicht 2,29 min (gesamter Datenblock).
- `AC-FRAME-013-7` — Bei Verwendung von DBM (37377 Zeichen) überschreitet die Message-Zeit nicht 23,26 min (gesamter tief-interleaved Block).

**Vom-Standard-vorgegebene Werte:**

| Parameter | Grenzwert | Wert | Einheit | Spec-Referenz |
|---|---|---|---|---|
| Ta max (Adressgröße) | 5 Wörter | 1960 | ms | Table A-XII |
| Tc max (Call-Zeit, ½ Tlc) | 12 Wörter | 4704 | ms | Table A-XII |
| Ts max (Suchlaufperiode) | — | 50 | s | Table A-XII |
| Tm max basic | — | 11,76 | s | Table A-XII |
| Tm max AMD (90 Zeichen) | — | 11,76 | s | Table A-XII |
| Tm max DTM (1053 Zeichen) | — | 2,29 | min | Table A-XII |
| Tm max DBM (37377 Zeichen) | — | 23,26 | min | Table A-XII |

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Sender-Hochlauflogik: Doppeltes Senden der Scanning-Erstworte als „Dummy"-Phase während des Sender-Hochlaufs (Implementierungsoption laut Note 1 zu A.5.2.5.1) — Vorschlag Rückverweis: implements REQ-FRAME-006
- Alternativ: Einsatz des ALE-Signals als Stimmton für den Tuner während des Hochlaufs (Implementierungsoption laut Note 1 zu A.5.2.5.1) — Vorschlag Rückverweis: implements REQ-FRAME-006
- Konkrete Sequenz-Automaten für erlaubte Wortfolgen (aus Figure A-18/19/20, noch nicht geliefert) — Vorschlag Rückverweis: implements REQ-FRAME-012

---

## 7. Synchronization — A.5.2.6

> **Spec-Stellen:** A.5.2.6 (Einleitung / Übersicht), A.5.2.6.1 (Transmit word phase), A.5.2.6.2 (Receiver word sync), A.5.2.6.3 (Synchronization criteria).

### EPIC-SYNC · Synchronization

#### US-SYNC-001

> Als ALE-Station will ich alle Sende- und Empfangsphasen exakt nach den Standard-Zeiten takten, damit die Interoperabilität mit anderen ALE-Geräten gewährleistet ist.

**Erfüllt durch:** REQ-SYNC-001, REQ-SYNC-002, REQ-SYNC-003, REQ-SYNC-004, REQ-SYNC-005, REQ-SYNC-006, REQ-SYNC-007

---

### 6.1 Systemcharakter der Synchronisation — A.5.2.6

#### REQ-SYNC-001 — Asynchroner Systembetrieb und Frame-interne Word-Sync

**Spec-Referenz:** A.5.2.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System ist inhärent asynchron und erfordert keine systemweite Synchronisation, ist jedoch mit solchen Verfahren kompatibel. Die innerhalb eines Frames eingebettete Timing- und Strukturinformation stellt die notwendigen Ankerpunkte bereit, um die Wortsynchronisation (Word Sync) während Linking-, Orderwire- und Anti-Interferenz-Funktionen zu erzielen und aufrechtzuerhalten.

**Akzeptanzkriterien:**
- `AC-SYNC-001-1` — Das System erfordert keine systemweite externe Synchronisation und muss auch ohne diese korrekt arbeiten.
- `AC-SYNC-001-2` — Das System ist mit Verfahren zur systemweiten Synchronisation kompatibel, sofern solche eingesetzt werden.
- `AC-SYNC-001-3` — Die innerhalb eines Frames eingebettete Timing- und Strukturinformation wird zur Erzielung und Aufrechterhaltung der Word-Sync während Linking-, Orderwire- und Anti-Interferenz-Funktionen genutzt.

---

### 6.2 Sendeseitige Wortphase — A.5.2.6.1

#### REQ-SYNC-002 — Konstante Wortphasenbeziehung im Sendemodulator

**Spec-Referenz:** A.5.2.6.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der ALE-Sendemodulator akzeptiert digitale Daten vom Encoder und gibt moduliertes Basisband-Audio an den Sender weiter. Nach dem Beginn der ersten Übertragung einer Station muss der ALE-Sendemodulator eine konstante Phasenbeziehung — innerhalb der vorgeschriebenen Timing-Genauigkeit — zwischen allen gesendeten dreifach-redundanten Wörtern zu jedem Zeitpunkt aufrechterhalten, bis der letzte Frame der Übertragung beendet ist. Es gilt:

T(späteres dreifach-redundantes Wort) − T(früheres dreifach-redundantes Wort) = n × Trw

wobei T( ) der Ereigniszeitpunkt eines gegebenen dreifach-redundanten Worts innerhalb eines beliebigen Frames ist, Trw die Periode von drei Wörtern (392 ms) ist und n eine beliebige ganze Zahl ist.

**Akzeptanzkriterien:**
- `AC-SYNC-002-1` — Nach Beginn der ersten Übertragung hält der Sendemodulator die konstante Phasenbeziehung zwischen allen gesendeten dreifach-redundanten Wörtern aufrecht.
- `AC-SYNC-002-2` — Die Differenz der Ereigniszeitpunkte zweier dreifach-redundanter Wörter innerhalb einer Übertragung ist stets ein ganzzahliges Vielfaches von Trw = 392 ms.
- `AC-SYNC-002-3` — Die Phasenbeziehung wird bis zum Ende des letzten Frames der Übertragung gehalten.
- `AC-SYNC-002-4` — Die Phasenbeziehung wird innerhalb der in A.5.1.4 vorgeschriebenen Timing-Genauigkeit eingehalten.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Trw (Redundant Word Period) | 392 | ms | A.5.2.6.1 |
| n | beliebige ganze Zahl | — | A.5.2.6.1 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-28 — Die vorgeschriebene Timing-Genauigkeit ist in A.5.1.4 definiert; der zugehörige Abschnitt wurde bereits extrahiert (REQ-WAVEFORM-013, 10 ppm).

---

#### REQ-SYNC-003 — Word-Phase-Tracking nur innerhalb einer Übertragung

**Spec-Referenz:** A.5.2.6.1 (NOTE)
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Word-Phase-Tracking wird ausschließlich innerhalb einer Übertragung durchgeführt und nicht zwischen separaten Übertragungen.

**Akzeptanzkriterien:**
- `AC-SYNC-003-1` — Das Word-Phase-Tracking ist auf die Dauer einer einzelnen Übertragung begrenzt.
- `AC-SYNC-003-2` — Zwischen zwei getrennten Übertragungen wird keine fortgesetzte Wortphasenbeziehung aufrechterhalten.

---

#### REQ-SYNC-004 — Unabhängigkeit des Sendemodulators vom Empfänger

**Spec-Referenz:** A.5.2.6.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die interne Wortphasenreferenz des Sendemodulators muss vom Empfänger unabhängig sein und muss innerhalb der geforderten Genauigkeit selbsttaktend (self-timed) arbeiten. Auf A.5.1.4 wird verwiesen.

**Akzeptanzkriterien:**
- `AC-SYNC-004-1` — Die Wortphasenreferenz des Sendemodulators ist zu keinem Zeitpunkt von der Empfängeraktivität abhängig.
- `AC-SYNC-004-2` — Der Sendemodulator arbeitet selbsttaktend innerhalb der in A.5.1.4 vorgeschriebenen Genauigkeit.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-29 — Annahme: Der Verweis auf A.5.1.4 betrifft die in REQ-WAVEFORM-013 bereits erfasste 10-ppm-Genauigkeit.

---

### 6.3 Empfangsseitige Wortsynchronisation — A.5.2.6.2

#### REQ-SYNC-005 — Empfangsdemodulator: Signalakquisition, Tracking und Demodulation

**Spec-Referenz:** A.5.2.6.2 / Figure A-11
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Empfangsdemodulator akzeptiert Basisband-Audio vom Empfänger, akquiriert, verfolgt und demoduliert ALE-Signale und stellt die zurückgewonnenen digitalen Daten den Decodern bereit. Im Datenblocknachrichten-Modus (DBM-Modus) muss der Empfangsdemodulator zusätzlich in der Lage sein, einzelne Datenbits für das tiefe Deinterleaving und Dekodieren zu lesen.

**Akzeptanzkriterien:**
- `AC-SYNC-005-1` — Der Empfangsdemodulator akquiriert ALE-Signale aus dem Basisband-Audio.
- `AC-SYNC-005-2` — Der Empfangsdemodulator verfolgt (trackt) ALE-Signale und demoduliert sie.
- `AC-SYNC-005-3` — Der Empfangsdemodulator stellt die zurückgewonnenen digitalen Daten den Decodern bereit.
- `AC-SYNC-005-4` — Im DBM-Modus ist der Empfangsdemodulator in der Lage, einzelne Datenbits für das tiefe Deinterleaving und Dekodieren zu lesen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-30 — Figure A-11 (Blockdiagramm des Empfangsdemodulators) wurde nicht geliefert; die dort dargestellte Systemstruktur kann nicht verifiziert werden.
- OPEN-31 — Die genauen Regeln für das DBM-Deinterleaving verweisen auf A.5.7 (DBM-Modus); dieser Inhalt wurde nicht vollständig geliefert.

---

### 6.4 Synchronisationskriterien — A.5.2.6.3

#### REQ-SYNC-006 — Synchronisationskriterien für jedes ALE-Wort

**Spec-Referenz:** A.5.2.6.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Decoder akzeptiert digitale Daten vom Empfangsdemodulator und führt Deinterleaving, Dekodierung, FEC und Datenprüfung durch. Zur anfänglichen und fortlaufenden Synchronisation sollten alle der folgenden Kriterien verwendet werden, um jedes ALE-Wort zu diskriminieren und zu lesen:

1. Erreichen oder Überschreiten eines Schwellwerts für einstimmige Stimmen (unanimous votes) im 2/3-Mehrheitsvoten-Decoder
2. Erfolgreiche Golay-Dekodierung der „A"-Wort-Bits
3. Erfolgreiche Golay-Dekodierung der „B"-Wort-Bits
4. Akzeptable Präambel gemäß gültigen Wortsequenzen (Figure A-14)
5. Akzeptable erste Zeichenbits (aus dem Basic-38-ASCII-Subset)
6. Akzeptable zweite Zeichenbits (aus dem Basic-38-ASCII-Subset)
7. Akzeptable dritte Zeichenbits (aus dem Basic-38-ASCII-Subset)
8. Historik, Status, Erwartungen und Protokoll
9. Korrekte dreifach-redundante Wortphase

Alle Kriterien zusammen müssen erfüllt sein, um ein Wort zu akzeptieren.

**Akzeptanzkriterien:**
- `AC-SYNC-006-1` — Alle neun der oben genannten Kriterien werden gemeinsam zur Diskriminierung und zum Lesen jedes ALE-Worts angewendet.
- `AC-SYNC-006-2` — Ein Wort wird nur dann akzeptiert, wenn sämtliche angewendeten Kriterien erfüllt sind.
- `AC-SYNC-006-3` — Der unanimous-vote-Schwellwert wird als leicht anpassbares BER-Signal-Qualitäts-Diskriminierungsmittel verwendet.
- `AC-SYNC-006-4` — Eine erfolgreiche Golay-Dekodierung zeigt an, dass alle erkannten Bitfehler innerhalb der Korrekturkapazität des FEC-Codes lagen und der Unkorrigierbar-Fehler-Flag nicht aufgetreten ist.
- `AC-SYNC-006-5` — Akzeptable Präambeln sind solche, die innerhalb der Grenzen dieses Standards liegen, wie in A.5.2.3.1.3 definiert.
- `AC-SYNC-006-6` — Akzeptable Zeichen bedeuten, dass jedes Zeichen innerhalb des zugehörigen ASCII-Subsets liegt.
- `AC-SYNC-006-7` — Alle drei Zeichen müssen innerhalb des Basic-38-ASCII-Subsets liegen, wenn eine Routing-Präambel (z. B. TO) dekodiert wurde.
- `AC-SYNC-006-8` — Bei einem initialen REP ist eine beliebige Bitkombination bedingt akzeptabel; ohne die notwendige Kenntnis des vorherigen Worts wird sie in den meisten Fällen als irrelevant betrachtet und verworfen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-32 — Figure A-14 (gültige Wortsequenzen) wurde nicht geliefert; die dort definierten akzeptablen Präambeln können nicht vollständig verifiziert werden (vgl. OPEN-27).
- OPEN-33 — Die Note zur Anwendung jeder Präambel in A.5.2.6.3 beschreibt Beispielfälle (uncommitted station, TO, THRU, REP, CMD, TIS, TWAS); die vollständige Protokolllogik für alle Präambeltypen hängt von den noch nicht gelieferten Abschnitten A.5.5 und A.5.3 ab.

---

#### REQ-SYNC-007 — Herstelleroptimierbare Parameter: Unanimous-Vote-Schwellwert und Golay-Modus

**Spec-Referenz:** A.5.2.6.3
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Die Anzahl der einstimmigen Stimmen stellt einen einfach anpassbaren Qualitäts-Diskriminator dar; der Schwellwert sollte vom Hersteller zur Leistungsoptimierung gewählt werden. Die Korrekturkapazität (Modus) des Golay-Codes sollte vom Hersteller zur Leistungsoptimierung gewählt werden und kann einen der vier Modi verwenden: (3/4, 2/5, 1/6, 0/7), wobei n/m bedeutet, dass bis zu „n" Fehler erkannt und korrigiert werden oder bis zu „m" Fehler erkannt, aber nicht korrigierbar sind. Als Designziel (DO) sollte eine automatische Anpassung des unanimous-vote-Schwellwerts und des Golay-Modus vorgesehen werden, um die Leistung unter wechselnden Bedingungen zu optimieren.

**Akzeptanzkriterien:**
- `AC-SYNC-007-1` — Das System bietet die Möglichkeit, den unanimous-vote-Schwellwert zur Leistungsoptimierung einzustellen.
- `AC-SYNC-007-2` — Das System unterstützt alle vier Golay-Korrekturmodi: (3/4), (2/5), (1/6), (0/7).
- `AC-SYNC-007-3` — Als Designziel (DO) wird eine automatische Anpassung von unanimous-vote-Schwellwert und Golay-Modus unter wechselnden Bedingungen bereitgestellt.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Golay-Modus 3/4 | bis zu 3 Fehler korrigierbar, bis zu 4 erkennbar | — | A.5.2.6.3 |
| Golay-Modus 2/5 | bis zu 2 Fehler korrigierbar, bis zu 5 erkennbar | — | A.5.2.6.3 |
| Golay-Modus 1/6 | bis zu 1 Fehler korrigierbar, bis zu 6 erkennbar | — | A.5.2.6.3 |
| Golay-Modus 0/7 | keine Korrektur, bis zu 7 erkennbar | — | A.5.2.6.3 |

**PRIORITÄTS-BEGRÜNDUNG:**
- Der unanimous-vote-Schwellwert und der Golay-Modus sind als hersteller-optimierbare Parameter beschrieben; die automatische Anpassung ist explizit als Designziel (DO), nicht als Shall-Anforderung, formuliert.

---

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- **Word-Phase-Tracking-Mechanismus (A.5.2.6.1)** — Konkrete Implementierung des selbsttaktenden Phasen-Referenz-Counters im Sendemodulator; begrenzt auf eine Übertragung, nicht zwischen Übertragungen. Vorschlag Rückverweis: implements REQ-SYNC-002, REQ-SYNC-003, REQ-SYNC-004
- **Redundant-Word Scheduler / Transmit State Machine (A.5.2.2.4, A.5.2.6.1)** — Erzwingt die adjazente 3×-Aussendung je logischem Wort und hält die Wortabfolge im Calling Cycle ein. Vorschlag Rückverweis: implements REQ-FEC-014, REQ-FEC-015, REQ-FEC-016, REQ-SYNC-002, REQ-SYNC-003, REQ-SYNC-004
- **RX Majority-Voter Front-End (A.5.2.2.4)** — Führt die 2/3-Mehrheitsbildung vor Deinterleaving/FEC aus und liefert das Majority-Wort an den Decoder. Vorschlag Rückverweis: implements REQ-FEC-017, REQ-FEC-018, REQ-FEC-019
- **DBM-Einzelbit-Lese-Mechanismus (A.5.2.6.2)** — Mechanismus zum Lesen einzelner Datenbits für Deep-Deinterleaving im DBM-Modus. Vorschlag Rückverweis: implements REQ-SYNC-005
- **Unanimous-Vote-Schwellwert-Konfiguration (A.5.2.6.3)** — Konfigurierbare Schwellwert-Logik inkl. optionaler automatischer Anpassung. Vorschlag Rückverweis: implements REQ-SYNC-007
- **Golay-Modus-Auswahl (A.5.2.6.3)** — Auswahl und optionale Autoanpassung des Golay-Korrekturmodus (3/4, 2/5, 1/6, 0/7). Vorschlag Rückverweis: implements REQ-SYNC-007

---

#### US-SYNC-002 — Kombinierte Synchronisation

**Epic:** EPIC-SYNC
**Spec-Referenz:** A.5.2.6.3
**Priorität:** MUST · **Status:** offen

> Als ALE-Station will ich die Synchronisationskriterien (Unanimous Votes, Golay, Präambel, ASCII-Checks) kombiniert anwenden, damit nur valide Wörter akzeptiert werden.

**Erfüllt durch:** _(Requirements werden mit dem Requirements-Prompt ergänzt)_

**Abgrenzung:** Diese Story beschreibt die Kombination der Synchronisationskriterien, nicht die einzelnen Prüfungen.

**Berührt auch:** A.5.2.6.3, A.5.2.6.2, A.5.2.6.1

---

## 8. Sounding — A.5.3

> **Spec-Stellen sammeln:** A.5.3.1 (Introduction), A.5.3.2 (Single channel), A.5.3.3 (Multiple channels), A.5.3.4 (Optional handshake).

### EPIC-SOUND · Sounding

#### US-SOUND-001

> Als ALE-Station will ich meine eigene Stationskennung auf einem oder mehreren Kanälen aussenden, damit mithörende Stationen die Kanalqualität bewerten können.

**Erfüllt durch:** REQ-SOUND-001, REQ-SOUND-002, REQ-SOUND-003, REQ-SOUND-004, REQ-SOUND-005, REQ-SOUND-006, REQ-SOUND-007, REQ-SOUND-008, REQ-SOUND-009, REQ-SOUND-010, REQ-SOUND-011, REQ-SOUND-012

---

### REQ-SOUND-002 — Sounding: Einseitige, periodische Übertragung auf unbesetzten Kanälen

**Spec-Referenz:** A.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Sounding ist eine einseitige, einwegige Übertragung, die in periodischen Intervallen auf unbesetzten Kanälen durchgeführt wird. Sounding ist keine interaktive, zweiseitige Technik wie Polling. Allerdings gibt die Identifizierung einer Station durch Hören ihres Sounding-Signals eine hohe Wahrscheinlichkeit (aber keine Garantie) für zweiseitige Konnektivität und kann passiv am Empfänger erfolgen. Sounding verwendet die standardmäßige ALE-Signalisierung; jede Station kann Sounding-Signale empfangen. Als Minimum muss die (Adresse)Information dem Bediener angezeigt werden und für Stationen mit Konnektivitäts- und LQA-Speichern muss die Information gespeichert und später für den Linkaufbau verwendet werden. Wenn eine Station auf einem Kanal, der zum Sounding vorgesehen ist, kürzlich Sendungen hatte, muss es nicht notwendig sein, erneut auf diesem Kanal zu senden, bis das Sounding-Intervall, restartet von diesen letzten Sendungen, abgelaufen ist. Wenn ein Netz (oder eine Gruppe) von Stationen abgehört wird, dienen ihre Antworten als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger. Alle Stationen müssen in der Lage sein, periodisches Sounding auf vorab vereinbarten Kanälen durchzuführen. Die Sounding-Fähigkeit kann vom Bediener oder Controller selektiv aktiviert werden, und der Zeitraum zwischen den Sounds kann vom Bediener oder Controller gemäß den Systemanforderungen einstellbar sein. Wenn verfügbar und nicht anderweitig zugewiesen oder vom Bediener oder Controller gerichtet, müssen alle ALE-Stationen die Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

**Akzeptanzkriterien:**
- `AC-SOUND-002-1` — Sounding ist eine einseitige, einwegige Übertragung.
- `AC-SOUND-002-2` — Sounding wird in periodischen Intervallen auf unbesetzten Kanälen durchgeführt.
- `AC-SOUND-002-3` — Sounding ist keine interaktive, zweiseitige Technik.
- `AC-SOUND-002-4` — Die Identifizierung durch Hören des Sounding-Signals gibt eine hohe Wahrscheinlichkeit für zweiseitige Konnektivität.
- `AC-SOUND-002-5` — Sounding kann passiv am Empfänger erfolgen.
- `AC-SOUND-002-6` — Sounding verwendet die standardmäßige ALE-Signalisierung.
- `AC-SOUND-002-7` — Jede Station kann Sounding-Signale empfangen.
- `AC-SOUND-002-8` — Die Adresse-Information muss dem Bediener als Minimum angezeigt werden.
- `AC-SOUND-002-9` — Stationen mit Konnektivitäts- und LQA-Speichern müssen die Information speichern und später für den Linkaufbau verwenden.
- `AC-SOUND-002-10` — Wenn eine Station kürzlich auf einem zum Sounding vorgesehenen Kanal gesendet hat, kann das erneute Senden entfallen, bis das Sounding-Intervall seit den letzten Sendungen abgelaufen ist.
- `AC-SOUND-002-11` — Antworten eines abgehörten Netzes (oder einer Gruppe) dienen als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger.
- `AC-SOUND-002-12` — Alle Stationen müssen periodisches Sounding auf vorab vereinbarten Kanälen durchführen können.
- `AC-SOUND-002-13` — Die Sounding-Fähigkeit kann selektiv vom Bediener oder Controller aktiviert werden.
- `AC-SOUND-002-14` — Der Zeitraum zwischen den Sounds kann vom Bediener oder Controller einstellbar sein.
- `AC-SOUND-002-15` — Verfügbare Stationen müssen Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

---

### REQ-SOUND-003 — Sounding-Struktur: Identisch zum Basic Call, ohne Calling Cycle und Message

**Spec-Referenz:** A.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Struktur des Sounds ist virtually identisch mit dem Basic Call; jedoch ist der Calling Cycle nicht nötig und es gibt keinen Message-Abschnitt. Es ist nur notwendig, den Conclusion (Terminator) zu senden, der die sendende Station identifiziert. Der Typ des Wortes, entweder TIS oder TWAS (aber nie beide), zeigt an, ob potenzielle Anrufer ermutigt oder ignoriert werden sollen.

**Akzeptanzkriterien:**
- `AC-SOUND-003-1` — Die Struktur des Sounds ist identisch mit dem Basic Call, mit Ausnahmen des Calling Cycles und des Message-Abschnitts.
- `AC-SOUND-003-2` — Der Conclusion (Terminator) muss gesendet werden, um die sendende Station zu identifizieren.
- `AC-SOUND-003-3` — Nur entweder TIS oder TWAS wird verwendet, niemals beide im selben Frame.
- `AC-SOUND-003-4` — Der Worttyp TIS zeigt an, dass potenzielle Anrufer ermutigt werden.
- `AC-SOUND-003-5` — Der Worttyp TWAS zeigt an, dass potenzielle Anrufer ignoriert werden sollen.

---

### REQ-SOUND-004 — Minimale redundante Sounding-Zeit (Trs)

**Spec-Referenz:** A.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die minimale redundante Sounding-Zeit (Trs) ist gleich der standardmäßigen Single-Word-Address-Calling-Zeit (Tlc), also 784 ms.

**Akzeptanzkriterien:**
- `AC-SOUND-004-1` — Die minimale redundante Sounding-Zeit (Trs) muss 784 ms betragen.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Minimale redundante Sounding-Zeit (Trs) | 784 | ms | A.5.3.1 |
| Standard Single-Word-Address-Calling-Zeit (Tlc) | 784 | ms | A.5.3.1 |

---

### REQ-SOUND-005 — Single-Channel Sounding-Protokoll

**Spec-Referenz:** A.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die fundamentale Fähigkeit zum automatischen Sounding auf einem Kanal muss gemäß dem Sounding-Protokoll (siehe Figure A-22) implementiert werden. Als Option können Stationen dieses Protokoll für Single-Channel Sounding, Connectivity Tracking und die Übertragung ihrer Verfügbarkeit für Rufe und Verkehr verwenden. Das Basisprotokoll besteht nur aus einem Teil: dem Sound. Der Sound enthält seine eigene Adresse ("TIS A"). Wenn "A" Anrufer ermutigt und einen empfängt, muss "A" dem Sound das optionale Handshake-Protokoll aus A.5.3.4 folgen. Wenn "A" plant, Anrufer zu ignorieren, muss es TWAS verwenden, was "B" und anderen mitteilt, keine Anrufe zu versuchen, und dann sofort zu normal "available" zurückkehren. In einigen Systemen muss eine Multikanal-Station "A" periodisch zu einem Single-Channel-Netz senden, um ihm mitzuteilen, dass sie auf diesem Kanal aktiv und verfügbar ist. Bei Empfang von "A"s Sound müssen "B" und andere Stationen "A"s Adresse als empfangenen Sound anzeigen und, wenn sie über LQA- und Connectivity-Speicher verfügen, die Konnektivitätsinformation speichern.

**Akzeptanzkriterien:**
- `AC-SOUND-005-1` — Alle Stationen besitzen die fundamentale Fähigkeit zum automatischen Single-Channel Sounding gemäß dem Standard-Protokoll.
- `AC-SOUND-005-2` — Stationen können optional das Protokoll für Single-Channel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung verwenden.
- `AC-SOUND-005-3` — Das Basis-Sounding-Protokoll besteht nur aus dem Sound.
- `AC-SOUND-005-4` — Der Sound enthält die eigene Adresse der sendenden Station.
- `AC-SOUND-005-5` — Wenn eine Station Anrufer ermutigt und einen empfängt, folgt sie dem optionalen Handshake-Protokoll aus A.5.3.4.
- `AC-SOUND-005-6` — Wenn eine Station Anrufer ignorieren will, verwendet sie TWAS und kehrt sofort zu "available" zurück.
- `AC-SOUND-005-7` — Multikanal-Stationen können periodisch zu einem Single-Channel-Netz senden, um Aktivität und Verfügbarkeit zu melden.
- `AC-SOUND-005-8` — Bei Empfang eines Sounds müssen Empfänger die Adresse anzeigen und bei Verfügbarkeit Konnektivitätsinformation speichern.

---

### REQ-SOUND-006 — Multichannel Sounding: Kompatibilität mit Scanning

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sounding muss mit dem Scanning-Timing kompatibel sein. Alle Stationen müssen in der Lage sein, die unten beschriebenen Scanning-Sounding-Protokolle durchzuführen, auch wenn sie auf einer festen Frequierung betrieben werden. Diese Protokolle stellen einseitige Konnektivität zwischen Stationen auf jedem verfügbaren, gemeinsam abgetasteten Kanal sicher und bestätigen sie positiv; sie unterstützen auch den Aufbau von Links zwischen Stationen, die auf Kontakt warten. Stationen müssen diese Protokolle für Multichannel Sounding, Connectivity Tracking und die Übertragung ihrer Verfügbarkeit für Rufe und Verkehr verwenden.

**Akzeptanzkriterien:**
- `AC-SOUND-006-1` — Sounding ist mit dem Scanning-Timing kompatibel.
- `AC-SOUND-006-2` — Alle Stationen können Scanning-Sounding-Protokolle durchführen, auch auf fester Frequenz.
- `AC-SOUND-006-3` — Die Protokolle stellen einseitige Konnektivität auf jedem verfügbaren, gemeinsam abgetasteten Kanal sicher und bestätigen sie.
- `AC-SOUND-006-4` — Stationen verwenden die Protokolle für Multichannel Sounding, Connectivity Tracking und Verfügbarkeitsübertragung.

---

### REQ-SOUND-007 — Multichannel Sounding: Timing-Berechnung für Scanning Sound Frame

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle Timing-Betrachtungen und Berechnungen für individuelles Scanning-Calling gelten auch für Scanning Sounding, einschließlich Sounding-Cycle-Zeiten und (optionaler) Handshake-Zeiten. Der Scanning Sound ist identisch mit dem Single-Channel Sound, außer durch die Erweiterung der redundanten Sounding-Zeit (Trs) durch Hinzufügen von Wörtern zur Scan Sounding Time (Tss), um eine Scanning Redundant Sound Time (Tsrs) zu bilden: Tsrs = Tss + Trs. Die Scan Sounding Time (Tss) hat den gleichen Zweck wie die Scan Calling Time (Tsc) für eine äquivalente Scanning-Situation, verwendet aber nur die Volladresse des Senders. Die Kanal-Scanning-Sequenzen und Auswahlkriterien für individuelles Scanning Calling gelten auch für Scanning Sounding. Die zu sondierenden Kanäle werden als "Sound Set" bezeichnet und sind üblicherweise identisch mit dem "Scan Set", der für Scanning verwendet wird.

**Akzeptanzkriterien:**
- `AC-SOUND-007-1` — Alle Timing-Betrachtungen und Berechnungen für individuelles Scanning Calling gelten auch für Scanning Sounding.
- `AC-SOUND-007-2` — Der Scanning Sound unterscheidet sich vom Single-Channel Sound durch die Erweiterung von Trs durch Tss zu Tsrs.
- `AC-SOUND-007-3` — Die Beziehung muss gelten: Tsrs = Tss + Trs.
- `AC-SOUND-007-4` — Tss hat den gleichen Zweck wie Tsc für eine äquivalente Scanning-Situation.
- `AC-SOUND-007-5` — Kanal-Scanning-Sequenzen und Auswahlkriterien für Scanning Calling gelten auch für Scanning Sounding.
- `AC-SOUND-007-6` — Das "Sound Set" für sondierende Kanäle ist üblicherweise identisch mit dem "Scan Set".

---

### REQ-SOUND-008 — Call-Rejection Scanning Sounding Protocol

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine Station "A" plant, Anrufer von "B" nach "A"s Sound zu ignorieren, muss das Call-Rejection Scanning Sounding Protocol verwendet werden. In einer für individuelles Scanning Calling identischen Weise landet "A" auf dem ersten Kanal im Scan Set, wartet (Twt), um zu sehen, ob der Kanal frei ist, stimmt den Coupler ab (Tt), geht auf volle Leistung und initiiert den Frame der Scanning Redundant Sound Times (Tsrs). Dieser Scanning Sound muss länger sein als die Scan-Periode (Ts) der Empfänger-Stationen um mindestens Trs, um eine verfügbare Detektionsperiode von mindestens Tdrw = 784 ms sicherzustellen. "A" verwendet auch "TWAS A" redundant, um mitzuteilen, dass keine Anrufe erwartet werden. Nach Abschluss des Scanning Sound Frame überlässt "A" sofort den Kanal und geht zum nächsten Kanal im Sound Set. Dieses Verfahren wiederholt sich, bis alle Kanäle gesendet wurden oder übersprungen wurden, falls besetzt. Wenn der calling ALE-Station alle vorab vereinbarten Sound Set Kanäle erschöpft sind, muss sie automatisch zum normalen "available" receive scan mode zurückkehren.

**Akzeptanzkriterien:**
- `AC-SOUND-008-1` — Bei geplantem Ignorieren von Anrufern muss das Call-Rejection Scanning Sounding Protocol verwendet werden.
- `AC-SOUND-008-2` — Die Station landet auf dem ersten Kanal im Scan Set und wartet (Twt), um die Kanalbelegung zu prüfen.
- `AC-SOUND-008-3` — Nach dem Abstimmen (Tt) geht die Station auf volle Leistung und initiiert den Scanning Sound Frame.
- `AC-SOUND-008-4` — Der Scanning Sound muss den Scan-Perioden der Empfänger-Stationen um mindestens Trs überschreiten, um Tdrw ≥ 784 ms sicherzustellen.
- `AC-SOUND-008-5` — "TWAS A" wird redundant verwendet, um mitzuteilen, dass keine Anrufe erwartet werden.
- `AC-SOUND-008-6` — Nach dem Scanning Sound Frame verlässt die Station sofort den Kanal und geht zum nächsten Kanal im Sound Set.
- `AC-SOUND-008-7` — Das Verfahren wiederholt sich, bis alle Kanäle gesendet oder übersprungen (bei Belegung) wurden.
- `AC-SOUND-008-8` — Nach Erschöpfung aller Sound Set Kanäle kehrt die Station automatisch zum "available" receive scan mode zurück.

---

### REQ-SOUND-009 — Scanning Detection: Mindestens eine Erfassungsmöglichkeit pro Kanal

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wie in der Abbildung dargestellt, wurde das Timing von "A" und "B" vorab vereinbart, um sicherzustellen, dass "B" mindestens eine Gelegenheit pro Kanal hat, "A"s Sound zu erreichen und zu "captures". Spezifisch: "B" kommt an, erkennt Sounds, wartet auf gute Wörter, liest mindestens drei (redundante) "TWAS A" (in 3 bis 4 Tw), speichert die Konnektivitätsinformation und verlässt sofort den Kanal, um das Scanning fortzusetzen.

**Akzeptanzkriterien:**
- `AC-SOUND-009-1` — Das Timing von Sounding- und Scanning-Stationen ist vorab vereinbart, um mindestens eine Erfassungsmöglichkeit pro Kanal sicherzustellen.
- `AC-SOUND-009-2` — Die scannende Station muss mindestens drei redundante Wörter des Sounding-Senders lesen können.
- `AC-SOUND-009-3` — Die scannende Station speichert die Konnektivitätsinformation und setzt das Scanning fort.

---

### REQ-SOUND-010 — Call-Acceptance Scanning Sounding Protocol

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Es gibt mehrere spezifische Protokoll-Unterschiede, wenn Station "A" plant, Anrufer nach dem Sound zu begrüßen. "A" soundet für die gleiche Zeitdauer wie zuvor. Da "A" für Anrufe empfänglich ist, muss es seine normale Scanning Dwell Time (Td) oder seine vorab festgelegte Wait-Before-Transmit Time (Twt), whichever länger ist, verwenden, um sowohl für Kanalaktivität als auch für Anrufe zu hören, bevor es sendet. Wenn der Kanal frei ist, initiiert "A" den Scanning Sound wie zuvor, aber mit "TIS A". Am Ende des Sounding Frame muss "A" für Anrufe warten, gemäß der Wait-for-Reply und Tune Time (Twrt) im individuellen Scanning Calling Protocol, in diesem Fall als 6 Tw (für schnell-abstimmende Stationen) dargestellt. Während dieses Wartens muss "A" (wie immer) auf Anrufe hören, die zufällig ankommen können, auch wenn sie nicht mit "A"s Sound assoziiert sind, sowie auf jeden anderen gehörten Sound, den "A" als Konnektivitätsinformation speichern muss, wenn es polling-fähig ist. Wenn keine Anrufe empfangen wurden, verlässt "A" den Kanal.

**Akzeptanzkriterien:**
- `AC-SOUND-010-1` — Bei geplantem Begrüßen von Anrufern verwendet Station "A" seine normale Scanning Dwell Time (Td) oder Twt, whichever länger ist, zum Hören vor dem Sounding.
- `AC-SOUND-010-2` — Bei freiem Kanal initiiert "A" den Scanning Sound mit "TIS A".
- `AC-SOUND-010-3` — "A" soundet für die gleiche Zeitdauer wie beim Call-Rejection Protocol.
- `AC-SOUND-010-4` — Am Ende des Sounding Frame wartet "A" für Anrufe gemäß Twrt (z. B. 6 Tw für schnell-abstimmende Stationen).
- `AC-SOUND-010-5` — Während des Wartens hört "A" auf assoziierte und nicht-assozierte Anrufe sowie auf andere Sounds.
- `AC-SOUND-010-6` — Gehörte andere Sounds werden als Konnektivitätsinformation gespeichert, wenn die Station polling-fähig ist.
- `AC-SOUND-010-7` — Wenn keine Anrufe empfangen wurden, verlässt "A" den Kanal.

---

### REQ-SOUND-011 — Optionales Handshake: Getriggert durch Konnektivität vom Sounding

**Spec-Referenz:** A.5.3.4
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Eine alternative Aktion ist die Implementierung eines optionalen Handshakes mit einer Station unmittelbar nach deren Sound. Dieses Protokoll ist in jeder Hinsicht identisch mit dem Single-Channel Individual Call Protocol, außer dass es manuell oder automatisch (Bediener oder Controller) getriggert wird durch den Erwerb der Konnektivität von der Station, die gerufen werden soll. Wenn ALE-Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt mit einer solchen Station erforderlich ist, sollte das optionale Handshake Protocol verwendet werden. Die rufende Station sollte unmittelbar den Call initiieren, nachdem sie bestimmt hat, dass die gerufene Station ihre Übertragung beendet hat. Eine Wait-Before-Transmit Time ist nicht erforderlich. Wenn "B" "A"s Sound hört und "A" sucht, ruft "B" unverzüglich mit dem einfachen Single-Channel Call. Wenn auch "B"s Bediener oder Controller "A"s Adresse identifiziert, kann es den optionalen Handshake versuchen.

**Akzeptanzkriterien:**
- `AC-SOUND-011-1` — Ein optionales Handshake kann unmittelbar nach einem Sounding mit einer gerufenen Station durchgeführt werden.
- `AC-SOUND-011-2` — Das Handshake-Protokoll ist identisch mit dem Single-Channel Individual Call Protocol, außer dem Trigger-Mechanismus.
- `AC-SOUND-011-3` — Das Handshake kann manuell oder automatisch (Bediener oder Controller) durch den Erwerb der Konnektivität vom Sounding getriggert werden.
- `AC-SOUND-011-4` — Wenn Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt erforderlich ist, sollte das optionale Handshake verwendet werden.
- `AC-SOUND-011-5` — Die rufende Station sollte unverzüglich den Call initiieren, nachdem die gerufene Station ihre Übertragung beendet hat.
- `AC-SOUND-011-6` — Eine Wait-Before-Transmit Time ist für das Handshake nach Sounding nicht erforderlich.
- `AC-SOUND-011-7` — Ein Empfänger, der den Sound einer gesuchten Station hört, kann unverzüglich mit dem einfachen Single-Channel Call antworten.

**PRIORITÄTS-BEGRÜNDUNG:**
- Das Handshake ist im Standard ausdrücklich als "optional" beschrieben; die Implementierung wird empfohlen, wenn Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt erforderlich ist.

---

### REQ-SOUND-012 — Sounding: Keine neuen Frequenz- oder Hardware-Erfordernisse

**Spec-Referenz:** A.5.3.1 / A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sounding verwendet die standardmäßige ALE-Signalisierung und die gleichen Frequenzen, die bereits für das Scanning vorgesehen sind. Es werden keine neuen Frequenzen oder speziellen Sounding-Kanäle benötigt. Das "Sound Set" ist üblicherweise identisch mit dem "Scan Set".

**Akzeptanzkriterien:**
- `AC-SOUND-012-1` — Sounding verwendet die standardmäßige ALE-Signalisierung.
- `AC-SOUND-012-2` — Sounding verwendet die gleichen Frequenzen wie das Scanning.
- `AC-SOUND-012-3` — Keine neuen Frequenzen oder speziellen Sounding-Kanäle werden benötigt.
- `AC-SOUND-012-4` — Das "Sound Set" ist üblicherweise identisch mit dem "Scan Set".

---

## 9. Channel Selection — A.5.4
> Spec-Stellen: A.5.4, A.5.4.1 (LQA), A.5.4.1.1-4 (BER, SINAD, MP, Display), A.5.4.2-3 (Current/Historical LQA), A.5.4.4 (Local Noise), A.5.4.5 (Single-station), A.5.4.6 (Multiple-station), A.5.4.7 (Listen before transmit)

### EPIC-CHAN · Kanalauswahl
#### US-CHAN-001
> Als ALE-Station will ich den besten verfügbaren Kanal automatisch auswählen, damit Verbindungen mit höchster Qualität aufgebaut werden.
**Erfüllt durch:** REQ-CHAN-001 bis REQ-CHAN-034

---

### 8.0 Channel Selection: Grundprinzip und LQA-Basis — A.5.4

#### REQ-CHAN-001 — Channel-Selection-System: Grundprinzip

**Spec-Referenz:** A.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss ein Channel-Selection-System implementieren, das es einer Station erlaubt, aus einem vorab vereinbarten Satz von Kanälen automatisch den besten verfügbaren Kanal für Calling und Kommunikation auszuwählen. Die Auswahl basiert auf gespeicherten Link-Quality-Daten und aktuellen Kanalbelegungsinformationen.

**Akzeptanzkriterien:**
- `AC-CHAN-001-1` — Das System implementiert eine automatische Kanalauswahl aus einem vorab vereinbarten Kanalsatz.
- `AC-CHAN-001-2` — Die Kanalauswahl berücksichtigt gespeicherte Link-Quality-Daten.
- `AC-CHAN-001-3` — Die Kanalauswahl berücksichtigt die aktuelle Kanalbelegung.

---

#### REQ-CHAN-002 — Link Quality Analysis (LQA): Grundfunktion

**Spec-Referenz:** A.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine Link Quality Analysis (LQA) durchführen, die Qualitätsmessungen aus empfangenen ALE-Signalen ableitet und im Link Quality Memory speichert. Die LQA-Daten bilden die Grundlage für die Kanalauswahl.

**Akzeptanzkriterien:**
- `AC-CHAN-002-1` — Das System leitet Qualitätsmessungen aus empfangenen ALE-Signalen ab.
- `AC-CHAN-002-2` — Die Messwerte werden im Link Quality Memory gespeichert.
- `AC-CHAN-002-3` — Die gespeicherten LQA-Daten bilden die Grundlage für die Kanalauswahl.

---

### 8.1 LQA — A.5.4.1
> Spec-Stellen: A.5.4.1

#### REQ-CHAN-003 — LQA zur Kanalauswertung und -auswahl

**Spec-Referenz:** A.5.4.1 / Absatz 1
**Priorität:** MUST · **Status:** offen

**Anforderung:** LQA-Daten sind zur Bewertung der Kanäle und zur Unterstützung der Auswahl eines "besten" (oder akzeptablen) Kanals für Calling und Kommunikation zu verwenden.

**Akzeptanzkriterien:**
- `AC-CHAN-003-1` — LQA-Werte beeinflussen die Kanalauswahl für Calling und Kommunikation.

---

#### REQ-CHAN-004 — Kontinuierliche LQA-Überwachung während der Kommunikation

**Spec-Referenz:** A.5.4.1 / Absatz 2
**Priorität:** MUST · **Status:** offen

**Anforderung:** LQA ist zur kontinuierlichen Überwachung der Link-Qualität während der mit ALE-Signalisierung führenden Kommunikation zu verwenden.

**Akzeptanzkriterien:**
- `AC-CHAN-004-1` — Die Link-Qualität wird während der gesamten Kommunikation fortlaufend mit LQA überwacht.

---

#### REQ-CHAN-005 — Verfügbarkeit und Übermittlung der gespeicherten LQA-Werte

**Spec-Referenz:** A.5.4.1 / Absatz 3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die gespeicherten LQA-Werte sind auf Anfrage oder nach Anweisung des Netzwerkmanagers übertragbar zu machen.

**Akzeptanzkriterien:**
- `AC-CHAN-005-1` — LQA-Werte sind auf Anfrage übertragbar.
- `AC-CHAN-005-2` — LQA-Werte sind nach Anweisung des Netzwerkmanagers übertragbar.

---

#### REQ-CHAN-006 — Automatische Einfügung des CMD LQA-Words

**Spec-Referenz:** A.5.4.1 / Absatz 4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle ALE-Stationen sind automatisch verpflichtet, das CMD LQA-Word in die Message-Sections ihrer Signale und Handshakes einzufügen, wenn dazu von der handshaking Station angefragt, in einem Netzwerk vereinbart oder vom Protokoll vorgeschrieben, es sei denn, der Operator oder Controller weist ausdrücklich etwas anderes an.

**Akzeptanzkriterien:**
- `AC-CHAN-006-1` — CMD LQA-Wort wird auf Anfrage der handshaking Station eingefügt.
- `AC-CHAN-006-2` — CMD LQA-Wort wird bei vorheriger Netzvereinbarung eingefügt.
- `AC-CHAN-006-3` — CMD LQA-Wort wird bei protokollarischer Vorgabe eingefügt.
- `AC-CHAN-006-4` — Eine explizite Anweisung des Operators oder Controllers hebt diese Pflicht auf.

---

#### REQ-CHAN-007 — LQA-Anfrage durch Control-Bit KA1 (polling-capable Station)

**Spec-Referenz:** A.5.4.1 / Absatz 5
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Eine ALE-Station, die LQA-Informationen erfordert und verwenden kann (polling-fähig), kann die Daten von einer anderen Station anfordern, indem das Control-Bit KA1 im CMD LQA-Wort auf "1" gesetzt wird.

**Akzeptanzkriterien:**
- `AC-CHAN-007-1` — Eine polling-fähige Station kann durch Setzen von KA1="1" einen LQA-Report anfordern.

**Prioritäts-Begründung (SHOULD):** Der Standard verwendet "may" für die Anfragefähigkeit — es ist optional, aber empfohlen für polling-fähige Stationen.

---

#### REQ-CHAN-008 — KA1 auf "0" setzen bei nicht-polling-fähigen Stationen

**Spec-Referenz:** A.5.4.1 / Absatz 6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine ALE-Station, die CMD LQA sendet, aber LQA-Informationen nicht verwenden kann (nicht polling-fähig), muss das Control-Bit KA1 auf "0" setzen.

**Akzeptanzkriterien:**
- `AC-CHAN-008-1` — Nicht-polling-fähige Stationen setzen KA1="0" in gesendetem CMD LQA.

---

#### REQ-CHAN-009 — Aktiv/Passiv-Entscheidung für LQA als Netzwerkmanagement-Entscheidung

**Spec-Referenz:** A.5.4.1 / Absatz 7
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Ob LQA aktiv oder passiv ist, ist eine Netzwerkmanagement-Entscheidung.

**Akzeptanzkriterien:**
- `AC-CHAN-009-1` — Der aktive/ passive Modus von LQA wird durch Netzwerkmanagement gesteuert.

**Prioritäts-Begründung (SHOULD):** Der Standard formuliert dies als Entscheidungsprinzip, nicht als zwingende Verhaltensvorgabe für jede Station.

---

#### REQ-CHAN-010 — LQA-Scores für Operator-Anzeige: höhere Zahlen = bessere Kanäle

**Spec-Referenz:** A.5.4.1 / Absatz 8
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Für menschliche Bedienung sollten LQA-Scores, die dem Operator angezeigt werden, höhere (Zahl-)Werte für bessere Kanäle haben.

**Akzeptanzkriterien:**
- `AC-CHAN-010-1` — Die Operator-Anzeige zeigt höhere Zahlenwerte für bessere Kanäle.

**Prioritäts-Begründung (SHOULD):** Der Standard verwendet "should" — es ist eine Empfehlung für UX, kein absolutes Muss.

---

### 8.2 BER — A.5.4.1.1
> Spec-Stellen: A.5.4.1.1

#### REQ-CHAN-011 — BER-Messung durch Zählen nicht-einstimmiger Abstimmungen

**Spec-Referenz:** A.5.4.1.1 / Absatz 1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die BER-Messung erfolgt durch Zählen der Anzahl der nicht-einstimmigen (2/3) Abstimmungen (out of 48) im Majority-Vote-Decoder. Der Messbereich erstreckt sich von 0 bis 48.

**Akzeptanzkriterien:**
- `AC-CHAN-011-1` — BER wird durch Zählen nicht-einstimmiger Majority-Vote-Abstimmungen gemessen.
- `AC-CHAN-011-2` — Der Messbereich beträgt 0 bis 48.

---

#### REQ-CHAN-012 — BER-Berechnung nach erreichtem Word-Sync

**Spec-Referenz:** A.5.4.1.1 / Absatz 2, 3, 4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Nach Erreichen des Word-Syncs sind alle empfangenen Words in einem Frame zu messen und ein linearer Durchschnitt BER/LQA wie folgt zu berechnen: Wenn der Golay-Decoder keine unkorrigierbaren Fehler in beiden Hälften des ALE-Words meldet, ist die Anzahl der nicht-einstimmigen Abstimmungen zum Gesamtsummenwert hinzuzufügen. Wenn mindestens eine Hälfte unkorrigierbare Fehler enthielt, sind die nicht-einstimmigen Abstimmungen zu verwerfen und 48 (der Maximalwert) zum Gesamtsummenwert hinzuzufügen. Am Ende der Übertragung ist der Gesamtsummenwert durch die Anzahl der empfangenen Words zu teilen und im Link Quality Memory als aktueller BER-Code für die sendende Station und den tragenden Kanal zu speichern.

**Akzeptanzkriterien:**
- `AC-CHAN-012-1` — Bei fehlenden unkorrigierbaren Fehlern in beiden Hälften: nicht-einstimmige Abstimmungen zur Gesamtsumme addieren.
- `AC-CHAN-012-2` — Bei unkorrigierbaren Fehlern in mindestens einer Hälfte: 48 zur Gesamtsumme addieren (nicht-einstimmige Abstimmungen verwerfen).
- `AC-CHAN-012-3` — Der Durchschnittswert (Gesamtsumme / Anzahl empfangener Words) wird im Link Quality Memory gespeichert.
- `AC-CHAN-012-4` — Der gespeicherte Wert wird der sendenden Station und dem tragenden Kanal zugeordnet.

---

### 8.3 SINAD — A.5.4.1.2
> Spec-Stellen: A.5.4.1.2

#### REQ-CHAN-013 — SINAD-Messung als (S+N+D)/(N+D)-Verhältnis

**Spec-Referenz:** A.5.4.1.2 / Absatz 1, 2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die SINAD-Messung ist ein (S+N+D)/(N+D)-Verhältnis, gemittelt über die Dauer jedes empfangenen ALE-Signals. SINAD-Werte sind auf allen ALE-Signalen zu messen.

**Akzeptanzkriterien:**
- `AC-CHAN-013-1` — SINAD wird als (S+N+D)/(N+D)-Verhältnis gemessen.
- `AC-CHAN-013-2` — Die Messung erfolgt über die volle Dauer des empfangenen ALE-Signals.
- `AC-CHAN-013-3` — SINAD wird auf allen empfangenen ALE-Signalen gemessen.

---

### 8.4 MP (optional) — A.5.4.1.3
> Spec-Stellen: A.5.4.1.3

#### REQ-CHAN-014 — MP-Messung ist optional

**Spec-Referenz:** A.5.4.1.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Die Messung von MP (Modulation Performance) mittels empfangener ALE-Signale ist optional.

**Akzeptanzkriterien:**
- `AC-CHAN-014-1` — Eine Implementierung kann MP-Messung unterstützen oder unterlassen.

**Prioritäts-Begründung (COULD):** Der Standard definiert MP als ausdrücklich optional.

---

### 8.5 Operator display — A.5.4.1.4
> Spec-Stellen: A.5.4.1.4

#### REQ-CHAN-015 — SINAD-Anzeige in dB

**Spec-Referenz:** A.5.4.1.4
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Die Anzeige von SINAD-Werten ist in dB durchzuführen.

**Akzeptanzkriterien:**
- `AC-CHAN-015-1` — SINAD-Werte werden dem Operator in dB angezeigt.

**Prioritäts-Begründung (SHOULD):** Der Standard verwendet "shall" für die Anzeige, jedoch ist dies als optional (optionaler Abschnitt) markiert.

---

### 8.6 Current channel quality report (LQA CMD) — A.5.4.2
> Spec-Stellen: A.5.4.2

#### REQ-CHAN-016 — CMD LQA Word ist verpflichtende Funktion

**Spec-Referenz:** A.5.4.2 / Absatz 1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Funktion zum Austausch aktueller LQA-Informationen unter ALE-Stationen (CMD LQA) ist verpflichtend.

**Akzeptanzkriterien:**
- `AC-CHAN-016-1` — Alle ALE-Stationen unterstützen die CMD LQA-Funktion.

---

#### REQ-CHAN-017 — CMD LQA Word enthält BER, SINAD und MP

**Spec-Referenz:** A.5.4.2 / Absatz 2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das CMD LQA-Wort enthält drei Arten von Analyseinformationen (BER, SINAD und MP), die separat von der ALE-Analyse-Fähigkeit erzeugt werden. Wenn das Control-Bit KA1 auf "1" gesetzt ist, muss die empfangende Station mit einem LQA-Report im Handshake antworten. Wenn KA1 auf "0" gesetzt ist, ist kein Report erforderlich.

**Akzeptanzkriterien:**
- `AC-CHAN-017-1` — CMD LQA-Wort trägt BER-, SINAD- und MP-Informationen.
- `AC-CHAN-017-2` — Bei KA1="1" antwortet die empfangende Station mit einem LQA-Report.
- `AC-CHAN-017-3` — Bei KA1="0" ist kein LQA-Report erforderlich.

---

### 8.7 BER field in LQA CMD — A.5.4.2.1
> Spec-Stellen: A.5.4.2.1

#### REQ-CHAN-018 — BER-Feld im LQA CMD enthält 5 Bits

**Spec-Referenz:** A.5.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Messung und Meldung von BER ist verpflichtend. Das BER-Feld im LQA CMD enthält fünf Bits. Tabelle A-XIII ist für die zugeordneten Werte heranzuziehen.

**Akzeptanzkriterien:**
- `AC-CHAN-018-1` — BER-Messung und -Meldung ist verpflichtend.
- `AC-CHAN-018-2` — Das BER-Feld besteht aus 5 Bits.
- `AC-CHAN-018-3` — Die BER-Werte entsprechen Tabelle A-XIII.

---

### 8.8 SINAD field in LQA CMD — A.5.4.2.2
> Spec-Stellen: A.5.4.2.2

#### REQ-CHAN-019 — SINAD-Feld im LQA CMD: 5 Bits, 0–30 dB

**Spec-Referenz:** A.5.4.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** SINAD wird im CMD LQA Word als 5 Bits dargestellt. Der Messbereich ist 0 bis 30 dB in 1-dB-Schritten. 00000 entspricht 0 dB oder weniger, 11111 bedeutet keine Messung verfügbar.

**Akzeptanzkriterien:**
- `AC-CHAN-019-1` — SINAD wird als 5-Bit-Feld dargestellt.
- `AC-CHAN-019-2` — Der Bereich erstreckt sich von 0 bis 30 dB in 1-dB-Schritten.
- `AC-CHAN-019-3` — Der Code 00000 repräsentiert 0 dB oder weniger.
- `AC-CHAN-019-4` — Der Code 11111 signalisiert "keine Messung verfügbar."

---

### 8.9 MP field in LQA CMD — A.5.4.2.3
> Spec-Stellen: A.5.4.2.3

#### REQ-CHAN-020 — MP-Feld im LQA CMD: 3 Bits, 0–6 ms

**Spec-Referenz:** A.5.4.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn implementiert, werden MP-Messungen im CMD LQA Word in 3 Bits dargestellt. Der gemessene Wert in Millisekunden ist auf die nächste ganze Zahl gerundet zu melden, außer Werte größer als 6 ms, die als 6 zu melden sind. Wenn MP nicht gemessen wird, ist der gemeldete MP-Wert 7.

**Akzeptanzkriterien:**
- `AC-CHAN-020-1` — MP wird in 3 Bits dargestellt (wenn implementiert).
- `AC-CHAN-020-2` — Der Wert in ms ist auf die nächste ganze Zahl gerundet.
- `AC-CHAN-020-3` — Werte größer als 6 ms werden als 6 gemeldet.
- `AC-CHAN-020-4` — Bei fehlender MP-Messung wird 7 gemeldet.

---

### 8.10 Local Noise Report CMD (optional) — A.5.4.4
> Spec-Stellen: A.5.4.4

#### REQ-CHAN-021 — Local Noise Report CMD (optional)

**Spec-Referenz:** A.5.4.4 / Absatz 1, 2
**Priorität:** COULD · **Status:** offen

**Anforderung:** Der Local Noise Report CMD bietet eine Broadcast-Alternative zum Sounding, die es empfangenden Stationen erlaubt, die bilaterale Link-Qualität für den das Report tragenden Kanal grob vorherzusagen. Der CMD meldet die mittlere und maximale Rauschleistung, die auf dem Kanal in den vergangenen 60 Minuten gemessen wurde.

**Akzeptanzkriterien:**
- `AC-CHAN-021-1` — Eine Implementierung kann den Local Noise Report CMD unterstützen oder unterlassen.
- `AC-CHAN-021-2` — Der Report enthält Mittel- und Maximalrauschleistung der vergangenen 60 Minuten.

**Prioritäts-Begründung (COULD):** Der Standard markiert diesen CMD ausdrücklich als optional.

---

#### REQ-CHAN-022 — Local Noise Report: Einheiten und Codierung

**Spec-Referenz:** A.5.4.4 / Absatz 2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Einheiten für Max- und Mean-Felder sind dB relativ zu 0,1 µV / 3 kHz Rauschen. Bei einem gemessenen Rauschwert von 0 dB oder weniger ist eine 0 zu senden. Für Messwerte von 0 dB bis +126 dB ist das Verhältnis in dB auf eine ganze Zahl gerundet zu senden. Für Rauschverhältnisse größer als +126 dB ist 126 zu senden. Der Code 127 (alle 1en) ist zu senden, wenn kein Report für ein Feld verfügbar ist.

**Akzeptanzkriterien:**
- `AC-CHAN-022-1` — Einheiten sind dB relativ zu 0,1 µV / 3 kHz.
- `AC-CHAN-022-2` — Wert ≤ 0 dB wird als 0 gesendet.
- `AC-CHAN-022-3` — Wert 0–126 dB wird als gerundete ganze Zahl gesendet.
- `AC-CHAN-022-4` — Wert > 126 dB wird als 126 gesendet.
- `AC-CHAN-022-5` — Code 127 signalisiert "kein Report verfügbar."

---

### 8.11 Single-station channel selection — A.5.4.5
> Spec-Stellen: A.5.4.5

#### REQ-CHAN-023 — Single-Station Channel Selection für eine Station

**Spec-Referenz:** A.5.4.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle Stationen müssen in der Lage sein, den (recent) besten Kanal für Calling oder Listening für eine einzelne Station basierend auf den Werten im LQA-Speicher auszuwählen.

**Akzeptanzkriterien:**
- `AC-CHAN-023-1` — Alle Stationen unterstützen die Kanalselektion für eine einzelne Station basierend auf LQA.

---

### 8.12 Single-station: Link establishment — A.5.4.5.1
> Spec-Stellen: A.5.4.5.1

#### REQ-CHAN-024 — Kanalselektion für Zwei-Wege-Link: beide Richtungen berücksichtigen

**Spec-Referenz:** A.5.4.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei der Auswahl eines Kanals für einen Zwei-Wege-Link sind die Link-Quality-Messungen für beide Richtungen auf jedem Frequenz zu berücksichtigen. Bilaterale (Handshake-)Scores sind die Summe der beiden LQA-Werte.

**Akzeptanzkriterien:**
- `AC-CHAN-024-1` — Für Zwei-Wege-Link-Auswahl werden Messungen in beide Richtungen einbezogen.
- `AC-CHAN-024-2` — Der bilaterale Score ist die Summe der LQA-Werte beider Richtungen.

---

#### REQ-CHAN-025 — LQA-Score-Semantik: kleinere Werte = besser

**Spec-Referenz:** A.5.4.5.1 / Figure A-27 Notes
**Priorität:** MUST · **Status:** offen

**Anforderung:** LQA = "0" ist ausgezeichnet, reichend bis "30", was sehr schlecht ist. LQA = "x" bedeutet, nach einem Handshake-Versuch nicht verfügbar. LQA = "-" bedeutet, nicht verfügbar, aber Handshake nicht versucht.

**Akzeptanzkriterien:**
- `AC-CHAN-025-1` — Ein LQA-Wert von 0 repräsentiert ausgezeichnete Qualität.
- `AC-CHAN-025-2` — Ein LQA-Wert von 30 repräsentiert sehr schlechte Qualität.
- `AC-CHAN-025-3` — Der Wert "x" kennzeichnet einen gescheiterten Handshake-Versuch.
- `AC-CHAN-025-4` — Der Wert "-" kennzeichnet fehlende Verfügbarkeit ohne erfolgten Handshake-Versuch.

---

### 8.13 Single-station: One-way broadcast — A.5.4.5.2
> Spec-Stellen: A.5.4.5.2

#### REQ-CHAN-026 — Kanalselektion für One-Way-Broadcast: TO-Scores gewichten

**Spec-Referenz:** A.5.4.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn nur eine Ein-Wege-Übertragung an eine Station erforderlich ist, TO-Scores (von der Ziel-Station gemeldet) sind stärker zu gewichten als FROM-Scores (von der Ziel-Station gemessen).

**Akzeptanzkriterien:**
- `AC-CHAN-026-1` — Bei One-Way-Broadcast werden TO-Scores stärker gewichtet als FROM-Scores.

---

### 8.14 Single-station: Listening — A.5.4.5.3
> Spec-Stellen: A.5.4.5.3

#### REQ-CHAN-027 — Kanalselektion für Listening: FROM-Scores gewichten

**Spec-Referenz:** A.5.4.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei der Auswahl eines Kanals zum Abhören einer anderen Station sind die auf Übertragungen von dieser Station gemessenen Scores (FROM) stärker zu gewichten als die von der Ziel-Station gemeldeten Scores.

**Akzeptanzkriterien:**
- `AC-CHAN-027-1` — Beim Abhören werden FROM-Scores stärker gewichtet als TO-Scores.

---

### 8.15 Multiple-station channel selection — A.5.4.6
> Spec-Stellen: A.5.4.6

#### REQ-CHAN-028 — Multi-Station Channel Selection

**Spec-Referenz:** A.5.4.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine Station muss ebenfalls in der Lage sein, den (recent) besten Kanal zum Anrufen oder Abhören mehrerer Stationen basierend auf den Werten im LQA-Speicher auszuwählen.

**Akzeptanzkriterien:**
- `AC-CHAN-028-1` — Die Station unterstützt die Kanalselektion für mehrere Stationen.
- `AC-CHAN-028-2` — Die Auswahl basiert auf den LQA-Speicherwerten.

---

#### REQ-CHAN-029 — Broadcast zu mehreren Stationen: TO-Scores priorisieren

**Spec-Referenz:** A.5.4.6 / Absatz 4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei einem Broadcast an mehrere Stationen werden die TO-Scores priorisiert.

**Akzeptanzkriterien:**
- `AC-CHAN-029-1` — Für Multi-Station Broadcast werden TO-Scores priorisiert.

---

#### REQ-CHAN-030 — Listening für mehrere Stationen: FROM-Scores priorisieren

**Spec-Referenz:** A.5.4.6 / Absatz 5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Zur Auswahl von Kanälen zum Abhören mehrerer Stationen werden die FROM-Scores priorisiert.

**Akzeptanzkriterien:**
- `AC-CHAN-030-1` — Für Multi-Station Listening werden FROM-Scores priorisiert.

---

### 8.16 Listen before transmit — A.5.4.7
> Spec-Stellen: A.5.4.7

#### REQ-CHAN-031 — Listen before Transmit: Verpflichtende Pause vor Call/Sound

**Spec-Referenz:** A.5.4.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bevor ein Calling oder ein Sound auf einem Kanal eingeleitet wird, muss eine ALE-Station für eine programmierbare Zeit (Twt) auf andere Verkehrstätigkeit auf dem Kanal lauschen und darf auf diesem Kanal nicht senden, wenn Verkehr erkannt wird. Normalerweise ist ein aufgrund erkannten Verkehrs abgebrochener Sound wieder zu planen, während für einen Call ein anderer Kanal zu wählen ist.

**Akzeptanzkriterien:**
- `AC-CHAN-031-1` — Vor jedem Call oder Sound wird für die Dauer Twt auf dem Kanal gelauscht.
- `AC-CHAN-031-2` — Bei erkannten Verkehr wird das Senden unterlassen.
- `AC-CHAN-031-3` — Ein aufgrund von Verkehr abgebrochener Sound wird erneut geplant.
- `AC-CHAN-031-4` — Ein aufgrund von Verkehr abgebrochener Call wählt einen anderen Kanal.

---

### 8.17 Listen-Before-Transmit duration — A.5.4.7.1
> Spec-Stellen: A.5.4.7.1

#### REQ-CHAN-032 — Listen-Before-Transmit-Dauer: programmierbar

**Spec-Referenz:** A.5.4.7.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Dauer der Listen-Before-Transmit-Pause ist vom Netzwerkmanager programmierbar. Wenn der ausgewählte Kanal nur für ALE-Übertragungen genutzt wird, muss die Pause nicht länger als 2 × Trw sein. Für andere Kanäle sind mindestens 2 Sekunden zu verwenden. Wenn die ALE-Station auf dem für die Übertragung ausgewählten Kanal bereits hörte, kann die auf dem Kanal verbrachte Hörzeit in die Listen-Before-Transmit-Zeit eingerechnet werden.

**Akzeptanzkriterien:**
- `AC-CHAN-032-1` — Die Listen-Before-Transmit-Dauer ist programmierbar.
- `AC-CHAN-032-2` — Bei ALE-nur Kanälen: Pause ≥ 2 × Trw.
- `AC-CHAN-032-3` — Bei anderen Kanälen: Pause ≥ 2 Sekunden.
- `AC-CHAN-032-4` — Bereits verbrachte Hörzeit kann angerechnet werden.

---

### 8.18 Modulations to be detected — A.5.4.7.2
> Spec-Stellen: A.5.4.7.2

#### REQ-CHAN-033 — Zu detektierende Modulationen bei Listen Before Transmit

**Spec-Referenz:** A.5.4.7.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Listen-Before-Transmit-Funktion muss Verkehr auf einem Kanal gemäß A.4.2.2 erkennen.

**Akzeptanzkriterien:**
- `AC-CHAN-033-1` — Verkehrserkennung erfolgt gemäß A.4.2.2.

---

### 8.19 Listen before transmit override — A.5.4.7.3
> Spec-Stellen: A.5.4.7.3

#### REQ-CHAN-034 — Listen-Before-Transmit Override

**Spec-Referenz:** A.5.4.7.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Operator ist berechtigt, sowohl die Listen-Before-Transmit-Pause als auch den Transmit-Lockout zu überschreiben (für Notfallzwecke).

**Akzeptanzkriterien:**
- `AC-CHAN-034-1` — Der Operator kann die Listen-Before-Transmit-Pause überschreiben.
- `AC-CHAN-034-2` — Der Operator kann den Transmit-Lockout überschreiben.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-CHAN-001 — A.4.2.2 wurde nicht mitgeliefert. Die spezifischen zu detektierenden Modulationsarten können nicht extrahiert werden.
- OPEN-CHAN-002 — "Trw" wird in REQ-CHAN-032 erwähnt (2 × Trw), aber der Wert von Trw wurde nicht mitgeliefert.
- OPEN-CHAN-003 — A.5.4.3 (Historical LQA Report) verweist auf MIL-STD-187-721, das nicht mitgeliefert wurde.

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- BER-Lookup-Tabelle A-XIII: 31 Einträge (0–30 + "no value") — Vorschlag Rückverweis: implements REQ-CHAN-018
- SINAD-Codierung: 5 Bits, 0–30 dB mapping — Vorschlag Rückverweis: implements REQ-CHAN-019
- MP-Codierung: 3 Bits, 0–6 ms + "not measured"=7 — Vorschlag Rückverweis: implements REQ-CHAN-020
- Local Noise Report Format (Figure A-26): CMD-Preamble "n", Max/ Mean Felder — Vorschlag Rückverweis: implements REQ-CHAN-021
- LQA-Memory-Struktur (Figure A-27): FROM/TO-Arrays pro Adresse/Kanal — Vorschlag Rückverweis: implements REQ-CHAN-024
- Bilaterale Score-Berechnung (Summe FROM + TO) — Vorschlag Rückverweis: implements REQ-CHAN-024


## 10. Link Establishment — A.5.5

> **Spec-Stellen sammeln:** A.5.5.2 (ALE states, timing, end-of-frame), A.5.5.3 (One-to-one calling), A.5.5.4 (One-to-many calling).

### EPIC-LINK · Link Establishment

#### US-LINK-001

> Als rufende Station will ich einzelne Stationen, Netze und Gruppen rufen können, damit alle Standard-Ruftypen für den Verbindungsaufbau verfügbar sind.

**Erfüllt durch:** REQ-LINK-001, REQ-LINK-002, REQ-LINK-003

---

### 9.1 Individual Call — A.5.5.3.1

#### REQ-LINK-001 — Individual Call

**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Ruf an eine einzelne Station durchläuft Scanning-Call, Leading-Call, Conclusion und Empfangsfenster.

**Akzeptanzkriterien:**

- `AC-LINK-001-1` — Die gesendete Wortfolge entspricht: Scanning (TO-Erstwort), Leading (vollständige Adresse ×2), Conclusion (eigene Adresse).

---

### 9.2 Net Call — A.5.5

#### REQ-LINK-002 — Net Call

**Spec-Referenz:** A.5.2.5.1
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Ein Netzruf verwendet denselben Ablauf wie der Individual Call; der Unterschied liegt in der Adressklassifikation.

---

### 9.3 Group Call — A.5.5

#### REQ-LINK-003 — Group Call

**Spec-Referenz:** A.5.2.5.1 / A.5.2.3.4.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Ein Gruppenruf adressiert mehrere Stationen; der Scanning-Call verwendet alternierende Routing-Wörter, der Leading-Call die vollständigen Adressen aller Ziele.

**Akzeptanzkriterien:**

- `AC-LINK-003-1` — Jede Zieladresse erscheint im Leading-Call vollständig.
- `AC-LINK-003-2` — Die Scanning-Sequenz endet nicht mit einem Wiederholungswort.

---

### 9.4 Empfang und Dekodierung — A.5.5.2 / A.5.5.3

> **Spec-Stellen sammeln:** A.5.5.2.4 (End of frame detection), A.5.5.3.2 (Receiving an individual call), A.5.2.6.2 (Receiver word sync), A.5.2.6.3 (Synchronization criteria).

#### US-LINK-002

> Als scannende Station will ich einen Calling Cycle standardkonform verarbeiten, damit ich auf Rufanfragen reagieren kann.

**Erfüllt durch:** REQ-LINK-004 bis REQ-LINK-006

---
#### US-LINK-003 — Link beenden und aufrechterhalten

**Epic:** EPIC-LINK
**Spec-Referenz:** A.5.5.3.1, A.5.5.3.2, A.5.5.3.3
**Priorität:** MUST · **Status:** offen

> Als verbundene Station will ich einen Link standardkonform beenden und aufrechterhalten, damit die Verbindung stabil bleibt oder sauber getrennt werden kann.

**Erfüllt durch:** _(Requirements werden mit dem Requirements-Prompt ergänzt)_

**Abgrenzung:** Diese Story beschreibt die Verwaltung des Links während der aktiven Verbindung, nicht die Initialisierung oder das Setup.

**Berührt auch:** A.5.5.3.4, A.5.5.3.5, A.5.5.3.6, A.5.5.3.7

---
#### US-LINK-004 — Manuelle Steuerung

**Epic:** EPIC-LINK
**Spec-Referenz:** A.5.5.3.1, A.5.5.3.2, A.5.5.3.3
**Priorität:** SHOULD · **Status:** offen

> Als Operator will ich manuell in den Verbindungsprozess eingreifen können, Notfallschutz auslösen und LQA-Werte einsehen, um die Funkverbindung bei Problemen zu steuern.

**Erfüllt durch:** _(Requirements werden mit dem Requirements-Prompt ergänzt)_

**Abgrenzung:** Diese Story beschreibt nur die manuelle Steuerung, nicht die automatischen Prozesse.

**Berührt auch:** A.5.5.3.1, A.5.5.3.2, A.5.5.3.3, A.5.5.3.4

---
#### US-LINK-005 — Mehrfachruf senden

**Epic:** EPIC-LINK
**Spec-Referenz:** A.5.5.4.1, A.5.5.4.2, A.5.5.4.3
**Priorität:** MUST · **Status:** offen

> Als rufende Station will ich Mehrfachrufe (Star Net, Star Group, AllCall, AnyCall) mit slotted Responses senden, damit mehrere Stationen effizient erreicht werden können.

**Erfüllt durch:** _(Requirements werden mit dem Requirements-Prompt ergänzt)_

**Abgrenzung:** Diese Story beschreibt nur das Senden von Mehrfachrufen, nicht das Empfangen.

**Berührt auch:** A.5.5.4.1, A.5.5.4.2, A.5.5.4.3, A.5.5.4.4, A.5.5.4.5

---
#### US-LINK-006 — Mehrfachruf empfangen

**Epic:** EPIC-LINK
**Spec-Referenz:** A.5.5.4.1, A.5.5.4.2, A.5.5.4.3
**Priorität:** MUST · **Status:** offen

> Als empfangende Station will ich auf Mehrfachrufe reagieren und slotted Responses senden, damit ich im Rahmen des TDMA-Schemas antworten kann.

**Erfüllt durch:** _(Requirements werden mit dem Requirements-Prompt ergänzt)_

**Abgrenzung:** Diese Story beschreibt nur das Empfangen von Mehrfachrufen, nicht das Senden.

**Berührt auch:** A.5.5.4.1, A.5.5.4.2, A.5.5.4.3, A.5.5.4.4, A.5.5.4.5


#### REQ-LINK-004 — Signal- und Symbolerkennung

**Spec-Referenz:** A.5.1.2 / A.5.2.6.2 · ⚠ Siehe OPEN-08
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System erkennt aus dem empfangenen Audiosignal die übertragenen Symbole.

**Akzeptanzkriterien:**

- `AC-LINK-004-1` — Ein sauber gesendetes Wort wird fehlerfrei zurückgewonnen (Loopback).

---

#### REQ-LINK-005 — End-of-Frame-Erkennung

**Spec-Referenz:** A.5.5.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Ende einer empfangenen Übertragung wird anhand einer gültigen Conclusion und der konstanten Wort-Phase erkannt.

**Akzeptanzkriterien:**

- `AC-LINK-005-1` — Nach einer gültigen Conclusion plus definiertem Wartedelay gilt die Übertragung als beendet.

---

#### REQ-LINK-006 — Adress-Erkennung im Scanning-Call

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Da der Scanning-Call nur das erste Adresswort überträgt, erkennt die Station einen an sie gerichteten Ruf bereits am ersten Wort ihrer eigenen Adresse.

**Akzeptanzkriterien:**

- `AC-LINK-006-1` — Eine Station mit mehr als drei Adresszeichen erkennt einen Scanning-Call, der nur ihr erstes Adresswort enthält.

---

### 9.5 Manual Operation — A.5.5.1

---
### REQ-LINK-007 — Manual Operation: Emergency Control
**Spec-Referenz:** A.5.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der ALE-Controller muss eine Notfallsteuerung durch den Operator ermöglichen. Jeder ALE-Controller muss eine manuelle Steuerungsfähigkeit bieten, um den Operator in Notfällen direkt an der Basis-SSB-Radio steuern zu können. Während aller anderen Zeiten soll das Radio automatisch gesteuert werden, und der Operator sollte das Radio über seinen zugehörigen Controller betreiben. Die empfangende und passive Erfassungsfähigkeit des Controllers, um „immer zu hören“ (z. B. Monitoring von Sounding-Signalen oder Warnung des Operators), darf nicht beeinträchtigt werden.

**Akzeptanzkriterien:**
- `AC-LINK-007-1` — Der ALE-Controller unterstützt Notfallsteuerung durch den Operator.
- `AC-LINK-007-2` — Jeder ALE-Controller bietet manuelle Steuerungsfähigkeit für den Operator.
- `AC-LINK-007-3` — In Notfällen kann der Operator direkt das Basis-SSB-Radio steuern.
- `AC-LINK-007-4` — Während normaler Betrieb wird das Radio automatisch gesteuert.
- `AC-LINK-007-5` — Der Operator betreibt das Radio über seinen zugehörigen Controller.
- `AC-LINK-007-6` — Die „always listening“-Fähigkeit des Controllers wird nicht beeinträchtigt.
- `AC-LINK-007-7` — Die Anforderung zur manuellen PTT-Bedienung gemäß 4.2.2 bleibt unberührt.

**Hinweis (NOTE aus A.5.5.1):** Diese Anforderung hebt die manuelle Push-to-Talk-Betriebsanforderung gemäß 4.2.2 nicht auf.

---

### 9.6 ALE Grundlagen — A.5.5.2

### REQ-LINK-008 — ALE Three-Way-Handshake
**Spec-Referenz:** A.5.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das grundlegende Protokoll zur Verbindungsaufnahme ist das dreifache Handshake (siehe Appendix I des Standards für den Überblick über selektives Rufen). Ein dreifacher Handshake ist ausreichend, um eine Verbindung zwischen einer rufenden Station und einer antwortenden Station herzustellen. Mit der Ergänzung von slotted Responses (beschrieben in A.5.5.4.2) kann dieselbe Aufruf-/Antwort-/Bestätigungssequenz auch eine einzelne rufende Station mit mehreren antwortenden Stationen verbinden.

**Akzeptanzkriterien:**
- `AC-LINK-008-1` — Das dreifache Handshake-Protokoll ist ausreichend für eine Verbindung.
- `AC-LINK-008-2` — Slotted Responses ermöglichen die Verbindung einer Station mit mehreren Stationen.
- `AC-LINK-008-3` — Die Handshake-Protokolle sind identisch für Einzel- und Mehr-Station-Verbindungen.

---

### 9.7 Timing — A.5.5.2.1

### REQ-LINK-009 — Timing-Funktionen und -Werte
**Spec-Referenz:** A.5.5.2.1 / Table A-XV
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System hängt von einer Auswahl an Timing-Funktionen ab, um die Effizienz und Effektivität des ALE zu optimieren. Die primären Timing-Funktionen und -Werte sind in Tabelle A-XV aufgeführt. Anhang A definiert die Timing-Symbole und Anhang B erklärt die Timing-Analyse und -Berechnung. Wenn ein ALE-Orderwire-Protokoll (AMD, DTM oder DBM) den Basis-Message-Abschnitt verlängert, darf dies frühestens ab dem Start des 30. Worts (11,368 s) beginnen. Die Verlängerung wird durch die Länge des entsprechenden Protokolls bestimmt; der Message-Abschnitt endet am Ende des Orderwire ohne weitere Verlängerung. Die Conclusion beginnt unmittelbar am Ende des Message-Abschnitts.

**Akzeptanzkriterien:**
- `AC-LINK-009-1` — Das System implementiert alle Timing-Funktionen gemäß Tabelle A-XV.
- `AC-LINK-009-2` — Alle Standard-Timing-Werte (Trw = 392 ms, Tw = 130,66… ms, Ts max = 50 s, etc.) werden eingehalten.
- `AC-LINK-009-3` — Ein verlängerter Message-Abschnitt (AMD/DTM/DBM) beginnt nicht vor dem Start des 30. Worts (11,368 s).
- `AC-LINK-009-4` — Der Message-Abschnitt endet am Ende des Orderwire-Protokolls ohne weitere Verlängerung.
- `AC-LINK-009-5` — Die Conclusion beginnt unmittelbar am Ende des Message-Abschnitts.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Symbol | Wert / Formel | Spec-Referenz |
|---|---|---|---|
| **Basis-Timing** | | | |
| Tone rate | — | 125 Symbole/s | Table A-XV |
| Tone period | Ttone | 8 ms | Table A-XV |
| On-air rate | — | 375 b/s | Table A-XV |
| On-air word | Tw | 130,66… ms | Table A-XV |
| On-air redundant word | Trw | 3 × Tw = 392 ms | Table A-XV |
| On-air leading redundant words | Tlrw | 2 × Trw = 784 ms | Table A-XV |
| On-air individual (net) address time | Ta | m × Trw; m = 1…5; Ta = 392…1960 ms | Table A-XV |
| Propagation delay | Tp | 0…70 ms | Table A-XV |
| **System-Limits** | | | |
| Address size limit | Ta max | 5 Wörter = 1960 ms | Table A-XV |
| Address first word limit | Tal | 392 ms | Table A-XV |
| Call time maximum (½ Tlc, 12 Wörter max) | Tc | 4704 ms | Table A-XV |
| Group addresses first word limit | Tcl | 1960 ms | Table A-XV |
| Maximum scan period | Ts max | 50 s | Table A-XV |
| Message section basic time | Tm max basic | 11,76 s | Table A-XV |
| Message section time limit, AMD (90 Zeichen) | Tm max AMD | 11,76 s | Table A-XV |
| Message section time limit, DTM (1053 Zeichen) | Tm max DTM | 2,29 min | Table A-XV |
| Message section time limit, DBM (37377 Zeichen) | Tm max DBM | 23,26 min | Table A-XV |
| Termination time limit | Tx max | 1960 ms | Table A-XV |
| **Individual Calling Timing** | | | |
| Minimum dwell time (5 ch/s scanning) | Td(5) min | 200 ms | Table A-XV |
| Minimum dwell time (2 ch/s scanning) | Td(2) min | 500 ms | Table A-XV |
| Probable max dwell per channel | Tdrw | 784 ms | Table A-XV |
| Leading call time | Tlc | 2 × Tc | Table A-XV |
| Scanning call time | Tsc | n × Tcl ≥ Ts | Table A-XV |
| Calling cycle time | Tcc | Tsc + Tlc ≥ Ts + Tlc | Table A-XV |
| Last word wait delay | Tlww | Trw = 392 ms | Table A-XV |
| Wait for reply time delay | Twr | Ttd + Tp + Tlww + Tta + Trwp + Tld + Tp + Trd (Formel, hardwarabhängig) | Table A-XV |
| Late detect delay | Tld | Tw = 130,66… ms | Table A-XV |
| Redundant word phase delay | Trwp | 0…Trw (0…392 ms) | Table A-XV |
| Wait for calling cycle end time | Twce | 2 × own Ts (Standard) | Table A-XV |
| Tune time | Tt | Hardwareabhängig (Default: 8 Tw = 1045,33… ms) | Table A-XV |
| Wait for reply and tune time | Twrt | Twr + Tt | Table A-XV |
| Detect signaling period | Tds | ≤ Td(5) = 200 ms | Table A-XV |
| Detect redundant word period | Tdrw | Trw + spare Trw = 784 ms | Table A-XV |
| Detect rotating redundant word period | Tdrrw | 2 × Trw + spare Trw = 1176 ms | Table A-XV |
| **Sounding Timing** | | | |
| Redundant sound time | Trs | 2 × Ta (rufende Station); abhängig von Adresslänge | Table A-XV |
| Scanning sound time | Tss | n × Ta (rufende Station) ≥ Ts | Table A-XV |
| Scanning redundant sound time | Tsrs | Tss + Trs ≥ Ts + Trs | Table A-XV |
| **Star Calling Timing** | | | |
| Minimum standard slot widths | Tsw min | 14 Tw oder 17 Tw (1. Handshake-Slots); 17 Tw oder 20 Tw (Folge-Slots); oder andere per CMD | Table A-XV |
| Slot widths | Tsw | 14, 17, 9 oder andere Tw | Table A-XV |
| Slot wait time (Uniform-Fall) | Tswt | Tsw × SN | Table A-XV |
| Slot wait time (allgemeine Formel) | Tswt(SN) | SN × [5 Tw + 2 Ta(caller) + (opt. LQA) Trw + (opt. Msg) Tm] + Ta(caller) + Σ Ta(m)(called) für m=1…SN-1 | Table A-XV |
| Wait for net reply (rufende Station) | Twrn | Tswt(NS + 1) allgemein; = Tsw × NS im Uniform-Fall | Table A-XV |
| Wait for net acknowledgment (gerufene Stationen) | Twan | Twrn + 2 × Trw | Table A-XV |
| Turnaround + Tune limit | Tta + Tt | ≤ 360 ms (Slot 0), ≤ 2100 ms (Slot 1), ≤ 1500 ms (andere Slots) | Table A-XV |
| Maximum star group wait for acknowledgment | Twan max | 107 Tw + 27 Ta(caller) + 13 Trw (opt. LQA) + 13 Tm (opt. Msg) | Table A-XV |
| Late arrival stations (Twan max) | — | 188 Tw (ohne LQA) oder 227 Tw (mit LQA) | Table A-XV |
| **Programmierbare Timing-Parameter (Typwerte)** | | | |
| Wait (listen first), allgemein | Twt | 2 s | Table A-XV |
| Wait (listen first), ALE/data-only channels | Twt | 784 ms | Table A-XV |
| Tune time (Standard, „blind" first call) | Tt | 8 Tw = 1045,33… ms | Table A-XV |
| Tune time (nächster Versuch) | Tt | 20 s | Table A-XV |
| Automatic sounding interval | Tps | 30 min | Table A-XV |
| Wait for activity | Twa | 30 s | Table A-XV |

---

### 9.8 ALE States — A.5.5.2.2

### REQ-LINK-010 — ALE States
**Spec-Referenz:** A.5.5.2.2 / Figure A-28
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein ALE-Controller kann als sich in einem von drei konzeptionellen „Zuständen“ befinden. Siehe Abbildung A-28.

**Akzeptanzkriterien:**
- `AC-LINK-010-1` — Der Controller kann sich in einem von drei Zuständen befinden.
- `AC-LINK-010-2` — Die Zustände sind konzeptionell definiert.
- `AC-LINK-010-3` — Die Abbildung A-28 zeigt die Zustände.

---

### 9.9 Channel Selection — A.5.5.2.3

### REQ-LINK-011 — Channel Selection: Scanning Call
**Spec-Referenz:** A.5.5.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine scannende rufende Station muss ALE-Aufrufe auf ihren gescannten Kanälen in der Reihenfolge senden, die durch ihren Kanal-Auswahl-Algorithmus bestimmt wird. Sie muss auf dem ersten Kanal, der einen Handshake mit der gerufenen Station(en) unterstützt, den Link herstellen.

**Akzeptanzkriterien:**
- `AC-LINK-011-1` — Die rufende Station sendet Aufrufe auf gescannten Kanälen in der durch den Kanal-Auswahl-Algorithmus bestimmten Reihenfolge.
- `AC-LINK-011-2` — Der Link wird auf dem ersten Kanal hergestellt, der einen erfolgreichen Handshake unterstützt.

---

### 9.10 Channel Selection — A.5.5.2.3.1

### REQ-LINK-012 — Channel Rejection
**Spec-Referenz:** A.5.5.2.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn ein Kanal nach dem Verknüpfen durch den Operator oder Controller als unbrauchbar abgelehnt wird, muss der ALE-Controller den Link gemäß A.5.5.3.5 beenden und die LQA-Daten mit Messungen, die während des Verknüpfens erhalten wurden, aktualisieren.

**Akzeptanzkriterien:**
- `AC-LINK-012-1` — Der Link wird beendet, wenn ein Kanal abgelehnt wird.
- `AC-LINK-012-2` — Die LQA-Daten werden aktualisiert.
- `AC-LINK-012-3` — Die Messungen stammen aus dem Verknüpfungsprozess.

---

### 9.11 Channel Selection — A.5.5.2.3.2

### REQ-LINK-013 — Busy Channel
**Spec-Referenz:** A.5.5.2.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Während des Scanning-Calling-Zyklus kann ein Caller auf besetzte Kanäle stoßen und diese überspringen, um Störungen an bestehendem Verkehr zu vermeiden. Nachdem alle verfügbaren Kanäle versucht wurden, und wenn kein Kontakt hergestellt wurde, sollte der Caller die zuvor besetzten Kanäle erneut durchsuchen und, wenn sie frei sind, versuchen, einen Aufruf zu starten.

**Akzeptanzkriterien:**
- `AC-LINK-013-1` — Besetzte Kanäle werden übersprungen.
- `AC-LINK-013-2` — Störungen werden verhindert.
- `AC-LINK-013-3` — Zuvor besetzte Kanäle werden erneut durchsucht.
- `AC-LINK-013-4` — Aufruf wird gestartet, wenn Kanäle frei sind.

---

### 9.12 Channel Selection — A.5.5.2.3.3

### REQ-LINK-014 — Exhausted Channel List
**Spec-Referenz:** A.5.5.2.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine rufende Station alle ihre vorab vereinbarten Scan-Kanäle erschöpft hat und keinen Link herstellen konnte, muss sie sofort zum normalen Empfangsscan-Modus (der verfügbare Zustand) zurückkehren. Sie muss auch den Operator (und den Netzwerk-Controller, falls vorhanden) benachrichtigen, dass der Aufrufversuch fehlgeschlagen ist.

**Akzeptanzkriterien:**
- `AC-LINK-014-1` — Der Link-Versuch wird beendet, wenn alle Kanäle erschöpft sind.
- `AC-LINK-014-2` — Der Zustand wechselt zum verfügbaren Scan-Modus.
- `AC-LINK-014-3` — Der Operator wird benachrichtigt.
- `AC-LINK-014-4` — Der Netzwerk-Controller wird benachrichtigt, falls vorhanden.

---

### 9.13 End of Frame Detection — A.5.5.2.4

### REQ-LINK-015 — Frame End Detection
**Spec-Referenz:** A.5.5.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** ALE-Controller müssen das Ende eines empfangenen ALE-Signals anhand der folgenden Methoden identifizieren. Der Controller sucht nach einer gültigen Conclusion (TIS oder TWAS, möglicherweise gefolgt von DATA und REP für maximal fünf Wörter, oder Tx max). Die Conclusion muss konstante redundante Wortphase innerhalb sich selbst (wenn ein Sound) und mit den vorhergehenden Wörtern aufrechterhalten. Der Controller prüft jedes aufeinanderfolgende redundante Wortphasen (Trw) nach dem TIS (oder TWAS) für das erste (bis zu vier) nicht lesbare oder ungültige Wort(e). Der Fehler bei der Erkennung eines gültigen Wortes (oder die Erkennung eines ungültigen Wortes) oder die Erkennung des letzten REP, plus die letzte Wort-Wartezeit (Tlww oder Trw), zeigt das Ende der empfangenen Übertragung an. Die maximal akzeptable Abschlussfolge ist TIS (oder TWAS), DATA, REP, DATA, REP.

**Akzeptanzkriterien:**
- `AC-LINK-015-1` — Das Ende eines empfangenen ALE-Signals wird durch Suche nach einer gültigen Conclusion (TIS oder TWAS, gefolgt von max. 5 Wörtern DATA/REP, oder Tx max) identifiziert.
- `AC-LINK-015-2` — Die Conclusion muss konstante redundante Wortphase innerhalb sich selbst (bei Sound) und gegenüber den vorangegangenen Wörtern aufrechterhalten.
- `AC-LINK-015-3` — Der Controller prüft jede aufeinanderfolgende Trw-Phase nach TIS/TWAS auf das erste (von bis zu vier) nicht lesbaren oder ungültigen Wort(e).
- `AC-LINK-015-4` — Fehlendes gültiges Wort, erkanntes ungültiges Wort oder Erkennung des letzten REP, plus Last-Word-Wait-Delay (Tlww = Trw = 392 ms), zeigt das Übertragungsende an.
- `AC-LINK-015-5` — Die maximal akzeptable Abschlussfolge ist: TIS (oder TWAS), DATA, REP, DATA, REP.

---

### 9.14 One-to-One Calling — A.5.5.3

### REQ-LINK-016 — One-to-One Calling Protocol
**Spec-Referenz:** A.5.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Protokoll zur Herstellung einer Verbindung zwischen zwei einzelnen Stationen besteht aus drei ALE-Frames: einem Aufruf, einer Antwort und einer Bestätigung. Die Sequenz der Ereignisse und die damit verbundenen Zeitüberschreitungen werden in den folgenden Abschnitten mit Hilfe einer rufenden Station SAM und einer gerufenen Station JOE beschrieben.

**Akzeptanzkriterien:**
- `AC-LINK-016-1` — Das Protokoll zum Verbindungsaufbau besteht aus genau drei ALE-Frames: Aufruf (Call), Antwort (Response) und Bestätigung (Acknowledgment).
- `AC-LINK-016-2` — Kein Link gilt als hergestellt, bevor alle drei Frames erfolgreich ausgetauscht wurden.
- `AC-LINK-016-3` — Alle in A.5.5.3.1–A.5.5.3.4 definierten Zeitüberschreitungen (Twr, Twce, Tlww, Tmmax, Txmax) werden eingehalten.

---

### 9.15 One-to-One Calling — A.5.5.3.1

### REQ-LINK-017 — Sending an Individual Call
**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Nach Auswahl eines Kanals für das Rufen beginnt die rufende Station (SAM) das Protokoll, indem sie zunächst auf dem Kanal hört, um aktive Kanäle zu vermeiden, und anschließend den Kanal abstimmt. Ist bekannt, dass die gerufene Station (JOE) auf dem gewählten Kanal hört (nicht scannt), sendet die rufende Station einen Einzelkanal-Aufruf, der nur einen Leading Call und eine Conclusion enthält (Figure A-29, oben). Andernfalls sendet sie einen längeren Calling Cycle, dem ein Scanning Call vorangestellt ist, der lang genug ist, um den Empfänger der gerufenen Station während des Scannens zu erfassen (Figure A-29, unten). Die Dauer des Scanning Calls muss 2 Trw für jeden Kanal betragen, den die gerufene Station scannt. Der Scanning-Call-Abschnitt enthält ausschließlich das erste Wort der gerufenen Stationsadresse mit TO-Präambel, so oft wiederholt wie nötig. Die vollständige Zieladresse wird im Leading Call zweimal übertragen (TO-Präambel beim ersten Wort, DATA/REP für Erweiterungen). Auf den Leading Call folgt optional ein Message-Abschnitt und dann eine Conclusion mit der vollständigen Adresse der rufenden Station (TIS SAM). Die rufende Station wartet danach eine voreingestellte Antwortzeit (Twr für Einzelkanal, Twrt für Mehrkanal). Kommt keine Antwort innerhalb dieser Zeit, gilt der Verbindungsversuch auf diesem Kanal als gescheitert; falls weitere Kanäle verfügbar sind, wird der Versuch auf einem neuen Kanal fortgesetzt, sonst kehrt der ALE-Controller in den verfügbaren Zustand zurück.

**Akzeptanzkriterien:**
- `AC-LINK-017-1` — Die rufende Station hört auf dem Kanal, bevor sie sendet (Listen-Before-Transmit).
- `AC-LINK-017-2` — Die rufende Station stimmt den Kanal ab, bevor sie sendet.
- `AC-LINK-017-3` — Bei bekanntem Einzelkanal-Hörer: Aufruf enthält nur Leading Call und Conclusion.
- `AC-LINK-017-4` — Bei scannender Gegenstation: dem Leading Call ist ein Scanning Call vorangestellt.
- `AC-LINK-017-5` — Die Dauer des Scanning Calls beträgt 2 Trw für jeden gescannten Kanal der gerufenen Station.
- `AC-LINK-017-6` — Der Scanning-Call-Abschnitt enthält ausschließlich das erste Adresswort der gerufenen Station (TO-Präambel).
- `AC-LINK-017-7` — Die vollständige Zieladresse wird im Leading Call zweimal übertragen.
- `AC-LINK-017-8` — Bei ausbleibender Antwort innerhalb Twr (Einzelkanal) oder Twrt (Mehrkanal) wird der nächste Kanal versucht oder der Versuch abgebrochen.

---

### 9.16 One-to-One Calling — A.5.5.3.2

### REQ-LINK-018 — Receiving an Individual Call
**Spec-Referenz:** A.5.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn die gerufene Station (JOE) auf dem Kanal ankommt, irgendwann während ihres Scan-Zeitraums Ts, und daher während des Aufrufzyklus von SAM, prüft die Station, ob ALE-Signale empfangen wurden. Wenn ALE-Signale empfangen wurden und die Station das Wort-Sync erreicht, prüft sie das empfangene Wort, um die geeignete Aktion zu bestimmen. Wenn JOE „TO JOE“ (oder eine akzeptable Entsprechung gemäß Protokollen) liest, stoppt die ALE-Station den Scan, wechselt in den Link-Zustand und liest weiter ALE-Wörter, während sie auf eine vordefinierte, begrenzte Zeit Twce wartet, bis der Aufrufzyklus endet und die Nachricht oder die Schlussfolge beginnt. Wenn das empfangene Wort potenziell von einem Sound oder einem anderen Protokoll stammt, verarbeitet die ALE-Station das Wort gemäß diesem Protokoll. Andernfalls kehrt die ALE-Station in ihren vorherigen Zustand zurück (z. B. verfügbar, wenn sie scannte, verknüpft, wenn sie mit einer anderen Station verknüpft war). Während des Lesens eines Aufrufs im Link-Zustand prüft die Station jedes neue empfangene Wort. Die Station bricht den Handshake sofort ab und kehrt in ihren vorherigen Zustand zurück, wenn eines der folgenden auftritt: Es wird nicht innerhalb Twce der Start einer schnellen-ID, Nachricht oder Frame-Schlussfolge empfangen, oder der Start einer Schlussfolge innerhalb Tmmax nach Beginn des Nachrichtenabschnitts; Ungültige Sequenz von ALE-Wort-Preambles wird empfangen, außer dass während des Empfangs eines Scanning-Aufrufs bis zu drei aufeinanderfolgende Wörter mit unkorrigierbaren Fehlern toleriert werden, ohne dass der Frame abgelehnt wird; Das Ende der Schlussfolge wird nicht innerhalb Tlww (plus die zusätzlichen Vielfachen von Trw, wenn eine erweiterte Adresse) nach dem ersten Wort der Schlussfolge erkannt wird.

**Akzeptanzkriterien:**
- `AC-LINK-018-1` — Erkennt die Station ALE-Signale und erreicht Word-Sync, prüft sie das empfangene Wort zur Aktionsbestimmung.
- `AC-LINK-018-2` — Bei Empfang von „To JOE“ (oder akzeptabler Entsprechung): Scan stoppen, Linking-Zustand eintreten, ALE-Wörter weiterlesen, auf Ablauf von Twce warten.
- `AC-LINK-018-3` — Bei potenziellem Sound oder anderem Protokoll: Wort gemäß diesem Protokoll verarbeiten.
- `AC-LINK-018-4` — Andernfalls: in vorherigen Zustand zurückehren (verfügbar oder gelinkt).
- `AC-LINK-018-5` — Abbruchbedingung 1: Kein Start von Quick-ID, Message oder Conclusion innerhalb Twce; oder kein Conclusionstart innerhalb Tmmax nach Beginn des Message-Abschnitts.
- `AC-LINK-018-6` — Abbruchbedingung 2: Ungültige Wortpräambel-Sequenz empfangen (Ausnahme: bis zu drei aufeinanderfolgende Wörter mit unkorrigierbaren Fehlern im Scanning Call werden toleriert).
- `AC-LINK-018-7` — Abbruchbedingung 3: Ende der Conclusion nicht innerhalb Tlww (plus etwaige Trw-Vielfache bei erweiterter Adresse) nach dem ersten Conclusionwort erkannt.
- `AC-LINK-018-8` — Bei erfolgreich empfangener TIS-Conclusion: Last-Word-Wait-Timeout Tlww = Trw starten und auf weitere Adresswörter sowie Frameende warten; dann Response auslösen.
- `AC-LINK-018-9` — Bei empfangener TWAS-Conclusion: nicht antworten, sofort in vorherigen Zustand zurückehren.

---

### 9.17 One-to-One Calling — A.5.5.3.3

### REQ-LINK-019 — Response
**Spec-Referenz:** A.5.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei Empfang eines Aufrufs, der an eine eigene Selbstadresse (JOE) gerichtet ist und eine gültige rufende Stationsadresse in einer TIS-Conclusion (SAM) enthält, prüft die gerufene Station, ob der Kanal frei ist. Ist der Kanal frei, stimmt sie ab, sendet eine Antwort (Figure A-30) und startet den eigenen Antwort-Timer Twr. Der längere Timeout Twrt ist nur erforderlich, wenn die rufende Station ihre Bestätigung auf einem anderen Kanal senden wird (Neuabstimmung nötig). Ist der Kanal belegt, ignoriert die ALE-Station den Aufruf und kehrt in ihren vorherigen Zustand zurück, sofern nicht anders konfiguriert. Die rufende Station (SAM) verarbeitet die Response mit denselben Prüfungen und Timeouts wie beim Empfang des Calls. SAM bricht den Handshake sofort ab bei: (1) kein passender Response-Calling-Cycle „TO SAM" innerhalb des Timeouts; (2) ungültige Wortpräambel-Sequenz; (3) keine Conclusion „TIS JOE" innerhalb Tlc (plus Tm max bei Message); (4) Ende der Conclusion nicht innerhalb Tlww nach dem ersten Conclusionwort. Nach Abbruch startet SAM das Rufprotokoll in der Regel auf einem anderen Kanal neu. Wird „TWAS JOE" empfangen, hat die gerufene Station den Verbindungsaufbau abgelehnt; SAM bricht den Versuch ab und informiert den Operator.

**Akzeptanzkriterien:**
- `AC-LINK-019-1` — Die gerufene Station prüft die Kanalbelegung, bevor sie antwortet.
- `AC-LINK-019-2` — Bei freiem Kanal: abstimmen, Antwort senden (Figure A-30), Timer Twr starten.
- `AC-LINK-019-3` — Bei belegtem Kanal: Aufruf ignorieren, in vorherigen Zustand zurückkehren.
- `AC-LINK-019-4` — Twr gilt für Einzelkanal; Twrt gilt, wenn Bestätigung auf anderem Kanal erwartet wird.
- `AC-LINK-019-5` — SAM verarbeitet die Response mit denselben Prüfungen und Timeouts wie beim Empfang des Calls.
- `AC-LINK-019-6` — SAM-Abbruchbedingung 1: kein „TO SAM" innerhalb Twr/Twrt.
- `AC-LINK-019-7` — SAM-Abbruchbedingung 2: ungültige Wortpräambel-Sequenz.
- `AC-LINK-019-8` — SAM-Abbruchbedingung 3: keine Conclusion „TIS JOE" innerhalb Tlc (+Tm max).
- `AC-LINK-019-9` — SAM-Abbruchbedingung 4: Ende der Conclusion nicht innerhalb Tlww erkannt.
- `AC-LINK-019-10` — Bei „TWAS JOE": SAM bricht Verbindungsaufbau ab und informiert den Operator.

---

### 9.18 One-to-One Calling — A.5.5.3.4

### REQ-LINK-020 — Acknowledgment
**Spec-Referenz:** A.5.5.3.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn alle Kriterien für eine akzeptable Antwort erfüllt sind und dies nicht vom Operator oder Netzwerk-Controller direkt angeordnet wurde, sendet die rufende Station eine ALE-Bestätigung (siehe Abb. A-31), wechselt in den verknüpften Zustand mit der gerufenen Station (JOE) und schaltet den Lautsprecher ein. Ein „Warten auf Aktivität“-Timer Twa wird gestartet (mit einem typischen Timeout von 30 Sekunden), der den Link beendet, wenn der Link für einen längeren Zeitraum ungenutzt bleibt (siehe A.5.5.3.5). Wenn die gerufene Station (JOE) einen passenden Antwortzyklus („TO SAM“) innerhalb ihres Timeout (entweder Twr oder Twrt) empfängt, verarbeitet sie den Rest des Frames gemäß den obigen Prüfungen und Zeitüberschreitungen für den Aufruf, bis sie entweder den Handshake abbricht oder die entsprechende Schlussfolge liest, die in diesem Beispiel „TIS JOE“ ist. Speziell bricht die rufende Station den Handshake sofort ab, wenn eines der folgenden auftritt: Es wird nicht innerhalb des Timeout ein passender Antwortzyklus („TO SAM“) empfangen; Eine ungültige Sequenz von ALE-Wort-Preambles tritt auf; Es wird nicht innerhalb Tlc (plus Tm max, wenn eine Nachricht enthalten ist) die entsprechende Schlussfolge empfangen; Das Ende der Schlussfolge wird nicht innerhalb Tlww (plus die zusätzlichen Vielfachen von Trw, wenn eine erweiterte Adresse) nach dem ersten Wort der Schlussfolge erkannt wird. Nach dem Abbruch eines Handshakes aus einem der oben genannten Gründe, startet die rufende Station normalerweise das Aufrufprotokoll, normalerweise auf einem anderen Kanal. Wenn die rufende Station die korrekte Schlussfolge von der gerufenen Station („TIS JOE“) empfängt, setzt sie einen letzten Wort-Warte-Timer wie oben beschrieben und bereitet sich auf das Senden einer Bestätigung vor. Wenn stattdessen „TWAS JOE“ empfangen wird, hat die gerufene Station den Verbindungsaufbau abgelehnt, und die rufende Station bricht den Verbindungsaufbau ab und informiert den Operator über den abgelehnten Versuch.

**Akzeptanzkriterien:**
- `AC-LINK-020-1` — SAM sendet ACK, wechselt in den Linked-Zustand mit JOE, schaltet den Lautsprecher ein und startet Timer Twa (Standard: 30 s).
- `AC-LINK-020-2` — SAM-Abbruchbedingung 1: kein passender ACK-Calling-Cycle „To JOE“ innerhalb Twr.
- `AC-LINK-020-3` — SAM-Abbruchbedingung 2: ungültige Wortpräambel-Sequenz.
- `AC-LINK-020-4` — SAM-Abbruchbedingung 3: keine Conclusion innerhalb Tlc (+Tm max) nach Framestart.
- `AC-LINK-020-5` — SAM-Abbruchbedingung 4: Ende der Conclusion nicht innerhalb Tlww erkannt.
- `AC-LINK-020-6` — JOE liest ACK mit denselben Prüfungen wie beim Call; bei TIS SAM: Linked-Zustand eintreten, Operator und Network-Controller informieren, Lautsprecher einschalten, Twa starten.
- `AC-LINK-020-7` — Bei TWAS SAM im ACK: JOE kehrt in den Pre-Linking-Zustand zurück.
- `AC-LINK-020-8` — Kommt SAMs ACK nach Ablauf von JOEs Twr, behandelt JOE es als neuen Einzelruf (nicht als Duplikat).

**Hinweise (NOTEs aus A.5.5.3.4):**
- NOTE 1: SAMs ACK erscheint identisch zu einem neuen Einzelruf — JOE sendet keine zweite Response, weil das ACK innerhalb des Twr-Fensters nach JOEs Response ankommt. Ein ACK nach Ablauf von Twr wird von JOE als neuer Ruf behandelt.
- NOTE 2: Ein typischer One-to-One Scanning-Call-Handshake (drei Frames) dauert zwischen 9 und 14 Sekunden.

---

### 9.19 One-to-One Calling — A.5.5.3.5

### REQ-LINK-021 — Link Termination
**Spec-Referenz:** A.5.5.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Beendigung eines Links nach einem erfolgreichen Handshake wird durch das Senden eines Frames mit einer TWAS-Ende-Struktur an alle verknüpften Stationen durchgeführt, die beendet werden sollen. Zum Beispiel, „TO JOE, TO JOE, TWAS SAM“ (wenn von SAM gesendet) beendet den Link zwischen Stationen SAM und JOE. JOE schaltet sofort den Lautsprecher aus und kehrt in den verfügbaren Zustand zurück, es sei denn, sie behält einen Link mit anderen Stationen auf dem Kanal. Ebenso kehrt SAM sofort in den verfügbaren Zustand zurück, es sei denn, sie behält einen Link mit anderen Stationen auf dem Kanal.

**Akzeptanzkriterien:**
- `AC-LINK-021-1` — Der Link wird durch TWAS beendet.
- `AC-LINK-021-2` — Alle verknüpften Stationen werden benachrichtigt.
- `AC-LINK-021-3` — Der Lautsprecher wird ausgeschaltet.
- `AC-LINK-021-4` — Die Station kehrt in den verfügbaren Zustand zurück.
- `AC-LINK-021-5` — Verknüpfungen mit anderen Stationen bleiben erhalten.

---

### 9.20 One-to-One Calling — A.5.5.3.5.1

### REQ-LINK-022 — Manual Termination
**Spec-Referenz:** A.5.5.3.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Mittel muss zur Verfügung stehen, das es dem Operator ermöglicht, eine Station manuell zurückzusetzen, was den Lautsprecher (s) stummschaltet, den ALE-Controller in den verfügbaren Zustand zurückversetzt und eine Link-Befehlsübertragung (TWAS) sendet, wie oben beschrieben, an alle verknüpften Stationen, es sei denn, diese Funktion wird vom Operator überschrieben. (DO: Stellen Sie eine manuelle Trennungsfunktion bereit, die einzelne Links trennt, während andere erhalten bleiben.)

**Akzeptanzkriterien:**
- `AC-LINK-022-1` — Der Operator kann manuell eine Station zurücksetzen.
- `AC-LINK-022-2` — Der Lautsprecher wird stummgeschaltet.
- `AC-LINK-022-3` — Der Controller kehrt in den verfügbaren Zustand zurück.
- `AC-LINK-022-4` — TWAS wird gesendet.
- `AC-LINK-022-5` — Andere Links bleiben erhalten.

---

### 9.21 One-to-One Calling — A.5.5.3.5.2

### REQ-LINK-023 — Automatic Termination
**Spec-Referenz:** A.5.5.3.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn keine Sprach-, Daten- oder Steuerinformation von einer Station innerhalb einer vordefinierten Aktivitätszeit (Twa) gesendet oder empfangen wird, schaltet der ALE-Controller den Lautsprecher aus, beendet den verknüpften Zustand mit allen verknüpften Stationen und kehrt in den verfügbaren Zustand zurück. Der Warte-Timer für Aktivität ist obligatorisch, kann aber vom Operator oder Netzwerkmanager deaktiviert werden. Diese zeitgesteuerte Zurücksetzung erfordert nicht, dass eine Befehlsübertragung (TWAS) gesendet wird, wie oben beschrieben. Es wird jedoch empfohlen, eine Befehlsübertragung zu senden, um die anderen verknüpften Stationen sofort in den verfügbaren Zustand zurückzusetzen. Die Beendigung während eines Handshakes oder Protokolls durch TWAS (oder einen Timer) sollte die empfangende (oder zeitüberschreitende) Station beenden, den Link mit dieser Station beenden, erneut stummschalten und sofort in den verfügbaren Zustand zurückkehren, es sei denn, sie behält einen Link mit einer anderen Station.

**Akzeptanzkriterien:**
- `AC-LINK-023-1` — Der Timer Twa wird gestartet.
- `AC-LINK-023-2` — Bei Aktivitätszeitüberschreitung wird der Link beendet.
- `AC-LINK-023-3` — Der Lautsprecher wird ausgeschaltet.
- `AC-LINK-023-4` — Die Station kehrt in den verfügbaren Zustand zurück.
- `AC-LINK-023-5` — Der Timer kann vom Operator deaktiviert werden.
- `AC-LINK-023-6` — Es wird empfohlen, TWAS zu senden.
- `AC-LINK-023-7` — Die Station kehrt in den verfügbaren Zustand zurück.

---

### 9.22 One-to-One Calling — A.5.5.3.6

### REQ-LINK-024 — Collision Detection
**Spec-Referenz:** A.5.5.3.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Während des Empfangs eines ALE-Signals kann die Kontinuität des empfangenen Signals verloren gehen (aufgrund von Faktoren wie Störung oder Ausbleichen), wie durch das Fehlen eines guten ALE-Wortes an einem Trw-Grenzwert angezeigt. Wenn eines oder beide Golay-Wörter eines empfangenen ALE-Wortes unkorrigierbare Fehler enthalten, versucht der ALE-Controller, das Wort-Sync wiederherzustellen, wobei ein Vorteil für Wörter gegeben wird, die zu derselben Wortphase wie das unterbrochene Frame gelangen. Wenn das Wort-Sync wiederhergestellt wird, aber bei einer neuen Wortphase, zeigt dies an, dass ein Kollision aufgetreten ist. Das unterbrochene Frame wird verworfen, und das unterbrechende Signal wird als neuer ALE-Frame verarbeitet.

**Akzeptanzkriterien:**
- `AC-LINK-024-1` — Die Kontinuität des Signals kann verloren gehen.
- `AC-LINK-024-2` — Unkorrigierbare Fehler werden erkannt.
- `AC-LINK-024-3` — Das Wort-Sync wird wiederhergestellt.
- `AC-LINK-024-4` — Bei neuer Wortphase wird eine Kollision angezeigt.
- `AC-LINK-024-5` — Das unterbrochene Frame wird verworfen.
- `AC-LINK-024-6` — Das unterbrechende Signal wird als neuer Frame verarbeitet.

---

### 9.23 One-to-Many Calling — A.5.5.4

### REQ-LINK-025 — One-to-Many Calling Protocol
**Spec-Referenz:** A.5.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Station kann gleichzeitig eine Mehr-Wege-Verbindung mit mehreren anderen Stationen herstellen, unter Verwendung der in den folgenden Unterabschnitten beschriebenen Protokolle.

**Akzeptanzkriterien:**
- `AC-LINK-025-1` — Eine Station kann gleichzeitig mit mehreren Stationen verknüpfen.
- `AC-LINK-025-2` — Die Protokolle sind beschrieben.

---

### 9.24 One-to-Many Calling — A.5.5.4.1

### REQ-LINK-026 — Slotted Responses
**Spec-Referenz:** A.5.5.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das einfache dreifache Handshake-Protokoll, das für individuelle Links verwendet wird, kann nicht für einen Mehr-Wege-Aufruf verwendet werden, da die Antworten von den gerufenen Stationen miteinander kollidieren würden. Stattdessen wird ein Zeitdivision-Multiple-Access (TDMA)-Schema verwendet. Jede gerufene Station sendet ihre Antwort in einem zugewiesenen oder berechneten Zeitfenster, wie später für das jeweilige Mehr-Wege-Protokoll beschrieben. Am Ende eines Mehr-Wege-Aufrufs müssen folgende Ereignisse eintreten: Der aufrufende Station wird ein Warten auf Antwort- und Tunen-Zeitfenster (WRTT) gesetzt, das ihre Bestätigung nach dem letzten Antwort-Zeitfenster auslöst. Der Wert dieser WRTT wird später für jedes Mehr-Wege-Protokoll beschrieben. Die gerufenen Stationen setzen ihre eigenen WRTTs, die ihre Wartezeiten für eine Bestätigung begrenzen. Um Zeit für das Erhalten des Wort-Sync während des führenden Aufrufs der Bestätigung zu gewährleisten, wird die Wartezeit auf Twan = Twrn + 2 Trw gesetzt. Jede gerufene Station setzt auch einen Slot-Warte-Timer Tswt, der ihren Antwortzeitpunkt auslöst. Die gerufenen Stationen tunen wie erforderlich während des unmittelbar nach dem Ende des Aufrufs liegenden Slots, genannt Slot 0. Wenn der Slot-Warte-Timer einer Station abläuft, sendet sie ihre Antwort und wartet weiter auf das Ablauf des WRTTs. Sollte dieser Timer vor dem Start einer Bestätigung von der aufrufenden Station ablaufen, bricht die gerufene Station den Verbindungsaufbau ab und kehrt in ihren vorherigen Zustand zurück.

**Akzeptanzkriterien:**
- `AC-LINK-026-1` — Das Handshake-Protokoll kann nicht für Mehr-Wege-Aufrufe verwendet werden.
- `AC-LINK-026-2` — Ein TDMA-Schema wird verwendet.
- `AC-LINK-026-3` — Jede Station sendet ihre Antwort in einem Zeitfenster.
- `AC-LINK-026-4` — Die WRTT wird gesetzt.
- `AC-LINK-026-5` — Die WRTTs der gerufenen Stationen werden begrenzt.
- `AC-LINK-026-6` — Die Wartezeit wird auf Twan = Twrn + 2 Trw gesetzt.
- `AC-LINK-026-7` — Jede Station setzt einen Slot-Warte-Timer.
- `AC-LINK-026-8` — Die Stationen tunen während des Slots 0.
- `AC-LINK-026-9` — Bei Ablauf des Slot-Warte-Timers wird die Antwort gesendet.
- `AC-LINK-026-10` — Bei Ablauf des WRTT-Timers wird der Verbindungsaufbau abgebrochen.

---

### 9.25 One-to-Many Calling — A.5.5.4.1.1

### REQ-LINK-027 — Slotted Response Frames
**Spec-Referenz:** A.5.5.4.1.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Slotted Response Frames sind identisch mit den Antworten im Einzelruf-Protokoll (siehe Abb. A-32), einschließlich eines führenden Aufrufs, eines optionalen Nachrichtenabschnitts und einer Frame-Schlussfolge. Eine gerufene Station schließt ihre Antwort mit TIS ab, um sie zu akzeptieren, oder mit TWAS, um sie abzulehnen. Wenn die aufrufende und die gerufene Station eine einwortige Adresse verwenden (wie gezeigt), sind die Slots jeweils 14 Tw, also etwa 1,8 Sekunden.

**Akzeptanzkriterien:**
- `AC-LINK-027-1` — Slotted Response Frames sind identisch mit Einzelruf-Antworten.
- `AC-LINK-027-2` — Die Frames enthalten führenden Aufruf, optionalen Nachrichtenabschnitt und Frame-Schlussfolge.
- `AC-LINK-027-3` — Die Antwort wird mit TIS oder TWAS abgeschlossen.
- `AC-LINK-027-4` — Bei einwortiger Adresse sind die Slots 14 Tw.

---

### 9.26 One-to-Many Calling — A.5.5.4.1.2

### REQ-LINK-028 — Slot Widths
**Spec-Referenz:** A.5.5.4.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sofern nicht anders angegeben, müssen alle Slots 14 Tw lang sein. Dies ermöglicht Antwortframes mit einwortigen Adressen, um die andere Seite der Welt zu durchqueren und gängige HF-Transceivern und Tunern zu verwenden. Wenn ein Slot erweitert wird, müssen alle folgenden Slots entsprechend verzögert werden. Wenn die aufrufende Stationsadresse länger als ein Wort ist, muss jeder Slot um zwei Trw (sechs Tw) pro zusätzlichen Adresswort erweitert werden. Wenn eine gerufene Stationsadresse länger als ein Wort ist, muss ihr Slot um ein Trw (drei Tw) pro zusätzlichen Adresswort erweitert werden. Slots werden um ein Trw (drei Tw) pro ALE-Wort erweitert, das im Nachrichtenabschnitt der Antworten enthalten ist (einschließlich LQA CMD).

**Akzeptanzkriterien:**
- `AC-LINK-028-1` — Alle Slots sind 14 Tw lang, sofern nicht anders angegeben.
- `AC-LINK-028-2` — Die Slots ermöglichen Durchquerung der Welt.
- `AC-LINK-028-3` — Erweiterte Slots werden entsprechend verzögert.
- `AC-LINK-028-4` — Die aufrufende Stationsadresse erweitert Slots.
- `AC-LINK-028-5` — Die gerufene Stationsadresse erweitert Slots.
- `AC-LINK-028-6` — Nachrichtenabschnitt erweitert Slots.

---

### 9.27 One-to-Many Calling — A.5.5.4.1.3

### REQ-LINK-029 — Slot Wait Time Formula
**Spec-Referenz:** A.5.5.4.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die allgemeine Formel zur Bestimmung der korrekten Zeit für slotted Responses in nicht-minimalen oder nicht-uniformen Fällen lautet wie folgt für eine ausgewählte Slotnummer SN: Tswt(SN) = SN x [5 Tw + 2 Ta (caller) + (optional message) Tm] + Ta (caller) + m = SN-1 Σ Ta (m) (called) m=1 Wobei Ta (caller) die Adresslänge (ein ganzzahliges Vielfaches von Trw) der aufrufenden Station ist, (optional message)Tm ist ein optionaler Nachrichtenabschnitt (gleiche Größe für alle Slots), vorhanden, wenn und nur wenn dies in der Anfrage angefordert wird. Ta(m) (called) ist die Adresslänge der Station, die in Slot m antwortet. (Beachten Sie, dass die Länge des Slots 0 durch die Adresslänge der aufrufenden Station bestimmt wird.) Die Formel für den Wartezeit-Timer der aufrufenden Station (Twrn) lautet: Twrn = Tswt (NS + 1) wobei NS die Gesamtzahl der Slots ist; eine wird hinzugefügt, um Slot 0 einzubeziehen. Die Formel für den Bestätigungs-Timer der gerufenen Station ist: Twan = Twrn + 2 Trw

**Akzeptanzkriterien:**
- `AC-LINK-029-1` — Die Formel zur Bestimmung der Zeit für slotted Responses ist definiert.
- `AC-LINK-029-2` — Die Slotnummer SN wird berücksichtigt.
- `AC-LINK-029-3` — Die Formel berücksichtigt die Adresslänge der aufrufenden Station.
- `AC-LINK-029-4` — Die Formel berücksichtigt den optionalen Nachrichtenabschnitt.
- `AC-LINK-029-5` — Die Formel berücksichtigt die Adresslänge der gerufenen Station.
- `AC-LINK-029-6` — Die Formel für Twrn ist definiert.
- `AC-LINK-029-7` — Die Formel für Twan ist definiert.

---

### 9.28 One-to-Many Calling — A.5.5.4.1.4

### REQ-LINK-030 — Slotted Response Example
**Spec-Referenz:** A.5.5.4.1.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das slotted Response-Beispiel ist in Abb. A-33 gezeigt.

**Akzeptanzkriterien:**
- `AC-LINK-030-1` — Das Beispiel ist in Abb. A-33 gezeigt.
- `AC-LINK-030-2` — Die Beispieldarstellung ist korrekt.

---

### 9.29 One-to-Many Calling — A.5.5.4.2

### REQ-LINK-031 — Star Net Calling Protocol
**Spec-Referenz:** A.5.5.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Net-Adresse wird einem Satz von Net-Mitglied-Stationen zugewiesen, wie in A.5.2.4.4 beschrieben. Die Slotnummer und die zu verwendende Adresse für jedes Net-Mitglied sind vorab bekannt und für alle Net-Mitglieder.

**Akzeptanzkriterien:**
- `AC-LINK-031-1` — Eine Net-Adresse wird einem Satz von Net-Mitglied-Stationen zugewiesen.
- `AC-LINK-031-2` — Die Slotnummer und Adresse sind vorab bekannt.
- `AC-LINK-031-3` — Die Informationen sind für alle Net-Mitglieder verfügbar.

---

### 9.30 One-to-Many Calling — A.5.5.4.2.1

### REQ-LINK-032 — Star Net Call
**Spec-Referenz:** A.5.5.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Star-Net-Aufruf ist identisch mit einem Einzelruf, außer dass die gerufene Stationsadresse eine Net-Adresse ist, wie in Abb. A-34 gezeigt. Die aufrufende Stationsadresse muss eine individuelle Stationsadresse (nicht eine Net- oder andere Kollektivadresse) sein.

**Akzeptanzkriterien:**
- `AC-LINK-032-1` — Ein Star-Net-Aufruf ist identisch mit einem Einzelruf.
- `AC-LINK-032-2` — Die gerufene Stationsadresse ist eine Net-Adresse.
- `AC-LINK-032-3` — Die aufrufende Stationsadresse ist individuell.

---

### 9.31 One-to-Many Calling — A.5.5.4.2.2

### REQ-LINK-033 — Star Net Response
**Spec-Referenz:** A.5.5.4.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine ALE-Station einen Aufruf empfängt, der an eine Net-Adresse gerichtet ist, die in ihrem Speicher für eigene Adressen enthalten ist (siehe A.4.3.2), verarbeitet sie den Aufruf mit denselben Prüfungen und Zeitüberschreitungen wie bei einem Einzelruf (siehe A.5.5.3.2). Wenn der Aufruf akzeptabel ist, antwortet sie gemäß A.5.5.4.1 unter Verwendung ihrer zugewiesenen Net-Mitglied-Adresse und Slotnummer für die Net-Adresse, die gerufen wurde.

**Akzeptanzkriterien:**
- `AC-LINK-033-1` — Die Station empfängt einen Aufruf an eine Net-Adresse.
- `AC-LINK-033-2` — Die Station verarbeitet den Aufruf mit Einzelruf-Prüfungen.
- `AC-LINK-033-3` — Die Station antwortet gemäß A.5.5.4.1.
- `AC-LINK-033-4` — Die Antwort verwendet die zugewiesene Net-Mitglied-Adresse und Slotnummer.

---

### 9.32 One-to-Many Calling — A.5.5.4.2.3

### REQ-LINK-034 — Star Net Acknowledgment
**Spec-Referenz:** A.5.5.4.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Star-Net-Bestätigungsprotokoll ist identisch mit einem Einzelruf-Bestätigungsprotokoll, außer dass die gerufene Stationsadresse eine Net-Adresse ist. Eine ALE-Station, die auf einen Net-Aufruf geantwortet hat, verarbeitet die Bestätigung von der aufrufenden Station gemäß A.5.5.3.4, außer dass der Wartezeit-Timer-Wert Twan von A.5.5.4.1.3 anstelle von Twr verwendet wird. Ein TWAS-Bestätigungsprotokoll von der aufrufenden Station kehrt die gerufene ALE-Station in ihren vorherigen Zustand zurück. Wenn ein TIS-Bestätigungsprotokoll von der aufrufenden Station empfangen wird, wechselt die gerufene ALE-Station in den verknüpften Zustand mit der aufrufenden Station (SAM in diesem Beispiel), benachrichtigt den Operator (und den Netzwerk-Controller, falls vorhanden), schaltet den Lautsprecher ein und setzt einen Wartezeit-Timer Twa.

**Akzeptanzkriterien:**
- `AC-LINK-034-1` — Das Bestätigungsprotokoll ist identisch mit Einzelruf-Bestätigung.
- `AC-LINK-034-2` — Die gerufene Stationsadresse ist eine Net-Adresse.
- `AC-LINK-034-3` — Der Wartezeit-Timer Twan wird verwendet.
- `AC-LINK-034-4` — TWAS kehrt die Station in ihren vorherigen Zustand zurück.
- `AC-LINK-034-5` — TIS wechselt in den verknüpften Zustand.
- `AC-LINK-034-6` — Der Operator wird benachrichtigt.
- `AC-LINK-034-7` — Der Lautsprecher wird eingeschaltet.
- `AC-LINK-034-8` — Der Timer Twa wird gesetzt.

---

### 9.33 One-to-Many Calling — A.5.5.4.3

### REQ-LINK-035 — Star Group Calling Protocol
**Spec-Referenz:** A.5.5.4.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gruppenrufprotokoll erweitert die Leistung von Mehr-Wege-Rufen auf ad-hoc-Sammlungen von Stationen, die nicht im Voraus als Net programmiert wurden. Es muss nichts über die Stationen außer ihren individuellen Adressen und gescannten Frequenzen bekannt sein. Da eine Gruppe nicht im Voraus eingerichtet ist, müssen Stationen in der Lage sein, Gruppenmitgliedschaft und Slot-Parameter dynamisch abzuleiten. Die Gruppenmitgliedschaft ist wie folgt begrenzt: Die Gesamtlänge der individuellen Adressen der Gruppenmitglieder darf 12 ALE-Wörter nicht überschreiten. Die Menge der einzigartigen ersten Adresswörter unter den Gruppenmitgliedern darf fünf Wörter nicht überschreiten.

**Akzeptanzkriterien:**
- `AC-LINK-035-1` — Das Gruppenrufprotokoll erweitert Mehr-Wege-Rufen.
- `AC-LINK-035-2` — Die Gruppe ist ad-hoc.
- `AC-LINK-035-3` — Es muss nichts über die Stationen außer ihren Adressen und Frequenzen bekannt sein.
- `AC-LINK-035-4` — Gruppenmitgliedschaft ist begrenzt.
- `AC-LINK-035-5` — Die Gesamtlänge darf 12 Wörter nicht überschreiten.
- `AC-LINK-035-6` — Die Menge der einzigartigen ersten Adresswörter darf fünf Wörter nicht überschreiten.

---

### 9.34 One-to-Many Calling — A.5.5.4.3.1

### REQ-LINK-036 — Star Group Scanning Call
**Spec-Referenz:** A.5.5.4.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine Gruppenadresse wird durch Kombination der individuellen Adressen der Stationen, die die Gruppe bilden, erzeugt. Während eines Scanning-Aufrufs werden nur die ersten Wörter der Adressen gesendet, genau wie bei Einzel- oder Net-Aufrufen. Die Menge der einzigartigen ersten Adresswörter für die Gruppenmitglieder muss in Rotation gesendet werden, bis das Ende von Tsc. Diese Adresswörter wechseln zwischen THRU- und REP-Preambles (siehe Abb. A-35 für ein Beispiel mit BOB, EDGAR und SAM). THRU BOB REP EDG THRU SAM REP BOB THRU EDG REP SAM TO BOB REP EDG DATA AR@ ... THRU BOB REP EDG THRU SAM (Ende von Tsc; Tlc beginnt auf der nächsten Zeile) TO SAM DATA UEL TO BOB REP EDG DATA AR@ TO SAM DATA UEL

**Akzeptanzkriterien:**
- `AC-LINK-036-1` — Eine Gruppenadresse wird durch Kombination individueller Adressen erzeugt.
- `AC-LINK-036-2` — Beim Scanning-Aufruf werden nur die ersten Wörter gesendet.
- `AC-LINK-036-3` — Die ersten Adresswörter werden in Rotation gesendet.
- `AC-LINK-036-4` — Die Adresswörter wechseln zwischen THRU und REP.
- `AC-LINK-036-5` — Die Adresswörter sind in Abb. A-35 gezeigt.

---

### 9.35 One-to-Many Calling — A.5.5.4.3.2

### REQ-LINK-037 — Star Group Leading Call
**Spec-Referenz:** A.5.5.4.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Während Tlc sendet die Station die vollständigen Adressen der potenziellen Gruppenmitglieder, unter Verwendung von TO-Preambles wie üblich. Bis zu 12 Adresswörter insgesamt sind für die vollständigen Adressen der Gruppenmitglieder erlaubt, so dass Tlc in einem Gruppenruf bis zu 24 Trw dauern kann. Beachten Sie in Abb. A-34, dass wenn ein TO-Wort auf ein anderes TO-Wort folgt, ein REP-Preamble verwendet werden muss, aber wenn ein TO-Wort auf ein REP-Wort folgt, bleibt es ein TO.

**Akzeptanzkriterien:**
- `AC-LINK-037-1` — Die vollständigen Adressen werden gesendet.
- `AC-LINK-037-2` — Die Adressen werden mit TO-Preambles gesendet.
- `AC-LINK-037-3` — Bis zu 12 Adresswörter sind erlaubt.
- `AC-LINK-037-4` — Tlc dauert bis zu 24 Trw.
- `AC-LINK-037-5` — TO-Wörter werden korrekt behandelt.

---

### 9.36 One-to-Many Calling — A.5.5.4.3.3

### REQ-LINK-038 — Star Group Call Conclusion
**Spec-Referenz:** A.5.5.4.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der optionale Nachrichtenabschnitt und die Schlussfolge eines Star-Group-Aufrufs müssen gemäß A.5.2.5.3 erfolgen.

**Akzeptanzkriterien:**
- `AC-LINK-038-1` — Der Nachrichtenabschnitt ist optional.
- `AC-LINK-038-2` — Die Schlussfolge folgt A.5.2.5.3.
- `AC-LINK-038-3` — Die Vorgaben sind korrekt.

---

### 9.37 One-to-Many Calling — A.5.5.4.3.4

### REQ-LINK-039 — Receiving a Star Group Call
**Spec-Referenz:** A.5.5.4.3.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Slots werden für Gruppenruf-Antworten abgeleitet, indem man den Aufbau der individuellen Adressen in der Aufruffolge beachtet. a. Wenn eine ALE-Station auf einem Kanal landet, der einen Gruppenruf sendet, liest sie entweder ein THRU- oder ein REP-Preamble. Wenn das Adresswort in diesem ersten empfangenen Wort mit dem ersten Wort einer ihrer individuellen Adressen übereinstimmt, bleibt die Station, um den führenden Aufruf zu lesen. Andernfalls bleibt die Station, um erste Adresswörter zu lesen, bis sie: • eine Übereinstimmung mit dem ersten Wort einer ihrer eigenen Adressen findet, oder • eine Wiederholung eines Wortes, das sie bereits gesehen hat, oder • fünf einzigartige Wörter. (In den letzten beiden Fällen ist die Station nicht angesprochen und kehrt in den verfügbaren oder verknüpften Zustand zurück, wie angemessen.) b. Wenn Tlc beginnt, prüft eine ALE-Station, die angesprochen wurde, ob sie ihre eigene Adresse in der Scanning-Call-Folge findet. Wenn ja, wird ein Slot-Zähler auf 1 gesetzt und inkrementiert für jedes Adresswort, das darauf folgt. Wenn diese Adresse erneut gefunden wird (wie es sein sollte, da die Adressliste in Tlc wiederholt wird), wird der Zähler zurückgesetzt auf 1 und inkrementiert für jedes folgende Adresswort, wie zuvor. Die Anzahl der Wörter in jedem folgenden Adresswort wird auch für die Berechnung von Tswt berücksichtigt. c. Der Nachrichtenabschnitt (falls vorhanden) und die Schlussfolge werden gemäß A.5.5.3.2 verarbeitet. In Fällen, in denen eine ansprechende ALE-Station zu spät auftritt, um die Größe der gerufenen Gruppe zu identifizieren, wird sie nicht in der Lage sein, den korrekten Twan zu berechnen. In diesem Fall wird sie einen Standardwert für Twan verwenden, der gleich der längsten möglichen Gruppenruf mit zwölf einwortigen Adressen ist. Sie wird jedoch bereits den korrekten Slot-Nummer berechnet haben, da sie ihre eigene Adresse erhalten hat, als sie die Adressen, die darauf folgten, gelesen hat.

**Akzeptanzkriterien:**
- `AC-LINK-039-1` — Slots werden abgeleitet.
- `AC-LINK-039-2` — Die Station liest THRU- oder REP-Preambles.
- `AC-LINK-039-3` — Die Station prüft auf Übereinstimmung.
- `AC-LINK-039-4` — Die Station kehrt in den verfügbaren Zustand zurück.
- `AC-LINK-039-5` — Tlc wird geprüft.
- `AC-LINK-039-6` — Der Slot-Zähler wird gesetzt und inkrementiert.
- `AC-LINK-039-7` — Die Nachrichtenabschnitt und Schlussfolge werden verarbeitet.
- `AC-LINK-039-8` — Bei spätem Eintreffen wird ein Standardwert verwendet.

---

### 9.38 One-to-Many Calling — A.5.5.4.3.5

### REQ-LINK-040 — Star Group Slotted Responses
**Spec-Referenz:** A.5.5.4.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Slotted Responses werden gemäß A.5.5.4.1 gesendet und überprüft, unter Verwendung der abgeleiteten Slot-Nummern und der eigenen Adresse, die im führenden Aufruf enthalten ist.

**Akzeptanzkriterien:**
- `AC-LINK-040-1` — Slotted Responses werden gemäß A.5.5.4.1 gesendet.
- `AC-LINK-040-2` — Slotted Responses werden überprüft.
- `AC-LINK-040-3` — Die abgeleiteten Slot-Nummern werden verwendet.
- `AC-LINK-040-4` — Die eigene Adresse wird verwendet.

---

### 9.39 One-to-Many Calling — A.5.5.4.3.6

### REQ-LINK-041 — Star Group Acknowledgment
**Spec-Referenz:** A.5.5.4.3.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Bestätigung in einem Gruppenruf-Handshake muss an irgendeine Subset der ursprünglich gerufenen Mitglieder gerichtet sein und ist normalerweise auf diejenigen begrenzt, deren Antworten von der aufrufenden Station gelesen wurden. Der führende Aufruf der Bestätigung enthält die vollständigen Adressen der angesprochenen Stationen, gesendet zweimal, unter Verwendung der gleichen Syntax wie im Aufruf (A.5.5.4.3.2). Eine ALE-Station, die auf einen Gruppenruf geantwortet hat, wartet auf eine Bestätigung und verarbeitet eine eingehende Bestätigung gemäß A.5.5.3.4, mit folgenden Ausnahmen: • Der Wartezeit-Timer-Wert wird vom Twan-Wert aus A.5.5.4.1.3 anstelle von Twr verwendet. • Selbstadressen suchen durch die gesamte führende Aufruf-Gruppenadresse. Eine ALE-Station, die geantwortet hat, aber nicht in der Bestätigung genannt wurde, kehrt in ihren vorherigen Zustand zurück. Eine ALE-Station, die in der Bestätigung genannt wurde, verfährt wie folgt: • Ein TWAS-Bestätigungsprotokoll von der aufrufenden Station kehrt die gerufene ALE-Station in ihren vorherigen Zustand zurück. • Wenn ein TIS-Bestätigungsprotokoll von der aufrufenden Station empfangen wird, wechselt die gerufene ALE-Station in den verknüpften Zustand mit der aufrufenden Station (SAM in diesem Beispiel), benachrichtigt den Operator (und den Netzwerk-Controller, falls vorhanden), schaltet den Lautsprecher ein und setzt einen Wartezeit-Timer Twa.

**Akzeptanzkriterien:**
- `AC-LINK-041-1` — Die Bestätigung ist an Subset gerichtet.
- `AC-LINK-041-2` — Die führende Aufruf-Adresse wird zweimal gesendet.
- `AC-LINK-041-3` — Die Bestätigung wird verarbeitet gemäß A.5.5.3.4.
- `AC-LINK-041-4` — Der Twan-Wert wird verwendet.
- `AC-LINK-041-5` — Selbstadressen werden durchsucht.
- `AC-LINK-041-6` — TWAS kehrt die Station in ihren vorherigen Zustand zurück.
- `AC-LINK-041-7` — TIS wechselt in den verknüpften Zustand.
- `AC-LINK-041-8` — Der Operator wird benachrichtigt.
- `AC-LINK-041-9` — Der Lautsprecher wird eingeschaltet.
- `AC-LINK-041-10` — Der Timer Twa wird gesetzt.

---

### 9.40 One-to-Many Calling — A.5.5.4.3.7

### REQ-LINK-042 — Star Group Call Example
**Spec-Referenz:** A.5.5.4.3.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** In dem Beispielgruppenruf in Abb. A-35 sendet SAMUEL in Slot 1, mit Tswt = 14 Tw (das einwortige Adresse JOE verursacht Slot 0 auf 14 Tw). EDGAR sendet in Slot 2, mit Tswt = 14 + 17 Tw = 31 Tw (Slot 1 ist 17 Tw wegen SAMUELS zweifachem Adresswort). BOB sendet in Slot 3, mit Tswt = 48 Tw. JOE sendet eine Bestätigung nach 62 Tw.

**Akzeptanzkriterien:**
- `AC-LINK-042-1` — Die Beispielgruppe ist in Abb. A-35 gezeigt.
- `AC-LINK-042-2` — Die Slot-Zeiten sind korrekt.
- `AC-LINK-042-3` — Die Bestätigung wird korrekt gesendet.

---

### 9.41 One-to-Many Calling — A.5.5.4.3.8

### REQ-LINK-043 — Multiple Self Addresses in Group Call
**Spec-Referenz:** A.5.5.4.3.8
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine Station mehrfach in einem Gruppenruf angesprochen wird, selbst mit verschiedenen Adressen, soll sie mindestens eine Adresse antworten. Hinweis: Die Tatsache, dass die ansprechende Station mehrere Adressen hat, ist dem Aufrufer nicht bekannt. In einigen Fällen wäre es verwirrend oder unangemessen, auf eine aber nicht auf eine andere Adresse zu antworten. Redundante Aufrufadressenkonflikte können nach erfolgreicher Verknüpfung aufgelöst werden, falls es ein Problem gibt.

**Akzeptanzkriterien:**
- `AC-LINK-043-1` — Eine Station antwortet auf mehrere Adressen.
- `AC-LINK-043-2` — Die Tatsache, dass die Station mehrere Adressen hat, ist dem Aufrufer nicht bekannt.
- `AC-LINK-043-3` — Konflikte können nach erfolgreicher Verknüpfung aufgelöst werden.

---

### 9.42 One-to-Many Calling — A.5.5.4.4

### REQ-LINK-044 — AllCall Protocol
**Spec-Referenz:** A.5.5.4.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AllCall fordert alle anhörenden Stationen auf, zu hören, aber nicht zu antworten. Die AllCall-Spezialadressstruktur(en) (siehe A.5.2.4.7) müssen die einzigen Mitglieder des Scanning-Aufrufs und des führenden Aufrufs sein und dürfen nicht in irgendeinem anderen Adressfeld oder Teil des Handshakes verwendet werden. Die globale AllCall-Adresse darf nur in TO-Wörtern verwendet werden. Selektive AllCalls mit mehr als einer selektiven AllCall-Adresse verwenden jedoch Gruppenadressierung, unter Verwendung von THRU während des Scanning-Aufrufs und TO während des führenden Aufrufs. Ein AllCall betrifft eine ALE-Station, wenn es sich um eine globale AllCall handelt oder wenn ein selektiver AllCall eine Zeichen enthält, das mit dem letzten Zeichen einer von dieser Station zugewiesenen Adresse übereinstimmt. Bei Empfang eines relevanten AllCall sendet eine ALE-Station eine vordefinierte begrenzte Zeit, Tcc max. • Wenn keine Nachrichtenabschnitt oder Frame-Schlussfolge innerhalb von Tcc max empfangen wird, kehrt der Controller automatisch zum Scanning zurück. • Wenn ein Quick-ID (eine Adresse, die mit einem FROM-Wort beginnt, direkt nach dem Aufrufzyklus) empfangen wird, wird die Pause für den Nachrichtenabschnitt um maximal fünf Wörter (5 Trw) erweitert, und wenn kein CMD empfangen wird, kehrt der Controller zum Scanning zurück. • Wenn eine Nachricht empfangen wird (indiziert durch Empfang eines CMD), wartet der Controller eine vordefinierte begrenzte Zeit, Tm max, um die Nachricht zu lesen. Wenn die Frame-Schlussfolge nicht innerhalb von Tm max empfangen wird, kehrt der Controller automatisch zum Scanning zurück. Wenn eine Schlussfolge empfangen wird (indiziert durch Empfang eines TIS oder TWAS), wartet der Controller (für eine vordefinierte begrenzte Zeit, Tx max) um die Adresse des Aufrufers zu lesen. Wenn das Ende des Signals nicht innerhalb von Tx max empfangen wird, kehrt der Controller automatisch zum Scanning zurück. Wenn ein relevanter AllCall-Frame erfolgreich empfangen und mit TIS abgeschlossen wird, wechselt der Controller in den verknüpften Zustand, benachrichtigt den Operator, schaltet den Lautsprecher ein und setzt einen Wartezeit-Timer. Wenn ein AllCall erfolgreich mit TWAS abgeschlossen wird, kehrt die gerufene Station automatisch zum Scanning zurück und antwortet nicht (es sei denn, dies wird vom Operator oder Controller direkt angeordnet). Wenn eine Station, die einen AllCall empfängt, einen Handshake initiieren möchte, kann der Operator einen Handshake innerhalb der Pause nach einer TIS-Schlussfolge initiieren. Beachten Sie, dass in allen Handshakes (der ursprüngliche AllCall bildet keinen Handshake) die AllCall-Adresse nicht verwendet wird. Um mögliche negative Auswirkungen durch Übernutzung oder Missbrauch von AllCalls zu minimieren, müssen Controller die Fähigkeit haben, AllCalls zu ignorieren. Normalerweise sollte AllCall-Verarbeitung aktiviert sein.

**Akzeptanzkriterien:**
- `AC-LINK-044-1` — Ein AllCall fordert Stationen auf, zu hören.
- `AC-LINK-044-2` — Ein AllCall fordert keine Antworten an.
- `AC-LINK-044-3` — Die AllCall-Adresse wird nur in TO-Wörtern verwendet.
- `AC-LINK-044-4` — Selektive AllCalls verwenden Gruppenadressierung.
- `AC-LINK-044-5` — Die Station wird identifiziert.
- `AC-LINK-044-6` — Die Pause wird gestartet.
- `AC-LINK-044-7` — Bei Quick-ID wird die Pause erweitert.
- `AC-LINK-044-8` — Bei Nachricht wird eine Zeit für das Lesen verwendet.
- `AC-LINK-044-9` — Bei Schlussfolge wird eine Zeit für das Lesen verwendet.
- `AC-LINK-044-10` — Bei TIS wird der Zustand gewechselt.
- `AC-LINK-044-11` — Bei TWAS kehrt die Station zum Scanning zurück.
- `AC-LINK-044-12` — Der Operator kann einen Handshake initiieren.
- `AC-LINK-044-13` — AllCall-Adresse wird nicht verwendet.
- `AC-LINK-044-14` — Controller können AllCalls ignorieren.
- `AC-LINK-044-15` — AllCall-Verarbeitung sollte aktiviert sein.

---

### 9.43 One-to-Many Calling — A.5.5.4.5

### REQ-LINK-045 — AnyCall Protocol
**Spec-Referenz:** A.5.5.4.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AnyCall ist ähnlich wie ein AllCall, aber es fordert Antworten an. Die Verwendung der AnyCall-Spezialadressstruktur(en) ist identisch mit der für die AllCall-Spezialadressstruktur(en). Bei Empfang eines relevanten AnyCall prüft eine ALE-Station den Aufruf gleich wie bei AllCalls, inklusive der Tcc max, Tm max und Tx max Grenzen. Wenn der AnyCall erfolgreich empfangen wurde und mit TIS abgeschlossen wird, wechselt der Controller in den Link-Zustand und generiert automatisch eine slotted Antwort gemäß A.5.5.4.1 und den folgenden speziellen Verfahren: • Da keine vorprogrammierten oder abgeleiteten Slot-Daten verfügbar sind, wählt der Controller zufällig eine Slot-Nummer, 1 bis 16. • Jeder Slot soll 20 Tw (2613,33...ms) breit sein, es sei denn, der aufrufende Station fordert LQA-Antworten an, in diesem Fall werden die Slots um 3 Tw auf 23 Tw erweitert, um den CMD LQA-Nachrichtenabschnitt zu unterstützen. • Der Controller berechnet Werte für Tswt und Twan unter Verwendung dieser Slot-Breite und seiner zufälligen Slot-Nummer. • Slot 0 wird für das Tunen verwendet, wie bei slotted Response-Protokollen üblich. • Nach Ablauf des Tswt-Zeitfensters sendet die Station eine Standard-Star-Net-Antwort, bestehend aus TO (mit der Adresse des Aufrufers) und TIS (mit der Adresse des Antworters), mit dem LQA CMD, falls angefordert. Antwortende verwenden eine eigene Adresse, die nicht länger als fünf Wörter minus zweimal die Länge der Aufrufadresse ist. (Zum Beispiel, wenn die Aufrufadresse zwei Wörter hat, verwendet der Antwortende eine einwortige Adresse.) Die AnyCall-Spezialadresse wird nicht in der Bestätigung gesendet. In diesem Protokoll werden Kollisionen erwartet und toleriert. Die Station, die den AnyCall sendet, versucht, die beste Antwort in jedem Slot zu lesen. Nach Empfang der slotted Antworten sendet die aufrufende Station eine ACK an eine Subset der Stationen, deren Antworten gelesen wurden, unter Verwendung einer individuellen oder Gruppenadresse. Die AnyCall-Spezialadresse wird nicht in der Bestätigung gesendet. Der aufrufende Station wählt die Schlussfolge der ACK, um den Link für zusätzliche Interoperation und Verkehr mit den Antwortenden aufrechtzuerhalten (TIS), oder sie kehrt alle zurück in das Scanning (TWAS), wie angemessen für den ursprünglichen Zweck des Aufrufers. Eine ALE-Station, die auf einen AnyCall geantwortet hat, wartet auf die Bestätigung und verarbeitet eine eingehende Bestätigung gemäß A.5.5.4.3.6. Um mögliche negative Auswirkungen durch Übernutzung oder Missbrauch von AnyCalls zu minimieren, müssen Controller die Fähigkeit haben, AnyCalls zu ignorieren. Normalerweise sollte AnyCall-Verarbeitung aktiviert sein.

**Akzeptanzkriterien:**
- `AC-LINK-045-1` — Ein AnyCall fordert Antworten an.
- `AC-LINK-045-2` — Die Verwendung der AnyCall-Adresse ist identisch mit AllCall.
- `AC-LINK-045-3` — Die Station prüft den Aufruf wie bei AllCall.
- `AC-LINK-045-4` — Bei TIS wird ein Link-Zustand gewechselt.
- `AC-LINK-045-5` — Die Station generiert eine slotted Antwort.
- `AC-LINK-045-6` — Die Station wählt zufällig eine Slot-Nummer.
- `AC-LINK-045-7` — Die Slot-Breite wird berechnet.
- `AC-LINK-045-8` — Slot 0 wird für das Tunen verwendet.
- `AC-LINK-045-9` — Die Antwort wird gesendet.
- `AC-LINK-045-10` — Kollisionen werden erwartet und toleriert.
- `AC-LINK-045-11` — Die aufrufende Station liest die beste Antwort.
- `AC-LINK-045-12` — Die aufrufende Station sendet eine ACK.
- `AC-LINK-045-13` — Die AnyCall-Adresse wird nicht verwendet.
- `AC-LINK-045-14` — Controller können AnyCalls ignorieren.
- `AC-LINK-045-15` — AnyCall-Verarbeitung sollte aktiviert sein.

---

### 9.44 One-to-Many Calling — A.5.5.4.6

### REQ-LINK-046 — Wildcard Calling Protocol
**Spec-Referenz:** A.5.5.4.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wildcard-Adressen müssen die einzigen Mitglieder eines Aufrufs in einem Aufruf sein und dürfen nicht in irgendeinem anderen Adressfeld oder Teil des Handshakes verwendet werden. Der Umfang (Anzahl der Fälle) der verwendeten Wildcards sollte auf die essentiellen Bedürfnisse des/der Benutzers/der Benutzer reduziert werden. Aufrufe an Wildcard-Adressen, die mit TWAS abgeschlossen werden, werden identisch mit dem AllCall-Protokoll verarbeitet. Antworten auf Wildcard-Aufrufe, die mit TIS abgeschlossen werden, werden in pseudozufällig ausgewählten Slots gesendet, gemäß dem AnyCall-Protokoll. Wie bei AllCall und AnyCall, kann der Controller programmierbar sein, um Wildcard-Aufrufe zu ignorieren, aber Wildcard-Aufruf-Verarbeitung sollte normalerweise aktiviert sein.

**Akzeptanzkriterien:**
- `AC-LINK-046-1` — Wildcard-Adressen sind die einzigen Mitglieder des Aufrufs.
- `AC-LINK-046-2` — Wildcard-Adressen werden nicht in anderen Adressfeldern verwendet.
- `AC-LINK-046-3` — Der Umfang der Wildcards ist begrenzt.
- `AC-LINK-046-4` — Aufrufe mit TWAS werden wie AllCall verarbeitet.
- `AC-LINK-046-5` — Antworten mit TIS werden in pseudozufälligen Slots gesendet.
- `AC-LINK-046-6` — Die Verarbeitung kann ignoriert werden.
- `AC-LINK-046-7` — Wildcard-Aufruf-Verarbeitung sollte aktiviert sein.

---

### 9.45 A.5.5.4.7 — Offene Punkte und Annahmen

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-LINK-002 — Die Abbildung A-28 wurde nicht geliefert; die darin gezeigten Zustände können nicht verifiziert werden.
- OPEN-LINK-003 — Die Abbildung A-29 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-004 — Die Abbildung A-30 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-005 — Die Abbildung A-31 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-006 — Die Abbildung A-32 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-007 — Die Abbildung A-33 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-008 — Die Abbildung A-34 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.
- OPEN-LINK-009 — Die Abbildung A-35 wurde nicht geliefert; die darin gezeigte Darstellung kann nicht verifiziert werden.

---

## 11. Adressierung — A.5.2.4

> **Spec-Stellen sammeln:** A.5.2.4.1–4.12 (alle Adresstypen), Table A-IX, Table A-X, Table A-XI.

### EPIC-ADDR · Adressierung

#### US-ADDR-001

> Als ALE-Station will ich Adressen, Adresstypen und Sonderadressierungen im Basic-38-Schema verarbeiten, damit individuelle, Netz-, Gruppen-, Broadcast-, Wildcard-, Selbst-, Null- und In-Link-Rufe standardkonform unterstützt werden.

**Erfüllt durch:** REQ-ADDR-001 bis REQ-ADDR-016

### 10.1 Einführung — A.5.2.4.1

#### REQ-ADDR-001 — Digitale Adressstruktur und Speicherkapazität

**Spec-Referenz:** A.5.2.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine digitale Adressstruktur auf Basis des standardisierten 24-Bit-Worts und des Basic-38-Zeichensatzes verwenden. ALE-Stationen müssen die Fähigkeit besitzen, mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen einzeln oder gemeinsam zu verlinken oder zu vernetzen. Alle ALE-Stationen müssen mindestens 20 eigene Adressen mit jeweils bis zu 15 Zeichen in beliebiger Kombination aus Einzel- und Netzrufen speichern und verwenden können. Das Standardwerk unterscheidet drei grundlegende Adressierungsarten: Einzelstation, Mehrfachstation und Sondermodi. Bestimmte alphanumerische Adresskombinationen können eine besondere Bedeutung für Notfälle oder spezifische Funktionen haben; solche Kombinationen sollten sorgfältig kontrolliert oder eingeschränkt werden.

**Akzeptanzkriterien:**
- `AC-ADDR-001-1` — Das System verwendet eine digitale Adressstruktur auf Basis des 24-Bit-Worts und des Basic-38-Zeichensatzes.
- `AC-ADDR-001-2` — Eine Station kann mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen verlinken oder vernetzen.
- `AC-ADDR-001-3` — Das System kann mindestens 20 eigene Adressen verwalten.
- `AC-ADDR-001-4` — Jede dieser eigenen Adressen kann bis zu 15 Zeichen lang sein.
- `AC-ADDR-001-5` — Das System unterstützt Einzelstation-, Mehrfachstation- und Sondermodi der Adressierung.
- `AC-ADDR-001-6` — Adresskombinationen mit Sonderbedeutung werden kontrolliert oder eingeschränkt behandelt.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Basis-Wortlänge | 24 | Bit | A.5.2.4.1 |
| Mindestanzahl eigener Adressen pro Station | 20 | Adressen | A.5.2.4.1 |
| Maximale Länge je eigener Adresse | 15 | Zeichen | A.5.2.4.1 |
| Anzahl grundlegender Adressierungsarten | 3 | Arten | A.5.2.4.1 |

### 10.2 Basic 38 ASCII subset — A.5.2.4.2

#### REQ-ADDR-002 — Basic-38-Zeichensatz und gültige Basisadresse

**Spec-Referenz:** A.5.2.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Basic-38-ASCII-Zeichensatz muss alle Großbuchstaben A bis Z und alle Ziffern 0 bis 9 sowie die Sonderzeichen „@“ und „?“ enthalten. Der Basic-38-ASCII-Zeichensatz muss für alle grundlegenden Adressierungsfunktionen verwendet werden. Eine gültige Basisadresse muss ein Routing-Präambelwort aus A.5.2.3.2 sowie drei alphanumerische Zeichen aus dem Basic-38-ASCII-Zeichensatz in beliebiger Kombination enthalten. Die Sonderzeichen „@“ und „?“ müssen für spezielle Funktionen verwendet werden. Die Unterscheidung des Basic-38-ASCII-Zeichensatzes darf nicht darauf beschränkt sein, nur die drei höchstwertigen Bits zu prüfen.

**Akzeptanzkriterien:**
- `AC-ADDR-002-1` — Der Basic-38-ASCII-Zeichensatz umfasst A bis Z, 0 bis 9, „@“ und „?“.
- `AC-ADDR-002-2` — Der Basic-38-ASCII-Zeichensatz wird für alle grundlegenden Adressierungsfunktionen verwendet.
- `AC-ADDR-002-3` — Eine gültige Basisadresse enthält ein Routing-Präambelwort und drei Zeichen aus dem Basic-38-ASCII-Zeichensatz.
- `AC-ADDR-002-4` — Die Sonderzeichen „@“ und „?“ werden für spezielle Funktionen verwendet.
- `AC-ADDR-002-5` — Die Erkennung von Basic-38-Zeichen darf nicht allein auf den drei höchstwertigen Bits beruhen.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Anzahl Großbuchstaben | 26 | Zeichen | A.5.2.4.2 |
| Anzahl Ziffern | 10 | Zeichen | A.5.2.4.2 |
| Anzahl Sonderzeichen für den Basic-38-Satz | 2 | Zeichen | A.5.2.4.2 |
| Anzahl alphanumerischer Zeichen für Basisadressen | 3 | Zeichen | A.5.2.4.2 |

### 10.3 Stuffing — A.5.2.4.3

#### REQ-ADDR-003 — Stuffing nicht vollständiger Adressen

**Spec-Referenz:** A.5.2.4.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Adressen, deren Länge kein Vielfaches von drei Zeichen ist, müssen kompatibel in standardisierte Adressfelder aufgenommen werden, indem die freien nachlaufenden Positionen mit dem Utility-Zeichen „@“ aufgefüllt werden. Die Wörter „Stuff-1“ und „Stuff-2“ dürfen nur im letzten Wort einer Adresse verwendet werden und sollen daher nur im führenden Ruf des Calling Cycles erscheinen.

**Akzeptanzkriterien:**
- `AC-ADDR-003-1` — Nicht durch drei teilbare Adresslängen werden durch Auffüllen der nachlaufenden Positionen mit „@“ in standardisierte Adressfelder überführt.
- `AC-ADDR-003-2` — Stuff-1 und Stuff-2 werden nur im letzten Wort einer Adresse verwendet.
- `AC-ADDR-003-3` — Stuff-1 und Stuff-2 erscheinen nur im führenden Ruf des Calling Cycles.

### 10.4 Individual addresses — A.5.2.4.4

#### REQ-ADDR-004 — Einzeladresse als grundlegendes Adresselement

**Spec-Referenz:** A.5.2.4.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das fundamentale Adressenelement im ALE-System ist das einzelne Routing-Wort mit drei Zeichen, das die grundlegende individuelle Stationsadresse bildet. Eine Adresse, die einer einzelnen Station innerhalb des bekannten oder verwendeten Netzes zugeordnet ist, muss als individuelle Adresse bezeichnet werden. Besteht sie aus einem Wort, muss sie als Basisgröße bezeichnet werden; überschreitet sie ein Wort, muss sie als erweiterte Größe bezeichnet werden. Das grundlegende Adresswort kann für Intranet- und Slotted-Betrieb verwendet werden und kann für Internet- und allgemeinen Gebrauch zu mehreren Wörtern erweitert werden.

**Akzeptanzkriterien:**
- `AC-ADDR-004-1` — Eine individuelle Stationsadresse basiert auf genau einem Routing-Wort mit drei Zeichen.
- `AC-ADDR-004-2` — Eine einer einzelnen Station zugeordnete Adresse wird als individuelle Adresse bezeichnet.
- `AC-ADDR-004-3` — Eine einwortige individuelle Adresse wird als Basisgröße bezeichnet.
- `AC-ADDR-004-4` — Eine mehr als ein Wort umfassende individuelle Adresse wird als erweiterte Größe bezeichnet.
- `AC-ADDR-004-5` — Das grundlegende Adresswort kann für Intranet- und Slotted-Betrieb verwendet werden.
- `AC-ADDR-004-6` — Das grundlegende Adresswort kann für Internet- und allgemeinen Gebrauch erweitert werden.

### 10.5 Address patterns and special calls — Table A-IX

#### REQ-ADDR-005 — Standard-Adressmuster und Sonderruf-Varianten

**Spec-Referenz:** Table A-IX / A.5.2.4.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die im Standard aufgeführten Adressmuster definieren die zulässigen Standard-, Stuffing-, AllCall-, selective AllCall-, AnyCall-, selective AnyCall-, double selective AnyCall- und Null-Adressformen. Nicht dargestellte Muster sind reserviert und müssen bis zur Standardisierung als ungültig behandelt werden. Das Utility-Zeichen „@“ steht für das spezielle Utility-Zeichen, das Wildcard-Zeichen „?“ für einen Platzhalter. Die Bezeichner A, B, C und D stehen jeweils für ein alphanumerisches Zeichen des Basic-38-ASCII-Zeichensatzes, das weder „@“ noch „?“ ist.

**Akzeptanzkriterien:**
- `AC-ADDR-005-1` — Nicht dargestellte Muster werden als reserviert und ungültig behandelt, bis sie standardisiert sind.
- `AC-ADDR-005-2` — „@“ wird als Utility-Zeichen behandelt.
- `AC-ADDR-005-3` — „?“ wird als Wildcard-Zeichen behandelt.
- `AC-ADDR-005-4` — A, B, C und D stehen nur für Zeichen aus dem Basic-38-ASCII-Zeichensatz, die weder „@“ noch „?“ sind.

**Vom-Standard-vorgegebene Werte:**

| Muster | Bedeutung | Spec-Referenz |
|---|---|---|
| TO ABC | Standard-Dreizeichenadresse „ABC“ | Table A-IX |
| TO AB@ | Stuff-1; reduzierte Adressfelder | Table A-IX |
| TO A@@ | Stuff-2; reduzierte Adressfelder | Table A-IX |
| TO @?@ | Globales AllCall; alle stoppen und hören zu, keiner antwortet | Table A-IX |
| TO @A@ / THRU REP @B@ | Selective AllCall | Table A-IX |
| TO @@? | Globales AnyCall; alle stoppen und antworten | Table A-IX |
| TO @@A / REP @B@ | Selective AnyCall | Table A-IX |
| TO @AB / REP @CD | Double selective AnyCall | Table A-IX |
| TO @@@ | Null-Adresse | Table A-IX |

### 10.6 Basic size — A.5.2.4.4.1

#### REQ-ADDR-006 — Basisgröße der Einzeladresse

**Spec-Referenz:** A.5.2.4.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine Basisadresse muss aus einem Routing-Präambelwort und drei Adresszeichen aus dem Basic-38-ASCII-Zeichensatz bestehen. Eine solche dreistellige Einzelwortadresse ist die minimale Struktur. Das Standardwerk gibt für eine dreistellige individuelle Adresse eine Basic-38-Adresskapazität von 46.656 an. Alle ALE-Stationen müssen mit spezifischen Zeit- und Steuerinformationen für alle eigenen Adressen arbeiten, beispielsweise mit vorab festgelegten Verzögerungen für slotted Netzantworten. Alle ALE-Stationen müssen mindestens eine einwortige Adresse für den automatischen Einsatz in einwortigen Adressprotokollen erhalten; dies ist eine zwingende Benutzeranforderung. Die Verwendung längerer Adressen darf durch das Design nicht ausgeschlossen werden.

**Akzeptanzkriterien:**
- `AC-ADDR-006-1` — Eine Basisadresse besteht aus einem Routing-Präambelwort und drei Adresszeichen.
- `AC-ADDR-006-2` — Eine dreistellige Einzelwortadresse ist die minimale Struktur.
- `AC-ADDR-006-3` — Für die dreistellige individuelle Adresse ist eine Basic-38-Adresskapazität von 46.656 gegeben.
- `AC-ADDR-006-4` — Alle eigenen Adressen sind mit spezifischen Zeit- und Steuerinformationen verknüpft.
- `AC-ADDR-006-5` — Jede ALE-Station besitzt mindestens eine einwortige Adresse für einwortige Adressprotokolle.
- `AC-ADDR-006-6` — Längere Adressen werden durch das Design nicht ausgeschlossen.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Mindeststruktur einer Basisadresse | 1 Wort / 3 Zeichen | — | A.5.2.4.4.1 |
| Basic-38-Adresskapazität | 46.656 | Adressen | A.5.2.4.4.1 |
| Mindestanzahl einwortiger Adressen pro Station | 1 | Adresse | A.5.2.4.4.1 |

### 10.7 Extended size — A.5.2.4.4.2

#### REQ-ADDR-007 — Erweiterte Adresse bis fünf Wörter

**Spec-Referenz:** A.5.2.4.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Erweiterte Adressen dürfen länger als ein Wort sein und dürfen das systemweite Maximum von fünf Wörtern beziehungsweise 15 Zeichen nicht überschreiten. Die 15-Zeichen-Kapazität ermöglicht eine ISDN-Adressfähigkeit. Eine erweiterte ALE-Adresse muss aus einem initialen Basisadresswort, beispielsweise TO oder TIS, sowie aus zusätzlichen Wörtern bestehen, die die weiteren Zeichen in der Folge DATA, REP, DATA, REP aufnehmen. Alle Adresszeichen müssen alphanumerische Mitglieder des Basic-38-ASCII-Zeichensatzes sein.

**Akzeptanzkriterien:**
- `AC-ADDR-007-1` — Erweiterte Adressen können länger als ein Wort sein.
- `AC-ADDR-007-2` — Erweiterte Adressen überschreiten nicht fünf Wörter.
- `AC-ADDR-007-3` — Erweiterte Adressen überschreiten nicht 15 Zeichen.
- `AC-ADDR-007-4` — Eine erweiterte Adresse beginnt mit einem Basisadresswort wie TO oder TIS.
- `AC-ADDR-007-5` — Weitere Zeichen werden über zusätzliche Wörter in der Folge DATA, REP, DATA, REP aufgenommen.
- `AC-ADDR-007-6` — Alle Adresszeichen stammen aus dem Basic-38-ASCII-Zeichensatz.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Maximale Wortzahl einer erweiterten Adresse | 5 | Wörter | A.5.2.4.4.2 |
| Maximale Länge einer erweiterten Adresse | 15 | Zeichen | A.5.2.4.4.2 |

### 10.8 Net addresses — A.5.2.4.5

#### REQ-ADDR-008 — Netzruf mit gemeinsamer Netadresse

**Spec-Referenz:** A.5.2.4.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Netzruf muss dazu dienen, schnell und effizient Kontakt mit mehreren vorab vereinbarten Net-Stationen herzustellen, möglichst gleichzeitig, unter Verwendung einer einzigen Netadresse, die allen Netmitgliedern gemeinsam zugewiesen ist. Wenn eine Netadressfunktion erforderlich ist, muss die rufende ALE-Station eine Adressstruktur verwenden, die der individuellen Stationsadresse entspricht und bei Bedarf basic oder extended sein darf. Für jede Netadresse an einer Netmitgliedsstation muss ein Response-Slot-Identifikator vorhanden sein; ein Slot-Width-Modifikator muss zusätzlich vorhanden sein, wenn dies durch das spezifische Standardprotokoll vorgegeben ist. Zusätzliche Informationen über die zugewiesenen Response-Slots und deren Größe müssen verfügbar sein. Das Mischen von individuellen, Netz- und Gruppenadressen sowie -rufen ist eingeschränkt.

**Akzeptanzkriterien:**
- `AC-ADDR-008-1` — Ein Netzruf verwendet eine einzige gemeinsam zugewiesene Netadresse.
- `AC-ADDR-008-2` — Die rufende Station verwendet eine Adressstruktur, die der individuellen Stationsadresse entspricht.
- `AC-ADDR-008-3` — Eine Netmitgliedsstation besitzt je Netadresse einen Response-Slot-Identifikator.
- `AC-ADDR-008-4` — Ein Slot-Width-Modifikator ist vorhanden, wenn das Standardprotokoll dies vorgibt.
- `AC-ADDR-008-5` — Zusätzliche Informationen zu zugewiesenen Response-Slots und deren Größe sind verfügbar.
- `AC-ADDR-008-6` — Das Mischen von individuellen, Netz- und Gruppenadressen sowie -rufen ist eingeschränkt.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-09 — Die konkrete Form der Einschränkung für das Mischen von individuellen, Netz- und Gruppenadressen verweist auf A.5.5.3 und A.5.5.4; diese Inhalte wurden nicht geliefert.

### 10.9 Group addresses — A.5.2.4.6

#### REQ-ADDR-009 — Gruppenruf mit individuellen Zieladressen

**Spec-Referenz:** A.5.2.4.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Gruppenruf muss dazu dienen, schnell und effizient Kontakt mit mehreren nicht vorab vereinbarten Gruppen-Stationen herzustellen, möglichst gleichzeitig, unter Verwendung einer kompakten Kombination ihrer jeweils individuell zugewiesenen Adressen. Wenn eine Gruppenadressfunktion erforderlich ist, muss die rufende ALE-Station eine Folge der tatsächlichen individuellen Stationsadressen der gerufenen Stationen verwenden. Die Reihenfolge muss dem durch das spezifische Standardprotokoll vorgegebenen Verfahren entsprechen. Die Adresse einer Station darf in einer Gruppenruf-Folge nicht mehr als einmal erscheinen, außer wenn dies durch die Gruppenrufprotokolle ausdrücklich erlaubt ist.

**Akzeptanzkriterien:**
- `AC-ADDR-009-1` — Ein Gruppenruf verwendet mehrere nicht vorab vereinbarte Stationsadressen in einer kompakten Kombination.
- `AC-ADDR-009-2` — Die rufende Station verwendet die tatsächlichen individuellen Stationsadressen der gerufenen Stationen.
- `AC-ADDR-009-3` — Die Reihenfolge folgt dem spezifischen Standardprotokoll.
- `AC-ADDR-009-4` — Eine Stationsadresse erscheint in einer Gruppenruf-Folge nicht mehr als einmal, außer wenn das Gruppenrufprotokoll dies ausdrücklich erlaubt.

**Vom-Standard-vorgegebene Werte:**

| Wert | Bedeutung | Spec-Referenz |
|---|---|---|
| Gruppenfeature | in AQC-ALE nicht verfügbar | A.5.2.4.6 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-10 — Die konkrete Ausnahme für mehrfache Vorkommen einer Adresse verweist auf A.5.5.4; dieser Inhalt wurde nicht geliefert.

### 10.10 AllCall addresses — A.5.2.4.7

#### REQ-ADDR-010 — Globaler AllCall und selective AllCall

**Spec-Referenz:** A.5.2.4.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AllCall muss ein allgemeiner Broadcast sein, der keine Antworten anfordert und keine bestimmte Adresse bezeichnet. Er muss für Notfälle, Broadcast-Datenaustausch sowie Propagations- und Konnektivitäts-Tracking verwendet werden können. Die globale AllCall-Adresse muss „@?@“ sein. Ein selective AllCall muss in Struktur, Funktion und Protokoll dem AllCall entsprechen, mit der Ausnahme, dass er das letzte einzelne Zeichen der Adressen der gewünschten Empfängergruppe festlegt. Wenn mehr als eine Teilmenge gewünscht ist, darf die selective-AllCall-Adressierung auch mit einer alternativen Adresse über die THRU/REP-Folge erfolgen. Das Zeichen, das ersetzt wird, muss ein alphanumerisches Zeichen des Basic-38-Satzes sein; die ausgewählten Zeichen bestimmen, welche Stationen das Scannen beenden und zuhören.

**Akzeptanzkriterien:**
- `AC-ADDR-010-1` — Ein AllCall fordert keine Antworten an.
- `AC-ADDR-010-2` — Ein AllCall bezeichnet keine spezifische Adresse.
- `AC-ADDR-010-3` — Die globale AllCall-Adresse ist „@?@“.
- `AC-ADDR-010-4` — Ein selective AllCall legt das letzte einzelne Zeichen der Zieladressen fest.
- `AC-ADDR-010-5` — Bei mehr als einer Teilmenge darf die selective-AllCall-Adressierung eine THRU/REP-Folge verwenden.
- `AC-ADDR-010-6` — Die ausgewählten Zeichen bestimmen, welche Stationen das Scannen beenden und zuhören.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Globale AllCall-Adresse | @?@ | Adresse | A.5.2.4.7 |
| Selective-AllCall-Muster | @A@ | Adresse | A.5.2.4.7 |
| Alternative selective AllCall-Muster | THRU REP @B@ | Adresse | A.5.2.4.7 |
| Zielgruppengröße bei selective AllCall | 1/36 | Anteil | A.5.2.4.7 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-11 — Die referenzierte AllCall-Protokolldetailtiefe in A.5.5.4.4 wurde nicht geliefert.
- OPEN-12 — Für ACQ-ALE wird in der Quelle eine Part2-Adressvorgabe genannt; der zugehörige Kontext wurde nicht geliefert.

### 10.11 AnyCalls — A.5.2.4.8

#### REQ-ADDR-011 — Globaler AnyCall, selective AnyCall und double selective AnyCall

**Spec-Referenz:** A.5.2.4.8
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AnyCall muss ein allgemeiner Broadcast sein, der Antworten anfordert, ohne bestimmte Empfänger zu benennen. Er muss für Notfälle, die Rekonstitution von Systemen und den Aufbau neuer Netze verwendet werden können. Die globale AnyCall-Adresse muss „@@?“ sein. Wenn zu viele Antworten eingehen oder die verfügbaren, aber nicht spezifizierten Antwortenden in logische Teilmengen organisiert werden müssen, muss ein selective AnyCall verwendet werden. Ein selective AnyCall muss in Struktur, Funktion und Protokoll dem globalen AnyCall entsprechen, mit der Ausnahme, dass er das letzte einzelne Zeichen der Adresse der gewünschten Empfängergruppe festlegt. Wenn noch engere Akzeptanz- und Antwortkriterien erforderlich sind, muss ein double selective AnyCall verwendet werden. Der double selective AnyCall muss eine operatorgewählte allgemeine Broadcast-Form sein, die dem selective AnyCall entspricht, aber in der Form „@AB“ die letzten zwei Zeichen festlegt, die die gewünschte Empfängergruppe besitzen muss, um eine Antwort auszulösen.

**Akzeptanzkriterien:**
- `AC-ADDR-011-1` — Ein AnyCall fordert Antworten an.
- `AC-ADDR-011-2` — Ein AnyCall benennt keine bestimmten Empfänger.
- `AC-ADDR-011-3` — Die globale AnyCall-Adresse ist „@@?“.
- `AC-ADDR-011-4` — Ein selective AnyCall legt das letzte einzelne Zeichen der Zieladressen fest.
- `AC-ADDR-011-5` — Ein double selective AnyCall legt die letzten zwei Zeichen der Zieladressen fest.
- `AC-ADDR-011-6` — Ein double selective AnyCall wird verwendet, wenn engere Akzeptanz- und Antwortkriterien erforderlich sind.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Globale AnyCall-Adresse | @@? | Adresse | A.5.2.4.8 |
| Selective-AnyCall-Muster | @@A | Adresse | A.5.2.4.8 |
| Double-Selective-AnyCall-Muster | @AB | Adresse | A.5.2.4.8 |
| Zielgruppengröße bei selective AnyCall | 1/36 | Anteil | A.5.2.4.8 |
| Zielgruppengröße bei double selective AnyCall | 1/1296 | Anteil | A.5.2.4.8 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-13 — Die referenzierte AnyCall-Protokolldetailtiefe in A.5.5.4.5 wurde nicht geliefert.
- OPEN-14 — Für ACQ-ALE wird in der Quelle eine Part2-Adressvorgabe genannt; der zugehörige Kontext wurde nicht geliefert.

### 10.12 Wildcards — A.5.2.4.9

#### REQ-ADDR-012 — Wildcard-Zeichen und gleichlange Adresslängen

**Spec-Referenz:** A.5.2.4.9 / Table A-XI
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Wildcard-Zeichen ist ein Sonderzeichen, das der Anrufer verwendet, um Mehrstationsadressen mit einer einzigen Rufadresse anzusprechen. Empfänger müssen das Wildcard-Zeichen als Ersatz für jedes alphanumerische Zeichen in ihren eigenen Adressen an derselben Position oder an denselben Positionen akzeptieren. Jedes Wildcard-Zeichen muss für eines von 36 Zeichen des Basic-38-Satzes stehen. Die Länge der rufenden Wildcard-Adresse und der gerufenen Adresse muss gleich sein. Das spezielle Wildcard-Zeichen muss „?“ sein.

**Akzeptanzkriterien:**
- `AC-ADDR-012-1` — Ein Wildcard-Zeichen kann Mehrstationsadressen mit einer einzigen Rufadresse ansprechen.
- `AC-ADDR-012-2` — Empfänger akzeptieren das Wildcard-Zeichen als Ersatz für jedes alphanumerische Zeichen an derselben Position.
- `AC-ADDR-012-3` — Jedes Wildcard-Zeichen steht für eines von 36 Zeichen des Basic-38-Satzes.
- `AC-ADDR-012-4` — Die Länge der rufenden und der gerufenen Adresse ist gleich.
- `AC-ADDR-012-5` — Das spezielle Wildcard-Zeichen ist „?“.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Ersatzumfang je Wildcard | 36 | Zeichen | A.5.2.4.9 |
| Spezielles Wildcard-Zeichen | ? | Zeichen | A.5.2.4.9 |
| Wildcard-Code | 0111111 | Bitmuster | A.5.2.4.9 |

### 10.13 Self addresses — A.5.2.4.10

#### REQ-ADDR-013 — Selbstadressierung mit eigenen Adressen

**Spec-Referenz:** A.5.2.4.10
**Priorität:** MUST · **Status:** offen

**Anforderung:** Für Selbsttest, Wartung und andere Zwecke müssen Stationen in der Lage sein, ihre eigenen Adressen in Rufen zu verwenden. Wenn eine Selbstadressierungsfunktion erforderlich ist, müssen die folgenden Selbstadressierungsstrukturen und -protokolle verwendet werden. Alle im Standard zulässigen Rufstrukturen und -protokolle, die einen spezifisch adressierten Calling Cycle enthalten, müssen akzeptabel sein, sofern es sich nicht um AllCall oder AnyCall handelt. Die Station darf dabei eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

**Akzeptanzkriterien:**
- `AC-ADDR-013-1` — Stationen können ihre eigenen Adressen in Rufen verwenden.
- `AC-ADDR-013-2` — Selbstadressierung wird für Selbsttest, Wartung und andere Zwecke unterstützt.
- `AC-ADDR-013-3` — Spezifisch adressierte Calling Cycles sind zulässig, sofern sie kein AllCall und kein AnyCall sind.
- `AC-ADDR-013-4` — Eine Station darf eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

### 10.14 Null address — A.5.2.4.11

#### REQ-ADDR-014 — Null-Adresse ohne Ziel, Antwort oder Annahme

**Spec-Referenz:** A.5.2.4.11
**Priorität:** MUST · **Status:** offen

**Anforderung:** Für Test, Wartung, Pufferzeiten und andere Zwecke muss die Station eine Null-Adresse verwenden, die von keiner Station adressiert, angenommen oder beantwortet wird. Wenn eine Null-Adressfunktion erforderlich ist, muss das Standardprotokoll für die Null-Adresse verwendet werden. Die spezielle Null-Adress-Form muss „TO @@@“ oder „REP @@@“ sein, wenn sie unmittelbar nach einem anderen TO steht. Die Null-Adresse darf nur TO oder REP verwenden und nur im Calling Cycle verwendet werden. In Gruppenrufen darf sie nur im führenden Ruf erscheinen und nicht im Scanning Call. Null-Adressen dürfen niemals in der Schlusssequenz verwendet werden. Wenn eine Null-Adresse in einem Gruppenruf erscheint, wird kein Platz in der zugehörigen Antwortslot-Zeile für eine Station belegt; der Slot bleibt leer und kann als Puffer genutzt werden.

**Akzeptanzkriterien:**
- `AC-ADDR-014-1` — Eine Null-Adresse ist weder adressiert noch angenommen noch beantwortet.
- `AC-ADDR-014-2` — Die spezielle Null-Adress-Form ist „TO @@@“ oder „REP @@@“ unter der angegebenen Folgebedingung.
- `AC-ADDR-014-3` — Die Null-Adresse verwendet nur TO oder REP.
- `AC-ADDR-014-4` — Die Null-Adresse wird nur im Calling Cycle verwendet.
- `AC-ADDR-014-5` — In Gruppenrufen erscheint die Null-Adresse nur im führenden Ruf und nicht im Scanning Call.
- `AC-ADDR-014-6` — Null-Adressen werden niemals in der Schlusssequenz verwendet.
- `AC-ADDR-014-7` — Ein gruppenrufbezogener Slot bleibt leer, wenn eine Null-Adresse dort verwendet wird.

### 10.15 In-link address — A.5.2.4.12

#### REQ-ADDR-015 — In-Link-Adresse für alle Mitglieder eines Links

**Spec-Referenz:** A.5.2.4.12
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die In-Link-Adressfunktion muss dazu dienen, dass alle Mitglieder des etablierten Links auf die Informationen reagieren, die in dem Frame enthalten sind, der die In-Link-Adresse trägt. Die In-Link-Adresse muss „?@?“ sein. Wenn ein Funkgerät in den verknüpften Zustand mit einer oder mehreren Stationen eintritt, muss es die Menge der als eigene Adressen erkannten Adressen um die In-Link-Adresse erweitern. Wenn ein Frame von einem Mitglied des Links unter Verwendung der In-Link-Adresse übertragen wird, sind alle Mitglieder öffentlich adressiert und müssen die Frame-Information verwenden. Wenn ein Mitglied einen Frame mit TWAS-Präambel sendet, müssen alle Mitglieder erkennen, dass die sendende Station den Link verlassen hat.

**Akzeptanzkriterien:**
- `AC-ADDR-015-1` — Die In-Link-Adresse gilt für alle Mitglieder des etablierten Links.
- `AC-ADDR-015-2` — Die In-Link-Adresse ist „?@?“.
- `AC-ADDR-015-3` — Ein Funkgerät erweitert im verknüpften Zustand seine als eigene Adressen erkannten Adressen um die In-Link-Adresse.
- `AC-ADDR-015-4` — Ein Frame mit In-Link-Adresse adressiert alle Mitglieder öffentlich.
- `AC-ADDR-015-5` — Ein Frame mit TWAS-Präambel signalisiert allen Mitgliedern, dass die sendende Station den Link verlassen hat.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-15 — Die in der Quelle erwähnten kurzen Keep-Alive-Beispiele werden als erläuternd verstanden; die exakte Protokollregel dafür wurde nicht weiter ausgeführt.

---

### 10.16 Adressvalidierung und Kompatibilitätsregeln — A.5.2.4

#### REQ-ADDR-016 — Adressvalidierung und Kompatibilitätsregeln

**Spec-Referenz:** A.5.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss empfangene Adressen validieren und sicherstellen, dass nur gültige Adressmuster gemäß dem Basic-38-Zeichensatz und den definierten Adressstrukturen verarbeitet werden. Reservierte oder unbekannte Adressmuster müssen als ungültig behandelt werden. ALE-Stationen müssen rückwärtskompatibel mit Implementierungen arbeiten, die nur Basisadressen unterstützen.

**Akzeptanzkriterien:**
- `AC-ADDR-016-1` — Empfangene Adressen werden gegen den Basic-38-Zeichensatz und die definierten Adressstrukturen validiert.
- `AC-ADDR-016-2` — Reservierte oder unbekannte Adressmuster werden als ungültig verworfen.
- `AC-ADDR-016-3` — Das System arbeitet rückwärtskompatibel mit Implementierungen, die nur Basisadressen unterstützen.


**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- **Adress-Normalizer (A.5.2.4.3 / A.5.2.4.10)** — Zerlegt Eingaben und gespeicherte Self-Addresses in 3-Zeichen-Wörter und ergänzt bei Bedarf mit @. Vorschlag Rückverweis: implements REQ-ADDR-003, REQ-ADDR-013, REQ-ADDR-014
- **Adress-Route-Assembler (A.5.2.4.4–A.5.2.4.12)** — Bildet daraus die Wortfolge TO/DATA/REP/TIS/TWAS/THRU gemäß Adresstyp und Calling Cycle. Vorschlag Rückverweis: implements REQ-ADDR-004, REQ-ADDR-006, REQ-ADDR-007, REQ-ADDR-008, REQ-ADDR-009, REQ-ADDR-010, REQ-ADDR-011, REQ-ADDR-012, REQ-ADDR-015
- **Address-Policy-Validator (A.5.2.4)** — Prüft Basic-38, Reserved Patterns und Rückwärtskompatibilität. Vorschlag Rückverweis: implements REQ-ADDR-016



## 12. Nachrichten — A.5.7

> **Spec-Stellen sammeln:** A.5.7.1 (Overview), A.5.7.2 (AMD), A.5.7.3 (DTM), A.5.7.4 (DBM).

### EPIC-MSG · Nachrichtenübertragung

#### REQ-MSG-001 — AMD/CMD/DTM/DBM

**Spec-Referenz:** A.5.7
**Priorität:** WON'T (this release) · **Status:** offen

**Anforderung:** _(Platzhalter — außerhalb des aktuellen Scopes.)_

---

## 13. Offene Punkte und Annahmen

| ID | Thema | Frage / Annahme | Entscheidung | Status |
|---|---|---|---|---|
| OPEN-01 | Twr-Modell | Hardware-Terme (Ttd, Tp, Tta) im SW-Modell = 0? | | offen |
| OPEN-02 | Net vs. Individual | Definiert das Projekt eine eigene Net-Regel oder identisch zu Individual? | | offen |
| OPEN-03 | Group Tlc | Gilt „zweimal senden" (Tlc = 2×Tc) auch für Group Calls? | | offen |
| OPEN-04 | Flag-Verwendung (A.5.2.6) | Verweis auf A.5.2.6 für Flag-Benennung und -Semantik — Inhalt nicht geliefert. | | offen |
| OPEN-05 | Scan-Call-Dauer (A.5.5.2) | Verweis auf A.5.5.2 für exakte Wiederholungsdauer im Scanning-Call — Inhalt nicht geliefert. | | offen |
| OPEN-06 | Rotations-Sequenz (A.5.5.3) | Verweis auf A.5.5.3 für Group-Call Rotationslogik — Inhalt nicht geliefert. | | offen |
| OPEN-07 | Unanimous Votes Verwendung (A.5.2.6) | Verweis auf A.5.2.6 für Nutzung der unanimous votes — Inhalt nicht geliefert. | | offen |
| OPEN-08 | REQ-LINK-004 Spec-Referenz | Genaue Spec-Stelle für Symbolerkennung (RX) noch zu bestimmen (A.5.1.2 / A.5.2.6.2 als Kandidaten). | | offen |
| OPEN-09 | Net-Mix-Regel | Konkrete Einschränkung für das Mischen von individuellen, Netz- und Gruppenadressen verweist auf A.5.5.3 und A.5.5.4 — Inhalt nicht geliefert. | | offen |
| OPEN-10 | Group-Call-Ausnahme | Konkrete Ausnahme für mehrfache Vorkommen einer Adresse verweist auf A.5.5.4 — Inhalt nicht geliefert. | | offen |
| OPEN-11 | AllCall-Protokoll | Die referenzierte Detailtiefe zu A.5.5.4.4 wurde nicht geliefert. | | offen |
| OPEN-12 | ACQ-ALE Part2 | Die in der Quelle genannte Part2-Adressvorgabe für ACQ-ALE wurde nicht im gelieferten Kontext ausgeführt. | | offen |
| OPEN-13 | AnyCall-Protokoll | Die referenzierte Detailtiefe zu A.5.5.4.5 wurde nicht geliefert. | | offen |
| OPEN-14 | ACQ-ALE Part2 (AnyCall) | Die in der Quelle genannte Part2-Adressvorgabe für ACQ-ALE wurde nicht im gelieferten Kontext ausgeführt. | | offen |
| OPEN-15 | Keep-Alive-Beispiel | Die kurzen Keep-Alive-Beispiele der In-Link-Adresse werden als erläuternd verstanden; die exakte Protokollregel wurde nicht weiter spezifiziert. | | offen |
| OPEN-16 | Figure A-14, Table A-VII | Gültige Sequenz- und Größenregeln für Frame-Konstruktion — Dokumente nicht geliefert (REQ-FRAME-001). | | offen |
| OPEN-17 | A.5.5 Frame-Formate | Nur teilweise geliefert; vollständige Format-Vorgaben mit LINK-Requirements abgleichen (REQ-FRAME-001). | | offen |
| OPEN-18 | Sound-Definition (A.5.3) | Vollständige Ausnahmeregel für Sounds noch nicht extrahiert — vgl. REQ-SOUND-001, REQ-FRAME-002. | | offen |
| OPEN-19 | Figure A-15 | Visuelle Darstellung des Leading Call nicht geliefert (REQ-FRAME-004). | | offen |
| OPEN-20 | 5.3.5.1 Attackzeit | Zulässige Attackzeit-Zusatz (additive zu 2,5 ms) noch nicht geliefert (REQ-FRAME-006). | | offen |
| OPEN-21 | Table A-I | Verlinkungswahrscheinlichkeitskriterium nicht geliefert; Compliance-Äquivalenz kann nicht vollständig geprüft werden (REQ-FRAME-006). | | offen |
| OPEN-22 | Figure A-16 | Visuelle Darstellung des Message-Abschnitts nicht geliefert (REQ-FRAME-008). | | offen |
| OPEN-23 | A.5.7.3 | REP/DATA-Regeln im Message-Abschnitt nicht vollständig geliefert (REQ-FRAME-008). | | offen |
| OPEN-24 | A.5.8.2.3 | AQC-ALE Inlink-Entry-Bedingung nicht geliefert (REQ-FRAME-009). | | offen |
| OPEN-25 | Figure A-17 | Visuelle Darstellung der Conclusion nicht geliefert (REQ-FRAME-010). | | offen |
| OPEN-26 | A.5.3 Sounds | Exception-Frame-Regeln noch nicht vollständig extrahiert (REQ-FRAME-011, REQ-SOUND-001). | | offen |
| OPEN-27 | Figure A-18 / A-19 / A-20 | Erlaubte Wortsequenz-Diagramme nicht geliefert; REQ-FRAME-012 bleibt unvollständig. | | offen |
| OPEN-28 | Timing-Genauigkeit (A.5.1.4) | Verweis in A.5.2.6.1 auf A.5.1.4 — Annahme: betrifft REQ-WAVEFORM-013 (10 ppm), bereits erfasst. | | offen |
| OPEN-29 | Self-Timed-Genauigkeit (A.5.1.4) | Verweis in A.5.2.6.1 auf A.5.1.4 für Self-Timed-Betrieb — Annahme: betrifft REQ-WAVEFORM-013, bereits erfasst. | | offen |
| OPEN-30 | Figure A-11 | Blockdiagramm des Empfangsdemodulators nicht geliefert; Systemstruktur kann nicht verifiziert werden (REQ-SYNC-005). | | offen |
| OPEN-31 | DBM-Deinterleaving (A.5.7) | Regeln für Deep-Deinterleaving im DBM-Modus verweisen auf A.5.7 — Inhalt nicht vollständig geliefert (REQ-SYNC-005). | | offen |
| OPEN-32 | Figure A-14 (Wortsequenzen) | Akzeptable Präambeln gemäß Figure A-14 nicht geliefert (REQ-SYNC-006, vgl. OPEN-27). | | offen |
| OPEN-33 | Protokolllogik alle Präambeltypen (A.5.5, A.5.3) | Vollständige Protokolllogik für alle Präambeltypen abhängig von A.5.5 und A.5.3 — Inhalte nicht vollständig geliefert (REQ-SYNC-006). | | offen |
| OPEN-LINK-001 | 4.2.2 Manual Control | NOTE in A.5.5.1 klärt: diese Anforderung hebt 4.2.2 (manuelle PTT) nicht auf. Eingearbeitet in REQ-LINK-007. | geschlossen | umgesetzt |
| OPEN-LINK-002 | Abbildung A-28 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-003 | Abbildung A-29 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-004 | Abbildung A-30 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-005 | Abbildung A-31 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-006 | Abbildung A-32 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-007 | Abbildung A-33 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-008 | Abbildung A-34 | Abbildung wurde nicht geliefert. | | offen |
| OPEN-LINK-009 | Abbildung A-35 | Abbildung wurde nicht geliefert. | | offen |

---

## 14. Traceability-Matrix

> Wird beim Befüllen gepflegt. Verbindet Spec → Requirement → Feature → Test. Eine Zeile je Requirement.

| Requirement | Spec-Referenz | User Story | Feature (Design-Doc) | Test-Case(s) | Status |
|---|---|---|---|---|---|
| REQ-GEN-001 | A.4.1 | US-GEN-001 | | | offen |
| REQ-GEN-002 | A.4.1.1 | US-GEN-001 | | | offen |
| REQ-GEN-003 | A.4.1.2 | US-GEN-001 | | | offen |
| REQ-GEN-004 | A.4.1.3 | US-GEN-001 | | | offen |
| REQ-GEN-005 | A.4.1.4 / A.4.1.5 | US-GEN-001 | | | offen |
| REQ-GEN-006 | A.4.2.1 | US-GEN-002 | | | offen |
| REQ-GEN-007 | A.4.2.1.1 | US-GEN-002 | | | offen |
| REQ-GEN-008 | A.4.2.1.2 | US-GEN-002 | | | offen |
| REQ-GEN-009 | A.4.2.2 / Table A-I | US-GEN-002 | | | offen |
| REQ-GEN-010 | A.4.2.3 / Table A-II | US-GEN-002 | | | offen |
| REQ-GEN-011 | A.4.2.3.1 | US-GEN-002 | | | offen |
| REQ-GEN-012 | A.4.2.3.2 | US-GEN-002 | | | offen |
| REQ-GEN-013 | A.4.3.1 / Table A-III | US-GEN-003 | | | offen |
| REQ-GEN-014 | A.4.3.2 / Table A-IV | US-GEN-003 | | | offen |
| REQ-GEN-015 | A.4.3.3 | US-GEN-003 | | | offen |
| REQ-GEN-016 | A.4.3.3.1 | US-GEN-003 | | | offen |
| REQ-GEN-017 | A.4.3.3.2 | US-GEN-003 | | | offen |
| REQ-GEN-018 | A.4.3.3.3 | US-GEN-003 | | | offen |
| REQ-GEN-019 | A.4.3.4 | US-GEN-003 | | | offen |
| REQ-GEN-020 | A.4.3.5 | US-GEN-003 | | | offen |
| REQ-GEN-021 | A.4.4 / Table A-V | US-GEN-004 | | | offen |
| REQ-GEN-022 | A.4.5.1 | US-GEN-005 | | | offen |
| REQ-GEN-023 | A.4.5.2 | US-GEN-005 | | | offen |
| REQ-GEN-024 | A.4.5.3 | US-GEN-005 | | | offen |
| REQ-GEN-025 | A.4.5.4 | US-GEN-005 | | | offen |
| REQ-WAVEFORM-001 | A.5.1.1 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-002 | A.5.1.2 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-003 | A.5.1.2 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-004 | A.5.1.2 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-005 | A.5.1.2 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-006 | A.5.1.3 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-007 | A.5.1.3 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-008 | A.5.1.3 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-009 | A.5.1.3 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-010 | A.5.1.3 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-011 | A.5.1.4 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-012 | A.5.1.4 | US-WAVEFORM-001 | | | offen |
| REQ-WAVEFORM-013 | A.5.1.4 | US-WAVEFORM-001 | | | offen |
| REQ-WORD-001 | A.5.2.3.1 / Figure A-12 | US-WORD-001 | | | offen |
| REQ-WORD-002 | A.5.2.3.1.2 / A.5.2.3.1.3 / Table A-VIII | US-WORD-001 | | | offen |
| REQ-WORD-003 | A.5.2.3.2.1 | US-WORD-001 | | | offen |
| REQ-WORD-004 | A.5.2.3.2.2 | US-WORD-001 | | | offen |
| REQ-WORD-005 | A.5.2.3.2.3 | US-WORD-001 | | | offen |
| REQ-WORD-006 | A.5.2.3.2.4 | US-WORD-001 | | | offen |
| REQ-WORD-007 | A.5.2.3.2.5 | US-WORD-001 | | | offen |
| REQ-WORD-008 | A.5.2.3.3 / A.5.2.3.3.1 | US-WORD-001 | | | offen |
| REQ-WORD-009 | A.5.2.3.4.1 | US-WORD-001 | | | offen |
| REQ-WORD-010 | A.5.2.3.4.2 | US-WORD-001 | | | offen |
| REQ-FEC-004 | A.5.2.2.1 | US-FEC-001 | | | offen |
| REQ-FEC-005 | A.5.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-006 | A.5.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-007 | A.5.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-008 | A.5.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-009 | A.5.2.2.2.1 | US-FEC-001 | | | offen |
| REQ-FEC-010 | A.5.2.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-011 | A.5.2.2.2.2 | US-FEC-001 | | | offen |
| REQ-FEC-012 | A.5.2.2.3 | US-FEC-001 | | | offen |
| REQ-FEC-013 | A.5.2.2.3 | US-FEC-001 | | | offen |
| REQ-FEC-014 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FEC-015 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FEC-016 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FEC-017 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FEC-018 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FEC-019 | A.5.2.2.4 | US-FEC-001 | | | offen |
| REQ-FRAME-001 | A.5.2.5 | US-FRAME-001 | | | offen |
| REQ-FRAME-002 | A.5.2.5.1 | US-FRAME-001 | | | offen |
| REQ-FRAME-003 | A.5.2.5.1 | US-FRAME-001 | | | offen |
| REQ-FRAME-004 | A.5.2.5.1 / Figure A-15 | US-FRAME-001 | | | offen |
| REQ-FRAME-005 | A.5.2.5.1 | US-FRAME-001 | | | offen |
| REQ-FRAME-006 | A.5.2.5.1 | US-FRAME-001 | | | offen |
| REQ-FRAME-007 | A.5.2.5.1 / A.5.2.5.2 | US-FRAME-001 | | | offen |
| REQ-FRAME-008 | A.5.2.5.2 / Figure A-16 | US-FRAME-001 | | | offen |
| REQ-FRAME-009 | A.5.2.5.2 / A.5.8.2.3 | US-FRAME-001 | | | offen |
| REQ-FRAME-010 | A.5.2.5.3 / Figure A-17 | US-FRAME-001 | | | offen |
| REQ-FRAME-011 | A.5.2.5.3 / A.5.3 | US-FRAME-001 | | | offen |
| REQ-FRAME-012 | A.5.2.5.4 / Figure A-18/19/20 | US-FRAME-001 | | | offen |
| REQ-FRAME-013 | A.5.2.5.4 / Table A-XII | US-FRAME-001 | | | offen |
| REQ-SYNC-001 | A.5.2.6 | US-SYNC-001 | | | offen |
| REQ-SYNC-002 | A.5.2.6.1 | US-SYNC-001 | | | offen |
| REQ-SYNC-003 | A.5.2.6.1 (NOTE) | US-SYNC-001 | | | offen |
| REQ-SYNC-004 | A.5.2.6.1 | US-SYNC-001 | | | offen |
| REQ-SYNC-005 | A.5.2.6.2 / Figure A-11 | US-SYNC-001 | | | offen |
| REQ-SYNC-006 | A.5.2.6.3 | US-SYNC-001 | | | offen |
| REQ-SYNC-007 | A.5.2.6.3 | US-SYNC-001 | | | offen |
| REQ-LINK-001 | A.5.5.3.1 | US-LINK-001 | | | offen |
| REQ-LINK-002 | A.5.2.5.1 | US-LINK-001 | | | offen |
| REQ-LINK-003 | A.5.2.5.1 / A.5.2.3.4.2 | US-LINK-001 | | | offen |
| REQ-LINK-004 | A.5.1.2 / A.5.2.6.2 | US-LINK-002 | | | offen |
| REQ-LINK-005 | A.5.5.2.4 | US-LINK-002 | | | offen |
| REQ-LINK-006 | A.5.2.5.1 | US-LINK-002 | | | offen |
| REQ-LINK-007 | A.5.5.1 | US-LINK-004 | | | offen |
| REQ-LINK-008 | A.5.5.2 | US-LINK-001 | | | offen |
| REQ-LINK-009 | A.5.5.2.1 / Table A-XV | US-LINK-001 | | | offen |
| REQ-LINK-010 | A.5.5.2.2 / Figure A-28 | US-LINK-001 | | | offen |
| REQ-LINK-011 | A.5.5.2.3 | US-LINK-001 | | | offen |
| REQ-LINK-012 | A.5.5.2.3.1 | US-LINK-001 | | | offen |
| REQ-LINK-013 | A.5.5.2.3.2 | US-LINK-001 | | | offen |
| REQ-LINK-014 | A.5.5.2.3.3 | US-LINK-001 | | | offen |
| REQ-LINK-015 | A.5.5.2.4 | US-LINK-002 | | | offen |
| REQ-LINK-016 | A.5.5.3 | US-LINK-001 | | | offen |
| REQ-LINK-017 | A.5.5.3.1 | US-LINK-001 | | | offen |
| REQ-LINK-018 | A.5.5.3.2 | US-LINK-002 | | | offen |
| REQ-LINK-019 | A.5.5.3.3 | US-LINK-002 | | | offen |
| REQ-LINK-020 | A.5.5.3.4 | US-LINK-001 | | | offen |
| REQ-LINK-021 | A.5.5.3.5 | US-LINK-001 | | | offen |
| REQ-LINK-022 | A.5.5.3.5.1 | US-LINK-001 | | | offen |
| REQ-LINK-023 | A.5.5.3.5.2 | US-LINK-001 | | | offen |
| REQ-LINK-024 | A.5.5.3.6 | US-LINK-001 | | | offen |
| REQ-LINK-025 | A.5.5.4 | US-LINK-001 | | | offen |
| REQ-LINK-026 | A.5.5.4.1 | US-LINK-001 | | | offen |
| REQ-LINK-027 | A.5.5.4.1.1 | US-LINK-001 | | | offen |
| REQ-LINK-028 | A.5.5.4.1.2 | US-LINK-001 | | | offen |
| REQ-LINK-029 | A.5.5.4.1.3 | US-LINK-001 | | | offen |
| REQ-LINK-030 | A.5.5.4.1.4 | US-LINK-001 | | | offen |
| REQ-LINK-031 | A.5.5.4.2 | US-LINK-001 | | | offen |
| REQ-LINK-032 | A.5.5.4.2.1 | US-LINK-001 | | | offen |
| REQ-LINK-033 | A.5.5.4.2.2 | US-LINK-001 | | | offen |
| REQ-LINK-034 | A.5.5.4.2.3 | US-LINK-001 | | | offen |
| REQ-LINK-035 | A.5.5.4.3 | US-LINK-001 | | | offen |
| REQ-LINK-036 | A.5.5.4.3.1 | US-LINK-001 | | | offen |
| REQ-LINK-037 | A.5.5.4.3.2 | US-LINK-001 | | | offen |
| REQ-LINK-038 | A.5.5.4.3.3 | US-LINK-001 | | | offen |
| REQ-LINK-039 | A.5.5.4.3.4 | US-LINK-001 | | | offen |
| REQ-LINK-040 | A.5.5.4.3.5 | US-LINK-001 | | | offen |
| REQ-LINK-041 | A.5.5.4.3.6 | US-LINK-001 | | | offen |
| REQ-LINK-042 | A.5.5.4.3.7 | US-LINK-001 | | | offen |
| REQ-LINK-043 | A.5.5.4.3.8 | US-LINK-001 | | | offen |
| REQ-LINK-044 | A.5.5.4.4 | US-LINK-001 | | | offen |
| REQ-LINK-045 | A.5.5.4.5 | US-LINK-001 | | | offen |
| REQ-LINK-046 | A.5.5.4.6 | US-LINK-001 | | | offen |
| REQ-SOUND-001 | A.5.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-002 | A.5.3.1 | US-SOUND-001 | | | offen |
| REQ-SOUND-003 | A.5.3.1 | US-SOUND-001 | | | offen |
| REQ-SOUND-004 | A.5.3.1 | US-SOUND-001 | | | offen |
| REQ-SOUND-005 | A.5.3.2 | US-SOUND-001 | | | offen |
| REQ-SOUND-006 | A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-007 | A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-008 | A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-009 | A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-010 | A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-SOUND-011 | A.5.3.4 | US-SOUND-001 | | | offen |
| REQ-SOUND-012 | A.5.3.1 / A.5.3.3 | US-SOUND-001 | | | offen |
| REQ-ADDR-001 | A.5.2.4.1 | US-ADDR-001 | | | offen |
| REQ-ADDR-002 | A.5.2.4.2 | US-ADDR-001 | | | offen |
| REQ-ADDR-003 | A.5.2.4.3 | US-ADDR-001 | | | offen |
| REQ-ADDR-004 | A.5.2.4.4 | US-ADDR-001 | | | offen |
| REQ-ADDR-005 | Table A-IX / A.5.2.4.4 | US-ADDR-001 | | | offen |
| REQ-ADDR-006 | A.5.2.4.4.1 | US-ADDR-001 | | | offen |
| REQ-ADDR-007 | A.5.2.4.4.2 | US-ADDR-001 | | | offen |
| REQ-ADDR-008 | A.5.2.4.5 | US-ADDR-001 | | | offen |
| REQ-ADDR-009 | A.5.2.4.6 | US-ADDR-001 | | | offen |
| REQ-ADDR-010 | A.5.2.4.7 | US-ADDR-001 | | | offen |
| REQ-ADDR-011 | A.5.2.4.8 | US-ADDR-001 | | | offen |
| REQ-ADDR-012 | A.5.2.4.9 / Table A-XI | US-ADDR-001 | | | offen |
| REQ-ADDR-013 | A.5.2.4.10 | US-ADDR-001 | | | offen |
| REQ-ADDR-014 | A.5.2.4.11 | US-ADDR-001 | | | offen |
| REQ-ADDR-015 | A.5.2.4.12 | US-ADDR-001 | | | offen |
| REQ-ADDR-016 | A.5.2.4 | US-ADDR-001 | | | offen |
| REQ-MSG-001 | A.5.7 | — | | | offen |
| REQ-CHAN-001 | A.5.4 | US-CHAN-001 | | | offen |
| REQ-CHAN-002 | A.5.4 | US-CHAN-001 | | | offen |
| REQ-CHAN-003 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-004 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-005 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-006 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-007 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-008 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-009 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-010 | A.5.4.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-011 | A.5.4.1.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-012 | A.5.4.1.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-013 | A.5.4.1.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-014 | A.5.4.1.3 | US-CHAN-001 | | | offen |
| REQ-CHAN-015 | A.5.4.1.4 | US-CHAN-001 | | | offen |
| REQ-CHAN-016 | A.5.4.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-017 | A.5.4.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-018 | A.5.4.2.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-019 | A.5.4.2.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-020 | A.5.4.2.3 | US-CHAN-001 | | | offen |
| REQ-CHAN-021 | A.5.4.4 | US-CHAN-001 | | | offen |
| REQ-CHAN-022 | A.5.4.4 | US-CHAN-001 | | | offen |
| REQ-CHAN-023 | A.5.4.5 | US-CHAN-001 | | | offen |
| REQ-CHAN-024 | A.5.4.5.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-025 | A.5.4.5.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-026 | A.5.4.5.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-027 | A.5.4.5.3 | US-CHAN-001 | | | offen |
| REQ-CHAN-028 | A.5.4.6 | US-CHAN-001 | | | offen |
| REQ-CHAN-029 | A.5.4.6 | US-CHAN-001 | | | offen |
| REQ-CHAN-030 | A.5.4.6 | US-CHAN-001 | | | offen |
| REQ-CHAN-031 | A.5.4.7 | US-CHAN-001 | | | offen |
| REQ-CHAN-032 | A.5.4.7.1 | US-CHAN-001 | | | offen |
| REQ-CHAN-033 | A.5.4.7.2 | US-CHAN-001 | | | offen |
| REQ-CHAN-034 | A.5.4.7.3 | US-CHAN-001 | | | offen |

---

## 15. Anhang A — Glossar

| Begriff | Bedeutung |
|---|---|
| GEN | General Requirements (A.4) — Allgemeine ALE-Betriebsanforderungen |
| ALE | Automatic Link Establishment |
| Epic | Große fachliche Klammer mehrerer User Stories |
| User Story | Fachlicher Bedarf aus Nutzer-/Systemsicht |
| Requirement | Prüfbare, lösungsneutrale Einzelanforderung |
| Acceptance Criterion | Testbares Kriterium einer Requirement |
| Trw | Redundant Word period (392 ms) |
| Tsc / Tlc / Tcc | Scanning / Leading / gesamte Calling-Cycle-Zeit |
| Twr | Wait-for-Response time |
