# LQA-Austausch und Auto-Relink — Technische Dokumentation

## Überblick

PC-ALE implementiert zwei zusammenhängende Mechanismen aus MIL-STD-188-141B Appendix A:

1. **Bilateraler LQA-Austausch (KA1-Response, A.5.4.4):** Während des 3-Wege-Handshakes
   tauschen beide Stationen ihre lokalen Kanalmessungen via CMD-Wörter aus. Nach einem
   erfolgreichen Link haben dadurch beide Stationen **bidirektionale** Qualitätsdaten für
   den aktuell genutzten Kanal.

2. **Auto-Relink (A.5.4.5):** Nach dem Linken überwacht die Station, ob ein bekannter
   Kanal bilateral deutlich besser wäre als der aktive. Ist das der Fall, wird der Link
   ordnungsgemäß per TWAS beendet und sofort auf dem besseren Kanal neu aufgebaut.

Nicht implementiert: A.5.5 Frequency-Select-CMD (erfordert Split-VFO / Vollduplex-Betrieb;
für Simplexbetrieb bietet TWAS+Relink den gleichen Effekt ohne zusätzliche Hardware-Anforderungen).

---

## Teil 1: Bilateraler LQA-Austausch (KA1-Response)

### Spec-Hintergrund (A.5.4.4)

MIL-STD-188-141B definiert im CMD-LQA-Wort (Zeichenwert `'a'`, Tabelle A-XIV) ein
24-Bit-Datenfeld mit folgenden Unterfeldern:

| Feld | Bits | Bedeutung |
|------|------|-----------|
| `KA1` | 13 | 1 = Gegenstation soll ihren LQA zurückmelden |
| `SINAD` | 5 | Empfangs-SINAD-Code (0–30; 31 = kein Wert; höher = besser) |
| `BER` | 5 | Empfangs-BER-Code (0–30; 31 = kein Wert; **niedriger = besser**) |
| `MP` | 5 | Mehrwegausbreitung in ms (0–30; 31 = kein Wert) |

Beide Stationen messen unabhängig die Eingangsqualität (`FROM`-Richtung: was sie
selbst empfangen). Der CMD-Austausch liefert der Gegenseite die `TO`-Richtung
(was die Gegenseite von der eigenen Station empfängt).

### Ablauf im Handshake

```
SAM (Rufende Station)                     JOE (Gerufene Station)
──────────────────────                    ──────────────────────
initiate_call() →                         [scant Kanäle]
  KA1=1 in pending_lqa_cmd_ gesetzt
  (eigene SINAD/BER für ersten Kanal
  aus LQA-DB vorbelegt)

CALL:  …TO:JOE  CMD:'a'(KA1=1,SINAD,BER,MP)  TIS:SAM…
                                          →  Wort empfangen in on_received_word()
                                             pending_bilateral_payload_ gespeichert

                                          maybe_emit_call_alert() →
                                            update_bilateral(freq, SAM, SINAD, BER, MP)
                                            [JOEs LQA-DB: SAM bilateral befüllt]
                                            KA1=1 erkannt →
                                            LQA-Report-Wörter für SAM generiert +
                                            eigene SINAD/BER (KA1=0) in pending_lqa_cmd_

RESPONSE:  …FROM:JOE  CMD:'a'(KA1=0,SINAD,BER,MP)  [LQA-Report-Wörter]  TIS:JOE…
  Wort empfangen in on_received_word() →
  pending_bilateral_payload_ gespeichert

ACK:  …TO:JOE  TIS:SAM…
                                          LINKED

on_operator_event(LINK_ESTABLISHED) →
  update_bilateral(freq, JOE, SINAD, BER, MP)
  [SAMs LQA-DB: JOE bilateral befüllt]
LINKED
```

Nach dem Handshake haben **beide Stationen** bilaterale Daten für den aktuellen Kanal:
- SAMs DB: `bilateral_sinad`/`bilateral_ber` für JOE auf dieser Frequenz = was JOE
  von SAM empfangen hat (= TO-Richtung von SAMs Perspektive)
- JOEs DB: `bilateral_sinad`/`bilateral_ber` für SAM = was SAM von JOE empfangen hat

