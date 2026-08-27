# Konzept: Verteilung empfangener GPS-Positionen an eine Web-API (Location Relay)

> Status: **Konzept / Architektur-Entscheidung** — keine Implementierung.
> Vorgänger: `docs/ALE_GPR_SPEC.md` (ALE-GPR Parse/Generate, implementiert & verifiziert 2026-08-20).
> Dieses Dokument beschreibt den *nächsten* Schritt: empfangene GPR-Positionen
> an einen zentralen Web-Service weiterleiten. Die Web-API und das Karten-Frontend
> werden hier *nicht* implementiert, nur spezifiziert und abgegrenzt.

---

## 0. TL;DR

- openALE wird um einen **Location Relay Client** erweitert: ein optionaler,
  opt-in, asynchroner Dienst, der **empfangene** ALE-GPR-Positionen an einen
  konfigurierten HTTPS-Endpunkt POSTet.
- Der Dienst wird als eigenständiger Daemon-Thread realisiert — exakt nach dem
  Vorbild von `SfiService` (`include/App/sfi_service.h`). Er berührt den
  ALE-Tick-Loop / Audio / Radio-Control **nie synchron**.
- Einzige notwendige Core-Änderung am RX-Pfad: `AmdData` erhält einen
  `call_context` (ALLCALL/INDIVIDUAL/NET/GROUP/LINKED), damit der Client die
  Shareability-Entscheidung treffen kann. Heute fehlt diese Information im
  `ALE_AMD_RECEIVED`-Event.
- Dedup-Anchor ist das GPR-**TIME-Feld** (Positionszeitstempel) je `source`,
  nicht der Empfangszeitpunkt.
- `source` = GPR-OBJECT (autoritativ, kann vom AMD-Sender abweichen — Relay-Fall),
  `observer` = eigene Self-Adresse, `relay` = AMD-Sender falls ≠ OBJECT.
- Die Web-API, Storage, Dedup-Serverseite, Last-Seen-TTL und Karten-Frontend
  liegen **außerhalb** des openALE-Core.

---

## 1. Bestandsaufnahme — was heute schon existiert

| Aspekt | Stand heute | file:line |
|---|---|---|
| GPR Parse/Generate | implementiert, verifiziert | `include/Protocol/Message/ale_gpr.h`, `src/Protocol/Message/ale_gpr.cpp` |
| GPR-Erkennung auf RX | **ja** — der Bridge-Handler parst bereits | `apps/ale_bridge.cpp:1654` (`is_gpr`/`parse_gpr`) |
| RX-Event für AMD | `ALE_AMD_RECEIVED` mit `AmdData{self_addr, peer_addr, text}` | `include/PAL/events.h:50`, `include/App/ale_event_data.h:30` |
| Dispatch-Stelle RX | `ALEController::amd_dispatch(peer, text)` | `src/App/ale_controller.cpp:2952` |
| Call-Typ auf Frame-Ebene | `FrameData{call_type, from_addr, to_addrs}` via `ALE_FRAME_DECODED` | `include/App/ale_event_data.h:48`, `src/App/ale_controller.cpp:496` |
| ALLCALL-Erkennung (RX) | `CallProcessor::Type::ALLCALL` | `include/Protocol/Control/ale_call_processor.h:45` |
| `CallType`-Enum | INDIVIDUAL, NET, GROUP, ALL_CALL, SOUNDING, AMD, *_ACK, UNKNOWN | `include/Protocol/ale_message.h:23` |
| Eigene Position (TX) | `broadcast_position_report()` / `GPR_BUILD` | `src/App/ale_controller.cpp:1850`, `apps/ale_bridge.cpp:672` |
| Known-Positions-Cache (GUI) | `gprPositions`, keyed by GPR-OBJECT (relay-safe) | `apps/gui/app.js:3522` |
| Async-HTTP-Vorbild | `SfiService` (Daemon-Thread, atomic, PendingUpdate) | `include/App/sfi_service.h`, `src/App/sfi_service.cpp` |
| HTTP-Client Win (TLS) | WinHTTP GET (`WINHTTP_FLAG_SECURE`) | `src/App/sfi_service.cpp:11` |
| HTTP-Client Linux | Raw-Socket HTTP/1.0, **kein TLS** (Port 80) | `src/App/sfi_service.cpp:80` |
| Config/Persistenz | `ALEStationConfig` + `import/export_settings` (key=value) | `include/App/ale_station_config.h`, `src/App/ale_controller.cpp:3830` |
| GUI-Settings-Pattern | `X_GET`/`X_SET`-Bridge-Kommandos, `applyXToBridge()`, Card-Markup | `apps/gui/app.js:3471`, `apps/gui/index.html:1032` |
| Service-Start/Stop bei Config-Änderung | `restart_location_services()` | `apps/ale_bridge.cpp:393` |

