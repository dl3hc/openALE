# Architecture Guardrail — PC-ALE System

## SCOPE OF THIS DOCUMENT

> **Aktiver Arbeitsbereich: ausschliesslich `PC-ALE` (Core / Domain)**
>
> Der Coding Agent arbeitet AUSSCHLIESSLICH am Repository `PC-ALE`.
> Weder Platform-Adapter (PC-ALE-Win, Linux, SDR) noch Application Layer
> werden in dieser Phase geschrieben.
>
> Diese Guardrail beschreibt die Gesamtarchitektur zur Orientierung — damit
> der Agent versteht, welche Rolle `PC-ALE` im System spielt und welche
> Grenzen der Core einzuhalten hat. Die anderen Schichten existieren
> bereits in rudimentärer Form oder entstehen später in separaten Projekten.
>
> **Reihenfolge:**
> 1. `PC-ALE` Core vollständig überarbeiten, verifizieren ← WIR SIND HIER
> 2. Application Layer entsteht in einem separaten Projekt

---

## 1. Objective

This document defines the mandatory architectural rules for the entire PC-ALE
system. All code changes MUST comply with these rules. Deviations are not
permitted unless explicitly marked as ARCHITECTURE-CHANGE and justified.

Core principles:

- Strict separation of Domain, Ports, and Adapters
- No hardware or OS dependencies within the Core
- Clear responsibilities between repositories

---

## 2. System Architecture Overview (Read-Only Reference)

The system uses the Ports & Adapters (Hexagonal Architecture) model:

```
┌─────────────────────────────────────────────────┐
│              Application Layer                  │  ← separates Projekt, später
│  (UI / CLI / Operator Interface)                │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│           PC-ALE  (Core / Domain)               │  ← AKTIVER ARBEITSBEREICH
│  ALEStateMachine · Modem · Protocol             │
│  FSK · FEC · FFT                                │
│  Nur PAL-Interfaces, keine Implementierungen    │
└─────────────────────────────────────────────────┘
                      │  (via PAL Interfaces)
                      ▼
┌─────────────────────────────────────────────────┐
│          PC-ALE-PAL  (Port Definitions)         │  ← Interface-Repo, keine Impl.
│  IAudioDriver · IRadio · ITimer · ILogger       │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│        Platform Repositories  (Adapters)        │  ← separates Projekt, später
│  PC-ALE-Win · PC-ALE-Linux · PC-ALE-SDR        │
│  WASAPI · ALSA · Hamlib · UHD                   │
└─────────────────────────────────────────────────┘
```

---

## 3. Rules for PC-ALE Core (Active Scope)

### 3.1 Was der Core DARF

- PAL-Interfaces ausschliesslich über Dependency Injection verwenden
- Domain-spezifische Logik enthalten:
  - `ALEStateMachine`
  - Modem / Protocol Stack
  - FSK / FFT / FEC Logic
  - `ToneGenerator`, `FFTDemodulator`, `SymbolDecoder`
- Abstrakte Interfaces definieren (in PC-ALE-PAL)
- PCM-Buffer-Daten empfangen und liefern (als `float*` oder `int16_t*`)

### 3.2 Was der Core NICHT DARF

- OS-APIs direkt aufrufen (ALSA, WASAPI, POSIX-Threads, Win32)
- Audio-Geräte öffnen oder schliessen
- Hardware-Pins, GPIO, serielle Ports ansprechen
- Konkrete PAL-Implementierungen referenzieren oder instanziieren
- Singleton-Pattern für Hardware-Zugriff
- `#ifdef _WIN32` oder plattformspezifische Kompilierungs-Guards
  (ausser in `src/platform/`-Verzeichnissen, die bereits existieren)

### 3.3 Grenze zur Aussenwelt: PCM-Buffer

Der Core endet an der PCM-Buffer-Grenze:

```
ALE State Machine
  → Protocol Layer
  → Modem Layer (ToneGenerator)
  → int16_t PCM-Buffer (OUTPUT — endet hier im Core)
  → [IAudioDriver — nicht im Core implementiert]

[IAudioDriver callback — kommt von aussen]
  → float PCM-Buffer (INPUT)
  → FFT Demodulator
  → Symbol Decoder
  → Protocol Layer
  → ALE State Machine
```

Der Core liefert und konsumiert PCM-Buffer. Wer den Buffer transportiert
(WASAPI, ALSA, Test-Fixture), ist dem Core gleichgültig.

---

## 4. Regeln für PAL (Referenz)

PAL ist ein reines Interface-Repository. Der Coding Agent schreibt KEINE
PAL-Implementierungen — er nutzt nur die Interfaces.

