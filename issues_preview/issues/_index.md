# PC-ALE — GitHub Issues Preview

Generiert aus `IMPLEMENTATION_BACKLOG.yml` + `REQUIREMENTS.md`  
**38 Features** | AC-Texte vollständig aus MIL-STD-188-141B

## Legende

| Icon | Status |
|------|--------|
| ✅ | `done` |
| ✅ | `verified` |
| 🔄 | `in-progress` |
| 🔍 | `validate` |
| ⬜ | `todo` |
| 🚫 | `blocked` |
| ⏭️ | `skip` |

---

## WAVEFORM

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| ✅ `done` | [FEAT-WAVEFORM-001 — Tone-Symbol-Mapping & Frequenztabelle](./FEAT-WAVEFORM-001.md) | 🔴 `MUST` | 7 | — |
| ✅ `verified` | [FEAT-WAVEFORM-002 — NCO-Tongenerator mit Phasenkontinuität](./FEAT-WAVEFORM-002.md) | 🔴 `MUST` | 4 | `FEAT-WAVEFORM-001` |
| 🔍 `validate` | [FEAT-WAVEFORM-003 — Timing-Konstanten & Wortgrenzen](./FEAT-WAVEFORM-003.md) | 🔴 `MUST` | 8 | `FEAT-WAVEFORM-001` |
| ⬜ `todo` | [FEAT-WAVEFORM-004 — Genauigkeits-Verifikation (Frequenz, Amplitude, Timing)](./FEAT-WAVEFORM-004.md) | 🔴 `MUST` | 5 | `FEAT-WAVEFORM-002`, `FEAT-WAVEFORM-003` |

## WORD

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| 🔄 `in-progress` | [FEAT-WORD-001 — word24 Bit-Layout Encoding/Decoding](./FEAT-WORD-001.md) | 🔴 `MUST` | 9 | — |
| 🔍 `validate` | [FEAT-WORD-002 — Adresswörter (TO / TIS / TWAS / THRU / FROM)](./FEAT-WORD-002.md) | 🔴 `MUST` | 34 | `FEAT-WORD-001` |
| ⬜ `todo` | [FEAT-WORD-003 — Message & Extension Words (CMD / DATA / REP)](./FEAT-WORD-003.md) | 🔴 `MUST` | 18 | `FEAT-WORD-001` |

## FEC

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| 🔍 `validate` | [FEAT-FEC-001 — Golay (24,12) Encoder](./FEAT-FEC-001.md) | 🔴 `MUST` | 9 | `FEAT-WORD-001` |
| 🔍 `validate` | [FEAT-FEC-002 — Golay (24,12) Decoder mit Fehlerkorrektur](./FEAT-FEC-002.md) | 🔴 `MUST` | 5 | `FEAT-FEC-001` |
| 🔍 `validate` | [FEAT-FEC-003 — Interleaving / Deinterleaving](./FEAT-FEC-003.md) | 🔴 `MUST` | 7 | `FEAT-FEC-001`, `FEAT-FEC-002` |
| ⬜ `todo` | [FEAT-FEC-004 — 3x Redundanz mit Majority-Vote (RX)](./FEAT-FEC-004.md) | 🔴 `MUST` | 12 | `FEAT-FEC-003` |
| ⬜ `todo` | [FEAT-FEC-005 — Unanimous-Votes-Erfassung](./FEAT-FEC-005.md) | 🔴 `MUST` | 2 | `FEAT-FEC-004` |

## FRAME

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| 🔍 `validate` | [FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis](./FEAT-FRAME-001.md) | 🔴 `MUST` | 4 | `FEAT-WORD-001`, `FEAT-FEC-003` |
| 🔍 `validate` | [FEAT-FRAME-002 — Scanning Call (Tsc-Phase)](./FEAT-FRAME-002.md) | 🔴 `MUST` | 13 | `FEAT-FRAME-001`, `FEAT-ADDR-002` |
| 🔍 `validate` | [FEAT-FRAME-003 — Leading Call (Tlc-Phase)](./FEAT-FRAME-003.md) | 🔴 `MUST` | 3 | `FEAT-FRAME-002`, `FEAT-ADDR-002` |
| ⬜ `todo` | [FEAT-FRAME-004 — Message-Abschnitt](./FEAT-FRAME-004.md) | 🔴 `MUST` | 11 | `FEAT-FRAME-003` |
| 🔍 `validate` | [FEAT-FRAME-005 — Conclusion](./FEAT-FRAME-005.md) | 🔴 `MUST` | 7 | `FEAT-FRAME-003` |
| ⬜ `todo` | [FEAT-FRAME-006 — Gültige Sequenzen & Frame-Limits](./FEAT-FRAME-006.md) | 🔴 `MUST` | 9 | `FEAT-FRAME-001` |

