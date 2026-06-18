# PC-ALE Feature & Design Specification

**Projekt:** PC-ALE
**Bezug:** `REQUIREMENTS.md` (MIL-STD-188-141B Appendix A)
**Status:** Entwurf
**Letzte Änderung:** 2026-06-12

---

## Über dieses Dokument

Dieses Dokument beschreibt **wie** das System die Anforderungen umsetzt. Es ist die Brücke zwischen dem lösungsneutralen Requirements-Dokument und dem Code.

Während `REQUIREMENTS.md` das **Was/Warum** festhält, enthält dieses Dokument die konkreten **technischen Entwurfsentscheidungen**: Datenstrukturen, Bit-Layouts, Algorithmen, Schnittstellen, Modulgrenzen, Funktionssignaturen.

### Traceability

> **Jedes Feature MUSS mindestens eine Requirement-ID referenzieren.**

```
Spec → REQUIREMENT (Was/Warum) → FEATURE (Wie, Design) → CODE → TEST
                                        │
                                        └─ implements: REQ-xxx-nnn
```

Code-Kommentare verweisen auf Feature-IDs: `// FEAT-WORD-001`

### Gruppierungsprinzip

Features sind **logisch** gruppiert — nicht 1:1 pro Requirement. Ein Feature implementiert eine kohärente technische Einheit, die mehrere eng verwandte Requirements abdeckt.

### ID-Schema

- **Features:** `FEAT-<Bereich>-<nnn>`
- **Design-Entscheidungen:** `DD-<nnn>`

Bereichs-Präfixe identisch zu REQUIREMENTS.md:
`GEN` · `WAVEFORM` · `WORD` · `FEC` · `FRAME` · `SYNC` · `SOUND` · `CHAN` · `LINK` · `ADDR` · `CMD` · `AQC`

### Status

`geplant` · `in Arbeit` · `implementiert` · `getestet`

---

## Inhaltsverzeichnis

1. [Architekturüberblick](#1-architekturüberblick)
2. [Feature-Katalog](#2-feature-katalog)
3. [Design-Entscheidungen](#3-design-entscheidungen)
4. [Schnittstellen](#4-schnittstellen)
5. [Feature-Detailbeschreibungen](#5-feature-detailbeschreibungen)
6. [Traceability-Matrix](#6-traceability-matrix)

---

## 1. Architekturüberblick

### 1.1 Schichtenmodell

```
APPLICATION LAYER (separates Projekt)
  UI / CLI / Operator Interface

        │
        ▼
PC-ALE CORE (Domain – Referenzimplementierung)
  │
  │  Signalfluss TX: Anwendung → Protocol → Word → FEC → FSK → HF
  │  Signalfluss RX: HF → FSK → FEC → Word → Protocol → Anwendung
  │
  ├── FS-1052 ARQ:                                          [außerhalb des ALE-Stacks]
  │     FS1052ARQ       (FS1052/fs1052_arq.h      / FS1052/fs1052_arq.cpp)
  │     FS1052Protocol  (FS1052/fs1052_protocol.h / FS1052/frame_format.cpp)
  │
  ├── Stores:                                               [geplant]
  │     ChannelStore
  │     SelfAddressStore
  │     OtherStationStore
  │     LQAStore
  │     MessageStore
  │     OperatingParameters   (Stores/ale_data_store.h / Stores/ale_data_store.cpp)
  │     AddressBook           (Stores/address_book.h   / Stores/address_book.cpp)
  │       – self_address        eigene Stationsadresse
  │       – known_stations      bekannte Gegenstellen + Namen
  │       – known_nets          bekannte Netzadressen
  │       – match_wildcard()    Adressvergleich mit '@'-Wildcard
  │
  ├── LQA:
  │     LQAAnalyzer     (LQA/lqa_analyzer.h / LQA/lqa_analyzer.cpp)
  │     LQAMetrics      (LQA/lqa_metrics.h  / LQA/lqa_metrics.cpp)
  │     LQADatabase     (LQA/lqa_database.h / LQA/lqa_database.cpp)
  │
  ├── Protocol:
  │   │
  │   ├── Control:
  │   │     ALEStateMachine     (Protocol/ale_state_machine.h    / Protocol/ale_state_machine.cpp)
  |	  |		ALETimingConstants	(Protocol/ale_timing.h)
  │   │     ChannelSelector     (Protocol/ale_channel_selector.h / Protocol/ale_channel_selector.cpp) [geplant]
  │   │     ListenBeforeTransmit(Protocol/ale_channel_selector.h / Protocol/ale_channel_selector.cpp) [geplant]
  │   │
  │   ├── Message:
  │   │     ALEMessage, MessageAssembler,
  │   │       FrameValidator,
  │   │       CallTypeDetector  (Protocol/ale_message.h / Protocol/ale_message.cpp)
  │   │                          (privat: src/Protocol/frame_validator.cpp)
  │   │                          (privat: src/Protocol/call_type_detector.cpp)
  │   │
  │   └── AQC:
  │         AQCProtocol         (Protocol/aqc_protocol.h / Protocol/aqc_protocol.cpp)
  │                              (privat: src/Protocol/aqc_parser.cpp)
  │
  ├── Word:
  │     PreambleType, WordType,
  │     ALEWord, WordParser     (Word/ale_word.h / Word/ale_word.cpp)
  │       – is_valid_basic38_char()
  │       – is_valid_expanded64_char()
  │       – uses_basic38()
  │
  ├── FEC:
  │     ALEFECCodec             (FEC/ale_fec_codec.h / FEC/FEC/ale_fec_codec.cpp)        ← public
  │     Golay(24,12)             (privat: src/FEC/golay.h       / src/FEC/golay.cpp)
  │     Interleaver              (privat: src/FEC/interleaver.h / src/FEC/interleaver.cpp)
  │
  └── Modem / FSK:
        ALE2GModem      (FSK/ale_waveform.h    / FSK/ale_waveform.cpp)               [geplant]
        ToneGenerator   (FSK/tone_generator.h  / src/FSK/tone_generator.cpp)
        FFTDemodulator  (FSK/fft_demodulator.h / FSK/fft_demodulator.cpp)
        SymbolDecoder   (FSK/symbol_decoder.h  / FSK/symbol_decoder.cpp)

        │ (PAL Interfaces)
        ▼
PC-ALE-PAL
  IAudioDriver · IRadio · ITimer · ILogger

        │
        ▼
PLATFORM ADAPTERS (separate Projekte)
  WasapiAudioDriver (src/platform/win/audio_wasapi.cpp)
  ALSA · SDR · Hamlib
```

---

## 1.2 Modulübersicht

| Modul | Verantwortung | Status | Features |
|---|---|---|---|
| `include/FS1052/fs1052_arq.h` / `src/FS1052/fs1052_arq.cpp` | FS-1052 ARQ Protokoll | implementiert | FEAT-LINK-003 |
| `include/FS1052/fs1052_protocol.h` / `src/FS1052/frame_format.cpp` | FS-1052 Frame-Format | implementiert | FEAT-LINK-003 |
| `include/Stores/ale_data_store.h` / `src/Stores/ale_data_store.cpp` | Kanal-/LQA-/Nachrichten-/Betriebsparameterspeicher | geplant | FEAT-GEN-004–008 |
| `include/Stores/address_book.h` / `src/Stores/address_book.cpp` | Eigene Adresse, bekannte Stationen/Netze, Wildcard-Match | geplant | FEAT-ADDR-001–005 |
| `include/LQA/lqa_analyzer.h` / `src/LQA/lqa_analyzer.cpp` | LQA-Analyse, Kanalauswahl | implementiert | FEAT-CHAN-001, FEAT-CHAN-005 |
| `include/LQA/lqa_metrics.h` / `src/LQA/lqa_metrics.cpp` | LQA-Metriken (SINAD, BER, Multipath, FEC-Fehler) | implementiert | FEAT-CHAN-002, FEAT-CHAN-003 |
| `include/LQA/lqa_database.h` / `src/LQA/lqa_database.cpp` | LQA-Datenbank, Kanalqualitätsspeicher | implementiert | FEAT-GEN-006, FEAT-CHAN-005 |
| `include/Protocol/ale_state_machine.h` / `src/Protocol/ale_state_machine.cpp` | Calling Cycle, Frame-Phasen, ALE-Zustände, Linking, Sounding | implementiert | FEAT-FRAME-001–006, FEAT-SYNC-001, FEAT-LINK-001–007, FEAT-SOUND-001–003 |
| `include/Protocol/ale_channel_selector.h` / `src/Protocol/ale_channel_selector.cpp` | Kanalauswahl, Listen-Before-Transmit, CMD-LQA-Reports | geplant | FEAT-CHAN-001–006 |
| `include/Protocol/ale_message.h` / `src/Protocol/ale_message.cpp` | MessageAssembler, ALEMessage, CallTypeDetector, Message-Section-Verarbeitung | implementiert | FEAT-FRAME-001, FEAT-FRAME-004 |
| `src/Protocol/frame_validator.cpp` *(privat)* | Frame-Strukturvalidierung (FROM-vor-CMD, THRU/REP-Alternierung, Sequenzlimits) | implementiert | FEAT-FRAME-001, FEAT-FRAME-006 |
| `src/Protocol/call_type_detector.cpp` *(privat)* | Erkennung von Individual-, Net-, Group-, Sounding- und AMD-Calls | implementiert | FEAT-FRAME-001 |
| `include/Protocol/aqc_protocol.h` / `src/Protocol/aqc_protocol.cpp` | AQC-ALE-Protokoll | implementiert | FEAT-GEN-010 |
| `src/Protocol/aqc_parser.cpp` *(privat)* | AQC-Frame-Parser | implementiert | FEAT-GEN-010 |
| `include/Protocol/ale_version_caps.h` / `src/Protocol/ale_version_caps.cpp` | Version CMD, Capabilities Query/Report, Capability-Parser | geplant | FEAT-GEN-011, FEAT-GEN-012 |
| `include/Protocol/ale_orderwire_protocols.h` / `src/Protocol/ale_orderwire_protocols.cpp` | AMD-, DTM- und DBM-Protokolle | geplant | FEAT-GEN-013–016 |
| `include/Word/ale_word.h` / `src/Word/ale_word.cpp` | PreambleType, WordType, ALEWord, WordParser (Basic-38, Expanded-64) | implementiert | FEAT-WORD-001–003 |
| `include/FEC/ale_fec_codec.h` / `src/FEC/ale_fec_codec.cpp` | FEC-Fassade: Golay + Interleaver + Vote-Verarbeitung | implementiert | FEAT-FEC-003–005 |
| `include/FEC/golay.h` / `src/FEC/golay.cpp` | Golay(24,12) Encoder/Decoder | implementiert | FEAT-FEC-001, FEAT-FEC-002 |
| `src/FEC/interleaver.h` / `src/FEC/interleaver.cpp` *(privat)* | Bit-Interleaver (A/B-Schema gemäß Fig. A-10) | implementiert | FEAT-FEC-003 |
| `include/FSK/symbol_decoder.h` / `src/FSK/symbol_decoder.cpp` | Symbol-Decoder, Majority-Vote, Vote-Metriken | implementiert | FEAT-FEC-004, FEAT-FEC-005 |
| `include/FSK/fft_demodulator.h` / `src/FSK/fft_demodulator.cpp` | FFT-Demodulator (64-Punkt), Peak-Detektor | implementiert | FEAT-SYNC-002 |
| `include/FSK/tone_generator.h` / `src/FSK/tone_generator.cpp` | NCO-Tongenerator, Phasenkontinuität | implementiert | FEAT-WAVEFORM-002 |
| `include/FSK/ale_waveform.h` / `src/FSK/ale_waveform.cpp` | Waveform-Parameter, Frequenzen, Timings, Symboltabellen, Symbol↔PCM-Adapter, RX-Wordsynchronisation | implementiert / geplant | FEAT-WAVEFORM-001–003, FEAT-SYNC-002, FEAT-SYNC-003 |

---

## 2. Feature-Katalog

| Feature-ID | Titel | Setzt um (REQ / Standard) | Modul | Priorität | Status |
|---|---|---|---|---|---|
| FEAT-GEN-001 | ALE Data-Link-Schichtstruktur & Grundbetrieb | REQ-GEN-001–005 | `Protocol/ale_state_machine.h/cpp` | MUST | geplant |
| FEAT-GEN-002 | Scan-Raten & AQC-Rückwärtskompatibilität | REQ-GEN-006–008 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-GEN-003 | Belegtheitserkennung & Linking-Wahrscheinlichkeit | REQ-GEN-009–012 | `tests/test_gen_performance.cpp` | MUST | geplant |
| FEAT-GEN-004 | Kanalspeicher (Channel Memory) | REQ-GEN-013 | `Stores/ale_data_store.h/cpp` | MUST | geplant |
| FEAT-GEN-005 | Selbstadressspeicher (Self Address Memory) | REQ-GEN-014 | `Stores/address_book.h/cpp` | MUST | geplant |
| FEAT-GEN-006 | Fremdstations-Tabelle & LQA-Speicher | REQ-GEN-015–018 | `Stores/ale_data_store.h/cpp`, `LQA/lqa_database.h/cpp` | MUST | geplant |
| FEAT-GEN-007 | Betriebsparameter-Programmierbarkeit | REQ-GEN-019 | `Stores/ale_data_store.h/cpp` | MUST | geplant |
| FEAT-GEN-008 | Nachrichtenspeicher (Message Memory) | REQ-GEN-020 | `Stores/ale_data_store.h/cpp` | MUST | geplant |
| FEAT-GEN-009 | ALE-Betriebsregeln (Operational Rules) | REQ-GEN-021 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-GEN-010 | AQC-ALE-Protokoll | REQ-GEN-022–025 | `Protocol/aqc_protocol.h/cpp`, `src/Protocol/aqc_parser.cpp` | COULD | geplant |
| FEAT-GEN-011 | Version CMD (`CMD VERSION`) | MIL-STD-188-141B A.5.6.6.1 | `Protocol/ale_version_caps.h/cpp` | MUST | geplant |
| FEAT-GEN-012 | Capabilities Query/Report (`CMD CAPABILITIES`) einschließlich Scan Rate, Channels Scanned, Tune Time, LP, ALQA, Scheduling, Orderwire | MIL-STD-188-141B A.5.6.6.2 | `Protocol/ale_version_caps.h/cpp` | MUST | geplant |
| FEAT-GEN-013 | ALE Message-Protocol Framework | MIL-STD-188-141B A.5.7.1 | `Protocol/ale_orderwire_protocols.h/cpp` | MUST | geplant |
| FEAT-GEN-014 | Automatic Message Display (AMD) | MIL-STD-188-141B A.5.7.2 | `Protocol/ale_orderwire_protocols.h/cpp`, `Protocol/ale_message.h/cpp` | MUST | geplant |
| FEAT-GEN-015 | Data Text Message (DTM) | MIL-STD-188-141B A.5.7.3 | `Protocol/ale_orderwire_protocols.h/cpp` | SHOULD | geplant |
| FEAT-GEN-016 | Data Block Message (DBM) | MIL-STD-188-141B A.5.7.4 | `Protocol/ale_orderwire_protocols.h/cpp` | SHOULD | geplant |
| FEAT-WAVEFORM-001 | Tone-Symbol-Mapping & Frequenztabelle | REQ-WAVEFORM-001–003 | `FSK/ale_waveform.h/cpp` | MUST | implementiert |
| FEAT-WAVEFORM-002 | NCO-Tongenerator mit Phasenkontinuität | REQ-WAVEFORM-004–005 | `FSK/tone_generator.h/cpp` | MUST | implementiert |
| FEAT-WAVEFORM-003 | Timing-Konstanten & Wortgrenzen | REQ-WAVEFORM-006–010 | `FSK/ale_waveform.h/cpp` | MUST | implementiert |
| FEAT-WAVEFORM-004 | Genauigkeits-Verifikation | REQ-WAVEFORM-011–013 | `tests/test_tone_accuracy.cpp` | MUST | geplant |
| FEAT-WORD-001 | Word24 Bit-Layout Encoding/Decoding | REQ-WORD-001–002 | `Word/ale_word.h/cpp` | MUST | implementiert |
| FEAT-WORD-002 | Adresswörter (TO/TIS/TWAS/THRU/FROM) | REQ-WORD-003–007 | `Word/ale_word.h/cpp` | MUST | implementiert |
| FEAT-WORD-003 | Message- und Extension-Words (CMD/DATA/REP) | REQ-WORD-008–010 | `Word/ale_word.h/cpp` | MUST | in Arbeit |
| FEAT-FEC-001 | Golay(24,12) Encoder | REQ-FEC-004–009 | `FEC/golay.h/cpp` | MUST | implementiert |
| FEAT-FEC-002 | Golay(24,12) Decoder mit Fehlerkorrektur | REQ-FEC-008, REQ-FEC-010–011 | `FEC/golay.h/cpp` | MUST | implementiert |
| FEAT-FEC-003 | Interleaving / Deinterleaving | REQ-FEC-012–013 | `FEC/interleaver.h/cpp`, `FEC/ale_fec_codec.h/cpp` | MUST | implementiert |
| FEAT-FEC-004 | 3×-Redundanz mit Majority-Vote | REQ-FEC-014–018 | `FSK/symbol_decoder.h/cpp` | MUST | in Arbeit |
| FEAT-FEC-005 | Unanimous-Vote-Erfassung | REQ-FEC-019 | `FSK/symbol_decoder.h/cpp`, `FEC/ale_fec_codec.h/cpp` | MUST | geplant |
| FEAT-FRAME-001 | Frame-Grundstruktur & Wortbasis | REQ-FRAME-001 | `Protocol/ale_message.h/cpp`, `frame_validator.cpp`, `call_type_detector.cpp` | MUST | offen |
| FEAT-FRAME-002 | Scanning Call – Individual Call | REQ-FRAME-002, REQ-FRAME-003 (Individual), REQ-FRAME-005–006 | `Protocol/ale_state_machine.cpp` | MUST | implementiert |
| FEAT-FRAME-003 | Leading Call | REQ-FRAME-004 | `Protocol/ale_state_machine.cpp` | MUST | offen |
| FEAT-FRAME-004 | Message-Abschnitt | REQ-FRAME-007–009 | `Protocol/ale_state_machine.cpp`, `Protocol/ale_orderwire_protocols.cpp` | MUST | geplant |
| FEAT-FRAME-005 | Conclusion | REQ-FRAME-010–011 | `Protocol/ale_state_machine.cpp` | MUST | implementiert |
| FEAT-FRAME-006 | Gültige Sequenzen & Frame-Limits | REQ-FRAME-012–013 | `Protocol/ale_state_machine.cpp`, `frame_validator.cpp` | MUST | geplant |
| FEAT-FRAME-007 | Scanning Call – Net Call | REQ-FRAME-003 (Net) | `Protocol/ale_state_machine.cpp` | MUST | offen |
| FEAT-SYNC-001 | Trw-Grid (Sendeseitiger Wortphasen-Anker) | REQ-SYNC-001–004 | `Protocol/ale_state_machine.cpp` | MUST | implementiert |
| FEAT-SYNC-002 | Empfangsseitige Wortsynchronisation | REQ-SYNC-005 | `FSK/ale_waveform.h/cpp`, `FSK/fft_demodulator.cpp` | MUST | geplant |
| FEAT-SYNC-003 | Synchronisationskriterien & Schwellwerte | REQ-SYNC-006–007 | `FSK/ale_waveform.h/cpp` | MUST | geplant |
| FEAT-SOUND-001 | Single-Channel Sounding | REQ-SOUND-002–005 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-SOUND-002 | Multichannel-Scanning-Sounding | REQ-SOUND-006–010 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-SOUND-003 | Optionales Sounding-Handshake | REQ-SOUND-011–012 | `Protocol/ale_state_machine.cpp` | SHOULD | geplant |
| FEAT-CHAN-001 | Channel Selection & LQA-Grundfunktion | REQ-CHAN-001–010 | `Protocol/ale_channel_selector.h/cpp`, `LQA/lqa_analyzer.h/cpp` | MUST | geplant |
| FEAT-CHAN-002 | BER-, SINAD- und Multipath-Messung | REQ-CHAN-011–015 | `LQA/lqa_metrics.h/cpp` | MUST | geplant |
| FEAT-CHAN-003 | CMD-LQA-Word (BER/SINAD/MP-Felder) | REQ-CHAN-016–020 | `Protocol/ale_channel_selector.cpp`, `LQA/lqa_metrics.h/cpp` | MUST | geplant |
| FEAT-CHAN-004 | Local Noise Report CMD | REQ-CHAN-021–022 | `Protocol/ale_channel_selector.cpp` | COULD | geplant |
| FEAT-CHAN-005 | Single- und Multi-Station Channel Selection | REQ-CHAN-023–030 | `Protocol/ale_channel_selector.h/cpp`, `LQA/lqa_database.h/cpp` | MUST | geplant |
| FEAT-CHAN-006 | Listen Before Transmit | REQ-CHAN-031–034 | `Protocol/ale_channel_selector.h/cpp` | MUST | geplant |
| FEAT-LINK-001 | Individual Call senden | REQ-LINK-001–002, 007–009, 016–017 | `Protocol/ale_state_machine.cpp` | MUST | implementiert |
| FEAT-LINK-002 | Individual Call empfangen & Response | REQ-LINK-004–006, 018–019 | `FSK/ale_waveform.cpp`, `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-LINK-003 | Acknowledgment & Link-Termination | REQ-LINK-020–023 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-LINK-004 | Kanalwechsel & Kollisionserkennung | REQ-LINK-010–015, 024 | `Protocol/ale_state_machine.cpp`, `Protocol/ale_channel_selector.cpp` | MUST | geplant |
| FEAT-LINK-005 | One-to-Many: Slotted Responses & Star Net | REQ-LINK-003, 025–034 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-LINK-006 | One-to-Many: Star Group Call | REQ-LINK-035–043 | `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-LINK-007 | AllCall-, AnyCall- und Wildcard-Protokolle | REQ-LINK-044–046 | `Protocol/ale_state_machine.cpp`, `Stores/address_book.cpp` | MUST | geplant |
| FEAT-ADDR-001 | Basic-38-Zeichensatz & Adressvalidierung | REQ-ADDR-001–002, 016 | `Word/ale_word.cpp`, `Stores/address_book.cpp` | MUST | implementiert |
| FEAT-ADDR-002 | Adress-Chunking, Stuffing & Erweiterung | REQ-ADDR-003–007 | `Word/ale_word.cpp`, `Protocol/ale_state_machine.cpp` | MUST | implementiert |
| FEAT-ADDR-003 | Net-, Group-, AllCall- und AnyCall-Adressen | REQ-ADDR-008–011 | `Stores/address_book.cpp`, `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-ADDR-004 | Wildcard-Matching | REQ-ADDR-012 | `Stores/address_book.cpp` | MUST | implementiert |
| FEAT-ADDR-005 | Self-, Null- und In-Link-Adressen | REQ-ADDR-013–015 | `Stores/address_book.cpp`, `Protocol/ale_state_machine.cpp` | MUST | geplant |
| FEAT-CMD-001 | CRC-FCS (16-Bit Rahmenprüfsumme) | MIL-STD-188-141B A.5.6.1 | `Protocol/ale_orderwire_protocols.h/cpp` | MUST | geplant |
| FEAT-CMD-002 | Power Control CMD | MIL-STD-188-141B A.5.6.2 | `Protocol/ale_orderwire_protocols.h/cpp` | COULD | geplant |
| FEAT-CMD-003 | Channel Related CMD Functions | MIL-STD-188-141B A.5.6.3 | `Protocol/ale_orderwire_protocols.h/cpp` | SHOULD | geplant |
| FEAT-CMD-004 | Time-Related CMDs (Tune&Wait, Schedule, Time Exchange) | MIL-STD-188-141B A.5.6.4 | `Protocol/ale_orderwire_protocols.h/cpp` | SHOULD | geplant |
| FEAT-CMD-005 | Modem Negotiation & Handoff CMD | MIL-STD-188-141B A.5.6.5 | `Protocol/ale_orderwire_protocols.h/cpp` | SHOULD | geplant |
| FEAT-CMD-006 | Do-Not-Respond CMD | MIL-STD-188-141B A.5.6.7 | `Protocol/ale_orderwire_protocols.h/cpp` | MUST | geplant |
| FEAT-CMD-007 | User Unique Functions (UUF) | MIL-STD-188-141B A.5.6.9 | `Protocol/ale_orderwire_protocols.h/cpp` | COULD | geplant |
| FEAT-AQC-001 | AQC Word-Struktur & Packed-Address (Base-40) | MIL-STD-188-141B A.5.8.1.1 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-002 | AQC Preamble-Typen (8 AQC-spezifische Preambles) | MIL-STD-188-141B A.5.8.1.2 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-003 | AQC Adresscharakteristiken & Call-Typen | MIL-STD-188-141B A.5.8.1.3–A.5.8.1.4 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-004 | AQC Calling Cycle & Unit Call Protokoll | MIL-STD-188-141B A.5.8.2.1–A.5.8.2.2 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-005 | AQC Star Net Call | MIL-STD-188-141B A.5.8.2.3 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-006 | AQC Orderwire Functions (ACK/NAK, Control Messages) | MIL-STD-188-141B A.5.8.3 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |
| FEAT-AQC-007 | AQC Linking Protection (LP) | MIL-STD-188-141B A.5.8.4 | `Protocol/aqc_protocol.h/cpp` | COULD | geplant |

---

## 3. Design-Entscheidungen

### DD-001 — word24 Bit-Layout: W1=bit23 (MSB)

**Betrifft Features:** FEAT-WORD-001
**Status:** akzeptiert

**Kontext:** Das 24-Bit-Wort enthält Preamble (3 Bit) und Payload (21 Bit = 3×7-Bit-ASCII). Die Frage war: welche Bits sind die höchstwertigen?

**Entscheidung:** W1 (erstes gesendetes Bit im Spec-Diagramm) = bit23 (MSB des uint32_t). Das Spec-Diagramm liest Felder von links (MSB) nach rechts (LSB):
```
word24[23:21] = W1..W3   = Preamble  (W1=MSB)
word24[20:14] = W4..W10  = Char1     (7-bit ASCII, W4=MSB)
word24[13:7]  = W11..W17 = Char2
word24[6:0]   = W18..W24 = Char3     (W24=LSB=bit0)
```

**Alternativen:** W1=bit0 (LSB) wurde initial implementiert — führte zu inkompatiblen Wörtern mit allen anderen ALE-Decodern.

**Konsequenzen:** Alle Shift-Operationen verwenden `>> 21` für Preamble-Extraktion, `>> 14` für Char1 etc. Encode: `(preamble << 21) | (char1 << 14) | (char2 << 7) | char3`.

---

### DD-002 — NCO 32-Bit-Phasenakkumulator für Tongenerator

**Betrifft Features:** FEAT-WAVEFORM-002
**Status:** akzeptiert

**Kontext:** 8-FSK erfordert phasenkontinuierliche Tonübergänge an Maxima/Minima (REQ-WAVEFORM-005).

**Entscheidung:** Numerically Controlled Oscillator (NCO) mit 32-Bit-Phasenakkumulator. Init auf `0x40000000` (= π/2 in Q32) — entspricht sin=+1, Ableitung=0. Symbolübergänge erfolgen damit automatisch an Waveform-Maxima.

**Alternativen:** Direkte Berechnung per `sin(2πft)` — keine Phasenkontinuität über Symbolgrenze garantiert.

**Konsequenzen:** Phase-Increment pro Sample: `(freq_hz << 32) / SAMPLE_RATE_HZ`. Integer-Arithmetik, keine Floating-Point-Drift.

---

### DD-003 — Golay-Split: Coder A (W1..W12) / Coder B (W13..W24) mit Parity-Inversion

**Betrifft Features:** FEAT-FEC-001/002/003
**Status:** akzeptiert

**Kontext:** Ein 24-Bit-Wort wird durch zwei Golay-(24,12)-Coder verarbeitet. Die untere Parity muss invertiert werden (Spec A.5.2.2.2).

**Entscheidung:**
```
Coder A: upper = word24[23:12] → Parity G1..G12  (normal)
Coder B: lower = word24[11:0]  → Parity G13..G24 (invertiert)

f.G[11:0]  = G1..G12    (normale Parity,    G1=bit11=MSB)
f.G[23:12] = ~G13..G24  (invertierte Parity, ~G13=bit23=MSB)
```

**Konsequenzen:** Beim Decode muss `~(f.G >> 12) & 0xFFF` für den unteren Parity angewendet werden.

---

### DD-004 — Interleave-Muster: A1 B1 A2 B2 … A24 B24 S49

**Betrifft Features:** FEAT-FEC-003
**Status:** akzeptiert

**Entscheidung:**
```
A = [W1..W12 | G1..G12]        Coder A, MSB zuerst
B = [W13..W24 | ~G13..~G24]    Coder B, MSB zuerst

Data-Bits  (k=0..11):  out[2k]   = W_(k+1)  = f.W[23-k]
                        out[2k+1] = W_(13+k) = f.W[11-k]
Parity-Bits (k=12..23): out[2k]   = G_(k-11) = f.G[23-k]
                        out[2k+1] = ~G_(k+1) = f.G[35-k]
out[48] = 0  (Stuff-Bit S49)
```

**Konsequenzen:** 49 Bits → 49 Symbole à 3 Bit → 147 Bit Kanal-Stream nach 3× Redundanz.

---

### DD-005 — Sample-Rate: 8 kHz intern, 48 kHz WASAPI

**Betrifft Features:** FEAT-WAVEFORM-002/003
**Status:** akzeptiert

**Entscheidung:** Modem arbeitet intern auf 8 kHz. WasapiAudioDriver führt Resampling durch (8kHz↔48kHz, Faktor 6) über `pal::Resampler`. Keine Allokation im RT-Thread.

**Konsequenzen:** Audio-Callback arbeitet mit `modem_frames = device_frames / ratio`. Jede Änderung der Modem-Sample-Rate erfordert Resampler-Neuinitialisierung.

---

### DD-006 — Globaler Trw-Grid-Anker (first_call_tx_ms)

**Betrifft Features:** FEAT-SYNC-001, FEAT-FRAME-001–005
**Status:** akzeptiert · **Implementiert in:** `enter_state(CALLING)`, `handle_calling()`

**Entscheidung:** `first_call_tx_ms` wird beim Eintritt in CALLING einmalig
gesetzt und nie wieder verändert. Alle Slot-Zeitpunkte werden ausschließlich
relativ zu diesem Anker berechnet (DD-013: Slots = Trw):

```
next_slot_ms = first_call_tx_ms + call_cycle_count × Trw_ms
```

`call_cycle_count` zählt Trw-Slots über die gesamte Übertragung und wird
nie zwischen Phasen zurückgesetzt. `call_cycles_in_phase` zählt zusätzlich
die Position innerhalb der aktuellen Phase (ebenfalls in Trw-Slots) und
wird beim Phasenwechsel auf 0 zurückgesetzt.

**Ausnahme LISTENING:** Die LISTENING-Phase (RX-Fenster, Twr-Timeout) liegt
außerhalb des Trw-Grids. Sie verwendet `listening_start_ms` — einen separaten
Zeitstempel, der beim Übergang CONCLUSION→LISTENING in `on_word_complete()`
gesetzt wird — ausschließlich für den Twr-Timeout. `first_call_tx_ms` bleibt
davon unberührt.

**Alternativen:** `call_phase_start_ms` als Referenz pro Phase — abgelehnt
und im Zuge von FEAT-FRAME-001 aus dem Code entfernt, weil ein neuer
Zeitanker beim Phasenwechsel das Trw-Grid bricht und Timing-Drift zwischen
den Phasen einführt.

---

### DD-007 — Adress-Chunking: 3-Zeichen-Gruppen mit @-Stuffing

**Betrifft Features:** FEAT-ADDR-002
**Status:** akzeptiert

**Entscheidung:** `chunk_address()` iteriert `i += 3` über die Adresse (max 15 Zeichen), paddet mit `@`. Worttyp-Sequenz: erstes Wort = TO/TIS/THRU, Folgewörter = DATA, REP, DATA, REP. REP darf NIEMALS unmittelbar auf TIS/TWAS folgen.

**Wichtig:** 15-Zeichen-Grenze = max 5 Wörter, nicht 15 separate Wörter.

---

### DD-008 — Datenspeicher: Trennung von ChannelStore und ALEStateMachine

**Betrifft Features:** FEAT-GEN-004–008
**Status:** akzeptiert

**Kontext:** REQ-GEN-013–020 fordern nichtflüchtige Speicher für Kanäle, Adressen, LQA, Nachrichten. Bisher war kein separates Speichermodul vorgesehen; die State Machine hatte alle Daten inline.

**Entscheidung:** Neues Modul `include/Stores/ale_data_store.h` / `src/Stores/ale_data_store.cpp` mit klar getrennten Store-Klassen. Die `ALEStateMachine` hält nur Referenzen darauf, besitzt aber keine Daten selbst. Persistierung erfolgt über ein abstraktes `IPersistenceBackend`-Interface — konkrete Implementierungen (Datei, EEPROM) sind plattformspezifisch.

**Konsequenzen:** `ALEStateMachine` erhält im Konstruktor `AleDataStore&`. Alle Stores sind über `AleDataStore` erreichbar. Tests können `InMemoryPersistenceBackend` injizieren.

---

### DD-009 — ALE State Machine: Zwei-Ebenen-Zustandsautomat (Figure A-28)

**Betrifft Features:** FEAT-LINK-001–004
**Status:** akzeptiert · **Implementiert in:** `ale_state_machine.h/.cpp`

**Kontext:** A.5.5.2.2 definiert drei konzeptionelle Zustände: AVAILABLE,
SOUNDING, LINKED. Die bisherige State Machine modelliert die Frame-Phasen
(SCANNING, LEADING, CONCLUSION, LISTENING), aber nicht die übergeordneten
Link-Zustände. Beide Ebenen werden benötigt.

**Entscheidung:** Zwei-Ebenen-State-Machine:

- **Outer States** (ALE-Protokollzustände, `enum class ALEState`):
  `IDLE` (= AVAILABLE), `SCANNING`, `CALLING`, `HANDSHAKE`, `LINKED`, `SOUNDING`, `ERROR`
- **Inner States (CALLING)** (Frame-Phasen, `enum class CallingPhase`, nur aktiv wenn
  Outer State = `CALLING`):
  `SCANNING_CALL`, `LEADING_CALL`, `MESSAGE`, `CONCLUSION`, `LISTENING`, `NET_CALL_STUB`
- **Inner States (LINKED)** (Link-Phasen, `enum class LinkedPhase`, nur aktiv wenn
  Outer State = `LINKED`):
  `ACTIVE`, `ORDERWIRE`, `TERMINATING`
- **Inner States (HANDSHAKE)** (Handshake-Phasen, `enum class HandshakePhase`, nur aktiv wenn
  Outer State = `HANDSHAKE`):
  `READING_CALL`, `CHANNEL_CHECK`, `SENDING_RESPONSE`, `WAITING_FOR_ACK`,
  `READING_ACK`, `READING_RESPONSE`, `SENDING_ACK` — Details: DD-014

```cpp
enum class ALEState : uint8_t {
    IDLE,       // = AVAILABLE; kein Ruf, kein Scan aktiv
    SCANNING,   // scannt Kanalset
    CALLING,    // sendet Call-Frame         (CallingPhase aktiv)
    HANDSHAKE,  // empfängt/sendet Handshake  (HandshakePhase aktiv, DD-014)
    LINKED,     // Verbindung hergestellt     (LinkedPhase aktiv)
    SOUNDING,   // Sounding-Frame wird gesendet
    ERROR,      // interner Fehlerzustand
};

enum class LinkedPhase : uint8_t {
    ACTIVE,      // Link hergestellt, Twa-Timer aktiv, Lautsprecher ein (REQ-LINK-020)
    ORDERWIRE,   // Message-Abschnitt nach Link-Aufbau (AMD/DTM/DBM, REQ-FRAME-004)
    TERMINATING, // TWAS wird gesendet oder ist empfangen (REQ-LINK-021/022/023)
};
```

`ALEStateMachine::current_state_` (ALEState) bestimmt die aktive Protokollebene.
Jeder mit Inner States versehene Outer State hat ein dediziertes Feld; das Feld
ist außerhalb seines Outer States bedeutungslos:

| Outer State | Feld | Typ | Ref |
|---|---|---|---|
| `CALLING` | `calling_phase_` | `CallingPhase` | — |
| `HANDSHAKE` | `handshake_phase_` | `HandshakePhase` | DD-014 |
| `LINKED` | `linked_phase_` | `LinkedPhase` | — |

**Implementierungsübersicht — Zustandsbaum:**
```
ALEState
├── IDLE  (= AVAILABLE)                          ✓ implementiert
├── SCANNING                                     ✓ implementiert
├── CALLING                                      ✓ implementiert
│   ├── CallingPhase::SCANNING_CALL              ✓ implementiert
│   ├── CallingPhase::LEADING_CALL               ✓ implementiert
│   ├── CallingPhase::CONCLUSION                 ✓ implementiert
│   ├── CallingPhase::LISTENING                  ✓ implementiert
│   ├── CallingPhase::MESSAGE                    ✗ fehlt  (OPEN-16, OPEN-17)
│   └── CallingPhase::NET_CALL_STUB              ✗ Platzhalter  (A.5.5.5.x)
├── HANDSHAKE                                    ✗ fehlt komplett  (DD-014)
│   ├── HandshakePhase::READING_CALL             ✗ fehlt  (FEAT-LINK-002)
│   ├── HandshakePhase::CHANNEL_CHECK            ✗ fehlt  (FEAT-LINK-002)
│   ├── HandshakePhase::SENDING_RESPONSE         ✗ fehlt  (FEAT-LINK-002)
│   ├── HandshakePhase::WAITING_FOR_ACK          ✗ fehlt  (FEAT-LINK-002)
│   ├── HandshakePhase::READING_ACK              ✗ fehlt  (FEAT-LINK-002)
│   ├── HandshakePhase::READING_RESPONSE         ✗ fehlt  (FEAT-LINK-003)
│   └── HandshakePhase::SENDING_ACK              ✗ fehlt  (FEAT-LINK-003)
├── LINKED                                       △ Outer State vorhanden; LinkedPhase fehlt
│   ├── LinkedPhase::ACTIVE                      ✗ fehlt  (FEAT-LINK-003)
│   ├── LinkedPhase::ORDERWIRE                   ✗ fehlt  (FEAT-FRAME-004)
│   └── LinkedPhase::TERMINATING                ✗ fehlt  (FEAT-LINK-003)
├── SOUNDING                                     ? Status unklar  (FEAT-SOUND-001/002)
└── ERROR                                        ? In Enum deklariert; Verhalten undefiniert
```

**Implementierungsabweichungen gegenüber dem Entwurf:**
- `LISTENING_FOR_RESPONSE` ist kein separater Outer State, sondern als
  `CallingPhase::LISTENING` als Inner State innerhalb von `ALEState::CALLING`
  modelliert. Funktional äquivalent, da Antworten nur in dieser Kombination
  empfangen werden können.
- `HANDSHAKE` (Outer State) und alle `HandshakePhase`-Inner States sind noch
  nicht implementiert — folgen mit FEAT-LINK-002 (JOE-Seite: READING_CALL bis
  READING_ACK) und FEAT-LINK-003 (SAM-Seite: READING_RESPONSE, SENDING_ACK).
- `MESSAGE` (Inner State von `CallingPhase`) ist noch nicht implementiert
  (OPEN-16, OPEN-17). Message ist optional; der Calling-Cycle-Kern
  (SCANNING_CALL→LEADING_CALL→CONCLUSION) ist vollständig.
- `LINKED::ORDERWIRE` ist noch nicht implementiert — folgt mit FEAT-FRAME-004.
  `LINKED::ACTIVE` und `LINKED::TERMINATING` folgen mit FEAT-LINK-003.
- `NET_CALL_STUB` ist Platzhalter für das Netz-Rufprotokoll (A.5.5.x).

**Phasenwechsel-Regel (DD-013):** Ein Wechsel des Inner State erfolgt
ausschließlich in `on_word_complete()`, niemals innerhalb eines laufenden
Trw-Slots. `call_cycles_in_phase` wird beim Phasenwechsel auf 0
zurückgesetzt; `call_cycle_count` läuft ohne Reset weiter (DD-006).

**Transition T-03 — Handshake-Abschluss (`CALLING` → `HANDSHAKE` → `LINKED`):**
T-03 bezeichnet die gesamte Übergangssequenz vom Ende des Call-Frames bis zum
Eintritt in `LINKED::ACTIVE`. Sie umfasst zwei Outer-State-Wechsel:

```
SAM: CALLING::LISTENING
       → HANDSHAKE::READING_RESPONSE   (liest JOEs Response)
       → HANDSHAKE::SENDING_ACK        (sendet ACK-Frame)
       → LINKED::ACTIVE

JOE: HANDSHAKE::READING_ACK           (liest SAMs ACK)
       → LINKED::ACTIVE               (nach Tlww)
```

Der Eintritt in `LINKED::ACTIVE` löst für beide Rollen aus: Lautsprecher
einschalten, Operator/Controller benachrichtigen, `ActivityTimer (Twa)` starten
(REQ-LINK-020, AC-LINK-020-1, AC-LINK-020-6). Details zu allen HANDSHAKE-Phasen
und Abbruchbedingungen: DD-014.

**Nebenläufiger RX-Pfad und Preemption-Regel:**
Der RX-Pfad läuft nebenläufig zum SM-Ablauf. Empfangene Wörter werden über
`ALEStateMachine::process_received_word(word)` injiziert. Geltende Regeln:

1. **Normale Transitionen:** State-Transitionen durch RX-Events erfolgen nur
   an Trw-Grenzen (DD-006, DD-013).
2. **Kollisions-Preemption (Sofort-Ausnahme):** Wird ein gültiges Wort mit
   abweichender Wortphase empfangen (≠ erwartete Trw-Grid-Position), ist der
   laufende Frame **sofort** zu verwerfen und das neue Signal als neuen Frame
   zu verarbeiten (REQ-LINK-024, AC-LINK-024-5/6). Diese Preemption erfolgt
   **ohne** Warten auf die nächste Trw-Grenze.
3. **Priorität:** Kollisions-Preemption hat Vorrang vor allen anderen RX-Events.

**Rückkehrzustand nach Timeout (`previous_outer_state_`):**
`ALEStateMachine` hält ein `previous_outer_state_`-Feld (`ALEState`), das beim
Eintritt in `CALLING` und `HANDSHAKE` gesetzt wird. Timeouts und Abbrüche
kehren zum gespeicherten Zustand zurück:

| Situation | Timeout | Rückkehrziel |
|---|---|---|
| Scanning Call, weiterer Kanal verfügbar | Twr / Twrt | `IDLE` → nächster Kanal |
| Scanning Call, alle Kanäle erschöpft | Twr / Twrt | `IDLE` |
| Abbruch: ungültige Präambel-Sequenz | sofort | `previous_outer_state_` |
| Abbruch: Kollision erkannt | sofort | `IDLE` |
| Kein Conclusion-Start innerhalb Twce | Twce | `previous_outer_state_` |
| Activity-Timeout im `LINKED`-Zustand | Twa | `IDLE` |

`previous_outer_state_` wird beim Eintritt in `CALLING` auf den aktuellen
Outer State gesetzt. Standard-Startwert ist `IDLE`.

**Alternativen:**
- Einebenen-Automat mit flachen Zuständen pro Phase — abgelehnt, weil
  Link-Zustände (LINKED, LISTENING_FOR_RESPONSE) und Frame-Phasen
  orthogonale Konzepte sind und ein flacher Automat deren Kombination
  nicht sauber abbildet.
- Phasenwechsel auf Tw-Ebene — abgelehnt, bricht das Trw-Grid (DD-006)
  und widerspricht DD-013.

---

### DD-010 — Timing-Tabelle A-XV: Alle Timer als benannte Konstanten

**Betrifft Features:** FEAT-LINK-001–004, FEAT-SOUND-001/002
**Status:** akzeptiert

**Entscheidung:** Alle Timing-Werte aus Tabelle A-XV werden als `constexpr` in `ALETimingConstants` (in `ale_state_machine.h`) abgelegt. Keine Magic Numbers im Code. Alle Timer-Berechnungen referenzieren diese Konstanten.

```cpp
namespace ALETimingConstants {
  constexpr uint32_t Tw_ms        = 130;   // 130,66... ms, ACHTUNG: abgeschnitten (nicht gerundet)
  constexpr uint32_t Trw_ms       = 392;   // 3 × Tw exakt = 49 symbols × 8 ms; NICHT 3×Tw_ms!
  constexpr uint32_t Tlrw_ms      = 784;   // 2 × Trw
  constexpr uint32_t Ts_max_ms    = 50000; // 50 s
  constexpr uint32_t Tm_max_ms    = 11760; // 11,76 s
  constexpr uint32_t Tx_max_ms    = 1960;  // 5 Wörter
  constexpr uint32_t Twr_ms       = 915;   // 7·Tw = 914,66...ms (slower equipment, maximum)
  constexpr uint32_t Twrt_ms      = 1960;  // Twr + Tt (default Tt=8Tw)
  constexpr uint32_t Twa_ms       = 30000; // 30 s Activity-Timeout
  constexpr uint32_t Tlww_ms      = 392;   // = Trw
  constexpr uint32_t Twce_ms      = 1960;  // FEHLER: Fixwert; Standard: 2 × eigene Ts (dynamisch)
  constexpr uint32_t Twt_ms       = 2000;  // Listen-Before-Transmit Standard
  constexpr uint32_t Twt_ale_ms   = 784;   // Listen-Before-Transmit ALE-only
  constexpr uint32_t Tc_max_ms	  = 4704;  // Maximum Call time 12 * Trw_ms
}
// <!-- FEHLER: Tw_ms = 130 ist abgeschnitten, nicht gerundet (Rundung → 131). Exakter Wert:
//      130,66...ms = 392/3 ms. ACHTUNG: 3 × Tw_ms = 390 ≠ Trw_ms = 392. Trw nie aus Tw_ms
//      berechnen — immer Trw_ms direkt verwenden. Quelle: Table A-XV / Annex B MIL-STD-188-141B -->
// <!-- FEHLER: Twr_ms = 915 entspricht 7·Tw = 914,66...ms (slower equipment, maximum lt. Annex B).
//      Für schnelle Geräte gilt 5·Tw = 653ms. Quelle: Table A-XV / Annex B -->
// <!-- FEHLER: Twce_ms = 1960 ist ein willkürlicher Fixwert ohne Basis im Standard. Standard:
//      Twce = 2 × eigene Ts (dynamisch). Beispiel 10 Kanäle × 200ms = Ts=2000ms → Twce=4000ms.
//      Laufzeit-Berechnung aus eigener Scan-Periode erforderlich. Quelle: Table A-XV / Annex B -->
```

---

### DD-011 — ChannelSelector: Eigenständiges Modul, nicht Teil der State Machine

**Betrifft Features:** FEAT-CHAN-001–006
**Status:** akzeptiert

**Kontext:** Im alten Design waren alle CHAN-Features in `Protocol/ale_state_machine.cpp` — ein 3000-Zeilen-File. Das macht Tests und Isolation unmöglich.

**Entscheidung:** Neues Modul `include/Protocol/ale_channel_selector.h` / `src/Protocol/ale_channel_selector.cpp`. Die `ALEStateMachine` ruft `ChannelSelector::select_best_channel()` auf und übergibt das Ergebnis an den Radio-Controller. LQA-Messungen werden von `ChannelSelector` empfangen und in `LQAStore` (via `AleDataStore`) geschrieben.

---

### DD-012 — Slotted Response: Tswt-Formel exakt aus A.5.5.4.1.3

**Betrifft Features:** FEAT-LINK-005/006
**Status:** akzeptiert

**Entscheidung:** Die exakte Formel wird implementiert (kein vereinfachtes Uniform-Schema):
```
Tswt(SN) = SN × [5·Tw + 2·Ta(caller) + (opt)Tm]
           + Ta(caller)
           + Σ Ta(m)(called) für m=1..SN-1
```
`Ta(x)` = `words_for_address(x) × Trw`. Uniform-Fall (alle Adressen 1 Wort, kein Message): `Tswt(SN) = SN × 14·Tw`. Beide Pfade implementieren, Uniform als Spezialfall verifizieren.

---

### DD-013 — Redundanzebene: State Machine arbeitet ausschließlich in Trw-Slots

**Betrifft Features:** FEAT-FRAME-001, FEAT-FRAME-002, FEAT-FRAME-003,
                       FEAT-FRAME-005, FEAT-SYNC-001, FEAT-LINK-001–004
**Status:** akzeptiert · **Implementiert in:** `ale_state_machine.cpp`, `ale2g_modem.cpp`

**Kontext:** Der Standard definiert das redundante Wort (Trw = 3 × Tw = 392 ms)
als unteilbare Übertragungseinheit. Ein Frame besteht aus „fortlaufenden
redundanten Wörtern" (REQ-FRAME-001). Die Frage war, auf welcher Ebene die
dreifache Wiederholung eines Worts implementiert wird und welche Schicht davon
wissen darf.

**Entscheidung:** Die dreifache Redundanz liegt vollständig und ausschließlich
in `ALE2GModem`. Kein Code außerhalb von `ALE2GModem` kennt oder implementiert
die Zahl 3.

Die `ALEStateMachine` arbeitet ausschließlich in Trw-Slots:

- Ein Aufruf von `transmit_word(word)` entspricht immer einer vollständigen
  Trw-Übertragung (3 × dasselbe Wort, intern). `words_pending` wird dabei
  um 1 erhöht und hält `handle_calling()` solange inaktiv.
- `call_cycle_count` zählt Trw-Slots, niemals Tw-Schritte.
- Alle Timing-Berechnungen verwenden Trw als Basiseinheit:

```
Tc    = wpa × Trw          (wpa = words_for_address())
Tlc   = 2 × Tc
Tsc   = C × 2 × Trw        (C = target_scan_channels)
Tcc   = Tsc + Tlc
```

`ALE2GModem` sendet nach Empfang eines `enqueue_word()`-Aufrufs exakt
3 × dasselbe Symbol-Paket und ruft danach `done_cb_()` auf, das die
Integration layer auf `sm.on_word_complete()` verdrahtet.
`on_word_complete()` erhöht `call_cycle_count` um 1, erhöht
`call_cycles_in_phase` um 1 und entscheidet über Phasenwechsel.

**ALE2GModem Word-Queue:** Multi-Wort-Adressen (z. B. für eine 4-Zeichen-
Adresse: TO + DATA = 2 Wörter) führen zu mehrfachen `transmit_word()`-Aufrufen
in einem einzigen `handle_calling()`-Durchlauf. `ALE2GModem::enqueue_word()`
stellt dabei überlaufende Wörter in eine interne Queue; jedes Wort wird
einzeln durch `done_cb_` quittiert. `words_pending` in `ALEStateMachine`
stellt sicher, dass `handle_calling()` nicht die nächste Sequenz startet,
bevor die aktuelle vollständig abgearbeitet ist.

**Verdrahtung (zwingend durch Integration Layer herzustellen):**
```cpp
modem.set_word_done_callback([&sm]{ sm.on_word_complete(); });
sm.set_transmit_callback([&modem](const ALEWord& w){ modem.enqueue_word(w); });
```

**Alternativen:** 3-fache Wiederholung in der State Machine (z.B. als
Schleife oder separater Zähler) — abgelehnt, weil jede Phase (Scanning,
Leading, Conclusion, Sounding) die Logik separat implementieren müsste und
die Timing-Formeln dann in Tw statt Trw rechnen würden, was zu Inkonsistenzen
mit dem Standard führt.

**Konsequenzen:**
- `ALEStateMachine`: kennt nur `transmit_word()` und `on_word_complete()`.
- `ALE2GModem`: einzige Stelle im Codebase mit dem Wissen über 3×
  Redundanz (`SYMBOL_REPETITION = 3` aus `symbol_decoder.h`).
- `words_pending`-Zähler in `ALEStateMachine` sichert Invariante:
  kein neuer Slot startet, solange Wörter noch in der Modem-Queue sind.
- Timing-Tests prüfen Trw-Grenzen, nicht Tw-Grenzen.
- `ALETimingConstants::Trw_ms` ist die kleinste Zeiteinheit, die die
  State Machine kennt.

---

### DD-014 — One-to-One Calling: HandshakePhase und vollständige Zustandsfolgen (A.5.5.3)

**Betrifft Features:** FEAT-LINK-001–003
**Status:** akzeptiert · **Implementiert in:** `ale_state_machine.h/.cpp` (geplant)

**Kontext:** Der Three-Way-Handshake (Call → Response → ACK, A.5.5.3) ist
asymmetrisch: Die rufende Station (SAM) und die gerufene Station (JOE)
durchlaufen unterschiedliche Zustandsfolgen. DD-009 definiert `HANDSHAKE` als
Outer State, lässt dessen Inner States aber offen. Dieser DD präzisiert die
Inner States für `HANDSHAKE` und legt die vollständigen Zustandsfolgen beider
Rollen mit allen Abbruchbedingungen fest.

**Entscheidung:** `enum class HandshakePhase` als Inner States für `HANDSHAKE`:

| Phase | Rolle | Beschreibung |
|---|---|---|
| `READING_CALL` | JOE | Liest SAMs Call-Frame; Twce-Timer läuft |
| `CHANNEL_CHECK` | JOE | 2 × Trw LBT auf Kanal vor Response (REQ-LINK-019) |
| `SENDING_RESPONSE` | JOE | Sendet Response-Frame (TO SAM … TIS JOE); startet Twr |
| `WAITING_FOR_ACK` | JOE | Wartet Twr auf SAMs ACK-Start ("TO JOE") |
| `READING_ACK` | JOE | Liest SAMs ACK-Frame; prüft Conclusion |
| `READING_RESPONSE` | SAM | Liest JOEs Response-Frame; prüft Conclusion |
| `SENDING_ACK` | SAM | Sendet ACK-Frame (TO JOE … TIS SAM) |

```cpp
enum class HandshakePhase : uint8_t {
    READING_CALL,      // JOE: SAMs Call-Frame lesen (Twce-Timer aktiv)
    CHANNEL_CHECK,     // JOE: 2×Trw LBT bevor Response senden
    SENDING_RESPONSE,  // JOE: Response-Frame senden, Twr starten
    WAITING_FOR_ACK,   // JOE: Twr auf SAMs ACK-Start warten
    READING_ACK,       // JOE: SAMs ACK-Frame lesen
    READING_RESPONSE,  // SAM: JOEs Response-Frame lesen
    SENDING_ACK,       // SAM: ACK-Frame senden
};
```

`ALEStateMachine::handshake_phase_` (HandshakePhase) gibt an, welche
Handshake-Rolle und welchen Schritt die Station gerade ausführt. Das Feld ist
außerhalb von `HANDSHAKE` bedeutungslos.

**Vollständige Zustandsfolge SAM — Caller (A.5.5.3.1, A.5.5.3.3, A.5.5.3.4):**

```
AVAILABLE
  → [initiate_call()] + LBT Twt
  → CALLING::SCANNING_CALL    (TO first_word × (C × 2 Trw), C = JOEs Kanalanzahl)
  → CALLING::LEADING_CALL     (vollständige Adresse × 2; TO + DATA/REP)
  → CALLING::MESSAGE          (optional: AMD/DTM/DBM, ≤ Tmmax)
  → CALLING::CONCLUSION       (TIS SAM, ≤ Txmax)
  → CALLING::LISTENING        (wartet Twr (1-Kanal) / Twrt (Multi-Kanal))
       │
       ├─ Timeout → nächster Kanal oder IDLE  (DD-009: Rückkehrziel-Tabelle)
       │
       └─ "TO SAM" innerhalb Twr/Twrt
          → HANDSHAKE::READING_RESPONSE
               │
               ├─ Abbruchbedingung (s.u.) → nächster Kanal / IDLE
               ├─ "TWAS JOE" → IDLE, Operator benachrichtigen (abgelehnt)
               │
               └─ "TIS JOE" + Tlww
                  → HANDSHAKE::SENDING_ACK  (TO JOE … TIS SAM)
                       └─ ACK gesendet → LINKED::ACTIVE
```

**Vollständige Zustandsfolge JOE — Called (A.5.5.3.2, A.5.5.3.3, A.5.5.3.4):**

```
AVAILABLE / SCANNING
  → "TO JOE" empfangen (Scan stoppen)
  → HANDSHAKE::READING_CALL    (SAMs Call-Frame lesen; Twce-Timer)
       │
       ├─ Abbruchbedingung (s.u.) → previous_outer_state_
       ├─ "TWAS SAM" → previous_outer_state_ (kein Response)
       │
       └─ "TIS SAM" + Tlww
          → HANDSHAKE::CHANNEL_CHECK  (2 × Trw LBT)
               │
               ├─ Kanal belegt → previous_outer_state_
               │
               └─ Kanal frei
                  → HANDSHAKE::SENDING_RESPONSE  (TO SAM … TIS JOE; Twr starten)
                       └─ Response gesendet
                          → HANDSHAKE::WAITING_FOR_ACK  (Twr-Timer)
                               │
                               ├─ Timeout → previous_outer_state_
                               │
                               └─ "TO JOE" innerhalb Twr
                                  → HANDSHAKE::READING_ACK
                                       │
                                       ├─ Abbruchbedingung (s.u.) → previous_outer_state_
                                       ├─ "TWAS SAM" → previous_outer_state_
                                       │
                                       └─ "TIS SAM" + Tlww → LINKED::ACTIVE
```

**Abbruchbedingungen `HANDSHAKE::READING_CALL` (JOE liest SAMs Call):**

| Bedingung | Referenz |
|---|---|
| Kein Message-/Conclusion-Start innerhalb Twce | AC-LINK-018-5 |
| Kein Conclusion-Start innerhalb Tmmax nach Message-Beginn | AC-LINK-018-5 |
| Ungültige Preamble-Sequenz (Ausnahme: ≤ 3 Fehler im Scanning-Call toleriert) | AC-LINK-018-6 |
| Conclusion-Ende nicht innerhalb Tlww (+ n × Trw je ext. Adresswort) | AC-LINK-018-7 |

**Abbruchbedingungen `HANDSHAKE::READING_RESPONSE` (SAM) und
`HANDSHAKE::READING_ACK` (JOE):**

| Bedingung | Referenz |
|---|---|
| Kein Frame-Start ("TO own") innerhalb Twr/Twrt | AC-LINK-019-6, AC-LINK-020-2 |
| Ungültige Preamble-Sequenz | AC-LINK-019-7, AC-LINK-020-3 |
| Keine Conclusion innerhalb Tlc (+ Tmmax bei Message) | AC-LINK-019-8, AC-LINK-020-4 |
| Conclusion-Ende nicht innerhalb Tlww erkannt | AC-LINK-019-9, AC-LINK-020-5 |
| Conclusion = TWAS (Ablehnung) | AC-LINK-019-10, AC-LINK-020-7 |

**`previous_outer_state_` in HANDSHAKE:** Beim Eintritt setzt JOE
`previous_outer_state_` auf den aktuellen Outer State (typisch `AVAILABLE`,
aber auch `LINKED` wenn bereits verlinkt — A.5.5.3.2: "return to its previous
state … linked if it was linked to another station").

**ACK-Frame-Inhalt:** Das ACK (A.5.5.3.4, Figure A-31) ist strukturell identisch
zu einem Single-Channel-Individual-Call von SAM an JOE:
`TO JOE, TO JOE, TIS SAM`. JOE sendet keine erneute Response, weil das ACK
innerhalb des Twr-Fensters nach JOEs Response eintrifft (NOTE 1 in A.5.5.3.4).
Kommt das ACK nach Ablauf von Twr, behandelt JOE es als neuen Ruf.

**Implementierungsabweichungen:**
`HandshakePhase` ist noch nicht implementiert. Alle HANDSHAKE-Phasen sind geplant
in FEAT-LINK-002 (JOEs Seite) und FEAT-LINK-003 (SAMs ACK-Sendung).

**Alternativen:**
- SENDING_RESPONSE / SENDING_ACK als CALLING-Phasen modellieren — abgelehnt,
  semantische Rolle und `previous_outer_state_`-Kontext müssen erhalten bleiben.
- Separate Outer States `RESPONDING` / `ACKNOWLEDGING` — abgelehnt,
  übermäßige Granularität; HandshakePhase disambiguiert SAM- und JOE-Rolle.

---

## 4. Schnittstellen

### 4.1 Datenfluss TX

```
ALEWord {type, address[3], valid}
   │
   ▼  ALE2GModem::enqueue_word(word)
   │     intern: word.encode()  →  ALEFECCodec::interleave_word()
uint64_t tx49  (49 interleaved Bits: A₁B₁A₂B₂…A₂₄B₂₄ S₄₉)
   │
   ▼  ALE2GModem::build_symbols()  [mod-49 zyklisches Packing, MSB-first]
std::array<uint8_t, 49> symbol_buf_  (Symbolwerte 0..7; symbol[k]=word_bits[3k..3k+2] MSB-first mod 49)
   │
   ▼  ToneGenerator::generate_symbols(49 Symbole, 3136 Samples)
int16_t[3136] pcm_word  (8 kHz, 392 ms = Trw, Amplitude 0.7f)
   │
   ▼  tx_callback  (z.B. WAV-Schreiber oder WasapiAudioDriver)
PCM-Ausgabe
```

### 4.2 Datenfluss RX

```
float[buffer_frames] pcm_device  (8 kHz, Eingang)
   │
   ▼  FFTBuffer::push_sample()  [Sliding-FFT, 64-Punkt]
std::array<float, 64> fft_mag  (Magnitudenspektrum)
   │  (pro Symbol-Periode = 64 Samples)
   ▼  SymbolDecoder: Peak-Bin → FREQ_TO_SYMBOL-Lookup
uint8_t symbol  (0..7)
   │  (nach 49 Symbolen = 1 Wort-Periode Trw = 392 ms)
   ▼  Bit-Extraktion MSB-first: stream[i] = (symbol[i/3] >> (2 - i%3)) & 1
   │  + Stride-49-Majority-Vote über bit_stream[i], [i+49], [i+98]
uint64_t tx49_voted  (48 gevotete Datenbits + S₄₉=0)
   │
   ▼  ALEFECCodec::deinterleave_word(tx49_voted)  [Deinterleave + Golay-Decode]
uint32_t word24
   │
   ▼  ALEWord::from_word24(word24)
ALEWord {type, address, valid, fec_errors}
   │
   ▼  ALEStateMachine::on_word_received(word)
ALE-Protokoll (HANDSHAKE / LINK_ESTABLISHED / ...)
```

### 4.3 Schnittstellen-Verträge

| Schnittstelle | Eingabe | Ausgabe | Thread-Safety |
|---|---|---|---|
| `ALEFECCodec::interleave_word` | `GolayCoded{coder_a, coder_b}` | `uint64_t tx49` | Thread-safe (stateless) |
| `ALEFECCodec::deinterleave_word` | `uint64_t tx49_voted` | `uint32_t word24` | Thread-safe (stateless) |
| `ALE2GModem::enqueue_word` | `const ALEWord&` | — (async via tx_cb) | Nur Main-Thread |
| `ALE2GModem::update` | `uint32_t time_ms` | — | Nur Main-Thread |
| `SymbolDecoder::decode_word_with_voting` | `WordVoteBuffer` (147 Symbole) | `uint64_t tx49_voted` | RX-Thread |
| `ALE2GModem::detect_symbol` | `const float*`, `size_t` | `uint8_t` | RT-Thread |
| `ALEStateMachine::update` | `uint32_t time_ms` | callbacks | Main-Thread only |
| `ChannelSelector::select_best_channel` | `const std::string& target_addr` | `uint8_t channel_index` | Main-Thread only |
| `ChannelSelector::update_lqa` | `const LQAMeasurement&` | — | Main-Thread only |
| `AleDataStore::channel_store` | — | `ChannelStore&` | Thread-safe (mutex intern) |
| `WasapiAudioDriver::set_audio_callback` | `AudioCallback` | — | Thread-safe (mutex intern) |

---

## 5. Feature-Detailbeschreibungen

---

### FEAT-GEN-001 — ALE Data Link Schichtstruktur & Grundbetrieb

**Setzt um:** REQ-GEN-001, REQ-GEN-002, REQ-GEN-003, REQ-GEN-004, REQ-GEN-005
**Modul:** `include/Protocol/ale_state_machine.h`, `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009
**Status:** geplant

#### Beschreibung
Implementiert die drei ALE-Sublayer (FEC, LP, ALE) als logische Schichtgrenzen im Code, die Scanning-Stop-Bedingungen, und die Channel-Quality-Anzeigeskala (0–30 SINAD). Die Sublayer existieren bereits als separate Module — dieses Feature dokumentiert und verifiziert die Grenzkonformität.

#### Technischer Entwurf

```
FEC-Sublayer  = ALEFECCodec (src/FEC/golay.cpp + FEC/ale_fec_codec.cpp)
LP-Sublayer   = LinkingProtection — derzeit Placeholder, Level 0..3 (DD-009)
ALE-Sublayer  = ALEStateMachine (Protocol/ale_state_machine.cpp)
```

Scanning-Stop-Bedingungen (A.4.1.2):
```cpp
enum class ScanStopReason {
    CONTROLLER_DECISION,   // normaler Betrieb
    MANUAL_STOP,           // Operator-Eingriff
    EXTERNAL_STOP_SCAN,    // externes Signal (falls unterstützt)
};
void ALEStateMachine::stop_scan(ScanStopReason reason);
```

Channel-Quality-Anzeige (A.4.1.5): Skalierung 0–30, 31=unbekannt, Basis SINAD — delegiert an `ChannelSelector::get_display_quality(uint8_t channel_index)` → `uint8_t` (0–31).

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-001-1 | tests/test_gen_basics.cpp: FEC-Sublayer-Grenze | offen |
| AC-GEN-002-1 | tests/test_gen_basics.cpp: Adressierungsstruktur A.5.2.4 | offen |
| AC-GEN-003-2 | tests/test_gen_basics.cpp: Stop-Scan alle 3 Bedingungen | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_state_machine.h` | `ScanStopReason`, `stop_scan()` | neu anzulegen |
| `include/Protocol/ale_state_machine.h` | `ALESubLayer` enum | FEC / LP / ALE |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-001 — ALE Data Link: Schichtstruktur und Sublayer

**Spec-Referenz:** A.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine digitale ALE-Datenverbindung bilden, die aus einem robusten Modem und einer Vorwärtsfehlerkorrektur-Codierung besteht. Der ALE-Datenlinkschicht enthält drei Sublayer: den FEC-Sublayer (unterer Sublayer für Fehlerkorrektur und -erkennung), den LP-Sublayer (Linking-Protection-Sublayer in der Mitte) sowie den ALE-Sublayer (oberer Sublayer mit dem ALE-Protokoll für Link-Aufbau, Datenkommunikation und rudimentäres LQA).

**Akzeptanzkriterien:**
- `AC-GEN-001-1` — Das System muss einen FEC-Sublayer bereitstellen, der Redundanz, Majority-Voting, Interleaving und Golay-Codierung auf 24-Bit-ALE-Wörter anwendet.
- `AC-GEN-001-2` — Das System muss einen LP-Sublayer (Linking Protection) zwischen FEC- und ALE-Sublayer bereitstellen.
- `AC-GEN-001-3` — Das System muss einen ALE-Sublayer bereitstellen, der Protokolle für Link-Aufbau, Datenkommunikation und rudimentäres LQA enthält.

---

##### REQ-GEN-002 — ALE-Adressen: Adressierungsstruktur

**Spec-Referenz:** A.4.1.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Stationen müssen die in A.5.2.4 spezifizierte Adressierungsstruktur verwenden, um einzelne Stationen sowie Gruppen von Stationen (Netze und Gruppen) zu identifizieren.

**Akzeptanzkriterien:**
- `AC-GEN-002-1` — Das System muss individuelle Stationsadressen gemäß A.5.2.4 unterstützen.
- `AC-GEN-002-2` — Das System muss Netz- und Gruppenadressierung gemäß A.5.2.4 unterstützen.

---

##### REQ-GEN-003 — Scanning: Wiederholtes Durchlaufen gespeicherter Kanäle

**Spec-Referenz:** A.4.1.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss in der Lage sein, ausgewählte, im Speicher abgelegte Kanäle wiederholt zu scannen — sowohl unter manueller Steuerung als auch unter Führung eines angeschlossenen automatisierten Controllers. Das System muss den Scan stoppen und auf dem zuletzt besuchten Kanal verharren, wenn eines der folgenden wählbaren Ereignisse eintritt: automatische Controller-Entscheidung zum Stopp, manueller Stopp-Scan-Eingang, oder Aktivierung einer externen Stop-Scan-Leitung (sofern vorhanden). Zu scannende Kanäle sollen nach Gruppen (Scan-Listen) und innerhalb der Gruppen individuell auswählbar sein.

**Akzeptanzkriterien:**
- `AC-GEN-003-1` — Das System muss gespeicherte Kanäle wiederholt scannen können (unter manueller und automatisierter Steuerung).
- `AC-GEN-003-2` — Der Scan muss stoppen und auf dem letzten Kanal verharren, wenn die Controller-Entscheidung, ein manueller Eingang oder eine externe Stop-Scan-Leitung aktiv werden.
- `AC-GEN-003-3` — Kanäle müssen gruppenweise und einzeln innerhalb von Gruppen (Scan-Listen) auswählbar sein.

---

##### REQ-GEN-004 — Calling: Ausführung des Rufprotokolls auf Anforderung

**Spec-Referenz:** A.4.1.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss auf Anforderung des Operators oder eines externen automatisierten Controllers das in A.5.5 spezifizierte Rufprotokoll ausführen.

**Akzeptanzkriterien:**
- `AC-GEN-004-1` — Das System muss das Rufprotokoll gemäß A.5.5 auf Operator- oder Controller-Anforderung ausführen.

---

##### REQ-GEN-005 — Kanalbewertung und Kanalqualitätsanzeige

**Spec-Referenz:** A.4.1.4, A.4.1.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss in der Lage sein, automatisch ALE-Sounding-Übertragungen gemäß A.5.3 auszusenden und die Signalqualität von ALE-Empfängen automatisch gemäß A.5.4.1 zu messen. Sofern eine Bedienerdisplay-Funktion bereitgestellt wird, muss die Anzeige eine einheitliche Skala von 0 bis 30 (mit 31 = unbekannt) verwenden, die auf SINAD (Signal-plus-Rauschen-plus-Verzerrung zu Rauschen-plus-Verzerrung) basiert.

**Akzeptanzkriterien:**
- `AC-GEN-005-1` — Das System muss automatisch ALE-Sounding-Übertragungen gemäß A.5.3 senden.
- `AC-GEN-005-2` — Das System muss die Signalqualität von ALE-Empfängen automatisch gemäß A.5.4.1 messen.
- `AC-GEN-005-3` — Falls eine Anzeigeeinheit vorhanden ist, muss die Kanalqualitäts-Skala einheitlich 0–30 (31 = unbekannt) auf SINAD-Basis sein.

---

### FEAT-GEN-002 — Scan-Raten & AQC-Rückwärtskompatibilität

**Setzt um:** REQ-GEN-006, REQ-GEN-007, REQ-GEN-008
**Modul:** `include/Protocol/ale_state_machine.h`, `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-010
**Status:** geplant

#### Technischer Entwurf

```cpp
enum class ScanRate : uint8_t {
    TWO_PER_SECOND   = 2,
    FIVE_PER_SECOND  = 5,
    TEN_PER_SECOND   = 10,  // Design Objective, optional
};

// Dwell-Zeit in ms, berechnet aus ScanRate:
// 2 ch/s → Td = 500 ms, 5 ch/s → Td = 200 ms, 10 ch/s → Td = 100 ms
uint32_t dwell_time_ms(ScanRate r);

// AQC: variable dwell rates — nur wenn AQC aktiv
struct ScanConfig {
    ScanRate  rate;
    bool      aqc_variable_dwell;   // REQ-GEN-007
};
void ALEStateMachine::set_scan_config(const ScanConfig&);
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-006-1 | tests/test_gen_scanning.cpp: 2 ch/s → Td=500ms | offen |
| AC-GEN-006-2 | tests/test_gen_scanning.cpp: 5 ch/s → Td=200ms | offen |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-006 — Scan-Rate: Selektierbare Werte 2 und 5 Kanäle/s

**Spec-Referenz:** A.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Stationen müssen selektierbare Scan-Raten von zwei und fünf Kanälen pro Sekunde unterstützen. Andere Scan-Raten (Design Objective: 10 Kanäle/s) können zusätzlich implementiert werden.

**Akzeptanzkriterien:**
- `AC-GEN-006-1` — Das System muss eine Scan-Rate von 2 Kanälen/s unterstützen.
- `AC-GEN-006-2` — Das System muss eine Scan-Rate von 5 Kanälen/s unterstützen.

---

##### REQ-GEN-007 — AQC-ALE Scan-Rate: Variable Verweilzeiten (NT)

**Spec-Referenz:** A.4.2.1.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Im optionalen AQC-ALE-Protokoll muss das System variable Verweilzeiten während des Scannens unterstützen, sodass Datenverkehr gemäß Tabelle A-II (Linking-Wahrscheinlichkeit) erkannt werden kann.

**Akzeptanzkriterien:**
- `AC-GEN-007-1` — Das System muss im AQC-ALE-Modus variable Verweilzeiten unterstützen.
- `AC-GEN-007-2` — Die Erkennungswahrscheinlichkeit für Datenverkehr muss den Werten aus Tabelle A-II entsprechen.

---

##### REQ-GEN-008 — AQC-ALE Rückwärtskompatibilität: Scan-Raten 2 und 5 Kanäle/s

**Spec-Referenz:** A.4.2.1.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Funkgeräte mit dem optionalen AQC-ALE sollen Scan-Raten von 2 und 5 Kanälen pro Sekunde für Rückwärtskompatibilität mit Nicht-AQC-ALE-Netzen bereitstellen.

**Akzeptanzkriterien:**
- `AC-GEN-008-1` — AQC-ALE-fähige Geräte sollen Scan-Raten von 2 und 5 Kanälen/s unterstützen.

---

### FEAT-GEN-003 — Belegtheitserkennung & Linking-Wahrscheinlichkeit (System-Tests)

**Setzt um:** REQ-GEN-009, REQ-GEN-010, REQ-GEN-011, REQ-GEN-012
**Modul:** `tests/test_gen_performance.cpp`
**Status:** geplant

#### Beschreibung
Dieses Feature ist ein **Verifikations-Feature**: Es enthält keine neuen Produktions-Code-Dateien, sondern System-Tests, die die Anforderungen der Tabellen A-I und A-II messen. Tabelle A-I (Occupancy Detection) und Tabelle A-II (Linking Probability) werden mit einem AWGN-Kanal-Simulator verifiziert.

#### Technischer Entwurf

```cpp
// Kanal-Simulator (AWGN ohne Fading, nur für Tests)
struct AWGNChannel {
    float snr_db;
    void add_noise(float* samples, size_t count);
};

// Performance-Test: Occupancy Detection (Tabelle A-I)
// Sendet 1000 Frames mit SNR=0dB, misst false_alarm_rate
TEST(OccupancyDetection, ALE_SNR0dB_DetectionProb80Percent) {
    AWGNChannel ch{0.0f};
    // Simuliere 1000 ALE-Frames + 1000 Noise-only-Frames
    // Erwarte: P_detect >= 0.80, false_alarm <= 0.01
}
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-009 — Belegtheitserkennung (NT): Mindestwahrscheinlichkeiten gemäß Tabelle A-I

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

##### REQ-GEN-010 — Linking-Wahrscheinlichkeit: Mindestanforderungen gemäß Tabelle A-II

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

##### REQ-GEN-011 — AQC-ALE Linking-Wahrscheinlichkeit (NT)

**Spec-Referenz:** A.4.2.3.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Bei implementiertem optionalen AQC-ALE-Protokoll muss die Linking-Wahrscheinlichkeit den Werten aus Tabelle A-II entsprechen, mit folgenden zusätzlichen Kriterien: Protokoll ist das AQC-Individual-Calling-Protokoll ohne Nachrichtenübertragung; Adressen haben 1 bis 6 Zeichen aus dem 38-Zeichen-Basic-ASCII-Subset; gerufene Einheiten scannen 10 Kanäle; Rufinitiierung mit gestopptem und auf die Ruffrequenz abgestimmtem Sender; der initiale Call-Probe darf nicht mehr als 10 Trw, die Call Response nicht mehr als 4 Trw und die Bestätigung nicht mehr als 2 Trw umfassen.

**Akzeptanzkriterien:**
- `AC-GEN-011-1` — Die AQC-ALE-Linking-Wahrscheinlichkeit muss den Tabelle-A-II-Werten entsprechen.
- `AC-GEN-011-2` — Call-Probe ≤ 10 Trw, Response ≤ 4 Trw, Bestätigung ≤ 2 Trw.

---

##### REQ-GEN-012 — AQC-ALE Linking-Leistung bei LP-Level 1 und 2

**Spec-Referenz:** A.4.2.3.2
**Priorität:** COULD · **Status:** offen

**Anforderung:** Die AQC-ALE Linking-Leistung darf durch LP-Level 1 oder 2 nicht verschlechtert werden. Scan-Raten von 2 oder 5 Kanälen/s können die Leistung verschlechtern, da während des Call-Probes möglicherweise nicht genügend redundante Wörter ausgestrahlt werden.

**Akzeptanzkriterien:**
- `AC-GEN-012-1` — Die AQC-ALE-Leistung darf bei LP-Level 1 oder 2 nicht abnehmen.

---

### FEAT-GEN-004 — Kanalspeicher (Channel Memory, A.4.3.1)

**Setzt um:** REQ-GEN-013
**Modul:** `include/Stores/ale_data_store.h`, `src/Stores/ale_data_store.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Datenstrukturen

```cpp
enum class AleMode : uint8_t { USB, LSB, AM };
enum class TRMode  : uint8_t { TX_RX, TX_ONLY, RX_ONLY };
enum class UseType : uint8_t { VOICE, DATA, VOICE_DATA };

struct ChannelEntry {
    uint32_t tx_freq_hz;
    uint32_t rx_freq_hz;
    AleMode  tx_mode;
    AleMode  rx_mode;
    TRMode   tr_mode;
    UseType  use;
    bool     scan_enabled;
    uint8_t  power_level;           // 0=LO, 1=HI
    bool     secure;                // SCTY: C=clear, S=secure, CS=both
    uint32_t sound_interval_ms;     // 0 = kein Sounding
    uint32_t next_sound_ms;         // Countdown
    // Design Objective Felder (optional):
    uint8_t  filter_width;
    uint8_t  agc_setting;
    uint8_t  antenna_port;
    uint8_t  info_port;
    bool     noise_blanker;
    // max 3 Sounding-Selbstadressen (DO):
    char     sound_self_addr[3][16];
    bool     valid;                 // false = leerer Slot
};

class ChannelStore {
public:
    static constexpr uint8_t CAPACITY = 100;

    bool     store(uint8_t index, const ChannelEntry& entry);
    bool     load(uint8_t index, ChannelEntry& out) const;
    // Recall gibt eine Kopie zurück — Original bleibt unverändert:
    ChannelEntry recall(uint8_t index) const;
    bool     recall_and_modify(uint8_t index, ChannelEntry& working_copy) const;
    uint8_t  count() const;
    bool     persist(IPersistenceBackend& backend) const;
    bool     restore(IPersistenceBackend& backend);
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-013-1 | tests/test_gen_data_store.cpp: capacity() >= 100 | offen |
| AC-GEN-013-2 | tests/test_gen_data_store.cpp: persist/restore Roundtrip | offen |
| AC-GEN-013-4 | tests/test_gen_data_store.cpp: recall_and_modify ändert nicht Original | offen |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-013 — Kanalspeicher: Mindestens 100 Kanaleinträge, nichtflüchtig

**Spec-Referenz:** A.4.3.1, Tabelle A-III
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gerät muss mindestens 100 verschiedene Kanalinformationssätze speichern, abrufen und verwenden können. Jeder Eintrag umfasst Sende- und Empfangsfrequenzen sowie zugehörige Modusinformationen (Sendeleistungspegel, Kanalnutzung, Sounding-Daten, Modulationstyp, Sende-/Empfangsmodus; optional: Filterbandbreite, AGC-Einstellung, Antennenport-Auswahl, Informationsport-Auswahl, Noise-Blanker-Einstellung, Sicherheitseinstellung, Sounding-Selbstadressen). Der Kanalspeicher muss nichtflüchtig sein. Jeder Kanal muss manuell oder per Controller abrufbar sein und darf nach dem Abruf ohne Veränderung des ursprünglich gespeicherten Eintrags modifiziert werden.

**Akzeptanzkriterien:**
- `AC-GEN-013-1` — Das Gerät muss mindestens 100 Kanaleinträge speichern können.
- `AC-GEN-013-2` — Der Kanalspeicher muss nichtflüchtig sein.
- `AC-GEN-013-3` — Jeder Kanal muss manuell oder per Controller abrufbar sein.
- `AC-GEN-013-4` — Nach dem Abruf darf der ursprüngliche gespeicherte Eintrag nicht verändert werden, auch wenn der abgerufene Eintrag modifiziert wird.

---

### FEAT-GEN-005 — Selbstadressspeicher (Self Address Memory, A.4.3.2)

**Setzt um:** REQ-GEN-014
**Modul:** `include/Stores/ale_data_store.h`, `src/Stores/ale_data_store.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Datenstrukturen

```cpp
struct SelfAddressEntry {
    char    self_addr[16];          // eigene Adresse (1-15 Zeichen + NUL)
    char    net_addr[16];           // Netzadresse (leer = kein Netz)
    uint8_t slot_number;            // SN für Tswt-Berechnung (0 = kein Netz)
    uint32_t slot_wait_time_tw;     // Tswt = Tsw × SN (in Tw-Einheiten)
    // Gültige Kanäle: Bitmaske oder Liste (max 100 Kanäle):
    uint8_t valid_channels[100 / 8 + 1]; // Bitmaske
    bool    valid;
};

class SelfAddressStore {
public:
    static constexpr uint8_t CAPACITY = 20;

    bool   store(uint8_t index, const SelfAddressEntry& entry);
    bool   load(uint8_t index, SelfAddressEntry& out) const;
    // Prüft ob eine empfangene Adresse zu einer Selbstadresse passt:
    bool   matches_any(const char* received_addr, SelfAddressEntry& matched) const;
    // Tswt-Berechnung:
    uint32_t slot_wait_time_ms(uint8_t sa_index) const;
    bool   persist(IPersistenceBackend&) const;
    bool   restore(IPersistenceBackend&);
};
```

**Slot-Wartezeit** (A.4.3.2): `Tswt(SN) = Tsw × SN`. Im Uniform-Fall gilt `Tsw = 14·Tw ≈ 1829 ms` (1-Wort-Adressen). Gespeichert als Vielfaches von Tw, berechnet in ms on demand.

<!-- FEHLER: Tsw = 14·Tw war als 5488 ms angegeben. Das ist 14·Trw = 14×392 ms (Faktor-3-Fehler).
     Korrekt: Tsw = 14·Tw = 14 × 130,66...ms ≈ 1829 ms. Quelle: Table A-XV / Annex B MIL-STD-188-141B -->

<!-- LÜCKE geschlossen (Quelle: Annex B MIL-STD-188-141B):
     Tsw hat drei normierte Varianten:
     - Standard-Antworten:  Tsw = 14·Tw ≈ 1829 ms
     - LQA-Antworten:       Tsw = 17·Tw ≈ 2221 ms
     - Tight-Slot-Antworten: Tsw = 9·Tw = 1176 ms
     Weitere Werte können per CMD definiert werden. Bei Implementierung von LQA-Replies
     (REQ-CHAN-016) muss Tsw = 17·Tw verwendet werden. -->

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-014 — Selbstadressspeicher: Mindestens 20 Einträge, nichtflüchtig

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

### FEAT-GEN-006 — Fremdstations-Tabelle & LQA-Speicher (A.4.3.3)

**Setzt um:** REQ-GEN-015, REQ-GEN-016, REQ-GEN-017, REQ-GEN-018
**Modul:** `include/Stores/ale_data_store.h`, `src/Stores/ale_data_store.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Datenstrukturen

```cpp
struct OtherStationEntry {
    char    addr[16];
    uint32_t twr_ms;        // individuelle Wartezeit (0 = Default)
    // Netz-Zuordnung:
    char    net_addr[16];
    uint8_t slot_number;
    uint32_t twrn_ms;
    uint8_t valid_channels[100 / 8 + 1];
    // DO: Antennen-Einstellungen:
    uint8_t antenna_selection;
    uint16_t antenna_azimuth;
    uint8_t power_limit;
    bool    valid;
};

struct LQACell {
    uint8_t  sinad_from;    // SINAD gemessen empfangsseitig (0-30, 31=unbekannt)
    uint8_t  sinad_to;      // SINAD von Fremdstation gemeldet (bilateral)
    uint8_t  ber_from;      // BER-Wert 0-30 (optional)
    uint8_t  ber_to;
    uint8_t  mp_from;       // Multipath 0-6ms (optional, 7=unbekannt)
    uint8_t  mp_to;
    uint32_t age_ms;        // Alter der Messung in ms seit letztem Update
    bool     valid;
};

// Adress/Kanal-Matrix: [other_station_index][channel_index]
class LQAStore {
public:
    static constexpr uint16_t CAPACITY     = 4000;
    static constexpr uint16_t CAPACITY_DO  = 10000;  // Design Objective
    static constexpr uint32_t POWER_DOWN_RETENTION_MS = 3600000; // 1h

    void     update(uint8_t station_idx, uint8_t channel_idx, const LQACell& cell);
    bool     get(uint8_t station_idx, uint8_t channel_idx, LQACell& out) const;
    // Zeitbasiertes Gewichts-Aging (A.4.3.3.2):
    void     apply_aging(uint32_t elapsed_ms);
    bool     persist(IPersistenceBackend&) const;
    bool     restore(IPersistenceBackend&);
};

class OtherStationStore {
public:
    static constexpr uint8_t CAPACITY = 100;

    bool     store(uint8_t index, const OtherStationEntry& entry);
    bool     load(uint8_t index, OtherStationEntry& out) const;
    // DO: auto-fill mit gehörten Adressen (LRU bei Kapazitätsüberschreitung):
    bool     auto_insert_heard(const char* addr, uint8_t channel);
    LQAStore lqa;
    bool     persist(IPersistenceBackend&) const;
    bool     restore(IPersistenceBackend&);
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-015 — Fremdstations-Tabelle: Mindestens 100 Einträge

**Spec-Referenz:** A.4.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Funksystem muss mindestens 100 verschiedene Informationssätze zu Adressen anderer Stationen und Netze, Kanalqualitätsdaten zu diesen Stationen (Messungen oder Prognosen) und gerätespezifische Einstellungen für Links zu jeder Station oder jedem Netz speichern, abrufen und verwenden können. Design Objective: Überschusskapazität, die nicht mit vorgeplanteren Fremdstations-Informationen belegt ist, soll automatisch mit auf gescannten oder überwachten Kanälen gehörten Adressen gefüllt werden; wenn die Kapazität erschöpft ist, sollen die ältesten gehörten Adressen durch die neuesten ersetzt werden. Diese Informationen sollen für Rufinitiierung und Aktivitätsbewertung verwendet werden.

**Akzeptanzkriterien:**
- `AC-GEN-015-1` — Das System muss mindestens 100 Fremdstations-Einträge speichern können.
- `AC-GEN-015-2` — Jeder Eintrag muss Adresse, Kanalqualitätsdaten und gerätespezifische Link-Einstellungen enthalten können.
- `AC-GEN-015-3` — DO (A.4.3.3): Freie Kapazität der Tabelle wird automatisch mit auf gescannten oder überwachten Kanälen gehörten Adressen befüllt.
- `AC-GEN-015-4` — DO (A.4.3.3): Bei voller Kapazität werden die ältesten Einträge durch die neuesten ersetzt (LRU-Verdrängung).
- `AC-GEN-015-5` — DO (A.4.3.3): Automatisch befüllte Einträge werden für Rufinitiierung und Aktivitätsbewertung verwendet.

<!-- LÜCKE geschlossen: AC-GEN-015-3 bis -5 aus Standard A.4.3.3 DO ergänzt.
     Text: "excess capacity...should be automatically filled with any addresses heard on any of
     the scanned or monitored channels...replacing the oldest heard addresses with the latest ones." -->

---

##### REQ-GEN-016 — Fremdstations-Adressspeicher: Individuelle Einträge und Netzinformationen, nichtflüchtig

**Spec-Referenz:** A.4.3.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Individuelle Stationsadressen müssen in separaten Tabelleneinträgen gespeichert werden und können mit einer spezifischen Wartezeit für Antworten (Twr) verknüpft sein, sofern dieser vom Standardwert abweicht. Netzinformationen müssen eigene Netz- und Netz-Member-Zuordnungen, relative Slot-Sequenzen und eigene Netz-Wartezeiten für Antworten (Twrn) für Rufinitiierungen enthalten. Der Adress- und Einstellungsspeicher muss nichtflüchtig sein.

**Akzeptanzkriterien:**
- `AC-GEN-016-1` — Individuelle Stationsadressen müssen in separaten Einträgen gespeichert sein.
- `AC-GEN-016-2` — Jeder Eintrag kann eine spezifische Twr-Wartezeit enthalten.
- `AC-GEN-016-3` — Netzinformationen müssen Netz-Member-Zuordnungen, Slot-Sequenzen und Twrn enthalten.
- `AC-GEN-016-4` — Der Speicher muss nichtflüchtig sein.

---

##### REQ-GEN-017 — LQA-Speicher: Mindestens 4000 Einträge, 1 Stunde Pufferung

**Spec-Referenz:** A.4.3.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gerät muss mindestens 4000 (Design Objective: 10.000) Konnektivitäts- und LQA-Informationssätze, die Kanälen und Fremdadressen zugeordnet sind, in einem LQA-Speicher halten können. Der LQA-Speicher muss bei Stromausfall oder Verlust der Primärversorgung mindestens eine Stunde lang erhalten bleiben. Jede Adress-/Kanalzelle muss mindestens bilaterale SINAD-Werte für empfangene Signale an der eigenen Station und von der eigenen Station empfangene und von der Fremdstation gemeldete Signale enthalten. Sie muss außerdem entweder eine Altersanzeige der Information oder einen Algorithmus zur automatischen Gewichtsreduzierung älterer Daten beinhalten. Design Objective: Zellen sollen auch bilaterale BER- und bilaterale Mehrweg-Informationen von geeignet ausgerüsteten Einheiten enthalten.

**Akzeptanzkriterien:**
- `AC-GEN-017-1` — Das System muss mindestens 4000 LQA-Einträge halten können.
- `AC-GEN-017-2` — Der LQA-Speicher muss bei Stromausfall mindestens 1 Stunde lang erhalten bleiben.
- `AC-GEN-017-3` — Jede Zelle muss bilaterale SINAD-Werte enthalten.
- `AC-GEN-017-4` — Jede Zelle muss eine Altersanzeige oder einen Gewichtsreduzierungsalgorithmus für ältere Daten enthalten.

---

##### REQ-GEN-018 — Fremdstations-Einstellungen: Nichtflüchtige Speicherung (DO)

**Spec-Referenz:** A.4.3.3.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Design Objective: Gerätespezifische Einstellungen für Links mit bestimmten Stationen oder Netzen sollen in nichtflüchtigem Speicher abgelegt werden. Solche Einstellungen können Antennenauswahl und -azimut, für diese Station oder dieses Netz autorisierte Kanäle, Leistungsgrenzen für das betreffende Netz usw. umfassen.

**Akzeptanzkriterien:**
- `AC-GEN-018-1` — Das System soll station- und netzspezifische Einstellungen in nichtflüchtigem Speicher ablegen können.

---

### FEAT-GEN-007 — Betriebsparameter-Programmierbarkeit (A.4.3.4)

**Setzt um:** REQ-GEN-019
**Modul:** `include/Stores/ale_data_store.h`, `src/Stores/ale_data_store.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Datenstrukturen

```cpp
struct OperatingParameters {
    // Scan
    ScanRate scan_rate;
    uint8_t  max_scan_chan;
    uint32_t max_tune_time_ms;
    uint32_t activity_timeout_ms;
    uint32_t listen_time_ms;
    uint8_t  scan_set;                  // Index in Kanalsatz-Tabelle

    // LQA
    bool     request_lqa;
    uint8_t  lqa_status;
    uint32_t lqa_age_ms;
    bool     auto_power_adj;

    // Akzeptanz-Flags
    bool     accept_any_call;
    bool     accept_all_call;
    bool     accept_amd;
    bool     accept_dtm;
    bool     accept_dbm;

    // Timing
    uint32_t turn_around_time_ms;
    uint32_t slot_wait_time_tw;

    // Adress-Tabellen (Indizes in SelfAddressStore / OtherStationStore)
    uint8_t  self_addr_table[20];
    uint8_t  other_addr_table[100];
    // ... alle weiteren Parameter aus A.4.3.4 analog
};

class OperatingParameterStore {
public:
    OperatingParameters params;
    bool persist(IPersistenceBackend&) const;
    bool restore(IPersistenceBackend&);
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-019 — Betriebsparameter: Programmierbarkeit durch Operator oder Controller

**Spec-Referenz:** A.4.3.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die folgenden ALE-Betriebsparameter müssen durch den Operator oder einen externen automatisierten Controller programmierbar sein (vollständige Definitionen in Anhang H):

`ScanRate`, `RequestLQA`, `OtherAddr`, `LqaStatus`, `MaxScanChan`, `AutoPowerAdj`, `OtherAddrStatus`, `LqaAge`, `MaxTuneTime`, `SelfAddrTable`, `TurnAroundTime`, `SelfAddrEntry`, `ActivityTimeout`, `SelfAddr`, `ListenTime`, `OtherAddrNetMembers`, `LqaMultipath`, `OtherAddrValidChannels`, `LqaSINAD`, `OtherAddrAnt`, `LqaBER`, `SelfAddrStatus`, `OtherAddrAntAzimuth`, `ScanSet`, `AcceptAnyCall`, `NetAddr`, `OtherAddrPower`, `AcceptAllcall`, `SlotWaitTime`, `LqaMatrix`, `AcceptAMD`, `SelfAddrValidChannels`, `LqaEntry`, `AcceptDTM`, `AcceptDBM`, `OtherAddrTable`, `LqaAddr`, `OtherAddrEntry`, `LqaChannel`, `ConnectionTable`, `ConnectionEntry`, `ConnectedAddr`, `ConnectionStatus`

**Akzeptanzkriterien:**
- `AC-GEN-019-1` — Alle aufgeführten Parameter müssen durch den Operator oder externen Controller programmierbar sein.

---

### FEAT-GEN-008 — Nachrichtenspeicher (Message Memory, A.4.3.5)

**Setzt um:** REQ-GEN-020
**Modul:** `include/Stores/ale_data_store.h`, `src/Stores/ale_data_store.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Datenstrukturen

```cpp
enum class MessageSource : uint8_t { PREPROGRAMMED, OPERATOR, INCOMING };

struct MessageEntry {
    MessageSource source;
    char          text[128];    // max 1000/12 Zeichen pro Nachricht (DO: 10000/100)
    uint16_t      length;
    uint64_t      timestamp_ms;
    bool          valid;
};

class MessageStore {
public:
    static constexpr uint8_t  CAPACITY_MIN      = 12;
    static constexpr uint8_t  CAPACITY_DO       = 100;
    static constexpr uint16_t TOTAL_CHARS_MIN   = 1000;
    static constexpr uint16_t TOTAL_CHARS_DO    = 10000;
    static constexpr uint32_t POWER_DOWN_RETENTION_MS = 3600000; // 1h

    bool   store(const MessageEntry& msg);
    bool   load(uint8_t index, MessageEntry& out) const;
    uint8_t count() const;
    bool   persist(IPersistenceBackend&) const;
    bool   restore(IPersistenceBackend&);
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-020 — Nachrichtenspeicher: Mindestens 12 Nachrichten / 1000 Zeichen, 1 Stunde Pufferung

**Spec-Referenz:** A.4.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Im Gerät muss Speicher für vorprogrammierte, vom Operator eingegebene und eingehende Nachrichten bereitgestellt werden. Dieser Speicher muss bei Stromausfall oder Verlust der Primärversorgung mindestens eine Stunde lang erhalten bleiben. Es müssen mindestens 12 Nachrichten (Design Objective: 100) bei einer Gesamtkapazität von mindestens 1000 Zeichen (Design Objective: 10.000 Zeichen) gespeichert werden können.

**Akzeptanzkriterien:**
- `AC-GEN-020-1` — Das System muss mindestens 12 Nachrichten speichern können.
- `AC-GEN-020-2` — Die Gesamtkapazität muss mindestens 1000 Zeichen betragen.
- `AC-GEN-020-3` — Der Nachrichtenspeicher muss bei Stromausfall mindestens 1 Stunde lang erhalten bleiben.

---

### FEAT-GEN-009 — ALE Betriebsregeln (Operational Rules, A.4.4)

**Setzt um:** REQ-GEN-021
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009
**Status:** geplant

#### Beschreibung
Keine neuen Datenstrukturen — dieses Feature verifiziert, dass die 11 Betriebsregeln aus Tabelle A-V in der State Machine korrekt implementiert sind. Es ist primär ein Verifikations-Feature mit zugehörigen Tests.

#### Technischer Entwurf

| Regel | Implementierungsort | Testbar durch |
|---|---|---|
| 1. Unabhängige RX-Fähigkeit | ALEFECCodec stateless, RX-Pfad unabhängig | Unit-Test: paralleler Encode/Decode |
| 2. Immer hören | `outer_state_ == AVAILABLE` → RX immer aktiv | Integration-Test: RX während TX |
| 3. Immer antworten | `accept_calls_` Flag, Standard=true | Test: Flag-Logik |
| 4. Automatische Rückkehr in Ausgangszustand | `outer_state_` wechselt nach Abschluss/Timeout aus Terminalzuständen zurück in vorherigen Zustand | Test: Zustandsautomat |
| 5. Kanal nicht stören | `ChannelSelector::listen_before_transmit()` | Test: LBT-Logik |
| 6. LQA austauschen | `ChannelSelector::update_lqa()` nach jedem RX | Test: LQA-Update |
| 7. Slot-Antwort | `ALEStateMachine::calculate_slot_response()` | Test: Tswt-Formel |
| 8. Konnektivität tracken | `OtherStationStore::auto_insert_heard()` | Test: Auto-Fill |
| 9. Höchste beidseitige Fähigkeit | Capability-Negotiation im Handshake | Test: Capability-Flags |
| 10. TX/RX-Zeit minimieren | Frame-Limits aus DD-010 | Timing-Test |
| 11. Leistung minimieren | `OperatingParameters::auto_power_adj` | Test: Power-Adj-Logik |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-021 — ALE-Betriebsregeln gemäß Tabelle A-V (Prioritätsreihenfolge)

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

### FEAT-GEN-010 — AQC-ALE Protokoll (A.4.5)

**Setzt um:** REQ-GEN-022, REQ-GEN-023, REQ-GEN-024, REQ-GEN-025
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Design-Entscheidungen:** DD-008
**Status:** geplant

#### Beschreibung
Optionale Erweiterung. AQC-ALE packt 3 Zeichen (21 Bit) in 16 Bit durch ein eigenes Encoding. Festes Bit verhindert Verwechslung mit Basis-ALE-Wörtern. AQC muss immer Basis-ALE hören und darauf antworten.

#### Datenstrukturen

```cpp
// AQC-Adressencoding: 3 Chars × 6 Bit = 18 Bit, aber max 6 Chars → 2 Wörter
// Bit 15 in jedem AQC-Adresswort ist fest = 1 → unterscheidet von Basis-ALE
struct AQCWord {
    uint16_t value;     // bit15=1 (festes Bit), bit14..0 = 3×5-Bit-Zeichen
};

// Preamble-Mapping AQC → Basis-ALE:
// FROM  → PART2  (2. Adresswort)
// THRU  → INLINK (verlinkte Transaktion)

class AQCProtocol {
public:
    // Kodiert max 6 Zeichen in 1-2 AQC-Wörter
    static std::vector<AQCWord> encode_address(const char* addr);
    static bool decode_address(const AQCWord* words, uint8_t count, char* out);
    // Prüft ob empfangenes Wort ein AQC-Wort ist (bit15 == 1)
    static bool is_aqc_word(uint16_t raw);
    // Fallback: Wenn Basis-ALE-Ruf erkannt → antworten in Basis-ALE-Modus
    static void handle_base_ale_call(ALEStateMachine& sm, const ALEWord& word);
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-GEN-022 — AQC-ALE: Einführung und Grundprinzip (NT)

**Spec-Referenz:** A.4.5.1
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE kann zusätzlich zur grundlegenden ALE-Funktionalität implementiert werden. Es stellt eine Linkaufbautechnik bereit, die deutlich weniger Zeit für den Linkaufbau benötigt als das Basis-ALE-System, indem zusätzliche Technologien eingesetzt und weniger genutzte Funktionen des Basissystems gegen einen schnelleren Verbindungsaufbau eingetauscht werden. Das AQC-ALE muss immer auf den Basis-ALE-Ruf hören und automatisch in diesem Modus antworten und operieren, wenn es per Basis-ALE gerufen wird.

**Akzeptanzkriterien:**
- `AC-GEN-022-1` — Das AQC-ALE-System muss immer auf Basis-ALE-Rufe hören.
- `AC-GEN-022-2` — Das AQC-ALE-System muss automatisch im Basis-ALE-Modus antworten, wenn es per Basis-ALE gerufen wird.

---

##### REQ-GEN-023 — AQC-ALE: Allgemeine Signalisierungsstrategien

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

##### REQ-GEN-024 — AQC-ALE: Unterstützte Funktionen

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

##### REQ-GEN-025 — AQC-ALE: Nicht unterstützte Funktionen

**Spec-Referenz:** A.4.5.4
**Priorität:** COULD · **Status:** offen

**Anforderung:** Das AQC-ALE-Protokoll stellt folgende Funktionen nicht bereit: Gruppenruf (als Alternative kann ein Controller nacheinander weitere Mitglieder über das Rufprotokoll hinzufügen); AMD, DTM und DBM während des Linkaufbaus (der Fokus liegt auf schnellstmöglichem Linkaufbau; nach dem Linkaufbau kann Information ausgetauscht werden); frühe Identifikation der Adresse des Senders während des Orderwire-Datenverkehrs oder zusätzliche Adressierungsidentifikation für Relay-Adressen (der vereinfachte Linkaufbau macht dies überflüssig; Orderwire-Nachrichten sind während des Linkaufbaus nicht zulässig).

**Akzeptanzkriterien:**
- `AC-GEN-025-1` — Das AQC-ALE darf keinen Gruppenruf bereitstellen (Alternative: sequenzielles Hinzufügen über Rufprotokoll).
- `AC-GEN-025-2` — AMD, DTM und DBM dürfen im AQC-ALE nicht während des Linkaufbaus verwendet werden.
- `AC-GEN-025-3` — Orderwire-Nachrichten sind während des AQC-ALE-Linkaufbaus nicht zulässig.

---


### FEAT-GEN-011 — Version CMD (A.5.6.6.1)

**Setzt um:** A.5.6.6.1  
**Modul:** `include/Protocol/ale_version_caps.h`, `src/Protocol/ale_version_caps.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
Die Version-Funktion ist ein kurzer CMD-Worttyp zur Identifikation von Hardware-, Firmware- und Network-Management-Komponenten. Der Request verwendet die Präambel `CMD`, den Familienbuchstaben `v` und als zweite Zeichenposition `s` für den Summary-Report. Die Antwort soll nur die durch KVC angeforderten Komponenten berichten und dabei das vom Gegenüber akzeptierte höchste gemeinsame Nachrichtenformat wählen.

#### Datenstrukturen
```cpp
enum class VersionComponent : uint8_t {
    Hardware,
    OperatingFirmware,
    NetworkManagementFirmware,
};

enum class VersionFormat : uint8_t {
    Summary,
    Full,
};

struct VersionCmd {
    uint8_t kvc_mask;   // Komponentenbitmaske
    uint8_t kvf_mask;   // akzeptierte Antwortformate
};

struct VersionReport {
    VersionFormat format;
    std::string hardware_version;
    std::string firmware_version;
    std::string hnmp_version;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-011-1 | tests/test_version_caps.cpp: `v`/`s`-Encoding | offen |
| AC-GEN-011-2 | tests/test_version_caps.cpp: KVC-Komponentenfilter | offen |
| AC-GEN-011-3 | tests/test_version_caps.cpp: KVF-Formatwahl / höchste gemeinsame Nachricht | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_version_caps.h` | `VersionCmd`, `VersionReport` | neu anzulegen |
| `src/Protocol/ale_version_caps.cpp` | `encode_version_cmd()`, `decode_version_report()` | neu anzulegen |

#### Requirement-Details

##### A.5.6.6.1 — Version CMD
Die Version-Funktion dient dazu, die Version eines ALE-Controllers abzufragen. Der Request beginnt mit `v` in der ersten Zeichenposition und verwendet `s` in der zweiten Zeichenposition für einen Summary-Report. Die KVC-Bits geben an, welche Komponenten berichtet werden sollen; die KVF-Bits geben an, welche Antwortformate akzeptabel sind. Der antwortende Controller soll alle angeforderten, tatsächlich vorhandenen Komponenten berichten und das höchstwertige gemeinsame ALE-Message-Format wählen.


### FEAT-GEN-012 — Capabilities Query/Report (A.5.6.6.2)

**Setzt um:** A.5.6.6.2  
**Modul:** `include/Protocol/ale_version_caps.h`, `src/Protocol/ale_version_caps.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
Die Capabilities-Funktion liefert eine kompakte Beschreibung der Fähigkeiten eines entfernten ALE-Controllers. Der Query verwendet `CMD` mit `c` als zweitem und `q` als drittem Zeichen. Der Report verwendet `CMD` mit `c` und `r` und besteht aus einem CMD-Wort plus fünf DATA-Wörtern. Die Felder sind konstant strukturiert und beschreiben den aktuellen Betriebszustand des Controllers.

#### Datenstrukturen
```cpp
struct CapabilitiesQuery {
    bool full_report_requested;   // c/q
};

struct CapabilitiesReport {
    uint8_t scan_rate_chps;        // 2, 5, ggf. 10
    uint8_t channels_scanned;      // Anzahl gescannter Kanäle
    uint16_t max_tune_time_ms;
    uint16_t turnaround_time_ms;
    uint16_t activity_timeout_s;
    uint16_t listen_time_ms;

    bool accept_all_calls;
    bool accept_any_calls;
    bool amd_enabled;
    bool dtm_enabled;
    bool dbm_enabled;

    uint8_t lp_levels_mask;
    bool time_service_enabled;
    bool alqa_enabled;
    bool orderwire_enabled;
    bool scheduling_enabled;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-012-1 | tests/test_version_caps.cpp: `c`/`q` Query | offen |
| AC-GEN-012-2 | tests/test_version_caps.cpp: `c`/`r` Report mit 5 DATA-Wörtern | offen |
| AC-GEN-012-3 | tests/test_version_caps.cpp: konstante Felder / unsigned timing fields | offen |
| AC-GEN-012-4 | tests/test_version_caps.cpp: Scan Rate, LP, ALQA, Orderwire, Scheduling | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_version_caps.h` | `CapabilitiesQuery`, `CapabilitiesReport` | neu anzulegen |
| `src/Protocol/ale_version_caps.cpp` | `encode_capabilities_query()`, `encode_capabilities_report()` | neu anzulegen |

#### Requirement-Details

##### A.5.6.6.2 — Capabilities Function
Die Capabilities-Funktion dient zur Ermittlung einer kompakten Darstellung der verfügbaren Funktionen eines entfernten ALE-Controllers. Der Report ist in seiner Wortstruktur fest und beschreibt den aktuellen, programmierten Betriebszustand. Die Datenfelder umfassen insbesondere Scan Rate, Channels Scanned, Maximum Tune Time, Turnaround Time, Activity Timeout, Listen Time, Accept All Calls, Accept Any Calls, AMD/DTM/DBM, LP-Level, Time-Service-Funktionen, ALQA-Funktionen, Orderwire-Funktionen und Scheduling-Funktionen.

##### A.5.6.6.2.1 — Capabilities Query
Der Query ist ein einzelnes CMD-Wort mit `c` in der zweiten Zeichenposition und `q` in der dritten Zeichenposition.

##### A.5.6.6.2.2 — Capabilities Report
Der Report besteht aus einem CMD-Wort und fünf DATA-Wörtern. Das zweite und vierte DATA-Wort verwenden REP statt DATA für die entsprechenden Präambeln, wie vom Protokoll gefordert.

##### A.5.6.6.2.3 — Reportfelder
Die Reportfelder sollen die aktuellen Timing- und Funktionsparameter des Controllers kodieren. Zeitfelder werden als vorzeichenlose Ganzzahlen dargestellt. Zu den relevanten Feldern gehören Scan Rate, Anzahl gescannter Kanäle, Maximum Tune Time, Turnaround Time, Activity Timeout, Listen Time, Acceptance von All/Any Calls, AMD/DTM/DBM, LP-Level, ALQA, Orderwire, Scheduling sowie weitere spezielle Steuerfunktionen wie Frequency Select, Channel Select, Modem Negotiation, Crypto Negotiation, Power Control, Tune and Wait und Do Not Respond.


### FEAT-GEN-013 — ALE Message-Protocol Framework (A.5.7.1)

**Setzt um:** A.5.7.1  
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
Das Message-Protocol-Framework bildet den gemeinsamen Rahmen für alle ALE-Orderwire-Protokolle. Es definiert die Nachrichtenphase eines Links, die Trennung von Adressierung und Nutzlast sowie den Übergang vom Call-/Handshake-Teil in den Orderwire-Teil. Das Framework ist die Basis für AMD, DTM und DBM.

#### Datenstrukturen
```cpp
enum class MessageProtocol : uint8_t {
    AMD,
    DTM,
    DBM,
};

struct MessageSession {
    MessageProtocol protocol;
    bool crc_enabled;
    bool arq_requested;
    uint32_t payload_length_bits;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-013-1 | tests/test_message_protocols.cpp: Protokollauswahl | offen |
| AC-GEN-013-2 | tests/test_message_protocols.cpp: Trennung von Call- und Message-Phase | offen |
| AC-GEN-013-3 | tests/test_message_protocols.cpp: korrekte Übergabe an AMD/DTM/DBM | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_orderwire_protocols.h` | `MessageProtocol`, `MessageSession` | neu anzulegen |
| `src/Protocol/ale_orderwire_protocols.cpp` | `select_message_protocol()` | neu anzulegen |

#### Requirement-Details

##### A.5.7.1 — Message Protocols Overview
Die ALE-Systeme sollen Information innerhalb des Orderwire- oder Message-Abschnitts des Frames übertragen können. Das Framework kapselt die Protokollauswahl und trennt Nutzdatenlogik von der Basisruf-/Handshake-Logik.


### FEAT-GEN-014 — Automatic Message Display (AMD) (A.5.7.2)

**Setzt um:** A.5.7.2  
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`, `include/Protocol/ale_message.h`, `src/Protocol/ale_message.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
AMD ist der textorientierte Orderwire-Modus für die Übertragung und Anzeige kurzer Nachrichten während eines Links. Die Funktion ist für die unmittelbare Darstellung beim Empfänger vorgesehen und gehört zu den standardmäßigen ALE-Messagetypen.

#### Datenstrukturen
```cpp
struct AmdMessage {
    std::string text;
    bool display_immediately;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-014-1 | tests/test_message_protocols.cpp: AMD-Nachrichtenaufbau | offen |
| AC-GEN-014-2 | tests/test_message_protocols.cpp: AMD-Anzeigeverhalten | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_orderwire_protocols.h` | `AmdMessage` | neu anzulegen |
| `src/Protocol/ale_orderwire_protocols.cpp` | `encode_amd()`, `decode_amd()` | neu anzulegen |

#### Requirement-Details

##### A.5.7.2 — AMD
AMD ist der Automatic-Message-Display-Modus. Er dient der Anzeige kurzer Meldungen auf der Gegenseite während eines Links. Die Message-Phase muss hierfür innerhalb des Linkzustands unterstützt werden.


### FEAT-GEN-015 — Data Text Message (DTM) (A.5.7.3)

**Setzt um:** A.5.7.3  
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
DTM ist der standardisierte Textnachrichtenmodus für ALE. Er verwendet CMD DTM, DATA- und REP-Wörter, optionalen CRC-Schutz und kann ASCII- sowie binäre Daten transportieren. DTM ist stärker strukturiert als AMD und für längere, robuste Nachrichtentransfers geeignet.

#### Datenstrukturen
```cpp
struct DtmBlock {
    std::vector<uint8_t> data_bits;
    bool crc_enabled;
    bool arq_requested;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-015-1 | tests/test_message_protocols.cpp: DTM-Aufbau | offen |
| AC-GEN-015-2 | tests/test_message_protocols.cpp: CRC im DTM | offen |
| AC-GEN-015-3 | tests/test_message_protocols.cpp: ASCII- und Binary-Transport | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_orderwire_protocols.h` | `DtmBlock` | neu anzulegen |
| `src/Protocol/ale_orderwire_protocols.cpp` | `encode_dtm()`, `decode_dtm()` | neu anzulegen |

#### Requirement-Details

##### A.5.7.3 — DTM
DTM ist der Data-Text-Message-Modus. Er verwendet den ALE-CRC dort, wo er vom Standard vorgesehen ist, und transportiert Text- oder binäre Nutzdaten in strukturierter Form. Die Empfangsseite muss Synchronisation, Reassembly und CRC-Prüfung unterstützen.


### FEAT-GEN-016 — Data Block Message (DBM) (A.5.7.4)

**Setzt um:** A.5.7.4  
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`  
**Design-Entscheidungen:** offen  
**Status:** geplant

#### Beschreibung
DBM ist der Blockmodus für hochrobuste Datenübertragung im ALE-Orderwire-Bereich. Er unterstützt voll gepufferte ASCII- oder Binärblöcke und ist für tiefes Interleaving sowie FEC optimiert. Der Modus ist für den Durchsatz robuster Datenübertragung konzipiert.

#### Datenstrukturen
```cpp
struct DbmBlock {
    std::vector<uint8_t> payload_bits;
    uint16_t crc16;
    bool buffered;
};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-GEN-016-1 | tests/test_message_protocols.cpp: DBM-Aufbau | offen |
| AC-GEN-016-2 | tests/test_message_protocols.cpp: CRC nach Datenblock | offen |
| AC-GEN-016-3 | tests/test_message_protocols.cpp: Blockpufferung und Transparenz | offen |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/Protocol/ale_orderwire_protocols.h` | `DbmBlock` | neu anzulegen |
| `src/Protocol/ale_orderwire_protocols.cpp` | `encode_dbm()`, `decode_dbm()` | neu anzulegen |

#### Requirement-Details

##### A.5.7.4 — DBM
DBM ist der Data-Block-Message-Modus. Er ist für robuste, gepufferte Datenübertragung ausgelegt und soll an den beteiligten DTEs transparent erscheinen. Der Modus stützt sich auf Interleaving, FEC und die im Standard spezifizierte CRC-Verarbeitung.

### FEAT-WAVEFORM-001 — Tone-Symbol-Mapping & Frequenztabelle

**Setzt um:** REQ-WAVEFORM-001, REQ-WAVEFORM-002, REQ-WAVEFORM-003
**Modul:** `include/FSK/ale_waveform.h`
**Design-Entscheidungen:** DD-002
**Status:** implementiert

#### Datenstrukturen
```cpp
constexpr std::array<uint32_t, 8> TONE_FREQS_HZ  = {750,1000,1250,1500,1750,2000,2250,2500};
constexpr std::array<uint8_t, 8>  FREQ_TO_SYMBOL = {0,1,3,2,6,7,5,4};
```

#### Verifikation
| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-WAVEFORM-002-1 | tests/test_fsk_core.cpp: 8 orthogonale Töne | implementiert |
| AC-WAVEFORM-002-2 | tests/test_fsk_core.cpp: 3 Bits pro Symbol | implementiert |
| AC-WAVEFORM-003-1 | tests/test_fsk_core.cpp: FREQ_TO_SYMBOL Bijektivität | implementiert |

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `include/FSK/ale_waveform.h` | `FREQ_TO_SYMBOL`, `TONE_FREQS_HZ` | static_assert Bijektivität |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WAVEFORM-001 — Modulzweck des ALE-Waveform

**Spec-Referenz:** A.5.1.1
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die ALE-Waveform ist so ausgelegt, dass sie durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden kann. Sie stellt eine robuste, langsame digitale Modem-Kapazität für multiple Zwecke bereit, einschließlich selektives Rufen und Datenübertragung.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-001-1` — Die Waveform muss durch das Audiopassband von Standard-SSB-Funkgeräten hindurchgeleitet werden können
- `AC-WAVEFORM-001-2` — Die Waveform muss selektives Rufen ermöglichen
- `AC-WAVEFORM-001-3` — Die Waveform muss Datenübertragung ermöglichen

---

##### REQ-WAVEFORM-002 — Modulationsart

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die Waveform muss eine 8-ary frequency shift-keying (FSK) Modulation mit acht orthogonalen Tönen verwenden, wobei jeweils ein Ton (Symbol) zu einer Zeit übertragen wird. Jeder Ton repräsentiert drei Bits Daten.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-002-1` — Die Modulation muss FSK mit exakt 8 orthogonalen Tönen sein
- `AC-WAVEFORM-002-2` — Pro Ton müssen exakt 3 Bits repräsentiert werden
- `AC-WAVEFORM-002-3` — Es muss immer nur ein Ton zu einer Zeit übertragen werden

---

##### REQ-WAVEFORM-003 — Tonfrequenzen und Bit-Zuordnung

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** implementiert

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

### FEAT-WAVEFORM-002 — NCO-Tongenerator mit Phasenkontinuität

**Setzt um:** REQ-WAVEFORM-004, REQ-WAVEFORM-005
**Modul:** `include/FSK/tone_generator.h`, `src/src/FSK/tone_generator.cpp`
**Design-Entscheidungen:** DD-002, DD-005
**Status:** implementiert

#### Datenstrukturen
```cpp
uint32_t phase_;                          // globaler NCO-Akkumulator
std::array<uint32_t, 8> phase_increment;  // pro Symbol, init in init_phase_increments()
std::array<float, 256>  sine_table;       // vorberechnete Sinustabelle
```

#### Algorithmus
```
generate_tone(symbol, num_samples, output, amplitude):
  phase_inc = phase_increment[symbol]
  for i in 0..num_samples:
    index   = phase_ >> 24        // obere 8 Bits = Tabellen-Index
    frac    = phase_ & 0xFFFFFF   // untere 24 Bits = Fraktion
    sample  = lerp(sine[index], sine[index+1], frac/2^24)
    output[i] = clamp(sample * amplitude * 32767, -32768, 32767)
    phase_ += phase_inc           // wraps bei 2^32
```

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `src/src/FSK/tone_generator.cpp` | `ToneGenerator::generate_tone` | `// FEAT-WAVEFORM-002` |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WAVEFORM-004 — Codierung und Interleaving der Bits

**Spec-Referenz:** A.5.1.2, Bezug auf A.5.2.2 und A.5.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die übertragenen Bits müssen codierte und interleaved Datenbits sein, die ein Wort bilden.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-004-1` — Die Bits müssen codiert und interleaved sein gemäß den in A.5.2.2 und A.5.2.3 definierten Verfahren (siehe OPEN-01, OPEN-02).
- `AC-WAVEFORM-004-2` — Jede Gruppe von Bits muss einem vollständigen ALE-Wort entsprechen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-01 — Der Verweis auf A.5.2.2 (FEC/Golay) und A.5.2.3 (Word structures) ist enthalten, aber der Inhalt dieser Abschnitte wurde nicht geliefert. Codierungsdetail und Interleaving-Regeln sind hier nicht spezifiziert.

---

##### REQ-WAVEFORM-005 — Phasenkontinuität der Tonübergänge

**Spec-Referenz:** A.5.1.2
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die Übergänge zwischen Tönen müssen phasenkontinuierlich sein und an den Maxima oder Minima der Welle (slope zero) erfolgen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-005-1` — Tonübergänge müssen phasenkontinuierlich sein
- `AC-WAVEFORM-005-2` — Tonübergänge müssen an einem Punkt mit null Ableitung (Maxima oder Minima) erfolgen

---

### FEAT-WAVEFORM-003 — Timing-Konstanten & Wortgrenzen

**Setzt um:** REQ-WAVEFORM-006–010
**Modul:** `include/FSK/ale_waveform.h`, `include/Protocol/ale_state_machine.h`
**Design-Entscheidungen:** DD-006
**Status:** implementiert

#### Konstanten
```cpp
constexpr uint32_t SAMPLE_RATE_HZ     = 8000;
constexpr uint32_t SYMBOL_RATE_BAUD   = 125;
constexpr uint32_t SAMPLES_PER_SYMBOL = 64;    // 8000/125
constexpr uint32_t SYMBOLS_PER_WORD   = 49;
// In ALETimingConstants (ale_state_machine.h):
constexpr uint32_t Tw_ms   = 130;  // 130,66... ms
constexpr uint32_t Trw_ms  = 392;  // WORD_DURATION_MS = 3 × Tw
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WAVEFORM-006 — Tonrate und Symbolperiode

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

### FEAT-WAVEFORM-004 — Genauigkeits-Verifikation

**Setzt um:** REQ-WAVEFORM-011, REQ-WAVEFORM-012, REQ-WAVEFORM-013
**Modul:** `tests/test_tone_accuracy.cpp` (neu anzulegen)
**Status:** geplant

#### Algorithmus
- Frequenz: Goertzel-Filter auf 64 Samples, Peak-Interpolation → ±1 Hz
- Amplitude: RMS aller 8 Symbole, max/min-Verhältnis ≤ 2 dB
- Timing: Verhältnis generierter Samples zu erwarteten ≤ 10 ppm

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WAVEFORM-011 — Tonfrequenzgenauigkeit am Baseband

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

##### REQ-WAVEFORM-012 — Sendeleistung der Töne im HF-Bereich

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

##### REQ-WAVEFORM-013 — Symbol-Timing-Genauigkeit

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

### FEAT-WORD-001 — word24 Bit-Layout Encoding/Decoding

**Setzt um:** REQ-WORD-001, REQ-WORD-002
**Modul:** `src/Word/ale_word.cpp`, `include/Word/ale_word.h`
**Design-Entscheidungen:** DD-001
**Status:** implementiert

#### Technischer Entwurf
```cpp
// Encoding:
uint32_t word24 = (static_cast<uint8_t>(type) << 21)
                | ((chars[0] & 0x7F) << 14)
                | ((chars[1] & 0x7F) <<  7)
                | ((chars[2] & 0x7F) <<  0);

// Decoding:
preamble = (word_bits >> 21) & 0x07;
char1    = (payload  >> 14) & 0x7F;
char2    = (payload  >>  7) & 0x7F;
char3    =  payload         & 0x7F;
```

#### Code-Referenz
| Datei | Symbol | Hinweis |
|---|---|---|
| `src/Word/ale_word.cpp` | `WordParser::encode_ascii`, `extract_preamble`, `extract_payload` | `// FEAT-WORD-001` |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WORD-001 — Grundstruktur des ALE-Worts

**Spec-Referenz:** A.5.2.3.1 / Figure A-12
**Priorität:** MUST · **Status:** implementiert

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

---

##### REQ-WORD-002 — Präambelbits und Worttypen

**Spec-Referenz:** A.5.2.3.1.2 / A.5.2.3.1.3 / Table A-VIII
**Priorität:** MUST · **Status:** implementiert

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

---

### FEAT-WORD-002 — Adresswörter (TO / TIS / TWAS / THRU / FROM)

**Setzt um:** REQ-WORD-003–007
**Modul:** `src/Word/ale_word.cpp`, `include/Word/ale_word.h`
**Design-Entscheidungen:** DD-001
**Status:** implementiert


#### Preamble-Tabelle
| Preamble | Wert | Funktion |
|---|---|---|
| TO | 2 (010) | Routing-Deskriptor für die aktuell adressierten Zielstation(en); wird in Einzelruf- und Net-Ruf-Protokollen verwendet. |
| TIS | 5 (101) | Routing-Deskriptor für die aktuell sendende Station; beendet den ALE-Frame bzw. die Übertragung bei Abschluss-/Antwortsequenzen und kennzeichnet den Accept-Sound. |
| TWAS | 3 (011) | Routing-Deskriptor wie TIS, jedoch zur Beendigung bzw. Ablehnung der Protokoll-/Handshake-Sequenz; kennzeichnet den Reject-Sound. |
| THRU | 1 (001) | Wird nur im Scanning-Call-Abschnitt von Gruppenrufen verwendet; alternierend mit REP zur Angabe mehrerer direkt anzurufender Stationen. |
| FROM | 4 (100) | Optionale Kennung der sendenden Station ohne Frame-Beendigung; wird nur unmittelbar vor CMD im Message-Abschnitt verwendet („Quick ID“). |


**Hinweis:** TWAS (kodierter Wert 3) ist der Standard-Mnemonic aus Table A-VIII. TIS und TWAS sind im selben Frame gegenseitig ausgeschlossen.


---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WORD-003 — TO für direkte Zieladressen

**Spec-Referenz:** A.5.2.3.2.1
**Priorität:** MUST · **Status:** implementiert

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

---

<!-- LÜCKE geschlossen: REQ-WORD-004–007 aus Standard A.5.2.3.2.2–A.5.2.3.2.5 ergänzt -->

##### REQ-WORD-004 — TIS als Abschluss-Designator der sendenden Station

**Spec-Referenz:** A.5.2.3.2.2
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Das TIS-Wort (101) muss als Routing-Designator die Adresse der aktuell sendenden (rufenden oder soundenden) Station angeben, die den Ruf oder Sound direkt überträgt. Außer bei Verwendung von TWAS muss TIS in allen ALE-Protokollen verwendet werden, um den ALE-Frame und die Übertragung zu beenden. TIS zeigt die Fortsetzung des Protokolls oder Handshakes an und leitet, fordert oder lädt (je nach Protokoll) Antworten oder Bestätigungen von anderen gerufenen oder empfangenden Stationen ein. TIS muss zur Kennzeichnung des Call-Acceptance-Sounds (TIS) verwendet werden. TIS und TWAS sind in demselben Frame gegenseitig ausgeschlossen.

**Akzeptanzkriterien:**
- `AC-WORD-004-1` — TIS enthält die Adresse der aktuell sendenden Station (erste drei Zeichen).
- `AC-WORD-004-2` — TIS beendet den ALE-Frame und die Übertragung (außer bei TWAS).
- `AC-WORD-004-3` — TIS zeigt Protokoll-/Handshake-Fortsetzung an und lädt Antworten ein.
- `AC-WORD-004-4` — Erweiterte Adressen werden identisch zur TO-Struktur in DATA/REP-Wörtern fortgesetzt (max. 5 Adresswörter, 15 Zeichen).
- `AC-WORD-004-5` — TIS und TWAS dürfen im selben Frame nicht gemeinsam vorkommen.
- `AC-WORD-004-6` — TIS wird ausschließlich im Conclusion-Abschnitt des ALE-Frames (oder als ganzer Sound) verwendet.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Preamble-Code TIS | 5 (101) | — | Table A-VIII |
| Maximale Adresslänge | 15 | Zeichen | A.5.2.3.2.2 |
| Maximale Anzahl Adresswörter | 5 | Wörter | A.5.2.3.2.2 |

---

##### REQ-WORD-005 — TWAS als Abschluss-Designator zur Ablehnung

**Spec-Referenz:** A.5.2.3.2.3
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Das TWAS-Wort (011) muss als Routing-Designator exakt wie TIS verwendet werden, mit folgenden Abweichungen: Es zeigt die Beendigung des ALE-Protokolls oder Handshakes an und lehnt, entmutigt oder lädt nicht ein (je nach Protokoll) Antworten oder Bestätigungen von anderen gerufenen oder empfangenden Stationen. TWAS muss zur Kennzeichnung des Call-Rejection-Sounds (TWAS) verwendet werden. TIS darf im selben Frame nicht gemeinsam mit TWAS verwendet werden.

**Akzeptanzkriterien:**
- `AC-WORD-005-1` — TWAS enthält die Adresse der aktuell sendenden Station (erste drei Zeichen).
- `AC-WORD-005-2` — TWAS beendet den ALE-Frame und zeigt Protokoll-/Handshake-Abbruch an.
- `AC-WORD-005-3` — TWAS lehnt Antworten ab bzw. entmutigt diese.
- `AC-WORD-005-4` — Erweiterte Adressen werden identisch zur TO-Struktur in DATA/REP-Wörtern fortgesetzt.
- `AC-WORD-005-5` — TIS und TWAS dürfen im selben Frame nicht gemeinsam vorkommen.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Preamble-Code TWAS | 3 (011) | — | Table A-VIII |

---

##### REQ-WORD-006 — THRU für Gruppenrufe im Scanning-Call-Abschnitt

**Spec-Referenz:** A.5.2.3.2.4
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Das THRU-Wort (001) muss ausschließlich im Scanning-Call-Abschnitt des Calling-Cycle für Gruppenruf-Protokolle verwendet werden. THRU muss alternierend mit REP als Routing-Designator verwendet werden, um den ersten Adress-Wort der direkt anzurufenden Stationen anzugeben. Jedes Adress-Erste-Wort ist auf ein grundlegendes Adresswort (drei Zeichen) begrenzt — keine erweiterten Adressen sind zulässig. Maximal fünf verschiedene Adress-Erste-Wörter sind in einem Gruppenruf erlaubt. THRU darf außerhalb des Scanning-Call-Abschnitts nicht für erweiterte Adressen verwendet werden.

**Akzeptanzkriterien:**
- `AC-WORD-006-1` — THRU wird nur im Scanning-Call-Abschnitt von Gruppenrufen verwendet.
- `AC-WORD-006-2` — THRU wird alternierend mit REP eingesetzt: Sequenz ist ausschließlich THRU/REP-Alternierungen.
- `AC-WORD-006-3` — Jedes THRU-Wort enthält nur ein Adress-Erstes-Wort (drei Zeichen, keine Erweiterung).
- `AC-WORD-006-4` — Maximal fünf verschiedene Adress-Erste-Wörter pro Gruppenruf.
- `AC-WORD-006-5` — THRU wird nicht im Leading-Call-Abschnitt verwendet; dort wird die vollständige Gruppenadresse per TO gesendet.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Preamble-Code THRU | 1 (001) | — | Table A-VIII |
| Max. Adress-Erste-Wörter pro Gruppenruf | 5 | Wörter | A.5.2.3.2.4 |
| Adresslänge im Scanning-Call (THRU) | 3 | Zeichen | A.5.2.3.2.4 |

---

##### REQ-WORD-007 — FROM als optionale Kennung der sendenden Station (Quick ID)

**Spec-Referenz:** A.5.2.3.2.5
**Priorität:** SHOULD · **Status:** implementiert

**Anforderung:** Das FROM-Wort (100) ist ein optionaler Designator, der die sendende Station identifiziert, ohne den ALE-Frame zu beenden. FROM muss die vollständige Adresse der sendenden Station enthalten (unter Verwendung von FROM sowie DATA- und REP-Wörtern identisch zur TO-Adressstruktur). Es darf maximal einmal pro Frame und ausschließlich unmittelbar vor einem CMD-Wort im Message-Abschnitt verwendet werden. FROM sollte eingesetzt werden, um eine „Quick ID" der sendenden Station bereitzustellen, wenn die normale Conclusion verzögert ist (z.B. bei langen Message-Abschnitten).

**Akzeptanzkriterien:**
- `AC-WORD-007-1` — FROM enthält die vollständige Adresse der sendenden Station.
- `AC-WORD-007-2` — FROM tritt maximal einmal pro Frame auf.
- `AC-WORD-007-3` — FROM steht ausschließlich unmittelbar vor einem CMD-Wort im Message-Abschnitt.
- `AC-WORD-007-4` — Erweiterte Adressen werden identisch zur TO-Struktur in DATA/REP-Wörtern fortgesetzt (max. 5 Adresswörter, 15 Zeichen).
- `AC-WORD-007-5` — FROM-Wörter in anderen Sequenzen als unmittelbar vor CMD werden ignoriert.

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Preamble-Code FROM | 4 (100) | — | Table A-VIII |
| Maximalanzahl FROM pro Frame | 1 | — | A.5.2.3.2.5 |
| Position im Frame | Nur unmittelbar vor CMD | — | A.5.2.3.2.5 |

---

### FEAT-WORD-003 — Message & Extension Words (CMD / DATA / REP)

**Setzt um:** REQ-WORD-008, REQ-WORD-009, REQ-WORD-010
**Modul:** `src/Word/ale_word.cpp`, `include/Word/ale_word.h`, `src/Protocol/Control/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-007
**Status:** in Arbeit — 10/18 AC implementiert, 4/18 geblockt (OPEN-03), 1/18 Stub

> **Legende:** ✅ implementiert + getestet · 🔶 senderseitig implementiert, kein empfangsseitiger Validator · 🚫 geblockt (OPEN-03) · 🔲 Stub

#### Preamble-Tabelle
| Preamble | Wert | Funktion |
|---|---|---|
| DATA | 0 (000) | Erweiterungswort — Adressfortsetzung, Message-Inhalt |
| REP | 7 (111) | Wiederholung vorheriger Preamble-Funktion mit neuen Daten |
| CMD | 6 (110) | Steuerkommando — LQA, Orderwire, Netzmanagement |

#### Sequenzregeln
```
Zulässige Adresssequenz (max 5 Wörter):
TO [DATA [REP [DATA [REP]]]]

REP VERBOTEN direkt nach TIS oder TWAS:
TIS → DATA → REP  ✓   (Extended TIS-Adresse)
TIS → REP         ✗   (Sequenzfehler!)

CMD nur im Message-Abschnitt (nach Leading Call, vor Conclusion).
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-WORD-008 — CMD als Sonder-Designator für Message-Wörter

**Spec-Referenz:** A.5.2.3.3 / A.5.2.3.3.1
**Priorität:** MUST · **Status:** teilweise — AC-008-2, AC-008-6 geblockt (OPEN-03)

**Anforderung:** Alle Message-Wörter müssen mit einem CMD-Präambelwort beginnen. CMD muss als Sonder-Designator für systemweite Koordination, Kommandierung, Kontrolle, Status, Information, Interoperation und andere Sonderzwecke verwendet werden. CMD muss für beliebige Kombinationen zwischen ALE-Stationen und Operatoren verwendbar sein. CMD ist nur innerhalb des Message-Abschnitts des ALE-Frames zulässig und muss innerhalb eines Frames einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz haben, damit die vorgesehenen Empfänger und der Sender eindeutig bestimmt werden. Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts. Die Orderwire-Funktionen werden mit CMD selbst oder in Kombination mit REP- und DATA-Message-Wörtern und den zugehörigen Funktionen ausgelöst.

**Akzeptanzkriterien:**
- ✅ `AC-WORD-008-1` — Jeder Message-Wort-Abschnitt beginnt mit CMD. → `FrameValidator::message_sections_begin_with_cmd()`
- 🚫 `AC-WORD-008-2` — CMD unterstützt die im Standard genannten systemweiten Sonderzwecke. → geblockt: OPEN-03
- ✅ `AC-WORD-008-3` — CMD wird nur im Message-Abschnitt verwendet. → `FrameValidator::cmd_not_before_address_section()`
- ✅ `AC-WORD-008-4` — Ein CMD im Frame hat einen vorausgehenden Ruf und eine nachfolgende Schlusssequenz. → `FrameValidator::cmd_has_call_and_conclusion()`
- ✅ `AC-WORD-008-5` — Das erste CMD beendet den Calling Cycle und markiert den Beginn des Message-Abschnitts. → `FrameValidator::first_cmd_begins_message_section()`
- 🚫 `AC-WORD-008-6` — Orderwire-Funktionen können über CMD allein oder in Kombination mit REP und DATA ausgelöst werden. → geblockt: OPEN-03

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-03 — Die in A.5.6 referenzierten Details zu CMD-Orderwire-Funktionen sind im gelieferten Text nicht enthalten.

### 3.5 Extension words — A.5.2.3.4

---

##### REQ-WORD-009 — DATA als Erweiterungs- und Informationswort

**Spec-Referenz:** A.5.2.3.4.1
**Priorität:** MUST · **Status:** teilweise — AC-009-3/4 nur senderseitig, AC-009-5 geblockt (OPEN-03)

**Anforderung:** Das DATA-Wort muss verwendet werden, um das Datenfeld eines vorherigen Worttyps zu erweitern, sofern dieses Wort nicht selbst DATA ist, oder um Informationen in einer Nachricht zu übertragen. In Verbindung mit TO, FROM, TIS oder TWAS muss DATA die Adresserweiterung von den ersten drei Zeichen auf sechs, neun oder mehr Zeichen in Vielfachen von drei ermöglichen, wenn es abwechselnd mit REP verwendet wird. Die ausgewählte Grenze für die Adresserweiterung beträgt insgesamt 15 Zeichen. In Verbindung mit CMD ist die Funktion von DATA durch den Standard für Message-Wörter und -Funktionen vorgegeben.

**Akzeptanzkriterien:**
- ✅ `AC-WORD-009-1` — DATA erweitert ein vorheriges Wortfeld, sofern das vorherige Wort nicht DATA ist. → `FrameValidator::data_not_after_data()` (positiv + negativ getestet)
- ✅ `AC-WORD-009-2` — DATA kann Informationen in einer Nachricht transportieren. → `WordParser::make_word(DATA, ...)` mit Expanded-64-Zeichensatz
- 🔶 `AC-WORD-009-3` — Mit TO, FROM, TIS oder TWAS erweitert DATA Adressen in Vielfachen von drei Zeichen. → senderseitig in `ALEStateMachine::chunk_address()`, kein empfangsseitiger Validator
- 🔶 `AC-WORD-009-4` — Die Adresserweiterung endet spätestens bei 15 Zeichen. → senderseitig in `ALEStateMachine::chunk_address()` (Clamp auf 15), kein empfangsseitiger Validator
- 🚫 `AC-WORD-009-5` — Mit CMD gilt die standarddefinierte Funktion für Message-Wörter. → geblockt: OPEN-03

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Maximale Adresslänge bei Erweiterung | 15 | Zeichen | A.5.2.3.4.1 |
| Erweiterungsschritt | 3 | Zeichen | A.5.2.3.4.1 |

---

##### REQ-WORD-010 — REP als Wiederholungs- und Erweiterungswort

**Spec-Referenz:** A.5.2.3.4.2
**Priorität:** MUST · **Status:** teilweise — AC-010-5 geblockt (OPEN-03), AC-010-7 Stub

**Anforderung:** Das REP-Wort muss verwendet werden, um eine vorherige Präambelfunktion oder Wortbedeutung zu duplizieren, während der Inhalt des Datenfelds geändert wird. Jede Änderung von Wörtern oder Datenfeldbits erfordert eine Änderung der Präambelbits, um Unklarheit und Fehler zu vermeiden. Wenn sich ein Wort ändert, muss die Präambel auch dann geändert werden, wenn das Datenfeld identisch zum vorherigen Wort ist. In Verbindung mit TO muss REP eine Adresserweiterung ermöglichen, sodass mehr als eine Adresse spezifiziert werden kann. In Verbindung mit DATA darf REP zur Erweiterung und Vergrößerung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden. REP darf direkt nach jedem anderen Worttyp folgen, außer nach sich selbst und außer nach TIS oder TWAS.

**Akzeptanzkriterien:**
- ✅ `AC-WORD-010-1` — REP dupliziert die vorherige Präambelfunktion oder Wortbedeutung bei verändertem Datenfeldinhalt. → `FrameValidator::reconstruct_to_addresses()`: `[TO, DATA, REP]` → `"ABCDEFGHI"`
- ✅ `AC-WORD-010-2` — Jede Änderung von Wort oder Datenfeldbit erfordert eine Änderung der Präambel. → `FrameValidator::no_consecutive_same_preamble()` + `static_assert` in `transmit_address_words()`
- ✅ `AC-WORD-010-3` — Ein Wortwechsel wird auch dann durch eine geänderte Präambel angezeigt, wenn das Datenfeld unverändert bleibt. → `FrameValidator::no_consecutive_same_preamble()` + `static_assert` in `transmit_address_words()`
- ✅ `AC-WORD-010-4` — REP ermöglicht in Verbindung mit TO die Spezifikation mehrerer Adressen. → `FrameValidator::reconstruct_to_addresses()`: `[TO, REP]` → zwei Empfänger `{"ABC", "DEF"}`
- 🚫 `AC-WORD-010-5` — REP darf zur Erweiterung von Adress-, Nachrichten-, Befehls- und Statusfeldern verwendet werden. → geblockt: OPEN-03 (Orderwire-Implementierung fehlt)
- ✅ `AC-WORD-010-6` — REP folgt nicht auf sich selbst sowie nicht auf TIS oder TWAS. → `FrameValidator::rep_not_preceded_by_self_tis_twas()`
- 🔲 `AC-WORD-010-7` — REP darf nur dort eingesetzt werden, wo keine Mehrfachsender-Situation entsteht. → Stub: `rep_not_used_in_multiple_sender_situation()` gibt immer `true` zurück

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Konkrete Bit-Reihenfolge und Feldzuordnung gemäß Figure A-12 — Vorschlag Rückverweis: implements REQ-WORD-001
- Adress-Word-Formatter (TO/FROM/TIS/TWAS/DATA/REP) — Wortbildung und Wortwechsel auf Protokollebene; die Segmentierung in 3-Zeichen-Blöcke wird vor der Wortausgabe durchgeführt. Vorschlag Rückverweis: implements REQ-WORD-003, REQ-WORD-004, REQ-WORD-005, REQ-WORD-006, REQ-WORD-007, REQ-WORD-009, REQ-WORD-010
- Adress-Normalisierung aus Self-/Persistent-Memory — Geladene oder selbst referenzierte Adressen werden vor der Wortbildung in die standardkonforme Wortfolge überführt und bei Bedarf mit @ aufgefüllt. Vorschlag Rückverweis: implements REQ-ADDR-013, REQ-ADDR-014
- Präambelbit-Mapping P3/P2/P1 zu den Worttypen gemäß Table A-VIII — Vorschlag Rückverweis: implements REQ-WORD-002

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-03 — Die in A.5.6 referenzierten Details zu CMD-Orderwire-Funktionen sind im gelieferten Text nicht enthalten. Betrifft: AC-WORD-008-2, AC-WORD-008-6, AC-WORD-009-5, AC-WORD-010-5.
- OPEN-04 — Die in A.5.8.1.1 und A.5.8.1.2 referenzierten AQC-ALE-Details sind im gelieferten Text nicht enthalten.
- AC-WORD-010-7 — Mehrsender-Detektion erfordert Frame-Kontext über mehrere Stationen; nicht lösbar ohne Protokollzustandsmaschinen-Integration.

## 5. Fehlerkorrektur (FEC) — A.5.2.2

> **Spec-Stellen sammeln:** A.5.2.2.1 (General), A.5.2.2.2 (Golay coding), A.5.2.2.3 (Interleaving), A.5.2.2.4 (Redundant words).

### EPIC-FEC · Fehlerkorrektur

#### US-FEC-001

> Als ALE-Station will ich übertragene Wörter gegen Funkstörungen absichern, damit der Empfänger auch bei Bitfehlern den Originalinhalt rekonstruiert.

> **Hinweis zur Nummerierung:** REQ-FEC-001 bis REQ-FEC-003 aus dem ursprünglichen Template wurden durch die spec-basierten Anforderungen REQ-FEC-004 bis REQ-FEC-019 ersetzt. Die IDs 001–003 bleiben reserviert.

**Erfüllt durch:** REQ-FEC-004, REQ-FEC-005, REQ-FEC-006, REQ-FEC-007, REQ-FEC-008, REQ-FEC-009, REQ-FEC-010, REQ-FEC-011, REQ-FEC-012, REQ-FEC-013, REQ-FEC-014, REQ-FEC-015, REQ-FEC-016, REQ-FEC-017, REQ-FEC-018, REQ-FEC-019

---

### FEAT-FEC-001 — Golay (24,12) Encoder

**Setzt um:** REQ-FEC-004–009
**Modul:** `include/FEC/golay.h` + `src/FEC/golay.cpp`
**Design-Entscheidungen:** DD-003
**Status:** implementiert
**Tests:** `tests/test_fec_golay.cpp`

#### Technischer Entwurf
```cpp
// Generator-Polynom: 0xAE3 = x^11+x^9+x^7+x^6+x^5+x+1
// Codeword = [info(12 MSB) | parity(12 LSB)]
uint32_t Golay::encode(uint16_t info);
// GOLAY_ENCODE_TABLE[4096]: constexpr, compile-time vorberechnet
// syndrome_table[4096]: lazy-initialisiert via std::call_once beim ersten decode()-Aufruf
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FEC-004 — Kombinierte Fehlerbehandlungs-Funktionen

**Spec-Referenz:** A.5.2.2.1
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die Funktionen Forward Error Correction, Interleaving und Redundanz sind im Sendencoder und Empfangsdecoder durchzuführen.

**Akzeptanzkriterien:**
- `AC-FEC-004-1` ✅ — Der Sendencoder muss FEC, Interleaving und Redundanz unterstützen
- `AC-FEC-004-2` ✅ — Der Empfangsdecoder muss FEC, Deinterleaving und Redundanzverarbeitung unterstützen

---

### FEAT-FEC-002 — Golay (24,12) Decoder mit Fehlerkorrektur

**Setzt um:** REQ-FEC-008, REQ-FEC-010, REQ-FEC-011
**Modul:** `include/FEC/golay.h`, `src/FEC/golay.cpp` *(privat)*
**Design-Entscheidungen:** DD-003
**Status:** implementiert

#### Hinweis zu REQ-FEC-008
REQ-FEC-008 (Parity-Check-Matrix H) ist implizit im Decoder enthalten: `syndrome = received × Hᵀ mod 2`. Die Syndromtabelle kodiert Hᵀ.

#### Algorithmus (tatsächliche Implementierung)

> **Abweichung von ursprünglicher Spec-Skizze:** Die Spec-Skizze beschrieb `DecodeResult { uint16_t info; uint8_t error_count; bool uncorrectable; }` mit `decode(uint32_t codeword_24bit)`. Die Implementierung übergibt das Info-Wort als Out-Parameter und verwendet benannte Flag-Konstanten statt `bool` — ausdrucksstärker, alle Requirements erfüllt.

```cpp
struct DecodeResult {
    uint8_t flag;             // DECODE_OK | DECODE_CORRECTED | DECODE_DETECTED
    uint8_t errors_corrected; // 0..3, nur gültig bei DECODE_CORRECTED
};

static constexpr uint8_t DECODE_OK            = 0x00; // kein Fehler
static constexpr uint8_t DECODE_CORRECTED     = 0x01; // 1–3 Bits korrigiert
static constexpr uint8_t DECODE_DETECTED      = 0xFE; // Fehler erkannt, nicht korrigierbar
static constexpr uint8_t DECODE_UNCORRECTABLE = 0xFF; // Alias (Rückwärtskompatibilität)

// Dekodiert 24-Bit-Codewort; korrigiert bis zu 3 Bitfehler.
// output: korrigiertes Info-Wort (CORRECTED) oder rohes Info-Feld (DETECTED).
static DecodeResult decode(uint32_t codeword, uint16_t& output);
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien sind erfüllt und durch `test_feat_fec_002_decoder_acs()` [TEST FEC-5] abgedeckt.

##### REQ-FEC-008 — Parity-Check-Matrix-Struktur

**Spec-Referenz:** A.5.2.2.2, Figure A-7
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Das System muss Übertragungsfehler anhand des vom Standard vorgeschriebenen Prüfverfahrens des Golay (24,12)-Codes erkennen.

**Akzeptanzkriterien:**
- `AC-FEC-008-1` ✅ — Das Fehlerprüfverfahren entspricht dem Golay-Prüfverfahren des Standards.
  - `compute_syndrome()`: `s = received_parity XOR GOLAY_ENCODE_TABLE[info]` ≡ `s = y × Hᵀ mod 2`
  - Gültiges Codewort → `DECODE_OK` (Syndrom = 0); alle 24 Einzelbitfehler korrekt erkannt.

---

##### REQ-FEC-010 — Dekodier-Formel und Syndrom-Bildung

**Spec-Referenz:** A.5.2.2.2.2
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Das System muss empfangene 24-Bit-Codewörter dekodieren und dabei Übertragungsfehler bis zur vorgeschriebenen Korrekturkapazität des Golay (24,12)-Codes erkennen und korrigieren.

**Akzeptanzkriterien:**
- `AC-FEC-010-1` ✅ — Syndrom-basiertes Dekodierverfahren: `syndrome_table[s]` → Fehlervektor-Lookup in `decode()`.
- `AC-FEC-010-2` ✅ — Eindeutige Zuordnung: alle 2325 Fehlermuster (Gewicht 0–3) haben distinkte Syndrome; verifiziert durch vollständige Korrektur aller Muster auf Referenzwort (2325/2325 korrekt).
- `AC-FEC-010-3` ✅ — Dekodiertes Datenwort = gesendetes Original bei Fehleranzahl ≤ 3: `output = (codeword ^ error_pattern) >> 12 & 0xFFF`.

---

##### REQ-FEC-011 — Fehler-Korrektur-Flag-Logik

**Spec-Referenz:** A.5.2.2.2.2
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Flags müssen entsprechend der Anzahl der korrigierten Fehler gesetzt werden. Wenn s ungleich 0 ist und e mehr Fehler enthält als vom Dekodiermodus korrigierbar, muss ein erkannter Fehler angezeigt und das entsprechende Flag gesetzt werden.

**Akzeptanzkriterien:**
- `AC-FEC-011-1` ✅ — `DECODE_CORRECTED = 0x01` wird gesetzt; `errors_corrected` enthält die tatsächliche Fehleranzahl (1–3).
- `AC-FEC-011-2` ✅ — `DECODE_DETECTED = 0xFE` wird gesetzt, wenn Syndrom ≠ 0 und kein passender Eintrag in der Syndromtabelle existiert.

**OFFENE PUNKTE / ANNAHMEN:**
- ~~OPEN-04~~ — Gelöst: Flag-Semantik ist als benannte Konstanten implementiert (`DECODE_OK`, `DECODE_CORRECTED`, `DECODE_DETECTED`, `DECODE_UNCORRECTABLE` als Alias). Die Zuordnung zu A.5.2.6 ist damit funktional vollständig.

---

### FEAT-FEC-003 — Interleaving / Deinterleaving

**Setzt um:** REQ-FEC-012, REQ-FEC-013
**Modul:** `include/FEC/word_interleaver.h`, `src/FEC/word_interleaver.cpp`, `include/FEC/ale_fec_codec.h`, `src/FEC/ale_fec_codec.cpp`
**Design-Entscheidungen:** DD-003, DD-004
**Status:** implementiert

#### Algorithmus (tatsächliche Implementierung)

```cpp
// WordInterleaver::interleave(uint32_t ale_word) → uint64_t (49 Bit)
//
// Eingabe: 24-Bit ALE-Wort  [W1=bit23 .. W24=bit0]
// Golay-Parität G1..G12 wird intern aus W1..W12 (bits 23..12) berechnet.
//
// Data-Bits (k=0..11):
out[2*k]   = W[23-k];   // A-Kanal: W_(k+1), W1=bit23..W12=bit12
out[2*k+1] = W[11-k];   // B-Kanal: W_(13+k), W13=bit11..W24=bit0
// Parity-Bits (k=12..23):
out[2*k]   = G[23-k];   // A-Kanal: G_(k-11) normal  (parity bits 11..0)
out[2*k+1] = ~G[23-k];  // B-Kanal: ~G_(k+1) invertiert
// Stuff-Bit:
out[48]    = 0;          // S49

// WordInterleaver::deinterleave(uint64_t transmitted, DecodeResult& fec_out) → uint32_t
// Extrahiert W1..W24 und G1..G12, wendet Golay-Fehlerkorrektur auf W1..W12 an.
// Bit 48 (S49) wird ignoriert.
```

**Öffentliche API über ALEFECCodec:**
```cpp
static uint64_t ALEFECCodec::interleave_word(uint32_t ale_word);
static uint32_t ALEFECCodec::deinterleave_word(uint64_t transmitted,
                                                Golay::DecodeResult& fec_out);
```

**Integration in parse_word() (`src/Word/ale_word.cpp`):**
```cpp
// Schritt 1: Majority-Voting über 3 Wort-Wiederholungen → 49-Bit transmitted word
uint64_t transmitted = 0;
SymbolDecoder::decode_word_with_voting(symbols, transmitted);
// Schritt 2: Deinterleave + Golay-Fehlerkorrektur → 24-Bit ALE-Wort
Golay::DecodeResult fec;
const uint32_t ale_word = ALEFECCodec::deinterleave_word(transmitted, fec);
// Schritt 3: Preamble + Payload aus korrigiertem ALE-Wort parsen
return parse_from_bits(ale_word, output, timestamp_ms);
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien sind erfüllt und durch `test_feat_fec_003_*()` [TEST FEC-6..9] abgedeckt.

##### REQ-FEC-012 — Interleaving-Vorgabe

**Spec-Referenz:** A.5.2.2.3
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die Datenbits des Worts und die Golay-FEC-Bits müssen vor der Übertragung gemäß dem im Standard definierten Muster interleaved werden. Die obere Hälfte der FEC-Bits muss dabei invertiert werden.

**Akzeptanzkriterien:**
- `AC-FEC-012-1` ✅ — W1..W24 werden gemäß Spec-Muster umgeordnet: A-Kanal (gerade Positionen 0..22) trägt W1..W12, B-Kanal (ungerade Positionen 1..23) trägt W13..W24.
- `AC-FEC-012-2` ✅ — G13..G24 (B-Kanal, Positionen 25..47) sind die bitweisen Inversen von G1..G12 (A-Kanal, Positionen 24..46); verifiziert für alle Probe-Worte [TEST FEC-8].
- `AC-FEC-012-3` ✅ — Verlustfreier Roundtrip `deinterleave(interleave(w)) == w` für alle 4096 Info-Worte [TEST FEC-6].

---

##### REQ-FEC-013 — Transmitted-Word-Struktur

**Spec-Referenz:** A.5.2.2.3
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Ein übertragenes Wort besteht aus 48 interleaved Bits plus einem 49ten Stuff-Bit S₄₉ (Wert = 0). Die Übertragung erfolgt als A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ mit 16⅓ Symbolen pro Wort. Das 49te Stuff-Bit wird vom Empfänger ignoriert.

**Akzeptanzkriterien:**
- `AC-FEC-013-1` ✅ — `WordInterleaver::TRANSMITTED_BITS = 49`; Rückgabe von `interleave_word()` ist `uint64_t` mit 48 Nutzbits + Bit 48 = S49.
- `AC-FEC-013-2` ✅ — Bit 48 des Rückgabewerts ist stets 0; verifiziert für alle Probe-Worte [TEST FEC-7].
- `AC-FEC-013-3` ✅ — Ausgabe folgt dem Muster A₁, B₁, ..., A₂₄, B₂₄, S₄₉: A-Bits auf geraden, B-Bits auf ungeraden Positionen.
- `AC-FEC-013-4` ✅ — Bit 48 wird in `deinterleave()` nicht ausgewertet; Flippen von S49 ändert das dekodierte Wort nicht [TEST FEC-9].

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Bits pro Wort | 48 | Bits | A.5.2.2.3 |
| Stuff-Bit (S₄₉) | 0 | — | A.5.2.2.3 |
| Symbole pro Wort | 16⅓ | Symbole | A.5.2.2.3 / A.5.1.3 |
| Übertragungssequenz | A₁, B₁, A₂, B₂, ..., A₂₄, B₂₄, S₄₉ | — | A.5.2.2.3 |

---

### FEAT-FEC-004 — 3× Redundanz mit Majority-Vote (TX + RX)

**Setzt um:** REQ-FEC-014–018
**Module:**
- TX: `include/Modem/ale2g_modem.h` / `src/Modem/ale2g_modem.cpp` (`ALE2GModem::update()`)
- RX: `include/FSK/symbol_decoder.h` / `src/FSK/symbol_decoder.cpp` (`decode_word_with_voting()`)

**Design-Entscheidungen:** DD-004
**Status:** implementiert

#### Architektur-Entscheidung: kein FEC-Thema

Das Majority-Voting gehört **nicht** in den FEC-Codec (`ale_fec_codec.cpp`).
Die Spec A.5.2.2.4 schreibt die Verarbeitungsreihenfolge klar vor:

> Demodulator → **2/3 Majority-Voter** → Deinterleaver → FEC-Decoder

Der Voter operiert auf dem rohen Symbol-/Bitstrom und ist eine Aufgabe des
FSK/Symbol-Decoder-Layers. `ale_fec_codec.deinterleave_word()` empfängt
bereits das fertig gevotete 49-Bit-Wort.

Die dreifache **Übertragung** (TX-Seite) gehört **nicht** in die State Machine,
sondern in die `ALE2GModem`-Schicht darunter.

#### TX — 3× Redundanz in ALE2GModem (nicht in der State Machine)

Die State Machine (`ALEStateMachine::transmit_word()`) ruft den `transmit_callback`
**einmal** pro logischem Wort-Slot auf — sie übergibt nur das logische Wort.
Die physikalische Dreifachwiederholung ist ausschließlich Sache der `ALE2GModem`-Schicht.

```cpp
// src/Protocol/Control/ale_state_machine.cpp
void ALEStateMachine::transmit_word(const ALEWord& word) {
    // A.5.2.2.4: 3× Redundanz liegt in ALE2GModem; SM gibt Wort einmalig weiter.
    if (transmit_callback)
        transmit_callback(word);
}
```

Die Integrationsschicht verdrahtet `transmit_callback` mit `ALE2GModem::enqueue_word()`.
`ALE2GModem::update(time_ms)` überträgt pro logischem Wort **einen Block von exakt 49 Symbolen
= 392 ms = Trw** (keine separaten Zeitslots für drei Kopien):

```cpp
// include/Modem/ale2g_modem.h  /  src/Modem/ale2g_modem.cpp
class ALE2GModem {
public:
    void enqueue_word(const ALEWord& word);  // von SM-Callback aufgerufen
    void update(uint32_t current_time_ms);   // von Integrationsschicht, wie SM::update()
    bool is_transmitting() const;
    void set_tx_callback(TxCallback cb);
    void set_word_done_callback(WordDoneCallback cb);
};
```

Timing-Modell — ein Wort = ein 392-ms-Block (REQ-WAVEFORM-008/010):

```
// Integrationsloop (z.B. 1-ms-Ticks im Test):
uint32_t now = get_time_ms();
sm.update(now);     // kann transmit_callback → ALE2GModem::enqueue_word() auslösen
modem.update(now);  // überträgt beim nächsten Tick den 49-Symbol-Block → done_cb_

// Zeitplan für ein Wort eingereiht bei t = T:
//   49 Symbole × 8 ms = 392 ms  →  done_cb_  (t ≈ T + 0 ms, synchron)
// Kein TW_INT_MS, keine separaten Kopie-Zeitstempel.
```

`build_symbols()` wandelt `pending_tx49_` → `symbol_buf_[49]`:
- Die drei Kopien des 49-Bit-Worts werden **mod-49 zyklisch** in 49 Symbole gepackt.
- Jedes Symbol trägt 3 Bits **MSB-first**: `symbol[k] = word_bits[3k..3k+2]` (Indizes mod 49).
- Invariante: `stream[i] = word_bit[i % 49]` → Stride-49-Majority-Vote in jedem Decoder korrekt.

`ALEWord → 24-Bit-Rohwort → Golay-FEC + Bit-Interleaving (ALEFECCodec::interleave_word()) → uint64_t tx49 → build_symbols() → 49 FSK-Symbole (MSB-first)`

#### RX — Majority-Vote im Symbol-Decoder

`SymbolDecoder::decode_word_with_voting()` votiert bit-weise über 3 Kopien des
49-Bit-Worts (Positionen k, k+49, k+98 im Symbolstrom). Nur Bits 0..47 werden
gezählt (`VOTE_BUFFER_LENGTH = 48`); Bit 48 (S49) bleibt 0 und geht nicht in
den `unanimous_count` ein — Spec A.5.2.2.4 spricht ausdrücklich von
**„48 possible votes"**.

Die RX-Seite empfängt **49 Symbole pro Wort-Periode** (Trw = 392 ms). Die korrekte
Bit-Extraktion und der Majority-Vote müssen die MSB-first-Konvention aus `build_symbols()`
berücksichtigen:

```
// Korrekte Extraktion: bit i im Wort-Bitstream = MSB-first aus Symbol i/3
stream[i] = (received_symbol[i/3] >> (2 - i%3)) & 1u   für i = 0..146

// Vote: bits bei Stride 49 sind identisch (mod-49-Invariante des Senders)
voted_bit[i] = majority(stream[i], stream[i+49], stream[i+98])   für i = 0..47

// Ergebnis → ALEFECCodec::deinterleave_word(tx49_voted)
```

> **⚠ OPEN-RX-01** — Die Implementierung in `src/FSK/symbol_decoder.cpp` ist
> noch nicht auf das mod-49-Format aktualisiert. Die alte Pseudoimplementierung
> (`symbol & 1u` auf einem 3×49-Symbol-Flat-Array) ist inkorrekt und muss durch
> die oben beschriebene Extraktion ersetzt werden, bevor der RX-Pfad nutzbar ist.

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FEC-014 — Redundanz-Übertragung

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Jedes 49-Bit-Wort muss redundant (dreifach) übertragen werden, um die Auswirkungen von Fading, Interferenz und Rauschen zu reduzieren.

**Akzeptanzkriterien:**
- `AC-FEC-014-1` ✅ — `ALE2GModem::update()` überträgt pro logischem Wort einen Block von **49 Symbolen = 392 ms = Trw**; die drei Kopien sind mod-49 zyklisch interleaved (stream[i]=word_bit[i%49]); `enqueue_word()` übergibt das Wort einmalig
- `AC-FEC-014-2` ✅ — Dreifachübertragung reduziert Fading-/Interferenz-/Rauscheffekte (spec-konform)

---

### FEAT-FEC-005 — Unanimous-Votes-Erfassung

**Setzt um:** REQ-FEC-019
**Modul:** `include/FSK/symbol_decoder.h`, `src/FSK/symbol_decoder.cpp`, `include/Word/ale_word.h`, `src/Word/ale_word.cpp`
**Design-Entscheidungen:** DD-004
**Status:** implementiert

#### Implementierung

`SymbolDecoder::decode_word_with_voting()` zählt während des Majority-Votings für Bits 0..47 einstimmige Stimmen und gibt sie als `uint8_t` (0..48) zurück. Bit 48 (S49) wird nicht gezählt (kein Vote). Der Rückgabewert wird direkt in `ALEWord::unanimous_votes` (`uint8_t`) gespeichert:

```cpp
// src/Word/ale_word.cpp
output.unanimous_votes = SymbolDecoder::decode_word_with_voting(symbols, transmitted);
```

```cpp
// include/Word/ale_word.h
uint8_t unanimous_votes;  // 2/3-voter unanimous count 0..48 (A.5.2.6.3)
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FEC-019 — Unanimous-Votes-Erfassung

**Spec-Referenz:** A.5.2.2.4
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die Anzahl der einstimmigen Stimmen der 48 möglichen Stimmen, die mit diesem Majority-Wort verbunden sind, müssen temporär für die Verwendung gemäß A.5.2.6 zurückgehalten werden.

**Akzeptanzkriterien:**
- `AC-FEC-019-1` — Die Anzahl der einstimmigen Stimmen muss für die 48 Bits erfasst werden
- `AC-FEC-019-2` — Die erfassten Werte müssen temporär gespeichert werden

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-07 — Der Verweis auf "A.5.2.6" für die Verwendung der unanimous votes ist enthalten, aber der Inhalt dieses Abschnitts wurde nicht geliefert. Die genaue Verwendung ist hier nicht spezifiziert.
- ANNAHME: Das Präfix "FEC" wurde gewählt, da der Text aus Abschnitt A.5.2.2 stammt.

---

### FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis

**Setzt um:** REQ-FRAME-001
**Modul:** `include/Protocol/Control/ale_state_machine.h`,
           `src/Protocol/Control/ale_state_machine.cpp`,
           `include/Modem/ale2g_modem.h`,
           `src/Modem/ale2g_modem.cpp`
**Design-Entscheidungen:** DD-006, DD-009, DD-013
**Status:** implementiert

#### Technischer Entwurf

Frame = Calling Cycle + [optional: Message] + Conclusion

Jeder Slot = 1 Trw = 3 × dasselbe Wort (intern in ALE2GModem, DD-013)
ALEStateMachine zählt ausschließlich Trw-Slots, niemals Tw-Schritte.

```
Tcc   = Tsc + Tlc
Tc    = wpa × Trw          (wpa = words_for_address())
Tlc   = 2 × Tc
Tsc   = C × 2 × Trw        (C = target_scan_channels)
```

Ablauf pro Trw-Slot (callback-getrieben, DD-009, DD-013):

```
ALEStateMachine::update()
  → words_pending == 0 && current_time_ms >= first_call_tx_ms + call_cycle_count × Trw_ms
  → transmit_word(next_logical_word)          [words_pending += 1]
  → ALE2GModem enqueues word, sends 3× copies
  → ALE2GModem::done_cb_() → sm.on_word_complete()
      call_cycle_count    += 1
      call_cycles_in_phase += 1
      [Phase transition if threshold reached]
```

**ALE2GModem Word-Queue:** Multi-Wort-Adressen (z. B. K6KB = TO + DATA)
rufen `transmit_word()` mehrfach auf. `ALE2GModem::enqueue_word()` stellt
überlaufende Wörter in eine interne Queue; `done_cb_` feuert nach jedem
einzelnen Wort (nicht erst wenn die Queue leer ist). `words_pending` in
`ALEStateMachine` hält `handle_calling()` solange inaktiv, bis alle
Wörter einer Sequenz abgearbeitet sind.

**Verdrahtung (Integration):**
```cpp
modem.set_word_done_callback([&sm]{ sm.on_word_complete(); });
sm.set_transmit_callback([&modem](const ALEWord& w){ modem.enqueue_word(w); });
```

Die dreifache Redundanz ist ausschließlich in `ALE2GModem` implementiert.
`ALEStateMachine` kennt weder die Zahl 3 noch Tw-Schritte (DD-013).

#### Verifikation

| Acceptance Criterion | Test-Case | Status |
|---|---|---|
| AC-FRAME-001-2 | `tests/test_frame.cpp`: `test_ac_001_2_modem_3x_identical_copies` — jedes logische Wort erzeugt exakt 3 aufeinanderfolgende identische Symbol-Pakete im Ausgabe-Stream von `ALE2GModem`; kein anderes Modul erzeugt Wiederholungen | implementiert |
| AC-FRAME-001-2 | `tests/test_frame.cpp`: `test_ac_001_2_cycle_count_increments_in_callback` — `ALEStateMachine` ruft `transmit_word()` pro logischem Wort genau 1× auf; `call_cycle_count` steigt um 1 pro `on_word_complete()`-Callback, nicht pro Tw | implementiert |
| AC-FRAME-001-3 | `tests/test_frame.cpp`: `test_ac_001_3_phase_sequence_via_callback_only` — Inner-State-Sequenz ist ausschließlich `SCANNING_CALL → LEADING_CALL → CONCLUSION`; jeder Wechsel erfolgt nur im `on_word_complete()`-Callback | implementiert |
| AC-FRAME-001-4 | `tests/test_frame.cpp`: `test_ac_001_4_slot_timing_formula` — gemessene Slot-Zeitpunkte entsprechen `first_call_tx_ms + call_cycle_count × Trw_ms`; Abweichung < 1 ms | implementiert |
| AC-FRAME-001-4 | `tests/test_frame.cpp`: `test_ac_001_4_timing_formulas` — `Tsc = C × 2 × Trw_ms`, `Tlc = 2 × wpa × Trw_ms`, `Tcc = Tsc + Tlc`; alle drei Werte in Trw-Einheiten verifiziert | implementiert |

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig
> umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind
> verbindliche Implementierungsvorgaben.

##### REQ-FRAME-001 — Frame-Grundstruktur und Wortbasis

**Spec-Referenz:** A.5.2.5
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Alle ALE-Übertragungen basieren auf den Tönen, dem Timing
sowie den Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3. Jede Aussendung
ist als „Frame" aufgebaut, der aus fortlaufenden redundanten Wörtern in
gültigen Sequenzen besteht. Ein Frame setzt sich aus genau drei grundlegenden
Abschnitten zusammen: Calling Cycle, Message und Conclusion. Die zulässigen
Formate und Abschnittsgrenzen sind in Figure A-14, Table A-VII und A.5.5
festgelegt.

**Akzeptanzkriterien:**
- `AC-FRAME-001-1` — Jede ALE-Aussendung verwendet ausschließlich Töne,
  Timing sowie Bit- und Wortstrukturen gemäß A.5.1 und A.5.2.3.
- `AC-FRAME-001-2` — Jeder Frame besteht ausschließlich aus fortlaufenden
  redundanten Wörtern in gültigen Sequenzen. Redundant bedeutet: jedes
  logische Wort wird exakt 3× gesendet (Trw = 3 × Tw, DD-013).
- `AC-FRAME-001-3` — Jeder Frame enthält die drei Abschnitte Calling Cycle,
  Message und Conclusion in genau dieser Reihenfolge (wobei Message optional
  ist).
- `AC-FRAME-001-4` — Die Konstruktion eines Frames entspricht den Vorgaben
  aus Figure A-14, Table A-VII und den Format-Beschreibungen aus A.5.5.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-16 — Figure A-14 und Table A-VII wurden nicht geliefert; die darin
  enthaltenen gültigen Sequenz- und Größenregeln müssen bei Vorlage dieser
  Abschnitte ergänzt werden.
- OPEN-17 — A.5.5 (Frame-Formate) wurde nur teilweise geliefert; vollständige
  Format-Vorgaben sind dort zu verifizieren.

---

### FEAT-FRAME-002 — Scanning Call — Individual Call

**Setzt um:** REQ-FRAME-002, REQ-FRAME-003 (Individual Call), REQ-FRAME-005, REQ-FRAME-006
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-006, DD-007
**Status:** implementiert

**Scope-Abgrenzung:**
- Net Call Scanning (TO mit Netzadresse) → `FEAT-FRAME-007`
- Group Call Scanning (THRU/REP) → separates Feature (noch nicht angelegt)

#### Technischer Entwurf
- Individual Call: `TO` mit ersten 3 Chars, kein DATA/REP
- `tsc_slots = target_scan_channels × 2`
- Transition zu LEADING_CALL wenn `call_cycles_in_phase >= tsc_slots`

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature im Individual-Call-Scope vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FRAME-002 — Calling Cycle: Gliederung und Bestandteile

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Der initiale Abschnitt aller Frames — mit Ausnahme von Sounds — ist der Calling Cycle (Tcc). Dieser besteht aus zwei Teilen: dem Scanning Call (Tsc) und dem Leading Call (Tlc). Der Calling Cycle endet mit dem Beginn des Message-Abschnitts oder, sofern kein Message-Abschnitt vorhanden, mit dem Beginn des Conclusion-Abschnitts.

**Akzeptanzkriterien:**
- `AC-FRAME-002-1` ✅ — Jeder Frame (außer Sounding) beginnt mit einem Calling Cycle.
- `AC-FRAME-002-2` ✅ — Der Calling Cycle besteht aus genau zwei Teilen: Scanning Call und Leading Call.
- `AC-FRAME-002-3` ✅ — Der Calling Cycle endet mit dem Beginn des Message- oder, bei fehlendem Message-Abschnitt, des Conclusion-Abschnitts.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-18 — Die genaue Definition von „Sounds" als Frame-Ausnahme verweist auf A.5.3; Inhalt dort noch nicht vollständig extrahiert (vgl. REQ-SOUND-001).

---

##### REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt (Individual Call)

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** implementiert (Individual Call)

**Anforderung:** Der Scanning Call setzt sich bei Individual Calls aus TO-Wörtern zusammen, die ausschließlich das erste Wort der Adresse der gerufenen Station enthalten. Der Satz unterschiedlicher Adress-Erstworte (Tcl) darf zur Abdeckung der Suchlaufperiode (Ts) so oft wie nötig wiederholt werden.

**Scope-Hinweis:** Net Call (TO mit Netzadresse) → `FEAT-FRAME-007`. Group Call (THRU/REP) → separates Feature.

**Akzeptanzkriterien:**
- `AC-FRAME-003-1` ✅ — Bei Individual Calls enthält der Scanning Call ausschließlich TO-Wörter mit dem ersten Adresswort der gerufenen Station. *(Net Call → FEAT-FRAME-007)*
- `AC-FRAME-003-3` ✅ — Der Scanning Call enthält nur das erste Wort der Zieladresse, keine vollständige Adresse.
- `AC-FRAME-003-4` ✅ — Der Satz der Adress-Erstworte (Tcl) wird so oft wiederholt, bis die Suchlaufperiode (Ts) überschritten wird.
- `AC-FRAME-003-5` ✅ — Die Dauer des Scanning Call (Tsc) ist ≥ Ts. *(Tsc = target_scan_channels × 2 × Trw)*

**Entfallene ACs (verschoben):**
- `AC-FRAME-003-2` — Group Call THRU/REP → separates Feature

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**
- Konkrete Wiederholungszählung der Tcl-Blöcke und Abbruchbedingung ≥ Ts — implements REQ-FRAME-003

---

##### REQ-FRAME-005 — Frame-Synchronisation: Kein dediziertes Sync-Word

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein ALE-Frame besitzt kein dediziertes „Flag Word" oder „Sync Word" für die Frame-Synchronisation. Empfangsstationen können den ALE-Signalempfang und die Dekodierung an jedem beliebigen Punkt nach dem Beginn der Übertragung aufnehmen.

**Akzeptanzkriterien:**
- `AC-FRAME-005-1` — Der Sender fügt kein dediziertes Flag- oder Sync-Wort zur Frame-Synchronisation ein.
- `AC-FRAME-005-2` — Eine Empfangsstation ist in der Lage, das ALE-Signal an einem beliebigen Punkt nach Übertragungsbeginn zu synchronisieren und zu lesen.

---

##### REQ-FRAME-006 — Sender-Hochlaufzeit: 90-Prozent-Leistung

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Sender muss innerhalb von 2,5 ms nach der ersten Tonübertragung nach Rufinitiierung mindestens 90 Prozent der gewählten HF-Sendeleistung erreicht haben. Die zulässige Verzögerung von 2,5 ms gilt zusätzlich zu der in 5.3.5.1 festgelegten Attackzeit. Nichterfüllung der 90-%-Bedingung beeinträchtigt die Verlinkungswahrscheinlichkeit; die Bedingung gilt als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist.

<!-- FEHLER: "Table A-I" in diesem Absatz ist falsch. Table A-I enthält die Occupancy Detection
     Probability (Belegtheitserkennung, REQ-GEN-009) — nicht das Verlinkungswahrscheinlichkeitskriterium.
     Die Linking Probability ist laut REQ-GEN-010 in Table A-II definiert. Außerdem: Standard A.5.2.5.1
     NOTE 2 enthält keine Compliance-Äquivalenzklausel via Tabellenverweis; dieser Passus konnte im
     Standard-JSON nicht verifiziert werden.
     NICHT PRÜFBAR: A.5.2.5.1 vollständiger Wortlaut (inkl. aller Notes) nicht im JSON-Extrakt. -->

**Akzeptanzkriterien:**
- `AC-FRAME-006-1` — Die Sendeleistung beträgt spätestens 2,5 ms nach dem ersten ALE-Ton mindestens 90 % der gewählten HF-Leistung.
- `AC-FRAME-006-2` — Die zulässige 2,5-ms-Verzögerung ist additiv zur Attackzeit aus 5.3.5.1.
- `AC-FRAME-006-3` — Die Einhaltung gilt auch dann als erfüllt, wenn das Verlinkungswahrscheinlichkeitskriterium gemäß Table A-I erfüllt ist (Compliance-Äquivalenz).
  <!-- NICHT PRÜFBAR: Table-Referenz vermutlich falsch (A-I vs. A-II), Compliance-Äquivalenzklausel
       im Standard-JSON nicht auffindbar. Quelle-Abschnitt A.5.2.5.1 unvollständig im Extrakt. -->

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Permissible power delay after first tone | 2,5 | ms | A.5.2.5.1 Note 2 |
| Mindest-Leistungsanteil bei Erstem Ton | 90 | % | A.5.2.5.1 |

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-20 — Der referenzierte Abschnitt 5.3.5.1 (allowable attack time) wurde nicht geliefert; die zusätzlich erlaubte Attackzeit ist dort zu verifizieren.
  <!-- NICHT PRÜFBAR: Abschnitt 5.3.5.1 nicht im Standard-JSON enthalten. -->
- OPEN-21 — ~~Table A-I (Verlinkungswahrscheinlichkeitskriterium) wurde nicht geliefert~~ → **AUFGELÖST:** Table A-I ist im Standard-JSON vorhanden und enthält die **Occupancy Detection Probability** (Belegtheitserkennung, REQ-GEN-009), **nicht** das Verlinkungskriterium. Der Verweis in REQ-FRAME-006 auf "Table A-I als Verlinkungskriterium" ist daher falsch (→ FEHLER-Marker in REQ-FRAME-006). Das eigentliche Verlinkungskriterium befindet sich in Table A-II (REQ-GEN-010).

---

### FEAT-FRAME-003 — Leading Call

**Setzt um:** REQ-FRAME-004
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-006, DD-007
**Status:** implementiert

#### Technischer Entwurf
- Sequenz: `TO, DATA, REP, DATA, REP` (je nach Adresslänge, max 5 Wörter)
- `total_slots = seq.size() × 2`
- `call_cycles_in_phase % seq.size()` = aktuelles Wort
- Transition zu CONCLUSION wenn `call_cycles_in_phase >= total_slots`

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FRAME-004 — Leading Call: Zusammensetzung und Adressinhalt

**Spec-Referenz:** A.5.2.5.1 / Figure A-15
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Der Leading Call besteht aus TO- und möglicherweise DATA- und REP-Wörtern, die die vollständige Adresse(n) der gerufenen Station(en) enthalten. Der Leading Call beginnt mit der Einleitung des Leading Call und endet mit dem Beginn des Message-Abschnitts bzw. des Conclusion-Abschnitts. Die Verwendung von REP und DATA ist in A.5.2.4 festgelegt.

**Akzeptanzkriterien:**
- `AC-FRAME-004-1` — Der Leading Call enthält die vollständige(n) Zieladresse(n) einschließlich aller Erweiterungswörter (DATA, REP).
- `AC-FRAME-004-2` — Der Leading Call beginnt mit dem Einleitungszeitpunkt des Leading Call und endet spätestens mit dem Beginn des Message- oder Conclusion-Abschnitts.
- `AC-FRAME-004-3` — Die Verwendung von DATA- und REP-Wörtern im Leading Call entspricht den Regeln aus A.5.2.4.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-19 — Figure A-15 wurde nicht geliefert; die visuelle Darstellung des Leading Call kann nicht verifiziert werden.

---

### FEAT-FRAME-004 — Message-Abschnitt

**Setzt um:** REQ-FRAME-007, REQ-FRAME-008, REQ-FRAME-009
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf

```cpp
enum class MessagePhase : uint8_t { QUICK_ID, CMD_SEQUENCE, DONE };

struct MessageSection {
    bool         has_quick_id;     // FROM + eigene Adresse
    std::vector<ALEWord> words;    // CMD [DATA REP ...]
    uint32_t     elapsed_ms;
};
```

Zustandsübergänge:
```
LEADING_CALL → [MESSAGE] → CONCLUSION
               ↑
          (wenn message_section nicht leer)

Zeitlimit Tm_max_ms = 11760 ms (DD-010).
AQC-Sonderregel: Message nur wenn outer_state_ == LINKED.
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FRAME-007 — Quick-ID: Optionaler Transmitter-Identifier am Beginn des Message-Abschnitts

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

##### REQ-FRAME-008 — Message-Abschnitt: Struktur und Zusammensetzung

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

##### REQ-FRAME-009 — Message-Abschnitt: AQC-ALE-Sonderregel

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

### FEAT-FRAME-005 — Conclusion

**Setzt um:** REQ-FRAME-010, REQ-FRAME-011
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-006, DD-007
**Status:** implementiert

#### Technischer Entwurf
```cpp
// build_conclusion_sequence(self_addr, is_reject)
// → TIS/TWAS + DATA/REP-Kette (wie Leading Call, aber mit self_addr)
// REP darf NICHT als erstes Erweiterungswort nach TIS/TWAS stehen
// → DATA muss immer das erste Erweiterungswort sein

// RX-Fenster öffnet am Trw-Grid-Boundary nach dem letzten Conclusion-Wort:
rx_open_ms = first_call_tx_ms + call_cycle_count × WORD_DURATION_MS;
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FRAME-010 — Conclusion: Struktur und Worttypen

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

##### REQ-FRAME-011 — Conclusion bei Sounds und Ausnahme-Frames

**Spec-Referenz:** A.5.2.5.3 / A.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sounding und Exception-Frames beginnen unmittelbar mit TIS- oder TWAS-Wörtern, ohne vorangehenden Calling Cycle oder Message-Abschnitt, gemäß den Regeln in A.5.3. Ein REP-Wort darf nicht unmittelbar auf ein TIS- oder TWAS-Wort folgen.

**Akzeptanzkriterien:**
- `AC-FRAME-011-1` — Bei Sounding und Exception-Frames beginnt die Aussendung direkt mit TIS- oder TWAS-Wörtern.
- `AC-FRAME-011-2` — Ein REP-Wort folgt niemals unmittelbar auf ein TIS- oder TWAS-Wort.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-26 — A.5.3 (Sounds) wurde noch nicht vollständig extrahiert; die vollständigen Regeln für den Sonderfall Sound-Frame sind dort zu vervollständigen (vgl. REQ-SOUND-001).

---

### FEAT-FRAME-006 — Gültige Sequenzen & Frame-Limits

**Setzt um:** REQ-FRAME-012, REQ-FRAME-013
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Frame-Limits (DD-010)
| Limit | Wert | Einheit |
|---|---|---|
| Adress-Größe max (Ta max) | 1960 | ms (5 Wörter) |
| Call time max (Tc) | 4704 | ms (12 Wörter) |
| Scan period max (Ts max) | 50000 | ms |
| Message basic max (Tm) | 11760 | ms |
| Termination max (Tx) | 1960 | ms |

Guard: `call_cycles_in_phase > 12` im LEADING_CALL → Error-Log + Abbruch.

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-FRAME-012 — Gültige Wortsequenzen im Frame

**Spec-Referenz:** A.5.2.5.4 / Figure A-18 / Figure A-19 / Figure A-20
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die acht ALE-Worttypen dürfen zur Konstruktion von Frames und Messages nur in den Sequenzen verwendet werden, die in Figure A-18, Figure A-19 und Figure A-20 erlaubt sind.

**Akzeptanzkriterien:**
- `AC-FRAME-012-1` — Jede gesendete Wortsequenz in einem Frame oder einer Message entspricht den erlaubten Sequenzen aus Figure A-18, Figure A-19 und Figure A-20.
- `AC-FRAME-012-2` — Jede empfangene Wortsequenz wird gegen die erlaubten Sequenzen validiert; unzulässige Sequenzen werden verworfen.

**OFFENE PUNKTE / ANNAHMEN:**
- OPEN-27 — Figure A-18, Figure A-19 und Figure A-20 wurden nicht geliefert; die vollständigen Sequenz-Regeln können nicht extrahiert werden. Dieser REQ bleibt bis zur Lieferung dieser Figures unvollständig.

---

##### REQ-FRAME-013 — Frame-Größen- und Zeitgrenzen (Table A-XII)

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

### FEAT-FRAME-007 — Scanning Call — Net Call

**Setzt um:** REQ-FRAME-003 (Net Call)
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-006, DD-007
**Status:** offen

**Abhängigkeiten:** FEAT-FRAME-002

#### Technischer Entwurf
- Net Call Scanning: `TO` mit ersten 3 Chars der Netzadresse, kein DATA/REP (identisch zu Individual Call)
- `NET_CALL_STUB` in `enter_state(CALLING)` durch Routing in `CallingPhase::SCANNING_CALL` ersetzen
- `tsc_slots = target_scan_channels × 2` (wie Individual Call)
- Im LEADING_CALL weiterhin `TWAS` statt `TO` (bereits implementiert in `build_leading_call_word()`)

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature im Net-Call-Scope umgesetzt.

##### REQ-FRAME-003 — Scanning Call: Zusammensetzung und Adressinhalt (Net Call)

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Scanning Call setzt sich bei Net-Calls aus TO-Wörtern zusammen, die ausschließlich das erste Wort der Adresse des Netzes enthalten. Der Satz unterschiedlicher Adress-Erstworte (Tcl) darf zur Abdeckung der Suchlaufperiode (Ts) so oft wie nötig wiederholt werden.

**Scope-Hinweis:** Individual Call Scanning → `FEAT-FRAME-002`. Group Call (THRU/REP) → separates Feature.

**Akzeptanzkriterien:**
- `AC-FRAME-003-1-NET` — Bei Net-Calls enthält der Scanning Call ausschließlich TO-Wörter mit dem ersten Adresswort des Netzes.
- `AC-FRAME-003-3-NET` — Der Scanning Call enthält nur das erste Wort der Netzadresse, keine vollständige Adresse.
- `AC-FRAME-003-4-NET` — Der Satz der Adress-Erstworte (Tcl) wird so oft wiederholt, bis die Suchlaufperiode (Ts) überschritten wird.
- `AC-FRAME-003-5-NET` — Die Dauer des Scanning Call (Tsc) ist ≥ Ts.

---

### FEAT-SYNC-001 — Trw-Grid (Sendeseitiger Wortphasen-Anker)

**Setzt um:** REQ-SYNC-001–004
**Modul:** `include/Protocol/ale_state_machine.h`, `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-006
**Status:** implementiert

#### Technischer Entwurf
```cpp
const uint32_t next_slot_ms = first_call_tx_ms
                            + call_cycle_count * ALETimingConstants::Trw_ms;
if (current_time_ms >= next_slot_ms) { transmit_word(); ++call_cycle_count; }
// first_call_tx_ms: EINMALIG in enter_state(CALLING) gesetzt, danach read-only
// call_cycle_count: NIEMALS zwischen Phasen zurückgesetzt
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-SYNC-001 — Asynchroner Systembetrieb und Frame-interne Word-Sync

**Spec-Referenz:** A.5.2.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System ist inhärent asynchron und erfordert keine systemweite Synchronisation, ist jedoch mit solchen Verfahren kompatibel. Die innerhalb eines Frames eingebettete Timing- und Strukturinformation stellt die notwendigen Ankerpunkte bereit, um die Wortsynchronisation (Word Sync) während Linking-, Orderwire- und Anti-Interferenz-Funktionen zu erzielen und aufrechtzuerhalten.

**Akzeptanzkriterien:**
- `AC-SYNC-001-1` — Das System erfordert keine systemweite externe Synchronisation und muss auch ohne diese korrekt arbeiten.
- `AC-SYNC-001-2` — Das System ist mit Verfahren zur systemweiten Synchronisation kompatibel, sofern solche eingesetzt werden.
- `AC-SYNC-001-3` — Die innerhalb eines Frames eingebettete Timing- und Strukturinformation wird zur Erzielung und Aufrechterhaltung der Word-Sync während Linking-, Orderwire- und Anti-Interferenz-Funktionen genutzt.

---

### FEAT-SYNC-002 — Empfangsseitige Wortsynchronisation

**Setzt um:** REQ-SYNC-005
**Modul:** `src/FSK/ale_waveform.cpp`
**Status:** geplant

#### Technischer Entwurf
**Design-Entscheidung:** Sliding-Window-Energie-Detektor (bevorzugt gegenüber Korrelation, da einfacher und ausreichend für bekannte Symbolrate).

```cpp
// Sliding Window über 64 Samples:
// Energy[n] = Σ pcm[n-k]² für k=0..63
// Symbolgrenze bei lokalem Minimum von Energy (Übergang zwischen Tönen)
// Word-Boundary nach je 49 Symbolen (= 49 × 64 = 3136 Samples)

class WordSyncDetector {
public:
    // Gibt true zurück wenn Word-Boundary erkannt
    bool process_sample(float pcm_sample);
    bool is_synced() const;
    uint32_t word_phase_ms() const;  // Zeitpunkt des letzten Word-Starts
private:
    std::array<float, 64> window_;
    uint8_t  symbol_count_;   // 0..48
    bool     synced_;
    uint32_t last_word_start_sample_;
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-SYNC-005 — Empfangsdemodulator: Signalakquisition, Tracking und Demodulation

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

### FEAT-SYNC-003 — Synchronisationskriterien & Schwellwerte

**Setzt um:** REQ-SYNC-006, REQ-SYNC-007
**Modul:** `src/FSK/ale_waveform.cpp`, `src/FEC/ale_fec_codec.cpp`
**Status:** geplant

#### Technischer Entwurf

```cpp
struct WordAcceptanceCriteria {
    uint8_t  min_unanimous_votes;   // konfigurierbar, z.B. 32 von 48
    uint8_t  golay_mode;            // 0=3/4err, 1=2/5err, 2=1/6err, 3=0/7err
    bool     require_valid_preamble;
    bool     require_basic38_chars;
    bool     auto_adapt;            // DO: automatische Anpassung
};

// Alle 9 Kriterien aus A.5.2.6.3:
bool accept_word(const ALEWord& w,
                 const WordAcceptanceCriteria& crit,
                 const WordHistory& history);
```

Golay-Modi:
| Modus | Korrigiert | Erkennt | Konfiguration |
|---|---|---|---|
| 0 | 3 Fehler | 4 Fehler | `golay_mode=0` |
| 1 | 2 Fehler | 5 Fehler | `golay_mode=1` |
| 2 | 1 Fehler | 6 Fehler | `golay_mode=2` |
| 3 | 0 Fehler | 7 Fehler | `golay_mode=3` |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-SYNC-006 — Synchronisationskriterien für jedes ALE-Wort

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

##### REQ-SYNC-007 — Herstelleroptimierbare Parameter: Unanimous-Vote-Schwellwert und Golay-Modus

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

### FEAT-SOUND-001 — Single-Channel Sounding (Grundfunktion)

**Setzt um:** REQ-SOUND-002–005
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf

Sounding ist eine einseitige, periodische Aussendung auf einem einzelnen, unbesetzten Kanal. Der Sound-Frame besteht aus der Conclusion-Sequenz mit der eigenen Adresse und verwendet entweder `TIS` oder `TWAS`. Es gibt keinen Calling Cycle und keine Message Section.

```text
Sound-Frame = Conclusion(TIS oder TWAS, self_addr)
Trs_min = 784 ms = 2 × Trw
```

Die Ausführung ist timergetrieben. Ein periodischer Sounding-Timer löst die Überprüfung aus, ob ein Kanal zum Sounding vorgesehen ist und ob auf diesem Kanal seit der letzten eigenen Aussendung noch keine Sounding-Wiederholung erforderlich ist. Sounding darf nur auf einem klaren Kanal stattfinden.

Wenn die Station Rufe akzeptieren soll, wird `TIS` verwendet. Wenn die Station Rufe ablehnen soll, wird `TWAS` verwendet. Empfangene Soundings werden unabhängig von der eigenen Sendebereitschaft verarbeitet: die Adresse wird angezeigt und, sofern verfügbar, in Connectivity- und LQA-Memory gespeichert.

#### Semantischer Ablauf für Implementierung

##### Sequenz 1 — Sounding-Timer-Auslösung

1. Der Sounding-Timer läuft ab.
2. Die Station prüft, ob Sounding aktiviert ist.
3. Die Station prüft, ob für den aktuellen Kanal eine erneute Aussendung erforderlich ist.
4. Falls auf dem Kanal kürzlich bereits gesendet wurde, wird die Aussendung bis zum Ablauf des Sounding-Intervalls unterdrückt.
5. Andernfalls wird der Sounding-Vorgang auf dem vorgesehenen Kanal vorbereitet.

---

##### Sequenz 2 — Kanalfreiheitsprüfung

1. Vor jeder Aussendung wird der Kanal auf Belegung geprüft.
2. Ist der Kanal belegt, wird keine Aussendung gestartet.
3. Der Kanal wird verworfen oder zu einem späteren Zeitpunkt erneut berücksichtigt.
4. Ist der Kanal frei, wird die Sounding-Aussendung gestartet.

---

##### Sequenz 3 — Single-Channel Sounding ausführen

1. Die Station sendet nur die Conclusion-Sequenz.
2. Die Conclusion besteht aus `TIS self_addr` oder `TWAS self_addr`.
3. Es werden keine Calling-Cycle-Sequenzen und keine Message Sections gesendet.
4. Die Aussendung muss die normativ geforderte redundante Mindestdauer erfüllen.
5. Nach Abschluss der Aussendung kehrt die Station in den normalen Betriebszustand zurück.

---

##### Sequenz 4 — Empfangene Soundings verarbeiten

1. Beim Empfang eines Sounding-Signals wird die Adresse der sendenden Station dekodiert.
2. Die Adresse wird dem Bediener angezeigt.
3. Falls konfiguriert, wird ein Alert ausgelöst.
4. Falls Connectivity-Memory vorhanden ist, wird die Verbindungsmöglichkeit gespeichert.
5. Falls LQA-Memory vorhanden ist, wird die LQA-Information gespeichert.
6. Die Information wird für spätere Linkentscheidungen und Verbindungsversuche verwendet.
7. Der Empfang eines Soundings führt nicht automatisch zu einem Handshake oder Call.

---

##### Sequenz 5 — Redundante Soundings unterdrücken

1. Vor einer erneuten Aussendung wird die letzte eigene Aktivität auf dem betreffenden Kanal geprüft.
2. Gilt die letzte Aktivität noch als innerhalb des gültigen Sounding-Intervalls, wird kein neues Sounding ausgelöst.
3. Dies gilt auch dann, wenn die letzte Aktivität keine explizite Sounding-Aussendung, sondern eine andere eigene Transmission war.
4. Erst nach Ablauf des Sounding-Intervalls darf der Kanal erneut für Sounding verwendet werden.

---

##### Sequenz 6 — Optionaler Handshake nach TIS

1. Wenn die Station `TIS` verwendet, signalisiert sie Rufannahmebereitschaft.
2. Wird nach dem Sounding ein Call empfangen, kann ein optionaler Handshake eingeleitet werden.
3. Der Handshake folgt dem regulären Call- bzw. Response-Protokoll.
4. Erfolgt kein Call, verlässt die Station den Kanal nach Ablauf der vorgesehenen Wartephase.

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-SOUND-002 — Sounding: Einseitige, periodische Übertragung auf unbesetzten Kanälen

**Spec-Referenz:** A.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Sounding ist eine einseitige, einwegige Übertragung, die in periodischen Intervallen auf unbesetzten Kanälen durchgeführt wird. Sounding ist keine interaktive, zweiseitige Technik wie Polling. Allerdings gibt die Identifizierung einer Station durch Hören ihres Sounding-Signals eine hohe Wahrscheinlichkeit (aber keine Garantie) für zweiseitige Konnektivität und kann passiv am Empfänger erfolgen. Sounding verwendet die standardmäßige ALE-Signalisierung; jede Station kann Sounding-Signale empfangen. Als Minimum muss die (Adresse)Information dem Bediener angezeigt werden und für Stationen mit Konnektivitäts- und LQA-Speichern muss die Information gespeichert und später für den Linkaufbau verwendet werden. Wenn eine Station auf einem Kanal, der zum Sounding vorgesehen ist, kürzlich Sendungen hatte, muss es nicht notwendig sein, erneut auf diesem Kanal zu senden, bis das Sounding-Intervall, restartet von diesen letzten Sendungen, abgelaufen ist. Wenn ein Netz (oder eine Gruppe) von Stationen abgehört wird, dienen ihre Antworten als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger. Alle Stationen müssen in der Lage sein, periodisches Sounding auf vorab vereinbarten Kanälen durchzuführen. Die Sounding-Fähigkeit kann vom Bediener oder Controller selektiv aktiviert werden, und der Zeitraum zwischen den Sounds kann vom Bediener oder Controller gemäß den Systemanforderungen einstellbar sein. Wenn verfügbar und nicht anderweitig zugewiesen oder vom Bediener oder Controller gerichtet, müssen alle ALE-Stationen die Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

**Akzeptanzkriterien:**

* `AC-SOUND-002-1` — Sounding ist eine einseitige, einwegige Übertragung.
* `AC-SOUND-002-2` — Sounding wird in periodischen Intervallen auf unbesetzten Kanälen durchgeführt.
* `AC-SOUND-002-3` — Sounding ist keine interaktive, zweiseitige Technik.
* `AC-SOUND-002-4` — Die Identifizierung durch Hören des Sounding-Signals gibt eine hohe Wahrscheinlichkeit für zweiseitige Konnektivität.
* `AC-SOUND-002-5` — Sounding kann passiv am Empfänger erfolgen.
* `AC-SOUND-002-6` — Sounding verwendet die standardmäßige ALE-Signalisierung.
* `AC-SOUND-002-7` — Jede Station kann Sounding-Signale empfangen.
* `AC-SOUND-002-8` — Die Adresse-Information muss dem Bediener als Minimum angezeigt werden.
* `AC-SOUND-002-9` — Stationen mit Konnektivitäts- und LQA-Speichern müssen die Information speichern und später für den Linkaufbau verwenden.
* `AC-SOUND-002-10` — Wenn eine Station kürzlich auf einem zum Sounding vorgesehenen Kanal gesendet hat, kann das erneute Senden entfallen, bis das Sounding-Intervall seit den letzten Sendungen abgelaufen ist.
* `AC-SOUND-002-11` — Antworten eines abgehörten Netzes (oder einer Gruppe) dienen als Sounding-Signale für die anderen Netz-(oder Gruppen-)Empfänger.
* `AC-SOUND-002-12` — Alle Stationen müssen periodisches Sounding auf vorab vereinbarten Kanälen durchführen können.
* `AC-SOUND-002-13` — Die Sounding-Fähigkeit kann selektiv vom Bediener oder Controller aktiviert werden.
* `AC-SOUND-002-14` — Der Zeitraum zwischen den Sounds kann vom Bediener oder Controller einstellbar sein.
* `AC-SOUND-002-15` — Verfügbare Stationen müssen Adressen aller gehörter Stationen automatisch und temporär anzeigen, mit einem vom Bediener wählbaren Alert.

---

##### REQ-SOUND-003 — Single-Channel Sound Frame Structure

**Spec-Referenz:** A.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Single-Channel-Sounding-Struktur ist im Wesentlichen identisch mit der Basis-Call-Struktur, benötigt jedoch keinen Calling Cycle und keine Message Section. Es ist nur die Conclusion zu senden, die die sendende Station identifiziert. Als Schlusswort ist entweder `TIS` oder `TWAS` zu verwenden, niemals beide. Die minimale redundante Sound-Zeit beträgt `Trs = 784 ms`.

**Akzeptanzkriterien:**

* `AC-SOUND-003-1` — Das Sound-Frame enthält keinen Calling Cycle.
* `AC-SOUND-003-2` — Das Sound-Frame enthält keine Message Section.
* `AC-SOUND-003-3` — Es wird nur die Conclusion-Sequenz gesendet.
* `AC-SOUND-003-4` — Die Conclusion enthält die sendende Station als Adresse.
* `AC-SOUND-003-5` — Es wird entweder `TIS` oder `TWAS` verwendet.
* `AC-SOUND-003-6` — `TIS` und `TWAS` dürfen nicht gleichzeitig verwendet werden.
* `AC-SOUND-003-7` — `Trs` erfüllt mindestens `784 ms`.

---

##### REQ-SOUND-004 — Single-Channel TIS/TWAS Semantics

**Spec-Referenz:** A.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Wahl zwischen `TIS` und `TWAS` bestimmt, ob potenzielle Anrufer ermutigt oder ignoriert werden. Wenn `TIS` verwendet wird, darf die Station nach dem Sounding einen optionalen Handshake akzeptieren. Wenn `TWAS` verwendet wird, soll die Station unmittelbar in den normalen verfügbaren Zustand zurückkehren.

**Akzeptanzkriterien:**

* `AC-SOUND-004-1` — `TIS` signalisiert Rufannahmebereitschaft.
* `AC-SOUND-004-2` — `TWAS` signalisiert Rufablehnung bzw. Nichtverfügbarkeit.
* `AC-SOUND-004-3` — Nach `TIS` kann ein optionaler Handshake erfolgen.
* `AC-SOUND-004-4` — Nach `TWAS` erfolgt keine Handshake-Phase.
* `AC-SOUND-004-5` — Nach `TWAS` kehrt die Station unmittelbar in den normalen Betriebszustand zurück.

---

##### REQ-SOUND-005 — Empfangsseitige Verarbeitung von Sounding

**Spec-Referenz:** A.5.3.1, A.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine empfangene Sounding-Aussendung ist als gültige Signalisierung zu behandeln und mindestens anzuzeigen. Wenn entsprechende Speicher vorhanden sind, sind Connectivity- und LQA-Daten zu aktualisieren.

**Akzeptanzkriterien:**

* `AC-SOUND-005-1` — Empfangene Soundings werden erkannt.
* `AC-SOUND-005-2` — Die Adresse des Senders wird dekodiert.
* `AC-SOUND-005-3` — Die Adresse wird dem Bediener angezeigt.
* `AC-SOUND-005-4` — Optional kann ein Alert ausgelöst werden.
* `AC-SOUND-005-5` — Connectivity-Daten werden gespeichert, sofern vorhanden.
* `AC-SOUND-005-6` — LQA-Daten werden gespeichert, sofern vorhanden.
* `AC-SOUND-005-7` — Der Empfang eines Soundings initiiert keinen automatischen Call.

---

### FEAT-SOUND-002 — Multichannel Scanning Sounding

**Setzt um:** REQ-SOUND-006–010
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf

Multichannel-Scanning-Sounding ist die kanalweise Ausführung von Sounding über ein Sound Set, typischerweise identisch zum Scan Set. Ziel ist die zuverlässige Feststellung und positive Bestätigung unilateraler Konnektivität auf jedem gemeinsam gescannten Kanal sowie die Übertragung der eigenen Verfügbarkeit für Calls und Traffic.

Das Protokoll besteht aus einer Scanning-Sound-Phase und einer redundanten Bestätigungsphase. Die erste Phase muss lang genug sein, damit eine empfangende Station mit derselben Kanalfolge mindestens ein vollständiges Adresswort erfassen kann. Die redundante Phase dient der positiven Konnektivitätsbestätigung.

```text
Tw   = 130.66 ms

Trw  = 392 ms

Tss  ≥ Ts_max
Trs  ≥ 784 ms
Tsrs = Tss + Trs
```

Für die Conclusion gilt:

* `TIS` signalisiert Call Acceptance.
* `TWAS` signalisiert Call Rejection.

Nach `TIS` verbleibt die Station zusätzlich für `Twrt` auf dem Kanal und akzeptiert optionale Handshakes. Nach `TWAS` wird der Kanal unmittelbar verlassen.

#### Semantischer Ablauf für Implementierung

##### Sequenz 1 — Initialisierung des Multichannel-Sounding-Zyklus

1. Der Sounding-Timer läuft ab.
2. Die Station prüft, ob Sounding aktiviert ist.
3. Das Sound Set wird bestimmt.
4. Standardmäßig ist das Sound Set identisch zum Scan Set.
5. Der Kanalindex wird auf den ersten Kanal des Sound Sets gesetzt.
6. Der Zustand wechselt in den Multichannel-Sounding-Zyklus.

---

##### Sequenz 2 — Kanalbezogene Sounding-Ausführung

1. Für jeden Kanal des Sound Sets wird die Station auf den Kanal abgestimmt.
2. Vor der Aussendung erfolgt eine Listen- und Freiheitsprüfung.
3. Ist der Kanal belegt, wird er übersprungen.
4. Ist der Kanal frei, beginnt die Sounding-Aussendung.
5. Die Aussendung erfolgt ausschließlich mit vollständigen Adresswörtern.
6. Die Dauer des Scanning Sounds wird so gewählt, dass jede scannende Gegenstation mindestens ein vollständiges Adresswort empfangen kann.

---

##### Sequenz 3 — Scanning Sound (Tss)

1. Die Station sendet ihre eigene Adresse als Sequenz vollständiger Adresswörter.

2. Die Dauer des Scanning Sounds erfüllt:

   ```text
   Tss ≥ Ts_max
   ```

3. Der Scanning Sound dient dazu, die unilaterale Konnektivität auf dem aktuellen Kanal festzustellen.

4. Während dieser Phase kann eine empfangende Station das Sounding passiv erfassen und die Information speichern.

---

##### Sequenz 4 — Redundant Sound (Trs)

1. Unmittelbar nach dem Scanning Sound folgt die redundante Aussendung.

2. Die redundante Aussendung verwendet ebenfalls ausschließlich vollständige Adresswörter.

3. Die Dauer der redundanten Phase erfüllt:

   ```text
   Trs ≥ 784 ms
   ```

4. Die redundante Phase bestätigt die Verfügbarkeit der sendenden Station positiv.

5. Der Redundant Sound darf nicht ausgelassen werden.

---

##### Sequenz 5 — TIS Call Acceptance

1. Wenn die Station Anrufe akzeptieren soll, wird `TIS` gesendet.
2. `TIS` signalisiert Rufannahmebereitschaft.
3. Nach Abschluss des Sounding-Frames verbleibt die Station für:

   ```text
   Twrt = Twr + Tt
   ```

   auf dem Kanal.
4. Während dieser Phase kann ein optionaler Handshake stattfinden.
5. Erfolgt kein Ruf, wird der Kanal nach Ablauf der Wartephase verlassen.

---

##### Sequenz 6 — TWAS Call Rejection

1. Wenn die Station Anrufe ablehnen soll, wird `TWAS` gesendet.
2. `TWAS` signalisiert Nichtverfügbarkeit.
3. Nach Abschluss von `Tsrs` erfolgt keine zusätzliche Wartephase.
4. Der Kanal wird unmittelbar verlassen.
5. Der nächste Kanal des Sound Sets wird bearbeitet.

---

##### Sequenz 7 — Empfangene Soundings verarbeiten

1. Empfängerseitig wird die sendende Adresse dekodiert.
2. Die Adresse wird dem Bediener angezeigt.
3. Optional wird ein Alert ausgelöst.
4. Falls Connectivity- und LQA-Speicher vorhanden sind, werden die Daten gespeichert.
5. Die Information wird für spätere Linkentscheidungen genutzt.
6. Ein empfangenes Sounding führt nicht automatisch zu einem Call oder Handshake.

---

##### Sequenz 8 — Abschluss des Sounding-Zyklus

1. Nachdem alle Kanäle des Sound Sets verarbeitet wurden, endet der Zyklus.
2. Der Kanalindex wird zurückgesetzt.
3. Der nächste Sounding-Zeitpunkt wird gemäß Intervall geplant.
4. Die Station kehrt in den Zustand `AVAILABLE` zurück.
5. Der normale Scanbetrieb wird fortgesetzt.

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-SOUND-006 — Multichannel Sounding: Kompatibilität mit Scanning

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sounding muss mit dem Scanning-Timing kompatibel sein. Alle Stationen müssen in der Lage sein, die beschriebenen Scanning-Sounding-Protokolle durchzuführen, auch wenn sie auf einer festen Frequenz betrieben werden. Die Protokolle stellen unilaterale Konnektivität auf jedem gemeinsam gescannten Kanal fest und bestätigen diese positiv. Weiterhin unterstützen sie den Aufbau von Links zwischen wartenden Stationen sowie die Übertragung der eigenen Verfügbarkeit für Rufe und Verkehr.

**Akzeptanzkriterien:**

* `AC-SOUND-006-1` — Sounding ist mit dem Scanning-Timing kompatibel.
* `AC-SOUND-006-2` — Alle Stationen können Scanning-Sounding-Protokolle durchführen, auch bei Betrieb auf fester Frequenz.
* `AC-SOUND-006-3` — Scanning Sounding stellt unilaterale Konnektivität auf jedem gemeinsam gescannten Kanal fest.
* `AC-SOUND-006-4` — Die Konnektivität wird durch redundantes Sounding positiv bestätigt.
* `AC-SOUND-006-5` — Das Protokoll unterstützt den Aufbau von Links zwischen wartenden Stationen.
* `AC-SOUND-006-6` — Das Protokoll dient dem Connectivity Tracking.
* `AC-SOUND-006-7` — Das Protokoll dient der Übertragung der eigenen Ruf- und Verkehrsverfügbarkeit.
* `AC-SOUND-006-8` — Es werden ausschließlich vollständige Adresswörter übertragen.
* `AC-SOUND-006-9` — Die Dauer des Scanning Sounds erfüllt `Tss ≥ Ts_max`.
* `AC-SOUND-006-10` — Die Dauer des redundanten Sounds erfüllt `Trs ≥ 784 ms`.
* `AC-SOUND-006-11` — Bei `TIS` verbleibt die Station für `Twrt` auf dem Kanal.
* `AC-SOUND-006-12` — Bei `TWAS` wird der Kanal unmittelbar verlassen.
* `AC-SOUND-006-13` — Nach Abschluss aller Kanäle kehrt die Station in den Zustand `AVAILABLE` zurück.

---

##### REQ-SOUND-007 — Timing-Anforderungen des Scanning Sounds

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Dauer des Scanning Sounds muss mindestens der maximalen Scanperiode aller Empfänger entsprechen, damit jede scannende Station mindestens eine vollständige Adressübertragung empfangen kann.

**Akzeptanzkriterien:**

* `AC-SOUND-007-1` — Die Anzahl der übertragenen Adresswörter wird dynamisch aus `Ts_max` bestimmt.
* `AC-SOUND-007-2` — Es gilt `Tss ≥ Ts_max`.
  <!-- Hinweis: Standard A.5.3.3 fordert Tss > Ts (tatsächliche Scan-Periode der Empfänger).
       Ts_max = 50 s ist der konservative Worst-Case (da Ts ≤ Ts_max für alle Stationen).
       Diese Formulierung ist standardkonform und ausreichend sicher. -->
* `AC-SOUND-007-3` — Jede gemeinsam scannende Station kann mindestens eine vollständige Adresse empfangen.
* `AC-SOUND-007-4` — Die Implementierung darf keine verkürzten Adressformen verwenden.

---

##### REQ-SOUND-008 — Redundant Sounding

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Nach dem eigentlichen Scanning Sound muss eine redundante Wiederholung vollständiger Adresswörter erfolgen, um die unilaterale Konnektivität positiv zu bestätigen.

**Akzeptanzkriterien:**

* `AC-SOUND-008-1` — Der Redundant Sound beginnt unmittelbar nach dem Scanning Sound.
* `AC-SOUND-008-2` — Es werden vollständige Adresswörter verwendet.
* `AC-SOUND-008-3` — Die Mindestdauer beträgt `Trs ≥ 784 ms`.
* `AC-SOUND-008-4` — Die Redundanz dient der positiven Konnektivitätsbestätigung.
* `AC-SOUND-008-5` — Der Redundant Sound darf nicht ausgelassen werden.

---

##### REQ-SOUND-009 — Verfügbarkeitsanzeige durch Conclusion

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Conclusion-Sequenz signalisiert die Bereitschaft zur Annahme weiterer Verbindungen.

**Akzeptanzkriterien:**

* `AC-SOUND-009-1` — `TIS` signalisiert Rufannahmebereitschaft.
* `AC-SOUND-009-2` — `TWAS` signalisiert Rufablehnung bzw. Nichtverfügbarkeit.
* `AC-SOUND-009-3` — Nach `TIS` verbleibt die Station für `Twrt` auf dem Kanal.
* `AC-SOUND-009-4` — Während `Twrt` können optionale Handshakes initiiert werden.
* `AC-SOUND-009-5` — Nach `TWAS` erfolgt keine zusätzliche Wartezeit.
* `AC-SOUND-009-6` — Die Conclusion wird nach Abschluss von `Tsrs` übertragen.

---

##### REQ-SOUND-010 — Sounding Cycle Execution

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Multichannel-Sounding muss sämtliche Kanäle des Sound Sets nacheinander abarbeiten und anschließend den normalen Betriebszustand wieder aufnehmen.

**Akzeptanzkriterien:**

* `AC-SOUND-010-1` — Jeder Kanal des Sound Sets wird genau einmal pro Zyklus bearbeitet.
* `AC-SOUND-010-2` — Die Reihenfolge entspricht der Kanalreihenfolge des Sound Sets.
* `AC-SOUND-010-3` — Nach Abschluss eines Kanals wird unmittelbar zum nächsten Kanal gewechselt.
* `AC-SOUND-010-4` — Nach Abschluss aller Kanäle endet der Sounding-Zyklus.
* `AC-SOUND-010-5` — Die Station kehrt anschließend in den Zustand `AVAILABLE` zurück.
* `AC-SOUND-010-6` — Der nächste Sounding-Zyklus wird entsprechend des konfigurierten Intervalls geplant.


### FEAT-CHAN-001 — Channel Selection & LQA Grundfunktion

**Setzt um:** REQ-CHAN-001–010
**Modul:** `include/Protocol/ale_channel_selector.h`, `src/Protocol/ale_channel_selector.cpp`
**Design-Entscheidungen:** DD-011
**Status:** geplant

#### Datenstrukturen

```cpp
class ChannelSelector {
public:
    ChannelSelector(AleDataStore& store);

    // Automatische Kanalauswahl aus vorab vereinbartem Set:
    uint8_t select_best_channel(const char* target_addr, bool for_link = true);

    // LQA-Update nach jedem empfangenen Frame:
    void update_lqa(const char* from_addr, uint8_t channel,
                    uint8_t sinad_measured, uint8_t ber_measured);

    // CMD LQA anfordern (KA1=1) oder melden (KA1=0):
    bool should_request_lqa() const;  // KA1-Logik

    // Anzeige-Score 0-30 (höher = besser für Operator):
    uint8_t get_display_quality(uint8_t channel_idx) const;

    // Listen-Before-Transmit:
    bool channel_is_busy(uint8_t channel_idx) const;
    void start_listen_before_transmit(uint8_t channel_idx);

private:
    AleDataStore& store_;
    bool          lqa_active_;  // Aktiv/Passiv per Netzwerkmanagement
    bool          auto_lqa_report_enabled_;
};
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-001 — Channel-Selection-System: Grundprinzip

**Spec-Referenz:** A.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss ein Channel-Selection-System implementieren, das es einer Station erlaubt, aus einem vorab vereinbarten Satz von Kanälen automatisch den besten verfügbaren Kanal für Calling und Kommunikation auszuwählen. Die Auswahl basiert auf gespeicherten Link-Quality-Daten und aktuellen Kanalbelegungsinformationen.

**Akzeptanzkriterien:**
- `AC-CHAN-001-1` — Das System implementiert eine automatische Kanalauswahl aus einem vorab vereinbarten Kanalsatz.
- `AC-CHAN-001-2` — Die Kanalauswahl berücksichtigt gespeicherte Link-Quality-Daten.
- `AC-CHAN-001-3` — Die Kanalauswahl berücksichtigt die aktuelle Kanalbelegung.

---

### FEAT-CHAN-002 — BER-Messung & SINAD-Messung

**Setzt um:** REQ-CHAN-011–015
**Modul:** `src/Protocol/ale_channel_selector.cpp`
**Design-Entscheidungen:** DD-011
**Status:** geplant

#### Algorithmus

**BER** (aus FEAT-FEC-005):
```cpp
// Nach jedem decodierten Wort:
uint8_t non_unanimous = 48 - majority_result.unanimous_count;
if (golay_had_uncorrectable_errors) {
    running_sum += 48;   // max Penalty
} else {
    running_sum += non_unanimous;
}
++word_count;
// Am Frame-Ende:
uint8_t ber_score = running_sum / word_count;  // 0..48
```

**SINAD** (0..30 dB, 1-dB-Schritte):
```cpp
// Gemittelt über Signaldauer:
// SINAD = 10 × log10((S+N+D) / (N+D))
// Praktisch: Energie-Verhältnis zwischen Signal-Band und Rausch-Band
float measure_sinad(const float* pcm, size_t samples, uint32_t freq_hz,
                    uint32_t sample_rate_hz);
uint8_t sinad_to_lqa_code(float sinad_db);  // 0..30, clamp, round
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-011 — BER-Messung durch Zählen nicht-einstimmiger Abstimmungen

**Spec-Referenz:** A.5.4.1.1 / Absatz 1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die BER-Messung erfolgt durch Zählen der Anzahl der nicht-einstimmigen (2/3) Abstimmungen (out of 48) im Majority-Vote-Decoder. Der Messbereich erstreckt sich von 0 bis 48.

**Akzeptanzkriterien:**
- `AC-CHAN-011-1` — BER wird durch Zählen nicht-einstimmiger Majority-Vote-Abstimmungen gemessen.
- `AC-CHAN-011-2` — Der Messbereich beträgt 0 bis 48.

---

### FEAT-CHAN-003 — CMD LQA Word (BER/SINAD/MP Felder)

**Setzt um:** REQ-CHAN-016–020
**Modul:** `src/Protocol/ale_channel_selector.cpp`
**Status:** geplant

#### CMD LQA Bit-Layout

```
CMD LQA Word (21 Bit Payload nach 3-Bit Preamble CMD=6):
Bit 20..16 = BER   (5 Bit, 0-30, Tabelle A-XIII)
Bit 15..11 = SINAD (5 Bit, 0-30 dB; 11111 = kein Wert)
Bit 10..8  = MP    (3 Bit, 0-6 ms; 111 = nicht gemessen)
Bit 7      = KA1   (1 Bit: 1 = LQA-Report anfordern)
Bit 6..0   = reserviert / 0

struct LQACmdPayload {
    uint8_t ber   : 5;   // 0-30 (Tabelle A-XIII)
    uint8_t sinad : 5;   // 0-30 dB, 31=unbekannt
    uint8_t mp    : 3;   // 0-6 ms, 7=nicht gemessen
    uint8_t ka1   : 1;   // LQA-Polling-Anfrage
    uint8_t rsvd  : 7;
};
uint32_t encode_lqa_cmd(const LQACmdPayload& p);
LQACmdPayload decode_lqa_cmd(uint32_t word24);
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-016 — CMD LQA Word ist verpflichtende Funktion

**Spec-Referenz:** A.5.4.2 / Absatz 1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Funktion zum Austausch aktueller LQA-Informationen unter ALE-Stationen (CMD LQA) ist verpflichtend.

**Akzeptanzkriterien:**
- `AC-CHAN-016-1` — Alle ALE-Stationen unterstützen die CMD LQA-Funktion.

---

### FEAT-CHAN-004 — Local Noise Report CMD (optional)

**Setzt um:** REQ-CHAN-021, REQ-CHAN-022
**Modul:** `src/Protocol/ale_channel_selector.cpp`
**Status:** geplant

#### Technischer Entwurf
```
CMD Local Noise Report Payload (21 Bit):
Bit 20..14 = Max-Rauschen  (7 Bit, 0-126 dB rel. 0,1µV/3kHz; 127=kein Report)
Bit 13..7  = Mean-Rauschen (7 Bit, gleiche Kodierung)
Bit 6..0   = reserviert

Kodierung: Wert ≤0 → 0, Wert 0-126 → round(dB), Wert >126 → 126, kein Wert → 127
Zeitfenster: letzte 60 Minuten
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-021 — Local Noise Report CMD (optional)

**Spec-Referenz:** A.5.4.4 / Absatz 1, 2
**Priorität:** COULD · **Status:** offen

**Anforderung:** Der Local Noise Report CMD bietet eine Broadcast-Alternative zum Sounding, die es empfangenden Stationen erlaubt, die bilaterale Link-Qualität für den das Report tragenden Kanal grob vorherzusagen. Der CMD meldet die mittlere und maximale Rauschleistung, die auf dem Kanal in den vergangenen 60 Minuten gemessen wurde.

**Akzeptanzkriterien:**
- `AC-CHAN-021-1` — Eine Implementierung kann den Local Noise Report CMD unterstützen oder unterlassen.
- `AC-CHAN-021-2` — Der Report enthält Mittel- und Maximalrauschleistung der vergangenen 60 Minuten.

**Prioritäts-Begründung (COULD):** Der Standard markiert diesen CMD ausdrücklich als optional.

---

##### REQ-CHAN-022 — Local Noise Report: Einheiten und Codierung

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

### FEAT-CHAN-005 — Single- & Multi-Station Channel Selection

**Setzt um:** REQ-CHAN-023–030
**Modul:** `src/Protocol/ale_channel_selector.cpp`
**Status:** geplant

#### Algorithmus

**Kanalauswahl Single-Station (Link Establishment):**
```cpp
uint8_t select_best_channel_for_link(const char* target_addr) {
    // Für jeden Kanal im Scan-Set:
    // bilateral_score[ch] = lqa.sinad_from[target][ch]  // TO-Score
    //                     + lqa.sinad_to[target][ch]    // FROM-Score (bilateral)
    // LQA-Score 0=exzellent, 30=sehr schlecht → minimiere bilateral_score
    // Sortiere nach bilateral_score ascending, wähle Channel[0]
}
```

**LQA-Score-Semantik** (A.4.5.1, Figure A-27):
```
0  = exzellent
30 = sehr schlecht
x  = Handshake fehlgeschlagen (nach Versuch)
-  = keine Messung (kein Versuch)
```

**Broadcast** (One-Way): TO-Score bevorzugen (Faktor 2).
**Listening**: FROM-Score bevorzugen (Faktor 2).

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-023 — Single-Station Channel Selection für eine Station

**Spec-Referenz:** A.5.4.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle Stationen müssen in der Lage sein, den (recent) besten Kanal für Calling oder Listening für eine einzelne Station basierend auf den Werten im LQA-Speicher auszuwählen.

**Akzeptanzkriterien:**
- `AC-CHAN-023-1` — Alle Stationen unterstützen die Kanalselektion für eine einzelne Station basierend auf LQA.

---

### FEAT-CHAN-006 — Listen Before Transmit

**Setzt um:** REQ-CHAN-031–034
**Modul:** `src/Protocol/ale_channel_selector.cpp`
**Status:** geplant

#### Technischer Entwurf

```cpp
// Twt: programmierbar über OperatingParameters
// ALE-only Kanäle: Twt = max(Twt_default, Twt_ale) = 784 ms
// Andere Kanäle:   Twt = max(Twt_default, 2000 ms)
// Bereits verbrachte Hörzeit wird angerechnet.

class ListenBeforeTransmit {
public:
    void start(uint8_t channel_idx, uint32_t twt_ms, uint32_t already_listened_ms = 0);
    bool is_clear() const;      // true wenn kein Verkehr erkannt
    bool is_elapsed() const;    // true wenn Twt abgelaufen
    void operator_override();   // REQ-CHAN-034
private:
    bool     overridden_;
    uint32_t start_ms_;
    uint32_t required_ms_;
    bool     traffic_detected_;
};
```

Verkehrserkennung: delegiert an `ChannelSelector::channel_is_busy()` — nutzt Occupancy-Detection aus REQ-GEN-009 (Tabelle A-I).

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-CHAN-031 — Listen before Transmit: Verpflichtende Pause vor Call/Sound

**Spec-Referenz:** A.5.4.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bevor ein Calling oder ein Sound auf einem Kanal eingeleitet wird, muss eine ALE-Station für eine programmierbare Zeit (Twt) auf andere Verkehrstätigkeit auf dem Kanal lauschen und darf auf diesem Kanal nicht senden, wenn Verkehr erkannt wird. Normalerweise ist ein aufgrund erkannten Verkehrs abgebrochener Sound wieder zu planen, während für einen Call ein anderer Kanal zu wählen ist.

**Akzeptanzkriterien:**
- `AC-CHAN-031-1` — Vor jedem Call oder Sound wird für die Dauer Twt auf dem Kanal gelauscht.
- `AC-CHAN-031-2` — Bei erkannten Verkehr wird das Senden unterlassen.
- `AC-CHAN-031-3` — Ein aufgrund von Verkehr abgebrochener Sound wird erneut geplant.
- `AC-CHAN-031-4` — Ein aufgrund von Verkehr abgebrochener Call wählt einen anderen Kanal.

---


### FEAT-LINK-001 — Individual Call senden

**Setzt um:** REQ-LINK-001, REQ-LINK-002, REQ-LINK-007, REQ-LINK-008, REQ-LINK-009, REQ-LINK-016, REQ-LINK-017
**Modul:** `src/Protocol/Control/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009, DD-010, DD-014
**Status:** implementiert (verifizieren gegen neue REQs)

#### Technischer Entwurf

Die rufende Station bildet den Aufruf als deterministische Zustandsfolge ab. Die Sequenz ist nur dann gültig, wenn Wortgrenzen, Timer und Abbruchbedingungen eingehalten werden.

```text
AVAILABLE
  → initiate_call(channel_id)
  → LBT / listen-before-transmit auf dem gewählten Kanal (Twt)
  → tune_channel(channel_id)

  → CALLING::SCANNING_CALL
       - nur wenn die gerufene Station nicht als Single-Channel-Hörer bekannt ist
       - sende nur das erste Adresswort der Zieladresse
       - verwende TO-Präambel
       - Wiederholung bis Tsc ≥ Ts bzw. bis die Scanmenge der Empfängerstation abgedeckt ist
       - Gruppenruf: THRU / REP alternierend gemäß Adresslogik

  → CALLING::LEADING_CALL
       - sende die vollständige Zieladresse
       - erstes Adresswort mit TO
       - zusätzliche Wortteile mit DATA / REP
       - wiederhole die vollständige Leading-Call-Sequenz exakt zweimal

  → CALLING::MESSAGE
       - optionaler Orderwire / Nachrichtenteil
       - AMD / DTM / DBM nur innerhalb der zulässigen Message-Grenzen
       - bei aktivem Orderwire endet die Message am Ende des Protokolls ohne Zusatzverlängerung

  → CALLING::CONCLUSION
       - sende Conclusion mit eigener Adresse der rufenden Station
       - z. B. TIS <eigene Adresse> oder TWAS <eigene Adresse> je nach Abschlusszweck

  → CALLING::LISTENING
       - starte Twr (Einzelkanal) oder Twrt (Mehrkanal)
       - warte auf den Beginn von "TO <eigene Adresse der rufenden Station>"
       - parallel: Wortsync, Preamble-Validierung, Phase-Kontinuität überwachen

       │
       ├─ Kein gültiger Response-Start innerhalb Twr / Twrt
       │    → nächster Kanal, falls verfügbar
       │    → sonst IDLE / AVAILABLE, Operator benachrichtigen
       │
       ├─ Ungültige Preamble-Sequenz
       │    → nächster Kanal / IDLE
       │
       ├─ "TWAS <eigene Adresse der rufenden Station>"
       │    → IDLE, Operator: Verbindungsaufbau abgelehnt
       │
       └─ "TIS <eigene Adresse der antwortenden Station>" vollständig gelesen + Tlww
            → HANDSHAKE::SENDING_ACK
                 - sende TO <Zieladresse der antwortenden Station> ... TIS <eigene Adresse der rufenden Station>
                 - ACK muss innerhalb des Twr-Fensters der Gegenstation liegen
                 - nach vollständig gesendetem ACK: LINKED::ACTIVE
```

#### Semantik der Call-Konstruktion

Die Wortfolge ist nicht frei formbar, sondern folgt einer festen Adresszerlegung.

* Scanning Call:

  * ausschließlich erstes Wort der Zieladresse
  * dient nur dem Einfangen einer scannden Empfängerstation
  * Wiederholung ist zulässig und notwendig, bis der Scanraum der Empfängerstation abgedeckt ist

* Leading Call:

  * komplette Zieladresse
  * zweimal gesendet
  * Wortaufteilung erfolgt über TO / DATA / REP
  * maximal zulässige Adresslänge und Wortzahl bleiben unverändert

* Conclusion:

  * enthält die eigene Adresse der rufenden Station
  * wird direkt nach dem Leading Call oder nach dem optionalen Message-Abschnitt gesendet

#### Abbruchbedingungen `CALLING::LISTENING` / `HANDSHAKE::READING_RESPONSE`

| Bedingung                                                                                     | Aktion                         | Referenz       |
| --------------------------------------------------------------------------------------------- | ------------------------------ | -------------- |
| Kein passender Response-Start "TO <eigene Adresse der rufenden Station>" innerhalb Twr / Twrt | nächster Kanal / IDLE          | AC-LINK-019-6  |
| Ungültige Wortpräambel-Sequenz                                                                | nächster Kanal                 | AC-LINK-019-7  |
| Keine passende Conclusion innerhalb Tlc (+ Tmmax)                                             | nächster Kanal                 | AC-LINK-019-8  |
| Ende der Conclusion nicht innerhalb Tlww erkannt                                              | nächster Kanal                 | AC-LINK-019-9  |
| "TWAS <eigene Adresse der antwortenden Station>" empfangen                                    | IDLE, Operator benachrichtigen | AC-LINK-019-10 |

#### Timer und Kanalverhalten

```cpp
// Nach CALLING::CONCLUSION:
start_timer(single_channel ? Twr_ms : Twrt_ms);

// Single-Channel:
// - kein Neuabstimmen erforderlich
// - Twr deckt Round-Trip und Turnaround ab

// Multi-Channel:
// - Twrt = Twr + Tt
// - Tt ist hardwareabhängig
// - während Twrt darf die Empfängerstation auf einen anderen Kanal nachziehen

// REQ-LINK-007:
// emergency_manual_control() darf outer_state_ unabhängig vom laufenden Call
// auf AVAILABLE zurücksetzen, ohne die Always-Listening-Fähigkeit zu beeinträchtigen.
```

---

##### REQ-LINK-001 — Individual Call

**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Ruf an eine einzelne Station durchläuft Scanning-Call, Leading-Call, Conclusion und Empfangsfenster.

**Akzeptanzkriterien:**

* `AC-LINK-001-1` — Die gesendete Wortfolge entspricht: Scanning (TO-Erstwort), Leading (vollständige Adresse ×2), Conclusion (eigene Adresse).

---

##### REQ-LINK-002 — Net Call

**Spec-Referenz:** A.5.2.5.1
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Ein Netzruf verwendet denselben Ablauf wie der Individual Call; der Unterschied liegt in der Adressklassifikation.

**Akzeptanzkriterien:**

* `AC-LINK-002-1` — Der Ablauf entspricht dem Individual Call.
* `AC-LINK-002-2` — Die Rufart wird ausschließlich über die Adressklassifikation bestimmt.

---

##### REQ-LINK-007 — Manual Operation: Emergency Control

**Spec-Referenz:** A.5.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der ALE-Controller muss eine Notfallsteuerung durch den Operator ermöglichen. Der Controller muss den Operator in Notfällen direkt an der Basis-SSB-Radio steuern lassen. Im Normalbetrieb soll das Radio automatisch gesteuert werden; die passive Empfangsfähigkeit des Controllers darf nicht beeinträchtigt werden.

**Akzeptanzkriterien:**

* `AC-LINK-007-1` — Der ALE-Controller unterstützt Notfallsteuerung durch den Operator.
* `AC-LINK-007-2` — Jeder ALE-Controller bietet manuelle Steuerungsfähigkeit für den Operator.
* `AC-LINK-007-3` — In Notfällen kann der Operator direkt das Basis-SSB-Radio steuern.
* `AC-LINK-007-4` — Während normaler Betrieb wird das Radio automatisch gesteuert.
* `AC-LINK-007-5` — Der Operator betreibt das Radio über seinen zugehörigen Controller.
* `AC-LINK-007-6` — Die „always listening“-Fähigkeit des Controllers wird nicht beeinträchtigt.
* `AC-LINK-007-7` — Die Anforderung zur manuellen PTT-Bedienung gemäß 4.2.2 bleibt unberührt.

---

##### REQ-LINK-008 — ALE Three-Way-Handshake

**Spec-Referenz:** A.5.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das grundlegende Protokoll zur Verbindungsaufnahme ist das dreifache Handshake. Ein dreifaches Handshake ist ausreichend, um eine Verbindung zwischen einer rufenden und einer antwortenden Station herzustellen.

**Akzeptanzkriterien:**

* `AC-LINK-008-1` — Das dreifache Handshake-Protokoll ist ausreichend für eine Verbindung.
* `AC-LINK-008-2` — Slotted Responses ermöglichen die Verbindung einer Station mit mehreren Stationen.
* `AC-LINK-008-3` — Die Handshake-Protokolle sind identisch für Einzel- und Mehr-Station-Verbindungen.

---

##### REQ-LINK-009 — Timing-Funktionen und -Werte

**Spec-Referenz:** A.5.5.2.1 / Table A-XV
**Priorität:** MUST · **Status:** offen

#### Timing (TABLE A-XV)

> **NOTE:** Refer to Annex A and Annex B for details.

##### Basic System Timing

| Parameter                      | Definition                        | Value                   |
| ------------------------------ | --------------------------------- | ----------------------- |
| Tone rate                      | Symbol rate                       | **125 symbols/s (sps)** |
| Tone period                    | `Ttone`                           | **8 ms**                |
| On-air rate                    | Data rate                         | **375 b/s**             |
| On-air word                    | `Tw`                              | **130.66 ms**           |
| On-air redundant word          | `Trw = 3 × Tw`                    | **392 ms**              |
| On-air leading redundant words | `Tlrw = 2 × Trw`                  | **784 ms**              |
| Individual/net address time    | `Ta = m × Trw`, `m = 1...5` words | **392–1960 ms**         |
| Propagation delay              | `Tp`                              | **0–70 ms**             |

---

##### System Timing Limits

| Parameter                               | Definition                                | Value                                                    |
| --------------------------------------- | ----------------------------------------- | -------------------------------------------------------- |
| Address size limit (5 words)            | `Ta max`                                  | **1960 ms**                                              |
| Address first word limit                | `Tal`                                     | **392 ms**                                               |
| Call time maximum                       | `Tc = 4704 ms` (½ of `Tlc`, max 12 words) | **4704 ms**                                              |
| Group addresses first word limit        | `Tcl`                                     | **1960 ms**                                              |
| Maximum scan period                     | `Ts max`                                  | **50 s**                                                 |
| Basic message section time              | `Tm max basic`                            | **11.76 s**                                              |
| AMD message section limit (90 chars)    | `Tm max AMD`                              | **11.76 s**                                              |
| DTM message section limit (1053 chars)  | `Tm max DTM`                              | **2.29 min** (entire data block)                         |
| DBM message section limit (37377 chars) | `Tm max DBM`                              | **23.26 min** (entire deeply interleaved block with CMD) |
| Termination time limit                  | `Tx max`                                  | **1960 ms**                                              |

**Message Extension Rule:**

If an ALE orderwire protocol (AMD, DTM, DBM) extends the basic message section:

* Extension shall begin **no later than the start of the 30th word (11.368 s)**.
* Message section duration is determined solely by the extended protocol length.
* The message section terminates at the end of the orderwire.
* The conclusion sequence starts immediately thereafter.

---

##### Individual Calling Timing

| Parameter                               | Definition                   | Value        |
| --------------------------------------- | ---------------------------- | ------------ |
| Minimum dwell time (5 ch/s scanning)    | `Td(5) min`                  | **200 ms**   |
| Minimum dwell time (2 ch/s scanning)    | `Td(2) min`                  | **500 ms**   |
| Probable maximum dwell time per channel | `Td = Tdrw`                  | **784 ms**   |
| Number of channels                      | `C`                          | Configurable |
| Scan period                             | `Ts = C × Td`                | Derived      |
| Call time                               | `Tc = ΣTa` within `Tlc`      | Derived      |
| Group call time                         | `Tcl = ΣTal` within `Tsc`    | Derived      |
| Leading call time                       | `Tlc = 2 × Tc`               | Derived      |
| Redundant call time                     | `Trc = Tlc + Tx`             | Derived      |
| Scanning call time                      | `Tsc = n × Tcl ≥ Ts`         | Derived      |
| Calling cycle time                      | `Tcc = Tsc + Tlc ≥ Ts + Tlc` | Derived      |
| Scanning redundant call time            | `Tsrc = Tsc + Trc`           | Derived      |
| Last word wait delay                    | `Tlww = Trw`                 | **392 ms**   |

Additional timing definitions:

| Parameter                             | Formula                                               |
| ------------------------------------- | ----------------------------------------------------- |
| Wait for response delay               | `Twr = Ttd + Tp + Tlww + Tta + Trwp + Tld + Tp + Trd` |
| Late detect delay                     | `Tld = Tw`                                            |
| Redundant word phase delay            | `Trwp = 0 ... Trw` (**0–392 ms**)                     |
| Turnaround time                       | `Tta = Trd + Tdek + Tenk + Ttc + Ttk + Ttd`           |
| Wait for calling cycle end            | `Twce = 2 × own Ts` (default)                         |
| Tune time                             | `Tt` (as required by slowest tuner)                   |
| Wait for reply and tune               | `Twrt = Twr + Tt`                                     |
| Detect signaling period               | `Tds ≤ Td(5)` (**≤ 200 ms**)                          |
| Detect redundant word period          | `Tdrw = Trw + spare Trw` = **784 ms**                 |
| Detect rotating redundant word period | `Tdrrw = 2 × Trw + spare Trw` = **1176 ms**           |

---

##### Sounding Timing

| Parameter                     | Formula                       |
| ----------------------------- | ----------------------------- |
| Redundant sound time          | `Trs = 2 × Ta`                |
| Scanning sound time           | `Tss = n × Ta ≥ Ts`           |
| Scanning redundant sound time | `Tsrs = Tss + Trs ≥ Ts + Trs` |

---

##### Star Calling Timing

| Parameter                    | Definition                                         |
| ---------------------------- | -------------------------------------------------- |
| Minimum standard slot widths | `Tsw min = 14, 17 Tw` (1st handshake slots)        |
|                              | `Tsw min = 17, 20 Tw` (subsequent handshake slots) |
|                              | Other values may be defined by CMD                 |
| Slot widths                  | `Tsw = 14, 17, 9, or other Tw`                     |
| Slot number                  | `SN`                                               |
| Number of slots              | `NS`                                               |

Slot timing:

| Parameter                   | Formula                                                                                                     |
| --------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Uniform slot wait time      | `Tswt = Tsw × SN`                                                                                           |
| General slot wait time      | `Tswt(SN) = SN × [5Tw + 2Ta(caller) + (optional LQA)Trw + (optional message)Tm] + Ta(caller) + ΣTa(called)` |
| Wait for net reply          | `Twrn = Tsw × NS` (uniform) or `Tswt(NS)`                                                                   |
| Wait for net acknowledgment | `Twan = Twrn + Tdrw`                                                                                        |

Turnaround limits:

| Condition   | Requirement          |
| ----------- | -------------------- |
| Slot 0      | `Tta + Tt ≤ 360 ms`  |
| Slot 1      | `Tta + Tt ≤ 2100 ms` |
| Other slots | `Tta + Tt ≤ 1500 ms` |

Maximum acknowledgment timing:

| Parameter                                            | Formula                                                                            |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Maximum star-group acknowledgment wait               | `Twan max = 107Tw + 27Ta(caller) + 13Trw (optional LQA) + 13Tm (optional message)` |
| Late-arrival stations (1-word addresses, no message) | `Twan max = 188Tw`                                                                 |
| Late-arrival stations with LQA                       | `Twan max = 227Tw`                                                                 |

---

##### Programmable Timing Parameters (Typical Values)

| Parameter                                       | Symbol        | Typical Value              |
| ----------------------------------------------- | ------------- | -------------------------- |
| Listen-before-transmit                          | `Twt`         | **2 s** (general channels) |
| Listen-before-transmit (ALE/data-only channels) | `Twt`         | **784 ms**                 |
| Tune time (blind first call)                    | `Tt = 8 × Tw` | **1045.33 ms**             |
| Tune time (subsequent attempts)                 | `Tt`          | **20 s**                   |
| Automatic sounding interval                     | `Tps`         | **30 min**                 |
| Wait for activity                               | `Twa`         | **30 s**                   |


**Anforderung:** Das ALE-System verwendet definierte Timing-Funktionen zur Steuerung von Ruf-, Antwort- und Verbindungsübergängen. Wenn ein ALE-Orderwire-Protokoll den Message-Abschnitt verlängert, darf die Verlängerung frühestens ab dem Start des 30. Worts beginnen. Die Conclusion beginnt unmittelbar am Ende des Message-Abschnitts.

**Akzeptanzkriterien:**

* `AC-LINK-009-1` — Das System implementiert alle Timing-Funktionen gemäß Tabelle A-XV.
* `AC-LINK-009-2` — Alle Standard-Timing-Werte werden eingehalten.
* `AC-LINK-009-3` — Ein verlängerter Message-Abschnitt beginnt nicht vor dem Start des 30. Worts.
* `AC-LINK-009-4` — Der Message-Abschnitt endet am Ende des Orderwire-Protokolls ohne weitere Verlängerung.
* `AC-LINK-009-5` — Die Conclusion beginnt unmittelbar am Ende des Message-Abschnitts.

---

##### REQ-LINK-016 — One-to-One Calling Protocol

**Spec-Referenz:** A.5.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Protokoll zur Herstellung einer Verbindung zwischen zwei einzelnen Stationen besteht aus drei ALE-Frames: Aufruf, Antwort und Bestätigung.

**Akzeptanzkriterien:**

* `AC-LINK-016-1` — Das Protokoll zum Verbindungsaufbau besteht aus genau drei ALE-Frames: Call, Response und Acknowledgment.
* `AC-LINK-016-2` — Kein Link gilt als hergestellt, bevor alle drei Frames erfolgreich ausgetauscht wurden.
* `AC-LINK-016-3` — Alle in A.5.5.3.1–A.5.5.3.4 definierten Zeitüberschreitungen werden eingehalten.

---

##### REQ-LINK-017 — Sending an Individual Call

**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Nach Auswahl eines Kanals für das Rufen beginnt die rufende Station mit Listen-Before-Transmit und Kanalabstimmung. Ist bekannt, dass die Gegenstation auf dem gewählten Kanal hört, wird ein Einzelkanal-Aufruf ohne Scanning Call gesendet. Andernfalls wird ein längerer Calling Cycle mit Scanning Call gesendet, der die Gegenstation während ihres Scans erfassen kann.

**Akzeptanzkriterien:**

* `AC-LINK-017-1` — Die rufende Station hört auf dem Kanal, bevor sie sendet.
* `AC-LINK-017-2` — Die rufende Station stimmt den Kanal ab, bevor sie sendet.
* `AC-LINK-017-3` — Bei bekanntem Einzelkanal-Hörer: Aufruf enthält nur Leading Call und Conclusion.
* `AC-LINK-017-4` — Bei scannender Gegenstation: dem Leading Call ist ein Scanning Call vorangestellt.
* `AC-LINK-017-5` — Die Dauer des Scanning Calls beträgt 2 Trw für jeden gescannten Kanal der gerufenen Station.
* `AC-LINK-017-6` — Der Scanning-Call-Abschnitt enthält ausschließlich das erste Adresswort der gerufenen Station.
* `AC-LINK-017-7` — Die vollständige Zieladresse wird im Leading Call zweimal übertragen.
* `AC-LINK-017-8` — Bei ausbleibender Antwort innerhalb Twr oder Twrt wird der nächste Kanal versucht oder der Versuch abgebrochen.

---

### FEAT-LINK-002 — Individual Call empfangen & Response

**Setzt um:** REQ-LINK-004, REQ-LINK-005, REQ-LINK-006, REQ-LINK-018, REQ-LINK-019
**Modul:** `src/FSK/ale_waveform.cpp`, `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009, DD-010, DD-014
**Status:** geplant

#### Technischer Entwurf

Die Empfangslogik unterscheidet strikt zwischen Word-Sync, Adress-Matching, Protokoll-Parsing und Zustandsübergang. Jedes empfangene Wort wird sequenziell bewertet.

```text
AVAILABLE / SCANNING
  → ALEStateMachine::process_received_word(word)
  → word_sync()
  → SelfAddressStore::matches_any(received_first_word)

  → wenn "TO <eigene Adresse der empfangenden Station>" oder äquivalente Zielerkennung:
       - Scan stoppen
       - previous_outer_state_ sichern
       - HANDSHAKE::READING_CALL

       │
       ├─ Ungültige Preamble-Sequenz
       │    → previous_outer_state_
       │
       ├─ Keine Quick-ID / Message / Conclusion innerhalb Twce
       │    → previous_outer_state_
       │
       ├─ Keine Conclusion innerhalb Tmmax nach Message-Beginn
       │    → previous_outer_state_
       │
       ├─ "TWAS <eigene Adresse der rufenden Station>" erkannt
       │    → previous_outer_state_ (kein Response senden)
       │
       └─ "TIS <eigene Adresse der rufenden Station>" vollständig gelesen + Tlww
            → HANDSHAKE::CHANNEL_CHECK
                 - 2 × Trw Listen-Before-Transmit auf dem Kanal
                 - wenn Kanal belegt: previous_outer_state_
                 - wenn Kanal frei:
                      → HANDSHAKE::SENDING_RESPONSE
                           TO <eigene Adresse der rufenden Station> [DATA …] ×2
                           TIS <eigene Adresse der empfangenden Station>
                           → Twr starten
                           → HANDSHAKE::WAITING_FOR_ACK
                                │
                                ├─ Twr-Timeout
                                │    → previous_outer_state_
                                │
                                └─ "TO <eigene Adresse der empfangenden Station>" innerhalb Twr
                                     → HANDSHAKE::READING_ACK
                                          │
                                          ├─ Abbruchbedingung
                                          │    → previous_outer_state_
                                          │
                                          ├─ "TWAS <eigene Adresse der empfangenden Station>"
                                          │    → previous_outer_state_
                                          │
                                          └─ "TIS <eigene Adresse der rufenden Station>" + Tlww
                                               → LINKED::ACTIVE
```

#### Semantik der Empfangsprüfung

Die Station reagiert nur dann auf einen Call, wenn alle folgenden Bedingungen gemeinsam erfüllt sind:

* das erste Wort der eigenen Adresse wurde korrekt dekodiert,
* der Empfang ist wortsynchron,
* die empfangene Sequenz gehört zu einem gültigen ALE-Call,
* die Conclusion ist syntaktisch und zeitlich gültig,
* der Kanal ist zum Zeitpunkt der Response frei.

Falls eine dieser Bedingungen nicht erfüllt ist, wird kein Response gesendet. Die Station kehrt in den vorherigen Zustand zurück.

#### Abbruchbedingungen `HANDSHAKE::READING_CALL`

| Bedingung                                                                           | Referenz      |
| ----------------------------------------------------------------------------------- | ------------- |
| Kein Start von Quick-ID, Message oder Conclusion innerhalb Twce                     | AC-LINK-018-5 |
| Kein Conclusion-Start innerhalb Tmmax nach Message-Beginn                           | AC-LINK-018-5 |
| Ungültige Preamble-Sequenz; Ausnahme: bis zu drei Fehler im Scanning Call toleriert | AC-LINK-018-6 |
| Schlussfolge nicht innerhalb Tlww (+ n×Trw bei erweiterter Adresse) erkannt         | AC-LINK-018-7 |

#### Abbruchbedingungen `HANDSHAKE::READING_ACK`

| Bedingung                                                         | Referenz      |
| ----------------------------------------------------------------- | ------------- |
| Kein "TO <eigene Adresse der empfangenden Station>" innerhalb Twr | AC-LINK-020-2 |
| Ungültige Preamble-Sequenz                                        | AC-LINK-020-3 |
| Keine Conclusion innerhalb Tlc (+ Tmmax)                          | AC-LINK-020-4 |
| Ende der Conclusion nicht innerhalb Tlww erkannt                  | AC-LINK-020-5 |

#### End-of-Frame-Erkennung

Ein empfangenes Frame ist erst beendet, wenn:

* eine gültige Conclusion erkannt wurde,
* die Wortphase konstant bleibt,
* und innerhalb des Tlww-Fensters kein weiteres Wort mehr detektiert wird.

Für erweiterte Adressen sind zusätzliche Folgewörter zulässig, sofern sie innerhalb der definierten Trw-basierten Grenzen liegen.

---

##### REQ-LINK-004 — Signal- und Symbolerkennung

**Spec-Referenz:** A.5.1.2 / A.5.2.6.2 · ⚠ Siehe OPEN-08
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das System erkennt aus dem empfangenen Audiosignal die übertragenen Symbole.

**Akzeptanzkriterien:**

* `AC-LINK-004-1` — Ein sauber gesendetes Wort wird fehlerfrei zurückgewonnen.

---

##### REQ-LINK-005 — End-of-Frame-Erkennung

**Spec-Referenz:** A.5.5.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Ende einer empfangenen Übertragung wird anhand einer gültigen Conclusion und der konstanten Wortphase erkannt.

**Akzeptanzkriterien:**

* `AC-LINK-005-1` — Nach einer gültigen Conclusion plus definiertem Wartedelay gilt die Übertragung als beendet.

---

##### REQ-LINK-006 — Adress-Erkennung im Scanning-Call

**Spec-Referenz:** A.5.2.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Da der Scanning-Call nur das erste Adresswort überträgt, erkennt die Station einen an sie gerichteten Ruf bereits am ersten Wort ihrer eigenen Adresse.

**Akzeptanzkriterien:**

* `AC-LINK-006-1` — Eine Station mit mehr als drei Adresszeichen erkennt einen Scanning-Call, der nur ihr erstes Adresswort enthält.

---

##### REQ-LINK-018 — Receiving an Individual Call

**Spec-Referenz:** A.5.5.3.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn die gerufene Station auf dem Kanal ankommt, prüft sie ALE-Signale, erreicht bei Bedarf Word-Sync und entscheidet anhand des empfangenen Wortes über die weitere Aktion. Bei gültigem "TO <eigene Adresse der empfangenden Station>" stoppt sie den Scan, tritt in den Link-Zustand ein und liest weiter bis zur Conclusion oder zum Message-Abschnitt. Bei ungültiger Sequenz kehrt sie in den vorherigen Zustand zurück.

**Akzeptanzkriterien:**

* `AC-LINK-018-1` — Erkennt die Station ALE-Signale und erreicht Word-Sync, prüft sie das empfangene Wort zur Aktionsbestimmung.
* `AC-LINK-018-2` — Bei Empfang von „TO <eigene Adresse der empfangenden Station>“: Scan stoppen, Linking-Zustand eintreten, ALE-Wörter weiterlesen, auf Ablauf von Twce warten.
* `AC-LINK-018-3` — Bei potenziellem Sound oder anderem Protokoll: Wort gemäß diesem Protokoll verarbeiten.
* `AC-LINK-018-4` — Andernfalls: in vorherigen Zustand zurückkehren.
* `AC-LINK-018-5` — Kein Start von Quick-ID, Message oder Conclusion innerhalb Twce; oder kein Conclusion-Start innerhalb Tmmax nach Beginn des Message-Abschnitts.
* `AC-LINK-018-6` — Ungültige Wortpräambel-Sequenz empfangen; Ausnahme: bis zu drei aufeinanderfolgende Fehler im Scanning Call werden toleriert.
* `AC-LINK-018-7` — Ende der Conclusion nicht innerhalb Tlww (plus zusätzliche Trw-Vielfache bei erweiterter Adresse) erkannt.
* `AC-LINK-018-8` — Bei erfolgreich empfangener TIS-Conclusion: Last-Word-Wait-Timer Tlww starten und auf weitere Adresswörter sowie Frameende warten.
* `AC-LINK-018-9` — Bei empfangener TWAS-Conclusion: nicht antworten, sofort in vorherigen Zustand zurückkehren.

---

##### REQ-LINK-019 — Response

**Spec-Referenz:** A.5.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei Empfang eines Aufrufs an die eigene Adresse prüft die gerufene Station zuerst die Kanalbelegung. Ist der Kanal frei, sendet sie die Antwort und startet Twr. Ist der Kanal belegt, wird der Aufruf ignoriert. Die rufende Station verarbeitet Response und Conclusion mit denselben Prüfungen und Timeouts wie beim Call.

**Akzeptanzkriterien:**

* `AC-LINK-019-1` — Die gerufene Station prüft die Kanalbelegung, bevor sie antwortet.
* `AC-LINK-019-2` — Bei freiem Kanal: abstimmen, Antwort senden, Timer Twr starten.
* `AC-LINK-019-3` — Bei belegtem Kanal: Aufruf ignorieren, in vorherigen Zustand zurückkehren.
* `AC-LINK-019-4` — Twr gilt für Einzelkanal; Twrt gilt, wenn Bestätigung auf anderem Kanal erwartet wird.
* `AC-LINK-019-5` — Die rufende Station verarbeitet die Response mit denselben Prüfungen und Timeouts wie beim Empfang des Calls.
* `AC-LINK-019-6` — Abbruchbedingung 1: kein "TO <eigene Adresse der rufenden Station>" innerhalb Twr/Twrt.
* `AC-LINK-019-7` — Abbruchbedingung 2: ungültige Wortpräambel-Sequenz.
* `AC-LINK-019-8` — Abbruchbedingung 3: keine Conclusion „TIS <eigene Adresse der empfängenden Station>" innerhalb Tlc (+Tmmax).
* `AC-LINK-019-9` — Abbruchbedingung 4: Ende der Conclusion nicht innerhalb Tlww erkannt.
* `AC-LINK-019-10` — Bei „TWAS <eigene Adresse der rufenden Station>“: Verbindungsaufbau abbrechen und Operator informieren.

---

### FEAT-LINK-003 — Acknowledgment & Link-Termination

**Setzt um:** REQ-LINK-020, REQ-LINK-021, REQ-LINK-022, REQ-LINK-023
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009, DD-010, DD-014
**Status:** geplant

#### Technischer Entwurf

Die Bestätigung ist semantisch eine eigene Sequenz, auch wenn ihre Wortform einem Einzelruf äußerlich gleicht. Die Gegenstation darf den ACK daher nur dann als neuen Ruf behandeln, wenn das ACK außerhalb des erwarteten Twr-Fensters eintrifft.

```text
HANDSHAKE::SENDING_ACK
  → sende TO <eigene Adresse der empfangenden Station> [DATA …] ×2
  → sende TIS <eigene Adresse der rufenden Station>
  → ACK vollständig gesendet
  → LINKED::ACTIVE
  → Lautsprecher aktivieren
  → Operator / Controller informieren
  → Twa starten

HANDSHAKE::READING_ACK (auf der Empfängerseite)
  → erkenne "TO <eigene Adresse der empfangenden Station>" innerhalb Twr
  → lese Folgewörter
  → bei "TIS <eigene Adresse der rufenden Station>" vollständig + Tlww:
       LINKED::ACTIVE
  → bei "TWAS <eigene Adresse der rufenden Station>":
       previous_outer_state_
  → bei Timeout / Preamble-Fehler / Framefehler:
       previous_outer_state_
```

#### Link-Termination

```text
LINKED::TERMINATING
  → sende TWAS-Frame an alle zu terminierenden Gegenstationen
  → Beispiel: TO <Zieladresse>, TO <Zieladresse>, TWAS <eigene Adresse>
  → Empfänger:
       - Lautsprecher stumm
       - Link mit dieser Station beenden
       - verfügbare Rolle wiederherstellen
  → Sender:
       - nach vollständiger TWAS-Ausgabe verfügbar
       - andere bestehende Links bleiben unverändert erhalten
```

#### Aktivitäts-Timer

```cpp
class ActivityTimer {
public:
    void reset();       // bei TX/RX jeder gültigen Aktivität
    void disable();     // vom Operator oder Netzwerkmanager abschaltbar
    bool is_expired() const;
private:
    bool enabled_;
    uint32_t last_activity_ms_;
};

// Twa ist obligatorisch, Default 30 s.
// Bei Ablauf:
// - Link beenden
// - Lautsprecher stummschalten
// - inneren Zustand auf AVAILABLE zurücksetzen
// - TWAS ist empfohlen, aber nicht zwingend
```

---

##### REQ-LINK-020 — Acknowledgment

**Spec-Referenz:** A.5.5.3.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn alle Kriterien für eine akzeptable Antwort erfüllt sind und dies nicht anderweitig angeordnet wurde, sendet die rufende Station eine Bestätigung, wechselt in den verknüpften Zustand und startet Twa. Die gerufene Station verarbeitet das ACK mit denselben Prüfungen wie einen Call.

**Akzeptanzkriterien:**

* `AC-LINK-020-1` — Die rufende Station sendet ACK, wechselt in den Linked-Zustand mit der Gegenstation, schaltet den Lautsprecher ein und startet Timer Twa.
* `AC-LINK-020-2` — Kein passender ACK-Calling-Cycle „TO <eigene Adresse der rufenden Station>" innerhalb Twr.
* `AC-LINK-020-3` — Ungültige Wortpräambel-Sequenz.
* `AC-LINK-020-4` — Keine Conclusion innerhalb Tlc (+Tmmax) nach Framestart.
* `AC-LINK-020-5` — Ende der Conclusion nicht innerhalb Tlww erkannt.
* `AC-LINK-020-6` — Die Gegenstation liest ACK mit denselben Prüfungen wie beim Call; bei TIS <eigene Adresse der rufenden Station>: Linked-Zustand, Operator- und Controller-Information, Lautsprecher ein, Twa starten.
* `AC-LINK-020-7` — Bei TWAS <eigene Adresse der rufenden Station> im ACK: Rückkehr in den Pre-Linking-Zustand.
* `AC-LINK-020-8` — Kommt das ACK nach Ablauf von Twr an, behandelt die Gegenstation es als neuen Einzelruf.

---

##### REQ-LINK-021 — Link Termination

**Spec-Referenz:** A.5.5.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Beendigung eines Links erfolgt durch das Senden eines Frames mit TWAS-Endung an alle verknüpften Stationen, die beendet werden sollen. Verbleibende Links mit anderen Stationen bleiben erhalten.

**Akzeptanzkriterien:**

* `AC-LINK-021-1` — Der Link wird durch TWAS beendet.
* `AC-LINK-021-2` — Alle verknüpften Stationen werden benachrichtigt.
* `AC-LINK-021-3` — Der Lautsprecher wird ausgeschaltet.
* `AC-LINK-021-4` — Die Station kehrt in den verfügbaren Zustand zurück.
* `AC-LINK-021-5` — Verknüpfungen mit anderen Stationen bleiben erhalten.

---

##### REQ-LINK-022 — Manual Termination

**Spec-Referenz:** A.5.5.3.5.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Operator muss eine Station manuell zurücksetzen können. Dabei werden Lautsprecher stummgeschaltet, der Controller in den verfügbaren Zustand versetzt und TWAS an alle betroffenen Stationen gesendet, sofern nicht überschrieben.

**Akzeptanzkriterien:**

* `AC-LINK-022-1` — Der Operator kann manuell eine Station zurücksetzen.
* `AC-LINK-022-2` — Der Lautsprecher wird stummgeschaltet.
* `AC-LINK-022-3` — Der Controller kehrt in den verfügbaren Zustand zurück.
* `AC-LINK-022-4` — TWAS wird gesendet.
* `AC-LINK-022-5` — Andere Links bleiben erhalten.

---

##### REQ-LINK-023 — Automatic Termination

**Spec-Referenz:** A.5.5.3.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn innerhalb Twa keine Aktivität erfolgt, wird der Link beendet, der Lautsprecher stummgeschaltet und der Controller kehrt in den verfügbaren Zustand zurück. TWAS ist empfohlen, aber nicht verpflichtend.

**Akzeptanzkriterien:**

* `AC-LINK-023-1` — Der Timer Twa wird gestartet.
* `AC-LINK-023-2` — Bei Aktivitätszeitüberschreitung wird der Link beendet.
* `AC-LINK-023-3` — Der Lautsprecher wird ausgeschaltet.
* `AC-LINK-023-4` — Die Station kehrt in den verfügbaren Zustand zurück.
* `AC-LINK-023-5` — Der Timer kann vom Operator deaktiviert werden.
* `AC-LINK-023-6` — Es wird empfohlen, TWAS zu senden.
* `AC-LINK-023-7` — Die Station kehrt in den verfügbaren Zustand zurück.

---

### FEAT-LINK-004 — Kanalwechsel & Kollisionserkennung

**Setzt um:** REQ-LINK-010–015, REQ-LINK-024
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009
**Status:** geplant

#### ALE States

```cpp
// Kanonischer Name: ALEState (DD-009).
// LISTENING_FOR_RESPONSE ist kein Outer State, sondern CallingPhase::LISTENING.
enum class ALEState : uint8_t {
    IDLE,       // = AVAILABLE; kein Ruf, kein Scan aktiv
    SCANNING,   // scannt Kanalset
    CALLING,    // sendet Call-Frame (CallingPhase aktiv)
    HANDSHAKE,  // empfängt oder sendet Handshake-Frame (HandshakePhase aktiv)
    LINKED,     // Verbindung hergestellt (LinkedPhase aktiv)
    SOUNDING,   // Sounding-Frame wird gesendet
    ERROR,      // interner Fehlerzustand
};
```

#### Kanalwechsel-Logik

```cpp
// Channel Rejection:
// - Operator / Controller verwirft Kanal
// - terminate_link()
// - update_lqa(worst)

// Busy Channel:
// - LBT erkennt Verkehr
// - Kanal überspringen
// - nach Erschöpfung aller Kanäle neu versuchen

// Exhausted Channel List:
// - alle Kanäle versucht
// - kein Link möglich
// - AVAILABLE / IDLE
// - Operator benachrichtigen
```

#### Kollisionserkennung

```cpp
bool detect_collision(uint32_t incoming_phase_ms, uint32_t expected_phase_ms) {
    return incoming_phase_ms != expected_phase_ms;
}
```

Semantisch gilt:

* Ein laufendes Frame wird nur fortgesetzt, wenn die Wortphase konsistent bleibt.
* Wird nach Wiedererlangung des Wort-Sync eine neue Wortphase beobachtet, ist das als Kollision zu behandeln.
* Das unterbrochene Frame wird verworfen.
* Das neu eintreffende Signal wird als neuer Frame verarbeitet.

---

##### REQ-LINK-010 — ALE States

**Spec-Referenz:** A.5.5.2.2 / Figure A-28
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein ALE-Controller kann als sich in einem von drei konzeptionellen Zuständen befindlich betrachtet werden.

**Akzeptanzkriterien:**

* `AC-LINK-010-1` — Der Controller kann sich in einem von drei Zuständen befinden.
* `AC-LINK-010-2` — Die Zustände sind konzeptionell definiert.
* `AC-LINK-010-3` — Die Abbildung A-28 zeigt die Zustände.

---

##### REQ-LINK-011 — Listen Before Transmit / Channel Selection

**Spec-Referenz:** A.5.5.3.1 / A.5.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Vor jeder Aussendung wird der Kanal zunächst abgehört und anschließend abgestimmt. Ein belegter Kanal darf nicht für einen neuen Ruf verwendet werden.

**Akzeptanzkriterien:**

* `AC-LINK-011-1` — Vor dem Senden wird auf dem Zielkanal abgehört.
* `AC-LINK-011-2` — Der Kanal wird vor dem Senden abgestimmt.
* `AC-LINK-011-3` — Ein belegter Kanal wird verworfen oder übersprungen.

---

##### REQ-LINK-012 — Channel Rejection

**Spec-Referenz:** A.5.5.3.1 / A.5.5.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wird ein Kanal vom Operator oder Controller verworfen, ist ein Link abzubrechen oder zu beenden, und die Link Quality muss auf den schlechtesten Wert gesetzt werden.

**Akzeptanzkriterien:**

* `AC-LINK-012-1` — Ein verworfener Kanal führt zur Beendigung des aktuellen Linkversuchs.
* `AC-LINK-012-2` — Die Link-Quality wird auf den schlechtesten Wert gesetzt.

---

##### REQ-LINK-013 — Busy Channel Handling

**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Erkennt LBT Verkehr, wird der Kanal übersprungen. Nach Erschöpfung des Kanalsets startet der Controller den Versuch neu.

**Akzeptanzkriterien:**

* `AC-LINK-013-1` — Ein belegter Kanal wird übersprungen.
* `AC-LINK-013-2` — Nach Erschöpfung aller Kanäle wird ein neuer Versuch gestartet.

---

##### REQ-LINK-014 — Exhausted Channel List

**Spec-Referenz:** A.5.5.3.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Sind alle Kanäle versucht und kein Link hergestellt worden, kehrt das System in den verfügbaren Zustand zurück und informiert den Operator.

**Akzeptanzkriterien:**

* `AC-LINK-014-1` — Nach Verwerfen aller Kanäle ist kein weiterer automatischer Versuch aktiv.
* `AC-LINK-014-2` — Der Controller kehrt in den verfügbaren Zustand zurück.
* `AC-LINK-014-3` — Der Operator wird benachrichtigt.

---

##### REQ-LINK-015 — Collision Detection

**Spec-Referenz:** A.5.5.3.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wird während des Empfangs eines ALE-Signals eine neue Wortphase nach Wiedererlangung des Wort-Sync erkannt, liegt eine Kollision vor. Das unterbrochene Frame ist zu verwerfen.

**Akzeptanzkriterien:**

* `AC-LINK-015-1` — Das System erkennt Unterbrechungen der Signal-Kontinuität.
* `AC-LINK-015-2` — Unkorrigierbare Fehler werden als Sync-Verlust behandelt.
* `AC-LINK-015-3` — Wort-Sync wird unter Bias zur ursprünglichen Wortphase wiederhergestellt.
* `AC-LINK-015-4` — Eine neue Wortphase wird als Kollision interpretiert.
* `AC-LINK-015-5` — Das laufende Frame wird verworfen.
* `AC-LINK-015-6` — Das neue Signal wird als neuer Frame verarbeitet.

---

##### REQ-LINK-024 — Collision Detection

**Spec-Referenz:** A.5.5.3.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Während des Empfangs eines ALE-Signals kann die Kontinuität des Signals verloren gehen. Wenn nach Wiedererlangung des Wort-Sync eine neue Wortphase beobachtet wird, ist dies als Kollision zu behandeln; das unterbrochene Frame wird verworfen, das neue Signal als eigener Frame verarbeitet.

**Akzeptanzkriterien:**

* `AC-LINK-024-1` — Die Kontinuität des Signals kann verloren gehen.
* `AC-LINK-024-2` — Unkorrigierbare Fehler werden erkannt.
* `AC-LINK-024-3` — Das Wort-Sync wird wiederhergestellt.
* `AC-LINK-024-4` — Bei neuer Wortphase wird eine Kollision angezeigt.
* `AC-LINK-024-5` — Das unterbrochene Frame wird verworfen.
* `AC-LINK-024-6` — Das unterbrechende Signal wird als neuer Frame verarbeitet.

---

### FEAT-LINK-005 — Frame- und Adress-Konstruktion

**Setzt um:** REQ-LINK-001, REQ-LINK-002, REQ-LINK-006, REQ-LINK-017, REQ-LINK-018, REQ-LINK-019, REQ-LINK-020, REQ-LINK-025
**Modul:** `src/Protocol/Control/word_parser.cpp`, `src/Protocol/Control/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-009, DD-010
**Status:** geplant

#### Technischer Entwurf

Diese Funktion kapselt die syntaktische Konstruktion aller ALE-Wortfolgen. Sie ist die Quelle der Wahrheit für Scanning Call, Leading Call, Response, ACK und TWAS.

```text
build_scanning_call(address):
  - nehme nur first_word(address)
  - verwende TO
  - wiederhole bis erforderliche Zeit erreicht ist

build_leading_call(address):
  - zerlege vollständige Adresse in Wortsegmente
  - erstes Wort mit TO
  - weitere Segmente mit DATA / REP
  - sende die komplette Adresse zweimal

build_conclusion(self_address, kind):
  - kind = TIS oder TWAS
  - Abschlusswort enthält die eigene Adresse
  - wird unmittelbar nach Message oder Leading Call gesendet
```

#### Adresssegmentierung

* bis 3 Zeichen: ein Wort
* bis 6 Zeichen: zwei Wörter
* bis 9 Zeichen: drei Wörter
* bis 12 Zeichen: vier Wörter
* bis 15 Zeichen: fünf Wörter
* darüber hinaus: ungültig

#### Integrationsregel

Jede Protokollphase verwendet dieselbe Konstruktion:

* Call: `Scanning Call + Leading Call + optional Message + Conclusion`
* Response: `Leading Call der Gegenstation + Conclusion`
* ACK: `Leading Call der Zieladresse + Conclusion`
* Termination: `Leading Call der zu terminierenden Zieladresse + TWAS`

---

##### REQ-LINK-025 — Frame Construction and Address Segmentation

**Spec-Referenz:** A.5.2.5, A.5.2.5.1, A.5.5.3.1, A.5.5.3.4, A.5.5.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Alle ALE-Frames müssen aus gültigen, redundanten Wortfolgen in der korrekten Reihenfolge konstruiert werden. Scanning Calls verwenden nur das erste Adresswort, Leading Calls die vollständige Adresse zweimal, und Conclusions enthalten die eigene Adresse der sendenden Station. Adressen über fünf Wörter sind ungültig.

**Akzeptanzkriterien:**

* `AC-LINK-025-1` — Scanning Call enthält nur das erste Adresswort.
* `AC-LINK-025-2` — Leading Call enthält die vollständige Zieladresse zweimal.
* `AC-LINK-025-3` — Conclusions enthalten die eigene Adresse der sendenden Station.
* `AC-LINK-025-4` — Adressen mit mehr als fünf Wörtern werden als ungültig verworfen.
* `AC-LINK-025-5` — Die Wortfolge bleibt in allen Protokollphasen syntaktisch konsistent.

---

### FEAT-LINK-006 — One-to-Many: Slotted Responses & Star Net

**Setzt um:** REQ-LINK-003, REQ-LINK-026–034
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-012
**Status:** geplant

#### Technischer Entwurf

```cpp
// Tswt-Berechnung (A.5.5.4.1.3, DD-012):
uint32_t calculate_tswt_ms(uint8_t slot_number,
                            uint32_t ta_caller_ms,
                            uint32_t ta_called_ms[],  // alle vorherigen Slots
                            uint8_t  num_previous_slots,
                            uint32_t optional_tm_ms = 0) {
    uint32_t base = slot_number * (5 * Tw_ms + 2 * ta_caller_ms + optional_tm_ms);
    base += ta_caller_ms;  // Slot-0-Beitrag
    for (int m = 0; m < num_previous_slots; ++m)
        base += ta_called_ms[m];
    return base;
}

// Twrn (aufrufende Station):
uint32_t twrn_ms = calculate_tswt_ms(total_slots + 1, ...);
// Twan (gerufene Stationen):
uint32_t twan_ms = twrn_ms + 2 * Trw_ms;

// Star Net Call = Individual Call mit Net-Adresse als TO
// Antwort mit Net-Member-Adresse (nicht der Net-Adresse)
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-LINK-003 — Group Call

**Spec-Referenz:** A.5.2.5.1 / A.5.2.3.4.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Ein Gruppenruf adressiert mehrere Stationen; der Scanning-Call verwendet alternierende Routing-Wörter, der Leading-Call die vollständigen Adressen aller Ziele.

**Akzeptanzkriterien:**

* `AC-LINK-003-1` — Jede Zieladresse erscheint im Leading-Call vollständig.
* `AC-LINK-003-2` — Die Scanning-Sequenz endet nicht mit einem Wiederholungswort.

---

##### REQ-LINK-026 — One-to-Many Calling Protocol

**Spec-Referenz:** A.5.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine Station kann gleichzeitig eine Mehr-Wege-Verbindung mit mehreren anderen Stationen herstellen, unter Verwendung der in den folgenden Unterabschnitten beschriebenen Protokolle.

**Akzeptanzkriterien:**

* `AC-LINK-026-1` — Eine Station kann gleichzeitig mit mehreren Stationen verknüpfen.
* `AC-LINK-026-2` — Die Protokolle sind beschrieben.

---

### FEAT-LINK-007 — One-to-Many: Star Group Call

**Setzt um:** REQ-LINK-035–043
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf

```cpp
// Group Call: Scanning = THRU/REP alternierend, max 5 einzigartige Erstworte
// Leading Call: TO + vollständige Adressen aller Ziele, max 12 Wörter total
// Zweites Ziel: REP statt TO (wenn direkt nach TO)

struct GroupCallState {
    std::vector<std::string> targets;      // max ~4 Ziele (12 Wörter total)
    std::vector<ALEWord>     scanning_seq; // THRU/REP-Rotation
    std::vector<ALEWord>     leading_seq;  // TO/DATA/REP-Kette aller Ziele
};

// Slot-Ableitung beim Empfang (REQ-LINK-039):
// Empfänger liest THRU/REP-Wörter → findet eigene Adresse
// Zählt Wörter danach → SN
// Falls zu spät: Twan_max = 107·Tw + 27·Ta(caller) + ... (Standard)
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-LINK-035 — Star Group Calling Protocol

**Spec-Referenz:** A.5.5.4.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das Gruppenrufprotokoll erweitert die Leistung von Mehr-Wege-Rufen auf ad-hoc-Sammlungen von Stationen, die nicht im Voraus als Net programmiert wurden. Es muss nichts über die Stationen außer ihren individuellen Adressen und gescannten Frequenzen bekannt sein. Da eine Gruppe nicht im Voraus eingerichtet ist, müssen Stationen in der Lage sein, Gruppenmitgliedschaft und Slot-Parameter dynamisch abzuleiten. Die Gruppenmitgliedschaft ist wie folgt begrenzt: Die Gesamtlänge der individuellen Adressen der Gruppenmitglieder darf 12 ALE-Wörter nicht überschreiten. Die Menge der einzigartigen ersten Adresswörter unter den Gruppenmitgliedern darf fünf Wörter nicht überschreiten.

**Akzeptanzkriterien:**

* `AC-LINK-035-1` — Das Gruppenrufprotokoll erweitert Mehr-Wege-Rufen.
* `AC-LINK-035-2` — Die Gruppe ist ad-hoc.
* `AC-LINK-035-3` — Es muss nichts über die Stationen außer ihren Adressen und Frequenzen bekannt sein.
* `AC-LINK-035-4` — Gruppenmitgliedschaft ist begrenzt.
* `AC-LINK-035-5` — Die Gesamtlänge darf 12 Wörter nicht überschreiten.
* `AC-LINK-035-6` — Die Menge der einzigartigen ersten Adresswörter darf fünf Wörter nicht überschreiten.

---

### FEAT-LINK-008 — AllCall, AnyCall & Wildcard Protokolle

**Setzt um:** REQ-LINK-044, REQ-LINK-045, REQ-LINK-046
**Modul:** `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf

```cpp
// AllCall @?@ oder selective: hören, kein Response
// Erkennungslogik:
bool is_allcall_addressed(const char* received_addr, const SelfAddressStore& sa) {
    if (strcmp(received_addr, "@?@") == 0) return true;  // global
    // Selective: letztes Zeichen von received_addr matcht letztes Zeichen
    //            einer Selbstadresse
    return sa.any_last_char_matches(received_addr[strlen(received_addr)-1]);
}

// AnyCall @@?: zufälliger Slot 1-16, Slotbreite 20·Tw (23·Tw mit LQA)
uint8_t random_slot = (rand() % 16) + 1;
uint32_t slot_width_ms = lqa_requested ? 23 * Tw_ms : 20 * Tw_ms;

// Wildcard: TWAS → AllCall-Logik, TIS → AnyCall-Logik
// Alle drei über OperatingParameters abschaltbar, Standard: aktiviert
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-LINK-044 — AllCall Protocol

**Spec-Referenz:** A.5.5.4.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AllCall fordert alle anhörenden Stationen auf, zu hören, aber nicht zu antworten. Die AllCall-Spezialadressstruktur(en) (siehe A.5.2.4.7) müssen die einzigen Mitglieder des Scanning-Aufrufs und des führenden Aufrufs sein und dürfen nicht in irgendeinem anderen Adressfeld oder Teil des Handshakes verwendet werden. Die globale AllCall-Adresse darf nur in TO-Wörtern verwendet werden. Selektive AllCalls mit mehr als einer selektiven AllCall-Adresse verwenden jedoch Gruppenadressierung, unter Verwendung von THRU während des Scanning-Aufrufs und TO während des führenden Aufrufs. Ein AllCall betrifft eine ALE-Station, wenn es sich um eine globale AllCall handelt oder wenn ein selektiver AllCall ein Zeichen enthält, das mit dem letzten Zeichen einer von dieser Station zugewiesenen Adresse übereinstimmt. Bei Empfang eines relevanten AllCall sendet eine ALE-Station eine vordefinierte begrenzte Zeit, Tcc max.
• Wenn keine Nachrichtenabschnitt oder Frame-Schlussfolge innerhalb von Tcc max empfangen wird, kehrt der Controller automatisch zum Scanning zurück.
• Wenn ein Quick-ID (eine Adresse, die mit einem FROM-Wort beginnt, direkt nach dem Aufrufzyklus) empfangen wird, wird die Pause für den Nachrichtenabschnitt um maximal fünf Wörter (5 Trw) erweitert, und wenn kein CMD empfangen wird, kehrt der Controller zum Scanning zurück.
• Wenn eine Nachricht empfangen wird (indiziert durch Empfang eines CMD), wartet der Controller eine vordefinierte begrenzte Zeit, Tm max, um die Nachricht zu lesen. Wenn die Frame-Schlussfolge nicht innerhalb von Tm max empfangen wird, kehrt der Controller automatisch zum Scanning zurück.
• Wenn eine Schlussfolge empfangen wird (indiziert durch Empfang eines TIS oder TWAS), wartet der Controller (für eine vordefinierte begrenzte Zeit, Tx max), um die Adresse des Aufrufers zu lesen. Wenn das Ende des Signals nicht innerhalb von Tx max empfangen wird, kehrt der Controller automatisch zum Scanning zurück.
• Wenn ein relevanter AllCall-Frame erfolgreich empfangen und mit TIS abgeschlossen wird, wechselt der Controller in den verknüpften Zustand, benachrichtigt den Operator, schaltet den Lautsprecher ein und setzt einen Wartezeit-Timer.
• Wenn ein AllCall erfolgreich mit TWAS abgeschlossen wird, kehrt die gerufene Station automatisch zum Scanning zurück und antwortet nicht (es sei denn, dies wird vom Operator oder Controller direkt angeordnet).
• Wenn eine Station, die einen AllCall empfängt, einen Handshake initiieren möchte, kann der Operator einen Handshake innerhalb der Pause nach einer TIS-Schlussfolge initiieren.
• Beachten Sie, dass in allen Handshakes (der ursprüngliche AllCall bildet keinen Handshake) die AllCall-Adresse nicht verwendet wird.
• Um mögliche negative Auswirkungen durch Übernutzung oder Missbrauch von AllCalls zu minimieren, müssen Controller die Fähigkeit haben, AllCalls zu ignorieren. Normalerweise sollte AllCall-Verarbeitung aktiviert sein.

**Akzeptanzkriterien:**

* `AC-LINK-044-1` — Ein AllCall fordert Stationen auf, zu hören.
* `AC-LINK-044-2` — Ein AllCall fordert keine Antworten an.
* `AC-LINK-044-3` — Die AllCall-Adresse wird nur in TO-Wörtern verwendet.
* `AC-LINK-044-4` — Selektive AllCalls verwenden Gruppenadressierung.
* `AC-LINK-044-5` — Die Station wird identifiziert.
* `AC-LINK-044-6` — Die Pause wird gestartet.
* `AC-LINK-044-7` — Bei Quick-ID wird die Pause erweitert.
* `AC-LINK-044-8` — Bei Nachricht wird eine Zeit für das Lesen verwendet.
* `AC-LINK-044-9` — Bei Schlussfolge wird eine Zeit für das Lesen verwendet.
* `AC-LINK-044-10` — Bei TIS wird der Zustand gewechselt.
* `AC-LINK-044-11` — Bei TWAS kehrt die Station zum Scanning zurück.
* `AC-LINK-044-12` — Der Operator kann einen Handshake initiieren.
* `AC-LINK-044-13` — AllCall-Adresse wird nicht verwendet.
* `AC-LINK-044-14` — Controller können AllCalls ignorieren.
* `AC-LINK-044-15` — AllCall-Verarbeitung sollte aktiviert sein.

---

##### REQ-LINK-045 — AnyCall Protocol

**Spec-Referenz:** A.5.5.4.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein AnyCall ist ähnlich wie ein AllCall, aber es fordert Antworten an. Die Verwendung der AnyCall-Spezialadressstruktur(en) ist identisch mit der für die AllCall-Spezialadressstruktur(en). Bei Empfang eines relevanten AnyCall prüft eine ALE-Station den Aufruf gleich wie bei AllCalls, inklusive der Tcc max, Tm max und Tx max Grenzen. Wenn der AnyCall erfolgreich empfangen wurde und mit TIS abgeschlossen wird, wechselt der Controller in den Link-Zustand und generiert automatisch eine slotted Antwort gemäß A.5.5.4.1 und den folgenden speziellen Verfahren:
• Da keine vorprogrammierten oder abgeleiteten Slot-Daten verfügbar sind, wählt der Controller zufällig eine Slot-Nummer, 1 bis 16.
• Jeder Slot soll 20 Tw (2613,33... ms) breit sein, es sei denn, der aufrufende Station fordert LQA-Antworten an; in diesem Fall werden die Slots um 3 Tw auf 23 Tw erweitert, um den CMD-LQA-Nachrichtenabschnitt zu unterstützen.
• Der Controller berechnet Werte für Tswt und Twan unter Verwendung dieser Slot-Breite und seiner zufälligen Slot-Nummer.
• Slot 0 wird für das Tunen verwendet, wie bei slotted Response-Protokollen üblich.
• Nach Ablauf des Tswt-Zeitfensters sendet die Station eine Standard-Star-Net-Antwort, bestehend aus TO (mit der Adresse des Aufrufers) und TIS (mit der Adresse des Antworters), mit dem LQA-CMD, falls angefordert.
• Antwortende verwenden eine eigene Adresse, die nicht länger als fünf Wörter minus zweimal die Länge der Aufrufadresse ist.
• Die AnyCall-Spezialadresse wird nicht in der Bestätigung gesendet.
• In diesem Protokoll werden Kollisionen erwartet und toleriert. Die Station, die den AnyCall sendet, versucht, die beste Antwort in jedem Slot zu lesen.
• Nach Empfang der slotted Antworten sendet die aufrufende Station eine ACK an eine Subset der Stationen, deren Antworten gelesen wurden, unter Verwendung einer individuellen oder Gruppenadresse. Die AnyCall-Spezialadresse wird nicht in der Bestätigung gesendet.
• Der aufrufende Station wählt die Schlussfolge der ACK, um den Link für zusätzliche Interoperation und Verkehr mit den Antwortenden aufrechtzuerhalten (TIS), oder sie kehrt alle zurück in das Scanning (TWAS), wie angemessen für den ursprünglichen Zweck des Aufrufers.
• Eine ALE-Station, die auf einen AnyCall geantwortet hat, wartet auf die Bestätigung und verarbeitet eine eingehende Bestätigung gemäß A.5.5.4.3.6.
• Um mögliche negative Auswirkungen durch Übernutzung oder Missbrauch von AnyCalls zu minimieren, müssen Controller die Fähigkeit haben, AnyCalls zu ignorieren. Normalerweise sollte AnyCall-Verarbeitung aktiviert sein.

**Akzeptanzkriterien:**

* `AC-LINK-045-1` — Ein AnyCall fordert Antworten an.
* `AC-LINK-045-2` — Die Verwendung der AnyCall-Adresse ist identisch mit AllCall.
* `AC-LINK-045-3` — Die Station prüft den Aufruf wie bei AllCall.
* `AC-LINK-045-4` — Bei TIS wird ein Link-Zustand gewechselt.
* `AC-LINK-045-5` — Die Station generiert eine slotted Antwort.
* `AC-LINK-045-6` — Die Station wählt zufällig eine Slot-Nummer.
* `AC-LINK-045-7` — Die Slot-Breite wird berechnet.
* `AC-LINK-045-8` — Slot 0 wird für das Tunen verwendet.
* `AC-LINK-045-9` — Die Antwort wird gesendet.
* `AC-LINK-045-10` — Kollisionen werden erwartet und toleriert.
* `AC-LINK-045-11` — Die aufrufende Station liest die beste Antwort.
* `AC-LINK-045-12` — Die aufrufende Station sendet eine ACK.
* `AC-LINK-045-13` — Die AnyCall-Adresse wird nicht verwendet.
* `AC-LINK-045-14` — Controller können AnyCalls ignorieren.
* `AC-LINK-045-15` — AnyCall-Verarbeitung sollte aktiviert sein.

---

##### REQ-LINK-046 — Wildcard Calling Protocol

**Spec-Referenz:** A.5.5.4.6
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wildcard-Adressen müssen die einzigen Mitglieder eines Aufrufs in einem Aufruf sein und dürfen nicht in irgendeinem anderen Adressfeld oder Teil des Handshakes verwendet werden. Der Umfang (Anzahl der Fälle) der verwendeten Wildcards sollte auf die essentiellen Bedürfnisse des/der Benutzer/der Benutzer reduziert werden. Aufrufe an Wildcard-Adressen, die mit TWAS abgeschlossen werden, werden identisch mit dem AllCall-Protokoll verarbeitet. Antworten auf Wildcard-Aufrufe, die mit TIS abgeschlossen werden, werden in pseudozufällig ausgewählten Slots gesendet, gemäß dem AnyCall-Protokoll. Wie bei AllCall und AnyCall kann der Controller programmierbar sein, um Wildcard-Aufrufe zu ignorieren, aber Wildcard-Aufruf-Verarbeitung sollte normalerweise aktiviert sein.

**Akzeptanzkriterien:**

* `AC-LINK-046-1` — Wildcard-Adressen sind die einzigen Mitglieder des Aufrufs.
* `AC-LINK-046-2` — Wildcard-Adressen werden nicht in anderen Adressfeldern verwendet.
* `AC-LINK-046-3` — Der Umfang der Wildcards ist begrenzt.
* `AC-LINK-046-4` — Aufrufe mit TWAS werden wie AllCall verarbeitet.
* `AC-LINK-046-5` — Antworten mit TIS werden in pseudozufälligen Slots gesendet.
* `AC-LINK-046-6` — Die Verarbeitung kann ignoriert werden.
* `AC-LINK-046-7` — Wildcard-Aufruf-Verarbeitung sollte aktiviert sein.

---

### FEAT-ADDR-001 — Basic-38-Zeichensatz & Adressvalidierung

**Setzt um:** REQ-ADDR-001, REQ-ADDR-002, REQ-ADDR-016
**Modul:** `include/Word/ale_word.h`, `src/Word/ale_word.cpp`
**Status:** implementiert

#### Technischer Entwurf

```cpp
bool is_valid_ale_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '@' || ch == '?';
}
// Validierung NICHT auf Top-3-Bits beschränkt (REQ-ADDR-002)
```

---

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-ADDR-001 — Digitale Adressstruktur und Speicherkapazität

**Spec-Referenz:** A.5.2.4.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine digitale Adressstruktur auf Basis des standardisierten 24-Bit-Worts und des Basic-38-Zeichensatzes verwenden. ALE-Stationen müssen die Fähigkeit besitzen, mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen einzeln oder gemeinsam zu verlinken oder zu vernetzen. Alle ALE-Stationen müssen mindestens 20 eigene Adressen mit jeweils bis zu 15 Zeichen in beliebiger Kombination aus Einzel- und Netzrufen speichern und verwenden können. Das Standardwerk unterscheidet drei grundlegende Adressierungsarten: Einzelstation, Mehrfachstation und Sondermodi. Bestimmte alphanumerische Adresskombinationen können eine besondere Bedeutung für Notfälle oder spezifische Funktionen haben; solche Kombinationen sollten sorgfältig kontrolliert oder eingeschränkt werden.

**Akzeptanzkriterien:**

* `AC-ADDR-001-1` — Das System verwendet eine digitale Adressstruktur auf Basis des 24-Bit-Worts und des Basic-38-Zeichensatzes.
* `AC-ADDR-001-2` — Eine Station kann mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen verlinken oder vernetzen.
* `AC-ADDR-001-3` — Das System kann mindestens 20 eigene Adressen verwalten.
* `AC-ADDR-001-4` — Jede dieser eigenen Adressen kann bis zu 15 Zeichen lang sein.
* `AC-ADDR-001-5` — Das System unterstützt Einzelstation-, Mehrfachstation- und Sondermodi der Adressierung.
* `AC-ADDR-001-6` — Adresskombinationen mit Sonderbedeutung werden kontrolliert oder eingeschränkt behandelt.

**Vom-Standard-vorgegebene Werte:**

| Parameter                                  | Wert | Einheit  | Spec-Referenz |
| ------------------------------------------ | ---: | -------- | ------------- |
| Basis-Wortlänge                            |   24 | Bit      | A.5.2.4.1     |
| Mindestanzahl eigener Adressen pro Station |   20 | Adressen | A.5.2.4.1     |
| Maximale Länge je eigener Adresse          |   15 | Zeichen  | A.5.2.4.1     |
| Anzahl grundlegender Adressierungsarten    |    3 | Arten    | A.5.2.4.1     |

##### REQ-ADDR-002 — Basic-38-Zeichensatz und gültige Basisadresse

**Spec-Referenz:** A.5.2.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der Basic-38-ASCII-Zeichensatz muss alle Großbuchstaben A bis Z und alle Ziffern 0 bis 9 sowie die Sonderzeichen „@“ und „?“ enthalten. Der Basic-38-ASCII-Zeichensatz muss für alle grundlegenden Adressierungsfunktionen verwendet werden. Eine gültige Basisadresse muss ein Routing-Präambelwort aus A.5.2.3.2 sowie drei alphanumerische Zeichen aus dem Basic-38-ASCII-Zeichensatz in beliebiger Kombination enthalten. Die Sonderzeichen „@“ und „?“ müssen für spezielle Funktionen verwendet werden. Die Unterscheidung des Basic-38-ASCII-Zeichensatzes darf nicht darauf beschränkt sein, nur die drei höchstwertigen Bits zu prüfen.

**Akzeptanzkriterien:**

* `AC-ADDR-002-1` — Der Basic-38-ASCII-Zeichensatz umfasst A bis Z, 0 bis 9, „@“ und „?“.
* `AC-ADDR-002-2` — Der Basic-38-ASCII-Zeichensatz wird für alle grundlegenden Adressierungsfunktionen verwendet.
* `AC-ADDR-002-3` — Eine gültige Basisadresse enthält ein Routing-Präambelwort und drei Zeichen aus dem Basic-38-ASCII-Zeichensatz.
* `AC-ADDR-002-4` — Die Sonderzeichen „@“ und „?“ werden für spezielle Funktionen verwendet.
* `AC-ADDR-002-5` — Die Erkennung von Basic-38-Zeichen darf nicht allein auf den drei höchstwertigen Bits beruhen.

**Vom-Standard-vorgegebene Werte:**

| Parameter                                         | Wert | Einheit | Spec-Referenz |
| ------------------------------------------------- | ---: | ------- | ------------- |
| Anzahl Großbuchstaben                             |   26 | Zeichen | A.5.2.4.2     |
| Anzahl Ziffern                                    |   10 | Zeichen | A.5.2.4.2     |
| Anzahl Sonderzeichen für den Basic-38-Satz        |    2 | Zeichen | A.5.2.4.2     |
| Anzahl alphanumerischer Zeichen für Basisadressen |    3 | Zeichen | A.5.2.4.2     |

##### REQ-ADDR-016 — Adressvalidierung und Kompatibilitätsregeln

**Spec-Referenz:** A.5.2.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss empfangene Adressen validieren und sicherstellen, dass nur gültige Adressmuster gemäß dem Basic-38-Zeichensatz und den definierten Adressstrukturen verarbeitet werden. Reservierte oder unbekannte Adressmuster müssen als ungültig behandelt werden. ALE-Stationen müssen rückwärtskompatibel mit Implementierungen arbeiten, die nur Basisadressen unterstützen.

**Akzeptanzkriterien:**

* `AC-ADDR-016-1` — Empfangene Adressen werden gegen den Basic-38-Zeichensatz und die definierten Adressstrukturen validiert.
* `AC-ADDR-016-2` — Reservierte oder unbekannte Adressmuster werden als ungültig verworfen.
* `AC-ADDR-016-3` — Das System arbeitet rückwärtskompatibel mit Implementierungen, die nur Basisadressen unterstützen.

**→ FÜR FEATURE-DOKUMENT (Implementierungsdetails, hier ausgelagert):**

* **Adress-Normalizer (A.5.2.4.3 / A.5.2.4.10)** — Zerlegt Eingaben und gespeicherte Self-Addresses in 3-Zeichen-Wörter und ergänzt bei Bedarf mit @. Vorschlag Rückverweis: implements REQ-ADDR-003, REQ-ADDR-013, REQ-ADDR-014
* **Adress-Route-Assembler (A.5.2.4.4–A.5.2.4.12)** — Bildet daraus die Wortfolge TO/DATA/REP/TIS/TWAS/THRU gemäß Adresstyp und Calling Cycle. Vorschlag Rückverweis: implements REQ-ADDR-004, REQ-ADDR-006, REQ-ADDR-007, REQ-ADDR-008, REQ-ADDR-009, REQ-ADDR-010, REQ-ADDR-011, REQ-ADDR-012, REQ-ADDR-015
* **Address-Policy-Validator (A.5.2.4)** — Prüft Basic-38, Reserved Patterns und Rückwärtskompatibilität. Vorschlag Rückverweis: implements REQ-ADDR-016

---

### FEAT-ADDR-002 — Adress-Chunking, Stuffing & Erweiterung

**Setzt um:** REQ-ADDR-003–007
**Modul:** `src/Word/ale_word.cpp`, `src/Protocol/ale_state_machine.cpp`
**Design-Entscheidungen:** DD-007
**Status:** implementiert

#### Technischer Entwurf
```
chunk_address("W1AWJ") → ["W1A", "WJ@"]
chunk_address("W1A")   → ["W1A"]
chunk_address("EDWARD")→ ["EDW", "ARD"]
words_for_address(addr) = ceil(min(len, 15) / 3)  // max 5
```

| Adresslänge | Wörter | Sequenz |
|---|---|---|
| 1–3 | 1 | TO |
| 4–6 | 2 | TO, DATA |
| 7–9 | 3 | TO, DATA, REP |
| 10–12 | 4 | TO, DATA, REP, DATA |
| 13–15 | 5 | TO, DATA, REP, DATA, REP |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-ADDR-003 — Stuffing nicht vollständiger Adressen

**Spec-Referenz:** A.5.2.4.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Adressen, deren Länge kein Vielfaches von drei Zeichen ist, müssen kompatibel in standardisierte Adressfelder aufgenommen werden, indem die freien nachlaufenden Positionen mit dem Utility-Zeichen „@“ aufgefüllt werden. Die Wörter „Stuff-1“ und „Stuff-2“ dürfen nur im letzten Wort einer Adresse verwendet werden und sollen daher nur im führenden Ruf des Calling Cycles erscheinen.

**Akzeptanzkriterien:**
- `AC-ADDR-003-1` — Nicht durch drei teilbare Adresslängen werden durch Auffüllen der nachlaufenden Positionen mit „@“ in standardisierte Adressfelder überführt.
- `AC-ADDR-003-2` — Stuff-1 und Stuff-2 werden nur im letzten Wort einer Adresse verwendet.
- `AC-ADDR-003-3` — Stuff-1 und Stuff-2 erscheinen nur im führenden Ruf des Calling Cycles.

### 10.4 Individual addresses — A.5.2.4.4

---

### FEAT-ADDR-003 — Net, Group, AllCall, AnyCall Adressen

**Setzt um:** REQ-ADDR-008–011
**Modul:** `src/Protocol/ale_state_machine.cpp`, `src/Word/ale_word.cpp`
**Status:** geplant

#### Technischer Entwurf

| Adresstyp | Pattern | Routing-Wort | Antwort |
|---|---|---|---|
| Net | beliebige Adresse aus SA | TO | Netz-Mitglieder mit Slot |
| Group | mehrere individuelle Adressen | TO/THRU/REP | jede Station mit Slot |
| AllCall | `@?@` oder `@A@` | TO | keine |
| Selective AllCall | `@A@` + THRU REP `@B@` | TO/THRU | keine |
| AnyCall | `@@?` oder `@@A` | TO | zufälliger Slot |
| Double-Selective AnyCall | `@AB` | TO | zufälliger Slot |

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-ADDR-008 — Netzruf mit gemeinsamer Netadresse

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

---

### FEAT-ADDR-004 — Wildcard-Matching

**Setzt um:** REQ-ADDR-012
**Modul:** `include/Word/ale_word.h`, `src/Word/ale_word.cpp`
**Status:** implementiert

#### Technischer Entwurf
```cpp
bool match_wildcard(const char* pattern, const char* address) {
    if (strlen(pattern) != strlen(address)) return false;
    for (size_t i = 0; i < strlen(pattern); ++i) {
        if (pattern[i] == '?') continue;  // matcht alles
        if (pattern[i] != address[i]) return false;
    }
    return true;
}
```

---



#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-ADDR-012 — Wildcard-Zeichen und gleichlange Adresslängen

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

---

### FEAT-ADDR-005 — Self, Null, In-Link Adressen

**Setzt um:** REQ-ADDR-013, REQ-ADDR-014, REQ-ADDR-015
**Modul:** `src/Word/ale_word.cpp`, `src/Protocol/ale_state_machine.cpp`
**Status:** geplant

#### Technischer Entwurf
```cpp
// Null-Adresse: "@@@ " → kein Ziel, kein Response, nur im Calling Cycle
constexpr const char* NULL_ADDRESS = "@@@";
bool is_null_address(const char* addr) { return strcmp(addr, NULL_ADDRESS) == 0; }

// In-Link-Adresse: "?@?" → alle Link-Mitglieder
constexpr const char* INLINK_ADDRESS = "?@?";
// Im LINKED-Zustand: SelfAddressStore enthält zusätzlich INLINK_ADDRESS
void ALEStateMachine::add_inlink_address_when_linked();

// Self-Adresse: Aus SelfAddressStore, mehrere erlaubt (mind. 1 Einwort-Adresse)
// Selbstadressierung für Test/Wartung: Calling Cycle mit eigener Adresse als TO
```

---


#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### REQ-ADDR-013 — Selbstadressierung mit eigenen Adressen

**Spec-Referenz:** A.5.2.4.10
**Priorität:** MUST · **Status:** offen

**Anforderung:** Für Selbsttest, Wartung und andere Zwecke müssen Stationen in der Lage sein, ihre eigenen Adressen in Rufen zu verwenden. Wenn eine Selbstadressierungsfunktion erforderlich ist, müssen die folgenden Selbstadressierungsstrukturen und -protokolle verwendet werden. Alle im Standard zulässigen Rufstrukturen und -protokolle, die einen spezifisch adressierten Calling Cycle enthalten, müssen akzeptabel sein, sofern es sich nicht um AllCall oder AnyCall handelt. Die Station darf dabei eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

**Akzeptanzkriterien:**
- `AC-ADDR-013-1` — Stationen können ihre eigenen Adressen in Rufen verwenden.
- `AC-ADDR-013-2` — Selbstadressierung wird für Selbsttest, Wartung und andere Zwecke unterstützt.
- `AC-ADDR-013-3` — Spezifisch adressierte Calling Cycles sind zulässig, sofern sie kein AllCall und kein AnyCall sind.
- `AC-ADDR-013-4` — Eine Station darf eine oder mehrere ihrer eigenen Rufadressen in den Calling Cycle einsetzen oder hinzufügen.

### 10.14 Null address — A.5.2.4.11

---

##### REQ-ADDR-014 — Null-Adresse ohne Ziel, Antwort oder Annahme

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

---

##### REQ-ADDR-015 — In-Link-Adresse für alle Mitglieder eines Links

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
## 11. Nachrichten — A.5.7

> **Spec-Stellen sammeln:** A.5.7.1 (Overview), A.5.7.2 (AMD), A.5.7.3 (DTM), A.5.7.4 (DBM).

### EPIC-MSG · Nachrichtenübertragung

Die Nachrichtenübertragung wird als Orderwire-Funktion innerhalb der Standard-ALE-Frame-Struktur umgesetzt. Der Coding Agent muss das Protokoll-Framework so behandeln, dass die Nachrichtensektion, die Wortfolgen und die Anzeige-/Pufferregeln der jeweiligen Unterprotokolle getrennt und vollständig abgebildet werden.

### FEAT-GEN-013 — ALE Message-Protocol Framework

**Setzt um:** A.5.7.1
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Technischer Entwurf

| Protokoll | Mandatory | Zeichensatz / Nutzdaten | Peak Throughput | ARQ |
|---|---|---|---|---|
| AMD | ja | Expanded 64 | 55 b/s | N |
| DTM | nein | ASCII oder unformatierte Binärbits | 61 b/s | N |
| DBM | nein | ASCII oder unformatierte Binärbits | 187 b/s | Opt |

Der ALE-Stack muss drei Message-Protokolle für Nutzdaten unterstützen: AMD, DTM und DBM. Alle ALE-Controller, die diesem Appendix entsprechen, müssen AMD implementieren. DTM und DBM sind optionale Orderwire-Protokolle mit höherer Robustheit beziehungsweise höherer Datenrate.

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### A.5.7.1 — Overview

**Anforderung:** Drei Message-Protokolle stehen für Nutzdaten über die ALE-Wellenform und Signalstruktur zur Verfügung. Die Eigenschaften dieser Protokolle sind in Tabelle A-XXXI zusammengefasst. Alle ALE-Controller, die diesem Appendix entsprechen, müssen AMD implementieren.

**Wesentliche Vorgaben:**
- Es gibt drei Protokolle für User Data: AMD, DTM und DBM.
- AMD ist verpflichtend.
- DTM und DBM sind optional.
- Die Protokolle unterscheiden sich in Zeichensatz, Peak Throughput und ARQ-Verhalten.

---

### FEAT-GEN-014 — Automatic Message Display (AMD)

**Setzt um:** A.5.7.2
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`, `include/Protocol/ale_message.h`, `src/Protocol/ale_message.cpp`
**Status:** geplant

#### Technischer Entwurf

AMD transportiert einfache ASCII-Textnachrichten mit bestehender Stationsausrüstung. Verwendet wird der Expanded-64-Zeichensatz. Der Nachrichtentext wird in der Standard-Wortstruktur als `CMD AMD` gefolgt von alternierenden `DATA`- und `REP`-Wörtern geführt. Eine AMD-Nachricht bleibt als geschlossene Einheit erhalten, wird in einem einzelnen Frame übertragen und darf nicht über mehrere Frames verteilt werden.

Die Station muss empfangene AMD-Nachrichten direkt an Operator und Controller anzeigen und bei Ankunft alarmieren können. Anzeige und Alarm müssen abschaltbar sein, wenn dies betrieblich erforderlich ist.

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### A.5.7.2.1 — Expanded 64-channel subset

**Anforderung:** Der Expanded-64-ASCII-Satz umfasst alle Großbuchstaben A–Z, alle Ziffern 0–9, die Utility-Symbole `@` und `?` sowie 26 weitere häufig verwendete Zeichen. Er wird für alle Basic-Orderwire-Nachrichten und standardisierte Sonderfunktionen verwendet. Für Orderwire-Nachrichten müssen die Zeichen in einer Folge aus `DATA`- und `REP`-Wörtern stehen und von einem zugehörigen `CMD`-Wort eingeleitet werden. Dieses `CMD` muss wiederum von einem gültigen Calling Cycle mit Basic-38-Adressierung eingeleitet werden. Die Zuordnung zur Expanded-64-Menge erfolgt über die beiden MSBs `b7` und `b6`; zulässig sind nur die Muster `01` und `10`. Es werden keine Paritätsbits übertragen. Die Nachricht muss sowohl vom Operator als auch vom Controller send- und empfangbar sein. Empfangene AMD-Nachrichten müssen direkt angezeigt und akustisch oder optisch signalisiert werden können; Anzeige und Alarm müssen abschaltbar sein.

**Wesentliche Vorgaben:**
- Expanded 64 enthält A–Z, 0–9, `@`, `?` und 26 weitere Zeichen.
- Orderwire-Nachrichten nutzen `DATA`/`REP` mit zugehörigem `CMD`.
- Basic-38-Adressierung ist Voraussetzung des Calling Cycle.
- Nur MSB-Muster `01` und `10` sind zulässig.
- Es werden keine Paritätsbits gesendet.
- Empfangene AMD-Nachrichten müssen direkt angezeigt und gemeldet werden können.
- Anzeige und Alarm müssen deaktivierbar sein.

##### A.5.7.2.2 — AMD protocol

**Anforderung:** Für ein ASCII-Short-Orderwire-AMD ist das folgende CMD-AMD-Protokoll zu verwenden, sofern kein anderes Protokoll dieses Standards substituiert. Die AMD-Nachricht wird im Standard-Wortformat aufgebaut und in die Message-Sektion des Frames eingefügt. Die Empfangsstation muss AMD-Nachrichten aus beliebigen ALE-Frames empfangen können, einschließlich Calls, Responses und Acknowledgments. Das erste Wort ist ein `CMD AMD`-Wort mit den ersten drei Zeichen der Nachricht. Danach folgen alternierende `DATA`- und `REP`-Wörter für den Rest der Nachricht. Alle `CMD`-, `DATA`- und `REP`-Wörter dürfen nur Zeichen aus dem Expanded-64-Satz enthalten. Jede AMD-Nachricht bleibt intakt und wird in genau einem Frame und in exakt derselben Reihenfolge wie die Nachricht selbst übertragen. Wenn ein oder zwei Zeichen fehlen, um das letzte Tripel zu füllen, muss der Controller automatisch mit Space (`0100000`) auffüllen, ohne Bedienereingriff. Das Ende der AMD-Nachricht wird durch den Beginn des Frame-Abschlusses oder durch das Eintreffen eines weiteren `CMD` markiert. Mehrere AMD-Nachrichten dürfen in einem Frame gesendet werden, müssen dann aber jeweils mit einem eigenen `CMD AMD` und den ersten drei Zeichen beginnen.

**Wesentliche Vorgaben:**
- Erstes Wort: `CMD AMD` mit den ersten drei Zeichen der Nachricht.
- Danach alternierende `DATA`- und `REP`-Wörter.
- Nur Expanded-64-Zeichen in `CMD`/`DATA`/`REP`.
- Eine AMD-Nachricht bleibt innerhalb eines einzigen Frames.
- Fehlende 1–2 Zeichen werden automatisch mit Space aufgefüllt.
- Ende durch Frame-Abschluss oder nächstes `CMD`.
- Mehrere AMD-Nachrichten pro Frame sind erlaubt, jeweils mit eigenem `CMD AMD`.

##### A.5.7.2.3 — Maximum AMD message size

**Anforderung:** Der Empfang eines `CMD AMD`-Worts warnt die Empfangsstation, dass eine AMD-Nachricht ankommt, und veranlasst sie, Operator und Controller zu alarmieren und die Nachricht anzuzeigen, sofern diese Ausgaben nicht deaktiviert wurden. Die Station muss mehrere separate AMD-Nachrichten, die in einer oder mehreren Übertragungen enthalten waren, unterscheiden und separat anzeigen können. Das AMD-Wortformat besteht aus einem `CMD` (`110`) in den Bits P3 bis P1 (W1 bis W3), gefolgt von drei Standard-Zeichenfeldern C1, C2 und C3. In jedem Zeichenfeld müssen die MSBs auf `01` oder `10` gesetzt sein, damit alle drei Zeichen Mitglieder des Expanded-64-Satzes sind. Der Rest der AMD-Nachricht wird identisch aufgebaut, mit alternierender Verwendung von `DATA`- und `REP`-Präambeln. Innerhalb der Message-Sektion dürfen beliebig viele AMD-Wörter bis zur Tm-max-Grenze von 30 Wörtern (90 Zeichen) gesendet werden. Tm max wird auf maximal 59 Wörter erweitert, wenn `CMD`-Wörter innerhalb der Message-Sektion enthalten sind. Die maximale AMD-Nachricht bleibt 30 Wörter, exklusive zusätzlicher `CMD`-Wörter innerhalb der Message-Sektion. Die maximale Anzahl von `CMD`-Wörtern innerhalb der Message-Sektion beträgt 30. Nachrichtentext muss verbatim angezeigt werden. Bei erkennbaren Informationsverlusten oder Fehlern muss die Station eine eindeutige und klar unterscheidbare Fehleranzeige ausgeben. Die Anzeige muss mindestens 20 Zeichen fassen (DO: mindestens 40). Der AMD-Nachrichtenspeicher muss für mindestens 90 Zeichen plus Senderadresse reichen (DO: mindestens 400). Auf operator- oder controllerseitige Anweisung muss die Station alle im AMD-Speicher abgelegten Nachrichten anzeigen und die Senderadresse identifizieren können. Wenn Wörter mit korrektem AMD-Format in einem Abschnitt eintreffen, der von einem anderen Protokoll kontrolliert wird, hat das andere Protokoll Vorrang; die Wörter werden von der AMD-Funktion ignoriert.

**Wesentliche Vorgaben:**
- `CMD AMD` signalisiert eingehende AMD-Nachrichten.
- Mehrere AMD-Nachrichten innerhalb einer oder mehrerer Übertragungen müssen getrennt darstellbar sein.
- AMD-Wortformat: `CMD` (`110`) + drei Zeichenfelder.
- MSBs der Zeichenfelder müssen `01` oder `10` sein.
- Tm max: 30 Wörter / 90 Zeichen; mit zusätzlichen `CMD`-Wörtern in der Message-Sektion bis maximal 59 Wörter.
- Maximal 30 `CMD`-Wörter innerhalb der Message-Sektion.
- Ausgabe verbatim.
- Fehler müssen über eine eindeutige Fehleranzeige markiert werden.
- Display: mindestens 20 Zeichen, DO mindestens 40.
- Speicher: mindestens 90 Zeichen plus Senderadresse, DO mindestens 400.
- Abruf und Senderadressanzeige müssen unterstützt werden.
- Bei Kollision mit anderer Protokollkontrolle hat das andere Protokoll Vorrang.

---

### FEAT-GEN-015 — Data Text Message (DTM)

**Setzt um:** A.5.7.3
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Technischer Entwurf

DTM überträgt ASCII- oder unformatierte Binärdaten an ausgewählte Stationen und gibt sie über Standard-DCE-Ports an DTE-Geräte aus. Der Modus ist schneller als AMD und robuster gegen schwache Signale und kurze Rauschimpulse. DTM-Nachrichten können unilateral oder bilateral sowie broadcast oder acknowledged sein. Die Datenblöcke werden vollständig gepuffert und sollen für die angeschlossenen DTEs transparent erscheinen.

Der Controller muss DTM-Datenverkehr auf die passende DCE-Schnittstelle oder das passende DTE-Gerät lenken können. Empfangene DTM-Nachrichten dürfen optional auf dem Operator-Display erscheinen.

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### A.5.7.3 — DTM mode

**Anforderung:** DTM ermöglicht die Kommunikation von Full-ASCII- oder unformatierten Binärbit-Nachrichten zu und von ausgewählten Stationen über ihre standardisierten DCE-Ports zu DTE-Geräten. DTM ist ein Standard-Speed-Modus wie AMD, jedoch robuster gegenüber schwachen Signalen und kurzen Rauschbursts. Auf MF/HF kann DTM unilateral oder bilateral sowie broadcast oder acknowledged betrieben werden. Die moderate Blockgröße nutzt die inhärente Redundanz und FEC, um schwache HF-Signale zu erkennen und kurze Rauschbursts zu tolerieren. Die Datenblöcke müssen vollständig gepuffert werden und sollen für die verwendeten DTEs transparent erscheinen. Unter Operator- oder Controller-Steuerung muss der DTM-Datenverkehr auf geeignete DCE-Ports bzw. DTE-Geräte geleitet werden können. Empfangene DTM-Nachrichten dürfen optional auf dem Operator-Display dargestellt werden, analog zu AMD.

**Wesentliche Vorgaben:**
- Full ASCII oder unformatierte Binärbits.
- Ausgabe und Eingabe über DCE-Ports.
- Standard-Speed wie AMD, aber robuster.
- Unilateral/bilateral und broadcast/acknowledged möglich.
- Vollständig gepuffert, transparent für DTEs.
- Routing zu Druckern/Terminals bzw. Computern/Kryptogeräten muss steuerbar sein.
- Operator-Display für empfangene DTM-Nachrichten ist optional.

##### CMD DTM modes

| Mode | Max size (Bits) | CRC | Data Capacity, ASCII | Data Capacity, Bits | ALE Word Redundancy | Data Transmission |
|---|---:|---:|---:|---:|---:|---:|
| BASIC | 651 | 16 Bits | 0–93 | 1–651 | 3 Fixed | 392 ms |
| EXTENDED | 7371 | 16 Bits | 3–1053, by 3 | 21–7371, by 21 | 3 Fixed | 392 ms |
| NULL | 0 | 0 | 0 | 0 | 0 | 0 |
| ARQ | 0 | 0 | 0 | 0 | 0 | 0 |

**Wesentliche Vorgaben:**
- BASIC: variable Menge, von zero bis full, exakt gemessen.
- EXTENDED: Größen in integralen Vielfachen des ALE-Basisworts.
- NULL und ARQ dienen Link-Management sowie Error-/Flow-Control.

##### DTM-Verhalten und Framing

**Anforderung:** DTM-Wörter werden im Message-Section-Teil des Standard-ALE-Frames eingefügt. Ein `CMD DTM`-Wort wird im Standard-24-Bit-Format mit CMD-Präambel erzeugt. Die zu übertragenden Daten werden ebenfalls als Wörter mit `DATA`- und `REP`-Präambeln eingefügt, anschließend Golay-FEC-kodiert und interleaved übertragen. Ein CMD-CRC folgt unmittelbar auf die Datenblock-Wörter und trägt den Fehlerkontroll-CRC-FCS. Wenn die Übertragungszeit des DTM-Blocks Tm max überschreitet, hat DTM Vorrang und erweitert das Tm-Limit entsprechend. Die DTM-Übertragung erhält die konsistente redundante Wortphase. Die Erweiterung der Nachricht ist immer ein Vielfaches von einem Trw, da die Standard-ALE-Wortstruktur verwendet wird. Die Übertragungszeit des DTM-Datenblocks (DTM-Wörter × 392 ms) umfasst nicht das Trw des vorhergehenden `CMD DTM` und auch nicht das des nachfolgenden `CMD CRC`.

**Wesentliche Vorgaben:**
- DTM liegt in der Message-Sektion des Standard-Frames.
- `CMD DTM` verwendet die Standard-24-Bit-Struktur mit CMD-Präambel.
- Daten werden mit `DATA`/`REP` eingefügt, dann Golay-FEC und Interleaving.
- `CMD CRC` folgt direkt auf den Datenblock.
- DTM darf Tm max überschreiten; das Protokoll erweitert dann die Grenze.
- Redundante Wortphase bleibt konsistent.
- Die Erweiterung erfolgt immer in Schritten von Trw.
- Blockzeit = DTM-Wörter × 392 ms, ohne Vor- und Nachwort-Trw.

---

### FEAT-GEN-016 — Data Block Message (DBM)

**Setzt um:** A.5.7.4
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Technischer Entwurf

DBM überträgt Full-ASCII- oder unformatierte Binärdaten an ausgewählte ALE-Stationen und gibt sie über Standard-DCE-Ports an DTE-Geräte aus. Der Modus ist schneller als DTM und AMD und zielt auf höhere Robustheit gegen lange Fades und große Rauschbursts. DBM-Nachrichten können unilateral oder bilateral sowie broadcast oder acknowledged sein. Große Datenblöcke nutzen tiefe Interleaving- und FEC-Verfahren.

Die Datenblöcke werden vollständig gepuffert und sollen für die DTEs transparent erscheinen. Unter Operator- oder Controller-Steuerung muss der Datenverkehr auf geeignete DCE-Ports bzw. DTE-Geräte geleitet werden können. Empfangene DBM-Nachrichten dürfen optional auf dem Operator-Display erscheinen.

#### Requirement-Details

> Die folgenden Requirement-Blöcke werden von diesem Feature vollständig umgesetzt. Alle Akzeptanzkriterien, Normwerte und offenen Punkte sind verbindliche Implementierungsvorgaben.

##### A.5.7.4 — DBM mode

**Anforderung:** DBM ermöglicht Full-ASCII- oder unformatierte Binärbit-Nachrichten zu und von ausgewählten ALE-Stationen über standardisierte DCE-Ports an DTE-Geräte. DBM ist ein Hochgeschwindigkeitsmodus gegenüber DTM und AMD, mit verbesserter Robustheit insbesondere gegen lange Fades und Rauschbursts. Auf MF/HF können DBM-Nachrichten unilateral oder bilateral sowie broadcast oder acknowledged sein. Große Datenblöcke erlauben die Nutzung tiefer Interleaving- und FEC-Techniken. Die Datenblöcke müssen vollständig gepuffert werden und sollen für die genutzten DTEs transparent erscheinen. Unter Operator- oder Controller-Steuerung muss der DBM-Datenverkehr auf geeignete DCE-Ports bzw. DTE-Geräte geleitet werden können. Empfangene DBM-Nachrichten dürfen optional auf dem Operator-Display dargestellt werden, analog zu AMD.

**Wesentliche Vorgaben:**
- Full ASCII oder unformatierte Binärbits.
- DCE/DTE-Anbindung.
- Höhere Datenrate als DTM und AMD.
- Robust gegen lange Fades und große Rauschbursts.
- Unilateral/bilateral und broadcast/acknowledged möglich.
- Vollständig gepuffert und transparent für DTEs.
- Routing zu Druckern/Terminals bzw. Computern/Kryptogeräten muss steuerbar sein.
- Operator-Display für empfangene DBM-Nachrichten ist optional.

##### CMD DBM modes

| Mode | Max size (Bits) | CRC | Data Capacity, ASCII | Data Capacity, Bits | ALE Word Redundancy |
|---|---:|---:|---:|---:|---:|
| BASIC | 588 | 16 Bits | 0–81 | 0–572 | 49 Fixed |
| EXTENDED | 262836 | 16 Bits | 81–37377, by 84 | 572–261644, by 588 | 49–21805, by 49 |
| NULL | 0 | 0 | 0 | 0 | 0 |
| ARQ | 0 | 0 | 0 | 0 | 0 |

**Wesentliche Vorgaben:**
- BASIC ist fest in der Größe.
- EXTENDED verwendet integrale Vielfache des BASIC-Blocks.
- NULL und ARQ dienen Link-Management sowie Error-/Flow-Control.

##### DBM-Verhalten und Framing

**Anforderung:** DBM-Wörter werden in die Message-Sektion des Standard-Frames eingefügt. Ein `CMD DBM`-Wort wird im Standardformat erzeugt. Die übertragenen Daten werden Golay-FEC-kodiert, interleaved und unmittelbar nach dem `CMD DBM` gesendet. Wenn die DBM-Übertragungszeit Tm max überschreitet, hat DBM Vorrang und erweitert das Tm-Limit. Die DBM-Übertragung erhält die erforderliche Konsistenz der redundanten Wortphase. Die Nachrichtenerweiterung ist immer ein Vielfaches von 8 Trw, da die Interleaver-Tiefe immer ein Vielfaches von 49 ist. Die Übertragungszeit des DBM-Datenblocks Tdbm ist gleich Interleaver-Tiefe × 64 ms, ohne das Trw des vorangehenden `CMD DBM`.

**Wesentliche Vorgaben:**
- DBM liegt in der Message-Sektion des Standard-Frames.
- `CMD DBM` wird im Standardformat erzeugt.
- Daten werden Golay-FEC-kodiert und interleaved übertragen.
- DBM darf Tm max überschreiten; das Protokoll erweitert dann die Grenze.
- Redundante Wortphase bleibt konsistent.
- Erweiterung immer in Schritten von 8 Trw.
- Tdbm = Interleaver-Tiefe × 64 ms, ohne Vorwort-Trw.

---


---

## CMD-Bereich (A.5.6 ALE Control Functions)

---

### FEAT-CMD-001 — CRC-FCS (16-Bit Rahmenprüfsumme)

**Setzt um:** MIL-STD-188-141B A.5.6.1
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Die CRC-Funktion (Cyclic Redundancy Check) sichert die Datenintegrität von ALE-Message-Abschnitten. Sie verwendet ein 16-Bit FCS (Frame Check Sequence) nach FED-STD-1003 mit dem Generatorpolynom X¹⁶ + X¹² + X⁵ + 1. Die Fehlererkennungswahrscheinlichkeit beträgt 2⁻¹⁶, unabhängig von der Bit-Anzahl.

#### Technischer Entwurf

```cpp
// CMD CRC Word: Preamble CMD (110), erstes Zeichen = 'x'/'y'/'z'/'{' (CRC-Typ)
// CRC-Analyse über alle ALE-Wörter des Message-Abschnitts
// (begrenzt durch Ende des Calling Cycle oder vorheriges CMD CRC-Wort)
struct CrcWord {
    PreambleType preamble = PreambleType::CMD;
    char type_char;  // 'x'=1111000, 'y'=1111001, 'z'=1111010, '{'=1111011
    uint16_t fcs;    // 16-Bit Frame Check Sequence
};

uint16_t compute_crc16(std::span<const ALEWord> words);
```

#### Requirement-Details

##### REQ-CMD-001 — CRC-FCS Generatorpolynom

**Spec-Referenz:** A.5.6.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** CMD CRC verwendet 16-Bit FCS nach FED-STD-1003, Polynom X¹⁶ + X¹² + X⁵ + 1.

**Akzeptanzkriterien:**
- `AC-CMD-001-1` — CRC-Berechnung nutzt Polynom 0x1021 (CRC-CCITT)
- `AC-CMD-001-2` — CMD CRC-Wort hat Preamble CMD und erstes Zeichen 'x', 'y', 'z' oder '{'
- `AC-CMD-001-3` — CRC deckt alle Wörter zwischen vorherigem CMD CRC (oder Calling-Cycle-Ende) und aktuellem CMD CRC ab

---

### FEAT-CMD-002 — Power Control CMD (optional)

**Setzt um:** MIL-STD-188-141B A.5.6.2
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Optionale Funktion zur Leistungsregelung. Ein Sender kann eine Gegenstation auffordern, die HF-Sendeleistung zu erhöhen oder zu senken. Drei Verfahren: Anforderung zur Anpassung, Meldung des aktuellen Pegels, relative/absolute Leistungsangabe in dB/dBW.

#### Technischer Entwurf

```cpp
// KP1-KP3 Kontrollbits für Leistungsanpassung
enum class PowerControlMode {
    REQUEST_ADJUST,    // Anforderung zur Anpassung
    REPORT_CURRENT,    // Meldung aktueller Pegel
    SPECIFY_RELATIVE,  // Relative Angabe (dB)
    SPECIFY_ABSOLUTE,  // Absolute Angabe (dBW)
};

struct PowerControlCmd {
    PowerControlMode mode;
    int8_t power_delta_db;   // für relative/absolute Angabe
};
```

#### Requirement-Details

##### REQ-CMD-002 — Power Control Steuerung

**Spec-Referenz:** A.5.6.2
**Priorität:** COULD · **Status:** offen

**Anforderung:** CMD Power Control mit KP1–KP3 Bits für Leistungsanpassung (optional).

**Akzeptanzkriterien:**
- `AC-CMD-002-1` — Power-Control-CMD enthält KP1–KP3 Kontrollbits
- `AC-CMD-002-2` — Alle drei Modi (REQUEST/REPORT/SPECIFY) sind implementierbar

---

### FEAT-CMD-003 — Channel Related CMD Functions

**Setzt um:** MIL-STD-188-141B A.5.6.3
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Kanalzugehörige Kontrollfunktionen: Kanalbezeichnung (5-stellige BCD-Frequenz), Frequenzbezeichnung (7-Bit Integer), Full-Duplex-Kanalaushandlung, sowie optionale LQA-Polling/Reporting-Funktionen (Referenz MIL-STD-187-721).

#### Technischer Entwurf

```cpp
// A.5.6.3.1: Kanalbezeichnung — 5-stelliger BCD-String (20 Bit) für Frequenz
// A.5.6.3.2: Frequenzbezeichnung — 7-Bit-Binärzahl (0-127), absolut oder relativ
// A.5.6.3.3: Full-Duplex Independent Link (optional)
struct ChannelDesignationCmd {
    uint32_t bcd_frequency;   // 20-Bit BCD, A.5.6.3.1
    uint8_t freq_index;       // 7-Bit, 0-127, A.5.6.3.2
    bool full_duplex;         // A.5.6.3.3
};
```

#### Requirement-Details

##### REQ-CMD-003 — Kanalbezeichnung BCD

**Spec-Referenz:** A.5.6.3.1, A.5.6.3.2
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Kanalbezeichnung als 5-stellige BCD (20 Bit) und Frequenzbezeichnung als 7-Bit-Index.

**Akzeptanzkriterien:**
- `AC-CMD-003-1` — BCD-Frequenzfeld hat 20 Bit (5 Stellen × 4 Bit)
- `AC-CMD-003-2` — Frequenz-Index 7 Bit, Wertebereich 0–127
- `AC-CMD-003-3` — Full-Duplex-Kanalaushandlung per CMD-Wort möglich (A.5.6.3.3)

---

### FEAT-CMD-004 — Time-Related CMDs (Tune & Wait, Schedule, Time Exchange)

**Setzt um:** MIL-STD-188-141B A.5.6.4
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Zeitbezogene CMD-Funktionen: Tune-and-Wait-Kommando (Gegenstation wartet auf Kanal), Scheduling-Kommandos (Zeitmanipulation in 4 Auflösungsstufen), Time-Exchange-Protokoll (Time-Is / Time-Request mit Authentifizierung), sowie Zeitqualitätsangabe.

#### Technischer Entwurf

```cpp
// A.5.6.4.1: Tune and Wait
// A.5.6.4.2: Scheduling – 4 Zeitauflösungsstufen:
//   0–4s  in 1/8s (Tw)-Schritten
//   0–36s in 1s   (3×Trw)-Schritten
//   0–31min in 1min (153×Trw)-Schritten
//   0–29h  in 1h  (9184×Trw)-Schritten
// A.5.6.4.3: Time Exchange (Time-Is / Time-Request)
// A.5.6.4.4: Coarse Time Word (Datum + Zeit + Qualität)
// A.5.6.4.5: Authentication Word (XOR aller 24-Bit-Wörter)
// A.5.6.4.6: Time Quality (0=keine bis 7=unbegrenzt, 6 Fenster: 20ms..60s)

struct SchedulingCmd {
    enum class Resolution { TW_EIGHTH, SECOND, MINUTE, HOUR } resolution;
    uint8_t value;
};

struct TimeExchangeCmd {
    bool is_request;           // Time-Request vs. Time-Is
    uint64_t coarse_time_ms;   // Datum+Zeit
    uint8_t quality;           // 0–7
    uint32_t authenticator;    // XOR der Nachrichtenwörter
};
```

#### Requirement-Details

##### REQ-CMD-004 — Scheduling-Zeitauflösungen

**Spec-Referenz:** A.5.6.4.2
**Priorität:** SHOULD · **Status:** offen

**Vom-Standard-vorgegebene Werte:**

| Auflösung | Einheit | Wertebereich | Schrittgröße |
|---|---|---|---|
| Fein | 1/8 s (Tw) | 0–4 s | 125 ms |
| Mittel | 1 s (3×Trw) | 0–36 s | 1 s |
| Grob | 1 min (153×Trw) | 0–31 min | 60 s |
| Sehr grob | 1 h (9184×Trw) | 0–29 h | 3600 s |

**Akzeptanzkriterien:**
- `AC-CMD-004-1` — Scheduling-CMD unterstützt alle 4 Zeitauflösungsstufen
- `AC-CMD-004-2` — Time-Is-CMD enthält Grobzeit (Datum+Zeit+Qualität) im Coarse-Time-Word
- `AC-CMD-004-3` — Authentication-Word = XOR aller 24-Bit-Wörter der Nachricht
- `AC-CMD-004-4` — Zeitqualität-Feld: 0 (keine Uhrzeit) bis 7 (unbegrenzt), 6 definierte Fenster (20ms, 100ms, 500ms, 2s, 10s, 60s)
- `AC-CMD-004-5` — Tune-and-Wait unterdrückt normale/voreingestellte Antworten der Gegenstation

---

### FEAT-CMD-005 — Modem Negotiation & Handoff CMD

**Setzt um:** MIL-STD-188-141B A.5.6.5
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Modem-Aushandlung im Orderwire-Abschnitt: Die initierende Station sendet einen Modem-Select-CMD mit dem gewünschten Modem-Code. Die antwortende Station kann akzeptieren oder eine Alternative vorschlagen. Modus-Kontrolle via Zeichen 'm' (1101101), Modem-Select via Zeichen 'n' (1101110).

#### Technischer Entwurf

```cpp
// A.5.6.5.1: Modem Selection CMD
// Modus-Kontrolle:  CMD-Zeichen1 = 'm' (1101101)
// Modem-Select:     CMD-Zeichen2 = 'n' (1101110)
struct ModemNegotiationCmd {
    uint8_t modem_code;    // Ziel-Modem-Typ
    bool accept;           // true = Akzeptanz, false = Gegenvorschlag
};
```

#### Requirement-Details

##### REQ-CMD-005 — Modem-Aushandlungsprotokoll

**Spec-Referenz:** A.5.6.5.1, A.5.6.5.1.2
**Priorität:** SHOULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-CMD-005-1` — Modem-CMD verwendet Zeichen 'm' (0x6D) für Modus-Kontrolle
- `AC-CMD-005-2` — Modem-CMD verwendet Zeichen 'n' (0x6E) für Modem-Select
- `AC-CMD-005-3` — Protokoll ermöglicht Akzeptanz oder Gegenvorschlag durch Gegenstation

---

### FEAT-CMD-006 — Do-Not-Respond CMD

**Setzt um:** MIL-STD-188-141B A.5.6.7
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Wenn eine Station dieses CMD empfängt, antwortet sie nicht, es sei denn, ein anderes CMD erfordert explizit eine Antwort (z.B. LQA-Anfrage oder DTM/DBM mit ARQ). Kein Three-Way-Handshake erforderlich.

#### Technischer Entwurf

```cpp
// Empfang von CMD DO-NOT-RESPOND → respond_flag = false
// Ausnahmen: LQA-Request, DTM/DBM mit ARQ
bool is_do_not_respond_cmd(const ALEWord& word);
```

#### Requirement-Details

##### REQ-CMD-006 — Do-Not-Respond Semantik

**Spec-Referenz:** A.5.6.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** Nach Empfang von CMD Do-Not-Respond darf die Station keine unaufgeforderte Antwort senden.

**Akzeptanzkriterien:**
- `AC-CMD-006-1` — Nach Empfang von Do-Not-Respond: respond_allowed = false
- `AC-CMD-006-2` — Ausnahme: LQA-Request-CMD darf trotzdem beantwortet werden
- `AC-CMD-006-3` — Ausnahme: DTM/DBM mit ARQ-Flag darf trotzdem beantwortet werden
- `AC-CMD-006-4` — Kein Three-Way-Handshake bei Do-Not-Respond

---

### FEAT-CMD-007 — User Unique Functions (UUF)

**Setzt um:** MIL-STD-188-141B A.5.6.9
**Modul:** `include/Protocol/ale_orderwire_protocols.h`, `src/Protocol/ale_orderwire_protocols.cpp`
**Status:** geplant

#### Beschreibung

Spezielle Funktionen für besondere Anwendungsfälle, koordiniert zwischen spezifischen Nutzern/Herstellern. 16.384 eindeutige UUF-Codes (14-Bit oder zwei Zeichen als Unique Index UI). UUF wird nur bei explizit adressierten und fähigen Stationen verwendet. CMD STAY gibt eine Wartezeit an, bevor UUF-Instruktionen ausgeführt werden.

#### Technischer Entwurf

```cpp
// 16384 UUF-Codes = 14-Bit UI (Unique Index)
// CMD STAY vor UUF: gibt Wartezeit an
// UUF nur im Message-Abschnitt, außerhalb Frame wenn möglich
struct UufCmd {
    uint16_t unique_index;  // 14-Bit UI, 0–16383
    std::vector<uint8_t> payload;
};
```

#### Requirement-Details

##### REQ-CMD-007 — UUF Unique Index

**Spec-Referenz:** A.5.6.9
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-CMD-007-1` — UUF Unique Index ist 14 Bit (0–16383), entspricht 16.384 Codes
- `AC-CMD-007-2` — CMD STAY wird vor UUF gesendet um Wartezeit anzugeben
- `AC-CMD-007-3` — UUF nur im Message-Abschnitt des Frames

---

## AQC-Bereich (A.5.8 Alternative Quick Call ALE)

---

### FEAT-AQC-001 — AQC Word-Struktur & Packed-Address (Base-40)

**Setzt um:** MIL-STD-188-141B A.5.8.1.1, A.5.8.1.1.1, A.5.8.1.1.2
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested — optionales Feature

#### Beschreibung

AQC-ALE-Wörter haben 3-Bit-Preamble, ein Address-Differentiation-Flag (Bit 4), ein 16-Bit Packed-Address-Feld und ein 4-Bit Data-Exchange-Feld. Die Packed-Address verwendet Base-40-Arithmetik: 3 Zeichen × 38-Zeichen-ASCII-Subset → 16 Bit. Legalbereich: "000" (1.641) bis "ZZZ" (62.358). Das Address-Differentiation-Flag (= MSB der 16-Bit-Adresse) stellt sicher, dass kein AQC-ALE-Wort je mit einem legalen Baseline-2G-ALE-Wort verwechselt werden kann.

#### Technischer Entwurf

```cpp
// AQC Word: 3-Bit Preamble | 1-Bit ADF | 16-Bit Packed Address | 4-Bit Data Exchange
// Packed Address: char1 × 1600 + char2 × 40 + char3 × 1  (Base-40)
// Ordinalwerte: 0='0'..9='9', 10='A'..35='Z', 36='@', 37='?'

struct AqcWord {
    uint8_t  preamble;          // 3 Bit
    bool     address_diff_flag; // Bit 4, Kopie MSB(packed_address)
    uint16_t packed_address;    // 16 Bit, Base-40
    uint8_t  data_exchange;     // 4 Bit
};

uint16_t aqc_pack_address(std::string_view addr3);   // 3-Zeichen → 16 Bit
std::string aqc_unpack_address(uint16_t packed);     // 16 Bit → 3 Zeichen
```

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Beschreibung |
|---|---|---|
| Basis | 40 | Zeichenanzahl für Multiplikation |
| Multiplier Char1 | 1600 (= 40²) | Höchstwertiges Zeichen |
| Multiplier Char2 | 40 | Mittleres Zeichen |
| Multiplier Char3 | 1 | Niedrigstwertiges Zeichen |
| Minimalwert | 1.641 | Adresse "000" |
| Maximalwert | 62.358 | Adresse "ZZZ" |

#### Requirement-Details

##### REQ-AQC-001 — Base-40 Packed Address

**Spec-Referenz:** A.5.8.1.1.1
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-001-1` — aqc_pack_address("ABC") liefert korrekten Base-40-Wert
- `AC-AQC-001-2` — Address-Differentiation-Flag = MSB der 16-Bit Packed Address
- `AC-AQC-001-3` — Kein legales AQC-ALE-Wort fällt mit einem legalen Baseline-2G-ALE-Wort zusammen
- `AC-AQC-001-4` — Round-Trip: aqc_unpack_address(aqc_pack_address(addr)) == addr

---

### FEAT-AQC-002 — AQC Preamble-Typen

**Setzt um:** MIL-STD-188-141B A.5.8.1.2
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested

#### Beschreibung

AQC-ALE definiert 8 eigene Preamble-Typen, die sich von den Baseline-2G-Preambles unterscheiden:

| Code | Typ | Bedeutung |
|---|---|---|
| 001 | INLINK | In-Link-Zustand (entspricht kein direktes 2G-Äquivalent) |
| 010 | TO | Calling-Wort (entspricht 2G TO) |
| 110 | CMD | Kommando (entspricht 2G CMD) |
| 100 | PART2 | Zweites Adresswort (AQC-spezifisch) |
| 101 | TIS | This-Is/Sounding-Conclusion (entspricht 2G TIS) |
| 011 | TWAS | That-Was/Termination (entspricht 2G TWAS) |
| 000 | DATA | Datenwort (entspricht 2G DATA) |
| 111 | REP | Repeat-Wort (entspricht 2G REP) |

#### Technischer Entwurf

```cpp
enum class AqcPreambleType : uint8_t {
    DATA    = 0b000,
    INLINK  = 0b001,
    TO      = 0b010,
    TWAS    = 0b011,
    PART2   = 0b100,
    TIS     = 0b101,
    CMD     = 0b110,
    REP     = 0b111,
};
```

#### Requirement-Details

##### REQ-AQC-002 — AQC Preamble-Kodierung

**Spec-Referenz:** A.5.8.1.2
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-002-1` — AqcPreambleType enthält alle 8 Typen mit korrekten 3-Bit-Codes
- `AC-AQC-002-2` — INLINK (001) nicht in Baseline-2G-Preamble-Enum vorhanden
- `AC-AQC-002-3` — PART2 (100) nicht in Baseline-2G-Preamble-Enum vorhanden

---

### FEAT-AQC-003 — AQC Adresscharakteristiken & Call-Typen

**Setzt um:** MIL-STD-188-141B A.5.8.1.3, A.5.8.1.4
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested

#### Beschreibung

AQC-ALE-Adressen haben 1–6 Zeichen (vs. 1–15 bei Baseline-2G). Das Over-the-Air-Format verwendet immer genau 2 AQC-Wörter mit '@' als Stuffzeichen für Adressen unter 6 Zeichen. Unterstützte Call-Typen: Unit (1–6 Zeichen), StarNet (1–6 Zeichen), AllCall (6 Zeichen, zweite Drei = erste Drei). Group-Calls nicht unterstützt.

#### Technischer Entwurf

```cpp
// AQC-Adresse: 1–6 Zeichen, ASCII-38-Zeichensatz
// Over-the-Air: immer genau 2 AQC-Wörter (Wort 1: Zeichen 1–3, Wort 2: Zeichen 4–6)
// Padding: '@' für unvollständige Zeichen
// AllCall: Zeichen 4–6 = Zeichen 1–3

struct AqcAddress {
    std::string value;  // 1–6 Zeichen, ASCII-38
    bool is_allcall() const;
    std::array<AqcWord, 2> to_words(AqcPreambleType pt) const;
};
```

#### Requirement-Details

##### REQ-AQC-003 — AQC Adresslänge und Formate

**Spec-Referenz:** A.5.8.1.3, A.5.8.1.4
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-003-1` — AQC-Adresse: 1–6 Zeichen (nicht 1–15 wie Baseline-2G)
- `AC-AQC-003-2` — Over-the-Air: genau 2 AQC-Wörter, '@'-Padding wenn < 6 Zeichen
- `AC-AQC-003-3` — AllCall: Zeichen 4–6 identisch mit Zeichen 1–3
- `AC-AQC-003-4` — Group-Calls nicht unterstützt in AQC-ALE

---

### FEAT-AQC-004 — AQC Calling Cycle & Unit Call Protokoll

**Setzt um:** MIL-STD-188-141B A.5.8.2.1, A.5.8.2.2
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested

#### Beschreibung

Der AQC-Calling-Cycle ist kürzer als der Baseline-2G-Zyklus. Ruf-Dauer: Anzahl-Kanäle × 0,196 s. Der Leading-Call-Abschnitt sendet die Zieladresse nur einmal (nicht zweifach wie Baseline-2G). Der Acknowledgement-Frame sendet nur die Conclusion. Optional PSK-Tonsequenz während Handshake-Legs.

#### Technischer Entwurf

```cpp
// AQC Call Duration: N_channels × 0.196s  (vs. Baseline: N × 2 × Trw)
// Leading Call: Zieladresse 1× (nicht 2× wie Baseline-2G)
// ACK Frame: nur Conclusion (kein TO+TIS wie Baseline-2G)

// Max Call Duration per channel count (A.5.8.2.1 Table):
// 1 ch:  4.8s  |  5 ch: 5.8s  | 10 ch: 6.8s  | 20 ch: 8.8s
constexpr float AQC_CALL_DURATION_PER_CHANNEL_SEC = 0.196f;
```

**Vom-Standard-vorgegebene Werte:**

| Kanäle | Max. Ruf-Dauer |
|---|---|
| 1 | 4,8 s |
| 5 | 5,8 s |
| 10 | 6,8 s |
| 20 | 8,8 s |

#### Requirement-Details

##### REQ-AQC-004 — AQC Calling-Dauer

**Spec-Referenz:** A.5.8.2.1
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-004-1` — Ruf-Dauer = N_channels × 0,196 s
- `AC-AQC-004-2` — Leading-Call sendet Zieladresse genau 1× (nicht 2×)
- `AC-AQC-004-3` — ACK-Frame enthält nur Conclusion (kein TO+TIS-Sequenz)

---

### FEAT-AQC-005 — AQC Star Net Call

**Setzt um:** MIL-STD-188-141B A.5.8.2.3
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested

#### Beschreibung

AQC-Star-Net-Call verwendet eine Netzadresse als Ruf-Probe. Slotted Response: 2-Wort-Adresse für TO und TIS. Slot 0 (17 Tw) für Nicht-Net-Mitglieder; Slots 1+ (11 Tw) für Net-Mitglieder. Kein LQA im ACK außer via Data-Exchange-Bits. Optionale PSK-Tonsequenz möglich.

#### Technischer Entwurf

```cpp
// AQC Star Net Slots:
// Slot 0 (non-member): 17 Tw
// Slots 1+ (member):   11 Tw  (kürzer als Baseline-2G: 14 Tw)
constexpr uint8_t AQC_SLOT_NON_MEMBER_TW = 17;
constexpr uint8_t AQC_SLOT_MEMBER_TW     = 11;
```

#### Requirement-Details

##### REQ-AQC-005 — AQC Star Net Slotbreiten

**Spec-Referenz:** A.5.8.2.3
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-005-1` — Slot 0 (Nicht-Mitglied) = 17 Tw
- `AC-AQC-005-2` — Slots 1+ (Net-Mitglied) = 11 Tw
- `AC-AQC-005-3` — Slotbreite bleibt unverändert mit optionaler PSK-Tonsequenz

---

### FEAT-AQC-006 — AQC Orderwire Functions

**Setzt um:** MIL-STD-188-141B A.5.8.3
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested, optional

#### Beschreibung

AQC-spezifische Orderwire-Funktionen: Operator-ACK/NAK-Transaktion (15 s Operator-Antwortfenster) und AQC Control Messages im Message-Abschnitt. MsgID-Feld (5 Bit) in CMD-Wörtern. Werte > 7 nicht erlaubt (Überschneidung mit ALE-Orderwire). Pflicht-Messages: AMD-Dictionary, Channel-Definition, Slot-Assignment. Optional: List/Define/Set Database Content.

#### Technischer Entwurf

```cpp
// AQC Control Message IDs (5-Bit MsgID, 0–7 erlaubt):
enum class AqcMsgId : uint8_t {
    AMD_DICTIONARY       = 0,
    CHANNEL_DEFINITION   = 1,
    SLOT_ASSIGNMENT      = 2,
    LIST_DB_CONTENT      = 3,  // optional
    LIST_DB_ACTIV_TIME   = 4,  // optional
    SET_DB_ACTIV_TIME    = 5,  // optional
    DEFINE_DB_CONTENT    = 6,  // optional
    DB_CONTENT_LISTING   = 7,  // optional
};
// MsgID > 7: verboten (Konflikt mit ALE-Orderwire)
```

#### Requirement-Details

##### REQ-AQC-006 — AQC MsgID-Bereich

**Spec-Referenz:** A.5.8.3.2
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-006-1` — MsgID-Feld ist 5 Bit breit, Werte 0–7 erlaubt
- `AC-AQC-006-2` — MsgID > 7 wird als ungültig abgelehnt
- `AC-AQC-006-3` — AMD_DICTIONARY (ID=0), CHANNEL_DEFINITION (ID=1), SLOT_ASSIGNMENT (ID=2) sind Pflicht-Messages
- `AC-AQC-006-4` — Operator-ACK/NAK-Fenster = 15 s

---

### FEAT-AQC-007 — AQC Linking Protection (LP)

**Setzt um:** MIL-STD-188-141B A.5.8.4
**Modul:** `include/Protocol/aqc_protocol.h`, `src/Protocol/aqc_protocol.cpp`
**Status:** geplant
**Hinweis:** (NT) Not Tested

#### Beschreibung

Im LP-Modus (Linking Protection) wird jedes 24-Bit-AQC-Wort nach Appendix B verschlüsselt. Die AQC-Wortnummern für LP unterscheiden sich von Baseline-2G: Scanning-TO = Word 0, PART2 = Word 1, TIS/TWAS-Conclusion = Word 2, folgendes PART2 = Word 3. Response-Frame und ACK-Frame verwenden ebenfalls definierte Wortnummern.

#### Technischer Entwurf

```cpp
// AQC LP Wortnummern (A.5.8.4):
// Scanning Call:   TO=0, PART2=1
// Conclusion:      TIS/TWAS=2, PART2=3
// Response Frame:  Word 0, 1, 2, 3
// ACK (2 Wörter):  Word 0, 1
// TOD: nach Ende des Scanning-Call-TO-Worts
```

#### Requirement-Details

##### REQ-AQC-007 — AQC LP Wortnummern-Schema

**Spec-Referenz:** A.5.8.4
**Priorität:** COULD · **Status:** offen

**Akzeptanzkriterien:**
- `AC-AQC-007-1` — AQC LP nutzt Appendix-B-Verschlüsselung auf allen 24-Bit-Wörtern
- `AC-AQC-007-2` — Scanning-Call: TO=Word 0, PART2=Word 1
- `AC-AQC-007-3` — Conclusion: TIS/TWAS=Word 2, PART2=Word 3
- `AC-AQC-007-4` — 2-Wort-ACK: Word 0 und Word 1

---

## 6. Traceability-Matrix

| Feature | Setzt um (REQ) | Modul / Code | Status |
|---|---|---|---|
| FEAT-GEN-001 | REQ-GEN-001–005 | Protocol/ale_state_machine.h/cpp | geplant |
| FEAT-GEN-002 | REQ-GEN-006–008 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-GEN-003 | REQ-GEN-009–012 | tests/test_gen_performance.cpp | geplant |
| FEAT-GEN-004 | REQ-GEN-013 | Stores/ale_data_store.h/cpp | geplant |
| FEAT-GEN-005 | REQ-GEN-014 | Stores/ale_data_store.h/cpp | geplant |
| FEAT-GEN-006 | REQ-GEN-015–018 | Stores/ale_data_store.h/cpp | geplant |
| FEAT-GEN-007 | REQ-GEN-019 | Stores/ale_data_store.h/cpp | geplant |
| FEAT-GEN-008 | REQ-GEN-020 | Stores/ale_data_store.h/cpp | geplant |
| FEAT-GEN-009 | REQ-GEN-021 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-GEN-010 | REQ-GEN-022–025 | Protocol/aqc_protocol.h/cpp | geplant |
| FEAT-GEN-011 | A.5.6.6.1 | include/Protocol/ale_version_caps.h, src/Protocol/ale_version_caps.cpp | geplant |
| FEAT-GEN-012 | A.5.6.6.2 | include/Protocol/ale_version_caps.h, src/Protocol/ale_version_caps.cpp | geplant |
| FEAT-GEN-013 | A.5.7.1 | include/Protocol/ale_orderwire_protocols.h, src/Protocol/ale_orderwire_protocols.cpp | geplant |
| FEAT-GEN-014 | A.5.7.2 | include/Protocol/ale_orderwire_protocols.h, src/Protocol/ale_orderwire_protocols.cpp | geplant |
| FEAT-GEN-015 | A.5.7.3 | include/Protocol/ale_orderwire_protocols.h, src/Protocol/ale_orderwire_protocols.cpp | geplant |
| FEAT-GEN-016 | A.5.7.4 | include/Protocol/ale_orderwire_protocols.h, src/Protocol/ale_orderwire_protocols.cpp | geplant |
| FEAT-WAVEFORM-001 | REQ-WAVEFORM-001–003 | include/FSK/ale_waveform.h | implementiert |
| FEAT-WAVEFORM-002 | REQ-WAVEFORM-004–005 | FSK/tone_generator.cpp | implementiert |
| FEAT-WAVEFORM-003 | REQ-WAVEFORM-006–010 | FSK/ale_waveform.h, Protocol/ale_state_machine.h | implementiert |
| FEAT-WAVEFORM-004 | REQ-WAVEFORM-011–013 | tests/test_tone_accuracy.cpp | geplant |
| FEAT-WORD-001 | REQ-WORD-001–002 | Word/ale_word.h/cpp | implementiert |
| FEAT-WORD-002 | REQ-WORD-003–007 | Word/ale_word.h/cpp | implementiert |
| FEAT-WORD-003 | REQ-WORD-008–010 | Word/ale_word.h/cpp | in Arbeit |
| FEAT-FEC-001 | REQ-FEC-004–009 | include/FEC/golay.h + src/FEC/golay.cpp | implementiert |
| FEAT-FEC-002 | REQ-FEC-008, REQ-FEC-010–011 | src/FEC/golay.cpp *(privat)* | implementiert |
| FEAT-FEC-003 | REQ-FEC-012–013 | FEC/ale_fec_codec.cpp | implementiert |
| FEAT-FEC-004 | REQ-FEC-014–018 | Modem/ale2g_modem.cpp (TX) + FSK/symbol_decoder (RX) | implementiert |
| FEAT-FEC-005 | REQ-FEC-019 | Word/ale_word.h (ALEWord::unanimous_votes) | implementiert |
| FEAT-FRAME-001 | REQ-FRAME-001 | Protocol/ale_state_machine.cpp | implementiert |
| FEAT-FRAME-002 | REQ-FRAME-002, REQ-FRAME-003 (Individual), REQ-FRAME-005–006 | Protocol/ale_state_machine.cpp | implementiert |
| FEAT-FRAME-007 | REQ-FRAME-003 (Net Call) | Protocol/ale_state_machine.cpp | offen |
| FEAT-FRAME-003 | REQ-FRAME-004 | Protocol/ale_state_machine.cpp | implementiert |
| FEAT-FRAME-004 | REQ-FRAME-007–009 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-FRAME-005 | REQ-FRAME-010–011 | Protocol/ale_state_machine.cpp | implementiert |
| FEAT-FRAME-006 | REQ-FRAME-012–013 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-SYNC-001 | REQ-SYNC-001–004 | Protocol/ale_state_machine.cpp | implementiert |
| FEAT-SYNC-002 | REQ-SYNC-005 | FSK/ale_waveform.cpp | geplant |
| FEAT-SYNC-003 | REQ-SYNC-006–007 | FSK/ale_waveform.cpp, FEC/ale_fec_codec.cpp | geplant |
| FEAT-SOUND-001 | REQ-SOUND-002–005 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-SOUND-002 | REQ-SOUND-006–010 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-SOUND-003 | REQ-SOUND-011–012 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-CHAN-001 | REQ-CHAN-001–010 | Protocol/ale_channel_selector.h/cpp | geplant |
| FEAT-CHAN-002 | REQ-CHAN-011–015 | LQA/lqa_metrics.h/cpp | geplant |
| FEAT-CHAN-003 | REQ-CHAN-016–020 | Protocol/ale_channel_selector.cpp, LQA/lqa_metrics.h/cpp | geplant |
| FEAT-CHAN-004 | REQ-CHAN-021–022 | Protocol/ale_channel_selector.cpp | geplant |
| FEAT-CHAN-005 | REQ-CHAN-023–030 | Protocol/ale_channel_selector.h/cpp, LQA/lqa_database.h/cpp | geplant |
| FEAT-CHAN-006 | REQ-CHAN-031–034 | Protocol/ale_channel_selector.h/cpp | geplant |
| FEAT-LINK-001 | REQ-LINK-001/002/007–009/016/017 | Protocol/ale_state_machine.cpp | implementiert* |
| FEAT-LINK-002 | REQ-LINK-004–006/018/019 | FSK/ale_waveform.cpp, Protocol/ale_state_machine.cpp | geplant |
| FEAT-LINK-003 | REQ-LINK-020–023 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-LINK-004 | REQ-LINK-010–015/024 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-LINK-005 | REQ-LINK-003/025–034 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-LINK-006 | REQ-LINK-035–043 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-LINK-007 | REQ-LINK-044–046 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-ADDR-001 | REQ-ADDR-001/002/016 | Word/ale_word.cpp (Zeichensatz), Stores/address_book.cpp (Validierung) | implementiert |
| FEAT-ADDR-002 | REQ-ADDR-003–007 | Word/ale_word.cpp, Protocol/ale_state_machine.cpp | implementiert |
| FEAT-ADDR-003 | REQ-ADDR-008–011 | Protocol/ale_state_machine.cpp | geplant |
| FEAT-ADDR-004 | REQ-ADDR-012 | Stores/address_book.cpp | implementiert |
| FEAT-ADDR-005 | REQ-ADDR-013–015 | Stores/address_book.cpp, Protocol/ale_state_machine.cpp | geplant |
| FEAT-CMD-001 | A.5.6.1 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-002 | A.5.6.2 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-003 | A.5.6.3 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-004 | A.5.6.4 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-005 | A.5.6.5 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-006 | A.5.6.7 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-CMD-007 | A.5.6.9 | `Protocol/ale_orderwire_protocols.h/cpp` | geplant |
| FEAT-AQC-001 | A.5.8.1.1–A.5.8.1.1.2 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-002 | A.5.8.1.2 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-003 | A.5.8.1.3–A.5.8.1.4 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-004 | A.5.8.2.1–A.5.8.2.2 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-005 | A.5.8.2.3 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-006 | A.5.8.3 | `Protocol/aqc_protocol.h/cpp` | geplant |
| FEAT-AQC-007 | A.5.8.4 | `Protocol/aqc_protocol.h/cpp` | geplant |

_* implementiert = vorhandene Implementierung; muss gegen neue REQ-LINK-007/008/009 verifiziert werden_

### 6.1 Neue Dateien (durch A.4-Erweiterung)

| Datei | Zweck | Feature |
|---|---|---|
| `include/Stores/ale_data_store.h` | Store-Interfaces + Datenstrukturen | FEAT-GEN-004–008 |
| `src/Stores/ale_data_store.cpp` | Store-Implementierungen | FEAT-GEN-004–008 |
| `include/Protocol/ale_channel_selector.h` | ChannelSelector Interface | FEAT-CHAN-001–006 |
| `src/Protocol/ale_channel_selector.cpp` | ChannelSelector Implementierung | FEAT-CHAN-001–006 |
| `include/Protocol/ale_version_caps.h` | Version- und Capabilities-Interfaces | FEAT-GEN-011–012 |
| `src/Protocol/ale_version_caps.cpp` | Version-/Capabilities-Implementierung | FEAT-GEN-011–012 |
| `include/Protocol/ale_orderwire_protocols.h` | AMD/DTM/DBM-Interfaces | FEAT-GEN-013–016 |
| `src/Protocol/ale_orderwire_protocols.cpp` | AMD/DTM/DBM-Implementierung | FEAT-GEN-013–016 |
| `include/Protocol/aqc_protocol.h` | AQC-ALE Protokoll Interface | FEAT-GEN-010 |
| `src/Protocol/aqc_protocol.cpp` | AQC-ALE Implementierung | FEAT-GEN-010 |
| `src/Protocol/aqc_parser.cpp` | AQC-Parser intern | FEAT-GEN-010 |
| `include/Stores/ale_persistence.h` | IPersistenceBackend Interface | FEAT-GEN-004–008 |

### 6.2 Abdeckungs-Check

| Prüfung | Ergebnis |
|---|---|
| MUST-Requirements ohne Feature | keine |
| WON'T-Requirements | keine |
| Features ohne Requirement | keine |
| OPEN Design-Entscheidungen | FEAT-SYNC-002 entschieden (Sliding-Window), FEAT-CHAN-005 Score-Formel entschieden (bilateral sum), FEAT-SOUND-002 Timing aus A.5.3.3 (Tsrs = Tss + Trs) |

---

## Anhang A — Glossar

| Begriff | Bedeutung |
|---|---|
| Feature | Technische Umsetzung einer oder mehrerer logisch zusammengehöriger Requirements |
| DD | Design Decision — dokumentiert das Warum einer technischen Wahl |
| NCO | Numerically Controlled Oscillator — Phasenakkumulator-basierter Tongenerator |
| Golay (24,12,3) | Linearer Blockcode, korrigiert bis zu 3 Bit-Fehler per 24-Bit-Codewort |
| Trw | Redundant Word period = 392 ms = 3 × Tw |
| Tw | Single Word period = 130,66... ms |
| Tsc / Tlc / Tcc | Scanning / Leading / gesamte Calling-Cycle-Zeit |
| Twr | Wait-for-Response = ~915 ms (konservativ) |
| Twrt | Wait-for-Response + Tune Time = ~1960 ms |
| Twa | Activity Timeout = 30 s (abschaltbar) |
| Twce | Wait-for-Calling-Cycle-End = 2 × eigene Ts |
| Tlww | Last-Word-Wait = Trw = 392 ms |
| Tswt(SN) | Slot Wait Time für Slotted Responses |
| LQA | Link Quality Analysis — BER, SINAD, MP |
| BER | Bit Error Ratio — aus Majority-Vote non-unanimous counts |
| SINAD | Signal+Noise+Distortion to Noise+Distortion ratio |
| AQC | Alternate Quick Call — optionale Schnell-Verbindung |
| IPersistenceBackend | Abstraktes Interface für nichtflüchtige Speicherung |
| Gold-Plating | Feature ohne Requirement-Bezug — zu vermeiden |


---

## 7. Requirements ohne Feature-Zuordnung

> Diese Requirements sind vollständig aus `REQUIREMENTS.md` übernommen. Sie sind entweder noch nicht einem Feature zugeordnet, oder ihr Feature-Detailabschnitt fehlt noch. Ein Coding Agent MUSS diese Requirements ebenfalls implementieren.

### ADDR-Bereich

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

---

### CHAN-Bereich

#### REQ-CHAN-002 — Link Quality Analysis (LQA): Grundfunktion

**Spec-Referenz:** A.5.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das ALE-System muss eine Link Quality Analysis (LQA) durchführen, die Qualitätsmessungen aus empfangenen ALE-Signalen ableitet und im Link Quality Memory speichert. Die LQA-Daten bilden die Grundlage für die Kanalauswahl.

**Akzeptanzkriterien:**
- `AC-CHAN-002-1` — Das System leitet Qualitätsmessungen aus empfangenen ALE-Signalen ab.
- `AC-CHAN-002-2` — Die Messwerte werden im Link Quality Memory gespeichert.
- `AC-CHAN-002-3` — Die gespeicherten LQA-Daten bilden die Grundlage für die Kanalauswahl.

---

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

#### REQ-CHAN-013 — SINAD-Messung als (S+N+D)/(N+D)-Verhältnis

**Spec-Referenz:** A.5.4.1.2 / Absatz 1, 2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die SINAD-Messung ist ein (S+N+D)/(N+D)-Verhältnis, gemittelt über die Dauer jedes empfangenen ALE-Signals. SINAD-Werte sind auf allen ALE-Signalen zu messen.

**Akzeptanzkriterien:**
- `AC-CHAN-013-1` — SINAD wird als (S+N+D)/(N+D)-Verhältnis gemessen.
- `AC-CHAN-013-2` — Die Messung erfolgt über die volle Dauer des empfangenen ALE-Signals.
- `AC-CHAN-013-3` — SINAD wird auf allen empfangenen ALE-Signalen gemessen.

---

#### REQ-CHAN-014 — MP-Messung ist optional

**Spec-Referenz:** A.5.4.1.3
**Priorität:** COULD · **Status:** offen

**Anforderung:** Die Messung von MP (Modulation Performance) mittels empfangener ALE-Signale ist optional.

**Akzeptanzkriterien:**
- `AC-CHAN-014-1` — Eine Implementierung kann MP-Messung unterstützen oder unterlassen.

**Prioritäts-Begründung (COULD):** Der Standard definiert MP als ausdrücklich optional.

---

#### REQ-CHAN-015 — SINAD-Anzeige in dB

**Spec-Referenz:** A.5.4.1.4
**Priorität:** SHOULD · **Status:** offen

**Anforderung:** Die Anzeige von SINAD-Werten ist in dB durchzuführen.

**Akzeptanzkriterien:**
- `AC-CHAN-015-1` — SINAD-Werte werden dem Operator in dB angezeigt.

**Prioritäts-Begründung (SHOULD):** Der Standard verwendet "shall" für die Anzeige, jedoch ist dies als optional (optionaler Abschnitt) markiert.

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

#### REQ-CHAN-018 — BER-Feld im LQA CMD enthält 5 Bits

**Spec-Referenz:** A.5.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Messung und Meldung von BER ist verpflichtend. Das BER-Feld im LQA CMD enthält fünf Bits. Tabelle A-XIII ist für die zugeordneten Werte heranzuziehen.

**Akzeptanzkriterien:**
- `AC-CHAN-018-1` — BER-Messung und -Meldung ist verpflichtend.
- `AC-CHAN-018-2` — Das BER-Feld besteht aus 5 Bits.
- `AC-CHAN-018-3` — Die BER-Werte entsprechen Tabelle A-XIII.

---

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

#### REQ-CHAN-026 — Kanalselektion für One-Way-Broadcast: TO-Scores gewichten

**Spec-Referenz:** A.5.4.5.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn nur eine Ein-Wege-Übertragung an eine Station erforderlich ist, TO-Scores (von der Ziel-Station gemeldet) sind stärker zu gewichten als FROM-Scores (von der Ziel-Station gemessen).

**Akzeptanzkriterien:**
- `AC-CHAN-026-1` — Bei One-Way-Broadcast werden TO-Scores stärker gewichtet als FROM-Scores.

---

#### REQ-CHAN-027 — Kanalselektion für Listening: FROM-Scores gewichten

**Spec-Referenz:** A.5.4.5.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Bei der Auswahl eines Kanals zum Abhören einer anderen Station sind die auf Übertragungen von dieser Station gemessenen Scores (FROM) stärker zu gewichten als die von der Ziel-Station gemeldeten Scores.

**Akzeptanzkriterien:**
- `AC-CHAN-027-1` — Beim Abhören werden FROM-Scores stärker gewichtet als TO-Scores.

---

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

#### REQ-CHAN-033 — Zu detektierende Modulationen bei Listen Before Transmit

**Spec-Referenz:** A.5.4.7.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Die Listen-Before-Transmit-Funktion muss Verkehr auf einem Kanal gemäß A.4.2.2 erkennen.

**Akzeptanzkriterien:**
- `AC-CHAN-033-1` — Verkehrserkennung erfolgt gemäß A.4.2.2.

---

### LINK-Bereich

#### REQ-LINK-011 — Channel Selection: Scanning Call
**Spec-Referenz:** A.5.5.2.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Eine scannende rufende Station muss ALE-Aufrufe auf ihren gescannten Kanälen in der Reihenfolge senden, die durch ihren Kanal-Auswahl-Algorithmus bestimmt wird. Sie muss auf dem ersten Kanal, der einen Handshake mit der gerufenen Station(en) unterstützt, den Link herstellen.

**Akzeptanzkriterien:**
- `AC-LINK-011-1` — Die rufende Station sendet Aufrufe auf gescannten Kanälen in der durch den Kanal-Auswahl-Algorithmus bestimmten Reihenfolge.
- `AC-LINK-011-2` — Der Link wird auf dem ersten Kanal hergestellt, der einen erfolgreichen Handshake unterstützt.

---

#### REQ-LINK-015 — Frame End Detection
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

#### REQ-LINK-026 — Slotted Responses
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

#### REQ-LINK-027 — Slotted Response Frames
**Spec-Referenz:** A.5.5.4.1.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Slotted Response Frames sind identisch mit den Antworten im Einzelruf-Protokoll (siehe Abb. A-32), einschließlich eines führenden Aufrufs, eines optionalen Nachrichtenabschnitts und einer Frame-Schlussfolge. Eine gerufene Station schließt ihre Antwort mit TIS ab, um sie zu akzeptieren, oder mit TWAS, um sie abzulehnen. Wenn die aufrufende und die gerufene Station eine einwortige Adresse verwenden (wie gezeigt), sind die Slots jeweils 14 Tw, also etwa 1,8 Sekunden.

**Akzeptanzkriterien:**
- `AC-LINK-027-1` — Slotted Response Frames sind identisch mit Einzelruf-Antworten.
- `AC-LINK-027-2` — Die Frames enthalten führenden Aufruf, optionalen Nachrichtenabschnitt und Frame-Schlussfolge.
- `AC-LINK-027-3` — Die Antwort wird mit TIS oder TWAS abgeschlossen.
- `AC-LINK-027-4` — Bei einwortiger Adresse sind die Slots 14 Tw.

---

#### REQ-LINK-028 — Slot Widths
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

#### REQ-LINK-029 — Slot Wait Time Formula
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

#### REQ-LINK-030 — Slotted Response Example
**Spec-Referenz:** A.5.5.4.1.4
**Priorität:** MUST · **Status:** offen

**Anforderung:** Das slotted Response-Beispiel ist in Abb. A-33 gezeigt.

**Akzeptanzkriterien:**
- `AC-LINK-030-1` — Das Beispiel ist in Abb. A-33 gezeigt.
- `AC-LINK-030-2` — Die Beispieldarstellung ist korrekt.

---

#### REQ-LINK-031 — Star Net Calling Protocol
**Spec-Referenz:** A.5.5.4.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Net-Adresse wird einem Satz von Net-Mitglied-Stationen zugewiesen, wie in A.5.2.4.4 beschrieben. Die Slotnummer und die zu verwendende Adresse für jedes Net-Mitglied sind vorab bekannt und für alle Net-Mitglieder.

**Akzeptanzkriterien:**
- `AC-LINK-031-1` — Eine Net-Adresse wird einem Satz von Net-Mitglied-Stationen zugewiesen.
- `AC-LINK-031-2` — Die Slotnummer und Adresse sind vorab bekannt.
- `AC-LINK-031-3` — Die Informationen sind für alle Net-Mitglieder verfügbar.

---

#### REQ-LINK-032 — Star Net Call
**Spec-Referenz:** A.5.5.4.2.1
**Priorität:** MUST · **Status:** offen

**Anforderung:** Ein Star-Net-Aufruf ist identisch mit einem Einzelruf, außer dass die gerufene Stationsadresse eine Net-Adresse ist, wie in Abb. A-34 gezeigt. Die aufrufende Stationsadresse muss eine individuelle Stationsadresse (nicht eine Net- oder andere Kollektivadresse) sein.

**Akzeptanzkriterien:**
- `AC-LINK-032-1` — Ein Star-Net-Aufruf ist identisch mit einem Einzelruf.
- `AC-LINK-032-2` — Die gerufene Stationsadresse ist eine Net-Adresse.
- `AC-LINK-032-3` — Die aufrufende Stationsadresse ist individuell.

---

#### REQ-LINK-033 — Star Net Response
**Spec-Referenz:** A.5.5.4.2.2
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine ALE-Station einen Aufruf empfängt, der an eine Net-Adresse gerichtet ist, die in ihrem Speicher für eigene Adressen enthalten ist (siehe A.4.3.2), verarbeitet sie den Aufruf mit denselben Prüfungen und Zeitüberschreitungen wie bei einem Einzelruf (siehe A.5.5.3.2). Wenn der Aufruf akzeptabel ist, antwortet sie gemäß A.5.5.4.1 unter Verwendung ihrer zugewiesenen Net-Mitglied-Adresse und Slotnummer für die Net-Adresse, die gerufen wurde.

**Akzeptanzkriterien:**
- `AC-LINK-033-1` — Die Station empfängt einen Aufruf an eine Net-Adresse.
- `AC-LINK-033-2` — Die Station verarbeitet den Aufruf mit Einzelruf-Prüfungen.
- `AC-LINK-033-3` — Die Station antwortet gemäß A.5.5.4.1.
- `AC-LINK-033-4` — Die Antwort verwendet die zugewiesene Net-Mitglied-Adresse und Slotnummer.

---

#### REQ-LINK-034 — Star Net Acknowledgment
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

#### REQ-LINK-036 — Star Group Scanning Call
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

#### REQ-LINK-037 — Star Group Leading Call
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

#### REQ-LINK-038 — Star Group Call Conclusion
**Spec-Referenz:** A.5.5.4.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Der optionale Nachrichtenabschnitt und die Schlussfolge eines Star-Group-Aufrufs müssen gemäß A.5.2.5.3 erfolgen.

**Akzeptanzkriterien:**
- `AC-LINK-038-1` — Der Nachrichtenabschnitt ist optional.
- `AC-LINK-038-2` — Die Schlussfolge folgt A.5.2.5.3.
- `AC-LINK-038-3` — Die Vorgaben sind korrekt.

---

#### REQ-LINK-040 — Star Group Slotted Responses
**Spec-Referenz:** A.5.5.4.3.5
**Priorität:** MUST · **Status:** offen

**Anforderung:** Slotted Responses werden gemäß A.5.5.4.1 gesendet und überprüft, unter Verwendung der abgeleiteten Slot-Nummern und der eigenen Adresse, die im führenden Aufruf enthalten ist.

**Akzeptanzkriterien:**
- `AC-LINK-040-1` — Slotted Responses werden gemäß A.5.5.4.1 gesendet.
- `AC-LINK-040-2` — Slotted Responses werden überprüft.
- `AC-LINK-040-3` — Die abgeleiteten Slot-Nummern werden verwendet.
- `AC-LINK-040-4` — Die eigene Adresse wird verwendet.

---

#### REQ-LINK-041 — Star Group Acknowledgment
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

#### REQ-LINK-042 — Star Group Call Example
**Spec-Referenz:** A.5.5.4.3.7
**Priorität:** MUST · **Status:** offen

**Anforderung:** In dem Beispielgruppenruf in Abb. A-35 sendet SAMUEL in Slot 1, mit Tswt = 14 Tw (das einwortige Adresse JOE verursacht Slot 0 auf 14 Tw). EDGAR sendet in Slot 2, mit Tswt = 14 + 17 Tw = 31 Tw (Slot 1 ist 17 Tw wegen SAMUELS zweifachem Adresswort). BOB sendet in Slot 3, mit Tswt = 48 Tw. JOE sendet eine Bestätigung nach 62 Tw.

**Akzeptanzkriterien:**
- `AC-LINK-042-1` — Die Beispielgruppe ist in Abb. A-35 gezeigt.
- `AC-LINK-042-2` — Die Slot-Zeiten sind korrekt.
- `AC-LINK-042-3` — Die Bestätigung wird korrekt gesendet.

---

#### REQ-LINK-043 — Multiple Self Addresses in Group Call
**Spec-Referenz:** A.5.5.4.3.8
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wenn eine Station mehrfach in einem Gruppenruf angesprochen wird, selbst mit verschiedenen Adressen, soll sie mindestens eine Adresse antworten. Hinweis: Die Tatsache, dass die ansprechende Station mehrere Adressen hat, ist dem Aufrufer nicht bekannt. In einigen Fällen wäre es verwirrend oder unangemessen, auf eine aber nicht auf eine andere Adresse zu antworten. Redundante Aufrufadressenkonflikte können nach erfolgreicher Verknüpfung aufgelöst werden, falls es ein Problem gibt.

**Akzeptanzkriterien:**
- `AC-LINK-043-1` — Eine Station antwortet auf mehrere Adressen.
- `AC-LINK-043-2` — Die Tatsache, dass die Station mehrere Adressen hat, ist dem Aufrufer nicht bekannt.
- `AC-LINK-043-3` — Konflikte können nach erfolgreicher Verknüpfung aufgelöst werden.

---

### MSG-Bereich

Die Nachrichten- und Orderwire-Funktionen sind in diesem Dokument als FEAT-GEN-013 bis FEAT-GEN-016 beschrieben. Die konkreten technischen Ausprägungen von AMD, DTM und DBM werden dort als separate Feature-Bausteine dokumentiert.

---
### SOUND-Bereich

#### REQ-SOUND-003 — Sounding-Struktur: Identisch zum Basic Call, ohne Calling Cycle und Message

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

#### REQ-SOUND-004 — Minimale redundante Sounding-Zeit (Trs)

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

#### REQ-SOUND-005 — Single-Channel Sounding-Protokoll

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

#### REQ-SOUND-007 — Multichannel Sounding: Timing-Berechnung für Scanning Sound Frame

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

#### REQ-SOUND-008 — Call-Rejection Scanning Sounding Protocol

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

#### REQ-SOUND-009 — Scanning Detection: Mindestens eine Erfassungsmöglichkeit pro Kanal

**Spec-Referenz:** A.5.3.3
**Priorität:** MUST · **Status:** offen

**Anforderung:** Wie in der Abbildung dargestellt, wurde das Timing von "A" und "B" vorab vereinbart, um sicherzustellen, dass "B" mindestens eine Gelegenheit pro Kanal hat, "A"s Sound zu erreichen und zu "captures". Spezifisch: "B" kommt an, erkennt Sounds, wartet auf gute Wörter, liest mindestens drei (redundante) "TWAS A" (in 3 bis 4 Tw), speichert die Konnektivitätsinformation und verlässt sofort den Kanal, um das Scanning fortzusetzen.

**Akzeptanzkriterien:**
- `AC-SOUND-009-1` — Das Timing von Sounding- und Scanning-Stationen ist vorab vereinbart, um mindestens eine Erfassungsmöglichkeit pro Kanal sicherzustellen.
- `AC-SOUND-009-2` — Die scannende Station muss mindestens drei redundante Wörter des Sounding-Senders lesen können.
- `AC-SOUND-009-3` — Die scannende Station speichert die Konnektivitätsinformation und setzt das Scanning fort.

---

#### REQ-SOUND-010 — Call-Acceptance Scanning Sounding Protocol

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

### SYNC-Bereich

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

### WAVEFORM-Bereich

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
**Priorität:** MUST · **Status:** implementiert

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
**Priorität:** MUST · **Status:** implementiert

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
**Priorität:** MUST · **Status:** implementiert

**Anforderung:** Die verdreifachte-Wort-Periode (3×Tw) für das grundlegende redundante Format muss 392 ms betragen.

**Akzeptanzkriterien:**
- `AC-WAVEFORM-010-1` — 3×Tw muss exakt 392 ms betragen

**Vom-Standard-vorgegebene Werte:**

| Parameter | Wert | Einheit | Spec-Referenz |
|---|---|---|---|
| Verdreifachte-Wort-Periode (3×Tw) | 392 | ms | A.5.1.3 |

---


### Hinweis zu REQ-GEN-026 und REQ-GEN-027–031

> Diese IDs werden in den User-Story-Texten von REQUIREMENTS.md referenziert, haben aber keinen eigenen Requirement-Block. Die inhaltliche Abdeckung erfolgt durch:
> - **REQ-GEN-026** entspricht REQ-GEN-021 (ALE Betriebsregeln, Tabelle A-V) → FEAT-GEN-009
> - **REQ-GEN-027–031** entsprechen REQ-GEN-022–025 (AQC-ALE) → FEAT-GEN-010




## ANNEX A — Definitions of timing symbols

Die folgenden Bezeichnungen und Kurzdefinitionen entsprechen dem Begriffsverzeichnis für die Timing-Variablen.

| Symbol     | Bedeutung                                                                                           |
| ---------- | --------------------------------------------------------------------------------------------------- |
| `C`        | Anzahl der Kanäle in der Sequenz                                                                    |
| `H`        | Handshake; vollständige Abfolge aus Call, Response und Acknowledgment                               |
| `n`        | Ganze Zahl                                                                                          |
| `NA`       | Anzahl der Adressen                                                                                 |
| `NAm`      | Anzahl der Adressen mit `m` Wörtern                                                                 |
| `NAW`      | Anzahl der ursprünglichen individuellen Adresswörter                                                |
| `NS`       | Anzahl der Slots in der Response-Periode                                                            |
| `s`        | Sekunden                                                                                            |
| `SN`       | Slot-Nummer                                                                                         |
| `T`        | Zeit                                                                                                |
| `Ta`       | Zeit einer vollständigen Einzel- oder Netzadresse                                                   |
| `Tal`      | Zeit des ersten Wortes einer Einzel- oder Netzadresse                                               |
| `Ta max`   | Maximale Zeit einer vollständigen Einzel- oder Netzadresse                                          |
| `Tc`       | Call-Zeit; Kombination vollständiger Adresse(n), typischerweise als führender Call `T1c` wiederholt |
| `Tc1`      | Kombinierte erste Wörter einer Gruppenstation-Adresse                                               |
| `Tcc`      | Calling-Cycle-Zeit                                                                                  |
| `Tc max`   | Maximale Call-Zeit                                                                                  |
| `Td`       | Grund-Dwell-Zeit pro Kanal während des Scans                                                        |
| `Tdbm`     | DBM-Zeit                                                                                            |
| `Tdek`     | Decode-Zeit                                                                                         |
| `Tdrrw`    | Detect rotating redundant word time                                                                 |
| `Tdrw`     | Detect redundant word time                                                                          |
| `Tds`      | Detect signaling time (Töne und Timing)                                                             |
| `Tenk`     | Encode-Zeit                                                                                         |
| `T1c`      | Leading call time                                                                                   |
| `T1d`      | Zusätzliche Delay-Zeit für späte Worterkennung                                                      |
| `Tlrw`     | On-air leading redundant words                                                                      |
| `T1ww`     | Last word wait delay                                                                                |
| `Tm`       | Orderwire-Message-Section-Zeit                                                                      |
| `Tm max`   | Maximale Orderwire-Message-Section-Zeit                                                             |
| `Tp`       | Propagationszeit                                                                                    |
| `Tps`      | Periodische Sounding-Periode                                                                        |
| `Trc`      | Redundant call time                                                                                 |
| `Trd`      | Interne Signalverzögerung des Empfängers                                                            |
| `Trs`      | Redundant sound time                                                                                |
| `Trsc`     | Scanning redundant call time                                                                        |
| `Trw`      | Redundant word time                                                                                 |
| `Trwp`     | Redundant word phase delay                                                                          |
| `Ts`       | Scan-Periode                                                                                        |
| `Tsc`      | Scan calling time                                                                                   |
| `Ts max`   | Maximale Scan-Periode                                                                               |
| `Ts min`   | Minimale Scan-Periode                                                                               |
| `Tsrc`     | Scanning redundant call time                                                                        |
| `Tsrs`     | Scanning redundant sound time                                                                       |
| `Tss`      | Scan sounding time                                                                                  |
| `Tsw`      | Slot width time                                                                                     |
| `Tswt`     | Slot wait time nach Ende des Calls bis Beginn der Slotted Response                                  |
| `Tt`       | Tuneup-Zeit für Antennentuner oder Coupler                                                          |
| `Tta`      | Turnaround-Zeit vom Signalende bis Antwortbeginn                                                    |
| `Ttc`      | Transmitter command time                                                                            |
| `Ttd`      | Interne Signalverzögerung des Senders                                                               |
| `Ttk`      | Transmitter acknowledgment time                                                                     |
| `Ttone`    | Tonperiode                                                                                          |
| `Tw`       | Wortzeit                                                                                            |
| `Twa`      | Wait for activity time                                                                              |
| `Twan`     | Wait for net acknowledgment time                                                                    |
| `Twan max` | Maximale Wartezeit für spät ankommende Gruppenruf-Acknowledgments                                   |
| `Twce`     | Wait for calling cycle end                                                                          |
| `Twr`      | Wait for reply time                                                                                 |
| `Twrn`     | Wait for net/group reply time                                                                       |
| `Twrt`     | Wait for reply and tune time                                                                        |
| `Twt`      | Wait/listen-first time vor Tune oder Sendung                                                        |
| `Tx`       | Termination section time                                                                            |
| `Tx max`   | Maximale Termination-Section-Zeit                                                                   |
| `WRT`      | Wait for reply timer                                                                                |
| `WRTT`     | Wait for response and tune timer                                                                    |

---

## ANNEX B — Timing

Die folgenden Timing-Regeln, Limits und Defaultwerte sind die normativen Zeitbezüge zu Annex A und zu Tabelle A-XV.

### Basic system timing

| Parameter                          | Formel                     | Wert          |
| ---------------------------------- | -------------------------- | ------------- |
| Tone rate                          | —                          | 125 symbols/s |
| Tone period                        | `Ttone`                    | 8 ms          |
| On-air bit rate                    | —                          | 375 bits/s    |
| On-air individual word period      | `Tw`                       | 130.66 ms     |
| On-air redundant word period       | `Trw = 3 × Tw`             | 392 ms        |
| On-air individual/net address time | `Ta = m × Trw`, `m = 1..5` | 392–1960 ms   |
| Propagation time                   | `Tp`                       | 0–70 ms       |

### System timing limits

| Parameter                              | Formel                        | Wert     |
| -------------------------------------- | ----------------------------- | -------- |
| Maximum individual/net address time    | `Ta max = 5 × Trw`            | 1960 ms  |
| Individual/net address first word      | `Tal = Trw`                   | 392 ms   |
| Maximum group combined first-word time | `Tcl max = 5 × Tal = 5 × Trw` | 1960 ms  |
| Maximum call time                      | `Tc max = 12 × Trw`           | 4704 ms  |
| Maximum scan cycle period              | `Ts max`                      | 50 s     |
| Basic message section time             | `Tm max basic = 30 × Trw`     | 11.76 s  |
| Message section with AMD               | `29 × Trw* + 30 × Trw`        | 23.128 s |
| Message section with DTM               | `29 × Trw* + 353 × Trw`       | 382 Trw  |
| Message section with DBM               | `29 × Trw* + 3560 × Trw`      | 3589 Trw |
| Maximum termination section time       | `Tx max = Ta max`             | 1960 ms  |

**Hinweis zur Message-Verlängerung:** Eine ALE-Orderwire-Erweiterung wie AMD, DTM oder DBM muss spätestens mit Beginn des 30. Wortes starten. Die Länge der erweiterten Orderwire-Protokolle bestimmt dann die effektive Message-Section-Zeit; die Conclusion beginnt am Ende dieser Message-Section.

### Individual calling

| Parameter                             | Formel                                      | Wert                             |
| ------------------------------------- | ------------------------------------------- | -------------------------------- |
| Minimum dwell time bei 5 ch/s         | `Td(5) min`                                 | 200 ms                           |
| Minimum dwell time bei 2 ch/s         | `Td(2) min`                                 | 500 ms                           |
| Minimum dwell time bei 10 ch/s        | `Td(10) min`                                | 100 ms                           |
| Scan period bei normalem Scanning     | `Ts min = C × Td min`                       | abhängig von `C`                 |
| Scan period für Tsc-Berechnung        | `Ts = C × Td = C × Tdrw`                    | `Tdrw`-basiert                   |
| Call time                             | `Tc`                                        | vollständige Adresse(n)          |
| First-word call time                  | `T1c`                                       | erste Wortgruppe der Adresse     |
| Leading call time                     | `T1c = 2 × Tc`                              | doppelte vollständige Adresse    |
| Scanning call time                    | `Tsc = n × Tc1 ≥ Ts`                        | `n` so gewählt, dass `Tsc ≥ Ts`  |
| Calling cycle time                    | `Tcc = Tsc + T1c ≥ Ts + T1c`                | abgeleitet                       |
| Redundant call time                   | `Trc = T1c + Tx`                            | abgeleitet                       |
| Scanning redundant call time          | `Trsc = Tsc + Trc`                          | abgeleitet                       |
| Last word wait delay                  | `T1ww = Trw`                                | 392 ms                           |
| Late word detection delay             | `T1d = Tw`                                  | 130.66 ms                        |
| Redundant word phase delay            | `Trwp = 0 .. Trw`                           | 0–392 ms                         |
| Turnaround time                       | `Tta = Trd + Tdek + Tenk + Ttc + Ttk + Ttd` | abgeleitet                       |
| Wait for calling cycle end            | `Twce = 2 × Ts`                             | Default                          |
| Wait for reply time                   | `Twr`                                       | aus Signal- und Laufzeitanteilen |
| Tune time                             | `Tt`                                        | abhängig vom langsamsten Tuner   |
| Wait for response and tune            | `Twrt = Twr + Tt`                           | abgeleitet                       |
| Detect signaling period               | `Tds ≤ Td(5)`                               | ≤ 200 ms                         |
| Detect redundant word period          | `Tdrw = Trw + spare Trw`                    | 784 ms                           |
| Detect rotating redundant word period | `Tdrrw = 2 × Trw + spare Trw`               | 1176 ms                          |

### Sounding

| Parameter                     | Formel                      | Wert                   |
| ----------------------------- | --------------------------- | ---------------------- |
| Redundant sound time          | `Trs = 2 × Ta(caller)`      | wie führende Call-Zeit |
| Scanning sound time           | `Tss = n × Ta(caller) ≥ Ts` | abgeleitet             |
| Scanning redundant sound time | `Tsrs = Tss + Trs`          | abgeleitet             |

### Star calling

| Parameter                                  | Definition / Formel                                                                                             |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------- |
| Minimum standard slot width                | `Tsw min = 14 Tw` für Standard-Replys                                                                           |
| Minimum standard slot width                | `Tsw min = 17 Tw` für LQA-Replys                                                                                |
| Minimum standard slot width                | `Tsw min = 9 Tw` für „tight slot“ Replys                                                                        |
| Slot width                                 | `Tsw = 14, 17, 9, oder anderes Tw`                                                                              |
| Slot number                                | `SN`                                                                                                            |
| Slot wait time                             | `Tswt = Tsw × SN`                                                                                               |
| General slot wait time                     | `Tswt(SN) = SN × [5 Tw + 2 Ta(caller) + (optional LQA) Trw + (optional message) Tm] + Ta(caller) + ΣTa(called)` |
| Wait for net reply                         | `Twrn = Tsw × NS` oder `Tswt(NS)`                                                                               |
| Wait for net acknowledgment                | `Twan = Twrn + Tdrw`                                                                                            |
| Turnaround plus tune time limits           | `Tta + Tt ≤ 1500 ms` für Standard-Slots                                                                         |
| Turnaround plus tune time limits           | `Tta + Tt ≤ 2100 ms` für Slot 1                                                                                 |
| Turnaround plus tune time limits           | `Tta + Tt ≤ 360 ms` für Slot 0                                                                                  |
| Maximum star-group wait for acknowledgment | `Twan max = 107 Tw + 27 Ta(caller) + 13 Trw + 13 Tm`                                                            |
| Late-arrival default maximum, standard     | `Twan max = 188 Tw`                                                                                             |
| Late-arrival default maximum, with LQA     | `Twan max = 277 Tw`                                                                                             |

### Programmable timing parameters

| Parameter                                        | Typical value                             |
| ------------------------------------------------ | ----------------------------------------- |
| Dwell time per channel, basic receive scanning   | `Td(5) = 200 ms`                          |
| Dwell time per channel, minimum receive scanning | `Td(2) = 500 ms`                          |
| Dwell time for Ts/Tsc calculations               | `Td = Tdrw = 2 × Trw = 784 ms`            |
| Wait before tune or transmit                     | `Twt = 2 s` für Sprach-/Allzweckkanäle    |
| Wait before tune or transmit                     | `Twt = 784 ms` für ALE-/Daten-only-Kanäle |
| Tune time allowance, first call                  | `Tt = 8 × Tw = 1045.33 ms`                |
| Tune time allowance, next try                    | `Tt = 20 s`                               |
| Automatic periodic sounding interval             | `Tps = 45 min`                            |
| Wait for activity after linking/use              | `Twa = 30 s`                              |




### ANNEX C — Summary of ALE Signal Parameters

| Parameter | Wert / Beschreibung |
|------------|----------------------|
| **ALE occupied bandwidth** | 500–2750 Hz |
| **Quantity of tones** | 8 (one per symbol period) |
| **Tone frequencies** | 750; 1000; 1250; 1500; 1750; 2000; 2250; 2500 Hz |
| **Tone values** | `000   001   011   010   110   111   101   100` |
| **Symbol changes** | Tone transitions are phase continuous |
| **Symbol structure** | 3 bits of binary coded data |
| **Symbol rate; period** | 125 symbols per second (sps); 8 ms |
| **Uncoded data rate** | 375 bits per second (b/s) transmitted |
| **Forward error correction** | Golay (24,12,3) half-rate coding (4 modes of correct/detect; 3/4, 2/5, 1/6, or 0/7) |
| **Auxiliary coding (DTM, AMD, basic ALE)** | Redundant ×3, with 2/3 majority vote (49 AMD/basic ALE transmitted bits) |
| **Auxiliary coding (DBM)** | Interleaving depth (ID) = 49 to 21805 = (n × 49) |
| **Coded data rate (DTM, AMD, basic ALE)** | 61.22 b/s |
| **Coded data rate (DBM)** | 187.5 b/s |
| **Coded data bits per basic ALE word (DTM, AMD)** | 24 bits (21 bits = 3 characters + 3-bit preamble) per word |
| **Coded data bits per message (DTM)** | 0 to 7371 bits per block |
| **Coded data bits per message (DBM)** | 0 to 261644 bits per block, plus 16-bit CRC (DBM) |
| **Throughput, maximum data rate (DTM, AMD, basic ALE)** | 53.57 b/s data bits |
| **Throughput, maximum data rate (DBM)** | 187.5 b/s data bits |
| **Characters per word (AMD or basic ALE)** | 0 to 3 expanded 64 or full ASCII characters |
| **Characters per message (DTM)** | 0 to 1053 ASCII characters per block |
| **Characters per message (DBM)** | 0 to 37377 full ASCII characters per block |
| **Character rate (DTM, AMD, basic ALE)** | 7.653 cps |
| **Character rate (DBM)** | 26.79 cps |
| **Equivalent throughput maximum word rate (DTM, AMD)** | 76.53 words per minute (wpm) (5 characters plus space per word) |
| **Equivalent throughput maximum word rate (DBM)** | 267.9 wpm (5 characters plus space per word) |
| **Unit period (DTM, AMD, or ALE word)** | 130.66... ms per word (`Tw`) or 392 ms per triple redundant word (`Trw`) |
| **Message period (DTM)** | 0 to 2.29 minutes per block |
| **Message period (DBM)** | 0 to 23.26 minutes per block |
| **Minimum sound time** | 784 ms (2 × `Trw`) |
| **Minimum call time** | 1176 ms (3 × `Trw`) |
| **Minimum handshake time** | 3528 ms (9 × `Trw`) for three-way linking |
| **Preamble (word types)** | 8 (3 bits) |
| **Character sets or random bits** | ASCII (Basic 38, Expanded 64, Full 128) |
| **Link quality analysis (LQA)** | ALE (BER, SINAD, and MP) |

#### Normative Hinweise

- Die acht ALE-Töne werden mit phasenkontinuierlichen Übergängen übertragen.
- Die Symbolcodierung verwendet 3 Bit pro Symbol bei einer Symbolrate von 125 sps.
- Die Forward Error Correction basiert auf dem Golay-(24,12,3)-Code.
- Standard-ALE, AMD und DTM verwenden eine dreifache Redundanz mit 2/3-Mehrheitsentscheidung.
- DBM verwendet zusätzlich eine tiefe Interleaving-Struktur mit variabler Interleaving-Tiefe.
- LQA-Messungen umfassen die Parameter:
  - **BER** (Bit Error Rate)
  - **SINAD** (Signal-to-Noise and Distortion Ratio)
  - **MP** (Multipath Spread)
