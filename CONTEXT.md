1. [Context: Automatic Link Establishment (ALE)]
2. [Architekturüberblick]

# Context: Automatic Link Establishment (ALE)

## Overview

This project implements an **HF Automatic Link Establishment (ALE)** controller according to **MIL-STD-188-141A/B** and **FED-STD-1045A**.

ALE automates HF link establishment by continuously monitoring multiple radio channels, measuring channel quality, and automatically selecting the best channel for communication without operator intervention.

The controller continuously operates as a state machine whose primary states are:

* Available (scanning)
* Linking
* Linked

Only one of these states is active at any given time.

---

# Core Concepts

## Scanning

When no link exists, every station continuously scans a predefined scan list of channels.

Typical scan rates are:

* 2 channels/second
* 5 channels/second

While scanning, the controller listens for:

* incoming ALE calls
* soundings
* channel activity

Scanning stops immediately when:

* an ALE call is detected
* the controller initiates a call
* a link is established

---

## Calling

A station that wishes to communicate with another station performs the following sequence:

1. Consult the LQA database.
2. Select the best known channel for the destination.
3. Verify that the channel is idle.
4. Transmit an ALE call.
5. Wait for a response.
6. If no response is received, retry on the next-best channel.

The calling sequence is entirely driven by the existing LQA database.

A successful link requires the standard three-way handshake:

Caller → Call

Callee → Response

Caller → Acknowledge

Only after completion of this handshake is the link considered established.

---

## Sounding

Sounding is an active channel quality measurement.

Stations periodically transmit short ALE soundings on configured channels.

Purpose:

* allow all listening stations to evaluate the propagation path
* update their own LQA database

Important:

A transmitted sounding is **not** intended to update the sender's own LQA database.

Only stations that **receive** a sounding perform channel measurements.

---

# Link Quality Analysis (LQA)

LQA is the heart of ALE.

It represents historical knowledge about the communication quality between two stations on a specific channel.

Typical metrics include:

* SINAD
* BER
* optional multipath information
* age of measurement

Conceptually:

LQA is indexed by

```
(remote station, channel)
```

Each entry represents channel quality between the local station and one remote station.

The standard defines bilateral information:

* quality measured locally from received signals
* quality reported back by the remote station

Older measurements gradually lose value because HF propagation changes continuously.

---

# Sources of LQA Data

LQA information may originate from only two legitimate sources.

## Passive measurements

Generated whenever a station successfully receives ALE traffic.

Examples:

* incoming calls
* responses
* acknowledgements
* traffic overheard between third-party stations

These measurements reflect actual received signal quality.

---

## Active measurements

Generated from received soundings.

Every received sounding allows the listening station to evaluate:

* SINAD
* BER
* channel usability

These measurements become new LQA entries.

---

# Data That Must NEVER Create LQA Entries

The following events must **not** create LQA database entries:

* transmitting a call
* transmitting a sounding
* initiating link establishment
* unsuccessful calls
* unanswered calls

No channel measurement exists in these situations.

Creating synthetic or inferred LQA values violates the ALE model.

---

# GUI Expectations

The "Heard Stations / LQA" view represents the contents of the LQA database.

Every displayed entry must correspond to an actual received measurement.

The GUI must never display:

* locally generated placeholder values
* speculative LQA values
* copied values
* caller-side estimates
* entries generated solely because the local station transmitted

Displaying such entries indicates corruption or leakage between transmit-side events and the LQA database.

---

# Other Station Table

The controller maintains a persistent table of known remote stations.

Each station contains:

* address
* valid channels
* per-channel LQA information
* optional station-specific configuration

Conceptually:

```
Remote Station
    ├── Channel 1 -> LQA
    ├── Channel 2 -> LQA
    ├── Channel 3 -> LQA
    └── ...
```

This table is used to determine which channel should be selected for future calls.

---

# ALE Design Principles

The implementation should follow these fundamental rules:

* Always listen when not linked.
* Always scan while available.
* Never interfere with occupied channels.
* Use LQA to select channels.
* Update LQA only from actual received measurements.
* Treat LQA as historical measurement data, never as prediction.
* Never fabricate LQA records from locally transmitted events.

---

# Mental Model

Think of ALE as two largely independent subsystems.

## Link Management

Responsible for:

* scanning
* calling
* sounding
* handshakes
* state transitions

## LQA Engine

Responsible only for:

* receiving measurements
* storing channel quality
* aging measurements
* ranking channels

The LQA engine is **measurement-driven**, not **transmission-driven**.

This distinction is critical and prevents many implementation bugs.

---

## 2. Architekturüberblick

### 2.1 Schichtenmodell

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



