# Aufgaben für Coding-Agent: 6 Fehler in der ale_bridge-GUI-Anbindung

> Diese Anweisung ist **selbstständig** — du hast keinen Kontext aus früheren Sessions.
> Lies zuerst diesen Orientierungsteil, dann die 6 Aufgaben. Alle Datei-/Zeilenangaben
> beziehen sich auf den aktuellen Stand des Repos `e:\repos\openALE` (Branch `develop`).
> Zeilennummern können leicht verschoben sein — orientiere dich an den Funktionsnamen.

## Projekt-Orientierung (wichtig, bevor du anfängst)

**Was das ist:** openALE, eine Clean-Room-Implementierung von MIL-STD-188-141B 2G ALE
(Automatic Link Establishment) für den Funkverkehr. C++17, Windows/MSVC, Build über CMake.

**Die hier relevanten Komponenten:**
- `apps/ale_bridge.cpp` — eine Konsolen-Exe, die einen `ale::ALEController` (das Protokoll-
  Herzstück) besitzt und ihn über **WebSocket** für eine Browser-GUI freigibt. Startet
  argumentlos (nur optional `--port N`, Default 8765).
- `apps/bridge/` — die Bridge-Hilfsschicht: `ws_server.h/.cpp` (handgeschriebener
  RFC6455-WebSocket-Server, Single-Client), `minijson.h` (handgeschriebener JSON-
  Parser/Serializer), `sha1.h`/`base64.h`/`ws_handshake.h`.
- `apps/gui/` — die **Produktiv-GUI** (statisches `index.html` + `app.js` + `styles.css`).
  Sie verbindet sich beim Laden zu `ws://localhost:<cfgWsPort>` und steuert den Controller
  über JSON-Kommandos. **Diese GUI bearbeitest du.**
- `apps/gui-demo/` — eine **eingefrorene reine Mock-Kopie**. **NICHT anfassen.** Sie darf
  niemals eine Bridge-Verbindung aufbauen und dient nur UI-Experimenten.

**Protokoll GUI↔Bridge (in `apps/gui/app.js`, Funktionen `connectBridge`/`bridgeSend`):**
- GUI→Bridge: Textframes `{"id":N,"cmd":"...", ...args}`.
- Bridge→GUI: Antwort `{"id":N,"ok":bool,...}` **oder** asynchrones Event
  `{"event":"...",...}` **oder** ein **Binär-Frame** mit 257 × float32 (FFT-Spektrum
  fürs Wasserfall-Diagramm).
- **Wichtige Eigenheit:** Events feuern *synchron während* der Kommandoverarbeitung, ein
  Event kann also **vor** der zugehörigen Antwort ankommen. Frames werden nach Form
  unterschieden (`event` vs. `id`), nie nach Reihenfolge.

**Regeln für dieses Projekt:**
- **Keine externen Abhängigkeiten** — alles ist handgeschrieben (JSON, SHA1, WebSocket).
  Füge KEINE Libraries/npm/vendoring hinzu. Bleib bei diesem Stil.
- `apps/gui-demo/` bleibt unangetastet.
- Die GUI muss ohne laufende Bridge weiterhin als Demo funktionieren (Fallback-Pfade in
  `app.js` prüfen `if (bridgeConnected)`).

**Bauen & Testen:**
```
# Konfigurieren (einmalig, falls build/ fehlt):  cmake -S . -B build
cmake --build build --target ale_bridge --config Debug      # nur die Bridge
cmake --build build --config Debug                          # alles
cd build && ctest -C Debug --output-on-failure              # 22 Test-Suiten, müssen grün bleiben
build/Debug/ale_bridge.exe                                  # Bridge starten (lauscht auf 8765)
```
GUI testen: `apps/gui/index.html` im Browser öffnen (Doppelklick / `file://` genügt für
WebSocket). Konsole (F12) auf Fehler prüfen.

---

