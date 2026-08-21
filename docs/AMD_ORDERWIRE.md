# AMD — Automatic Message Display (A.5.7.2)

AMD ermöglicht, kurze ASCII-Klartexte im Rahmen eines normalen Anrufs zu übertragen. Der Text
erscheint beim Empfänger sofort — ohne dass eine vollständige Verbindung (`LINKED`-Zustand)
aufgebaut sein muss.

## Zeichensatz

AMD verwendet den **Expanded-64-Zeichensatz** (A.5.7.2.1): Großbuchstaben, Ziffern, Leerzeichen
und gängige Sonderzeichen (0x20–0x5F). Kleinbuchstaben sind **nicht** im Zeichensatz enthalten;
nicht unterstützte Zeichen werden beim Senden durch `?` ersetzt.

## Auslösen (WebSocket-Bridge-API)

```json
→ { "cmd": "AMD", "to": "BOB", "text": "POSITION 14MHZ" }
← { "id": ..., "ok": true, "msg": "OK: ..." }

// Nachricht senden UND danach verlinkt bleiben:
→ { "cmd": "AMD", "to": "BOB", "text": "POSITION 14MHZ", "link": true }
```

`ALEController::send_amd(target, text, link_after_send=false)`
([ale_controller.h](../include/App/ale_controller.h)) sendet den Text sofort, wenn die Station
bereits mit `target` `LINKED` ist; andernfalls wird die Nachricht eingereiht und ein Call zu
`target` initiiert (Multi-Channel-Retry: AMD wird auf jedem versuchten Kanal erneut gesendet,
bis der Call erfolgreich ist oder alle Kanäle erschöpft sind). Eine neue `AMD`-Anfrage
überschreibt eine noch ausstehende.

**`link` (Bridge-Feld, optional, Default `false`)** entscheidet — nur im nicht-`LINKED`-Pfad —,
ob nach der Zustellung eine echte Verbindung bestehen bleibt (Ion2G-Modell, siehe unten). Wird
das Feld weggelassen, bleibt keine Verbindung bestehen: eine reine Nachricht erzeugt keinen
ungewollten Link mehr (vormals löste jedes `AMD` immer einen vollen Handshake bis `LINKED` aus).

## Ablauf

AMD reitet im **Rufrahmen** (frame 1) mit — die Nachricht ist beim Empfänger da, bevor der
Handshake überhaupt abgeschlossen ist. Der **dritte Handshake-Rahmen** (bisher "ACK") trägt
keinen Nachrichteninhalt mehr — er ist ausschließlich die Link/Kein-Link-Entscheidung des
Anrufers: `TIS` = Link bleibt bestehen, `TWAS` = Handschlag beendet, kein Link.

```
Sender (SAM):                                    Empfänger (BOB):
  AMD → to=BOB, text="POSITION 14MHZ"             [wartet im Scan-Modus]
  → initiiert Call zu BOB
    → Rahmen 1: TO:BOB×2  FROM:SAM  CMD:POS  DATA:ITIO  REP:N 1  DATA:4MHZ  TIS:SAM
                                                  [AM] AMD from SAM: POSITION 14MHZ
                                                  [>>] Incoming call from: SAM
    ← Rahmen 2: TO:SAM×2  TIS:BOB (Antwort) ←
    → Rahmen 3: TO:BOB×2  TWAS:SAM  (link=false, Default)  →
                                                  AMD received — caller declined link
  beide Seiten kehren zu SCANNING/IDLE zurück, KEIN LINK ESTABLISHED
```

Mit `link=true` sieht Rahmen 3 stattdessen `TO:BOB×2 TIS:SAM` — dann folgt wie bisher
`LINK ESTABLISHED`. Der AMD-Text wird beim Empfänger **vor** der Call-Received-Meldung
ausgegeben, damit der Operator den Kontext kennt, bevor über den Link entschieden wird. Der
Empfänger muss **nicht** im `LINKED`-Zustand sein — AMD funktioniert während der gesamten
Handshake-Phase und ist von der Link-Entscheidung unabhängig zustellbar.

## Frame-Aufbau (MIL-STD-188-141B A.5.7.2.2 — Wortformat)

Das erste AMD-Wort trägt den **CMD-Präambel** (Binär 110) mit den ersten 3 Zeichen. Alle
weiteren Wörter wechseln zwischen DATA und REP:

```
… TO:BOB×2  FROM:SAM  [CMD 'a' + LQA]  CMD(Zeichen 1–3)  DATA(4–6)  REP(7–9)  DATA(10–12)  …  TIS:SAM
```

`FROM:SAM` identifiziert den Anrufer bereits im ersten Rahmen (Basic-38-Präambel `FROM`,
`AddressEncoder`/`ALESequenceBuilder::from_id()`), sodass der Empfänger den Absender kennt,
ohne auf den dritten Rahmen warten zu müssen.

Maximal **30 Wörter = 90 Zeichen** pro Nachricht (A.5.7.2.3, `Tm_max`, siehe
[ale_timing.h](../include/Protocol/Control/ale_timing.h)). Das letzte Wort wird bei Bedarf
automatisch mit Leerzeichen (0x20) aufgefüllt.

## Empfangsausgabe

```
[AM] AMD from SAM: QRV 14250 USB
[>>] Incoming call from: SAM
```

## Beispiele

```json
// Einfache Statusmeldung — keine Verbindung bleibt bestehen
{ "cmd": "AMD", "to": "BOB", "text": "QRV 14250 USB" }

// Positionsmeldung mit Koordinaten
{ "cmd": "AMD", "to": "HQ", "text": "52N 013E 14250" }

// Nachricht senden UND anschließend verlinkt bleiben (z.B. weiteres Gespräch geplant)
{ "cmd": "AMD", "to": "HQ", "text": "STANDBY FOR TRAFFIC", "link": true }
```
