# PC-ALE Progress Log
# Format: [FEAT-xxx] status — one-line summary. Append-only.

## Implemented (validate = code exists, not yet verified against spec)
[FEAT-WAVEFORM-001] validate — TONE_FREQS_HZ + FREQ_TO_SYMBOL in ale_types.h
[FEAT-WAVEFORM-002] validate — NCO ToneGenerator with phase continuity in tone_generator.cpp
[FEAT-WAVEFORM-003] validate — timing constants (SAMPLE_RATE_HZ, SYMBOLS_PER_WORD, Trw_ms=392) in ale_types.h
[FEAT-WORD-001]     validate — word24 encode/decode with W1=bit23 in ale_word.cpp
[FEAT-WORD-002]     validate — 5 address preamble types (TO/TIS/TWS/THRU/FROM) in ale_word.cpp
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
[FEAT-WORD-002]     done — FrameValidator (thru/from sequence rules) + WordParser::make_word() in ale_word.h/cpp; 19 ALECalling tests grün
[FEAT-ADDR-004]     validate — match_wildcard() for '?' in ale_word.cpp

## Known Issues
- FEAT-FEC-004: majority vote is placeholder
- FEAT-LINK-001: REQ-LINK-007 (emergency control) and REQ-LINK-009 (timing A-XV) not verified
- All GEN/CHAN features: new files not yet created
- See FEATURES_DESIGN_REVIEW.md: F-001 (Tw_ms timing trap), F-002 (AllCall logic)