## Aufgabe 1 — Beim Bridge-Start einen anklickbaren GUI-Link in der Konsole ausgeben

**Soll:** Nach dem Start von `ale_bridge.exe` soll in der Konsole ein Link wie
`http://localhost:8765/index.html` erscheinen. Beim Anklicken (die meisten Terminals
machen `http://`-URLs klickbar) öffnet sich der Browser direkt mit der GUI.

**Das bedeutet:** Die Bridge muss die statischen GUI-Dateien (`index.html`, `app.js`,
`styles.css` aus `apps/gui/`) **über HTTP auf demselben Port** ausliefern. Aktuell macht
der Server nur den WebSocket-Upgrade und schließt jede Nicht-WS-Verbindung sofort.

**Wo / was:**
1. `apps/bridge/ws_server.cpp`, Accept-Schleife `io_thread_main()` (~Zeile 140–181) und
   `read_handshake_key()` (~Zeile 39–68). Aktuell liest `read_handshake_key` die ganze
   HTTP-Anfrage, zieht aber nur den `Sec-WebSocket-Key`; fehlt der, wird die Verbindung
   geschlossen. **Umbauen:** die komplette Request-Zeile (`GET /pfad HTTP/1.1`) + Header
   erfassen. Dann verzweigen:
   - Enthält die Anfrage `Upgrade: websocket` (bzw. einen `Sec-WebSocket-Key`) → WS-
     Handshake wie bisher.
   - Sonst (normales `GET /index.html`, `GET /app.js`, `GET /styles.css`, `GET /`) →
     die Datei aus dem Web-Root lesen und als HTTP-200-Antwort ausliefern
     (`Content-Type`: `text/html` / `application/javascript` / `text/css`), Verbindung
     schließen, weiter in der Accept-Schleife. `GET /` → `index.html`. Unbekannte Pfade →
     `404`. **Kein Pfad-Traversal** zulassen (`..` ablehnen).
2. **Web-Root-Auflösung (heikel!):** Die Exe läuft aus `build/Debug/`, die GUI liegt in
   `apps/gui/`. Du musst `apps/gui/` robust finden. Empfehlung: Pfad relativ zum
   Exe-Verzeichnis auflösen (Windows: `GetModuleFileNameW`) und vom Exe- Verzeichnis aus
   nach oben nach `apps/gui/index.html` suchen; zusätzlich ein paar Fallback-Kandidaten
   (CWD, `./apps/gui`). Alternativ einen optionalen Parameter `--webroot PATH` mit
   sinnvollem Default. Wähle eine Variante, die funktioniert, wenn man die Exe aus
   `build/Debug/` **oder** aus dem Repo-Root startet.
3. `apps/bridge/ws_server.cpp`: `listen(server, 1)` (~Zeile in `start()`) auf z. B.
   `listen(server, 16)` erhöhen — der Browser öffnet beim Seitenladen mehrere parallele
   Verbindungen; ein Backlog von 1 verwirft welche.
4. `apps/ale_bridge.cpp`, in `main()` nach `ws.start(port)` (~Zeile 510): zusätzlich zur
   bestehenden Zeile den Link ausgeben, z. B.
   `printf("[ale_bridge] open GUI:  http://localhost:%u/index.html\n", port);`

**Bekannte Einschränkung (im Code als Kommentar festhalten):** Der `WsServer` ist
Single-Client/blocking — sobald der WebSocket offen ist, blockiert `handle_client()` die
Accept-Schleife, es können also währenddessen keine weiteren HTTP-Requests bedient werden.
Für den normalen Ablauf (Browser lädt erst index.html/app.js/styles.css, **dann** öffnet
app.js den WebSocket) reicht das. Wenn du es robuster willst, kannst du statische Requests
in einem kurzlebigen Detached-Thread bedienen — aber halte es zunächst minimal.