### Implementierung (Schlüsselstellen)

| Schritt | Funktion | Datei |
|---------|----------|-------|
| SAM kodiert KA1=1 | `initiate_call()` Block A4 | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| JOE empfängt CMD | `on_received_word()` → `pending_bilateral_payload_` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| JOE schreibt bilateral + antwortet | `maybe_emit_call_alert()` Block A5 + C5 TX | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| SAM empfängt JOEs CMD | `on_received_word()` → `pending_bilateral_payload_` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| SAM schreibt bilateral | `on_operator_event(LINK_ESTABLISHED)` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| Datenhaltung | `LQADatabase::update_bilateral()` | [lqa_database.cpp](../src/LQA/lqa_database.cpp) |

---

## Teil 2: Bilateraler Kanal-Score (A.5.4.5)

### Scoring-Formel

```
bilateral_channel_score(entry) = (FROM-Qualität + TO-Qualität) / 2
```

- **FROM-Qualität:** Lokal gemessene Eingangsqualität (Soundings, BER/SNR während LINKED).
  Fällt auf `entry.score` (Composite) zurück, wenn keine eigene SINAD-Messung vorhanden.
- **TO-Qualität:** Vom Peer via CMD 'a' gemeldete SINAD/BER für unsere Aussendung
  (`bilateral_sinad` Code in dB, höher = besser; `bilateral_ber` Code niedriger = besser).
  Der schwächere der beiden BER/SINAD-Werte begrenzt die TO-Qualität.

