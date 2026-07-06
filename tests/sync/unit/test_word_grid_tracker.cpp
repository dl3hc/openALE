/**
 * \file tests/sync/unit/test_word_grid_tracker.cpp
 * \brief AC-GRID-001..013 — WordGridTracker unit tests (no PCM).
 *
 * Tests inject DecodedCandidate values directly into WordGridTracker::process_candidate()
 * and assert grid-lock state and word-callback behaviour.  No signal generation,
 * no Goertzel, no ring buffer — pure grid-lock / refinement / gate logic.
 *
 * Coverage:
 *   001  cold acquisition — TO cold-locks the grid
 *   002  cold acquisition — DATA does NOT cold-lock
 *   003  cold acquisition — TIS / TWAS / FROM all cold-lock
 *   004  boundary refinement — higher-energy candidate committed
 *   005  boundary refinement — first candidate committed when it is the best
 *   006  min-spacing gate — no duplicate callback after lock
 *   007  on-grid FEC-corrected word accepted after lock
 *   008  off-grid FEC-corrected word rejected after lock
 *   009  silence gap resets grid lock
 *   010  silence gap commits pending refinement word
 *   011  uncorrectable word surfaces on locked grid at exact slot
 *   012  uncorrectable word NOT surfaced off-grid
 *   013  adaptive FEC mode shifts toward Mode1_6 on repeated clean words
 */

#include "FEC/golay.h"
#include "FSK/ale_waveform.h"
#include "Modem/word_grid_tracker.h"
#include "Word/ale_word.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace ale;
using namespace ale::ALE2GModem;

