# Channel-, Net- und Contact-Verwaltung

Channels steuern, auf welcher Frequenz (und in welchem Modus) openALE sendet und empfängt.
Ohne konfigurierte Channels bleibt das Radio auf der Frequenz, auf der es vor dem Start war.
Nets gruppieren Channel-IDs, um beim Anruf einer Gegenstation automatisch die passende
Scanning-Call-Länge zu bestimmen; Contacts verknüpfen ein Adressbuch-Callsign mit einem Net.

## Verwaltung über die WebSocket-Bridge (JSON)

Diese Kommandos treffen auf `ALEController` (`apps/ale_bridge.cpp`); die GUI (Desktop + Mobile)
nutzt exakt dieselbe API.

**Channels:**

| Kommando | Beschreibung |
|----------|-------------|
| `CHANNELS_LIST` | Aktuelle Channel-Liste abfragen |
| `CHANNEL_ADD` | Channel hinzufügen/überschreiben — Felder: `rx_hz`, `tx_hz`, `mode`, `id`, `label`, `enabled`, `rx_only`, `tx_only`, `voice_use`, `data_use`, `inhibit_calling`, `inhibit_sounding`, `inhibit_reporting`, `ale_only` |
| `CHANNEL_DEL` | Channel per `rx_hz` entfernen |
| `CHANNEL_RENAME` | Channel-ID umbenennen (`old_id`, `new_id`) |
| `STATION_LOAD` / `CHANNELS_LOAD` | Station-Datei laden (`path`) |
| `STATION_SAVE` / `CHANNELS_SAVE` | Station-Datei schreiben (`path`) |

**Nets:**

| Kommando | Beschreibung |
|----------|-------------|
| `NETS_LIST` | Alle Nets mit zugewiesenen Channels abfragen |
| `NET_ADD` | Net hinzufügen (`name`) |
| `NET_DEL` | Net entfernen (`name`) |
| `NET_ASSIGN` | Channel-ID einem Net zuweisen (`net`, `channel_id`) |
| `NET_UNASSIGN` | Channel-ID aus einem Net entfernen (`net`, `channel_id`) |
| `NET_UPDATE` | Net-Policy setzen: `name`, `dwell_ms`, `scanning_enabled`, `sounding_enabled`, `sounding_interval_sec`, `calling_length_c` |
| `SCAN_NET_SET` / `SCAN_NET_GET` | Aktives Scan-Net setzen/abfragen |

**Contacts (Adressbuch):**

| Kommando | Beschreibung |
|----------|-------------|
| `CONTACTS_LIST` | Adressbuch abfragen |
| `CONTACT_ADD` | Kontakt hinzufügen (`callsign`, `name`) |
| `CONTACT_DEL` | Kontakt entfernen (`callsign`) |
| `CONTACT_SELECT` | Kontakt als aktives Call-Ziel auswählen (`callsign`) |

Ein Net bestimmt die Scanning-Call-Länge beim Anruf einer Gegenstation:
`Tsc = C × 2 × Trw`, wobei `C` (`calling_length_c`) die Anzahl der **scan-fähigen**
Channels des Nets ist, dem die Gegenstation laut Adressbuch angehört. Ist die Gegenstation
nicht im Adressbuch oder keinem Net zugeordnet, bleibt die zuvor konfigurierte
Scanning-Call-Länge (Standard: 1 Channel) unverändert.

## Non-Volatile-Speicherung (`.ale`-Dateiformat)

Channels, Nets und Contacts werden gemeinsam in einer Textdatei mit der Endung `.ale`
gespeichert (siehe `save_station_file()` / `load_station_file()` in
[ale_controller.cpp](../src/App/ale_controller.cpp)) — eine Zeile pro Eintrag, Kommentare
beginnen mit `#`.

### Channel-Zeile

```
[ID:id] rx_hz tx_hz mode [flags] [label]
```

| Feld | Beschreibung |
|------|-------------|
| `ID:id` | Channel-ID (optional beim Laden — alte Dateien ohne ID bekommen automatisch fortlaufende `C-<n>`-IDs; wird beim Speichern immer geschrieben; ändert sich nicht bei Frequenzänderung) |
| `rx_hz` | RX-Frequenz in Hz (Pflicht), oder `rx_hz:tx_hz` (Kurzform) |
| `tx_hz` | TX-Frequenz in Hz (0 oder fehlt = Simplexbetrieb) |
| `mode` | Modulationsart: `USB` (Standard), `LSB`, `AM`, `FM`, `FMW`, `CWU`, `CWL`, `FSK`, `DATA_USB`, … |
| `[flags]` | optionales, kommasepariertes Flag-Token direkt nach `mode` (nur geschrieben, wenn ein Flag vom Default abweicht) |
| `label` | Freitext-Label (optional, beliebig lang) |

Flag-Codes:

| Code | Bedeutung |
|------|-----------|
| `OFF` | `enabled=false` (Kanal deaktiviert) |
| `RX`  | `rx_only` (nur Empfang, kein TX) |
| `TX`  | `tx_only` (nur Senden, kein RX) |
| `IC`  | `inhibit_calling` |
| `IS`  | `inhibit_sounding` |
| `IR`  | `inhibit_reporting` (bilateraler LQA-CMD-'a'-Austausch) |
| `AO`  | `ale_only` — kurzes LBT-Fenster nach A.5.4.7.1 (784 ms statt 2 s) |

**Beispiel:**

```
# openALE station file — MIL-STD-188-141B
# ID:id rx_hz tx_hz mode [flags] [label]   flags=[OFF,RX,IC,IS,IR]
ID:C-1 14250000 0 USB 40m-Calling
ID:C-2 7100000 0 USB [IC] 40m-Backup
ID:C-3 3500000 3600000 LSB [IS,IR,RX] 80m-DX
ID:C-4 14074000 0 DATA_USB FT8
```

### Net-Zeile

```
NET:name id,id,...  dwell=ms scan=0|1 sound=0|1 sndint=sec c=N
```

```
# NET:name id,id,...  [dwell=ms] [scan=0|1] [sound=0|1] [sndint=sec] [c=N]
NET:XYZ C-1,C-3 dwell=200 scan=1 sound=0 sndint=300 c=2
```

| Feld | Beschreibung |
|------|-------------|
| `dwell` | Scan-Dwell-Zeit pro Channel in ms |
| `scan` | Scanning für dieses Net aktiviert |
| `sound` | Auto-Sounding für dieses Net aktiviert |
| `sndint` | Sounding-Intervall in Sekunden |
| `c` | `calling_length_c` — Anzahl scan-fähiger Channels für die Scanning-Call-Länge |

### Contact-Zeile

```
CONTACT:callsign|name|status|net_members|valid_channels
```

```
# CONTACT:callsign|name|status|net_members|valid_channels
CONTACT:BOB|Bob K1ABC|enabled|XYZ|ALL
```

`status` ist `enabled`/`disabled`; `net_members` eine kommaseparierte Liste von Net-Namen;
`valid_channels` entweder `ALL` oder eine kommaseparierte Liste von Channel-IDs.

### Laden/Speichern

Beim `STATION_LOAD`/`STATION_SAVE`-Kommando (bzw. beim Start über den GUI-Onboarding-Flow)
werden Channels, Nets und Contacts vollständig wiederhergestellt bzw. geschrieben. Änderungen
über `CHANNEL_ADD`/`CHANNEL_DEL`/`NET_*`/`CONTACT_*` werden nicht automatisch in eine Datei
zurückgeschrieben — dafür ist ein explizites `STATION_SAVE` nötig.