**Verifikation:** Bridge starten, im Browser `http://localhost:8765/index.html` öffnen →
GUI lädt **über HTTP** (nicht `file://`) und verbindet sich anschließend per WebSocket
(Bridge-Log zeigt `[ws_server] client connected`).

---

## Aufgabe 2 — Audio-Geräte werden nicht vollständig enumeriert

**Soll:** Die Geräteliste in den Audio-Settings soll alle relevanten Ein-/Ausgabegeräte
zeigen, die der Nutzer erwartet.

**Wo / was:** `src/App/audio_device.cpp`:
- `WasapiDevice::list_flow()` (~Zeile 581): `EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)`
  listet **nur aktive** Endpunkte. Geräte im Zustand *deaktiviert* oder *nicht eingesteckt*
  (`DEVICE_STATE_DISABLED` / `DEVICE_STATE_UNPLUGGED`) fehlen. **Das ist der Hauptverdacht.**
- `WasapiDevice::resolve_device()` (~Zeile 537) benutzt denselben `DEVICE_STATE_ACTIVE`-
  Filter und matcht per Substring auf den Friendly-Name. **Zweiter Punkt:** doppelte
  Friendly-Names (z. B. zwei „T24i-2L") sind nicht unterscheidbar — `resolve_device`
  nimmt den ersten Treffer, das kann das falsche Gerät sein.

**Vorgehen:**
1. Stelle zunächst fest, **welche** Geräte konkret fehlen: `build/Debug/ale_bridge.exe`
   starten, ein WebSocket-Client (oder die GUI) `{"cmd":"AUDIO_DEVICES"}` schicken, und
   die Liste mit dem Windows-Sound-Dialog vergleichen.
2. Wenn deaktivierte/getrennte Geräte fehlen und gewünscht sind: Statusmaske in
   `list_flow` erweitern (z. B. `DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED`). **Achtung:**
   `resolve_device`/`open()` kann nur **aktive** Geräte wirklich öffnen — also entweder im
   Listing den Zustand mit anzeigen, oder inaktive Geräte ausgrauen. Triff hier eine
   saubere Entscheidung und dokumentiere sie im Code-Kommentar.
3. Doppelte Namen entschärfen: an den Friendly-Name etwas Eindeutiges anhängen (z. B.
   Index oder einen Teil der Endpoint-ID), und denselben String dann auch in
   `resolve_device` zum Matching nutzen, damit Auswahl und Öffnen konsistent sind.

**Hinweis:** Das Bridge-Kommando `AUDIO_DEVICES` (`apps/ale_bridge.cpp`) enumeriert
absichtlich über ein **Wegwerf-`make_audio_device()->list_devices()`**, damit die Liste
auch funktioniert, **bevor** ein Gerät geöffnet ist. Das ist korrekt — die Ursache liegt in
`list_devices()`/`list_flow()` selbst, nicht in der Bridge.

**Verifikation:** `AUDIO_DEVICES` liefert die erwarteten Geräte; das gewählte Gerät lässt
sich per `AUDIO_OPEN` öffnen (Bridge-Log zeigt `WASAPI render ... capture ...`).

---

## Aufgabe 3 — Gewählte Audio-Geräte „springen zurück" beim erneuten Öffnen der Settings

**Symptom:** Nach Connect + Save in den Audio-Settings ist beim erneuten Öffnen des
Settings-Dialogs die Geräteauswahl wieder verstellt (auf das erste Listenelement).

**Ursache:** `apps/gui/app.js`, `enumDevices()` (~Zeile 631) baut bei jedem Aufruf die
`<option>`-Liste der Dropdowns `audioIn`/`audioOut` **komplett neu** — und `openSettings()`
(~Zeile 599) ruft `enumDevices()` jedes Mal auf. Beim Neuaufbau geht die aktuelle Auswahl
verloren (`.value` fällt auf das erste Element zurück). Die GUI merkt sich nirgends, welches
Gerät gerade gewählt/geöffnet ist.

**Wo / was (`apps/gui/app.js`):**
1. Zwei Modul-Variablen einführen, z. B. `let audioInSelected = ''; let audioOutSelected = '';`.
2. Diese setzen, wenn der Nutzer ein Gerät wählt **und** wenn `openAudioDevice()`
   (~Zeile 662) erfolgreich war (im `AUDIO_OPEN`-Reply-Callback). Tipp: `onchange`-Handler
   an den beiden `<select>` in `apps/gui/index.html` (`id="audioIn"`/`id="audioOut"`)
   ergänzen, die die Variablen aktualisieren.
3. In `enumDevices()` **nach** dem Neuaufbau der Optionen die gemerkte Auswahl
   wiederherstellen: wenn `audioInSelected` in den neuen Optionen vorkommt, `audioIn.value =
   audioInSelected` setzen (analog für out). So bleibt die Auswahl über
   Schließen/Öffnen des Dialogs erhalten.
4. Optional, sauberer: das aktuell geöffnete Gerät als „aktiv" markieren. (Die Bridge meldet
   die offenen Geräte aktuell nicht zurück — wenn du das willst, könntest du `AUDIO_OPEN`
   so erweitern, dass die Antwort die tatsächlich geöffneten Gerätenamen enthält, und die
   GUI daraus die Auswahl ableitet. Nicht zwingend.)