**Zentrale Lücke:** `AmdData` enthält `self_addr`/`peer_addr`/`text`, aber
**keinen Call-Typ**. Ein Downstream-Klient kann heute nicht unterscheiden, ob ein
empfangenes GPR via ALLCALL, Individual-Call, NETCALL oder über eine bestehende
Verbindung (LINKED) ankam. Das ist genau die Information, die das
Privacy-Gating („nur ALLCALL teilen") braucht.

---

## 2. Proposed Data Flow

```
                         Funk (ALLCALL / Individual / NET / linked AMD)
                                          │
                                          ▼
   RX-Pipeline → SM → MessageAssembler ──────────────► ALE_FRAME_DECODED
   (ale_controller.cpp:496)                    (FrameData: call_type, to_addrs)
                                          │
   AMD-Assembly → amd_dispatch(peer, text) │  (call context hier bekannt)
   (ale_controller.cpp:2952)               │
                                          ▼
                          ALE_AMD_RECEIVED  (AmdData + neu: call_context)  ◄── Core-Änderung
                                          │
                ┌─────────────────────────┼──────────────────────────┐
                ▼                          ▼                          ▼
        GUI-Bridge-Handler         LocationRelayService         (weitere Subscriber)
        (schon vorhanden:          (NEU — opt-in, async)         z.B. ale_monitor
         parse + gprPositions)              │
                                             ▼
                              Shareability-Gate (Config):
                              - master enabled?
                              - call_context erlaubt? (ALLCALL/INDIV/NET/LINKED)
                              - source != self?  (nie eigene TX weiterleiten)
                                             │  ✓
                                             ▼
                              Dedup + Throttle (per source + GPR-TIME)
                                             │  ✓
                                             ▼
                              Bounded Queue (z.B. 64, drop oldest)
                                             │
                              ─── Worker-Thread (nie ALEController) ───
                                             ▼
                              HTTPS POST /api/v1/locations   (WinHTTP / TLS)
                                             │
                              ┌──────────────┴──────────────┐
                              ▼ 2xx                         ▼ 5xx / timeout
                           acked, drop                 Backoff, retry; dann discard
                              │                             │
                              └──────────► PendingUpdate ◄──┘
                                          (loc_dirty + status text)
                                             │
                              Bridge-Main-Loop drain → ALE-Log + WS-Event
                              (KEIN Blockieren von Tick/Audio/Radio)
```

---

## 3. Component Boundaries

```
openALE-Core (dieses Konzept)
├── ALEController            — AMD-RX-Dispatch, call_context an AmdData anhängen
├── LocationRelayService     — NEU: opt-in Async-Client (Daemon-Thread, Queue, POST)
├── HttpPoster               — NEU: dünne HTTP-POST-Abstraktion (WinHTTP | OpenSSL/libcurl)
├── ALEStationConfig         — NEU: LocationSharingConfig-Felder
├── PAL Event Bus            — nur Consumer (ALE_AMD_RECEIVED); kein neuer EventType nötig
└── ale_bridge + GUI         — NEU: Privacy/Network-Settings-Card, GET/SET, Service-Start/Stop

Externer Web-Service (NICHT Teil dieses Konzepts, nur spezifiziert)
├── Location Web API         — Auth, Storage, Server-Dedup, Last-Seen, Map-Data-API
└── Web Frontend             — Karten-Rendering (Leaflet/OSM oder Google Maps), Marker, Details
```

**Regel:** openALE enthält keine Kartenlogik, keine Storage, keine Server-Dedup,
kein Last-Seen-TTL. openALE liefert nur: GPR erkennen → shreden → privacy-gaten →
dedupen → asynchron POSTen → Status loggen.

---

## 4. Wo werden eingehende GPRs erkannt? (Frage 1)

Heute: in `ALEController::amd_dispatch()` (ale_controller.cpp:2952) wird das
fertige AMD-Text per `ALE_AMD_RECEIVED` emittiert. Der **GPR-Parse** passiert
aktuell nur downstream im GUI-Bridge-Handler (ale_bridge.cpp:1654), nicht im
Core. Für das Relay muss der GPR-Parse in den Relay-Client wandern (oder dort
neu erfolgen) — der Core selbst bleibt GPR-agnostisch, ausgenommen das
Anhängen des `call_context`.

Empfehlung: der `LocationRelayService` abonniert `ALE_AMD_RECEIVED`, ruft
`ale::is_gpr(d->text)` und bei Treffer `ale::parse_gpr(d->text)` selbst auf —
genau wie der Bridge-Handler es heute tut. Damit bleibt der Core unverändert
bis auf das `call_context`-Feld (Abschnitt 8).

---

## 5. Wo wird über Shareability entschieden? (Frage 2)

Im `LocationRelayService`, *nicht* im Core. Der Service hält eine Referenz auf
die aktuelle `ALEStationConfig` (bzw. einen kleinen snapshot der relevanten
Felder) und wendet das Gate an:

1. `location_sharing_enabled` == true? (Master-Opt-In, default OFF)
2. `call_context` in der erlaubten Menge? (allcall/individual/net/linked-Toggles)
3. `source` (GPR-OBJECT) != eigene Self-Adresse? (nie eigene Position relayen)
4. `valid_position` oder mindestens `valid_gpr_structure`? (siehe Abschnitt 11)

Das Gate ist eine gekapselte Funktion `bool is_shareable(const AleGpr&, const AmdData&, const LocationSharingConfig&)`,
einzeln testbar, konfigurationsgetrieben.

---

## 6. Eigene / empfangene / weitergeleitete Position (Frage 3)

| Begriff | Identität | Quelle |
|---|---|---|
| **Eigene Position** | Self-Adresse (Observer) | `GpsService` / `config.station_lat_deg` — geht via TX als GPR über Funk raus. Wird **nicht** an die API relayet (die API empfängt Third-Party-Positionen). |
| **Empfangene Position (direkt)** | `source` = GPR-OBJECT = AMD-Sender (`peer_addr`) | `ALE_AMD_RECEIVED` |
| **Weitergeleitete Position (Relay)** | `source` = GPR-OBJECT ≠ AMD-Sender; `relay` = AMD-Sender | `ALE_AMD_RECEIVED` — die GUI keyed `gprPositions` bereits nach OBJECT (app.js:3522-Kommentar). |

Der API-Payload enthält daher **immer** `observer` = Self-Adresse, `source` =
GPR-OBJECT, und optional `relay` = `peer_addr` (nur wenn ≠ OBJECT). Damit kann der
Server „KQ6XA gehört von DF3SR, relayed von DL3ABC" korrekt abbilden.

**Wichtig:** die eigene, gesendete GPR wird bewusst **nicht** an die eigene API
gemeldet — das verhindert den „forward everything I TX"-Missbrauchsfall und
Doppeleinträge. Die eigene Position taucht auf der Karte über *andere* Observer
auf, die sie hören.

---

## 7. ALLCALL-Erkennung und Call-Typ-Transport (Frage 4)

Heute fehlt der Call-Typ im `ALE_AMD_RECEIVED`-Event. Zwei_saubere Optionen:

- **(A) `AmdData` um `call_context` erweitern (empfohlen).** In `amd_dispatch()`
  wird aus dem aktuellen SM-/Call-Processor-Zustand ein kompakter String
  abgeleitet: `"ALLCALL" | "INDIVIDUAL" | "NET" | "GROUP" | "LINKED"`. Für den
  Calling-Frame-Pfad liegt die Klassifikation bereits vor
  (`CallProcessor::Type::ALLCALL`); für den Linked-Pfad ist es `"LINKED"`.
  Das Feld ist ein `const char*` (wie die anderen AmdData-Felder), gültig für
  die Callback-Dauer.
- (B) Serverseitige Korrelation `ALE_FRAME_DECODED` ↔ `ALE_AMD_RECEIVED` über
  `frame_id`. Abgelehnt: `AmdData` hat keine `frame_id`, beide Events feuern aus
  unterschiedlichen Pfaden/Zeitpunkten — fragil.

Der Call-Typ wird im API-Payload als `call_type` übertragen (Abschnitt 9). Damit
ist die „nur ALLCALL teilen"-Privacy-Option ein reiner Client-Filter.

---

## 8. Interne Datenstruktur: LocationReport (Frage 5)

```cpp
// include/App/location_relay_service.h (neu)
namespace ale {

/// Ein shredbarer, konfigurierter Report — erzeugt vom Gate, konsumiert vom Worker.
struct LocationReport {
    // Identitäten
    std::string observer;        // Self-Adresse zum Empfangszeitpunkt
    std::string source;          // GPR-OBJECT (autoritativ)
    std::string relay;           // AMD-Sender, nur gesetzt wenn != source
    std::string source_type;     // "ale_gpr"  (später evtl. "nmea_gga")

    // Original + strukturiert (Spec §20.5)
    std::string raw_gpr;         // unverändert, wie empfangen
    bool   has_position = false;
    double lat = 0.0, lon = 0.0; // ggf. gerundet (Privacy)
    bool   has_altitude = false; double altitude = 0.0; char altitude_unit = 0;
    bool   has_timestamp = false; std::time_t timestamp_utc = 0;
    std::string comment;         // ggf. geleert (Privacy: include_comment=false)

    // Kontext
    std::string call_context;    // ALLCALL | INDIVIDUAL | NET | GROUP | LINKED
    std::time_t received_at;      // Empfangszeit (UTC), vom Event

    // Dedup-Anchor
    std::string dedup_key;        // source + "|" + gpr-timestamp + "|" + rounded latlon
};

} // namespace ale
```

Der `dedup_key` wird schon beim Enqueue berechnet, sodass der Worker nur noch
„key bereits gesendet?" prüfen muss.

---

## 9. API-Schnittstelle (Frage 7)

```http
POST /api/v1/locations
Content-Type: application/json
Authorization: Ed25519 <callsign>
X-Timestamp: <ISO8601>
X-Signature: <base64 signature over "<timestamp>\n<raw JSON body>">
```

```json
{
  "observer": "DF3SR",
  "source": "KQ6XA",
  "relay": "",
  "source_type": "ale_gpr",
  "raw_gpr": "GPR*KQ6XA*37N654321*122W987654*000003M*20260820Z135235*EVERYTHING FINE",
  "latitude": 37.654321,
  "longitude": -122.987654,
  "altitude": 3,
  "altitude_unit": "M",
  "timestamp": "2026-08-20T13:52:35Z",
  "received_at": "2026-08-20T13:52:36Z",
  "call_type": "ALLCALL"
}
```

- `raw_gpr` ist **verbatim** wie empfangen (Spec §20.5). Der Server kann
  normalisieren oder neu parsen.
- Strukturierte Felder sind optional (null), wenn `valid_position`/`valid_timestamp`
  false — siehe Abschnitt 11.
- Versioning via Pfad `/api/v1/`.
- Idempotenz-Empfehlung: Client sendet `dedup_key` als `Idempotency-Key`-Header,
  Server dedupliziert zusätzlich über `(source, timestamp, observer)`.

### Antwort-Codes (Frage 9, 13)

| Code | Bedeutung | Client-Aktion |
|---|---|---|
| 2xx | akzeptiert | Report aus Queue entfernen, Log „accepted (200)" |
| 409 | Server-Duplikat | aus Queue entfernen, still (kein Retry) |
| 401/403 | Auth-Problem | aus Queue entfernen, WARN loggen, **kein** Endlos-Retry (Signatur ungültig, Callsign unbekannt/pending/revoked, oder observer-Mismatch) |
| 422 | malformed | aus Queue entfernen, WARN loggen (Client-Bug) |
| 429 | rate-limit | Backoff, `Retry-After` honorieren |
| 5xx / timeout | Server/Netz | Backoff-Retry, nach max. Versuchen discard |

**Blockierfreiheit (Spec §20.13):** Der Worker-Thread ist der einzige, der HTTP
macht. ALE-Tick, Audio, Radio-Control laufen im Main-Thread / separaten Threads
und berühren den Worker nie. Eine totgegangene API hat **keinen** Effekt auf
Handshake, Audio, CAT. Im schlimmsten Fall wächst die Queue bis zum Bound und
verwirft dann älteste Reports (best-effort).

---

## 10. Authentifizierung (Frage 8)

- **Per-Callsign Ed25519-Signierung**, nicht ein geteiltes Bearer-Token: jede
  openALE-Instanz erzeugt lokal ein Ed25519-Schlüsselpaar (nur der 32-Byte-Seed
  wird persistiert, in `location_relay_identity.key`, nicht in `station.state`)
  und signiert jeden Request als `<timestamp>\n<raw JSON body>`.
- **Registrierung**: `POST /api/v1/register {callsign, public_key}` landet
  `pending`; ein Operator genehmigt manuell über `admin-cli.js approve
  <callsign>` auf dem Server (kein Netzwerk-Admin-Endpoint — direkter
  Dateisystemzugriff ist die Vertrauensgrenze).
- **Server-Verifikation**: Node's eingebautes `crypto.verify('Ed25519', ...)`
  gegen den gespeicherten Public Key; verwirft veraltete Timestamps (>300s
  Skew) und wiederholte `(callsign, signature)`-Paare (Replay).
- **Autorisierungs-Fix**: der Server erzwingt `body.observer === <callsign aus
  Header>` — das eigentliche Fix für die Spoofing-Lücke des alten
  Shared-Token-Modells (ein Token-Inhaber konnte für jede beliebige Station
  senden). `source` bleibt frei/vouched-for, da ein Relay per Definition
  fremde Positionen weiterleitet.
- **HTTPS only**: Client lehnt `http://` ab, Ausnahme `http://127.0.0.1`/`localhost`
  (für lokalen Test-Server).
- Der private Schlüssel/Seed wird **niemals** übertragen oder geloggt — nur
  Callsign, Timestamp und Signatur gehen über die Leitung.

---

## 11. Deduplikation & Rate-Limiting (Fragen 6, 10, 12)

### Client-Dedup (verhindert 20 identische Requests)

Anchor: das **GPR-TIME-Feld** (Positionszeitstempel), **nicht** der
Empfangszeitpunkt. Begründung: dieselbe gesendete GPR-Nachricht hat weltweit
dasselbe TIME-Feld; N Observer empfangen exakt denselben Wert. Der Empfangszeitpunkt
hingegen streut pro Observer — ungeeignet.

```
dedup_key = source + "|" + gpr_timestamp_utc + "|" + round(lat, k) + "," + round(lon, k)
```

- `source` = GPR-OBJECT (nicht AMD-Sender — sonst würde ein Relay dieselbe
  Position als „neue" durchkommen).
- `round(latlon, k)` mit `k = location_sharing_round_digits` (Privacy-Rundung,
  default 6 → volle GPS-Präzision, keine Blurring). Operator kann `k` senken,
  um die Position bewusst zu verwischen; Rundung dient dann gleichzeitig
  Privacy **und** Dedup-Stabilität.
- LRU-Set pro Observer, bounded (z.B. letzte 256 Keys). Treffer → kein Enqueue.
- Bewusst **pro Observer**: zwei Stationen dürfen dieselbe Source durchaus
  beide melden (der Server will ja „gehört von DF3SR **und** DL3ABC").

### Client-Throttle

- `location_sharing_min_interval_sec` (default 30) **pro source**: selbst bei
  wechselndem TIME-Feld (Bewegung) nicht öfter als alle N s einen Report je
  Source. Schützt vor einem mobilen Sender, der sekündlich neue GPRs sendet.
- Kein globales Rate-Limit auf Client-Seite nötig; der Server bringt
  429/Retry-After, falls nötig.

### Server-Dedup (out of scope, nur spezifiziert)

Server dedupliziert über `(source, timestamp)` + konfigurierbarem Zeitfenster
und speichert **mehrere Observer** zu *einem* Stationsmarker (Spec §20.9).
Mehrere Observer erzeugen also **keinen** Mehrfachmarker.

---

## 12. Error / Retry-Strategie (Fragen 9, 14)

- **Bounded Queue** (`location_sharing_queue_size`, default 64). Queue voll →
  ältester Report wird verworfen (best-effort, Spec §20.14 erlaubt das explizit).
- **Max. Retry-Anzahl** pro Report (default 3) bei 5xx/timeout.
- **Exponential Backoff**: 5 s → 15 s → 45 s, dann discard. 429 honoriert
  `Retry-After`.
- **4xx (außer 429/409)** → sofort discard (Client-/Auth-Fehler haben kein
  Retry-Potenzial).
- **Stale-Discard**: ein Report, der älter als z.B. 10 min in der Queue, wird
  beim Dequeue verworfen (Position ist eh nicht mehr „live").
- Empfehlung für **MVP**: best-effort ohne persistente Offline-Speicherung
  (Spec §20.14 erlaubt beide Varianten). Persistenz wäre ein späterer
  Aufsatz, der die bestehende `station.state`-Persistenz nicht belasten darf.

---

## 13. Privacy / Security-Einstellungen (Fragen 11, 18)

Neu in `ALEStationConfig` (als gekapselter Block, serialisiert wie die
`position_report_*`-Felder):

```cpp
// ── Location Relay (empfangene GPR-Positionen an Web-API weiterleiten) ───
bool      location_sharing_enabled        = false;  // Master-Opt-In (Spec §20.2)
std::string location_api_url              = "";     // https://…/api/v1/locations
std::string location_relay_identity_path  = "";     // location_relay_identity.key (Ed25519-Seed, neben station.state)
bool      location_sharing_allcall        = true;   // ALLCALL weiterleiten
bool      location_sharing_individual     = false;  // Individual Calls
bool      location_sharing_net            = false;  // NETCALL
bool      location_sharing_linked         = false;  // AMDs über bestehende Verbindung
uint32_t  location_sharing_min_interval_sec = 30;   // Throttle pro source
uint8_t   location_sharing_round_digits   = 6;      // Privacy-Rundung lat/lon (6 = volle GPS-Präzision)
bool      location_sharing_include_comment = false; // GPR-Kommentar nicht senden
uint16_t  location_sharing_queue_size     = 64;     // Bounded Queue
```

- **Default alles OFF** außer `allcall=true` (sinnvoll nur, wenn master enabled).
- **Master-Opt-In** ist die einzige harte Sicherheitsbarriere; alle
  Per-Call-Type-Toggles sind darunter wirksam.
- **Rundung** reduziert Positionsauflösung (Privacy); wirkt auch auf Dedup.
- **Kommentar-Filter** verhindert Leakage freier Textfelder.
- **Eigener Endpunkt** je Station über `location_api_url`/`token`.
- Zusätzlich empfohlen (open): Mindest-Positionsänderung wie bei
  `position_report_change_m`, um stehende Sender nicht zu repetieren.

### GUI (Spec §20.18)

Neue Settings-Card **„Privacy / Network — Location Relay"**, direkt benachbart
zur „Position Reports"-Section (index.html:1032ff, mobile/index.html:950ff),
identisches Markup (`.ssec-title` / `.fg` / `.finput` / `.fradio-group` /
`.fselect` / Apply-Button). Klare Privacy-Warnung als `.fhint`:

> *Off by default. When enabled, ALE-GPR positions received from other
> stations are forwarded to the configured HTTPS endpoint. Your own
> transmitted positions are never forwarded. No effect on ALE/audio/radio.*

Bridge-Kommandos: `LOCATION_SHARING_GET` / `LOCATION_SHARING_SET`, nach dem
`POSITION_REPORT_GET/SET`-Vorbild (ale_bridge.cpp:1067/1079). Service-Start/Stop
in `restart_location_services()` erweitern (ale_bridge.cpp:393): bei
`enabled` Service starten, sonst `stop()`.

---

## 14. Online / Stale / Offline (Frage 12) — serverseitig

Nicht in openALE. Server modelliert:

```
station { callsign, last_position, last_position_timestamp, last_seen_at,
          last_observer, observers[], report_count }
```

TTL-Schwellen (serverseitig konfigurierbar, Spec §20.10):

| Zustand | Bedingung |
|---|---|
| ONLINE  | `last_seen_at` ≤ X min (z.B. 15) |
| RECENT  | ≤ Y min (z.B. 60) |
| STALE   | ≤ Z min (z.B. 1440) |
| OFFLINE | > Z min |

openALE liefert nur `received_at`; der Server berechnet `last_seen_at` und
den Zustand.

---

## 15. Kartendaten (Fragen 13, 14, 15, 16)

**Auf die Karte** (Marker): callsign, lat/lon, last_seen, online/stale/offline.
**Nur in Detailansicht**: altitude, observer count, report count, last heard by,
comment, (später) track/history.

Provider-Agnostik (Spec §20.15/16): openALE sendet nur JSON. Der Server stellt
eine **Map-Data-API** (`GET /api/v1/locations`, `GET /api/v1/stations/{id}`)
bereit, idealerweise als GeoJSON, sodass das Frontend zwischen Leaflet (OSM)
und Google Maps frei wählen kann. openALE-Core enthält **keinen**
Karten-Provider.

---

## 16. NMEA / GPGGA (Spec §20.6)

Sauber trennen:

- **ALE-GPR** = Funk-/ALE-Empfangsformat → primärer Input für das Relay
  (`source_type = "ale_gpr"`).
- **NMEA / GPGGA** = lokale GNSS-Quelle für die **eigene** Position. Heute schon
  als `position_report_format = GGA` für den **TX**-Pfad unterstützt
  (`ale_station_config.h:144`). NMEA ist **nicht** Voraussetzung für
  Location-Sharing.

Ein „empfangenes" rohes `$GPGGA` als AMD ist unüblich; für MVP nicht vorgesehen.
Falls später gewünscht: `source_type = "nmea_gga"` mit eigenem Parser, aber
klartrennbar von ALE-GPR halten.

---

## 17. Logging (Spec §20.19)

Über `pal::get_logger()` (CLAUDE.md-Konvention), niemals printf:

```
[INFO] GPR received: KQ6XA 37.654321,-122.987654 via ALLCALL
[INFO] Location sharing: queued KQ6XA
[INFO] Location API: report accepted (200)
[WARN] Location API unavailable: connection timeout
[WARN] Location report deferred (queue full, discarded oldest)
[WARN] Location API 401 — auth failed, stopping retries
```

- Keine vollständigen Tokens, keine `Authorization`-Header.
- Kein Repeat-Spam: der Dedup/Throttle verhindert ohnehin 20× „queued".
- Status-Rückkanal: `PendingUpdate` erhält `loc_dirty` + kurzen Statustext;
  der Bridge-Main-Loop (ale_bridge.cpp:1795-Drain-Pattern) loggt und pusht ein
  WS-Event, sodass die GUI den Relay-Status anzeigen kann.

---

## 17a. ALE-GPR als primäres Format (Spec §20.5)

`raw_gpr` wird **unverändert** übertragen; zusätzlich die strukturierten Felder
aus `parse_gpr()`. Damit kann der Server beides nutzen. Für
`manual_or_invalid_position`-GPRs (Spec §15, `#`-Platzhalter) gilt: nur
`valid_gpr_structure` nötig zum Weiterleiten; strukturierte Felder werden null
gesendet. Der Server entscheidet, ob er solche Einträge kartiert. Ein
`valid_position == false`-Report ist trotzdem nützlich („Station gehört,
Position unbekannt" → Online ohne Marker).

---

## 18. Implementierungs-Schritte (Konzept-Phase → Code-Phase)

> Die eigentliche Code-Implementierung erfolgt erst nach Freigabe dieses
> Konzepts. Reihenfolge so gewählt, dass jeder Schritt für sich testbar ist.

### Phase A — Core: call_context in AmdData (kleinste Core-Änderung)
1. `include/App/ale_event_data.h`: `AmdData` um `const char* call_context` erweitern.
2. `src/App/ale_controller.cpp:2952` (`amd_dispatch`): `call_context` aus
   SM-/Call-Processor-Zustand ableiten (ALLCALL/INDIVIDUAL/NET/GROUP/LINKED).
   Dafür muss `amd_dispatch` den Kontext kennen — ggf. Signatur auf
   `amd_dispatch(peer, text, call_context)` erweitern und an allen
   Aufrufstellen (Calling-Frame-Pfad, Linked-Pfad) befüllen.
3. Bridge-Handler (ale_bridge.cpp:1648) + ale_monitor: `call_context`
   durchreichen (für GUI-Anzeige optional).

### Phase B — LocationRelayService + HttpPoster (async Client)
4. `include/App/http_poster.h` / `src/App/http_poster.cpp`: dünne Abstraktion
   `bool http_post_json(url, token, body, status_out)`. Windows: WinHTTP POST
   (Erweiterung von `sfi_service.cpp:11` auf POST + Header + Body + Status).
   Linux: OpenSSL- oder libcurl-Impl; **bis dahin** Linux-Stub mit WARN-Log
   „HTTPS relay needs TLS backend" (siehe offene Fragen).
5. `include/App/location_relay_service.h` / `.cpp` nach `SfiService`-Vorbild:
   Daemon-Thread, atomic running, bounded `std::deque<LocationReport>`,
   Mutex. `enqueue(LocationReport)`, Worker-Loop mit Backoff.
6. Gate-Funktion `is_shareable(...)`, Dedup-LRU, Throttle-Map (per source).
7. Subscribe `ALE_AMD_RECEIVED` im Bridge (wie GPS/SFI-Services), im Handler:
   `is_gpr` → `parse_gpr` → `is_shareable` → `dedup` → `enqueue`.

### Phase C — Config + Persistenz + GUI
8. `ALEStationConfig`-Felder (Abschnitt 13) + `import_settings`/`export_settings`/
   `write_settings_body` (Pattern ale_controller.cpp:3909/3933).
9. Bridge `LOCATION_SHARING_GET/SET` (Pattern ale_bridge.cpp:1067/1079);
   `restart_location_services()` erweitern (ale_bridge.cpp:393).
10. GUI-Cards desktop (index.html ~1032) + mobile (mobile/index.html ~950):
    Privacy/Network-Card, `syncLocationSharingFromBridge()` /
    `applyLocationSharingToBridge()` (Pattern app.js:3471/3506). Mobile
    bewusst parallel zu Desktop halten (Memory: keine unbefragte Mobile-Divergenz).

### Phase D — Extern (separat, nicht openALE-Core)
11. Web-API (`POST /api/v1/locations`, `GET`-Endpunkte), Storage, Server-Dedup,
    Last-Seen-TTL.
12. Karten-Frontend (Leaflet/OSM oder Google Maps), Marker, Detailansicht.

---

## 19. Open Questions

1. **Linux TLS-Backend.** ~~`sfi_service.cpp:80` macht Linux nur Raw-HTTP ohne
   TLS.~~ **Gelöst für Location Relay** (`http_poster.cpp`, die hier
   empfohlene `HttpPoster`-Abstraktion): POSIX nutzt jetzt mbedTLS (bereits
   Projekt-Abhängigkeit, siehe `apps/bridge/tls_support.cpp`), Windows
   weiterhin WinHTTP; beide unterstützen optionales Cert-Pinning
   (`location_ca_cert_path`) für selbstsignierte Relay-Server-Zertifikate.
   `sfi_service.cpp` selbst hat eine eigene, separate Raw-HTTP-Implementierung
   (unabhängig von `HttpPoster`) und bleibt von dieser Änderung unberührt —
   außerhalb des Scopes von Location Relay.
2. **call_context-Ableitung im Linked-Pfad.** Ist ein AMD über eine bestehende
   Verbindung `LINKED` oder der ursprüngliche Call-Typ der Verbindung? Vorschlag:
   `LINKED` (konfigurierbar via `location_sharing_linked`). Am State-Machine
   verifizieren.
3. **Own-Position-Reporting an die API?** Konzept sagt *nein* (nur empfangene).
   Falls der Operator die eigene Position *auch* direkt melden will: separater,
   expliziter Modus (z.B. `location_self_report`), nicht über den Relay-Pfad.
   — **Operator-Entscheidung nötig.**
4. **Malformed-GPR-Policy.** `valid_gpr_structure == true` aber
   `valid_position == false` weiterleiten („gehört, Position unbekannt") oder
   verwerfen? Vorschlag: weiterleiten, Server entscheidet. Konfigurierbar machen?
5. **Idempotency-Key-Header** verlangt Server-Unterstützung. Soll der Client ihn
   trotzdem senden (Server ignoriert unbekannte Header i.d.R.)? Vorschlag: ja.
6. **Schlüssel-Speicherung.** Gelöst: der Ed25519-Seed liegt in
   `location_relay_identity.key` (0600 auf POSIX, restriktive ACL auf
   Windows, best-effort), separat von `station.state` — kein Klartext-Secret
   mehr in der normalen Konfigurationsdatei.
7. **Mobile Divergenz.** Card bewusst parallel zu Desktop; keine abweichende
   Richtungscodierung (Memory `openale_feedback_mobile_alignment_consistency`).

---

## 20. Antworten auf die 16 Konzept-Fragen (Kompakt)

1. **Wo erkannt?** `ALE_AMD_RECEIVED` aus `amd_dispatch()` (ale_controller.cpp:2952); GPR-Parse im Relay-Client via `is_gpr`/`parse_gpr`.
2. **Wo Shareability entschieden?** Im `LocationRelayService` (gekapselte `is_shareable()`), nicht im Core.
3. **Eigene/empfangene/weitergeleitete?** Eigene = Self-Adresse (TX, nie relayed); empfangen: source=OBJECT=sender; weitergeleitet: source=OBJECT≠sender, relay=sender; observer=Self immer.
4. **ALLCALL erkannt/transportiert?** Heute fehlt im `AmdData` — wird um `call_context` ergänzt (Abschnitt 7); als `call_type` im API-Payload.
5. **Datenstruktur?** `LocationReport` (Abschnitt 8).
6. **Dedup?** Anchor = GPR-TIME + source + gerundete latlon, LRU pro Observer (Abschnitt 11).
7. **API?** `POST /api/v1/locations`, Ed25519-signiert, JSON, v1-Pfad (Abschnitt 9).
8. **Auth?** Per-Callsign Ed25519-Signatur, HTTPS-only, Seed nie übertragen/geloggt (Abschnitt 10).
9. **API-Ausfälle?** Async Worker-Thread, bounded Queue, Backoff, 4xx sofort discard, nie ALE/Audio/Radio blockierend (Abschnitt 12).
10. **Reporting-Rate begrenzt?** Per-source `min_interval_sec` + Dedup; Server 429/Retry-After (Abschnitt 11).
11. **Privacy/Security?** Master-OFF, Per-Call-Type-Toggles, Rundung, Kommentar-Filter, HTTPS, Token (Abschnitt 13).
12. **Online/stale/offline?** Serverseitig aus `last_seen_at` + TTL (Abschnitt 14).
13. **Kartendaten speichern?** Server: station, last_position, last_position_timestamp, last_seen_at, last_observer, observers, report_count (Abschnitt 14).
14. **Karte vs. Detail?** Karte: callsign/latlon/last_seen/Zustand; Detail: altitude/observer-count/comment/etc. (Abschnitt 15).
15. **openALE vs. Web-Service?** openALE: GPR-Erkennung + Gate + Dedup + Async-POST + Log. Web-Service: Auth/Storage/Server-Dedup/TTL/Map-API/Frontend (Abschnitt 3).
16. **Provider-Agnostik?** openALE sendet nur JSON; Server mappt auf GeoJSON; Frontend wählt Leaflet/OSM oder Google Maps frei (Abschnitt 15).

---

*Dieses Dokument ist eine Architekturentscheidung. Implementierung (Phase A–C)
nach Freigabe; Web-API/Frontend (Phase D) separat.*