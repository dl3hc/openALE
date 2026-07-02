/**
 * \file tests/sync/integration/test_word_sync.cpp
 * \brief AC-SYNC-002-001 — Empfangsseitige 49-Symbol-Wortsynchronisation
 *
 * The Demodulator (ale2g_modem.cpp) implements word-sync via a sliding Goertzel
 * decode (DECODE_STEP_COARSE=16/FINE=4 samples): during acquisition every 16 samples it decodes
 * the last 3136-sample window and gates acceptance on:
 *   1. unanimous 2/3-vote count >= threshold  — enforces correct stride-49 phase;
 *      a window offset by even 1 symbol (64 samples) scrambles the three redundant
 *      copies of each bit, collapsing the unanimous count to near zero.
 *   2. acceptable leading preamble (TO/TWAS/TIS) for initial acquisition.
 * This is the "comparable method" to a sliding-window energy detector.
 *
 * Test: feed 2 clean ALE words with a sub-symbol leading silence and assert:
 *   (1) Sync (first word callback) fires within 2 × WORD_SAMPLES + SILENCE samples.
 *   (2) Decoded word content matches the transmitted word, proving correct
 *       stride-49 alignment (a misaligned window fails the unanimous-vote gate
 *       or produces wrong content).
 */

#include "Modem/ale2g_modem.h"
#include "Codec/ale_encoder.h"
#include "FSK/tone_generator.h"
#include "FSK/ale_waveform.h"
#include "Word/ale_word.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ale;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

// ============================================================================
// AC-SYNC-002-001 — Sync within first 2 received words
//
// Arrange: 16-sample leading silence + 2 clean ALE words (TO "SAM").
//   Total PCM = 16 + 2 × 3136 = 6288 samples.
//
// Assert:
//   (1) First word callback fires at sample <= SILENCE + 2 × WORD_SAMPLES
//       i.e. sync established within the first 2 received words.
//   (2) Decoded type == TO and address == "SAM": correct stride-49 alignment.
// ============================================================================
void test_ac_sync_002_001_sync_within_two_words()
{
    std::printf("[AC-SYNC-002-001] Word-sync within first 2 received ALE words\n");

    // Leading silence: sub-symbol (< SAMPLES_PER_SYMBOL=64) so alignment is non-trivial
    // but realistic for any TX path.
    constexpr uint32_t SILENCE      = 16;
    constexpr uint32_t WORD_SAMPLES = SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL;   // 3136
    constexpr uint32_t FEED_LIMIT   = SILENCE + 2 * WORD_SAMPLES;    // 6288

    // Build 2 identical clean TO "SAM" words (preamble required for acquisition).
    const ALEWord expected = WordParser::make_word(PreambleType::TO, "SAM");
    check(expected.valid, "WordParser::make_word(TO, SAM) must succeed");
    const SymbolFrame frame = ALEEncoder::encode(expected);

    ToneGenerator gen;
    std::vector<int16_t> pcm(SILENCE, int16_t(0));
    for (int i = 0; i < 2; ++i) {
        const size_t off = pcm.size();
        pcm.resize(off + WORD_SAMPLES);
        gen.generate_symbols(frame.data(), SYMBOLS_PER_WORD,
                             pcm.data() + static_cast<ptrdiff_t>(off), TX_AMPLITUDE);
    }

    // Feed to the Demodulator in 16-sample chunks (= DECODE_STEP_COARSE); track the
    // sample position at which the first word callback fires.
    ALE2GModem::Demodulator demod;
    uint32_t fed            = 0;
    uint32_t sync_at_sample = 0;
    bool     sync_seen      = false;
    ALEWord  first_decoded{};

    demod.set_word_callback([&](const ALEWord& w) {
        if (!sync_seen) {
            sync_seen      = true;
            sync_at_sample = fed;   // fed is set before push_samples()
            first_decoded  = w;
        }
    });

    constexpr uint32_t STEP = 16;
    const auto total = static_cast<uint32_t>(pcm.size());
    for (uint32_t i = 0; i < total; i += STEP) {
        const uint32_t n = (i + STEP <= total) ? STEP : (total - i);
        fed = i + n;
        demod.push_samples(pcm.data() + i, n);
    }

    // (1) Sync within 2 words
    check(sync_seen,
          "demodulator must deliver first word within 2-word PCM window");
    if (sync_seen) {
        const bool timing_ok = (sync_at_sample <= FEED_LIMIT);
        check(timing_ok,
              "sync must fire within first 2 received words (<= SILENCE + 2 x WORD_SAMPLES)");
        std::printf("  sync at sample %u / %u  (limit=%u): %s\n",
                    sync_at_sample, total, FEED_LIMIT,
                    timing_ok ? "PASS" : "FAIL");
    }

    // (2) Content correct — proves stride-49 alignment (not a spurious decode
    //     from a misaligned window that happened to pass FEC by luck).
    if (sync_seen) {
        const bool type_ok = (first_decoded.type == expected.type);
        const bool addr_ok = (std::strcmp(first_decoded.address, expected.address) == 0);
        check(type_ok, "decoded word type must match transmitted TO");
        check(addr_ok, "decoded word address must match transmitted \"SAM\"");
        std::printf("  decoded: %-4s '%s'  type=%s addr=%s\n",
                    WordParser::word_type_name(first_decoded.type),
                    first_decoded.address,
                    type_ok ? "PASS" : "FAIL",
                    addr_ok ? "PASS" : "FAIL");
    }

    std::printf("  => AC-SYNC-002-001: %s\n\n",
                (g_failures == 0) ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    std::printf("\n");
    std::printf("╔═══════════════════════════════════════════════════════════════╗\n");
    std::printf("║  test_word_sync — AC-SYNC-002-001                            ║\n");
    std::printf("║  Empfangsseitige 49-Symbol-Wortsynchronisation               ║\n");
    std::printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    test_ac_sync_002_001_sync_within_two_words();

    if (g_failures == 0) {
        std::printf("PASS  word-sync acquires within 2 received words (stride-49 correct)\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d failure(s)\n", g_failures);
    return 1;
}