**Verifikation:** Gerät wählen → „Connect Audio" → Settings schließen → Settings erneut
öffnen → Audio-Sektion: dieselben Geräte sind weiterhin ausgewählt.

---

## Aufgabe 4 — Scan-Button ist immer aktiv und lässt sich nicht deaktivieren

**Symptom:** Der „Scan"-Button im Header ist schon beim Start aktiv (zeigt „■ Stop") und
geht durch Klick nicht aus.

**Ursachen (mehrere):**
1. `apps/gui/app.js`, BOOT-Sektion (~Zeile 1303): `goScanning();` wird beim Laden
   unbedingt aufgerufen → `setStatus(...,'scanning')` (~Zeile 363) setzt den Button auf
   aktiv, noch bevor überhaupt eine Bridge verbunden ist.
2. `apps/ale_bridge.cpp` (~Zeile 540): die Bridge ruft beim Start **`ctrl.start_scanning()`**
   auf → die State Machine geht sofort in `SCANNING` → `STATUS` liefert „SCANNING" →
   `applyBridgeState()` (~Zeile 90) → `goScanning()` → Button aktiv.
3. **Kernproblem:** `ALEController::start_available()` (`src/App/ale_controller.cpp:380`)
   wechselt den SM-Zustand **NICHT** von `SCANNING` nach `IDLE` zurück — es aktiviert nur den
   Demodulator. Der GUI-Button schickt bei „Stop" aber `AVAILABLE` (`toggleScan()`,
   app.js ~Zeile 982). Da sich der SM-Zustand dabei nicht ändert, kommt **kein** `state`-
   Event, und der Button bleibt auf „Stop" hängen.

