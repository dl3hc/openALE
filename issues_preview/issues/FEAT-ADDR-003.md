# FEAT-ADDR-003 — Net, Group, AllCall, AnyCall Adressen

**Status:** ⬜ `todo` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `ADDR`

## 📁 Module
- `src/ale_state_machine.cpp`
- `src/ale_word.cpp`

## 🔗 Depends on
- 🔍 `FEAT-ADDR-002` — Adress-Chunking, Stuffing & Erweiterung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-ADDR-008 — Netzruf mit gemeinsamer Netadresse
**Spec:** `A.5.2.4.5` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein Netzruf muss dazu dienen, schnell und effizient Kontakt mit mehreren vorab vereinbarten Net-Stationen herzustellen, möglichst gleichzeitig, unter Verwendung einer einzigen Netadresse, die allen Netmitgliedern gemeinsam zugewiesen ist. Wenn eine Netadressfunktion erforderlich ist, muss die rufende ALE-Station eine Adressstruktur verwenden, die der individuellen Stationsadresse entspricht und bei Bedarf basic oder extended sein darf. Für jede Netadresse an einer Netmitgliedsstation muss ein Response-Slot-Identifikator vorhanden sein; ein Slot-Width-Modifikator muss zusätzlich vorhanden sein, wenn dies durch das spezifische Standardprotokoll vorgegeben ist. Zusätzliche Informationen über die zugewiesenen Response-Slots und deren Größe müssen verfügbar sein. Das Mischen von individuellen, Netz- und Gruppenadressen sowie -rufen ist eingeschränkt.

### REQ-ADDR-009 — Gruppenruf mit individuellen Zieladressen
**Spec:** `A.5.2.4.6` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein Gruppenruf muss dazu dienen, schnell und effizient Kontakt mit mehreren nicht vorab vereinbarten Gruppen-Stationen herzustellen, möglichst gleichzeitig, unter Verwendung einer kompakten Kombination ihrer jeweils individuell zugewiesenen Adressen. Wenn eine Gruppenadressfunktion erforderlich ist, muss die rufende ALE-Station eine Folge der tatsächlichen individuellen Stationsadressen der gerufenen Stationen verwenden. Die Reihenfolge muss dem durch das spezifische Standardprotokoll vorgegebenen Verfahren entsprechen. Die Adresse einer Station darf in einer Gruppenruf-Folge nicht mehr als einmal erscheinen, außer wenn dies durch die Gruppenrufprotokolle ausdrücklich erlaubt ist.

### REQ-ADDR-010 — Globaler AllCall und selective AllCall
**Spec:** `A.5.2.4.7` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein AllCall muss ein allgemeiner Broadcast sein, der keine Antworten anfordert und keine bestimmte Adresse bezeichnet. Er muss für Notfälle, Broadcast-Datenaustausch sowie Propagations- und Konnektivitäts-Tracking verwendet werden können. Die globale AllCall-Adresse muss „@?@“ sein. Ein selective AllCall muss in Struktur, Funktion und Protokoll dem AllCall entsprechen, mit der Ausnahme, dass er das letzte einzelne Zeichen der Adressen der gewünschten Empfängergruppe festlegt. Wenn mehr als eine Teilmenge gewünscht ist, darf die selective-AllCall-Adressierung auch mit einer alternativen Adresse über die THRU/REP-Folge erfolgen. Das Zeichen, das ersetzt wird, muss ein alphanumerisches Zeichen des Basic-38-Satzes sein; die ausgewählten Zeichen bestimmen, welche Stationen das Scannen beenden und zuhören.

