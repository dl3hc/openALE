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
```

`ALEController::send_amd(target, text)` ([ale_controller.h](../include/App/ale_controller.h))
sendet den Text sofort, wenn die Station bereits mit `target` `LINKED` ist; andernfalls wird die
Nachricht eingereiht und ein Call zu `target` initiiert (Multi-Channel-Retry: AMD wird auf jedem
versuchten Kanal erneut gesendet, bis der Call erfolgreich ist oder alle Kanäle erschöpft sind).
Eine neue `AMD`-Anfrage überschreibt eine noch ausstehende.

## Ablauf

```
Sender (SAM):                          Empfänger (BOB):
  AMD → to=BOB, text="POSITION 14MHZ"  [wartet im Scan-Modus]
  → initiiert Call zu BOB
    → sendet: …TO:BOB  CMD:POS  DATA:ITIO  REP:N 1  DATA:4MHZ  TIS:SAM…
                                        [AM] AMD from SAM: POSITION 14MHZ
                                        [>>] Incoming call from: SAM
  ← 3-Wege-Handshake läuft automatisch →
                                        LINK ESTABLISHED  Peer: SAM
```

Der AMD-Text wird beim Empfänger **vor** der Call-Received-Meldung ausgegeben, damit der
Operator den Kontext kennt, bevor die Verbindung bestätigt wird. Der Empfänger muss **nicht**
im `LINKED`-Zustand sein — AMD funktioniert während der gesamten Handshake-Phase.

## Frame-Aufbau (MIL-STD-188-141B A.5.7.2.2)

Das erste AMD-Wort trägt den **CMD-Präambel** (Binär 110) mit den ersten 3 Zeichen. Alle
weiteren Wörter wechseln zwischen DATA und REP:

```
CMD(Zeichen 1–3)  DATA(4–6)  REP(7–9)  DATA(10–12)  …  TIS:SAM
```

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
// Einfache Statusmeldung
{ "cmd": "AMD", "to": "BOB", "text": "QRV 14250 USB" }

// Positionsmeldung mit Koordinaten
{ "cmd": "AMD", "to": "HQ", "text": "52N 013E 14250" }
```
