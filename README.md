# PC-ALE 2.0 - Open Source ALE for Amateur Radio

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]() [![Tests](https://img.shields.io/badge/tests-91%2F91%20passing-brightgreen)]() [![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)]() [![License](https://img.shields.io/badge/license-MIT-blue)]()

**Repository:** https://github.com/Alex-Pennington/PC-ALE  
**Author:** Alex Pennington, AAM402/KY4OLB  
**UI Concept:** https://alex-pennington.github.io/pcale-ui-concept/  
**License:** MIT

A modern, production-ready C++17 clean-room implementation of **MIL-STD-188-141B Automatic Link Establishment (ALE)** and **FED-STD-1052 ARQ** for HF radio systems.

---

## 🎯 Project Philosophy

This implementation is built **entirely from public MIL-STD specifications**, not derived from proprietary source code. Reference implementations are analyzed only for validation and understanding expected behavior, following clean-room engineering principles.

**Key Principles:**
- ✅ Specification-first development
- ✅ Modern C++17 standard library usage
- ✅ Comprehensive unit testing (100% pass rate)
- ✅ Zero dependencies on legacy code
- ✅ Cross-platform (Windows, Linux, macOS, Raspberry Pi)
- ✅ Production-ready architecture
- ✅ Community-driven development

---

## 🚀 Quick Start

### Build
```bash
git clone https://github.com/dl3hc/PC-ALE.git
cd PC-ALE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --verbose
```

### Optional: Hamlib (Radio CAT/PTT)

Ohne Hamlib laufen alle Protokoll-, Audio- und Modem-Funktionen normal.
`--radio` ist dann nicht verfügbar.

**Linux:**
```bash
sudo apt install libhamlib-dev
cmake -S . -B build   # wird automatisch erkannt
cmake --build build
```

**Windows (einmalig in MSYS2 MinGW64-Shell):**

MSYS2 von https://www.msys2.org/ installieren, dann in der MinGW64-Shell:
```bash
pacman -S git base-devel mingw-w64-x86_64-toolchain automake autoconf libtool
bash scripts/build_hamlib.sh
```

Das Skript klont Hamlib 4.5.2, baut es als statische Library und installiert
sie nach `libs/hamlib-built/`. Danach genügt ein normales CMake-Build:

```powershell
mkdir build; cd build; cmake ..; cmake --build .
```

CMake erkennt `libs/hamlib-built/` automatisch und linkt statisch —
keine DLLs, kein PATH-Setup nötig.

> **Endnutzer** müssen das nicht selbst tun — fertig gebaute Binaries
> werden mit jedem Release als Download bereitgestellt.

---

## 📡 ale_cli — Verwendung

### Grundprinzip

Jede Instanz startet im Idle-Modus und reagiert automatisch auf Anrufe.
`--call` initiiert beim Start einen Anruf; ohne `--call` wartet die Station.

```
ale_cli --self ADDR [--call TARGET] [Audio-Optionen] [Radio-Optionen] [RX-Tuning]
```

### Pflichtparameter

| Option | Beschreibung |
|--------|-------------|
| `--self ADDR` | Eigene ALE-Adresse (3–15 Zeichen, Basic-38-Zeichensatz) |

### Audio

| Option | Beschreibung |
|--------|-------------|
| `--in-device NAME` | Soundkarten-Eingang (Teilstring des Gerätenamens) |
| `--out-device NAME` | Soundkarten-Ausgang |
| `--list-devices` | Verfügbare Audiogeräte auflisten und beenden |

### Radio (CAT / PTT)

| Option | Beschreibung |
|--------|-------------|
| `--radio hamlib:229:COM3` | IC-7300 über seriellen Port COM3 |
| `--radio hamlib:229:tcp://127.0.0.1:4532` | IC-7300 über laufenden rigctld |
| `--radio hamlib:2:tcp://127.0.0.1:4532` | `radio_mock.exe` (Test ohne Hardware) |

Hamlib-Modell-IDs: `rigctl -l` listet alle unterstützten Geräte.

### RX-Empfindlichkeit (A.5.2.6.3)

| Option | Beschreibung |
|--------|-------------|
| `--golay-mode N` | Golay-Korrekturstärke: 3=3/4 (Standard), 2=2/5, 1=1/6, 0=0/7 |
| `--unanimous N` | Min. übereinstimmende 2/3-Votes pro Wort (0–49, Standard 33) |
| `--adaptive` | Golay-Modus + Votes automatisch an Signalqualität anpassen |
| `--debug-rx` | RX-Pegel und jedes dekodierte Wort auf stdout ausgeben |

### Sonstige Optionen

| Option | Beschreibung |
|--------|-------------|
| `--no-scan` | Scanning-Abschnitt überspringen (Zielstation auf Festkanal) |
| `--channels FILE` | Channel-Liste aus `.ale`-Datei laden; Änderungen via CMD werden automatisch zurückgeschrieben |

### Laufzeit-Kommandos (stdin)

Während `ale_cli` läuft, können Kommandos über stdin eingegeben werden:

**Verbindungssteuerung:**

| Kommando | Beschreibung |
|----------|-------------|
| `CMD:CALL ADDR` | Verbindungsaufbau zu ADDR initiieren |
| `CMD:AMD TEXT` | AMD-Orderwire-Nachricht für den nächsten Anruf einreihen (max. 90 Zeichen) |
| `CMD:TERMINATE` | Aktive Verbindung beenden |
| `CMD:REJECT` | Eingehenden Anruf ablehnen (TWAS) |
| `CMD:SCAN` | Scanner-Modus starten |
| `CMD:STATUS` | Aktuellen SM-Zustand ausgeben |
| `CMD:HELP` | Vollständige Kommandoliste |

**Channel-Verwaltung:**

| Kommando | Beschreibung |
|----------|-------------|
| `CMD:ADD_CHANNEL rx_hz[:tx_hz] [mode] [label]` | Channel hinzufügen oder überschreiben |
| `CMD:DEL_CHANNEL rx_hz` | Channel per RX-Frequenz entfernen |
| `CMD:LIST_CHANNELS` | Aktuelle Channel-Liste ausgeben |
| `CMD:CLEAR_CHANNELS` | Alle Channels entfernen |
| `CMD:SAVE_CHANNELS [path]` | Channel-Liste in Datei schreiben |
| `CMD:LOAD_CHANNELS path` | Channel-Liste aus Datei laden |

**Net-Verwaltung:**

| Kommando | Beschreibung |
|----------|-------------|
| `CMD:ADD_NET name` | Net hinzufügen |
| `CMD:DEL_NET name` | Net entfernen |
| `CMD:ASSIGN_CHANNEL net id` | Channel-ID einem Net zuweisen |
| `CMD:UNASSIGN_CHANNEL net id` | Channel-ID aus einem Net entfernen |
| `CMD:LIST_NETS` | Alle Nets mit zugewiesenen Channels ausgeben |

---

## 🔁 Testszenarien

### Ein-PC-Test (Loopback mit VB-Audio CABLE A+B)

```
Terminal 1 (BOB — wartet):
  ale_cli --self BOB --in-device "CABLE-A Output" --out-device "CABLE-B Input"

Terminal 2 (SAM — ruft an):
  ale_cli --self SAM --in-device "CABLE-B Output" --out-device "CABLE-A Input"
  → CMD:CALL BOB
```

CABLE-A trägt SAM→BOB, CABLE-B trägt BOB→SAM.

### Zwei-PC-Test (physisches Kabel oder Soundkarten-Loopback)

```
PC 1 (BOB):  ale_cli --self BOB
PC 2 (SAM):  ale_cli --self SAM
→ Lautsprecherausgang PC2 an Mikrofon PC1 (und umgekehrt)
→ CMD:CALL BOB im SAM-Terminal
```

### Drei-Terminal-Test mit Mock-TRX

Testet den vollständigen Radio-Steuer-Code-Pfad (PTT, Frequenz, Modus) ohne Hardware:

```
Terminal 1 — Mock-TRX starten:
  radio_mock.exe

Terminal 2 — BOB (wartet):
  ale_cli --self BOB \
          --in-device "CABLE-A Output" --out-device "CABLE-B Input" \
          --radio hamlib:2:tcp://127.0.0.1:4532

Terminal 3 — SAM (ruft an):
  ale_cli --self SAM \
          --in-device "CABLE-B Output" --out-device "CABLE-A Input" \
          --radio hamlib:2:tcp://127.0.0.1:4532
  → CMD:CALL BOB
```

In Terminal 1 ist dann live zu sehen:
```
[TRX] Freq  14000.000 kHz
[TRX] Mode  USB  BW=2400 Hz
[TRX] PTT ON  ←── TX
[TRX] PTT OFF ──► RX
```

---

## 📨 AMD — Automatic Message Display (A.5.7.2)

AMD ermöglicht, kurze ASCII-Klartexte im Rahmen eines normalen Anrufs zu übertragen.
Der Text erscheint beim Empfänger sofort — ohne dass eine vollständige Verbindung
(LINKED-Zustand) aufgebaut sein muss.

### Zeichensatz

AMD verwendet den **Expanded-64-Zeichensatz** (A.5.7.2.1): Großbuchstaben, Ziffern,
Leerzeichen und gängige Sonderzeichen (0x20–0x5F).  Kleinbuchstaben sind **nicht**
im Zeichensatz enthalten.

### Ablauf

```
Sender (SAM):                        Empfänger (BOB):
  CMD:AMD POSITION 14MHZ             [wartet im Scan-Modus]
  CMD:CALL BOB
    → sendet: …TO:BOB  CMD:POS  DATA:ITIO  REP:N 1  DATA:4MHZ  TIS:SAM…
                                     [AM] AMD from SAM: POSITION 14MHZ
                                     [>>] Incoming call from: SAM
                                     [>>] Protocol response in progress...
  ← 3-Wege-Handshake läuft automatisch →
                                     LINK ESTABLISHED  Peer: SAM
```

Der AMD-Text wird **vor** der `on_call_received`-Meldung ausgegeben, damit der
Operator den Kontext kennt, bevor die Verbindung bestätigt wird.

### Frame-Aufbau (MIL-STD-188-141B A.5.7.2.2)

Das erste AMD-Wort trägt den **CMD-Präambel** (Binär 110) mit den ersten 3 Zeichen.
Alle weiteren Wörter wechseln zwischen DATA und REP:

```
CMD(Zeichen 1–3)  DATA(4–6)  REP(7–9)  DATA(10–12)  …  TIS:SAM
```

Maximal **30 Wörter = 90 Zeichen** pro Nachricht (A.5.7.2.3, Tm\_max).
Das letzte Wort wird bei Bedarf automatisch mit Leerzeichen (0x20) aufgefüllt.

### Beispiele

```
# Einfache Statusmeldung (Leerzeichen erlaubt):
CMD:AMD QRV 14250 USB
CMD:CALL BOB

# Positionsmeldung mit Koordinaten:
CMD:AMD 52N 013E 14250
CMD:CALL HQ

# Ohne folgende Verbindung (nur AMD, kein Link):
# AMD wird auf jedem Kanal gesendet, solange CMD:CALL läuft (Multi-Channel-Retry).
```

### Empfangsausgabe

```
[AM] AMD from SAM: QRV 14250 USB
[>>] Incoming call from: SAM
```

### Hinweise

- `CMD:AMD` reiht die Nachricht ein und gilt für den **nächsten** `CMD:CALL`.
  Bei Multi-Channel-Calling wird sie auf **jedem Kanal** erneut gesendet.
  Nach Abschluss des Rufs (erfolgreich oder alle Kanäle erschöpft) wird sie automatisch gelöscht.
- Eine neue `CMD:AMD`-Eingabe überschreibt die vorherige.
- Nicht-Expanded-64-Zeichen werden durch `?` ersetzt.
- Der Empfänger muss **nicht** im LINKED-Zustand sein — AMD funktioniert
  während der gesamten Handshake-Phase.

---

## 📻 Channel-Verwaltung

Channels steuern, auf welcher Frequenz (und in welchem Modus) `ale_cli` sendet und empfängt.
Ohne konfigurierte Channels bleibt das Radio auf der Frequenz, auf der es vor dem Start war.

### Channel-Format

```
CMD:ADD_CHANNEL rx_hz[:tx_hz]  [mode]  [label]
```

| Feld | Beschreibung |
|------|-------------|
| `rx_hz` | RX-Frequenz in Hz (Pflicht) |
| `tx_hz` | TX-Frequenz in Hz (optional; 0 oder fehlt = Simplexbetrieb) |
| `mode` | Modulationsart: `USB` (Standard), `LSB`, `AM`, `FM`, `CW`, `DATA_USB`, … |
| `label` | Freitext-Label (optional, beliebig lang) |

**Beispiele:**

```
CMD:ADD_CHANNEL 14250000                          # 14,250 MHz USB Simplex
CMD:ADD_CHANNEL 14250000 USB 40m-Calling          # mit Label
CMD:ADD_CHANNEL 3500000:3600000 LSB 80m-DX        # Duplex RX/TX getrennt
CMD:ADD_CHANNEL 14074000 DATA_USB FT8             # Datenmodus
```

Jeder Channel erhält automatisch eine **Channel-ID** (`C-1`, `C-2`, …, analog zur
Annex-B-Tabelle A-XVI), sobald er per `CMD:ADD_CHANNEL` oder per Datei geladen wird.
Diese ID wird verwendet, um Channels einem **Net** zuzuweisen (siehe unten) —
sie ändert sich nicht, wenn die Frequenz nachträglich geändert wird.

### Non-Volatile-Speicherung (`.ale`-Dateiformat)

Channels werden in einer einfachen Textdatei mit der Endung `.ale` gespeichert —
eine Zeile pro Channel, Kommentare beginnen mit `#`. Die `ID:`-Markierung ist
optional beim Laden (alte Dateien ohne ID bekommen beim Laden automatisch
fortlaufende IDs zugewiesen) und wird beim Speichern immer geschrieben:

```
# PC-ALE channel list — MIL-STD-188-141B
# ID:id rx_hz tx_hz mode [label]
ID:C-1 14250000 0 USB 40m-Calling
ID:C-2 7100000 0 USB
ID:C-3 3500000 3600000 LSB 80m-DX
ID:C-4 14074000 0 DATA_USB FT8
# NET:name id,id,...
NET:XYZ C-1,C-3
```

### Verwendung mit `--channels`

```powershell
# Beim Start laden (Datei muss nicht existieren — wird beim ersten Speichern angelegt):
ale_cli.exe --self SAM --channels meine_channels.ale

# Danach Channels zur Laufzeit hinzufügen — werden sofort in meine_channels.ale geschrieben:
CMD:ADD_CHANNEL 14250000 USB 40m-Calling
CMD:ADD_CHANNEL 7100000 USB

# Vollständiges Beispiel mit Radio:
ale_cli.exe --self SAM \
            --channels meine_channels.ale \
            --radio hamlib:229:tcp://127.0.0.1:4532 \
            --in-device "CABLE-B Output" --out-device "CABLE-A Input"
```

Wenn `--channels` gesetzt ist:
- `CMD:ADD_CHANNEL`, `CMD:DEL_CHANNEL` und `CMD:CLEAR_CHANNELS` schreiben die Datei automatisch.
- `CMD:SAVE_CHANNELS` speichert explizit (auch ohne `--channels`).
- Beim nächsten Start werden alle Channels sofort wiederhergestellt.

---

## 📡 Net-Verwaltung

Ein **Net** ist eine benannte Teilmenge von Channel-IDs (z.B. Net `XYZ` →
`{C-1, C-3, C-7}`). Nets dienen dazu, beim Anruf einer Gegenstation automatisch
die richtige Scanning-Call-Länge zu bestimmen: `Tsc = C × 2 × Trw`, wobei `C`
die Anzahl der **scan-fähigen** (`SCAN=Y`, intern `Channel::enabled`) Channels
des Nets ist, dem die Gegenstation laut Adressbuch (`CMD:ADD_CONTACT`-Äquivalent
`ALEController::add_contact()`) angehört.

```
CMD:ADD_NET XYZ
CMD:ASSIGN_CHANNEL XYZ C-1
CMD:ASSIGN_CHANNEL XYZ C-3
CMD:LIST_NETS
  XYZ: C-1,C-3  [scan=2]
```

Nets werden zusammen mit den Channels in dieselbe `.ale`-Datei gespeichert
(`NET:`-Zeilen, siehe oben) und beim nächsten Start wiederhergestellt.

Ist die Gegenstation nicht im Adressbuch oder keinem Net zugeordnet, bleibt die
zuvor konfigurierte Scanning-Call-Länge (Standard: 1 Channel) unverändert —
das bestehende Verhalten von `ale_cli` (`--no-scan`) ändert sich dadurch nicht.

---

## radio_mock — Mock-TRX Server

`radio_mock.exe` implementiert das Hamlib rigctld-Netzwerkprotokoll und gibt
alle CAT-Befehle, die `ale_cli` sendet, lesbar im Terminal aus — ohne jede Hardware.

```bash
radio_mock.exe [port]    # Default-Port: 4532
```

`ale_cli` verbindet sich mit `--radio hamlib:2:tcp://127.0.0.1:<port>`.
Hamlib-Modell `2` ist `RIG_MODEL_NETRIGCTL` — der eingebaute rigctld-Client-Backend.

---

## 📶 LQA-Austausch und Auto-Relink (A.5.4.4 / A.5.4.5)

PC-ALE implementiert den vollständigen bilateralen LQA-Austausch nach MIL-STD-188-141B
sowie einen spec-konformen Mechanismus zur automatischen Kanaloptimierung.

### Bilateraler Kanal-Score

Beim normalen 3-Wege-Handshake (CALL → RESPONSE → ACK) tauschen beide Stationen ihre
lokale Empfangsqualität via **CMD-LQA-Wörter** (Tabelle A-XIV, Zeichenwert `'a'`) aus.
Das `KA1`-Bit (Bit 13) in SAMs CMD-Wort signalisiert JOE, dass eine Rückmeldung
erwartet wird — JOE antwortet in seiner RESPONSE mit einem eigenen CMD 'a' und (falls
KA1=1) zusätzlichen LQA-Report-Wörtern für alle bekannten Kanäle.

Nach dem Handshake haben beide Stationen **bidirektionale** Qualitätsdaten:
- **FROM-Qualität:** Lokal gemessen (SINAD, BER aus Soundings und laufendem Link)
- **TO-Qualität:** Vom Peer gemeldet (was er von uns empfängt)

Der **bilaterale Kanal-Score** (A.5.4.5) ist das Minimum beider Richtungen:
```
score = min(FROM-Qualität, TO-Qualität)
```
Die schlechtere Übertragungsrichtung bestimmt die nutzbare Linkqualität.

### Auto-Relink

Wenn `Auto-Relink` aktiviert ist, überwacht die Station im `LINKED`-Zustand kontinuierlich,
ob ein anderer bekannter Kanal bilateral deutlich besser wäre. Ist die Bedingung erfüllt:

```
bilateraler Score (bester Kanal) > bilateraler Score (aktueller Kanal) + Threshold
```

…terminiert die Station den Link ordnungsgemäß per **TWAS** und initiiert sofort einen
neuen Call auf dem besseren Kanal. JOE geht nach TWAS in SCANNING zurück und nimmt den
Neulink an.

Ein konfigurierbarer **Schwellwert** (Standard: 5 Punkte auf der 0–30-Skala) verhindert
"Channel-Thrashing" bei geringfügigen Qualitätsunterschieden.

**Konfiguration (GUI → Einstellungen → LQA-Tab):**
- **Auto-Relink** Toggle — aktiviert den Mechanismus
- **Threshold** — Mindestverbesserung in Score-Punkten (1–30)

**Bridge-API:**
```json
→ { "cmd": "RELINK_SET", "relink_enabled": true, "relink_threshold": 5.0 }
→ { "cmd": "RELINK_GET" }
```

Vollständige technische Dokumentation: [docs/LQA_BILATERAL_RELINK.md](docs/LQA_BILATERAL_RELINK.md)

---

## 📊 Current Status

| Phase | Component | Status |
|-------|-----------|--------|
| 1 | 8-FSK Modem Core | ✅ Complete |
| 2 | Word Structure & Protocol | ✅ Complete |
| 3 | Link State Machine | ✅ Complete |
| 4 | AQC-ALE Extensions | ✅ Complete |
| 5 | FS-1052 ARQ Protocol | ✅ Complete |
| 6 | LQA System | ✅ Complete |
| 7 | Audio I/O (WASAPI/ALEController) | ✅ Complete |
| 8 | Radio CAT / PTT (Hamlib + IRadio) | ✅ Complete |

### Integration Layers — Status

| Module | Purpose | Status |
|--------|---------|--------|
| Audio I/O | WASAPI (Windows); ALSA/CoreAudio geplant | ✅ Windows fertig |
| Radio CAT/PTT | Hamlib-Backend; `ale_cli --radio` | ✅ Fertig |
| `radio_mock` | rigctld-kompatibler Test-TRX | ✅ Fertig |
| Qt6 UI | Cross-platform Benutzeroberfläche | ❌ Ausstehend |
| SDR Integration | Direktanbindung SDR (SoapySDR o.ä.) | ❌ Ausstehend |

---

## 🏗️ Architecture: Protocol Stack + Platform Layer

PC-ALE 2.0 uses a **two-repository architecture**:

```
┌─────────────────────────────────────────────────────┐
│  PC-ALE (This Repository)                           │
│  ┌───────────────────────────────────────────────┐  │
│  │ Layers 3-7: ALE Protocol Stack                │  │
│  │ • FS-1052 ARQ (Layer 4)                       │  │
│  │ • ALE State Machine (Layer 3)                 │  │
│  │ • 2G/AQC Protocol, LQA (Layer 3)              │  │
│  │ • 8-FSK Modem, Golay FEC (Physical)           │  │
│  └───────────────┬───────────────────────────────┘  │
└──────────────────┼──────────────────────────────────┘
                   │ Uses interfaces from
┌──────────────────▼──────────────────────────────────┐
│  PC-ALE-PAL (Platform Abstraction Layer)            │
│  ┌───────────────────────────────────────────────┐  │
│  │ Layers 1-2: Hardware Abstraction Interfaces   │  │
│  │ • IAudioDriver - Sound card I/O               │  │
│  │ • IRadio - Frequency, mode, PTT, power        │  │
│  │ • ITimer - Millisecond timing                 │  │
│  │ • ILogger - Diagnostic output                 │  │
│  │ • IEventHandler - Threading/callbacks         │  │
│  │ • ISIS - Serial communication                 │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                   │ Implemented by
┌──────────────────▼──────────────────────────────────┐
│  Platform-Specific Repositories (Community)          │
│  • PC-ALE-Linux-DRAWS (ALSA, libgpiod)              │
│  • PC-ALE-Windows (WASAPI, Hamlib)                  │
│  • PC-ALE-RaspberryPi-Bare (Circle framework)       │
│  • PC-ALE-SDR (SoapySDR/UHD implementation)         │
└─────────────────────────────────────────────────────┘
```

**Why Separate Repositories?**
- PC-ALE = **Pure protocol stack** (no OS dependencies)
- [PC-ALE-PAL](https://github.com/Alex-Pennington/PC-ALE-PAL) = **Interface contracts** (what must be implemented)
- Platform repos = **Concrete implementations** (ALSA, WASAPI, etc.)

**This enables:**
- ✅ Same protocol code on Windows, Linux, macOS, bare metal
- ✅ Multiple platforms can be developed in parallel
- ✅ Clean separation: protocol maintainers don't need hardware expertise
- ✅ Community can contribute platform ports without touching core

**Get Started:**
1. Clone PC-ALE (protocol stack) - this repository
2. Clone [PC-ALE-PAL](https://github.com/Alex-Pennington/PC-ALE-PAL) (interfaces)
3. Clone or create a platform implementation (e.g., PC-ALE-Linux-DRAWS)
4. Build and run!

---

## 📐 Layer Details

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│              (User Interface, Message Handling)              │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                  DATA LINK LAYER (Phase 5)                   │
│     FS-1052 ARQ: Reliable transfer, ACK/NAK, Retransmit     │
│         (Control Frames, Data Frames, CRC-32)                │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│   LQA SYSTEM (Phase 6)  │  LINK ESTABLISHMENT (Phase 3)     │
│   Channel quality       │  State Machine: IDLE→SCANNING→    │
│   Sounding analysis     │  CALLING→HANDSHAKE→LINKED         │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│          PROTOCOL LAYER (Phases 2 & 4)                       │
│   2G ALE Words: Preamble + 21-bit Payload                   │
│   AQC-ALE Extensions: DE fields, CRC, Slotted Response      │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│              PHYSICAL LAYER (Phase 1)                        │
│   8-FSK Modem: 750-2500 Hz, 125 baud, Golay FEC             │
│   FFT Demodulator, Tone Generator, Symbol Decoder           │
└─────────────────────────────────────────────────────────────┘
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for detailed documentation.

---

## 🏆 Key Features

### 8-FSK Modem (Physical Layer)
- FFT-based demodulator with sliding 64-point FFT
- 8 tone generator (750-2500 Hz, 250 Hz spacing)
- Symbol-to-bits decoder with peak detection
- Majority voting (3x redundancy error correction)
- Extended Golay (24,12) encoder/decoder - 3-bit error correction

### Protocol Layer
- Word structure parser (preamble + 21-bit payload)
- Preamble types: DATA, THRU, TO, TWAS, FROM, TIS, CMD, REP
- ASCII-64 character encoding/decoding
- Address book with wildcard matching
- Message assembly and call type detection

### Link State Machine
- 6 states: IDLE, SCANNING, CALLING, HANDSHAKE, LINKED, SOUNDING
- Channel selection, scan list, dwell time management
- Event-driven architecture with callbacks
- Timeout handling (call, link, scan)

### AQC-ALE Extensions
- Data Element extraction (DE1-DE9)
- 16 traffic classes (voice modes, data modes, email)
- Transaction codes (ACK, NAK, TERMINATE)
- CRC-8/CRC-16 orderwire protection
- Slotted response timing (8 slots × 200ms)

### FS-1052 ARQ Protocol
- Selective repeat ARQ with 256-bit ACK bitmap
- CRC-32 frame protection
- Automatic retransmission on timeout/NAK
- Data rates: 75, 150, 300, 600, 1200, 2400, 4800 bps
- Configurable window size and retry limits

### LQA System
- Persistent quality tracking (binary + CSV formats)
- Time-weighted averaging, composite scoring (0-31)
- SNR/BER/SINAD/multipath/noise floor metrics
- Channel ranking and best channel selection
- Sounding analysis and scheduling

---

## 📋 Standards Compliance

| Parameter | Value | Standard |
|-----------|-------|----------|
| **ALE Standard** | MIL-STD-188-141B Appendix A | 2G ALE |
| **ARQ Standard** | FED-STD-1052 | Data Link Protocol |
| **Modulation** | 8-FSK (8 tones) | 750-2500 Hz |
| **Symbol Rate** | 125 baud | 125 symbols/sec |
| **Tone Spacing** | 250 Hz | Narrowband HF |
| **FEC** | Golay (24,12) | 3-bit correction |
| **Sample Rate** | 8000 Hz | Audio sampling |

---

## 🛠️ Libraries

The build produces these static libraries:

| Library | Purpose |
|---------|---------|
| `libale_fsk_core.a` | 8-FSK modulation/demodulation |
| `libale_fec.a` | Golay (24,12) error correction |
| `libale_protocol.a` | Word parsing, messages, addressing |
| `libale_link.a` | ALE state machine |
| `libale_aqc.a` | AQC-ALE protocol extensions |
| `libale_fs1052.a` | FS-1052 ARQ reliable data link |
| `libale_lqa.a` | Link Quality Analysis system |

---

## 🧪 Testing

```bash
cd build
ctest --verbose

# Or run individual test suites:
./test_fsk_core          # Phase 1: FSK modem
./test_protocol          # Phase 2: Protocol parser
./test_state_machine     # Phase 3: State machine
./test_aqc_parser        # Phase 4: AQC-ALE
./test_aqc_crc           # Phase 4: CRC validation
./test_fs1052_frames     # Phase 5: Frame formatting
./test_fs1052_arq        # Phase 5: ARQ state machine
./test_lqa_database      # Phase 6: LQA database
./test_lqa_metrics       # Phase 6: Metrics collection
./test_lqa_analyzer      # Phase 6: Channel analysis
```

---

## 📚 Documentation

- [Architecture](docs/ARCHITECTURE.md) - System design and layers
- [API Reference](docs/API_REFERENCE.md) - Complete API documentation
- [Integration Guide](docs/INTEGRATION_GUIDE.md) - Audio I/O and radio integration
- [LQA-Austausch und Auto-Relink](docs/LQA_BILATERAL_RELINK.md) - Bilateraler CMD-LQA-Austausch und automatische Kanaloptimierung (A.5.4.4/A.5.4.5)
- [MIL-STD Compliance](docs/MIL_STD_COMPLIANCE.md) - Standards compliance matrix
- [Glossary](docs/GLOSSARY.md) - ALE terminology and acronyms
- [Testing Guide](docs/TESTING.md) - Running and writing tests

### Phase Documentation
- [Phase 3: Link State Machine](docs/PHASE3_COMPLETE.md)
- [Phase 4: AQC-ALE Extensions](docs/PHASE4_COMPLETE.md)
- [Phase 5: FS-1052 ARQ](docs/PHASE5_COMPLETE.md)
- [Phase 6: LQA System](docs/PHASE6_COMPLETE.md)

---

## 🤝 Community-Driven Development

This project exists because:
- ION2G has problems and limited development
- PC-ALE 1.x is abandonware
- MARS has excellent tools locked behind membership requirements
- The ham community deserves modern, open, cross-platform ALE software

### Ways to Contribute

| Contribution Type | What It Looks Like |
|-------------------|-------------------|
| **Direct Funding** | Sponsor AI development costs via GitHub Sponsors |
| **AI-Assisted Dev** | Use your own API access to implement features, submit PRs |
| **Testing** | Beta test releases, report bugs, validate on your hardware |
| **Documentation** | Write tutorials, improve guides |
| **Hardware Loans** | Loan radios or SDRs for compatibility testing |
| **OTA Testing** | Participate in on-air interop tests |
| **Code Review** | Review pull requests, audit code quality |

### Funding

AI-assisted development has real compute costs. If you want to support continued development:

| Method | Link |
|--------|------|
| GitHub Sponsors | github.com/sponsors/Alex-Pennington |
| PayPal | paypal.me/prior2fork |

---

## 🔧 Requirements

- **C++ Compiler**: C++17 or later (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake**: 3.15 or later
- **Platform**: Windows, Linux, macOS, Raspberry Pi

No external dependencies required for core functionality.

---

## 📄 License

MIT License - See [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

This clean-room implementation references:
- **MIL-STD-188-141B** - Official U.S. Department of Defense specification
- **FED-STD-1052** - HF Radio Data Link Protocol specification
- **LinuxALE** (GPL) - Analyzed for validation purposes only
- **MARS-ALE** - Design patterns studied, no code reused

---

**PC-ALE 2.0** - Professional-grade HF radio ALE implementation  
*Built from specifications, designed for reliability, open to all*

---

*Alex Pennington, AAM402/KY4OLB*  
*Contact: projects@organicengineer.com*  
*GitHub: github.com/Alex-Pennington*
