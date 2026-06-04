# FEAT-ADDR-002 — Adress-Chunking, Stuffing & Erweiterung

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `ADDR`

## 📁 Module
- `src/ale_word.cpp`
- `src/ale_state_machine.cpp`

## 🔗 Depends on
- 🔍 `FEAT-ADDR-001` — Basic-38-Zeichensatz & Adressvalidierung

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-ADDR-003 — Stuffing nicht vollständiger Adressen
**Spec:** `A.5.2.4.3` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Adressen, deren Länge kein Vielfaches von drei Zeichen ist, müssen kompatibel in standardisierte Adressfelder aufgenommen werden, indem die freien nachlaufenden Positionen mit dem Utility-Zeichen „@“ aufgefüllt werden. Die Wörter „Stuff-1“ und „Stuff-2“ dürfen nur im letzten Wort einer Adresse verwendet werden und sollen daher nur im führenden Ruf des Calling Cycles erscheinen.

### REQ-ADDR-004 — Einzeladresse als grundlegendes Adresselement
**Spec:** `A.5.2.4.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das fundamentale Adressenelement im ALE-System ist das einzelne Routing-Wort mit drei Zeichen, das die grundlegende individuelle Stationsadresse bildet. Eine Adresse, die einer einzelnen Station innerhalb des bekannten oder verwendeten Netzes zugeordnet ist, muss als individuelle Adresse bezeichnet werden. Besteht sie aus einem Wort, muss sie als Basisgröße bezeichnet werden; überschreitet sie ein Wort, muss sie als erweiterte Größe bezeichnet werden. Das grundlegende Adresswort kann für Intranet- und Slotted-Betrieb verwendet werden und kann für Internet- und allgemeinen Gebrauch zu mehreren Wörtern erweitert werden.

### REQ-ADDR-005 — Standard-Adressmuster und Sonderruf-Varianten
**Spec:** `Table A-IX / A.5.2.4.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Die im Standard aufgeführten Adressmuster definieren die zulässigen Standard-, Stuffing-, AllCall-, selective AllCall-, AnyCall-, selective AnyCall-, double selective AnyCall- und Null-Adressformen. Nicht dargestellte Muster sind reserviert und müssen bis zur Standardisierung als ungültig behandelt werden. Das Utility-Zeichen „@“ steht für das spezielle Utility-Zeichen, das Wildcard-Zeichen „?“ für einen Platzhalter. Die Bezeichner A, B, C und D stehen jeweils für ein alphanumerisches Zeichen des Basic-38-ASCII-Zeichensatzes, das weder „@“ noch „?“ ist.

### REQ-ADDR-006 — Basisgröße der Einzeladresse
**Spec:** `A.5.2.4.4.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Eine Basisadresse muss aus einem Routing-Präambelwort und drei Adresszeichen aus dem Basic-38-ASCII-Zeichensatz bestehen. Eine solche dreistellige Einzelwortadresse ist die minimale Struktur. Das Standardwerk gibt für eine dreistellige individuelle Adresse eine Basic-38-Adresskapazität von 46.656 an. Alle ALE-Stationen müssen mit spezifischen Zeit- und Steuerinformationen für alle eigenen Adressen arbeiten, beispielsweise mit vorab festgelegten Verzögerungen für slotted Netzantworten. Alle ALE-Stationen müssen mindestens eine einwortige Adresse für den automatischen Einsatz in einwortigen Adressprotokollen erhalten; dies ist eine zwingende Benutzeranforderung. Die Verwendung längerer Adressen darf durch das Design nicht ausgeschlossen werden.

### REQ-ADDR-007 — Erweiterte Adresse bis fünf Wörter
**Spec:** `A.5.2.4.4.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Erweiterte Adressen dürfen länger als ein Wort sein und dürfen das systemweite Maximum von fünf Wörtern beziehungsweise 15 Zeichen nicht überschreiten. Die 15-Zeichen-Kapazität ermöglicht eine ISDN-Adressfähigkeit. Eine erweiterte ALE-Adresse muss aus einem initialen Basisadresswort, beispielsweise TO oder TIS, sowie aus zusätzlichen Wörtern bestehen, die die weiteren Zeichen in der Folge DATA, REP, DATA, REP aufnehmen. Alle Adresszeichen müssen alphanumerische Mitglieder des Basic-38-ASCII-Zeichensatzes sein.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-ADDR-003 — Stuffing nicht vollständiger Adressen (`A.5.2.4.3`)
- [ ] ⬜ **`AC-ADDR-003-1`** — Nicht durch drei teilbare Adresslängen werden durch Auffüllen der nachlaufenden Positionen mit „@“ in standardisierte Adressfelder überführt.
- [ ] ⬜ **`AC-ADDR-003-2`** — Stuff-1 und Stuff-2 werden nur im letzten Wort einer Adresse verwendet.
- [ ] ⬜ **`AC-ADDR-003-3`** — Stuff-1 und Stuff-2 erscheinen nur im führenden Ruf des Calling Cycles.