```cpp
// Korrekt im Core: Interface via DI empfangen
class ALE2GModem {
public:
    explicit ALE2GModem(ToneGenerator& tg) : tone_gen_(tg) {}
    // ToneGenerator ist Core-Logik, kein PAL-Interface
};

// Korrekt: IAudioDriver nur als Parameter
void run(pal::IAudioDriver& driver, ALE2GModem& modem);

// VERBOTEN im Core:
// WasapiAudioDriver driver;  ← konkrete Implementierung
// driver.initialize(...);    ← Hardware-Zugriff
```

---

## 5. Audio und Modem Flow (Vollreferenz)

### TX Path

```
ALEStateMachine::update()
  → transmit_callback(ALEWord)
  → transmit_ale_word(word24)
  → ALEFECCodec::encode(word24)      [Core: FEC]
  → ALE2GModem::transmit_word()      [Core: Symbol-Mapping]
  → ToneGenerator::generate_tone()   [Core: NCO]
  → int16_t PCM-Buffer
  → IAudioDriver::callback (tx)      [PAL-Grenze]
  → Platform Audio Driver            [Adapter — nicht im Core]
  → Hardware
```

### RX Path

```
Hardware
  → Platform Audio Driver            [Adapter — nicht im Core]
  → IAudioDriver::callback (rx)      [PAL-Grenze]
  → float PCM-Buffer
  → ALE2GModem::detect_symbol()      [Core: Goertzel/FFT]
  → ALE2GModem::symbols_to_bits()    [Core: Symbol-Mapping]
  → ALEFECCodec::decode()            [Core: FEC]
  → WordParser::parse_from_bits()    [Core: Protocol]
  → ALEStateMachine::process_received_word()
```

---

## 6. Verbotene Kopplungen

| Verboten | Begründung |
|---|---|
| Core → ALSA/WASAPI/Hamlib | OS-API im Domain-Layer |
| Core → Platform Repositories | Dependency-Inversion verletzt |
| Modem → IAudioDriver direkt | Modem operiert nur auf PCM-Buffer |
| State Machine → Hardware direkt | Mehrfach verletzt Separation |
| PAL → Hardware APIs | PAL ist nur Interface-Definition |
| Globale Singletons für Hardware | Verhindert Testbarkeit |

---

## 7. Dependency Injection Rule

Alle externen Abhängigkeiten MÜSSEN injiziert werden:

```cpp
// Korrekt:
ALE2GModem modem(tone_gen);               // ToneGenerator injiziert
ALEStateMachine sm;
sm.set_transmit_callback([&](auto& w) {}); // Callback injiziert

// VERBOTEN:
static WasapiAudioDriver g_driver;         // Singleton
```

---

## 8. Testbarkeit-Regel

Da der Core keine Hardware-Abhängigkeiten haben darf, MUSS er vollständig
ohne Audio-Hardware testbar sein:

```cpp
// Jeder Test läuft ohne WASAPI-Gerät:
// - ToneGenerator → PCM-Buffer → direkt in ALEFECCodec::decode()
// - Kein audio_driver.initialize() in Unit-Tests
// - Software-Loopback als Integrationstest
```

Alle Tests im `tests/`-Verzeichnis des Core MÜSSEN ohne Hardware laufen.
Tests die Hardware benötigen gehören in das Platform-Repository.

---

## 9. Erweiterungs-Regel

Neue Modem-Arten (ARDOP, 3G ALE) oder neue Hardware-Plattformen:

- MÜSSEN als neue Adapter implementiert werden
- DÜRFEN den Core oder PAL nicht verändern (ausser Interface-Ergänzungen)
- MÜSSEN vorhandene Interfaces implementieren

---

## 10. Anti-Patterns (Explizit Verboten)

| Anti-Pattern | Beispiel |
|---|---|
| Mock-Code im Production-Core | `if (testing_mode) return fake_value;` |
| Hardware-Zugriff im Modem | `modem.start_audio_device()` |
| DSP-Logik im Platform-Layer | FFT-Code in `audio_wasapi.cpp` |
| Audio-Callback-Logik in State Machine | `sm.on_audio_frame()` direkt an ISR |
| Plattform-Guards im Core | `#ifdef _WIN32` in `ale_state_machine.cpp` |

---

## 11. Priority bei Konflikten

1. Architecture Guardrail (dieses Dokument)
2. PAL Interface Definition
3. Domain Logic (REQUIREMENTS.md / FEATURES_DESIGN.md)
4. Platform Implementation

---

## 12. Review-Pflicht

Jede Änderung in folgenden Bereichen erfordert Abgleich mit diesem Dokument:

- `src/` (Core-Logik)
- `include/` (Core-Header)
- `extern/PC-ALE-PAL/` (PAL-Interfaces)
- Neue Abhängigkeiten in `CMakeLists.txt`
