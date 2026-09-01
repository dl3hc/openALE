/**
 * \file tests/chan/unit/test_sounding_identity_accumulator.cpp
 * \brief AC-SND-001..006 — SoundingIdentityAccumulator unit tests (no PCM, no
 *        ALEController, no SM). Tests inject ALEWord values directly into
 *        on_word()/timed_out()/finalize() and assert the reassembled address,
 *        averaged metrics, and timer behaviour.
 *
 * Coverage:
 *   001  single clean cycle reconstructs the full address
 *   002  originally-reported bug: a later repeat's lost extension word does
 *        not discard an earlier repeat's fully-assembled address
 *   003  positional corruption: a type-mismatched word is never slot-assigned;
 *        a later clean repeat still recovers the full address
 *   004  CONFIRMED anchor mismatch (repeats twice) flushes the finished
 *        station first, with no cross-contamination between the two
 *        stations' addresses
 *   005  golay_uncorrectable word contributes to linear BER/SNR/SINAD only,
 *        never to the reassembled station string
 *   006  settle-refresh regression guard: an invalid-but-not-uncorrectable
 *        word mid-burst refreshes the inactivity timer — the exact gap that
 *        caused the originally-reported premature commit
 *   007  root-cause regression guard: an UNCONFIRMED (single-observation)
 *        anchor mismatch — e.g. a one-off Golay miscorrection — does not
 *        flush, does not truncate, and does not contaminate the open
 *        session; the real station's next repeat recovers cleanly
 *   008  an unconfirmed mismatch followed by the ORIGINAL station's own
 *        settle (never confirmed) commits the original session untouched —
 *        the stray word leaves no trace
 */

#include "App/sounding_identity_accumulator.h"
#include "Word/ale_word.h"

#include <cmath>
#include <cstdio>

using namespace ale;

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

constexpr uint32_t kFreq = 7102000u;

/// Build a word with explicit LQA-relevant fields (WordParser::make_word()
/// only sets type/address/valid from character content — votes/uncorrectable/
/// sinad default to 0/false/0.0 and must be set explicitly per test case).
static ALEWord make_word(PreambleType type, const char chars[3], bool valid = true,
                          uint8_t votes = 48, bool uncorrectable = false, float sinad = 20.0f)
{
    ALEWord w = WordParser::make_word(type, chars);
    w.valid               = valid;
    w.unanimous_votes     = votes;
    w.golay_uncorrectable = uncorrectable;
    w.sinad_db            = sinad;
    return w;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void test_001_single_clean_cycle()
{
    std::printf("[AC-SND-001] single clean cycle reconstructs the full address\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    auto r1 = acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t);
    check(!r1.has_value(), "001: opening a session returns no immediate result");
    t += 100;
    auto r2 = acc.on_word(make_word(PreambleType::DATA, "ZXB"), kFreq, t);
    check(!r2.has_value(), "001: folding an extension returns no immediate result");

    auto result = acc.finalize();
    check(result.has_value(), "001: finalize() produces a result");
    const bool pass = result.has_value() && result->station == "SL3ZXB"
        && result->frequency_hz == kFreq && !result->twas_conclusion;
    check(result.has_value() && result->station == "SL3ZXB", "001: station == \"SL3ZXB\"");
    check(result.has_value() && result->frequency_hz == kFreq, "001: frequency_hz preserved");
    check(result.has_value() && !result->twas_conclusion, "001: TIS -> twas_conclusion == false");

    std::printf("  station=%s: %s\n", result ? result->station.c_str() : "(none)",
                pass ? "PASS" : "FAIL");
}

void test_002_repeat_redundancy_recovers_dropped_extension()
{
    std::printf("[AC-SND-002] originally-reported bug: repeat redundancy recovers a dropped extension\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    // Repeat 1: anchor + extension, complete.
    acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t);  t += 100;
    acc.on_word(make_word(PreambleType::DATA, "ZXB"), kFreq, t); t += 100;
    // Repeat 2: anchor only — its extension word is lost before timeout.
    acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t);  t += 100;

    auto result = acc.finalize();
    check(result.has_value(), "002: finalize() produces a result");
    const bool pass = result.has_value() && result->station == "SL3ZXB";
    check(pass, "002: station == \"SL3ZXB\" (recovered from repeat 1, not truncated to \"SL3\")");

    std::printf("  station=%s: %s\n", result ? result->station.c_str() : "(none)",
                pass ? "PASS" : "FAIL");
}

void test_003_positional_mismatch_not_assigned_then_recovered()
{
    std::printf("[AC-SND-003] positional corruption: mismatched word never slot-assigned; later repeat recovers\n");

    // Part A: TIS + a REP where DATA was expected (slot 1 type mismatch).
    // The REP's content must NOT be accepted into slot 1.
    {
        SoundingIdentityAccumulator acc;
        uint32_t t = 0;
        acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t); t += 100;
        acc.on_word(make_word(PreambleType::REP, "ZXB"), kFreq, t); t += 100; // slot 1 expects DATA, not REP

        auto result = acc.finalize();
        check(result.has_value(), "003a: finalize() produces a result");
        const bool pass = result.has_value() && result->station == "SL3";
        check(pass, "003a: station == \"SL3\" (mismatched REP content excluded from slot 1)");
        std::printf("  part A station=%s: %s\n", result ? result->station.c_str() : "(none)",
                    pass ? "PASS" : "FAIL");
    }

    // Part B: same mismatch, but a later clean repeat (same anchor) supplies
    // the correct DATA word — cross-cycle voting recovers the full address.
    {
        SoundingIdentityAccumulator acc;
        uint32_t t = 0;
        acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t); t += 100;
        acc.on_word(make_word(PreambleType::REP, "ZXB"), kFreq, t); t += 100; // mismatch, cycle closes
        acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t); t += 100; // repeat 2, same anchor
        acc.on_word(make_word(PreambleType::DATA, "ZXB"), kFreq, t); t += 100; // now correctly in slot 1

        auto result = acc.finalize();
        check(result.has_value(), "003b: finalize() produces a result");
        const bool pass = result.has_value() && result->station == "SL3ZXB";
        check(pass, "003b: station == \"SL3ZXB\" (recovered via later clean repeat)");
        std::printf("  part B station=%s: %s\n", result ? result->station.c_str() : "(none)",
                    pass ? "PASS" : "FAIL");
    }
}

