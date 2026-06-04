# PC-ALE Project Context

## What & Why
C++ implementation of MIL-STD-188-141B Appendix A ALE modem for HF digital radio.
Enables PC-based stations to interoperate with military/civilian ALE networks.
Phase 1: Core/Domain only. Platform adapters and Application Layer = separate projects later.

## Architecture (Ports & Adapters)
```
[Application Layer]  ← separate project, later
        ↓
[PC-ALE Core]        ← ACTIVE SCOPE
  ALEStateMachine · Modem · FEC · Protocol
  Uses PAL interfaces via DI only. No hardware. No OS APIs.
        ↓ (PAL interfaces only)
[PC-ALE-PAL]         ← interface repo, read-only
  IAudioDriver · IRadio · ITimer · ILogger
        ↓
[Platform Adapters]  ← separate projects, later
  PC-ALE-Win · PC-ALE-Linux · PC-ALE-SDR
```

## Data Flow
```
TX: ALEStateMachine → ALEFECCodec::encode → ALE2GModem::transmit_word
    → ToneGenerator::generate_tone → int16_t PCM buffer → [IAudioDriver]

RX: [IAudioDriver] → float PCM buffer → ALE2GModem::detect_symbol
    → ALEFECCodec::decode → WordParser::parse_from_bits → ALEStateMachine
```

## Module Map
| File | Responsibility | Features |
|---|---|---|
| `include/ale_types.h` | Tone freqs, timing constants | WAVEFORM-001/003 |
| `include/ale_word.h` + `src/ale_word.cpp` | word24 encode/decode, address chunking | WORD-001–003, ADDR-001–005 |
| `src/fec/golay.cpp` | Golay (24,12) encode/decode | FEC-001/002 |
| `src/ale_fec_codec.cpp` | Interleave, 3× redundancy, majority-vote | FEC-003–005 |
| `src/ale_state_machine.cpp` | Calling cycle, frame phases, link protocol, sounding | FRAME, SYNC-001, SOUND, LINK, GEN-001/002/009 |
| `src/ale2gmodem.cpp` | Symbol↔PCM, Goertzel detector, word sync | SYNC-002/003 |
| `src/fsk/tone_generator.cpp` | NCO 8-FSK tone generation | WAVEFORM-002 |
| `include/ale_data_store.h` + `src/ale_data_store.cpp` | Channel/address/LQA/message stores | GEN-004–008 *(new)* |
| `include/ale_channel_selector.h` + `src/ale_channel_selector.cpp` | LQA measurement, channel selection, LBT | CHAN-001–006 *(new)* |
| `include/ale_aqc.h` + `src/ale_aqc.cpp` | AQC-ALE optional protocol | GEN-010 *(new)* |
| `include/ale_persistence.h` | IPersistenceBackend interface | GEN-004–008 *(new)* |

## Key Design Decisions (summary — full detail in FEATURES_DESIGN.md)
- **DD-001** word24: W1=bit23 (MSB). Preamble[23:21], Char1[20:14], Char2[13:7], Char3[6:0]
- **DD-002** NCO: 32-bit phase accumulator, init=0x40000000 (π/2), integer arithmetic
- **DD-003** Golay split: Coder A=upper 12bit (normal parity), Coder B=lower 12bit (inverted parity)
- **DD-004** Interleave: A1B1A2B2…A24B24+S49 stuffbit
- **DD-005** Sample rate: 8 kHz core, platform handles 8↔48 kHz resampling
- **DD-006** Timing anchor: `first_call_tx_ms` set once at CALLING entry, never reset
- **DD-007** Address chunking: 3-char groups, @-stuffing, max 15 chars = 5 words
- **DD-008** Data stores separate from ALEStateMachine; `AleDataStore&` injected via constructor
- **DD-009** Two-level state machine: outer (AVAILABLE/CALLING/LISTENING/LINKED/SOUNDING) + inner (SCANNING/LEADING/MESSAGE/CONCLUSION)
- **DD-010** Timing: `Tw_ms=130` is ROUNDED (exact=130.666ms). `Trw_ms=392` is spec-exact. NEVER compute `n*Tw_ms` for timing — use direct spec values. `static_assert(Trw_ms==392)`
- **DD-011** ChannelSelector is its own module (not in ALEStateMachine)
- **DD-012** Tswt(SN) = SN×[5·Tw+2·Ta(caller)+Tm] + Ta(caller) + Σ Ta(m) for m=1..SN-1

## Current Focus
- FEAT-WORD-002 done: FrameValidator + WordParser::make_word() added to ale_word.h/cpp
- FEAT-WORD-003 done: Message & Extension Words (CMD/DATA/REP) implemented
- Bug #5 (pre-existing StateMachine test#3 failure) open — not blocking current work
- FEC majority-vote placeholder still in `remove_redundancy_3x()` — needs real 2/3 voting

## Next Steps (priority order)
1. FEAT-WORD-003 (Issue #4) — Message & Extension Words (CMD/DATA/REP)
2. FEAT-FEC-004 — fix majority-vote placeholder (in-progress)
3. FEAT-FEC-005 — unanimous-votes count
4. FEAT-GEN-004 — ChannelStore (new file: ale_data_store.h/cpp)
5. FEAT-GEN-005 — SelfAddressStore

## Known Issues / Open Design Points
- `IPersistenceBackend` interface not yet defined (needed before GEN-004–008)
- `AleDataStore` aggregator class not yet defined (needed before CHAN-001)
- AQC 5-bit character mapping table not specified in standard — mark OPEN-AQC-001
- REQ-SOUND-001 is placeholder in REQUIREMENTS.md, not yet extractable
- Review findings F-001 (Tw_ms timing), F-002 (AllCall logic), L-001..L-006 — see FEATURES_DESIGN_REVIEW.md