# PC-ALE Progress Log
# Format: [FEAT-xxx] status — one-line summary. Append-only.

## Implemented (validate = code exists, not yet verified against spec)
[FEAT-WAVEFORM-001] validate — TONE_FREQS_HZ + FREQ_TO_SYMBOL in ale_types.h
[FEAT-WAVEFORM-002] validate — NCO ToneGenerator with phase continuity in tone_generator.cpp
[FEAT-WAVEFORM-003] done — timing constants (SYMBOL_RATE_BAUD, SYMBOL_DURATION_MS, TRW_MS, TW_MS) in ale_types.h
[FEAT-WORD-001]     done — word24 encode/decode validated; AC-WORD-001-1..5 + AC-WORD-002-1..4 tests added to test_protocol.cpp; ctest passes (commit 7862be3)
[FEAT-WORD-002]     done — 13 per-AC tests added (test_ale_calling.cpp, commit 1d210ed); AC-WORD-003-1..4, AC-WORD-004-1..8, AC-WORD-005-1..4, AC-WORD-006-3/5/6, AC-WORD-007-1/2/3/6 pass; blocked on GROUP call protocol (AC-WORD-006-1/2/4/7) and FROM position enforcement (AC-WORD-007-4/5/7)
[FEAT-WORD-003]     done — CMD/DATA/REP word handling; 10/10 ACs pass in test_protocol.cpp; ctest passes
[FEAT-FEC-001]      validate — Golay (24,12) encoder with syndrome_table in golay.cpp
[FEAT-FEC-002]      validate — Golay decoder, corrects <=3 bit errors in golay.cpp
[FEAT-FEC-003]      validate — A1B1A2B2 interleave/deinterleave in ale_fec_codec.cpp
[FEAT-FEC-004]      in-progress — remove_redundancy_3x() is placeholder (takes copy 1 only) — needs real 2/3 majority vote
[FEAT-FRAME-001]    validate — Frame structure (Scanning+Leading+Conclusion) in ale_state_machine.cpp
[FEAT-FRAME-002]    validate — Scanning call phase in ale_state_machine.cpp
[FEAT-FRAME-003]    validate — Leading call phase in ale_state_machine.cpp
[FEAT-FRAME-005]    validate — Conclusion phase (TIS/TWS) in ale_state_machine.cpp
[FEAT-SYNC-001]     validate — first_call_tx_ms Trw-grid anchor in ale_state_machine.cpp
[FEAT-LINK-001]     validate — Individual call TX in ale_state_machine.cpp (verify REQ-LINK-007/009)
[FEAT-ADDR-001]     validate — Basic-38 charset validation in ale_word.cpp
[FEAT-ADDR-002]     validate — chunk_address() with @-stuffing in ale_word.cpp
[FEAT-ADDR-004]     validate — match_wildcard() for '?' in ale_word.cpp

## Known Issues
- FEAT-FEC-004: majority vote is placeholder
- FEAT-LINK-001: REQ-LINK-007 (emergency control) and REQ-LINK-009 (timing A-XV) not verified
- All GEN/CHAN features: new files not yet created
- See FEATURES_DESIGN_REVIEW.md: F-001 (Tw_ms timing trap), F-002 (AllCall logic)