void test_004_confirmed_anchor_mismatch_flushes_without_cross_contamination()
{
    std::printf("[AC-SND-004] CONFIRMED anchor mismatch (repeats) flushes finished station, no cross-contamination\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, t);  t += 100;
    acc.on_word(make_word(PreambleType::DATA, "ZXB"), kFreq, t); t += 100; // station A complete

    // First sighting of "BOB" is parked, not trusted yet.
    auto unconfirmed = acc.on_word(make_word(PreambleType::TIS, "BOB"), kFreq, t);
    t += 100;
    check(!unconfirmed.has_value(), "004: a single differing anchor does not flush");

    // Repeat confirms it really is a new station.
    auto flushed = acc.on_word(make_word(PreambleType::TIS, "BOB"), kFreq, t);
    t += 100;
    check(flushed.has_value(), "004: confirmed anchor mismatch returns a flushed result");
    const bool a_pass = flushed.has_value() && flushed->station == "SL3ZXB";
    check(a_pass, "004: flushed station == \"SL3ZXB\" (station A, complete)");

    acc.on_word(make_word(PreambleType::DATA, "SON"), kFreq, t); t += 100;
    auto b = acc.finalize();
    check(b.has_value(), "004: finalize() produces station B's result");
    const bool b_pass = b.has_value() && b->station == "BOBSON";
    check(b_pass, "004: station == \"BOBSON\" (no cross-contamination with station A)");

    std::printf("  A=%s B=%s: %s\n",
                flushed ? flushed->station.c_str() : "(none)",
                b ? b->station.c_str() : "(none)",
                (a_pass && b_pass) ? "PASS" : "FAIL");
}

void test_007_unconfirmed_mismatch_does_not_truncate_or_contaminate()
{
    std::printf("[AC-SND-007] root cause: UNCONFIRMED single-word anchor mismatch does not truncate/contaminate\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    acc.on_word(make_word(PreambleType::TIS, "DL3"), kFreq, t);  t += 200;
    acc.on_word(make_word(PreambleType::DATA, "HC1"), kFreq, t); t += 200; // repeat 1 complete: DL3HC1

    // A one-off Golay miscorrection: a single stray anchor word with
    // different content. Must NOT flush "DL3HC1", must NOT be trusted.
    auto stray = acc.on_word(make_word(PreambleType::TIS, "DL2"), kFreq, t);
    t += 200;
    check(!stray.has_value(), "007: a single stray anchor mismatch returns no flushed result");

    // The real station's next real repeat must still fold in cleanly — the
    // stray word left no trace on the open session.
    acc.on_word(make_word(PreambleType::TIS, "DL3"), kFreq, t);  t += 200;
    acc.on_word(make_word(PreambleType::DATA, "HC1"), kFreq, t); t += 200;

    auto result = acc.finalize();
    check(result.has_value(), "007: finalize() produces a result");
    const bool pass = result.has_value() && result->station == "DL3HC1";
    check(pass, "007: station == \"DL3HC1\" (not truncated to \"DL3\", not contaminated by \"DL2\")");

    std::printf("  station=%s: %s\n", result ? result->station.c_str() : "(none)",
                pass ? "PASS" : "FAIL");
}

void test_008_unconfirmed_mismatch_then_original_settles_untouched()
{
    std::printf("[AC-SND-008] unconfirmed mismatch never repeats; original session settles untouched\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    acc.on_word(make_word(PreambleType::TIS, "DL3"), kFreq, t);  t += 200;
    acc.on_word(make_word(PreambleType::DATA, "HC1"), kFreq, t); t += 200;

    auto stray = acc.on_word(make_word(PreambleType::TIS, "DL2"), kFreq, t); // never repeats
    check(!stray.has_value(), "008: unconfirmed mismatch returns no flushed result");

    auto result = acc.finalize();
    check(result.has_value(), "008: finalize() produces a result");
    const bool pass = result.has_value() && result->station == "DL3HC1";
    check(pass, "008: station == \"DL3HC1\" (the never-confirmed stray word left no trace)");

    std::printf("  station=%s: %s\n", result ? result->station.c_str() : "(none)",
                pass ? "PASS" : "FAIL");
}

void test_005_uncorrectable_word_metrics_only()
{
    std::printf("[AC-SND-005] golay_uncorrectable word: linear metrics only, never in station string\n");

    SoundingIdentityAccumulator acc;
    uint32_t t = 0;

    // word1: clean anchor, votes=48 -> snr=31, ber=0, sinad=20.
    acc.on_word(make_word(PreambleType::TIS, "SL3", true, 48, false, 20.0f), kFreq, t);
    t += 100;
    // word2: uncorrectable extension attempt -> snr=0 (invalid), ber=48, sinad=5 (still folded).
    acc.on_word(make_word(PreambleType::DATA, "ZXB", false, 0, true, 5.0f), kFreq, t);
    t += 100;

    auto result = acc.finalize();
    check(result.has_value(), "005: finalize() produces a result");
    const bool station_pass = result.has_value() && result->station == "SL3";
    check(station_pass, "005: station == \"SL3\" (uncorrectable word excluded from slot content)");

    const float expected_ber   = (0.0f + 48.0f) / 2.0f;   // 24.0
    const float expected_snr   = (31.0f + 0.0f) / 2.0f;   // 15.5
    const float expected_sinad = (20.0f + 5.0f) / 2.0f;   // 12.5
    const bool metrics_pass = result.has_value()
        && std::fabs(result->ber      - expected_ber)   < 0.01f
        && std::fabs(result->snr_db   - expected_snr)   < 0.01f
        && std::fabs(result->sinad_db - expected_sinad) < 0.01f;
    check(metrics_pass, "005: ber/snr_db/sinad_db reflect both words averaged linearly");

    std::printf("  station=%s ber=%.2f snr=%.2f sinad=%.2f: %s\n",
                result ? result->station.c_str() : "(none)",
                result ? result->ber : -1.0f, result ? result->snr_db : -1.0f,
                result ? result->sinad_db : -1.0f,
                (station_pass && metrics_pass) ? "PASS" : "FAIL");
}

void test_006_settle_refresh_regression_guard()
{
    std::printf("[AC-SND-006] settle-refresh regression guard: invalid word extends the deadline\n");

    SoundingIdentityAccumulator acc;
    acc.on_word(make_word(PreambleType::TIS, "SL3"), kFreq, 0); // last_word_ms = 0

    // Invalid-but-not-golay_uncorrectable word at t=700 must still refresh
    // the timer (this is the direct fix for the original bug: the old code
    // silently dropped such a word without refreshing anything).
    acc.on_word(make_word(PreambleType::DATA, "ZXB", /*valid=*/false, /*votes=*/20,
                          /*uncorrectable=*/false, /*sinad=*/10.0f), kFreq, 700);

    // Without the fix, relative to the original TIS alone, now=784 would
    // already be timed out (784-0 >= 784). With the fix, last_word_ms=700,
    // so 784-700=84 < 784 -> must NOT be timed out yet.
    const bool not_timed_out_at_784 = !acc.timed_out(784);
    check(not_timed_out_at_784, "006: timer refreshed by the invalid word — not timed out at now=784");

    const bool not_timed_out_just_before = !acc.timed_out(700 + 783);
    check(not_timed_out_just_before, "006: not timed out at now=700+783 (1 ms before the refreshed deadline)");

    const bool timed_out_at_deadline = acc.timed_out(700 + 784);
    check(timed_out_at_deadline, "006: timed out at now=700+784 (exactly the refreshed deadline)");

    const bool pass = not_timed_out_at_784 && not_timed_out_just_before && timed_out_at_deadline;
    std::printf("  %s\n", pass ? "PASS" : "FAIL");
}

// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("\n");
    std::printf("======================================================================\n");
    std::printf("  AC-SND-001..006 -- SoundingIdentityAccumulator unit tests (no PCM)\n");
    std::printf("======================================================================\n\n");

    test_001_single_clean_cycle();                              std::printf("\n");
    test_002_repeat_redundancy_recovers_dropped_extension();     std::printf("\n");
    test_003_positional_mismatch_not_assigned_then_recovered();  std::printf("\n");
    test_004_confirmed_anchor_mismatch_flushes_without_cross_contamination(); std::printf("\n");
    test_005_uncorrectable_word_metrics_only();                  std::printf("\n");
    test_006_settle_refresh_regression_guard();                  std::printf("\n");
    test_007_unconfirmed_mismatch_does_not_truncate_or_contaminate(); std::printf("\n");
    test_008_unconfirmed_mismatch_then_original_settles_untouched(); std::printf("\n");

    std::printf("======================================================================\n");
    if (g_failures == 0) {
        std::printf("PASS  %d/%d checks\n", g_tests, g_tests);
        return 0;
    }
    std::printf("FAIL  %d/%d checks failed\n", g_failures, g_tests);
    return 1;
}
