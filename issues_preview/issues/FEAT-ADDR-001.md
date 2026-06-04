# FEAT-ADDR-001 — Basic-38-Zeichensatz & Adressvalidierung

**Status:** 🔍 `validate` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST` &nbsp;|&nbsp; **Domain:** `ADDR`

## 📁 Module
- `src/ale_word.cpp`
- `include/ale_word.h`

## 🔗 Depends on
- 🔄 `FEAT-WORD-001` — word24 Bit-Layout Encoding/Decoding

## 📋 Spezifikation (MIL-STD-188-141B)

### REQ-ADDR-001 — Digitale Adressstruktur und Speicherkapazität
**Spec:** `A.5.2.4.1` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das ALE-System muss eine digitale Adressstruktur auf Basis des standardisierten 24-Bit-Worts und des Basic-38-Zeichensatzes verwenden. ALE-Stationen müssen die Fähigkeit besitzen, mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen einzeln oder gemeinsam zu verlinken oder zu vernetzen. Alle ALE-Stationen müssen mindestens 20 eigene Adressen mit jeweils bis zu 15 Zeichen in beliebiger Kombination aus Einzel- und Netzrufen speichern und verwenden können. Das Standardwerk unterscheidet drei grundlegende Adressierungsarten: Einzelstation, Mehrfachstation und Sondermodi. Bestimmte alphanumerische Adresskombinationen können eine besondere Bedeutung für Notfälle oder spezifische Funktionen haben; solche Kombinationen sollten sorgfältig kontrolliert oder eingeschränkt werden.

### REQ-ADDR-002 — Basic-38-Zeichensatz und gültige Basisadresse
**Spec:** `A.5.2.4.2` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Der Basic-38-ASCII-Zeichensatz muss alle Großbuchstaben A bis Z und alle Ziffern 0 bis 9 sowie die Sonderzeichen „@“ und „?“ enthalten. Der Basic-38-ASCII-Zeichensatz muss für alle grundlegenden Adressierungsfunktionen verwendet werden. Eine gültige Basisadresse muss ein Routing-Präambelwort aus A.5.2.3.2 sowie drei alphanumerische Zeichen aus dem Basic-38-ASCII-Zeichensatz in beliebiger Kombination enthalten. Die Sonderzeichen „@“ und „?“ müssen für spezielle Funktionen verwendet werden. Die Unterscheidung des Basic-38-ASCII-Zeichensatzes darf nicht darauf beschränkt sein, nur die drei höchstwertigen Bits zu prüfen.

### REQ-ADDR-016 — Adressvalidierung und Kompatibilitätsregeln
**Spec:** `A.5.2.4` &nbsp;|&nbsp; **Priorität:** 🔴 `MUST`

> Das ALE-System muss empfangene Adressen validieren und sicherstellen, dass nur gültige Adressmuster gemäß dem Basic-38-Zeichensatz und den definierten Adressstrukturen verarbeitet werden. Reservierte oder unbekannte Adressmuster müssen als ungültig behandelt werden. ALE-Stationen müssen rückwärtskompatibel mit Implementierungen arbeiten, die nur Basisadressen unterstützen.


## ✅ Acceptance Criteria

> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün + kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning

### REQ-ADDR-001 — Digitale Adressstruktur und Speicherkapazität (`A.5.2.4.1`)
- [ ] ⬜ **`AC-ADDR-001-1`** — Das System verwendet eine digitale Adressstruktur auf Basis des 24-Bit-Worts und des Basic-38-Zeichensatzes.
- [ ] ⬜ **`AC-ADDR-001-2`** — Eine Station kann mit einer oder mehreren vorab vereinbarten oder bedarfsabhängigen Stationen verlinken oder vernetzen.
- [ ] ⬜ **`AC-ADDR-001-3`** — Das System kann mindestens 20 eigene Adressen verwalten.
- [ ] ⬜ **`AC-ADDR-001-4`** — Jede dieser eigenen Adressen kann bis zu 15 Zeichen lang sein.
- [ ] ⬜ **`AC-ADDR-001-5`** — Das System unterstützt Einzelstation-, Mehrfachstation- und Sondermodi der Adressierung.
- [ ] ⬜ **`AC-ADDR-001-6`** — Adresskombinationen mit Sonderbedeutung werden kontrolliert oder eingeschränkt behandelt.

### REQ-ADDR-002 — Basic-38-Zeichensatz und gültige Basisadresse (`A.5.2.4.2`)
- [ ] ⬜ **`AC-ADDR-002-1`** — Der Basic-38-ASCII-Zeichensatz umfasst A bis Z, 0 bis 9, „@“ und „?“.
- [ ] ⬜ **`AC-ADDR-002-2`** — Der Basic-38-ASCII-Zeichensatz wird für alle grundlegenden Adressierungsfunktionen verwendet.
- [ ] ⬜ **`AC-ADDR-002-3`** — Eine gültige Basisadresse enthält ein Routing-Präambelwort und drei Zeichen aus dem Basic-38-ASCII-Zeichensatz.
- [ ] ⬜ **`AC-ADDR-002-4`** — Die Sonderzeichen „@“ und „?“ werden für spezielle Funktionen verwendet.
- [ ] ⬜ **`AC-ADDR-002-5`** — Die Erkennung von Basic-38-Zeichen darf nicht allein auf den drei höchstwertigen Bits beruhen.

### REQ-ADDR-016 — Adressvalidierung und Kompatibilitätsregeln (`A.5.2.4`)
- [ ] ⬜ **`AC-ADDR-016-1`** — Empfangene Adressen werden gegen den Basic-38-Zeichensatz und die definierten Adressstrukturen validiert.
- [ ] ⬜ **`AC-ADDR-016-2`** — Reservierte oder unbekannte Adressmuster werden als ungültig verworfen.
- [ ] ⬜ **`AC-ADDR-016-3`** — Das System arbeitet rückwärtskompatibel mit Implementierungen, die nur Basisadressen unterstützen.

## 🧪 Tests
- `tests/test_protocol.cpp`

## 💡 Implementierungshinweise

A-Z, 0-9, '@', '?'. Raw 7-bit ASCII. is_valid_ale_char(). Validierung bei encode und decode.

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
