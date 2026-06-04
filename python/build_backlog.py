#!/usr/bin/env python3
"""
build_backlog.py
Leitet IMPLEMENTATION_BACKLOG.yml aus REQUIREMENTS_INDEX.yml ab.
Bestehende Feature-Einträge (module, notes, tests, depends_on) bleiben erhalten.
Alle Feature-Status werden bewusst auf "todo" gesetzt — der Agent muss
jeden Eintrag selbst verifizieren.
"""
import re, yaml, sys, os, datetime
from copy import deepcopy

ROOT  = os.path.dirname(os.path.abspath(__file__))
INDEX = os.path.join(ROOT, 'REQUIREMENTS_INDEX.yml')
OLD   = os.path.join(ROOT, 'IMPLEMENTATION_BACKLOG.yml')
DST   = os.path.join(ROOT, 'IMPLEMENTATION_BACKLOG.yml')

# ---------------------------------------------------------------------------
# 1. INDEX einlesen
# ---------------------------------------------------------------------------
with open(INDEX, 'r', encoding='utf-8') as f:
    raw = f.read()
# YAML-Kommentare entfernen
raw_no_comments = re.sub(r'#[^\n]*', '', raw)
idx = yaml.safe_load(raw_no_comments)
all_reqs = idx['requirements']

# ---------------------------------------------------------------------------
# 2. Bestehenden Backlog einlesen (für module / notes / tests / depends_on)
# ---------------------------------------------------------------------------
with open(OLD, 'r', encoding='utf-8') as f:
    old_raw = f.read()
old_no_comments = re.sub(r'#[^\n]*', '', old_raw)
old_data = yaml.safe_load(old_no_comments)
old_features = {f['id']: f for f in old_data.get('features', [])}

# ---------------------------------------------------------------------------
# 3. Feature-Mapping: REQ-Bereich → FEAT-Gruppen
#    Jede FEAT-Gruppe bündelt verwandte Requirements.
#    GEN-Einträge sind neu; alle anderen aus dem bestehenden Backlog übernommen.
# ---------------------------------------------------------------------------