### REQ-ADDR-004 — Einzeladresse als grundlegendes Adresselement (`A.5.2.4.4`)
- [ ] ⬜ **`AC-ADDR-004-1`** — Eine individuelle Stationsadresse basiert auf genau einem Routing-Wort mit drei Zeichen.
- [ ] ⬜ **`AC-ADDR-004-2`** — Eine einer einzelnen Station zugeordnete Adresse wird als individuelle Adresse bezeichnet.
- [ ] ⬜ **`AC-ADDR-004-3`** — Eine einwortige individuelle Adresse wird als Basisgröße bezeichnet.
- [ ] ⬜ **`AC-ADDR-004-4`** — Eine mehr als ein Wort umfassende individuelle Adresse wird als erweiterte Größe bezeichnet.
- [ ] ⬜ **`AC-ADDR-004-5`** — Das grundlegende Adresswort kann für Intranet- und Slotted-Betrieb verwendet werden.
- [ ] ⬜ **`AC-ADDR-004-6`** — Das grundlegende Adresswort kann für Internet- und allgemeinen Gebrauch erweitert werden.

### REQ-ADDR-005 — Standard-Adressmuster und Sonderruf-Varianten (`Table A-IX / A.5.2.4.4`)
- [ ] ⬜ **`AC-ADDR-005-1`** — Nicht dargestellte Muster werden als reserviert und ungültig behandelt, bis sie standardisiert sind.
- [ ] ⬜ **`AC-ADDR-005-2`** — „@“ wird als Utility-Zeichen behandelt.
- [ ] ⬜ **`AC-ADDR-005-3`** — „?“ wird als Wildcard-Zeichen behandelt.
- [ ] ⬜ **`AC-ADDR-005-4`** — A, B, C und D stehen nur für Zeichen aus dem Basic-38-ASCII-Zeichensatz, die weder „@“ noch „?“ sind.

### REQ-ADDR-006 — Basisgröße der Einzeladresse (`A.5.2.4.4.1`)
- [ ] ⬜ **`AC-ADDR-006-1`** — Eine Basisadresse besteht aus einem Routing-Präambelwort und drei Adresszeichen.
- [ ] ⬜ **`AC-ADDR-006-2`** — Eine dreistellige Einzelwortadresse ist die minimale Struktur.
- [ ] ⬜ **`AC-ADDR-006-3`** — Für die dreistellige individuelle Adresse ist eine Basic-38-Adresskapazität von 46.656 gegeben.
- [ ] ⬜ **`AC-ADDR-006-4`** — Alle eigenen Adressen sind mit spezifischen Zeit- und Steuerinformationen verknüpft.
- [ ] ⬜ **`AC-ADDR-006-5`** — Jede ALE-Station besitzt mindestens eine einwortige Adresse für einwortige Adressprotokolle.
- [ ] ⬜ **`AC-ADDR-006-6`** — Längere Adressen werden durch das Design nicht ausgeschlossen.

### REQ-ADDR-007 — Erweiterte Adresse bis fünf Wörter (`A.5.2.4.4.2`)
- [ ] ⬜ **`AC-ADDR-007-1`** — Erweiterte Adressen können länger als ein Wort sein.
- [ ] ⬜ **`AC-ADDR-007-2`** — Erweiterte Adressen überschreiten nicht fünf Wörter.
- [ ] ⬜ **`AC-ADDR-007-3`** — Erweiterte Adressen überschreiten nicht 15 Zeichen.
- [ ] ⬜ **`AC-ADDR-007-4`** — Eine erweiterte Adresse beginnt mit einem Basisadresswort wie TO oder TIS.
- [ ] ⬜ **`AC-ADDR-007-5`** — Weitere Zeichen werden über zusätzliche Wörter in der Folge DATA, REP, DATA, REP aufgenommen.
- [ ] ⬜ **`AC-ADDR-007-6`** — Alle Adresszeichen stammen aus dem Basic-38-ASCII-Zeichensatz.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

chunk_address(): i+=3, '@'-Pad. Max 15 Zeichen = 5 Wörter. Sequenz TO→DATA→REP→DATA→REP.

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