## SYNC

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| 🔍 `validate` | [FEAT-SYNC-001 — Trw-Grid (Sendeseitiger Wortphasen-Anker)](./FEAT-SYNC-001.md) | 🔴 `MUST` | 11 | `FEAT-FRAME-001` |
| ⬜ `todo` | [FEAT-SYNC-002 — Empfangsseitige Wortsynchronisation](./FEAT-SYNC-002.md) | 🔴 `MUST` | 4 | `FEAT-SYNC-001`, `FEAT-FEC-005` |
| ⬜ `todo` | [FEAT-SYNC-003 — Synchronisationskriterien & Schwellwerte](./FEAT-SYNC-003.md) | 🔴 `MUST` | 79 | `FEAT-SYNC-002`, `FEAT-FEC-005` |

## SOUND

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| ⬜ `todo` | [FEAT-SOUND-001 — Single-Channel Sounding](./FEAT-SOUND-001.md) | 🟢 `COULD` | 21 | `FEAT-FRAME-005`, `FEAT-ADDR-002` |
| ⬜ `todo` | [FEAT-SOUND-002 — Multi-Channel Sounding](./FEAT-SOUND-002.md) | 🟢 `COULD` | 29 | `FEAT-SOUND-001` |
| ⬜ `todo` | [FEAT-SOUND-003 — Optionales Handshake nach Sounding](./FEAT-SOUND-003.md) | 🟢 `COULD` | 18 | `FEAT-SOUND-001` |

## CHAN

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| ⬜ `todo` | [FEAT-CHAN-001 — LQA-Messung (BER, SINAD, MP)](./FEAT-CHAN-001.md) | 🔴 `MUST` | 29 | `FEAT-FEC-005` |
| ⬜ `todo` | [FEAT-CHAN-002 — LQA CMD Reporting](./FEAT-CHAN-002.md) | 🔴 `MUST` | 22 | `FEAT-CHAN-001` |
| ⬜ `todo` | [FEAT-CHAN-003 — Kanalauswahl-Algorithmen](./FEAT-CHAN-003.md) | 🔴 `MUST` | 13 | `FEAT-CHAN-001` |
| ⬜ `todo` | [FEAT-CHAN-004 — Listen Before Transmit](./FEAT-CHAN-004.md) | 🔴 `MUST` | 11 | `FEAT-CHAN-001` |

## LINK

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| ⬜ `todo` | [FEAT-LINK-001 — FSK Symbol-Detektor (RX-Pfad)](./FEAT-LINK-001.md) | 🔴 `MUST` | 1 | `FEAT-WAVEFORM-001`, `FEAT-WAVEFORM-002` |
| 🔍 `validate` | [FEAT-LINK-002 — Individual & Net Call TX](./FEAT-LINK-002.md) | 🔴 `MUST` | 1 | `FEAT-FRAME-005`, `FEAT-ADDR-002` |
| ⬜ `todo` | [FEAT-LINK-003 — Group Call TX](./FEAT-LINK-003.md) | 🟡 `SHOULD` | 2 | `FEAT-LINK-002`, `FEAT-ADDR-003` |
| ⬜ `todo` | [FEAT-LINK-004 — End-of-Frame-Erkennung](./FEAT-LINK-004.md) | 🔴 `MUST` | 1 | `FEAT-LINK-001`, `FEAT-FEC-004` |
| 🔍 `validate` | [FEAT-LINK-005 — Adress-Präfix-Matching](./FEAT-LINK-005.md) | 🔴 `MUST` | 231 | `FEAT-WORD-001` |

## ADDR

| Status | Feature | Priorität | ACs | Depends on |
|--------|---------|-----------|-----|------------|
| 🔍 `validate` | [FEAT-ADDR-001 — Basic-38-Zeichensatz & Adressvalidierung](./FEAT-ADDR-001.md) | 🔴 `MUST` | 14 | `FEAT-WORD-001` |
| 🔍 `validate` | [FEAT-ADDR-002 — Adress-Chunking, Stuffing & Erweiterung](./FEAT-ADDR-002.md) | 🔴 `MUST` | 25 | `FEAT-ADDR-001` |
| ⬜ `todo` | [FEAT-ADDR-003 — Net, Group, AllCall, AnyCall Adressen](./FEAT-ADDR-003.md) | 🔴 `MUST` | 22 | `FEAT-ADDR-002` |
| 🔍 `validate` | [FEAT-ADDR-004 — Wildcard-Matching](./FEAT-ADDR-004.md) | 🔴 `MUST` | 5 | `FEAT-ADDR-001` |
| ⬜ `todo` | [FEAT-ADDR-005 — Self, Null, In-Link Adressen](./FEAT-ADDR-005.md) | 🔴 `MUST` | 16 | `FEAT-ADDR-002` |

---

## Status-Übersicht

| Status | Anzahl |
|--------|--------|
| ✅ `done` | 1 |
| ✅ `verified` | 1 |
| 🔄 `in-progress` | 1 |
| 🔍 `validate` | 15 |
| ⬜ `todo` | 20 |