# Format: (feat_id, title, [req_ids], priority, depends_on, module, notes, test_file)
FEATURE_PLAN = [
    # ── GEN (neu aus A.4) ──────────────────────────────────────────────────
    ('FEAT-GEN-001',
     'ALE Data Link Schichtstruktur & Grundbetrieb',
     ['REQ-GEN-001', 'REQ-GEN-002', 'REQ-GEN-003', 'REQ-GEN-004', 'REQ-GEN-005'],
     'MUST', [],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'ALE-Sublayer / FEC-Sublayer / LP-Sublayer Schichtgrenzen. Scanning-Stop-Logik (Controller, manuell, extern). Adressierungsstruktur A.5.2.4. Channel-Quality-Anzeige 0-30 SINAD.',
     'tests/test_gen_basics.cpp'),

    ('FEAT-GEN-002',
     'Scan-Raten & AQC-Rückwärtskompatibilität',
     ['REQ-GEN-006', 'REQ-GEN-007', 'REQ-GEN-008'],
     'MUST', ['FEAT-GEN-001'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Selektierbare Scan-Raten 2 und 5 ch/s (MUST). AQC variable Verweilzeiten (COULD). AQC Rückwärtskompatibilität 2/5 ch/s (SHOULD).',
     'tests/test_gen_scanning.cpp'),

    ('FEAT-GEN-003',
     'Belegtheitserkennung & Linking-Wahrscheinlichkeit (System-Tests)',
     ['REQ-GEN-009', 'REQ-GEN-010', 'REQ-GEN-011', 'REQ-GEN-012'],
     'MUST', ['FEAT-GEN-002', 'FEAT-CHAN-001'],
     ['tests/test_gen_performance.cpp'],
     'Tabelle A-I Occupancy Detection (AWGN, Falschalarmrate <=1%). Tabelle A-II Linking Probability (CCIR Good/Poor, Doppler +60Hz). AQC Linking-Wahrscheinlichkeit und LP-Level 1/2.',
     'tests/test_gen_performance.cpp'),

    ('FEAT-GEN-004',
     'Kanalspeicher (Channel Memory, A.4.3.1)',
     ['REQ-GEN-013'],
     'MUST', ['FEAT-GEN-001'],
     ['src/ale_data_store.cpp', 'include/ale_data_store.h'],
     'Min 100 nichtflüchtige Kanaleinträge. Felder: TX/RX-Freq, Modusinfo (Leistung, Nutzung, Sounding, Modulation, T/R-Modus). Abruf manuell/Controller. Recall ändert nicht den gespeicherten Eintrag.',
     'tests/test_gen_data_store.cpp'),

    ('FEAT-GEN-005',
     'Selbstadressspeicher (Self Address Memory, A.4.3.2)',
     ['REQ-GEN-014'],
     'MUST', ['FEAT-GEN-004'],
     ['src/ale_data_store.cpp', 'include/ale_data_store.h'],
     'Min 20 nichtflüchtige Selbstadresssätze. Netz-Member-Adresse mit Slot-Wartezeit Tswt(SN) = Tsw × SN. Einzelruf → sofort antworten. Netz ohne Member → linked-State ohne Antwort.',
     'tests/test_gen_data_store.cpp'),

    ('FEAT-GEN-006',
     'Fremdstations-Tabelle & LQA-Speicher (A.4.3.3)',
     ['REQ-GEN-015', 'REQ-GEN-016', 'REQ-GEN-017', 'REQ-GEN-018'],
     'MUST', ['FEAT-GEN-004'],
     ['src/ale_data_store.cpp', 'include/ale_data_store.h'],
     'Min 100 Fremdstationseinträge. Individuelle Adressen + Twr. Netzinfo (Member-Zuordnung, Slot-Sequenz, Twrn). Nichtflüchtig. LQA-Speicher min 4000 Einträge (DO 10000), 1h Pufferung bei Stromausfall. Bilaterale SINAD + Altersindikator/Gewichtsreduzierung.',
     'tests/test_gen_data_store.cpp'),

    ('FEAT-GEN-007',
     'Betriebsparameter-Programmierbarkeit (A.4.3.4)',
     ['REQ-GEN-019'],
     'MUST', ['FEAT-GEN-004', 'FEAT-GEN-005', 'FEAT-GEN-006'],
     ['src/ale_data_store.cpp', 'include/ale_data_store.h'],
     'Alle 43 Parameter aus A.4.3.4 durch Operator/Controller programmierbar: ScanRate, RequestLQA, OtherAddr, LqaStatus, MaxScanChan, AutoPowerAdj, OtherAddrStatus, LqaAge, MaxTuneTime, SelfAddrTable, TurnAroundTime, SelfAddrEntry, ActivityTimeout, SelfAddr, ListenTime, OtherAddrNetMembers, LqaMultipath, OtherAddrValidChannels, LqaSINAD, OtherAddrAnt, LqaBER, SelfAddrStatus, OtherAddrAntAzimuth, ScanSet, AcceptAnyCall, NetAddr, OtherAddrPower, AcceptAllcall, SlotWaitTime, LqaMatrix, AcceptAMD, SelfAddrValidChannels, LqaEntry, AcceptDTM, AcceptDBM, OtherAddrTable, LqaAddr, OtherAddrEntry, LqaChannel, ConnectionTable, ConnectionEntry, ConnectedAddr, ConnectionStatus.',
     'tests/test_gen_data_store.cpp'),

    ('FEAT-GEN-008',
     'Nachrichtenspeicher (Message Memory, A.4.3.5)',
     ['REQ-GEN-020'],
     'MUST', ['FEAT-GEN-004'],
     ['src/ale_data_store.cpp', 'include/ale_data_store.h'],
     'Min 12 Nachrichten (DO 100), min 1000 Zeichen (DO 10000). Nichtflüchtig für min 1h bei Stromausfall. Vorprogrammierte, Operator-eingabe und eingehende Nachrichten.',
     'tests/test_gen_data_store.cpp'),

    ('FEAT-GEN-009',
     'ALE Betriebsregeln (Operational Rules, A.4.4)',
     ['REQ-GEN-021'],
     'MUST', ['FEAT-GEN-001', 'FEAT-GEN-002', 'FEAT-GEN-006'],
     ['src/ale_state_machine.cpp'],
     'Alle 11 Betriebsregeln in Prioritätsreihenfolge: (1) unabhängige RX-Fähigkeit, (2) immer hören, (3) immer antworten, (4) immer scannen, (5) aktiven Kanal nicht stören (Tabelle A-I), (6) LQA immer austauschen, (7) Slot-Antwort im richtigen Zeitschlitz, (8) Konnektivität suchen/verfolgen, (9) höchste beidseitige Fähigkeit, (10) TX/RX-Zeit minimieren, (11) Leistung automatisch minimieren.',
     'tests/test_gen_operational_rules.cpp'),

    ('FEAT-GEN-010',
     'AQC-ALE Protokoll (A.4.5)',
     ['REQ-GEN-022', 'REQ-GEN-023', 'REQ-GEN-024', 'REQ-GEN-025'],
     'COULD', ['FEAT-GEN-001', 'FEAT-GEN-002'],
     ['src/ale_aqc.cpp', 'include/ale_aqc.h'],
     'AQC immer auf Basis-ALE hören und antworten. 3 Chars → 16 Bit. Max 6 Zeichen. 6 Chars pro Transaktion. FROM→PART2, THRU→INLINK. Adress-/Nachrichtenpräambeln getrennt. Festes Bit verhindert Kollision mit Basis-ALE. Min 8 Info-Bits. LP-Level 0-3, Unit/Star-Net/All/AnyCalls, LQA-Handshake, Orderwire. Kein Gruppenruf, kein AMD/DTM/DBM während Setup, keine frühe FROM-ID.',
     'tests/test_aqc_protocol.cpp'),

    # ── WAVEFORM ────────────────────────────────────────────────────────────
    ('FEAT-WAVEFORM-001',
     'Tone-Symbol-Mapping & Frequenztabelle',
     ['REQ-WAVEFORM-001', 'REQ-WAVEFORM-002', 'REQ-WAVEFORM-003'],
     'MUST', [],
     ['include/ale_types.h'],
     'FREQ_TO_SYMBOL[rank] und TONE_FREQS_HZ[rank] in ale_types.h. static_assert prüft Bijektivität.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-WAVEFORM-002',
     'NCO-Tongenerator mit Phasenkontinuität',
     ['REQ-WAVEFORM-004', 'REQ-WAVEFORM-005'],
     'MUST', ['FEAT-WAVEFORM-001'],
     ['extern/PC-ALE/src/fsk/tone_generator.cpp'],
     '32-Bit NCO, Init 0x40000000 (pi/2). Phasenkontinuität durch geteilten Akkumulator.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-WAVEFORM-003',
     'Timing-Konstanten & Wortgrenzen',
     ['REQ-WAVEFORM-006', 'REQ-WAVEFORM-007', 'REQ-WAVEFORM-008',
      'REQ-WAVEFORM-009', 'REQ-WAVEFORM-010'],
     'MUST', ['FEAT-WAVEFORM-001'],
     ['include/ale_types.h', 'include/ale_state_machine.h'],
     'WORD_DURATION_MS=392, SAMPLES_PER_SYMBOL=64, SYMBOLS_PER_WORD=49 als Konstanten.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-WAVEFORM-004',
     'Genauigkeits-Verifikation (Frequenz, Amplitude, Timing)',
     ['REQ-WAVEFORM-011', 'REQ-WAVEFORM-012', 'REQ-WAVEFORM-013'],
     'MUST', ['FEAT-WAVEFORM-002', 'FEAT-WAVEFORM-003'],
     ['tests/test_tone_accuracy.cpp'],
     'Goertzel-basierte Frequenzmessung +-1Hz; RMS-Vergleich aller 8 Symbole <=2dB; Timing 10ppm.',
     'tests/test_tone_accuracy.cpp'),

    # ── WORD ────────────────────────────────────────────────────────────────
    ('FEAT-WORD-001',
     'word24 Bit-Layout Encoding/Decoding',
     ['REQ-WORD-001', 'REQ-WORD-002'],
     'MUST', [],
     ['src/ale_word.cpp', 'include/ale_word.h'],
     'W1=bit23 (MSB). Preamble [23:21], Char1 [20:14], Char2 [13:7], Char3 [6:0]. Raw 7-bit ASCII.',
     'tests/test_protocol.cpp'),

    ('FEAT-WORD-002',
     'Adresswörter (TO / TIS / TWAS / THRU / FROM)',
     ['REQ-WORD-003', 'REQ-WORD-004', 'REQ-WORD-005', 'REQ-WORD-006', 'REQ-WORD-007'],
     'MUST', ['FEAT-WORD-001'],
     ['src/ale_word.cpp', 'include/ale_word.h'],
     'Alle 5 Adress-Preambles implementiert. TWS/TWAS = Wert 3 (gleicher Bit-Wert).',
     'tests/test_protocol.cpp'),

    ('FEAT-WORD-003',
     'Message & Extension Words (CMD / DATA / REP)',
     ['REQ-WORD-008', 'REQ-WORD-009', 'REQ-WORD-010'],
     'MUST', ['FEAT-WORD-001'],
     ['src/ale_word.cpp', 'include/ale_word.h'],
     'REP darf nicht direkt auf TIS/TWAS folgen. Sequenzregel: TO, DATA, REP, DATA, REP.',
     'tests/test_protocol.cpp'),

    # ── FEC ─────────────────────────────────────────────────────────────────
    ('FEAT-FEC-001',
     'Golay (24,12) Encoder',
     ['REQ-FEC-004', 'REQ-FEC-005', 'REQ-FEC-006', 'REQ-FEC-007', 'REQ-FEC-009'],
     'MUST', ['FEAT-WORD-001'],
     ['extern/PC-ALE/src/fec/golay.cpp', 'extern/PC-ALE/include/ale/golay.h'],
     'Systematischer Extended Golay (24,12,3). Polynom 0xAE3. Syndromtabelle 4096 Einträge.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-FEC-002',
     'Golay (24,12) Decoder mit Fehlerkorrektur',
     ['REQ-FEC-008', 'REQ-FEC-010', 'REQ-FEC-011'],
     'MUST', ['FEAT-FEC-001'],
     ['extern/PC-ALE/src/fec/golay.cpp'],
     'Syndrom-basiert. Korrigiert <=3 Bit. Gibt 0xFF zurück wenn unkorrektierbar. Parity-Check-Matrix H (pT|I12) für Fehlerprüfung.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-FEC-003',
     'Interleaving / Deinterleaving',
     ['REQ-FEC-012', 'REQ-FEC-013'],
     'MUST', ['FEAT-FEC-001', 'FEAT-FEC-002'],
     ['src/ale_fec_codec.cpp', 'include/ale_fec_codec.h'],
     'Muster A1B1A2B2...A24B24+S49. G13..G24 invertiert. Coder A=W1..W12, B=W13..W24.',
     'tests/test_fsk_core.cpp'),

    ('FEAT-FEC-004',
     '3x Redundanz mit Majority-Vote (RX)',
     ['REQ-FEC-014', 'REQ-FEC-015', 'REQ-FEC-016', 'REQ-FEC-017', 'REQ-FEC-018'],
     'MUST', ['FEAT-FEC-003'],
     ['src/ale_fec_codec.cpp'],
     'remove_redundancy_3x() aktuell Placeholder (nimmt erste Kopie). Muss 2/3-Voting implementieren. Bit 49 ignorieren.',
     'tests/test_fec_majority_vote.cpp'),

    ('FEAT-FEC-005',
     'Unanimous-Votes-Erfassung',
     ['REQ-FEC-019'],
     'MUST', ['FEAT-FEC-004'],
     ['src/ale_fec_codec.cpp', 'include/ale_fec_codec.h'],
     'Zählt einstimmige Votes (alle 3 gleich) über 48 Bits. Ergebnis temporär für FEAT-SYNC-003 speichern.',
     'tests/test_fec_majority_vote.cpp'),

    # ── FRAME ───────────────────────────────────────────────────────────────
    ('FEAT-FRAME-001',
     'Frame-Grundstruktur & Wortbasis',
     ['REQ-FRAME-001'],
     'MUST', ['FEAT-WORD-001', 'FEAT-FEC-003'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Frame = Tcc + [Message] + Conclusion. Tc = wpa*Trw, Tlc = 2*Tc, Tsc = C*2*Trw.',
     'tests/ale_modem_integration_test.cpp'),

    ('FEAT-FRAME-002',
     'Scanning Call (Tsc-Phase)',
     ['REQ-FRAME-002', 'REQ-FRAME-003', 'REQ-FRAME-005', 'REQ-FRAME-006'],
     'MUST', ['FEAT-FRAME-001', 'FEAT-ADDR-002'],
     ['src/ale_state_machine.cpp'],
     'INDIVIDUAL/NET: TO + erste 3 Chars, kein DATA/REP. GROUP: THRU/REP alternierend. tsc_slots = C*2.',
     'tests/ale_modem_integration_test.cpp'),

    ('FEAT-FRAME-003',
     'Leading Call (Tlc-Phase)',
     ['REQ-FRAME-004'],
     'MUST', ['FEAT-FRAME-002', 'FEAT-ADDR-002'],
     ['src/ale_state_machine.cpp'],
     'Vollständige Zieladresse 2x senden. Pro Trw-Slot 1 Wort. call_cycles_in_phase % seq.size() = aktuelles Wort.',
     'tests/ale_modem_integration_test.cpp'),

    ('FEAT-FRAME-004',
     'Message-Abschnitt',
     ['REQ-FRAME-007', 'REQ-FRAME-008', 'REQ-FRAME-009'],
     'MUST', ['FEAT-FRAME-003'],
     ['src/ale_state_machine.cpp'],
     'Quick-ID = FROM + eigene Adresse, nur 1x, nur vor CMD. Message = CMD [+DATA/REP]. AQC: Message nur im Link-State.',
     'tests/ale_modem_integration_test.cpp'),

    ('FEAT-FRAME-005',
     'Conclusion (TIS / TWAS) & Frame-Ende',
     ['REQ-FRAME-010', 'REQ-FRAME-011'],
     'MUST', ['FEAT-FRAME-004'],
     ['src/ale_state_machine.cpp'],
     'Conclusion = TIS oder TWAS (nie beide). Enthält vollständige Senderadresse. REP darf nicht direkt nach TIS/TWAS folgen. Sounds/Exception-Frames beginnen direkt mit TIS/TWAS.',
     'tests/ale_modem_integration_test.cpp'),

    ('FEAT-FRAME-006',
     'Gültige Wortsequenzen & Frame-Limits (Table A-XII)',
     ['REQ-FRAME-012', 'REQ-FRAME-013'],
     'MUST', ['FEAT-FRAME-005'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Sequenz-Automat für erlaubte Wortfolgen. Limits: Ta max 5W/1960ms, Tc max 12W/4704ms, Ts max 50s, Tm max 11.76s, AMD 11.76s, DTM 2.29min, DBM 23.26min.',
     'tests/ale_modem_integration_test.cpp'),

    # ── SYNC ────────────────────────────────────────────────────────────────
    ('FEAT-SYNC-001',
     'Asynchroner Systembetrieb & TX Word Phase',
     ['REQ-SYNC-001', 'REQ-SYNC-002', 'REQ-SYNC-003', 'REQ-SYNC-004'],
     'MUST', ['FEAT-FEC-003', 'FEAT-FRAME-001'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Keine systemweite Sync nötig. TX-Phasenreferenz self-timed 10ppm. T(spät)-T(früh) = n×Trw. Phase-Tracking nur innerhalb einer Übertragung. TX unabhängig vom RX.',
     'tests/test_sync.cpp'),

    ('FEAT-SYNC-002',
     'RX Word Sync & Demodulator',
     ['REQ-SYNC-005'],
     'MUST', ['FEAT-SYNC-001'],
     ['extern/PC-ALE/src/fsk/fsk_demodulator.cpp'],
     'Empfangsdemodulator: Akquisition, Tracking, Demodulation. DBM-Modus: einzelne Datenbits für Deep-Deinterleaving.',
     'tests/test_sync.cpp'),

    ('FEAT-SYNC-003',
     'Synchronisationskriterien & Golay-Modi',
     ['REQ-SYNC-006', 'REQ-SYNC-007'],
     'MUST', ['FEAT-SYNC-002', 'FEAT-FEC-005'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Alle 9 Kriterien kombiniert: unanimous-votes, Golay-A/B, Präambel, 3x ASCII Basic-38, Historie, Wortphase. Konfigurierbarer unanimous-Schwellwert. 4 Golay-Modi (3/4, 2/5, 1/6, 0/7). DO: automatische Anpassung.',
     'tests/test_sync.cpp'),

    # ── SOUND ───────────────────────────────────────────────────────────────
    ('FEAT-SOUND-001',
     'Single-Channel Sounding (Grundfunktion)',
     ['REQ-SOUND-002', 'REQ-SOUND-003', 'REQ-SOUND-004', 'REQ-SOUND-005'],
     'MUST', ['FEAT-FRAME-005', 'FEAT-CHAN-001'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Einseitig, periodisch, auf unbesetzten Kanälen. Struktur = Basic Call ohne Calling Cycle und Message (nur Conclusion TIS/TWAS). Trs = 784ms. TIS = Anrufer erwünscht, TWAS = abweisend. Adresse anzeigen + LQA speichern.',
     'tests/test_sound.cpp'),

    ('FEAT-SOUND-002',
     'Multichannel Scanning Sounding',
     ['REQ-SOUND-006', 'REQ-SOUND-007', 'REQ-SOUND-008',
      'REQ-SOUND-009', 'REQ-SOUND-010'],
     'MUST', ['FEAT-SOUND-001', 'FEAT-GEN-002'],
     ['src/ale_state_machine.cpp'],
     'Tsrs = Tss + Trs. Tss >= Ts. Call-Rejection: TWAS, Kanal verlassen. Call-Acceptance: TIS, Twrt warten. Mindestens 3 redundante Wörter empfangbar. Sound Set = Scan Set.',
     'tests/test_sound.cpp'),

    ('FEAT-SOUND-003',
     'Optionales Sounding-Handshake (A.5.3.4)',
     ['REQ-SOUND-011', 'REQ-SOUND-012'],
     'SHOULD', ['FEAT-SOUND-002'],
     ['src/ale_state_machine.cpp'],
     'Handshake = Single-Channel Individual Call Protocol, getriggert durch Konnektivität vom Sounding. Keine Wait-Before-Transmit-Time. Sound Set = Scan Set, keine neuen Frequenzen.',
     'tests/test_sound.cpp'),

    # ── CHAN ─────────────────────────────────────────────────────────────────
    ('FEAT-CHAN-001',
     'Channel Selection & LQA Grundfunktion',
     ['REQ-CHAN-001', 'REQ-CHAN-002', 'REQ-CHAN-003', 'REQ-CHAN-004',
      'REQ-CHAN-005', 'REQ-CHAN-006', 'REQ-CHAN-007', 'REQ-CHAN-008',
      'REQ-CHAN-009', 'REQ-CHAN-010'],
     'MUST', ['FEAT-GEN-006'],
     ['src/ale_channel_selector.cpp', 'include/ale_channel_selector.h'],
     'Automatische Kanalauswahl aus vorab vereinbartem Set auf Basis LQA. CMD LQA obligatorisch. KA1=1 → Report anfordern, KA1=0 → kein Report. Aktiv/Passiv = Netzwerkmanagement. Höhere Scores = bessere Kanäle (Anzeige).',
     'tests/test_channel_selection.cpp'),

    ('FEAT-CHAN-002',
     'BER-Messung & SINAD-Messung',
     ['REQ-CHAN-011', 'REQ-CHAN-012', 'REQ-CHAN-013', 'REQ-CHAN-014', 'REQ-CHAN-015'],
     'MUST', ['FEAT-CHAN-001', 'FEAT-FEC-005'],
     ['src/ale_channel_selector.cpp', 'include/ale_channel_selector.h'],
     'BER: non-unanimous votes / 48, Bereich 0-48. Bei unkorrigierbarem Golay: 48 addieren. Mittelwert über Frame → LQA Memory. SINAD: (S+N+D)/(N+D) über Signaldauer. MP: optional.',
     'tests/test_channel_selection.cpp'),

    ('FEAT-CHAN-003',
     'CMD LQA Word (BER/SINAD/MP Felder)',
     ['REQ-CHAN-016', 'REQ-CHAN-017', 'REQ-CHAN-018',
      'REQ-CHAN-019', 'REQ-CHAN-020'],
     'MUST', ['FEAT-CHAN-002'],
     ['src/ale_channel_selector.cpp', 'include/ale_channel_selector.h'],
     'CMD LQA obligatorisch. BER: 5 Bit (Tabelle A-XIII). SINAD: 5 Bit 0-30dB, 11111=kein Wert. MP: 3 Bit 0-6ms, 7=nicht gemessen.',
     'tests/test_channel_selection.cpp'),

    ('FEAT-CHAN-004',
     'Local Noise Report CMD (optional)',
     ['REQ-CHAN-021', 'REQ-CHAN-022'],
     'COULD', ['FEAT-CHAN-003'],
     ['src/ale_channel_selector.cpp'],
     'Broadcast-Alternative zum Sounding. Mittel- und Max-Rauschen der letzten 60min. Einheiten dBrel 0.1µV/3kHz. Wert <=0→0, 0-126→gerundet, >126→126, kein Report→127.',
     'tests/test_channel_selection.cpp'),

    ('FEAT-CHAN-005',
     'Single- & Multi-Station Channel Selection',
     ['REQ-CHAN-023', 'REQ-CHAN-024', 'REQ-CHAN-025',
      'REQ-CHAN-026', 'REQ-CHAN-027', 'REQ-CHAN-028',
      'REQ-CHAN-029', 'REQ-CHAN-030'],
     'MUST', ['FEAT-CHAN-003'],
     ['src/ale_channel_selector.cpp'],
     'Single-Station: bester Kanal aus LQA. Bilateral (Link): FROM+TO summieren. LQA 0=exzellent, 30=sehr schlecht, x=Handshake fehlgeschlagen, -=nicht versucht. Broadcast: TO gewichten. Listening: FROM gewichten. Multi-Station analog.',
     'tests/test_channel_selection.cpp'),

    ('FEAT-CHAN-006',
     'Listen Before Transmit',
     ['REQ-CHAN-031', 'REQ-CHAN-032', 'REQ-CHAN-033', 'REQ-CHAN-034'],
     'MUST', ['FEAT-CHAN-001'],
     ['src/ale_channel_selector.cpp'],
     'Twt programmierbar. ALE-only Kanäle: min 2×Trw. Andere: min 2s. Verbrachte Hörzeit anrechenbar. Abgebrochener Sound: neu planen. Abgebrochener Call: anderen Kanal wählen. Operator kann überschreiben.',
     'tests/test_channel_selection.cpp'),

    # ── LINK ────────────────────────────────────────────────────────────────
    ('FEAT-LINK-001',
     'Individual Call (senden)',
     ['REQ-LINK-001', 'REQ-LINK-002', 'REQ-LINK-007',
      'REQ-LINK-008', 'REQ-LINK-009', 'REQ-LINK-016', 'REQ-LINK-017'],
     'MUST', ['FEAT-FRAME-006', 'FEAT-CHAN-006'],
     ['src/ale_state_machine.cpp', 'include/ale_state_machine.h'],
     'Three-Way-Handshake. Timer Tabelle A-XV. Manual-Operation Emergency Control (PTT bleibt laut 4.2.2). Scanning Call: 2×Trw pro gescanntem Kanal. Leading Call: vollst. Adresse 2x. Twr/Twrt bei Timeout → nächsten Kanal.',
     'tests/test_link.cpp'),

    ('FEAT-LINK-002',
     'Individual Call (empfangen & Response)',
     ['REQ-LINK-004', 'REQ-LINK-005', 'REQ-LINK-006',
      'REQ-LINK-018', 'REQ-LINK-019'],
     'MUST', ['FEAT-LINK-001', 'FEAT-SYNC-003'],
     ['src/ale_state_machine.cpp'],
     'Signal/Symbol-Erkennung. End-of-Frame via Conclusion + Wortphase. Scanning-Call Adress-Erkennung am ersten Wort. Abbruchbedingungen Twce, Tmmax, Tlww. TWAS → kein Response.',
     'tests/test_link.cpp'),

    ('FEAT-LINK-003',
     'Acknowledgment & Link-Termination',
     ['REQ-LINK-020', 'REQ-LINK-021', 'REQ-LINK-022', 'REQ-LINK-023'],
     'MUST', ['FEAT-LINK-002'],
     ['src/ale_state_machine.cpp'],
     'ACK = neuer Einzelruf-Frame. Twa-Timer (Standard 30s). TWAS-Conclusion = Link-Ende. Manual Reset sendet TWAS. Auto-Termination wenn Twa abläuft. TWAS empfangen → vorherigen Zustand.',
     'tests/test_link.cpp'),

    ('FEAT-LINK-004',
     'Kanalwechsel & Kollisionserkennung',
     ['REQ-LINK-010', 'REQ-LINK-011', 'REQ-LINK-012',
      'REQ-LINK-013', 'REQ-LINK-014', 'REQ-LINK-015', 'REQ-LINK-024'],
     'MUST', ['FEAT-LINK-001', 'FEAT-CHAN-006'],
     ['src/ale_state_machine.cpp'],
     'ALE States (Figure A-28). Scanning Call: Reihenfolge per Kanal-Auswahl-Algorithmus. Channel Rejection: LQA aktualisieren. Busy Channel überspringen, später neu versuchen. Kanalliste erschöpft → available + Operator benachrichtigen. Kollision: neue Wortphase = unterbrechendes Frame übernehmen.',
     'tests/test_link.cpp'),

    ('FEAT-LINK-005',
     'One-to-Many: Slotted Responses & Star Net',
     ['REQ-LINK-003', 'REQ-LINK-025', 'REQ-LINK-026', 'REQ-LINK-027',
      'REQ-LINK-028', 'REQ-LINK-029', 'REQ-LINK-030',
      'REQ-LINK-031', 'REQ-LINK-032', 'REQ-LINK-033', 'REQ-LINK-034'],
     'MUST', ['FEAT-LINK-003', 'FEAT-ADDR-003'],
     ['src/ale_state_machine.cpp'],
     'TDMA-Schema. Tswt(SN) = SN×[5Tw+2Ta(caller)+Tm] + Ta(caller) + Σ Ta(m) für m=1..SN-1. Slot 0 = Tunen. Twan = Twrn + 2×Trw. Slots 14Tw Standard. Erweiterung für längere Adressen/Messages. Star Net = Einzelruf mit Net-Adresse.',
     'tests/test_link_multi.cpp'),

    ('FEAT-LINK-006',
     'One-to-Many: Star Group Call',
     ['REQ-LINK-035', 'REQ-LINK-036', 'REQ-LINK-037', 'REQ-LINK-038',
      'REQ-LINK-039', 'REQ-LINK-040', 'REQ-LINK-041',
      'REQ-LINK-042', 'REQ-LINK-043'],
     'MUST', ['FEAT-LINK-005'],
     ['src/ale_state_machine.cpp'],
     'Ad-hoc-Gruppe. Scanning: THRU/REP rotierend. Leading: TO vollst. Adressen 2x, max 12W. Slot-Ableitung aus Adressreihenfolge. Twan max = 107Tw+... Mehrfache Selbstadressen: mind. 1 antworten.',
     'tests/test_link_multi.cpp'),

    ('FEAT-LINK-007',
     'AllCall, AnyCall & Wildcard Protokolle',
     ['REQ-LINK-044', 'REQ-LINK-045', 'REQ-LINK-046'],
     'MUST', ['FEAT-LINK-005', 'FEAT-ADDR-003', 'FEAT-ADDR-004'],
     ['src/ale_state_machine.cpp'],
     'AllCall @?@: hören, kein Response. AnyCall @@?: pseudozufällig Slot 1-16, Slotbreite 20Tw (23Tw mit LQA). Wildcard: TWAS→AllCall-Logik, TIS→AnyCall-Logik. Alle drei abschaltbar, Standard aktiviert.',
     'tests/test_link_multi.cpp'),

    # ── ADDR ────────────────────────────────────────────────────────────────
    ('FEAT-ADDR-001',
     'Basic-38 Zeichensatz & Adressvalidierung',
     ['REQ-ADDR-001', 'REQ-ADDR-002', 'REQ-ADDR-003',
      'REQ-ADDR-005', 'REQ-ADDR-016'],
     'MUST', [],
     ['src/ale_word.cpp', 'include/ale_word.h'],
     'A-Z, 0-9, @, ?. Validierung nicht nur auf Top-3-Bits. Stuffing mit @. Reservierte Muster verwerfen. Rückwärtskompatibilität Basisadressen.',
     'tests/test_protocol.cpp'),

    ('FEAT-ADDR-002',
     'Individual Adressen (Basic & Extended)',
     ['REQ-ADDR-004', 'REQ-ADDR-006', 'REQ-ADDR-007'],
     'MUST', ['FEAT-ADDR-001'],
     ['src/ale_word.cpp', 'include/ale_word.h'],
     'chunk_address(): i+=3, @-Pad. Max 15 Zeichen = 5 Wörter. Sequenz TO→DATA→REP→DATA→REP.',
     'tests/test_protocol.cpp'),

    ('FEAT-ADDR-003',
     'Net, Group, AllCall, AnyCall Adressen',
     ['REQ-ADDR-008', 'REQ-ADDR-009', 'REQ-ADDR-010', 'REQ-ADDR-011'],
     'MUST', ['FEAT-ADDR-002'],
     ['src/ale_state_machine.cpp', 'src/ale_word.cpp'],
     'AllCall=@?@, AnyCall=@@?. Netz-Adresse mit TO. Group nutzt THRU/REP. Slot-Identifikatoren für Netz.',
     'tests/test_protocol.cpp'),

    ('FEAT-ADDR-004',
     'Wildcard-Matching',
     ['REQ-ADDR-012'],
     'MUST', ['FEAT-ADDR-001'],
     ['src/ale_word.cpp'],
     "match_wildcard(): '?' = ein beliebiges alphanumerisches Zeichen. Muster- und Adresslänge müssen gleich sein.",
     'tests/test_protocol.cpp'),

    ('FEAT-ADDR-005',
     'Self, Null, In-Link Adressen',
     ['REQ-ADDR-013', 'REQ-ADDR-014', 'REQ-ADDR-015'],
     'MUST', ['FEAT-ADDR-002'],
     ['src/ale_word.cpp', 'src/ale_state_machine.cpp'],
     "Null='@@@' (nur TO/REP im Calling Cycle). In-Link='?@?' (alle Link-Mitglieder). Self: mehrere Adressen möglich.",
     'tests/test_protocol.cpp'),

    # ── MSG (bewusst skip) ──────────────────────────────────────────────────
    ('FEAT-MSG-001',
     'AMD / DTM / DBM Nachrichtenprotokolle',
     ['REQ-MSG-001'],
     "WON'T", [],
     [],
     "WON'T this release. AMD/DTM/DBM explizit außerhalb des aktuellen Scopes. Kein Code erforderlich.",
     'tests/test_msg.cpp'),
]

# ---------------------------------------------------------------------------
# 4. AC-Lookup aus Index bauen (req_id → [ac_dict, ...])
# ---------------------------------------------------------------------------
ac_by_req = {}
for r in all_reqs:
    rid = r['id']
    ac_by_req[rid] = r.get('ac', [])

# ---------------------------------------------------------------------------
# 5. Features zusammenbauen
# ---------------------------------------------------------------------------
features = []
for (feat_id, title, req_ids, priority, depends_on,
     module, notes, test_file) in FEATURE_PLAN:

    # ACs aus allen zugehörigen Requirements sammeln
    acs = []
    seen_ac_ids = set()
    for rid in req_ids:
        for ac in ac_by_req.get(rid, []):
            ac_id   = ac.get('id', '')
            ac_text = ac.get('text', '')
            if ac_id and ac_id not in seen_ac_ids:
                seen_ac_ids.add(ac_id)
                acs.append({'id': ac_id, 'text': ac_text, 'status': 'open'})

    feat = {
        'id':       feat_id,
        'title':    title,
        'status':   'skip' if priority == "WON'T" else 'todo',
        'priority': priority,
        'implements': req_ids,
        'module':   module,
        'depends_on': depends_on,
        'acceptance_criteria': acs,
        'tests': [{'file': test_file,
                   'description': f'Verifikation für {feat_id}'}],
        'notes': notes,
    }
    features.append(feat)

# ---------------------------------------------------------------------------
# 6. Ausgabe als YAML — manuell formatiert für Lesbarkeit
# ---------------------------------------------------------------------------
today = datetime.date.today().isoformat()
total = len(features)

def esc(s):
    """Minimales YAML-String-Escaping."""
    s = str(s)
    if any(c in s for c in ['"', '\n', ':', '#', '{', '}', '[', ']']):
        s = s.replace('\\', '\\\\').replace('"', '\\"')
        return f'"{s}"'
    # Wenn der String mit einem Sonderzeichen beginnt → quoten
    if s and s[0] in ('*', '&', '!', '|', '>', '?', '-', '%', '@', '`'):
        return f'"{s}"'
    return s

lines = [
    '# =============================================================================',
    '# IMPLEMENTATION BACKLOG — PC-ALE / PC-ALE-Win',
    '# =============================================================================',
    '#',
    '# Abgeleitet aus: REQUIREMENTS_INDEX.yml',
    '# Basis-Standard: MIL-STD-188-141B Appendix A',
    '#',
    '# WORKFLOW DES CODING AGENTS (ein Feature nach dem anderen):',
    '#   1. Erstes Feature mit status: todo und erfüllten depends_on wählen',
    '#   2. status → in-progress',
    '#   3. Existierenden Code lesen (module:), REQ in REQUIREMENTS_INDEX.yml lesen',
    '#   4. Feature implementieren — NUR was in acceptance_criteria steht',
    '#   5. Tests schreiben/aktualisieren — jedes AC braucht mind. einen Test',
    '#   6. Alle AC auf status: verified setzen wenn Test grün',
    '#   7. status → done, notes ausfüllen, Backlog zurückschreiben',
    '#   8. Weiter zu Schritt 1',
    '#',
    '# DONE-KRITERIUM:',
    '#   ☑ Alle acceptance_criteria.status: verified',
    '#   ☑ Alle Tests grün (ctest)',
    '#   ☑ Kein TODO/PLACEHOLDER im produzierten Code',
    '#   ☑ Kein Compiler-Warning (-W4)',
    '#',
    '# STATUS-WERTE:',
    '#   todo        → noch nicht begonnen, oder: zurückgesetzt für Neuverifikation',
    '#   in-progress → aktuell in Arbeit (nur 1 Feature gleichzeitig)',
    '#   done        → implementiert + alle ACs verified + Tests grün',
    '#   blocked     → blockiert (reason: angeben)',
    '#   skip        → bewusst übersprungen (reason: angeben)',
    '#',
    '# WICHTIG — ALLE STATUS SIND BEWUSST "todo":',
    '#   Der Agent darf keinen Eintrag als bereits korrekt implementiert annehmen.',
    '#   Auch wenn Code bereits existiert (module: enthält Dateipfade), muss der',
    '#   Agent diesen Code lesen, gegen die ACs prüfen, Lücken schließen und',
    '#   erst dann done setzen. Falsch-positiv ist gefährlicher als unnötige Arbeit.',
    '#',
    '# EINE-FEATURE-REGEL:',
    '#   Niemals mehr als ein Feature gleichzeitig. Kein Scope Creep.',
    '#   Entdeckte Bugs → neues FEAT anlegen, nicht jetzt lösen.',
    '# =============================================================================',
    '',
    'metadata:',
    f'  project: PC-ALE / PC-ALE-Win',
    f'  standard: MIL-STD-188-141B Appendix A',
    f'  last_updated: {today!r}',
    f'  total_features: {total}',
    '  status_summary:',
    f'    todo: {total}',
    '',
    'features:',
]

for feat in features:
    lines.append(f"- id: {feat['id']}")
    lines.append(f"  title: {esc(feat['title'])}")
    lines.append(f"  status: {feat['status']}")
    lines.append(f"  priority: {feat['priority']}")

    lines.append("  implements:")
    for r in feat['implements']:
        lines.append(f"  - {r}")

    lines.append("  module:")
    for m in feat['module']:
        lines.append(f"  - {m}")

    if feat['depends_on']:
        lines.append("  depends_on:")
        for d in feat['depends_on']:
            lines.append(f"  - {d}")
    else:
        lines.append("  depends_on: []")

    lines.append("  acceptance_criteria:")
    for ac in feat['acceptance_criteria']:
        lines.append(f"  - id: {ac['id']}")
        txt = str(ac['text'])
        if len(txt) > 120:
            txt = txt[:117] + '...'
        txt = txt.replace('\\', '\\\\').replace('"', '\\"')
        lines.append(f'    text: "{txt}"')
        lines.append(f"    status: open")

    lines.append("  tests:")
    for t in feat['tests']:
        lines.append(f"  - file: {t['file']}")
        lines.append(f"    description: {esc(t['description'])}")

    notes = str(feat['notes']).replace('\\', '\\\\').replace("'", '"')
    lines.append(f"  notes: '{notes}'")
    lines.append('')

output = '\n'.join(lines)
with open(DST, 'w', encoding='utf-8') as f:
    f.write(output)

print(f"OK: {total} Features → {DST}")
print(f"    {len(output.splitlines())} Zeilen")

# Schnellcheck: AC-Abdeckung
all_req_ids  = {r['id'] for r in all_reqs}
covered_reqs = set()
for (feat_id, title, req_ids, *_) in FEATURE_PLAN:
    covered_reqs.update(req_ids)
missing = all_req_ids - covered_reqs
if missing:
    print(f"\nWARNING: {len(missing)} Requirements nicht abgedeckt:")
    for m in sorted(missing):
        print(f"  {m}")
else:
    print(f"    Alle {len(all_req_ids)} Requirements abgedeckt ✓")
