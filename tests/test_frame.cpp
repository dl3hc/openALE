/**
 * \file test_frame.cpp
 * \brief Tests for FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis
 *
 * Part 1 (AC-FRAME-002): Frame class unit tests — encode() correctness and
 *   roundtrip via ALEFECCodec::deinterleave_word().
 *
 * Part 2 (AC-FRAME-001): ALEStateMachine / ALE2GModem::Modulator integration tests —
 *   slot timing, phase sequencing, and modem 3× copy behaviour.
 *
 * Timing model (DD-013):
 *   ALEStateMachine is callback-driven: at tune-complete the COMPLETE calling
 *   TX sequence (scanning + leading + conclusion) is enqueued back-to-back;
 *   on_word_complete() is fired once per symbol frame physically consumed by
 *   the audio layer and advances call_cycle_count / the calling phases.
 *   The Trw grid is defined by the sample stream (one word = 49 symbols ×
 *   8 ms = 392 ms), never by wall time.
 *
 *   Test drive pattern:
 *     sm.update(Twt + Tt)       → full TX sequence enqueued (words_pending = N)
 *     sm.on_word_complete()     → call_cycle_count increments, phases advance
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "Word/ale_frame.h"
#include "FEC/ale_fec_codec.h"
#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <cstring>
#include <algorithm>

namespace ale {

// ============================================================================
// AC-FRAME-002 — Frame class unit tests
// ============================================================================

// AC-FRAME-002-1: Frame::encode() produces one entry per word and each entry
// equals ALEWord::encode() for that word.
bool test_ac_002_1_encode_delegates_to_word_encode()
{
    std::cout << "\n[AC-FRAME-002-1] Frame::encode() delegates to ALEWord::encode()\n";

    const char addr[] = {'S', 'A', 'M'};
    ALEWord word = WordParser::make_word(PreambleType::TO, addr);
    Frame frame({word});

    auto encoded = frame.encode();
    bool size_ok  = (encoded.size() == 1);
    bool value_ok = size_ok && (encoded[0] == word.encode());

    std::cout << "  encode() has 1 element: " << (size_ok  ? "PASS" : "FAIL") << "\n";
    std::cout << "  encode()[0] == word.encode(): " << (value_ok ? "PASS" : "FAIL") << "\n";

    return size_ok && value_ok;
}

// AC-FRAME-002-2: encode() → deinterleave_word() round-trips preamble and
// payload back to their original values with zero FEC errors.
bool test_ac_002_2_encode_roundtrip()
{
    std::cout << "\n[AC-FRAME-002-2] Frame::encode() roundtrip via ALEFECCodec::deinterleave_word()\n";

    const char addr[] = {'S', 'A', 'M'};
    ALEWord word = WordParser::make_word(PreambleType::TO, addr);
    Frame frame({word});

    auto encoded = frame.encode();

    Golay::DecodeResult fec;
    uint32_t decoded      = ALEFECCodec::deinterleave_word(encoded[0], fec);
    uint32_t got_preamble = decoded >> PAYLOAD_BITS;
    uint32_t got_payload  = decoded & ((1u << PAYLOAD_BITS) - 1u);

    bool fec_ok      = (fec.flag == Golay::DECODE_OK);
    bool preamble_ok = (got_preamble == static_cast<uint32_t>(word.type));
    bool payload_ok  = (got_payload  == word.raw_payload);

    std::cout << "  FEC clean (DECODE_OK): "      << (fec_ok      ? "PASS" : "FAIL") << "\n";
    std::cout << "  preamble round-trips (TO=2): " << (preamble_ok ? "PASS" : "FAIL")
              << " (got " << got_preamble << ")\n";
    std::cout << "  payload round-trips: "         << (payload_ok  ? "PASS" : "FAIL")
              << " (expected " << word.raw_payload << ", got " << got_payload << ")\n";

    return fec_ok && preamble_ok && payload_ok;
}

// AC-FRAME-002-3: multi-word Frame preserves insertion order in encode().
bool test_ac_002_3_multi_word_order()
{
    std::cout << "\n[AC-FRAME-002-3] Frame::encode() preserves word order\n";

    const char sam[] = {'S', 'A', 'M'};
    const char bob[] = {'B', 'O', 'B'};
    ALEWord w1 = WordParser::make_word(PreambleType::TO,  sam);
    ALEWord w2 = WordParser::make_word(PreambleType::TIS, bob);
    Frame frame({w1, w2});

    auto encoded = frame.encode();
    bool size_ok  = (encoded.size() == 2);
    bool order_ok = size_ok
                    && (encoded[0] == w1.encode())
                    && (encoded[1] == w2.encode());

    std::cout << "  encode() has 2 elements: "     << (size_ok  ? "PASS" : "FAIL") << "\n";
    std::cout << "  word order preserved: "         << (order_ok ? "PASS" : "FAIL") << "\n";

    return size_ok && order_ok;
}

// AC-FRAME-002-4: empty Frame produces an empty encode() result.
bool test_ac_002_4_empty_frame()
{
    std::cout << "\n[AC-FRAME-002-4] Empty Frame\n";

    Frame empty;
    bool is_empty   = empty.empty();
    bool size_zero  = (empty.size() == 0);
    bool enc_empty  = empty.encode().empty();

    std::cout << "  empty() == true: "              << (is_empty  ? "PASS" : "FAIL") << "\n";
    std::cout << "  size() == 0: "                  << (size_zero ? "PASS" : "FAIL") << "\n";
    std::cout << "  encode() returns empty vector: " << (enc_empty ? "PASS" : "FAIL") << "\n";

    return is_empty && size_zero && enc_empty;
}

// ============================================================================
// Test harness
// ============================================================================

struct FrameHarness {
    ALEStateMachine sm;
    ALE2GModem::Modulator      modem;

    std::vector<ALEWord>              tx_words;           // logical words sent by SM
    std::vector<std::vector<uint8_t>> tx_bufs;            // symbol frames from modem
    std::vector<uint32_t>             word_complete_times; // times of on_word_complete() calls
    std::vector<CallingPhase>         phase_at_complete;  // calling_phase at each on_word_complete

    FrameHarness(const std::string& self, const std::string& to_addr,
                 uint32_t scan_ch)
    {
        sm.set_self_address(self);
        sm.set_target_scan_channels(scan_ch);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_channel_callback([](const Channel&){});
        sm.set_rx_enabled_callback([](bool){});

        sm.set_transmit_callback([this](const ALEWord& w) {
            tx_words.push_back(w);
            modem.enqueue_word(w);
        });

        sm.initiate_call(to_addr);
    }

    // Advance SM and pull any pending symbol frames from modem.
    // Fires on_word_complete() once per pulled frame.
    void tick(uint32_t t) {
        current_ms_ = t;
        sm.update(t);
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modem.pull_symbol_frame(syms)) {
            tx_bufs.emplace_back(syms, syms + SYMBOLS_PER_WORD);
            word_complete_times.push_back(t);
            phase_at_complete.push_back(sm.get_calling_phase());
            sm.on_word_complete();
        }
    }

    // Drive N full Trw slots starting at t0 — one tick per slot.
    void drive_slots(uint32_t t0, uint32_t n_slots) {
        const uint32_t Trw = ALETimingConstants::Trw_ms;
        for (uint32_t i = 0; i < n_slots; ++i)
            tick(t0 + i * Trw);
    }

private:
    uint32_t current_ms_ = 0;
};

// ============================================================================
// AC-FRAME-001-2 (row 1) — ALE2GModem::Modulator emits exactly one symbol frame of
// SYMBOLS_PER_WORD (49) values per logical word; symbols are in range 0–7.
// ============================================================================

bool test_ac_001_2_modem_symbol_frame_per_word()
{
    std::cout << "\n[AC-FRAME-001-2] ALE2GModem::Modulator: one symbol frame (49 symbols, 0-7) per word\n";

    ALE2GModem::Modulator modem;

    ALEWord word = ALEWord();
    word.type       = PreambleType::TO;
    word.address[0] = 'S'; word.address[1] = 'A'; word.address[2] = 'M';
    word.address[3] = '\0';
    word.valid       = true;
    modem.enqueue_word(word);

    uint8_t syms[SYMBOLS_PER_WORD];
    const bool pulled = modem.pull_symbol_frame(syms);

    bool count_ok = pulled;
    std::cout << "  pull_symbol_frame returns true: "
              << (count_ok ? "PASS" : "FAIL") << "\n";

    bool size_ok = count_ok;  // pull fills exactly SYMBOLS_PER_WORD values
    std::cout << "  symbol frame has " << SYMBOLS_PER_WORD << " symbols: "
              << (size_ok ? "PASS" : "FAIL") << "\n";

    bool range_ok = size_ok;
    if (size_ok) {
        for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i) {
            if (syms[i] > 7) { range_ok = false; break; }
        }
    }
    std::cout << "  all symbols in 0-7 range: " << (range_ok ? "PASS" : "FAIL") << "\n";

    // A second pull with no enqueued word must return false
    const bool no_extra = !modem.pull_symbol_frame(syms);
    std::cout << "  idle pull returns false: "
              << (no_extra ? "PASS" : "FAIL") << "\n";

    return count_ok && size_ok && range_ok && no_extra;
}

// ============================================================================
// AC-FRAME-001-2 (row 2) — SM calls transmit_word() exactly once per logical
// word; call_cycle_count increments per on_word_complete(), not per transmit.
// ============================================================================

bool test_ac_001_2_cycle_count_increments_in_callback()
{
    std::cout << "\n[AC-FRAME-001-2] call_cycle_count increments only in on_word_complete()\n";

    // scan_ch=0: skip SCANNING_CALL, enter LEADING_CALL directly.
    // TO address "BOB" (1 word), self "SAM" (1 word).
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(0);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});

    uint32_t transmit_count = 0;
    sm.set_transmit_callback([&](const ALEWord&) { ++transmit_count; });

    sm.initiate_call("BOB");

    // Before any update: counters at zero
    bool pre_ok = (sm.get_call_cycle_count() == 0 && sm.get_words_pending() == 0);
    std::cout << "  before update: call_cycle_count=0, words_pending=0: "
              << (pre_ok ? "PASS" : "FAIL") << "\n";

    // Advance through LBT (Twt=784 ms) and TUNING (Tt=1045 ms) — AC-LINK-017-1/2.
    // No words are transmitted during these pre-TX phases.  At tune-complete
    // the full sequence is enqueued: leading "BOB"×2 + conclusion TIS "SAM" = 3.
    const uint32_t T_LBT = ALETimingConstants::Twt_ms;
    const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;
    sm.update(T_LBT);   // LBT ends → TUNING
    sm.update(T_TX);    // TUNING ends → LEADING_CALL, full TX sequence enqueued

    bool tx_fired  = (transmit_count == 3);
    bool count_unchanged = (sm.get_call_cycle_count() == 0);  // NOT yet incremented
    bool pending_up      = (sm.get_words_pending() == 3);

    std::cout << "  after tune-complete: transmit_word called 3×: "
              << (tx_fired ? "PASS" : "FAIL") << " (got " << transmit_count << ")\n";
    std::cout << "  after tune-complete: call_cycle_count still 0: "
              << (count_unchanged ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";
    std::cout << "  after tune-complete: words_pending=3: "
              << (pending_up ? "PASS" : "FAIL")
              << " (got " << sm.get_words_pending() << ")\n";

    // Fire on_word_complete → call_cycle_count must now be 1
    sm.on_word_complete();
    bool count_incremented = (sm.get_call_cycle_count() == 1);
    bool pending_down      = (sm.get_words_pending() == 2);

    std::cout << "  after on_word_complete(): call_cycle_count=1: "
              << (count_incremented ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";
    std::cout << "  after on_word_complete(): words_pending=2: "
              << (pending_down ? "PASS" : "FAIL")
              << " (got " << sm.get_words_pending() << ")\n";

    return pre_ok && tx_fired && count_unchanged && pending_up
        && count_incremented && pending_down;
}

// ============================================================================
// AC-FRAME-001-3 — Inner-state sequence is exclusively
//   SCANNING_CALL → LEADING_CALL → CONCLUSION
// and every transition happens only inside on_word_complete(), never between.
// ============================================================================

bool test_ac_001_3_phase_sequence_via_callback_only()
{
    std::cout << "\n[AC-FRAME-001-3] Phase sequence and callback-only transitions\n";

    // scan_ch=2: Tsc = C×2 = 4 slots; TO "BOB" (1 word); self "SAM" (1 word).
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(2);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_transmit_callback([](const ALEWord&){});

    sm.initiate_call("BOB");

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    // Advance through LBT and TUNING — these are timer-based transitions that
    // legitimately happen inside update(), not in on_word_complete().
    const uint32_t T_LBT = ALETimingConstants::Twt_ms;
    const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;
    sm.update(T_LBT);   // LBT ends → TUNING
    sm.update(T_TX);    // TUNING ends → SCANNING_CALL, first_call_tx_ms = T_TX

    std::vector<CallingPhase> phase_log;
    // Record phase BEFORE update() and BEFORE on_word_complete().
    // A transition must not appear between the two — only inside the callback.
    // (This invariant applies only to word-based phase transitions SCANNING→…;
    //  LBT→TUNING→SCANNING_CALL are timer-based and happen inside update().)

    bool transition_outside_callback = false;

    // Helper: record current phase, call update(), check phase unchanged,
    // call on_word_complete(), record new phase.
    auto tick = [&](uint32_t t) {
        CallingPhase before_update = sm.get_calling_phase();
        sm.update(t);
        CallingPhase after_update  = sm.get_calling_phase();
        // Phase must not change inside update() (only on_word_complete() may change it)
        if (before_update != after_update)
            transition_outside_callback = true;

        if (sm.get_words_pending() > 0) {
            sm.on_word_complete();
            phase_log.push_back(sm.get_calling_phase()); // phase after callback
        }
    };

    // Drive 4 SCANNING slots (Tsc = 4 Trw-slots)
    for (uint32_t i = 0; i < 4; ++i)
        tick(T_TX + i * Trw);

    // Drive 2 LEADING slots (Tlc = 2 × 1 × Trw = 2 Trw-slots for 1-word address)
    for (uint32_t i = 4; i < 6; ++i)
        tick(T_TX + i * Trw);

    // Drive 1 CONCLUSION slot
    tick(T_TX + 6 * Trw);

    // ── Check no transition happened outside on_word_complete() ──────────
    bool no_outside_transition = !transition_outside_callback;
    std::cout << "  no phase change inside update(): "
              << (no_outside_transition ? "PASS" : "FAIL") << "\n";

    // ── Check phase sequence ──────────────────────────────────────────────
    // Expected: after slot 0–3 → SCANNING; after slot 3 → LEADING; after slots 4–5 → LEADING/CONCLUSION; ...
    // Concretely:
    //   after slot 0: SCANNING (call_cycles_in_phase=1 < 4)
    //   after slot 1: SCANNING (2 < 4)
    //   after slot 2: SCANNING (3 < 4)
    //   after slot 3: LEADING  (4 >= 4 → transition)
    //   after slot 4: LEADING  (1 < 2)
    //   after slot 5: CONCLUSION (2 >= 2 → transition)
    //   after slot 6: LISTENING  (1 >= 1 → transition)

    const CallingPhase expected[] = {
        CallingPhase::SCANNING_CALL,  // after slot 0
        CallingPhase::SCANNING_CALL,  // after slot 1
        CallingPhase::SCANNING_CALL,  // after slot 2
        CallingPhase::LEADING_CALL,   // after slot 3 (transition in callback)
        CallingPhase::LEADING_CALL,   // after slot 4
        CallingPhase::CONCLUSION,     // after slot 5 (transition in callback)
        CallingPhase::LISTENING,      // after slot 6 (transition in callback)
    };

    bool seq_ok = (phase_log.size() == 7);
    std::cout << "  7 on_word_complete() callbacks fired: "
              << (seq_ok ? "PASS" : "FAIL")
              << " (got " << phase_log.size() << ")\n";

    // Must match CallingPhase enum order exactly (LBT=0 … NET_CALL_STUB=8).
    static const char* PNAMES[] = {
        "LBT", "TUNING", "SCANNING_CALL", "LEADING_CALL", "MESSAGE",
        "CONCLUSION", "LISTENING", "SENDING_ACK", "NET_CALL_STUB"
    };

    bool all_phases_ok = seq_ok;
    for (size_t i = 0; seq_ok && i < 7; ++i) {
        bool ok = (phase_log[i] == expected[i]);
        all_phases_ok &= ok;
        std::cout << "  slot " << i << ": expected "
                  << PNAMES[static_cast<int>(expected[i])]
                  << ", got "
                  << PNAMES[static_cast<int>(phase_log[i])]
                  << ": " << (ok ? "PASS" : "FAIL") << "\n";
    }

    return no_outside_transition && all_phases_ok;
}

// ============================================================================
// AC-FRAME-001-4 (row 1) — Deterministic render contract: the complete TX
// sequence is enqueued at tune-complete and consumed as contiguous symbol
// frames.  Word boundaries are defined by the sample stream (one word =
// SYMBOLS_PER_WORD × FFT_SIZE samples), so word N starts exactly at sample
// N × 3136 — no wall-clock scheduling involved.
// ============================================================================

bool test_ac_001_4_contiguous_sequence_at_tune_complete()
{
    std::cout << "\n[AC-FRAME-001-4] TX sequence enqueued at tune-complete; "
                 "word grid = sample count\n";

    // scan_ch=1 (Tsc=2), addr="BOB" (1 word), self="SAM" (1 word).
    // Total logical words: 2 scan + 2 leading + 1 conclusion = 5.
    FrameHarness h("SAM", "BOB", 1);

    const uint32_t T_LBT = ALETimingConstants::Twt_ms;
    const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;

    h.tick(T_LBT);  // LBT ends → TUNING; no TX during pre-TX phases
    bool none_before = h.tx_bufs.empty();
    std::cout << "  no symbol frames before tune-complete: "
              << (none_before ? "PASS" : "FAIL") << "\n";

    h.tick(T_TX);   // TUNING ends → full sequence enqueued and pulled gap-free

    bool five = (h.tx_bufs.size() == 5);
    std::cout << "  5 contiguous symbol frames after tune-complete: "
              << (five ? "PASS" : "FAIL") << " (got " << h.tx_bufs.size() << ")\n";

    // Each frame carries exactly SYMBOLS_PER_WORD symbols → in the audio layer
    // word N occupies samples [N×3136, (N+1)×3136); the Trw grid (392 ms) is a
    // pure property of the sample stream.
    bool sizes_ok = true;
    for (const auto& buf : h.tx_bufs)
        sizes_ok &= (buf.size() == SYMBOLS_PER_WORD);
    std::cout << "  every frame has " << SYMBOLS_PER_WORD << " symbols: "
              << (sizes_ok ? "PASS" : "FAIL") << "\n";

    bool listening = (h.sm.get_calling_phase() == CallingPhase::LISTENING);
    std::cout << "  phase LISTENING after last completion: "
              << (listening ? "PASS" : "FAIL") << "\n";

    return none_before && five && sizes_ok && listening;
}

// ============================================================================
// AC-FRAME-001-4 (row 2) — Tsc = C×2×Trw, Tlc = 2×wpa×Trw, Tcc = Tsc+Tlc,
// all verified in Trw-slot counts.
// ============================================================================

bool test_ac_001_4_timing_formulas()
{
    std::cout << "\n[AC-FRAME-001-4] Tsc / Tlc / Tcc formulas (Trw-slot counts)\n";

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    struct Case {
        const char* to_addr;
        const char* self;
        uint32_t    scan_ch;
        uint32_t    expected_tsc_slots;   // C × 2
        uint32_t    expected_tlc_slots;   // 2 × wpa
        uint32_t    expected_tcc_slots;   // Tsc + Tlc
    };

    const Case cases[] = {
        // C=1, addr="SAM" (1 word), self="BOB" (1 word)
        { "SAM", "BOB", 1, 2,  2, 4  },
        // C=3, addr="SAM" (1 word), self="BOB"
        { "SAM", "BOB", 3, 6,  2, 8  },
        // C=2, addr="K6KB" (2 words: K6K + B@@), self="BOB"
        { "K6KB", "BOB", 2, 4, 4, 8  },
        // C=1, addr="MIAMI" (2 words: MIA + MI@), self="BOB"
        { "MIAMI", "BOB", 1, 2, 4, 6 },
    };

    bool all_ok = true;

    for (const auto& c : cases) {
        ALEStateMachine sm;
        sm.set_self_address(c.self);
        sm.set_target_scan_channels(c.scan_ch);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([](const ALEWord&){});

        sm.initiate_call(c.to_addr);

        // Advance through LBT and TUNING before counting word-phase slots.
        const uint32_t T_LBT = ALETimingConstants::Twt_ms;
        const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;
        sm.update(T_LBT);
        sm.update(T_TX);

        // Count slots in each phase by driving until CONCLUSION phase begins.
        uint32_t scanning_slots = 0;
        uint32_t leading_slots  = 0;

        for (uint32_t slot = 0; slot < 100; ++slot) {
            CallingPhase ph = sm.get_calling_phase();
            if (ph == CallingPhase::CONCLUSION || ph == CallingPhase::LISTENING)
                break;

            sm.update(T_TX + slot * Trw);
            sm.on_word_complete();

            if (ph == CallingPhase::SCANNING_CALL) ++scanning_slots;
            if (ph == CallingPhase::LEADING_CALL)  ++leading_slots;
        }

        bool tsc_ok  = (scanning_slots == c.expected_tsc_slots);
        bool tlc_ok  = (leading_slots  == c.expected_tlc_slots);
        bool tcc_ok  = ((scanning_slots + leading_slots) == c.expected_tcc_slots);

        all_ok &= tsc_ok && tlc_ok && tcc_ok;

        std::cout << "  to=\"" << c.to_addr << "\" C=" << c.scan_ch
                  << ": Tsc=" << scanning_slots << " (exp " << c.expected_tsc_slots << ")"
                  << " Tlc=" << leading_slots   << " (exp " << c.expected_tlc_slots << ")"
                  << " Tcc=" << (scanning_slots + leading_slots) << " (exp " << c.expected_tcc_slots << ")"
                  << " → " << (tsc_ok && tlc_ok && tcc_ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// Main
// ============================================================================

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis         ║\n";
    std::cout << "║  REQ-FRAME-001/002 Acceptance Tests                       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) { ++pass_count; }
        else        { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    // ── Frame class unit tests ────────────────────────────────────────────────
    run("AC-FRAME-002-1        encode() delegates to ALEWord::encode()",
        test_ac_002_1_encode_delegates_to_word_encode());

    run("AC-FRAME-002-2        encode() roundtrip via deinterleave_word()",
        test_ac_002_2_encode_roundtrip());

    run("AC-FRAME-002-3        encode() preserves word order (multi-word)",
        test_ac_002_3_multi_word_order());

    run("AC-FRAME-002-4        empty Frame → empty encode()",
        test_ac_002_4_empty_frame());

    // ── SM / modem integration timing tests ──────────────────────────────────
    run("AC-FRAME-001-2 (modem) ALE2GModem::Modulator emits one symbol frame (49 symbols) per word",
        test_ac_001_2_modem_symbol_frame_per_word());

    run("AC-FRAME-001-2 (SM)    call_cycle_count increments in on_word_complete()",
        test_ac_001_2_cycle_count_increments_in_callback());

    run("AC-FRAME-001-3         phase sequence via callback only",
        test_ac_001_3_phase_sequence_via_callback_only());

    run("AC-FRAME-001-4 (timing) contiguous TX sequence at tune-complete",
        test_ac_001_4_contiguous_sequence_at_tune_complete());

    run("AC-FRAME-001-4 (formulas) Tsc/Tlc/Tcc in Trw-slot counts",
        test_ac_001_4_timing_formulas());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count
              << "  Failed: " << std::setw(2) << fail_count
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main()
{
    return ale::run_all_tests();
}