Der Durchschnitt beider Richtungen ergibt den bilateralen Score (Spec A.5.4.5.1: „Summe
der beiden LQA-Werte"; durch Division auf 0–30-Skala normiert, identisches Ranking).
Höher ist besser; bei gleichen Scores gewinnt der ausgewogenere Pfad (Tiebreaker).
Wenn `bilateral_sinad > 30` (kein Wert empfangen), wird der unilaterale Composite-Score
als Fallback verwendet (damit das Ranking auch ohne bilaterale Daten sinnvoll bleibt).

### Kanal-Ranking

```cpp
// rank_channels_for_station(peer) — aus lqa_analyzer.cpp
// Gibt Kanäle sortiert nach bilateral_channel_score() zurück (bester zuerst)
auto ranked = lqa_analyzer_.rank_channels_for_station(peer);
ranked[0].frequency_hz  // beste Frequenz
ranked[0].score         // bilateraler Score
```

Dieses Ranking nutzt `initiate_call()` bereits automatisch: Beim Anruf wird der Kanal
mit dem höchsten bilateralen Score zuerst versucht.

---

## Teil 3: Auto-Relink (evaluate_relink)

### Konzept

Nach dem Linken kann die rufende Station erkennen, dass inzwischen ein anderer Kanal
deutlich besser wäre — z.B. weil sich die Ausbreitungsbedingungen geändert haben oder
weil durch Soundings/vorherige Links neue bilaterale Daten für andere Frequenzen vorliegen.

Der Standard bietet dafür keinen eigenen Steuermechanismus im LINKED-State (A.5.5
Frequency-Select-CMD erfordert Split-VFO). Die spec-konforme Lösung ist TWAS + Neulink:

1. Aktiven Link per TWAS ordnungsgemäß beenden  
2. Sofort neuen Call auf dem jetzt besten Kanal initiieren  
3. JOE geht nach TWAS automatisch zurück in SCANNING und nimmt den neuen Call an

### Auslösebedingungen

`evaluate_relink()` wird in jedem `update()`-Tick aufgerufen, wenn:
- `relink_enabled = true` (Konfiguration)
- `lqa_enabled = true` (LQA-Aufzeichnung läuft)
- State-Machine ist im Zustand `LINKED`
- Kein Relink bereits in Bearbeitung (`pending_relink_addr_.empty()`)

Innerhalb von `evaluate_relink()` gelten zusätzlich:

```
Hysterese-Guard:  rx_ber_settle_ms_ > 0
                  UND (jetzt - rx_ber_settle_ms_) >= 4 × Tdrw  (≈ 640 ms)
```
`rx_ber_settle_ms_` wird gesetzt, sobald der erste BER-Messwert im LINKED-State stabil
ist. Das verhindert, dass ein Relink sofort nach dem Linking ausgelöst wird, bevor die
Qualitätsmessung des aktiven Kanals überhaupt stabilisiert ist.

### Relink-Entscheidungslogik

```
ranked = rank_channels_for_station(peer)   // bilaterales Ranking
best_freq  = ranked[0].frequency_hz
best_score = ranked[0].score

cur_score  = ranked[i].score  (i: Index des aktuell genutzten Kanals)

Auslösung wenn:
  best_freq != current_freq
  AND best_score > cur_score + relink_improvement_threshold
```

`relink_improvement_threshold` (Standard: 5.0 Punkte auf der 0–30-Skala) ist eine
Hysterese gegen "Channel-Thrashing": Der Wechsel lohnt sich nur, wenn die Verbesserung
deutlich und nicht nur marginal ist.

### Ablauf nach Auslösung

```
evaluate_relink() setzt pending_relink_addr_ = peer
evaluate_relink() ruft sm_.terminate_link() auf → SM sendet TWAS, geht → IDLE

nächster update()-Tick:
  pending_relink_addr_ nicht leer
  State == IDLE oder SCANNING
  → initiate_call(pending_relink_addr_)
     (nutzt rank_channels_for_station() → bester Kanal wird zuerst versucht)
  pending_relink_addr_.clear()

JOE empfängt neuen CALL, antwortet, 3-Wege-Handshake auf neuem Kanal
```

### Sequenzdiagramm

```
SAM (LINKED auf 14.250 MHz)             JOE (LINKED auf 14.250 MHz)
───────────────────────────             ────────────────────────────

[neues LQA-Sounding empfangen:
 7.100 MHz bilateral score 22 > 14.250 MHz score 12 + threshold 5]

evaluate_relink() →
  pending_relink_addr_ = "JOE"
  sm_.terminate_link()

SM → sendet TWAS ─────────────────────► JOE empfängt TWAS
SM → IDLE                               JOE → SCANNING

update():
  State==IDLE, pending_relink_addr_="JOE"
  → initiate_call("JOE")
    rank[0] = 7.100 MHz (score 22)

CALL auf 7.100 MHz ───────────────────► [JOE scant, empfängt CALL auf 7.100 MHz]

            ◄── 3-Wege-Handshake ──►

LINKED auf 7.100 MHz                    LINKED auf 7.100 MHz
(besserer Kanal)                        (besserer Kanal)
```

### Implementierung (Schlüsselstellen)

| Element | Datei / Stelle |
|---------|---------------|
| `evaluate_relink()` | [ale_controller.cpp:941](../src/App/ale_controller.cpp) |
| Aufruf-Hook in `update()` | [ale_controller.cpp:742](../src/App/ale_controller.cpp) |
| Relink-Execute-Hook in `update()` | [ale_controller.cpp:750](../src/App/ale_controller.cpp) |
| `pending_relink_addr_` Member | [ale_controller.h](../include/App/ale_controller.h) |
| `relink_enabled` / `relink_improvement_threshold` | [ale_station_config.h:74](../include/App/ale_station_config.h) |

---

## Konfiguration und Persistenz

### Konfigurationsfelder (ALEStationConfig)

```cpp
// include/App/ale_station_config.h
bool  relink_enabled              = false;  // Auto-Relink ein/aus
float relink_improvement_threshold = 5.0f;  // Mindest-Score-Verbesserung (0–30)
```

### Settings-Datei (export_settings / import_settings)

```ini
relink_enabled=1
relink_improvement_threshold=5.000000
```

### WebSocket-Bridge API (ale_bridge.cpp)

**Relink-Einstellungen setzen:**
```json
→ { "cmd": "RELINK_SET", "relink_enabled": true, "relink_threshold": 5.0 }
← { "ok": true }
```

**Relink-Einstellungen abfragen:**
```json
→ { "cmd": "RELINK_GET" }
← { "ok": true, "relink_enabled": true, "relink_threshold": 5.0 }
```

### GUI (apps/gui/)

Im LQA-Tab der Einstellungen:

- **Auto-Relink** Toggle (Checkbox `cfgAutoRelink`) — schaltet Relink ein/aus
- **Threshold** Zahlenfeld (`cfgRelinkThreshold`, 1–30 Punkte) — Mindestverbesserung
- Änderungen werden sofort via `applyRelinkToBridge()` → `RELINK_SET` gepusht
- Beim Connect wird der Core-Zustand via `syncRelinkFromBridge()` → `RELINK_GET` eingelesen

---

## Schwellwert-Empfehlungen

| Szenario | Threshold |
|----------|-----------|
| Aggressive Optimierung (viele Kanäle, stabile Bedingungen) | 2–3 Punkte |
| Standard (Ausgewogen) | 5 Punkte (Default) |
| Stabil, wenig Unterbrechungen | 8–10 Punkte |
| Relink nur bei großen Qualitätsdifferenzen | 15+ Punkte |

**Hinweis:** Jeder Relink unterbricht die laufende Kommunikation kurz (TWAS + neuer
3-Wege-Handshake, ca. 3–10 Sekunden je nach Scanning-Konfiguration). Zu kleine
Schwellwerte können bei instabilen Ausbreitungsbedingungen zu "Channel-Thrashing" führen.

---

## Einschränkungen und Randbedingungen

- **Nur rufende Station initiiert:** Nur SAM (die ursprünglich rufende Station) führt
  `evaluate_relink()` aus. JOE sendet kein eigenes TWAS für Relink. Für bidirektionales
  Relink müsste die Funktion auf beiden Seiten aktiv sein — was allerdings zu Race
  Conditions führen könnte (beide senden gleichzeitig TWAS).

- **LQA-Daten müssen vorhanden sein:** Ohne vorherige Soundings oder Links zu anderen
  Frequenzen ist `rank_channels_for_station()` leer und Relink wird nicht ausgelöst.
  Auto-Relink ist deshalb nur sinnvoll, wenn die Station aktiv Soundings empfängt und
  LQA aufzeichnet.

- **Relink-Richtung:** Der neue Kanal wird durch das LQA-Ranking zum Zeitpunkt des
  Relinks bestimmt. Wenn inzwischen neue Soundings eingegangen sind, kann das ein
  anderer Kanal sein als der, der den Relink ausgelöst hat.

---

## Teil 4: Enhanced Frequency-Select (CMD 'f', A.5.6.3.2)

### Konzept

Statt unilateralem TWAS+Relink (Auto-Relink) kann die vorschlagende Station einen
besseren Kanal bilateral mit der Gegenstation aushandeln, bevor TWAS gesendet wird.
Das Protokoll nutzt den Standard-CMD 'f' (TABLE A-XVI = Frequency-Select) als
Post-Link-Orderwire.

**Abwärtskompatibilität:** Standard-ALE-2G-Stationen ignorieren CMD 'f' im LINKED-State
per A.5.6.3.2d — der Vorschlag läuft in einen Timeout und der Link bleibt aktiv.

### CMD 'f' Wortformat (A.5.6.3.2)

**Word 1 — CMD 'f' (21-bit Payload):**

| Bits    | Inhalt |
|---------|--------|
| [20:14] | `'f'` = 1100110 (Frequency-Select CMD-Code) |
| [13:8]  | Control = 000000 (absolut) |
| [7:4]   | 100-Hz-BCD = 0 |
| [3:0]   | 10-Hz-BCD = 0 |

**Word 2 — DATA (21-bit Payload) — BCD-Frequenzbezeichner:**

| Bits    | Inhalt |
|---------|--------|
| [20]    | 0 (W4 = 0) |
| [19:16] | 10-MHz-BCD |
| [15:12] | 1-MHz-BCD |
| [11:8]  | 100-kHz-BCD |
| [7:4]   | 10-kHz-BCD |
| [3:0]   | 1-kHz-BCD |

DATA-Payload = 0 (alle BCD-Felder null) = **Reject-Sentinel** (keine reale Frequenz
beginnt mit 0 MHz und 0 kHz auf jedem Feld).

### Ablauf

```
Station A (Proposer, LINKED)       Station B (Responder, LINKED)
──────────────────────────         ──────────────────────────────
evaluate_freq_proposal() →
  besserer Kanal erkannt
  fs_phase_ = PROPOSED
  send_freq_select_orderwire(freq)

CMD 'f' + DATA(freq) + TIS:SAM ──► on_received_word(): CMD 'f' + DATA erfasst
                                    handle_freq_select_proposal(freq)
                                    LQA bewertet: score besser als Threshold?

                  ACCEPT:           send_freq_select_orderwire(freq)   [Echo]
   ◄── CMD 'f' + DATA(freq) + TIS:JOE ──
   handle_freq_select_response(freq)
   fs_phase_ = EXECUTING
   pending_relink_addr_ = peer
   sm_.terminate_link() → TWAS

                  REJECT:           send_freq_select_orderwire(0)       [freq=0]
   ◄── CMD 'f' + DATA(0) + TIS:JOE ──
   handle_freq_select_response(0)
   fs_phase_ = IDLE
   fs_cooldown_ms_ = now + 60s
   [Link bleibt aktiv]

                  TIMEOUT (Standard-Station):
   [kein DATA-Follow nach 3s]
   fs_phase_ = IDLE
   [Link bleibt aktiv]
```

### CMD-Zeichencode-Erkennung (Spec-Bugfix)

CMD-Zeichencodes wie `'f'` (0x66), `'a'` (0x61), `'n'` (0x6E) liegen im b7b6="11"-Bereich
(0x60–0x7F) des 7-Bit-ASCII und **fallen sowohl aus Basic38 als auch aus Expanded64 heraus**.
Der bestehende Decoder setzt daher `word.address[0]='?'` und `word.valid=false` für empfangene
CMD-Wörter über Funk.

**Fix:** `cmd_char_code(word)` in `ale_freq_select.h` liest den CMD-Zeichencode direkt aus
`word.raw_payload >> 14` und umgeht die fehlerhafte Zeichensatz-Validierung.
Dieser Ansatz wird konsequent für alle CMD-Erkennungen in `on_received_word()` angewandt
(`'a'`, `'n'`, `'r'`, `'f'`).

### Kollisions-Auflösung

Wenn beide Stationen gleichzeitig einen Vorschlag senden:
- Lexikografisch niedrigere Selbst-Adresse **weicht aus** (eigenen Vorschlag verwerfen)
- Lexikografisch höhere Selbst-Adresse **hat Priorität**

### Konfiguration

```cpp
// include/App/ale_station_config.h
bool enhanced_freq_select = false;  // CMD 'f' Aushandlung ein/aus
```

Settings-Datei:
```ini
enhanced_freq_select=1
```

WebSocket-Bridge:
```json
→ { "cmd": "FREQ_SELECT_SET", "enhanced_freq_select": true }
← { "ok": true }

→ { "cmd": "FREQ_SELECT_GET" }
← { "ok": true, "enhanced_freq_select": true }
```

### Abwärtskompatibilitäts-Matrix

| Szenario | Verhalten |
|----------|-----------|
| Beide Standard-ALE-2G | Kein CMD 'f' im LINKED-State; TWAS+Relink wenn Auto-Relink=ON |
| SAM Enhanced, JOE Standard | CMD 'f' an JOE → per A.5.6.3.2d ignoriert → Timeout → Link bleibt |
| Beide Enhanced | CMD 'f' + DATA Verhandlung → koordiniertes TWAS+Relink oder Ablehnung |
| Enhanced deaktiviert | `evaluate_relink()` wie bisher — keine Änderung |
| CALLING/HANDSHAKE/SOUNDING | Vollständig unverändert |

### Implementierung (Schlüsselstellen)

| Element | Datei / Stelle |
|---------|---------------|
| BCD-Encoding/Decoding | [ale_freq_select.h](../include/Protocol/Control/ale_freq_select.h) |
| `evaluate_freq_proposal()` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| `handle_freq_select_response()` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| `handle_freq_select_proposal()` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| `trigger_linked_orderwire()` | [ale_state_machine.cpp](../src/Protocol/Control/ale_state_machine.cpp) |
| `cmd_char_code()` helper | [ale_freq_select.h](../include/Protocol/Control/ale_freq_select.h) |
| `enhanced_freq_select` config | [ale_station_config.h](../include/App/ale_station_config.h) |