### REQ-ADDR-011 — Globaler AnyCall, selective AnyCall und double selective AnyCall
**Spec:** `A.5.2.4.8` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Ein AnyCall muss ein allgemeiner Broadcast sein, der Antworten anfordert, ohne bestimmte Empfänger zu benennen. Er muss für Notfälle, die Rekonstitution von Systemen und den Aufbau neuer Netze verwendet werden können. Die globale AnyCall-Adresse muss „@@?“ sein. Wenn zu viele Antworten eingehen oder die verfügbaren, aber nicht spezifizierten Antwortenden in logische Teilmengen organisiert werden müssen, muss ein selective AnyCall verwendet werden. Ein selective AnyCall muss in Struktur, Funktion und Protokoll dem globalen AnyCall entsprechen, mit der Ausnahme, dass er das letzte einzelne Zeichen der Adresse der gewünschten Empfängergruppe festlegt. Wenn noch engere Akzeptanz- und Antwortkriterien erforderlich sind, muss ein double selective AnyCall verwendet werden. Der double selective AnyCall muss eine operatorgewählte allgemeine Broadcast-Form sein, die dem selective AnyCall entspricht, aber in der Form „@AB“ die letzten zwei Zeichen festlegt, die die gewünschte Empfängergruppe besitzen muss, um eine Antwort auszulösen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-ADDR-008 — Netzruf mit gemeinsamer Netadresse (`A.5.2.4.5`)
- [ ] ⬜ **`AC-ADDR-008-1`** — Ein Netzruf verwendet eine einzige gemeinsam zugewiesene Netadresse.
- [ ] ⬜ **`AC-ADDR-008-2`** — Die rufende Station verwendet eine Adressstruktur, die der individuellen Stationsadresse entspricht.
- [ ] ⬜ **`AC-ADDR-008-3`** — Eine Netmitgliedsstation besitzt je Netadresse einen Response-Slot-Identifikator.
- [ ] ⬜ **`AC-ADDR-008-4`** — Ein Slot-Width-Modifikator ist vorhanden, wenn das Standardprotokoll dies vorgibt.
- [ ] ⬜ **`AC-ADDR-008-5`** — Zusätzliche Informationen zu zugewiesenen Response-Slots und deren Größe sind verfügbar.
- [ ] ⬜ **`AC-ADDR-008-6`** — Das Mischen von individuellen, Netz- und Gruppenadressen sowie -rufen ist eingeschränkt.

### REQ-ADDR-009 — Gruppenruf mit individuellen Zieladressen (`A.5.2.4.6`)
- [ ] ⬜ **`AC-ADDR-009-1`** — Ein Gruppenruf verwendet mehrere nicht vorab vereinbarte Stationsadressen in einer kompakten Kombination.
- [ ] ⬜ **`AC-ADDR-009-2`** — Die rufende Station verwendet die tatsächlichen individuellen Stationsadressen der gerufenen Stationen.
- [ ] ⬜ **`AC-ADDR-009-3`** — Die Reihenfolge folgt dem spezifischen Standardprotokoll.
- [ ] ⬜ **`AC-ADDR-009-4`** — Eine Stationsadresse erscheint in einer Gruppenruf-Folge nicht mehr als einmal, außer wenn das Gruppenrufprotokoll dies ausdrücklich erlaubt.

### REQ-ADDR-010 — Globaler AllCall und selective AllCall (`A.5.2.4.7`)
- [ ] ⬜ **`AC-ADDR-010-1`** — Ein AllCall fordert keine Antworten an.
- [ ] ⬜ **`AC-ADDR-010-2`** — Ein AllCall bezeichnet keine spezifische Adresse.
- [ ] ⬜ **`AC-ADDR-010-3`** — Die globale AllCall-Adresse ist „@?@“.
- [ ] ⬜ **`AC-ADDR-010-4`** — Ein selective AllCall legt das letzte einzelne Zeichen der Zieladressen fest.
- [ ] ⬜ **`AC-ADDR-010-5`** — Bei mehr als einer Teilmenge darf die selective-AllCall-Adressierung eine THRU/REP-Folge verwenden.
- [ ] ⬜ **`AC-ADDR-010-6`** — Die ausgewählten Zeichen bestimmen, welche Stationen das Scannen beenden und zuhören.

### REQ-ADDR-011 — Globaler AnyCall, selective AnyCall und double selective AnyCall (`A.5.2.4.8`)
- [ ] ⬜ **`AC-ADDR-011-1`** — Ein AnyCall fordert Antworten an.
- [ ] ⬜ **`AC-ADDR-011-2`** — Ein AnyCall benennt keine bestimmten Empfänger.
- [ ] ⬜ **`AC-ADDR-011-3`** — Die globale AnyCall-Adresse ist „@@?“.
- [ ] ⬜ **`AC-ADDR-011-4`** — Ein selective AnyCall legt das letzte einzelne Zeichen der Zieladressen fest.
- [ ] ⬜ **`AC-ADDR-011-5`** — Ein double selective AnyCall legt die letzten zwei Zeichen der Zieladressen fest.
- [ ] ⬜ **`AC-ADDR-011-6`** — Ein double selective AnyCall wird verwendet, wenn engere Akzeptanz- und Antwortkriterien erforderlich sind.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

AllCall='@?@', AnyCall='@@?'. Netz-Adresse mit TO. Group nutzt THRU/REP. Slot-Identifikatoren für Netz.

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