namespace {

static int g_failures = 0;
static int g_tests    = 0;

void check(bool cond, const char* msg)
{
    ++g_tests;
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        ++g_failures;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static constexpr uint32_t W = SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL;  // WORD_SAMPLES = 3136
static constexpr uint32_t S = SAMPLES_PER_SYMBOL;                     // 64

/// Build a clean decoded candidate (DECODE_OK, 49 unanimous votes).
static DecodedCandidate make_clean(PreambleType type, float energy = 1000.0f)
{
    DecodedCandidate c;
    c.word            = WordParser::make_word(type, "SAM");
    c.fec.flag        = Golay::DECODE_OK;
    c.unanimous_votes = 49;
    c.word_energy     = energy;
    c.decoded_ok      = true;
    return c;
}

/// Build a FEC-corrected candidate (DECODE_CORRECTED, 40 unanimous votes).
static DecodedCandidate make_corrected(PreambleType type, float energy = 1000.0f)
{
    DecodedCandidate c;
    c.word            = WordParser::make_word(type, "SAM");
    c.fec.flag        = Golay::DECODE_CORRECTED;
    c.unanimous_votes = 40;
    c.word_energy     = energy;
    c.decoded_ok      = true;
    return c;
}

/// Null candidate: decoded_ok=false, votes=0 — passes neither emit branch.
static DecodedCandidate make_null()
{
    return DecodedCandidate{};
}

/// Drive the tracker to a locked state and return the anchor position.
/// Returns the write_pos at which the grid was committed.
static uint32_t lock_grid(WordGridTracker& t, uint32_t start = W)
{
    DecodedCandidate c = make_clean(PreambleType::TO, 1000.0f);
    t.process_candidate(c, start);               // opens refinement
    t.process_candidate(make_null(), start + S); // closes refinement, fires callback
    return start;  // anchor = start (best_pos of first and only candidate)
}

// ─────────────────────────────────────────────────────────────────────────────

void test_001_to_cold_locks()
{
    std::printf("[AC-GRID-001] cold acquisition: TO cold-locks grid\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    check(!t.is_grid_locked(), "001: initially unlocked");

    DecodedCandidate c = make_clean(PreambleType::TO);
    t.process_candidate(c, W);               // opens refinement
    check(!t.is_grid_locked(), "001: not locked mid-refinement");
    check(fired == 0,          "001: no callback during refinement");

    t.process_candidate(make_null(), W + S); // closes refinement → commit
    check(t.is_grid_locked(), "001: locked after commit");
    check(fired == 1,         "001: exactly one callback after commit");

    std::printf("  grid_locked=%s callbacks=%d: %s\n",
                t.is_grid_locked() ? "true" : "false", fired,
                (t.is_grid_locked() && fired == 1) ? "PASS" : "FAIL");
}

void test_002_data_does_not_lock()
{
    std::printf("[AC-GRID-002] cold acquisition: DATA does NOT cold-lock\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    DecodedCandidate c = make_clean(PreambleType::DATA);
    t.process_candidate(c, W);
    t.process_candidate(make_null(), W + S);

    check(!t.is_grid_locked(), "002: DATA must not cold-lock grid");
    check(fired == 0,          "002: no callback for DATA cold-acquisition");

    const char* pass = (!t.is_grid_locked() && fired == 0) ? "PASS" : "FAIL";
    std::printf("  grid_locked=%s callbacks=%d: %s\n",
                t.is_grid_locked() ? "true" : "false", fired, pass);
}

void test_003_other_address_preambles_lock()
{
    std::printf("[AC-GRID-003] cold acquisition: TIS / TWAS / FROM all cold-lock\n");

    const PreambleType types[] = {
        PreambleType::TIS, PreambleType::TWAS, PreambleType::FROM
    };
    const char* names[] = { "TIS", "TWAS", "FROM" };

    for (size_t i = 0; i < 3; ++i) {
        WordGridTracker t;
        int fired = 0;
        t.set_word_callback([&](const ALEWord&) { ++fired; });

        t.process_candidate(make_clean(types[i]), W);
        t.process_candidate(make_null(), W + S);

        check(t.is_grid_locked() && fired == 1,
              "003: address preamble must cold-lock grid");
        std::printf("  %-4s: grid=%s fired=%d: %s\n", names[i],
                    t.is_grid_locked() ? "Y" : "N", fired,
                    (t.is_grid_locked() && fired == 1) ? "PASS" : "FAIL");
    }
}

void test_004_refinement_picks_higher_energy()
{
    std::printf("[AC-GRID-004] boundary refinement: higher-energy candidate committed\n");

    WordGridTracker t;
    ALEWord committed;
    t.set_word_callback([&](const ALEWord& w) { committed = w; });

    // Low-energy first candidate
    DecodedCandidate c1 = make_clean(PreambleType::TO, 400.0f);
    c1.word.sinad_db = 10.0f;

    // Higher-energy second candidate (better aligned, higher SINAD)
    DecodedCandidate c2 = make_clean(PreambleType::TO, 1600.0f);
    c2.word.sinad_db = 28.0f;

    t.process_candidate(c1, W);           // opens refinement, best=c1
    t.process_candidate(c2, W + 4);       // still in window, best=c2
    t.process_candidate(make_null(), W + S); // commit

    check(committed.sinad_db == 28.0f,
          "004: higher-energy candidate (sinad=28) must be committed");
    std::printf("  committed sinad=%.1f (expect 28): %s\n",
                committed.sinad_db,
                (committed.sinad_db == 28.0f) ? "PASS" : "FAIL");
}

void test_005_refinement_keeps_first_if_best()
{
    std::printf("[AC-GRID-005] boundary refinement: first candidate committed if best\n");

    WordGridTracker t;
    ALEWord committed;
    t.set_word_callback([&](const ALEWord& w) { committed = w; });

    DecodedCandidate c1 = make_clean(PreambleType::TO, 2000.0f);
    c1.word.sinad_db = 29.0f;

    DecodedCandidate c2 = make_clean(PreambleType::TO, 500.0f);
    c2.word.sinad_db = 11.0f;

    t.process_candidate(c1, W);
    t.process_candidate(c2, W + 4);
    t.process_candidate(make_null(), W + S);

    check(committed.sinad_db == 29.0f,
          "005: first candidate (sinad=29) must be committed when it has higher energy");
    std::printf("  committed sinad=%.1f (expect 29): %s\n",
                committed.sinad_db,
                (committed.sinad_db == 29.0f) ? "PASS" : "FAIL");
}

void test_006_min_spacing_gate()
{
    std::printf("[AC-GRID-006] min-spacing gate: no duplicate callback after lock\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    lock_grid(t, W);
    const int after_lock = fired;

    // Second clean word at write_pos = W + S — only 64 samples after the anchor.
    // min_spacing = W - S = 3072 so this must be rejected.
    DecodedCandidate c = make_clean(PreambleType::TO);
    t.process_candidate(c, W + S);
    t.process_candidate(make_null(), W + S + S);

    check(fired == after_lock,
          "006: second word < min_spacing after anchor must not fire callback");
    std::printf("  callbacks after lock=%d (no new ones expected): %s\n",
                fired - after_lock,
                (fired == after_lock) ? "PASS" : "FAIL");
}

void test_007_on_grid_corrected_accepted()
{
    std::printf("[AC-GRID-007] on-grid FEC-corrected word accepted after lock\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    lock_grid(t, W);
    const int after_lock = fired;

    // Exactly one word later (phase 0) — FEC-corrected should be accepted.
    DecodedCandidate c = make_corrected(PreambleType::TO);
    const uint32_t next_word_pos = W + W;
    t.process_candidate(c, next_word_pos);
    t.process_candidate(make_null(), next_word_pos + S);

    check(fired == after_lock + 1,
          "007: on-grid FEC-corrected word must be accepted");
    std::printf("  on-grid CORRECTED → callback fired=%s: %s\n",
                (fired == after_lock + 1) ? "yes" : "no",
                (fired == after_lock + 1) ? "PASS" : "FAIL");
}

void test_008_off_grid_corrected_rejected()
{
    std::printf("[AC-GRID-008] off-grid FEC-corrected word rejected after lock\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    lock_grid(t, W);
    const int after_lock = fired;

    // 200 samples past the grid boundary: phase = 200, outside ±S = ±64 window.
    DecodedCandidate c = make_corrected(PreambleType::TO);
    t.process_candidate(c, W + W + 200);
    t.process_candidate(make_null(), W + W + 200 + S);

    check(fired == after_lock,
          "008: off-grid FEC-corrected word must be rejected");
    std::printf("  off-grid CORRECTED (+200) → no new callback: %s\n",
                (fired == after_lock) ? "PASS" : "FAIL");
}

void test_009_silence_gap_resets_grid()
{
    std::printf("[AC-GRID-009] silence gap resets grid lock\n");

    WordGridTracker t;
    lock_grid(t, W);
    check(t.is_grid_locked(), "009: pre-condition: grid must be locked");

    t.on_silence_gap();

    check(!t.is_grid_locked(), "009: grid must be unlocked after silence gap");
    std::printf("  grid_locked after on_silence_gap: %s: %s\n",
                t.is_grid_locked() ? "true" : "false",
                !t.is_grid_locked() ? "PASS" : "FAIL");
}

void test_010_silence_gap_commits_refinement()
{
    std::printf("[AC-GRID-010] silence gap commits pending refinement\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    // Open a refinement window but don't close it.
    DecodedCandidate c = make_clean(PreambleType::TO);
    t.process_candidate(c, W);
    check(fired == 0, "010: no callback while refinement still open");

    // Silence gap arrives before refinement window closes naturally.
    t.on_silence_gap();

    check(fired == 1,         "010: silence gap must commit the pending refinement word");
    check(!t.is_grid_locked(), "010: grid must be released after silence gap");
    std::printf("  silence-gap commit: fired=%d locked=%s: %s\n",
                fired, t.is_grid_locked() ? "true" : "false",
                (fired == 1 && !t.is_grid_locked()) ? "PASS" : "FAIL");
}

void test_011_uncorrectable_surfaces_on_grid()
{
    std::printf("[AC-GRID-011] uncorrectable word surfaces on locked grid\n");

    WordGridTracker t;
    std::vector<ALEWord> received;
    t.set_word_callback([&](const ALEWord& w) { received.push_back(w); });

    lock_grid(t, W);
    const size_t after_lock = received.size();

    // Construct an uncorrectable candidate at the exact grid slot (write_pos = W + W).
    DecodedCandidate unc;
    unc.decoded_ok           = false;
    unc.unanimous_votes      = 40;  // >= min_unanimous (33)
    unc.word.golay_uncorrectable = true;
    unc.word.valid           = false;
    unc.word.type            = PreambleType::TO;

    t.process_candidate(unc, W + W);

    check(received.size() == after_lock + 1,
          "011: uncorrectable word at exact grid slot must fire callback");
    if (received.size() > after_lock) {
        check(!received.back().valid,
              "011: surfaced word must have valid=false");
        check(received.back().golay_uncorrectable,
              "011: surfaced word must have golay_uncorrectable=true");
    }
    const bool pass = (received.size() == after_lock + 1
                       && !received.back().valid
                       && received.back().golay_uncorrectable);
    std::printf("  on-grid uncorrectable surfaced valid=F golay_unc=T: %s\n",
                pass ? "PASS" : "FAIL");
}

void test_012_uncorrectable_not_surfaced_off_grid()
{
    std::printf("[AC-GRID-012] uncorrectable word NOT surfaced off-grid\n");

    WordGridTracker t;
    int fired = 0;
    t.set_word_callback([&](const ALEWord&) { ++fired; });

    lock_grid(t, W);
    const int after_lock = fired;

    // Off-grid position: W + W + 200 → phase=200, which is != 0.
    DecodedCandidate unc;
    unc.decoded_ok           = false;
    unc.unanimous_votes      = 40;
    unc.word.golay_uncorrectable = true;
    unc.word.valid           = false;

    t.process_candidate(unc, W + W + 200);

    check(fired == after_lock,
          "012: off-grid uncorrectable word must NOT fire callback");
    std::printf("  off-grid uncorrectable (+200) → no callback: %s\n",
                (fired == after_lock) ? "PASS" : "FAIL");
}

void test_013_adaptive_fec_shifts_mode()
{
    std::printf("[AC-GRID-013] adaptive FEC mode shifts toward Mode1_6 on clean link\n");

    WordGridTracker t;
    t.set_adaptive_fec(true);
    check(t.golay_mode() == GolayMode::Mode3_4,
          "013: initial mode must be Mode3_4");

    // Lock the grid once (seeds the adaptive FEC observer).
    t.set_word_callback([](const ALEWord&){});
    lock_grid(t, W);

    // Feed many more clean words (49 votes each) so the quality EWMA climbs.
    // Each word needs a full refinement cycle.
    for (int i = 1; i <= 30; ++i) {
        const uint32_t base = W + static_cast<uint32_t>(i) * W;
        t.process_candidate(make_clean(PreambleType::TO, 1000.0f), base);
        t.process_candidate(make_null(), base + S);
    }

    // After many 49-vote words the quality should have climbed past CLEAN_LO (45)
    // and the adaptive mode should have shifted away from Mode3_4.
    const bool shifted = (t.golay_mode() == GolayMode::Mode2_5 ||
                          t.golay_mode() == GolayMode::Mode1_6);
    check(shifted, "013: mode must shift toward Mode1_6 after many clean words");
    std::printf("  mode after 30 clean words: %s (expect Mode2_5 or Mode1_6): %s\n",
                t.golay_mode() == GolayMode::Mode1_6 ? "Mode1_6" :
                t.golay_mode() == GolayMode::Mode2_5 ? "Mode2_5" : "Mode3_4",
                shifted ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    std::printf("\n");
    std::printf("╔══════════════════════════════════════════════════════════════════╗\n");
    std::printf("║  AC-GRID-001..013 — WordGridTracker unit tests (no PCM)         ║\n");
    std::printf("╚══════════════════════════════════════════════════════════════════╝\n\n");

    test_001_to_cold_locks();        std::printf("\n");
    test_002_data_does_not_lock();   std::printf("\n");
    test_003_other_address_preambles_lock(); std::printf("\n");
    test_004_refinement_picks_higher_energy(); std::printf("\n");
    test_005_refinement_keeps_first_if_best(); std::printf("\n");
    test_006_min_spacing_gate();     std::printf("\n");
    test_007_on_grid_corrected_accepted(); std::printf("\n");
    test_008_off_grid_corrected_rejected(); std::printf("\n");
    test_009_silence_gap_resets_grid(); std::printf("\n");
    test_010_silence_gap_commits_refinement(); std::printf("\n");
    test_011_uncorrectable_surfaces_on_grid(); std::printf("\n");
    test_012_uncorrectable_not_surfaced_off_grid(); std::printf("\n");
    test_013_adaptive_fec_shifts_mode(); std::printf("\n");

    std::printf("══════════════════════════════════════════════════════════════════\n");
    if (g_failures == 0) {
        std::printf("PASS  %d/%d checks\n", g_tests, g_tests);
        return 0;
    }
    std::fprintf(stderr, "FAIL  %d/%d passed — %d failure(s)\n",
                 g_tests - g_failures, g_tests, g_failures);
    return 1;
}
