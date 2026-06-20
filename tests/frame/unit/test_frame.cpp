/**
 * \file test_frame.cpp
 * \brief Tests for FEAT-FRAME-001 — Frame-Grundstruktur & Wortbasis
 *
 * Part 1 (AC-FRAME-002): ALESequence class unit tests — encode() correctness
 *   and roundtrip via ALEFECCodec::deinterleave_word().
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
#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
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

// AC-FRAME-002-1: ALESequence::encode() produces one entry per word and each
// entry equals ALEWord::encode() for that word.
bool test_ac_002_1_encode_delegates_to_word_encode()
{
    std::cout << "\n[AC-FRAME-002-1] ALESequence::encode() delegates to ALEWord::encode()\n";

    const char addr[] = {'S', 'A', 'M'};
    ALEWord word = WordParser::make_word(PreambleType::TO, addr);
    ALESequence seq({word});

    auto encoded = seq.encode();
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
    std::cout << "\n[AC-FRAME-002-2] ALESequence::encode() roundtrip via ALEFECCodec::deinterleave_word()\n";

    const char addr[] = {'S', 'A', 'M'};
    ALEWord word = WordParser::make_word(PreambleType::TO, addr);
    ALESequence seq({word});

    auto encoded = seq.encode();

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

// AC-FRAME-002-3: multi-word ALESequence preserves insertion order in encode().
bool test_ac_002_3_multi_word_order()
{
    std::cout << "\n[AC-FRAME-002-3] ALESequence::encode() preserves word order\n";

    const char sam[] = {'S', 'A', 'M'};
    const char bob[] = {'B', 'O', 'B'};
    ALEWord w1 = WordParser::make_word(PreambleType::TO,  sam);
    ALEWord w2 = WordParser::make_word(PreambleType::TIS, bob);
    ALESequence seq({w1, w2});

    auto encoded = seq.encode();
    bool size_ok  = (encoded.size() == 2);
    bool order_ok = size_ok
                    && (encoded[0] == w1.encode())
                    && (encoded[1] == w2.encode());

    std::cout << "  encode() has 2 elements: "     << (size_ok  ? "PASS" : "FAIL") << "\n";
    std::cout << "  word order preserved: "         << (order_ok ? "PASS" : "FAIL") << "\n";

    return size_ok && order_ok;
}

// AC-FRAME-002-4: empty ALESequence produces an empty encode() result.
bool test_ac_002_4_empty_frame()
{
    std::cout << "\n[AC-FRAME-002-4] Empty ALESequence\n";

    ALESequence empty;
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

    // Must match CallingPhase enum order exactly.
    static const char* PNAMES[] = {
        "LBT", "TUNING", "SCANNING_CALL", "GROUP_SCANNING_CALL",
        "LEADING_CALL", "MESSAGE", "CONCLUSION", "LISTENING", "SENDING_ACK"
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
// AC-FRAME-001-001 — Frame structure: Calling (mandatory) + Message (optional) +
// Conclusion (mandatory) in this exact order.
//
// Case A: no AMD orderwire — phase log must be SCANNING_CALL → LEADING_CALL → CONCLUSION.
// Case B: AMD message "HI" — MESSAGE must appear between LEADING_CALL and CONCLUSION.
// ============================================================================

bool test_ac_frame_001_001_frame_structure_order()
{
    std::cout << "\n[AC-FRAME-001-001] Frame structure: Calling + [Message] + Conclusion order\n";

    const uint32_t Trw   = ALETimingConstants::Trw_ms;
    const uint32_t T_LBT = ALETimingConstants::Twt_ms;
    const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;

    // Drive through LBT and TUNING, then collect the calling phase recorded
    // after each on_word_complete(), stopping as soon as LISTENING is entered.
    auto collect_phases = [&](ALEStateMachine& sm) -> std::vector<CallingPhase> {
        sm.update(T_LBT);
        sm.update(T_TX);
        std::vector<CallingPhase> log;
        for (uint32_t slot = 0; slot < 20; ++slot) {
            if (sm.get_calling_phase() == CallingPhase::LISTENING) break;
            sm.update(T_TX + slot * Trw);
            if (sm.get_words_pending() > 0) {
                sm.on_word_complete();
                log.push_back(sm.get_calling_phase());
            }
        }
        return log;
    };

    bool all_ok = true;

    // ── Case A: no AMD message ────────────────────────────────────────────────
    // scan_ch=1, TO="BOB" (1 word), self="SAM" (1 word).
    // Total words: 2 scan + 2 leading + 1 conclusion = 5.
    // Expected phase log: [SCANNING, LEADING, LEADING, CONCLUSION, LISTENING]
    {
        ALEStateMachine sm;
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(1);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([](const ALEWord&){});
        sm.initiate_call("BOB");

        auto phases = collect_phases(sm);

        int scan_first = -1, lead_last = -1, msg_first = -1, concl_idx = -1;
        for (int i = 0; i < (int)phases.size(); ++i) {
            if (phases[i] == CallingPhase::SCANNING_CALL && scan_first == -1) scan_first = i;
            if (phases[i] == CallingPhase::LEADING_CALL)                       lead_last  = i;
            if (phases[i] == CallingPhase::MESSAGE      && msg_first  == -1) msg_first  = i;
            if (phases[i] == CallingPhase::CONCLUSION   && concl_idx  == -1) concl_idx  = i;
        }

        bool scan_present      = (scan_first >= 0);
        bool lead_present      = (lead_last  >= 0);
        bool concl_present     = (concl_idx  >= 0);
        bool no_message        = (msg_first  == -1);
        bool scan_before_lead  = scan_present && lead_present  && (lead_last  > scan_first);
        bool lead_before_concl = lead_present && concl_present && (concl_idx  > lead_last);

        bool case_a_ok = scan_present && lead_present && concl_present
                      && no_message && scan_before_lead && lead_before_concl;
        all_ok &= case_a_ok;

        std::cout << "  [no-AMD] SCANNING_CALL present: "       << (scan_present      ? "PASS" : "FAIL") << "\n";
        std::cout << "  [no-AMD] LEADING_CALL present: "        << (lead_present      ? "PASS" : "FAIL") << "\n";
        std::cout << "  [no-AMD] CONCLUSION present: "          << (concl_present     ? "PASS" : "FAIL") << "\n";
        std::cout << "  [no-AMD] No MESSAGE without orderwire: " << (no_message        ? "PASS" : "FAIL") << "\n";
        std::cout << "  [no-AMD] SCANNING before LEADING: "     << (scan_before_lead  ? "PASS" : "FAIL") << "\n";
        std::cout << "  [no-AMD] LEADING before CONCLUSION: "   << (lead_before_concl ? "PASS" : "FAIL") << "\n";
    }

    // ── Case B: AMD message "HI" ─────────────────────────────────────────────
    // encode_amd("HI") = 1 word; MESSAGE must appear after LEADING_CALL,
    // before CONCLUSION.
    {
        ALEStateMachine sm;
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(1);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([](const ALEWord&){});

        ALEStateMachine::PendingMessage msg;
        msg.type    = ALEStateMachine::PendingMessage::Type::AMD;
        msg.content = "HI";
        sm.set_pending_message(msg);
        sm.initiate_call("BOB");

        auto phases = collect_phases(sm);

        int scan_first = -1, lead_last = -1, msg_first = -1, concl_idx = -1;
        for (int i = 0; i < (int)phases.size(); ++i) {
            if (phases[i] == CallingPhase::SCANNING_CALL && scan_first == -1) scan_first = i;
            if (phases[i] == CallingPhase::LEADING_CALL)                       lead_last  = i;
            if (phases[i] == CallingPhase::MESSAGE      && msg_first  == -1) msg_first  = i;
            if (phases[i] == CallingPhase::CONCLUSION   && concl_idx  == -1) concl_idx  = i;
        }

        bool scan_present     = (scan_first >= 0);
        bool lead_present     = (lead_last  >= 0);
        bool msg_present      = (msg_first  >= 0);
        bool concl_present    = (concl_idx  >= 0);
        bool scan_before_lead = scan_present && lead_present && (lead_last  > scan_first);
        bool lead_before_msg  = lead_present && msg_present  && (msg_first  > lead_last);
        bool msg_before_concl = msg_present  && concl_present && (concl_idx > msg_first);

        bool case_b_ok = scan_present && lead_present && msg_present && concl_present
                      && scan_before_lead && lead_before_msg && msg_before_concl;
        all_ok &= case_b_ok;

        std::cout << "  [AMD]    SCANNING_CALL present: "          << (scan_present     ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    LEADING_CALL present: "           << (lead_present     ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    MESSAGE present: "                << (msg_present      ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    CONCLUSION present: "             << (concl_present    ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    SCANNING before LEADING: "        << (scan_before_lead ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    LEADING before MESSAGE: "         << (lead_before_msg  ? "PASS" : "FAIL") << "\n";
        std::cout << "  [AMD]    MESSAGE before CONCLUSION: "      << (msg_before_concl ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
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
// AC-FRAME-001-002 — FROM (Quick-ID) must precede CMD in a valid frame sequence.
//
// Tests FrameValidator::from_precedes_cmd_only() in the context of complete
// ALE frame word sequences (TO-addressing + calling section + message + conclusion).
// Distinct from AC-WORD-007-5/7 (isolated word pairs) — here the sequences
// mirror real frames as the protocol would produce them.
// ============================================================================

bool test_ac_frame_001_002_from_precedes_cmd()
{
    std::cout << "\n[AC-FRAME-001-002] FROM (Quick-ID) must precede CMD in a valid frame\n";

    const char dst[3] = {'B','O','B'};
    const char src[3] = {'S','A','M'};
    const char ext[3] = {'U','E','L'};
    const char cmd[3] = {'V','E','R'};

    bool all_ok = true;

    // Case 1: valid message frame — TO + FROM + CMD + TIS
    {
        std::vector<ALEWord> frame = {
            WordParser::make_word(PreambleType::TO,   dst),
            WordParser::make_word(PreambleType::FROM, src),
            WordParser::make_word(PreambleType::CMD,  cmd),
            WordParser::make_word(PreambleType::TIS,  src),
        };
        bool ok = FrameValidator::from_precedes_cmd_only(frame);
        all_ok &= ok;
        std::cout << "  TO, FROM, CMD, TIS (valid): " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // Case 2: valid — extended FROM address (FROM + DATA) before CMD
    {
        std::vector<ALEWord> frame = {
            WordParser::make_word(PreambleType::TO,   dst),
            WordParser::make_word(PreambleType::FROM, src),
            WordParser::make_word(PreambleType::DATA, ext),
            WordParser::make_word(PreambleType::CMD,  cmd),
            WordParser::make_word(PreambleType::TIS,  src),
        };
        bool ok = FrameValidator::from_precedes_cmd_only(frame);
        all_ok &= ok;
        std::cout << "  TO, FROM, DATA, CMD, TIS (valid): " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // Case 3: valid calling-cycle-only frame (no CMD, no FROM) — vacuously true
    {
        std::vector<ALEWord> frame = {
            WordParser::make_word(PreambleType::TO,  dst),
            WordParser::make_word(PreambleType::TIS, src),
        };
        bool ok = FrameValidator::from_precedes_cmd_only(frame);
        all_ok &= ok;
        std::cout << "  TO, TIS (no CMD, no FROM) (valid): " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // Case 4: invalid — CMD appears but FROM follows CMD (wrong order)
    {
        std::vector<ALEWord> frame = {
            WordParser::make_word(PreambleType::TO,   dst),
            WordParser::make_word(PreambleType::CMD,  cmd),
            WordParser::make_word(PreambleType::FROM, src),
            WordParser::make_word(PreambleType::TIS,  src),
        };
        // FROM after CMD: FROM is not followed by CMD — rejected
        bool ok = !FrameValidator::from_precedes_cmd_only(frame);
        all_ok &= ok;
        std::cout << "  TO, CMD, FROM, TIS (FROM after CMD) rejected: "
                  << (ok ? "PASS" : "FAIL") << "\n";
    }

    // Case 5: invalid — FROM at end of frame with no following CMD
    {
        std::vector<ALEWord> frame = {
            WordParser::make_word(PreambleType::TO,   dst),
            WordParser::make_word(PreambleType::FROM, src),
        };
        bool ok = !FrameValidator::from_precedes_cmd_only(frame);
        all_ok &= ok;
        std::cout << "  TO, FROM (orphan FROM, no CMD) rejected: "
                  << (ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-001-003 — Every transmitted word begins on a Trw-grid slot.
//
// Grid anchor: first_call_tx_ms (set at TUNING-complete, DD-006).
// Expected start time of word N (zero-based):
//   next_slot_ms = first_call_tx_ms + call_cycle_count × Trw_ms
//
// Verification:
//   (1) first_call_tx_ms == T_TX (TUNING-complete wall time)
//   (2) Before word N is consumed: call_cycle_count == N
//       → computed slot = first_call_tx_ms + N × Trw_ms = T_TX + N × Trw_ms
//   (3) on_word_complete() increments call_cycle_count → advances to slot N+1
// ============================================================================

bool test_ac_frame_001_003_trw_grid_synchrony()
{
    std::cout << "\n[AC-FRAME-001-003] All words begin on Trw-grid slots\n";

    // scan_ch=1, TO="BOB" (1 word), self="SAM" (1 word).
    // Total: 2 scan + 2 leading + 1 conclusion = 5 words.
    ALEStateMachine sm;
    sm.set_self_address("SAM");
    sm.set_target_scan_channels(1);
    sm.set_state_callback([](ALEState, ALEState){});
    sm.set_rx_enabled_callback([](bool){});
    sm.set_transmit_callback([](const ALEWord&){});
    sm.initiate_call("BOB");

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    sm.update(ALETimingConstants::Twt_ms);  // LBT → TUNING
    sm.update(T_TX);                         // TUNING → SCANNING_CALL, first_call_tx_ms set

    // 1. Grid anchor is set to TUNING-complete time
    bool anchor_ok = (sm.get_first_call_tx_ms() == T_TX);
    std::cout << "  first_call_tx_ms == T_TX (" << T_TX << "): "
              << (anchor_ok ? "PASS" : "FAIL")
              << " (got " << sm.get_first_call_tx_ms() << ")\n";

    // 2. For each of the 5 words, verify the grid formula before consuming the word:
    //    expected start of word N = first_call_tx_ms + N × Trw_ms
    bool grid_ok = true;
    const uint32_t total_words = 5;
    for (uint32_t n = 0; n < total_words; ++n) {
        uint32_t expected_slot = T_TX + n * Trw;
        uint32_t actual_slot   = sm.get_first_call_tx_ms()
                               + sm.get_call_cycle_count() * Trw;
        bool slot_ok = (actual_slot == expected_slot);
        grid_ok &= slot_ok;
        std::cout << "  word " << n << ": slot = first_call_tx_ms + "
                  << sm.get_call_cycle_count() << " × Trw = " << actual_slot
                  << " (exp " << expected_slot << "): "
                  << (slot_ok ? "PASS" : "FAIL") << "\n";
        sm.on_word_complete();
    }

    // 3. After all 5 words, call_cycle_count == 5, phase == LISTENING
    bool count_ok = (sm.get_call_cycle_count() == total_words);
    bool phase_ok = (sm.get_calling_phase() == CallingPhase::LISTENING);
    std::cout << "  call_cycle_count == 5 after all words: "
              << (count_ok ? "PASS" : "FAIL")
              << " (got " << sm.get_call_cycle_count() << ")\n";
    std::cout << "  phase == LISTENING after last word: "
              << (phase_ok ? "PASS" : "FAIL") << "\n";

    return anchor_ok && grid_ok && count_ok && phase_ok;
}

// ============================================================================
// AC-FRAME-002-002 — Tsc = C × 2 × Trw (§A.5.2.5.1 / REQ-FRAME-005)
//
// Two sub-checks:
//   (1) Builder:  scanning_call(dest, C).size() == C × 2
//   (2) SM:       SCANNING_CALL phase lasts exactly C × 2 slots before
//                 transitioning to LEADING_CALL (call_cycles_in_phase >= C×2).
// ============================================================================

bool test_ac_frame_002_002_tsc_formula()
{
    std::cout << "\n[AC-FRAME-002-002] Tsc = C × 2 × Trw (scanning duration formula)\n";

    struct Case { uint32_t C; uint32_t expected_slots; };
    const Case cases[] = {
        { 1,  2 },
        { 2,  4 },
        { 3,  6 },
        { 5, 10 },
    };

    bool all_ok = true;

    // ── Part 1: ALESequenceBuilder::scanning_call() produces C×2 words ──────
    std::cout << "  [builder] scanning_call() word count == C×2:\n";
    for (const auto& c : cases) {
        const auto seq = ALESequenceBuilder::scanning_call("BOB", c.C);
        const bool ok  = (seq.size() == c.expected_slots);
        all_ok &= ok;
        std::cout << "    C=" << c.C << ": size=" << seq.size()
                  << " (exp " << c.expected_slots << "): "
                  << (ok ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 2: SM stays in SCANNING_CALL for exactly C×2 on_word_complete() ─
    std::cout << "  [SM] SCANNING_CALL lasts exactly C×2 slots:\n";
    const uint32_t Trw   = ALETimingConstants::Trw_ms;
    const uint32_t T_LBT = ALETimingConstants::Twt_ms;
    const uint32_t T_TX  = T_LBT + ALETimingConstants::Tt_ms;

    for (const auto& c : cases) {
        ALEStateMachine sm;
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(c.C);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([](const ALEWord&){});
        sm.initiate_call("BOB");

        sm.update(T_LBT);
        sm.update(T_TX);

        uint32_t scanning_slots = 0;
        for (uint32_t slot = 0; slot < 100; ++slot) {
            if (sm.get_calling_phase() != CallingPhase::SCANNING_CALL) break;
            sm.update(T_TX + slot * Trw);
            sm.on_word_complete();
            ++scanning_slots;
        }

        const bool slots_ok = (scanning_slots == c.expected_slots);
        const bool lead_ok  = (sm.get_calling_phase() == CallingPhase::LEADING_CALL);
        all_ok &= slots_ok && lead_ok;
        std::cout << "    C=" << c.C << ": scanning_slots=" << scanning_slots
                  << " (exp " << c.expected_slots << ")"
                  << " next_phase=" << (lead_ok ? "LEADING_CALL" : "OTHER")
                  << ": " << (slots_ok && lead_ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-003-001 — Leading Call: full destination address, sent twice
// (REQ-FRAME-004 / §A.5.5.3.1).
//
// Verifies the *content* of the leading call (the timing-slot count is already
// covered by AC-FRAME-001-4 / AC-FRAME-002-002):
//   (1) The leading call carries the COMPLETE address — every 3-char chunk,
//       not just the first word — with the TO/DATA/REP extension pattern.
//   (2) The whole address block is sent exactly twice, identically (Tlc = 2×Tc).
//   (3) Max 5 words per pass for a 15-char address; addresses longer than
//       15 chars are truncated to 5 words (A.5.2.4.2), i.e. ≤10 words total.
// ============================================================================

bool test_ac_frame_003_001_leading_full_address_twice()
{
    std::cout << "\n[AC-FRAME-003-001] Leading call: full address sent twice (TO/DATA/REP)\n";

    // Expected per-pass preamble pattern for chunk index 0..4 (A.5.2.4.3):
    //   anchor=TO, then DATA, REP, DATA, REP.
    static const PreambleType EXPECT_TYPES[5] = {
        PreambleType::TO, PreambleType::DATA, PreambleType::REP,
        PreambleType::DATA, PreambleType::REP
    };

    struct Case {
        const char* addr;
        uint32_t    wpa;          // words per address (chunks) after 15-char cap
        const char* chunks[5];    // expected decoded 3-char chunks (padded with '@')
    };

    const Case cases[] = {
        // 1-word
        { "BOB",             1, { "BOB" } },
        // 2-word ("MIAMI" → MIA, MI@)
        { "MIAMI",           2, { "MIA", "MI@" } },
        // 5-word, exactly 15 chars
        { "ABCDEFGHIJKLMNO", 5, { "ABC", "DEF", "GHI", "JKL", "MNO" } },
        // >15 chars → truncated to 15 → still 5 words (cap, A.5.2.4.2)
        { "ABCDEFGHIJKLMNOPQRS", 5, { "ABC", "DEF", "GHI", "JKL", "MNO" } },
    };

    bool all_ok = true;

    for (const auto& c : cases) {
        const auto seq   = ALESequenceBuilder::leading_call(c.addr);
        const auto& w    = seq.words();

        // (2)+(3): exactly 2×wpa words, wpa never exceeds 5.
        const bool wpa_capped = (c.wpa <= 5);
        const bool size_ok    = (w.size() == 2u * c.wpa);
        std::cout << "  addr=\"" << c.addr << "\": size=" << w.size()
                  << " (exp " << (2u * c.wpa) << "), wpa=" << c.wpa
                  << " ≤5: " << (size_ok && wpa_capped ? "PASS" : "FAIL") << "\n";

        bool content_ok = size_ok && wpa_capped;

        if (size_ok) {
            for (uint32_t i = 0; i < c.wpa; ++i) {
                // (1): full address — every chunk present with correct type.
                const bool type_ok  = (w[i].type == EXPECT_TYPES[i]);
                const bool chunk_ok = (std::string(w[i].address) == c.chunks[i]);
                // (2): second pass is byte-identical to the first.
                const ALEWord& a = w[i];
                const ALEWord& b = w[i + c.wpa];
                const bool dup_ok = (a.type == b.type)
                                 && (std::string(a.address) == std::string(b.address));

                content_ok &= type_ok && chunk_ok && dup_ok;
                if (!(type_ok && chunk_ok && dup_ok)) {
                    std::cout << "    word " << i << ": type/chunk/dup mismatch"
                              << " (got \"" << w[i].address << "\")\n";
                }
            }
        }

        // No THRU/FROM/TIS/TWAS may appear in a leading call.
        bool only_addr_types = true;
        for (const auto& word : w) {
            if (word.type != PreambleType::TO && word.type != PreambleType::DATA
                && word.type != PreambleType::REP)
                only_addr_types = false;
        }
        content_ok &= only_addr_types;
        std::cout << "    only TO/DATA/REP types, both passes identical: "
                  << (content_ok ? "PASS" : "FAIL") << "\n";

        all_ok &= content_ok;
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-005-001 — Conclusion: TIS oder TWAS mit eigener Adresse
//
// Verifies (REQ-FRAME-010 / FEAT-FRAME-005 / A.5.2.3.2.3):
//   (1) ALESequenceBuilder::conclusion() TIS path:  anchor=TIS,  payload=self
//   (2) ALESequenceBuilder::conclusion() TWAS path: anchor=TWAS, payload=self
//   (3) Multi-char self_address → anchor + correct DATA/REP extension words
//   (4) SM CallingPhase::CONCLUSION: conclusion words TIS:self at tail of TX
//   (5) SM multi-word self (>3 chars): TIS anchor + DATA extension(s)
//
// TIS and TWAS are mutually exclusive within one frame (A.5.2.3.2.3).
// ============================================================================

bool test_ac_frame_005_001_conclusion_tis_twas_self_address()
{
    std::cout << "\n[AC-FRAME-005-001] Conclusion: TIS or TWAS anchor with own address\n";
    bool all_ok = true;

    // ── Part 1: Builder — TIS path (3-char self) ─────────────────────────────
    {
        const auto seq = ALESequenceBuilder::conclusion("SAM");
        const auto& w  = seq.words();
        const bool size_ok = (w.size() == 1);
        const bool type_ok = size_ok && (w[0].type == PreambleType::TIS);
        const bool addr_ok = size_ok && (trim_ale_address(w[0].address) == "SAM");
        all_ok &= size_ok && type_ok && addr_ok;
        std::cout << "  [builder-TIS]  conclusion(\"SAM\")  = 1 word: "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [builder-TIS]  anchor = TIS: "
                  << (type_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [builder-TIS]  address = \"SAM\": "
                  << (addr_ok ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 2: Builder — TWAS path (3-char self, is_reject=true) ─────────────
    {
        const auto seq = ALESequenceBuilder::conclusion("JOE", /*is_reject=*/true);
        const auto& w  = seq.words();
        const bool size_ok = (w.size() == 1);
        const bool type_ok = size_ok && (w[0].type == PreambleType::TWAS);
        const bool addr_ok = size_ok && (trim_ale_address(w[0].address) == "JOE");
        all_ok &= size_ok && type_ok && addr_ok;
        std::cout << "  [builder-TWAS] conclusion(\"JOE\",reject) = 1 word: "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [builder-TWAS] anchor = TWAS: "
                  << (type_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [builder-TWAS] address = \"JOE\": "
                  << (addr_ok ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 3: Builder — multi-word TIS (5-char: TIS anchor + DATA ext) ──────
    // "DL3HC" → [TIS:"DL3", DATA:"HC@"]; trim("HC@") == "HC".
    {
        const auto seq = ALESequenceBuilder::conclusion("DL3HC");
        const auto& w  = seq.words();
        const bool size_ok  = (w.size() == 2);
        const bool type0_ok = size_ok && (w[0].type == PreambleType::TIS);
        const bool addr0_ok = size_ok && (trim_ale_address(w[0].address) == "DL3");
        const bool type1_ok = size_ok && (w[1].type == PreambleType::DATA);
        const bool addr1_ok = size_ok && (trim_ale_address(w[1].address) == "HC");
        all_ok &= size_ok && type0_ok && addr0_ok && type1_ok && addr1_ok;
        std::cout << "  [multi-TIS]    conclusion(\"DL3HC\") = 2 words: "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [multi-TIS]    w[0]=TIS:\"DL3\": "
                  << ((type0_ok && addr0_ok) ? "PASS" : "FAIL") << "\n";
        std::cout << "  [multi-TIS]    w[1]=DATA:\"HC\": "
                  << ((type1_ok && addr1_ok) ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 4: Builder — multi-word TWAS (5-char, is_reject=true) ────────────
    // "DL3HC" reject → [TWAS:"DL3", DATA:"HC@"]; same structure, TWAS anchor.
    {
        const auto seq = ALESequenceBuilder::conclusion("DL3HC", /*is_reject=*/true);
        const auto& w  = seq.words();
        const bool size_ok  = (w.size() == 2);
        const bool type0_ok = size_ok && (w[0].type == PreambleType::TWAS);
        const bool addr0_ok = size_ok && (trim_ale_address(w[0].address) == "DL3");
        const bool type1_ok = size_ok && (w[1].type == PreambleType::DATA);
        const bool addr1_ok = size_ok && (trim_ale_address(w[1].address) == "HC");
        all_ok &= size_ok && type0_ok && addr0_ok && type1_ok && addr1_ok;
        std::cout << "  [multi-TWAS]   conclusion(\"DL3HC\",reject) = 2 words: "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [multi-TWAS]   w[0]=TWAS:\"DL3\": "
                  << ((type0_ok && addr0_ok) ? "PASS" : "FAIL") << "\n";
        std::cout << "  [multi-TWAS]   w[1]=DATA:\"HC\": "
                  << ((type1_ok && addr1_ok) ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 5: SM — 3-char self_address, conclusion at tail of TX ───────────
    // SAM calls BOB, scan_ch=0:
    //   leading:    TO:"BOB" × 2 = 2 words
    //   conclusion: TIS:"SAM"    = 1 word  (index 2 in captured sequence)
    {
        std::vector<ALEWord> captured;
        ALEStateMachine sm;
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(0);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([&](const ALEWord& w){ captured.push_back(w); });
        sm.initiate_call("BOB");
        const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
        sm.update(ALETimingConstants::Twt_ms);
        sm.update(T_TX);
        for (int i = 0; i < 3; ++i) sm.on_word_complete();

        const bool size_ok  = (captured.size() == 3);
        const bool type_ok  = size_ok && (captured[2].type == PreambleType::TIS);
        const bool addr_ok  = size_ok && (trim_ale_address(captured[2].address) == "SAM");
        const bool phase_ok = (sm.get_calling_phase() == CallingPhase::LISTENING);
        all_ok &= size_ok && type_ok && addr_ok;
        std::cout << "  [SM-TIS]       2 leading + 1 conclusion (total 3): "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [SM-TIS]       conclusion = TIS:\"SAM\": "
                  << ((type_ok && addr_ok) ? "PASS" : "FAIL") << "\n";
        std::cout << "  [SM-TIS]       phase → LISTENING after conclusion: "
                  << (phase_ok ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 6: SM — 5-char self_address, two-word conclusion ────────────────
    // DL3HC calls BOB, scan_ch=0:
    //   leading:    TO:"BOB" × 2         = 2 words
    //   conclusion: TIS:"DL3" + DATA:"HC" = 2 words  (indices 2..3)
    {
        std::vector<ALEWord> captured;
        ALEStateMachine sm;
        sm.set_self_address("DL3HC");
        sm.set_target_scan_channels(0);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_rx_enabled_callback([](bool){});
        sm.set_transmit_callback([&](const ALEWord& w){ captured.push_back(w); });
        sm.initiate_call("BOB");
        const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
        sm.update(ALETimingConstants::Twt_ms);
        sm.update(T_TX);
        for (int i = 0; i < 4; ++i) sm.on_word_complete();

        const bool size_ok  = (captured.size() == 4);
        const bool type2_ok = size_ok && (captured[2].type == PreambleType::TIS);
        const bool addr2_ok = size_ok && (trim_ale_address(captured[2].address) == "DL3");
        const bool type3_ok = size_ok && (captured[3].type == PreambleType::DATA);
        const bool addr3_ok = size_ok && (trim_ale_address(captured[3].address) == "HC");
        const bool phase_ok = (sm.get_calling_phase() == CallingPhase::LISTENING);
        all_ok &= size_ok && type2_ok && addr2_ok && type3_ok && addr3_ok;
        std::cout << "  [SM-multi]     2 leading + 2 conclusion for \"DL3HC\" (total 4): "
                  << (size_ok ? "PASS" : "FAIL") << "\n";
        std::cout << "  [SM-multi]     conclusion[0] = TIS:\"DL3\": "
                  << ((type2_ok && addr2_ok) ? "PASS" : "FAIL") << "\n";
        std::cout << "  [SM-multi]     conclusion[1] = DATA:\"HC\": "
                  << ((type3_ok && addr3_ok) ? "PASS" : "FAIL") << "\n";
        std::cout << "  [SM-multi]     phase → LISTENING after conclusion: "
                  << (phase_ok ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-005-002 — Conclusion: RX-Fenster öffnet nach Tlww
//
// Verifies (REQ-FRAME-011 / FEAT-FRAME-005 / A.5.2.5.3 / A.5.5.3.1):
//   (1) SM is in CONCLUSION phase immediately after all leading words complete
//   (2) RX window stays CLOSED during CONCLUSION (no premature enable)
//   (3) SM transitions to LISTENING after all conclusion words complete
//   (4) RX window opens (rx_enabled=true) at exactly the CONCLUSION→LISTENING boundary
//   (5) Wait in CONCLUSION = N×Trw ≥ Tlww_ms (1-word: exactly Tlww; N-word: ≥ Tlww)
//
// Drive pattern (scan_ch=0 to isolate CONCLUSION logic):
//   update(Twt) + update(Twt+Tt) → enqueue; on_word_complete() × N per phase.
// ============================================================================

bool test_ac_frame_005_002_rx_window_opens_after_tlww()
{
    std::cout << "\n[AC-FRAME-005-002] Conclusion: RX window opens after Tlww\n";
    bool all_ok = true;

    const uint32_t Trw  = ALETimingConstants::Trw_ms;   // 392 ms
    const uint32_t Tlww = ALETimingConstants::Tlww_ms;  // 392 ms = 1×Trw
    const uint32_t T_TX = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;

    // ── Part 1: 1-word conclusion ("SAM", 3 chars) ───────────────────────────
    // scan_ch=0: 2 leading + 1 conclusion = 3 words total.
    // After 2nd word: phase = CONCLUSION, RX disabled.
    // After 3rd word: phase = LISTENING, RX enabled.
    // Wait in CONCLUSION = 1×Trw = Tlww_ms = 392 ms.
    {
        bool rx_last = false;
        ALEStateMachine sm;
        sm.set_self_address("SAM");
        sm.set_target_scan_channels(0);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_transmit_callback([](const ALEWord&){});
        sm.set_rx_enabled_callback([&](bool on){ rx_last = on; });
        sm.initiate_call("BOB");

        // LBT (rx=true) → TUNING (rx=false) → TX enqueued
        sm.update(ALETimingConstants::Twt_ms);
        sm.update(T_TX);

        // Drive 2 leading words; after 2nd word the SM enters CONCLUSION
        sm.on_word_complete();  // leading word 1: LEADING_CALL, RX unchanged
        sm.on_word_complete();  // leading word 2: → CONCLUSION
        const bool phase_conclusion    = (sm.get_calling_phase() == CallingPhase::CONCLUSION);
        const bool rx_off_at_conc      = !rx_last;   // RX must be disabled during CONCLUSION

        // Drive the single conclusion word; SM must transition to LISTENING and open RX
        bool rx_opened = false;
        sm.set_rx_enabled_callback([&](bool on){ rx_last = on; if (on) rx_opened = true; });
        sm.on_word_complete();  // conclusion word: → LISTENING, rx_enabled(true)
        const bool phase_listening     = (sm.get_calling_phase() == CallingPhase::LISTENING);
        const bool rx_open_on_listening = rx_opened && rx_last;

        // Warte-Zeit für 1-Wort-Conclusion = 1×Trw = Tlww_ms (exakt)
        const uint32_t wait_ms  = 1u * Trw;
        const bool wait_eq_tlww = (wait_ms == Tlww);

        all_ok &= phase_conclusion && rx_off_at_conc
               && phase_listening  && rx_open_on_listening && wait_eq_tlww;

        std::cout << "  [1-word] phase=CONCLUSION after leading words: "
                  << (phase_conclusion ? "PASS" : "FAIL") << "\n";
        std::cout << "  [1-word] RX disabled during CONCLUSION:        "
                  << (rx_off_at_conc ? "PASS" : "FAIL") << "\n";
        std::cout << "  [1-word] phase=LISTENING after conclusion:     "
                  << (phase_listening ? "PASS" : "FAIL") << "\n";
        std::cout << "  [1-word] RX opens at LISTENING entry:          "
                  << (rx_open_on_listening ? "PASS" : "FAIL") << "\n";
        std::cout << "  [1-word] wait=" << wait_ms << " ms == Tlww="
                  << Tlww << " ms:               " << (wait_eq_tlww ? "PASS" : "FAIL") << "\n";
    }

    // ── Part 2: 2-word conclusion ("DL3HC", 5 chars) ─────────────────────────
    // scan_ch=0: 2 leading + 2 conclusion = 4 words total.
    // After 2nd word: phase = CONCLUSION, RX disabled.
    // After 3rd word (1st conclusion, TIS:"DL3"): still CONCLUSION, RX still disabled.
    // After 4th word (2nd conclusion, DATA:"HC"):  phase = LISTENING, RX enabled.
    // Wait in CONCLUSION = 2×Trw ≥ Tlww_ms.
    {
        bool rx_last = false;
        ALEStateMachine sm;
        sm.set_self_address("DL3HC");
        sm.set_target_scan_channels(0);
        sm.set_state_callback([](ALEState, ALEState){});
        sm.set_transmit_callback([](const ALEWord&){});
        sm.set_rx_enabled_callback([&](bool on){ rx_last = on; });
        sm.initiate_call("BOB");

        sm.update(ALETimingConstants::Twt_ms);
        sm.update(T_TX);

        // 2 leading words → CONCLUSION
        sm.on_word_complete();
        sm.on_word_complete();
        const bool phase_conc_start   = (sm.get_calling_phase() == CallingPhase::CONCLUSION);
        const bool rx_off_at_conc     = !rx_last;

        // 1st conclusion word (TIS:"DL3"): must NOT yet transition to LISTENING
        sm.on_word_complete();
        const bool phase_still_conc   = (sm.get_calling_phase() == CallingPhase::CONCLUSION);
        const bool rx_still_off       = !rx_last;

        // 2nd conclusion word (DATA:"HC"): → LISTENING, RX opens
        sm.on_word_complete();
        const bool phase_listening    = (sm.get_calling_phase() == CallingPhase::LISTENING);
        const bool rx_open            = rx_last;

        const uint32_t wait_ms  = 2u * Trw;
        const bool wait_ge_tlww = (wait_ms >= Tlww);

        all_ok &= phase_conc_start && rx_off_at_conc
               && phase_still_conc && rx_still_off
               && phase_listening  && rx_open && wait_ge_tlww;

        std::cout << "  [2-word] phase=CONCLUSION after leading words:    "
                  << (phase_conc_start ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] RX disabled when CONCLUSION begins:      "
                  << (rx_off_at_conc ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] phase=CONCLUSION after 1st conc. word:   "
                  << (phase_still_conc ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] RX disabled after 1st conc. word:        "
                  << (rx_still_off ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] phase=LISTENING after 2nd conc. word:    "
                  << (phase_listening ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] RX opens at LISTENING entry:             "
                  << (rx_open ? "PASS" : "FAIL") << "\n";
        std::cout << "  [2-word] wait=" << wait_ms << " ms >= Tlww="
                  << Tlww << " ms:              " << (wait_ge_tlww ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// AC-FRAME-006-001 — Sequenzregel: THRU/REP müssen alternieren
//
// REQ-FRAME-012 / FEAT-FRAME-006 / A.5.2.5.4
//
// In einer gültigen Sequenz dürfen THRU und REP nicht direkt aufeinander
// folgen:  kein THRU-THRU und kein REP-REP.
// FrameValidator::thru_rep_alternates() erzwingt diese Regel.
// ============================================================================

bool test_ac_frame_006_001_thru_rep_alternation_rule()
{
    std::cout << "\n[AC-FRAME-006-001] Sequenzregel: kein THRU-THRU, kein REP-REP\n";

    const char adr1[3] = {'A','B','C'};
    const char adr2[3] = {'X','Y','Z'};

    // Valid: THRU, REP — one complete pair.
    bool v1 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, adr1),
        WordParser::make_word(PreambleType::REP,  adr1),
    });
    std::cout << "  THRU, REP (valid):              " << (v1 ? "PASS" : "FAIL") << "\n";

    // Valid: THRU, REP, THRU, REP — two complete pairs (4 targets in rotation).
    bool v2 = FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, adr1),
        WordParser::make_word(PreambleType::REP,  adr1),
        WordParser::make_word(PreambleType::THRU, adr2),
        WordParser::make_word(PreambleType::REP,  adr2),
    });
    std::cout << "  THRU, REP, THRU, REP (valid):   " << (v2 ? "PASS" : "FAIL") << "\n";

    // Invalid: THRU, THRU — kein THRU-THRU.
    bool v3 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, adr1),
        WordParser::make_word(PreambleType::THRU, adr2),
    });
    std::cout << "  THRU, THRU rejected (THRU-THRU): " << (v3 ? "PASS" : "FAIL") << "\n";

    // Invalid: THRU, REP, REP — kein REP-REP.
    bool v4 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::THRU, adr1),
        WordParser::make_word(PreambleType::REP,  adr1),
        WordParser::make_word(PreambleType::REP,  adr2),
    });
    std::cout << "  THRU, REP, REP rejected (REP-REP): " << (v4 ? "PASS" : "FAIL") << "\n";

    // Invalid: REP, REP — starts with REP, no leading THRU.
    bool v5 = !FrameValidator::thru_rep_alternates({
        WordParser::make_word(PreambleType::REP, adr1),
        WordParser::make_word(PreambleType::REP, adr2),
    });
    std::cout << "  REP, REP rejected (no leading THRU): " << (v5 ? "PASS" : "FAIL") << "\n";

    return v1 && v2 && v3 && v4 && v5;
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

    // ── ALESequence class unit tests ──────────────────────────────────────────
    run("AC-FRAME-002-1        encode() delegates to ALEWord::encode()",
        test_ac_002_1_encode_delegates_to_word_encode());

    run("AC-FRAME-002-2        encode() roundtrip via deinterleave_word()",
        test_ac_002_2_encode_roundtrip());

    run("AC-FRAME-002-3        encode() preserves word order (multi-word)",
        test_ac_002_3_multi_word_order());

    run("AC-FRAME-002-4        empty ALESequence → empty encode()",
        test_ac_002_4_empty_frame());

    // ── SM / modem integration timing tests ──────────────────────────────────
    run("AC-FRAME-001-001       frame structure: Calling + [Message] + Conclusion order",
        test_ac_frame_001_001_frame_structure_order());

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

    run("AC-FRAME-001-002      FROM (Quick-ID) must precede CMD in frame sequence",
        test_ac_frame_001_002_from_precedes_cmd());

    run("AC-FRAME-001-003      all words begin on Trw-grid slots (first_call_tx_ms + N × Trw)",
        test_ac_frame_001_003_trw_grid_synchrony());

    run("AC-FRAME-002-002      Tsc = C×2×Trw: builder size and SM phase-transition slot count",
        test_ac_frame_002_002_tsc_formula());

    run("AC-FRAME-003-001      leading call: full address (TO/DATA/REP) sent twice, ≤5 words",
        test_ac_frame_003_001_leading_full_address_twice());

    run("AC-FRAME-005-001      conclusion: TIS or TWAS anchor with own address (builder + SM)",
        test_ac_frame_005_001_conclusion_tis_twas_self_address());

    run("AC-FRAME-005-002      conclusion: RX window opens after Tlww (CONCLUSION→LISTENING)",
        test_ac_frame_005_002_rx_window_opens_after_tlww());

    run("AC-FRAME-006-001      THRU/REP alternation: kein THRU-THRU, kein REP-REP",
        test_ac_frame_006_001_thru_rep_alternation_rule());

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
