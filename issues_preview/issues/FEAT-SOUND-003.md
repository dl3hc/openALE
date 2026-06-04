# FEAT-SOUND-003 — Optionales Handshake nach Sounding

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🟢 `COULD` &nbsp;|&nbsp; **Domain:** `SOUND`

## 📁 Module
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- ⬜ `FEAT-SOUND-001` — Single-Channel Sounding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-SOUND-010 — Call-Acceptance Scanning Sounding Protocol
**Spec:** `A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Es gibt mehrere spezifische Protokoll-Unterschiede, wenn Station "A" plant, Anrufer nach dem Sound zu begrüßen. "A" soundet für die gleiche Zeitdauer wie zuvor. Da "A" für Anrufe empfänglich ist, muss es seine normale Scanning Dwell Time (Td) oder seine vorab festgelegte Wait-Before-Transmit Time (Twt), whichever länger ist, verwenden, um sowohl für Kanalaktivität als auch für Anrufe zu hören, bevor es sendet. Wenn der Kanal frei ist, initiiert "A" den Scanning Sound wie zuvor, aber mit "TIS A". Am Ende des Sounding Frame muss "A" für Anrufe warten, gemäß der Wait-for-Reply und Tune Time (Twrt) im individuellen Scanning Calling Protocol, in diesem Fall als 6 Tw (für schnell-abstimmende Stationen) dargestellt. Während dieses Wartens muss "A" (wie immer) auf Anrufe hören, die zufällig ankommen können, auch wenn sie nicht mit "A"s Sound assoziiert sind, sowie auf jeden anderen gehörten Sound, den "A" als Konnektivitätsinformation speichern muss, wenn es polling-fähig ist. Wenn keine Anrufe empfangen wurden, verlässt "A" den Kanal.

### REQ-SOUND-011 — Optionales Handshake: Getriggert durch Konnektivität vom Sounding
**Spec:** `A.5.3.4` &nbsp;|&nbsp; **Priorität:** 🟡 `SHOULD`

> Eine alternative Aktion ist die Implementierung eines optionalen Handshakes mit einer Station unmittelbar nach deren Sound. Dieses Protokoll ist in jeder Hinsicht identisch mit dem Single-Channel Individual Call Protocol, außer dass es manuell oder automatisch (Bediener oder Controller) getriggert wird durch den Erwerb der Konnektivität von der Station, die gerufen werden soll. Wenn ALE-Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt mit einer solchen Station erforderlich ist, sollte das optionale Handshake Protocol verwendet werden. Die rufende Station sollte unmittelbar den Call initiieren, nachdem sie bestimmt hat, dass die gerufene Station ihre Übertragung beendet hat. Eine Wait-Before-Transmit Time ist nicht erforderlich. Wenn "B" "A"s Sound hört und "A" sucht, ruft "B" unverzüglich mit dem einfachen Single-Channel Call. Wenn auch "B"s Bediener oder Controller "A"s Adresse identifiziert, kann es den optionalen Handshake versuchen.

### REQ-SOUND-012 — Sounding: Keine neuen Frequenz- oder Hardware-Erfordernisse
**Spec:** `A.5.3.1 / A.5.3.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Sounding verwendet die standardmäßige ALE-Signalisierung und die gleichen Frequenzen, die bereits für das Scanning vorgesehen sind. Es werden keine neuen Frequenzen oder speziellen Sounding-Kanäle benötigt. Das "Sound Set" ist üblicherweise identisch mit dem "Scan Set".


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-SOUND-010 — Call-Acceptance Scanning Sounding Protocol (`A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-010-1`** — Bei geplantem Begrüßen von Anrufern verwendet Station "A" seine normale Scanning Dwell Time (Td) oder Twt, whichever länger ist, zum Hören vor dem Sounding.
- [ ] ⬜ **`AC-SOUND-010-2`** — Bei freiem Kanal initiiert "A" den Scanning Sound mit "TIS A".
- [ ] ⬜ **`AC-SOUND-010-3`** — "A" soundet für die gleiche Zeitdauer wie beim Call-Rejection Protocol.
- [ ] ⬜ **`AC-SOUND-010-4`** — Am Ende des Sounding Frame wartet "A" für Anrufe gemäß Twrt (z. B. 6 Tw für schnell-abstimmende Stationen).
- [ ] ⬜ **`AC-SOUND-010-5`** — Während des Wartens hört "A" auf assoziierte und nicht-assozierte Anrufe sowie auf andere Sounds.
- [ ] ⬜ **`AC-SOUND-010-6`** — Gehörte andere Sounds werden als Konnektivitätsinformation gespeichert, wenn die Station polling-fähig ist.
- [ ] ⬜ **`AC-SOUND-010-7`** — Wenn keine Anrufe empfangen wurden, verlässt "A" den Kanal.

### REQ-SOUND-011 — Optionales Handshake: Getriggert durch Konnektivität vom Sounding (`A.5.3.4`)
- [ ] ⬜ **`AC-SOUND-011-1`** — Ein optionales Handshake kann unmittelbar nach einem Sounding mit einer gerufenen Station durchgeführt werden.
- [ ] ⬜ **`AC-SOUND-011-2`** — Das Handshake-Protokoll ist identisch mit dem Single-Channel Individual Call Protocol, außer dem Trigger-Mechanismus.
- [ ] ⬜ **`AC-SOUND-011-3`** — Das Handshake kann manuell oder automatisch (Bediener oder Controller) durch den Erwerb der Konnektivität vom Sounding getriggert werden.
- [ ] ⬜ **`AC-SOUND-011-4`** — Wenn Stationen Scanning Sounding betreiben und für Anrufe empfänglich sind oder Kontakt erforderlich ist, sollte das optionale Handshake verwendet werden.
- [ ] ⬜ **`AC-SOUND-011-5`** — Die rufende Station sollte unverzüglich den Call initiieren, nachdem die gerufene Station ihre Übertragung beendet hat.
- [ ] ⬜ **`AC-SOUND-011-6`** — Eine Wait-Before-Transmit Time ist für das Handshake nach Sounding nicht erforderlich.
- [ ] ⬜ **`AC-SOUND-011-7`** — Ein Empfänger, der den Sound einer gesuchten Station hört, kann unverzüglich mit dem einfachen Single-Channel Call antworten.

### REQ-SOUND-012 — Sounding: Keine neuen Frequenz- oder Hardware-Erfordernisse (`A.5.3.1 / A.5.3.3`)
- [ ] ⬜ **`AC-SOUND-012-1`** — Sounding verwendet die standardmäßige ALE-Signalisierung.
- [ ] ⬜ **`AC-SOUND-012-2`** — Sounding verwendet die gleichen Frequenzen wie das Scanning.
- [ ] ⬜ **`AC-SOUND-012-3`** — Keine neuen Frequenzen oder speziellen Sounding-Kanäle werden benötigt.
- [ ] ⬜ **`AC-SOUND-012-4`** — Das "Sound Set" ist üblicherweise identisch mit dem "Scan Set".

## 🧪 Tests
- `tests/test_sounding.cpp`

## 💡 Implementierungshinweise

Verbindungsaufbau ausgelöst durch Konnektivitäts-Erkennung beim Sounding per A.5.3.4.

---
## 🤖 Agent-Workflow

```
1. Dieses Issue auf status: in-progress setzen
2. Alle module-Pfade lesen
3. Für jede Spec-Referenz: MIL-STD-188-141B Appendix A, Abschnitt X lesen
4. Nur implementieren, was in den Acceptance Criteria steht
5. Für jedes AC mindestens einen Test schreiben
6. ctest ausführen — alle Tests müssen grün sein
7. Alle AC-Checkboxen abhaken → status: done
```