**Wo / was:**
1. `apps/ale_bridge.cpp`: das `ctrl.start_scanning()` beim Start **entfernen** bzw. durch
   `ctrl.start_available()` ersetzen, sodass die Station nach dem Start in `IDLE` ist
   (Button „▶ Scan", aus). Begründung: Scannen ergibt erst mit konfigurierten Kanälen Sinn
   (siehe Aufgabe 5).
2. **Core:** dafür sorgen, dass „verfügbar/idle" den Zustand wirklich nach `IDLE` bringt,
   wenn man aus `SCANNING` kommt — sonst ist der Button-Toggle nie zuverlässig. Die State
   Machine behandelt bereits `ALEEvent::STOP_SCAN` → `IDLE`
   (`src/Protocol/Control/ale_state_machine.cpp:124`). Lass also den `AVAILABLE`-Pfad
   `STOP_SCAN` auslösen, wenn der SM gerade in `SCANNING` ist. Sauberste Umsetzung:
   entweder in `ALEController::start_available()` ein `sm_.process_event(ALEEvent::STOP_SCAN)`
   ergänzen (prüfe: `STOP_SCAN` aus `IDLE` muss ein harmloser No-op bleiben — `start_available()`
   wird auch von `apps/ale_cli.cpp` benutzt!), oder im Bridge-`AVAILABLE`-Handler ein
   passendes Controller-API aufrufen. Danach kommt beim Stoppen ein echtes `state`-Event
   `IDLE` → der Button geht korrekt aus.
3. `apps/gui/app.js` BOOT: `goScanning()` nicht mehr unbedingt aufrufen — beim Start `goIdle()`
   (Button aus); der echte Zustand kommt ohnehin über `syncAllFromBridge()`/`STATUS`, sobald
   verbunden.

**Verifikation:** Bridge + GUI starten → Scan-Button ist **aus**. Mit ≥2 Kanälen (Aufgabe 5)
einmal „Scan" klicken → Button „■ Stop", SM in SCANNING. Nochmal klicken → Button „▶ Scan",
SM in IDLE. Beide Übergänge müssen funktionieren.

---

## Aufgabe 5 — Scan-Button löst keine Scan-Aktion aus; Scannen erst ab ≥2 Kanälen

**Soll:** Scannen ist nur möglich, wenn **mindestens 2 Kanäle** in den Settings hinterlegt
sind. Vorher muss es **graceful** abgefangen werden (Button deaktiviert/ausgegraut, kein
fehlschlagendes Kommando).

**Hintergrund:** Channel-Hopping über eine Kanalliste braucht mehrere Kanäle; mit 0 oder 1
Kanal macht `start_scanning()` nichts Sinnvolles. Die GUI hält die Kanäle in der globalen
Variable `channels` (Array, `apps/gui/app.js`), die per `syncChannelsFromBridge()` mit dem
Core synchron gehalten wird.

**Wo / was (`apps/gui/app.js`):**
1. Hilfsfunktion `function scanEnabled() { return channels.length >= 2; }`.
2. Den Scan-Button (`id="scanBtn"`, `apps/gui/index.html` Zeile 38) ausgrauen/deaktivieren,
   wenn `!scanEnabled()` — z. B. `disabled`-Attribut setzen und einen Tooltip
   „Mindestens 2 Kanäle in den Einstellungen anlegen". Diese Auswertung an allen Stellen
   aufrufen, an denen sich die Kanalzahl ändert: am Ende von `syncChannelsFromBridge()`,
   `addCh()`, `delCh()` und einmal beim BOOT.
3. `toggleScan()` (~Zeile 982): wenn `!scanEnabled()`, **nicht** `SCAN` senden, sondern eine
   kurze Nutzerrückmeldung geben (z. B. `pushLog([['data','Scanning braucht ≥2 Kanäle']],'miss')`)
   und abbrechen.
4. Optional defensiv im Core/Bridge: `SCAN`-Kommando bei <2 Kanälen mit `ok:false` + `error`
   beantworten (Bridge `dispatch_command`, `apps/ale_bridge.cpp`), damit auch nicht-GUI-
   Clients sauber abprallen. Primär ist aber die GUI-Sperre.

**Verifikation:** Bridge + GUI, **0 Kanäle** → Scan-Button ausgegraut, Klick bleibt wirkungslos
(mit Hinweis). In den Settings 2 Kanäle anlegen → Button wird aktiv → Klick startet echtes
Scanning (SM → SCANNING, Statuslog „Starting ALE scanner").

---

## Aufgabe 6 — FFT-Wasserfall ist nicht mit den Spektrumdaten verbunden

**Symptom:** Das Wasserfall-Diagramm zeigt nur die synthetische Demo-Animation, nie das
echte FFT-Spektrum der Bridge.

**Hauptursache (konkreter Bug):** `apps/gui/app.js`, `connectBridge()` (~Zeile 32–36) setzt
**nicht** `ws.binaryType = 'arraybuffer'`. Der WebSocket-Default ist `'blob'`, also kommen
die Binär-Spektrum-Frames als `Blob` an. Die Prüfung `if (ev.data instanceof ArrayBuffer)`
in `ws.onmessage` (~Zeile 45) ist damit **immer false** → die Frames werden verworfen →
`onSpectrumFrame()` (~Zeile 132) läuft nie → `latestSpectrum` bleibt `null` → `genRow()`
(~Zeile 250) nutzt nie die echten Daten.

**Fix:** in `connectBridge()` direkt nach `ws = new WebSocket(...)` ergänzen:
`ws.binaryType = 'arraybuffer';`. Das ist die eigentliche Verbindung.

**Zweite Bedingung (kein Bug, aber wichtig zu verstehen/sicherzustellen):** Das Spektrum-FFT
läuft nur, wenn (a) ein Audiogerät geöffnet ist (per `AUDIO_OPEN`, Aufgabe-2/3-Flow) **und**
(b) der Demodulator **RX-enabled** ist. Siehe `src/Modem/ale2g_modem.cpp:153`
(`if (!enabled_) return;` ganz oben in `push_samples`) — die Spektrumberechnung (~Zeile 163)
steht hinter diesem Guard. Der Demodulator wird über die State Machine RX-aktiv (in
IDLE/SCANNING/LISTENING). `ALEController::start_available()` aktiviert RX
(`demodulator_.set_enabled(true)`, `ale_controller.cpp:385`). Stelle daher sicher, dass die
Station nach dem Start in einem RX-aktiven Zustand ist (passt zu Aufgabe 4: Start in
`available`/IDLE mit aktivem RX), damit nach `AUDIO_OPEN` echtes Spektrum fließt. Der
Wasserfall zeigt also reale Daten **erst nach** „Connect Audio" — das ist erwartetes
Verhalten, kein Fehler.

**Verifikation:** Bridge + GUI, Audio per „Connect Audio" mit einem Gerät verbinden, das
echtes Signal/Rauschen liefert (z. B. eine VB-Audio-CABLE-Schleife oder ein Mikrofon). Im
Wasserfall müssen sich echte Spektren zeigen (statt der Demo-Zufallsmuster). Zur schnellen
Kontrolle: in der Browser-Konsole loggen, dass `onSpectrumFrame` aufgerufen wird /
`latestSpectrum` gefüllt ist.

---

## Abschluss / Gesamt-Verifikation

1. `cmake --build build --config Debug` fehlerfrei.
2. `cd build && ctest -C Debug` — alle **22** Suiten weiterhin grün (deine Änderungen
   dürfen keine Protokoll-/Core-Tests brechen; besonders bei Aufgabe 4 die `STOP_SCAN`-
   Änderung gegen `StateMachine`/`ALECalling` prüfen).
3. `apps/gui-demo/` ist unverändert.
4. End-to-End mit echter GUI: Bridge starten → ausgegebenen `http://localhost:8765/index.html`
   öffnen → Settings → Audio: vollständige Geräteliste, Auswahl bleibt nach
   Schließen/Öffnen erhalten → „Connect Audio" → Wasserfall zeigt echtes Spektrum →
   Scan-Button erst ab 2 Kanälen aktiv und sauber an/aus schaltbar.

**Falls du Build-/Test-Infrastruktur anfassen musst:** Es gibt keinen Node/npm-Toolchain;
GUI-Tests laufen rein im Browser. Für automatisierte WS-Tests existiert in `tests/` keine
JS-Suite — die C++-Tests (`tests/test_ws_bridge_utils.cpp`) decken nur die Utility-Header ab.
