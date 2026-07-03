/**
 * \file tests/sync/unit/test_sync_criteria.cpp
 * \brief AC-SYNC-003-001 — All 9 A.5.2.6.3 sync criteria are active rejection gates.
 *
 * Each sub-test isolates one criterion by feeding a word that violates it and
 * verifying rejection, then confirming the equivalent valid word is accepted
 * (control / positive case).
 *
 * Criterion mapping (MIL-STD-188-141B A.5.2.6.3):
 *   1  unanimous-vote count >= threshold         Demodulator accept_word_() / set_min_unanimous_votes()
 *   2  successful Golay decode of "A" half       ALEFECCodec::deinterleave_word() Golay A
 *   3  successful Golay decode of "B" half       ALEFECCodec::deinterleave_word() Golay B
 *   4  acceptable leading preamble (initial only) Demodulator is_acquisition_anchor()
 *   5  acceptable first character bits (Basic-38) WordParser::parse_from_bits() / decode_ascii()
 *   6  acceptable second character bits           same
 *   7  acceptable third character bits            same
 *   8  history / state / expectations / protocol  Demodulator grid_locked_ + min_spacing gate
 *   9  correct triple-redundant word phase        unanimous threshold + on_word_boundary (locked)
 *
 * Criteria 8 and 9 are structural / state-machine properties verified partly
 * here and partly by reference to AC-SYNC-002-001 (test_word_sync.cpp).
 */

#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "FEC/ale_fec_codec.h"
#include "FEC/golay.h"
#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include "Modem/ale2g_modem.h"
#include "Word/ale_word.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

// Corrupt 2 of the 3 redundant copies of tx49 bit `bit` inside a SymbolFrame.
// Flips the 2/3 majority for that bit → exactly 1 post-vote bit error.
// Carries of bit b live at stream positions b, b+49, b+98;
// stream position p is in symbol[p/3] at sub-bit (2 - p%3) (MSB-first).
static void corrupt_bit(SymbolFrame& frame, int bit)
{
    const int copies[3] = { bit, bit + 49, bit + 98 };
    for (int c = 0; c < 2; ++c) {
        const int p = copies[c];
        frame[static_cast<size_t>(p / 3)] ^= static_cast<uint8_t>(1u << (2 - (p % 3)));
    }
}

// Build PCM for one ALE word (16-sample leading silence + 2-symbol tail).
// The tail matters: the demodulator's word-boundary refinement needs up to one
// symbol of lookahead past the last word before committing it via word_cb_.  A
// real capture stream always keeps delivering samples (silence/noise) after a
// transmission ends; a buffer that stops dead at the last tone sample is a
// test artifact no physical audio path produces.
static std::vector<int16_t> make_pcm(const ALEWord& word)
{
    const SymbolFrame frame = ALEEncoder::encode(word);
    ToneGenerator gen;
    constexpr uint32_t SILENCE = 16;
    std::vector<int16_t> pcm(SILENCE, 0);
    pcm.resize(SILENCE + SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL);
    gen.generate_symbols(frame.data(), SYMBOLS_PER_WORD,
                         pcm.data() + SILENCE, TX_AMPLITUDE);
    pcm.insert(pcm.end(), 2 * SAMPLES_PER_SYMBOL, 0);
    return pcm;
}

// Feed all PCM to demodulator in DECODE_STEP_COARSE-sized chunks (acquisition stride).
static void feed(ALE2GModem::Demodulator& d, const std::vector<int16_t>& pcm)
{
    constexpr uint32_t STEP = 16;
    const auto total = static_cast<uint32_t>(pcm.size());
    for (uint32_t i = 0; i < total; i += STEP) {
        const uint32_t n = (i + STEP <= total) ? STEP : (total - i);
        d.push_samples(pcm.data() + i, n);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Criterion 1 — unanimous-vote count >= threshold
//
// Gate: accept_word_() returns false when unanimous_votes < min_unanimous_votes_.
//
// Max possible unanimous_votes = 48 (bits 0..47 are voted; S49 bit 48
// is excluded per A.5.2.2.4).  Setting threshold = 49 therefore rejects
// every word, including a bit-exact clean word that scores 48.
//
// Negative: threshold 49 > 48 → clean TO "SAM" word rejected.
// Positive: default threshold 33 → same clean word accepted.
// ═══════════════════════════════════════════════════════════════════════════
void test_criterion_1_unanimous_vote_threshold()
{
    std::printf("[Criterion 1] unanimous-vote count >= threshold\n");

    const ALEWord word = WordParser::make_word(PreambleType::TO, "SAM");
    check(word.valid, "make_word(TO, SAM) must succeed");
    const std::vector<int16_t> pcm = make_pcm(word);

    // Negative: threshold 49 is above the maximum attainable score (48)
    // → clean word is rejected by the unanimous-vote gate.
    {
        ALE2GModem::Demodulator d;
        d.set_min_unanimous_votes(49);
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, pcm);
        check(!fired,
              "criterion 1 negative: threshold=49 (> max 48) must reject clean word");
        std::printf("  negative (threshold=49, max=48): %s\n", !fired ? "PASS" : "FAIL");
    }

    // Positive: default threshold 33 accepts a bit-exact clean word.
    {
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, pcm);
        check(fired,
              "criterion 1 positive: default threshold (33) must accept clean word");
        std::printf("  positive (threshold=33): %s\n", fired ? "PASS" : "FAIL");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Criteria 2 & 3 — successful Golay decode of A-half and B-half
//
// Gate: ALEDecoder::decode() returns false when deinterleave_word() sets
//       fec.flag == DECODE_DETECTED (errors beyond correction capacity).
//
// Interleaved tx49 bit layout (word_interleaver.cpp):
//   even positions 0,2,4,...,46 → A-channel (Coder A)
//   odd  positions 1,3,5,...,47 → B-channel (Coder B)
//
// corrupt_bit(frame, b) flips the 2/3 majority for tx49 bit b → 1 post-vote
// bit error in the corresponding Golay half.
//
// Negative A: 4 A-channel errors (bits 0,2,4,6) → exceeds Mode3_4 limit (3)
//             → DECODE_DETECTED for A-half → decode returns false.
// Negative B: 4 B-channel errors (bits 1,3,5,7) → same for B-half.
// Positive:   3 A-channel errors (Mode3_4 limit) → DECODE_CORRECTED, returns true.
// ═══════════════════════════════════════════════════════════════════════════
void test_criteria_2_3_golay_decode_gate()
{
    std::printf("[Criteria 2/3] successful Golay decode of A-half and B-half\n");

    const ALEWord word = WordParser::make_word(PreambleType::TO, "SAM");
    check(word.valid, "make_word(TO, SAM) must succeed");

    // Negative A: 4 A-channel errors → A-half DECODE_DETECTED
    {
        SymbolFrame frame = ALEEncoder::encode(word);
        corrupt_bit(frame, 0);   // even = A-channel
        corrupt_bit(frame, 2);
        corrupt_bit(frame, 4);
        corrupt_bit(frame, 6);

        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0;
        const bool ok = ALEDecoder::decode(frame.data(), out, fec, &uv);
        check(!ok,
              "criterion 2 negative: 4 A-half errors must fail Golay → reject");
        check(fec.flag == Golay::DECODE_DETECTED,
              "criterion 2: 4 A-half errors must produce DECODE_DETECTED");
        std::printf("  A-half 4 errors: decode=%s fec=%s → %s\n",
                    ok ? "true" : "false",
                    fec.flag == Golay::DECODE_DETECTED ? "DETECTED" : "other",
                    (!ok && fec.flag == Golay::DECODE_DETECTED) ? "PASS" : "FAIL");
    }

    // Negative B: 4 B-channel errors → B-half DECODE_DETECTED
    {
        SymbolFrame frame = ALEEncoder::encode(word);
        corrupt_bit(frame, 1);   // odd = B-channel
        corrupt_bit(frame, 3);
        corrupt_bit(frame, 5);
        corrupt_bit(frame, 7);

        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0;
        const bool ok = ALEDecoder::decode(frame.data(), out, fec, &uv);
        check(!ok,
              "criterion 3 negative: 4 B-half errors must fail Golay → reject");
        check(fec.flag == Golay::DECODE_DETECTED,
              "criterion 3: 4 B-half errors must produce DECODE_DETECTED");
        std::printf("  B-half 4 errors: decode=%s fec=%s → %s\n",
                    ok ? "true" : "false",
                    fec.flag == Golay::DECODE_DETECTED ? "DETECTED" : "other",
                    (!ok && fec.flag == Golay::DECODE_DETECTED) ? "PASS" : "FAIL");
    }

    // Positive: exactly 3 A-channel errors (Mode3_4 correction limit) → accepted
    {
        SymbolFrame frame = ALEEncoder::encode(word);
        corrupt_bit(frame, 0);
        corrupt_bit(frame, 2);
        corrupt_bit(frame, 4);

        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0;
        const bool ok = ALEDecoder::decode(frame.data(), out, fec, &uv);
        check(ok,
              "criteria 2/3 positive: 3 A-half errors (Mode3_4 limit) must decode OK");
        check(fec.flag == Golay::DECODE_CORRECTED,
              "criteria 2/3 positive: 3 A-half errors must produce DECODE_CORRECTED");
        std::printf("  A-half 3 errors (limit): decode=%s fec=%s → %s\n",
                    ok ? "true" : "false",
                    fec.flag == Golay::DECODE_CORRECTED ? "CORRECTED" : "other",
                    (ok && fec.flag == Golay::DECODE_CORRECTED) ? "PASS" : "FAIL");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Criterion 4 — acceptable leading preamble (initial sync only)
//
// Gate: accept_word_() checks is_acquisition_anchor() before setting
//       grid_locked_.  Acquisition anchors per A.5.2.6.3: TO, TWAS, TIS.
//       FROM, CMD, THRU, DATA, REP are NOT acquisition anchors and cannot
//       start a new link.
//
// Negative: FROM and CMD fed as the first word → no callback.
// Positive: TO, TWAS, TIS fed as the first word → callback fires.
// ═══════════════════════════════════════════════════════════════════════════
void test_criterion_4_acceptable_preamble()
{
    std::printf("[Criterion 4] acceptable leading preamble (initial sync)\n");

    // Negative: FROM is not an acquisition anchor
    {
        const ALEWord w = WordParser::make_word(PreambleType::FROM, "SAM");
        check(w.valid, "make_word(FROM, SAM) must succeed");
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, make_pcm(w));
        check(!fired,
              "criterion 4 negative: FROM must not trigger initial acquisition");
        std::printf("  FROM as 1st word: %s\n", !fired ? "PASS" : "FAIL");
    }

    // Negative: CMD is not an acquisition anchor
    {
        const ALEWord w = WordParser::make_word(PreambleType::CMD, "SAM");
        check(w.valid, "make_word(CMD, SAM) must succeed");
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, make_pcm(w));
        check(!fired,
              "criterion 4 negative: CMD must not trigger initial acquisition");
        std::printf("  CMD as 1st word: %s\n", !fired ? "PASS" : "FAIL");
    }

    // Positive: TO triggers acquisition
    {
        const ALEWord w = WordParser::make_word(PreambleType::TO, "SAM");
        check(w.valid, "make_word(TO, SAM) must succeed");
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, make_pcm(w));
        check(fired,
              "criterion 4 positive: TO must trigger initial acquisition");
        std::printf("  TO  as 1st word: %s\n", fired ? "PASS" : "FAIL");
    }

    // Positive: TWAS triggers acquisition
    {
        const ALEWord w = WordParser::make_word(PreambleType::TWAS, "SAM");
        check(w.valid, "make_word(TWAS, SAM) must succeed");
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, make_pcm(w));
        check(fired,
              "criterion 4 positive: TWAS must trigger initial acquisition");
        std::printf("  TWAS as 1st word: %s\n", fired ? "PASS" : "FAIL");
    }

    // Positive: TIS triggers acquisition
    {
        const ALEWord w = WordParser::make_word(PreambleType::TIS, "SAM");
        check(w.valid, "make_word(TIS, SAM) must succeed");
        ALE2GModem::Demodulator d;
        bool fired = false;
        d.set_word_callback([&](const ALEWord&) { fired = true; });
        feed(d, make_pcm(w));
        check(fired,
              "criterion 4 positive: TIS must trigger initial acquisition");
        std::printf("  TIS  as 1st word: %s\n", fired ? "PASS" : "FAIL");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Criteria 5, 6, 7 — acceptable character bits (Basic-38 subset)
//
// Gate: ALEDecoder::decode() calls WordParser::parse_from_bits(), which calls
//       decode_ascii().  For routing preambles (TO, TWAS, TIS, FROM, THRU, CMD)
//       each of the three 7-bit character fields must be in {A-Z, 0-9, '@', '?'}.
//
// Strategy: construct a 24-bit word24 manually using ALEFECCodec::encode_word()
// and ALEFECCodec::interleave_word() — bypassing WordParser, which would reject
// invalid characters early — then round-trip through ALEDecoder::decode().
// The Golay codec operates on any 12-bit value, so the encoding succeeds; the
// invalidity is detected only at the character-validation stage.
//
// Invalid character chosen: lowercase 'a' (0x61) — not in Basic-38 (A-Z, 0-9,
// '@', '?') per A.5.2.4.2.
//
// Negative: 'a' in position 0, 1, or 2 independently → decode returns false.
// Positive: all three positions hold valid 'S' → decode returns true.
// ═══════════════════════════════════════════════════════════════════════════
void test_criteria_5_6_7_basic38_character_gate()
{
    std::printf("[Criteria 5/6/7] acceptable character bits (Basic-38)\n");

    // Build word24 = TO preamble + three 7-bit character fields.
    // Character order: bits 20-14 (c0), bits 13-7 (c1), bits 6-0 (c2).
    auto make_word24 = [](char c0, char c1, char c2) -> uint32_t {
        return (static_cast<uint32_t>(PreambleType::TO) << 21)
             | ((static_cast<uint32_t>(c0) & 0x7Fu) << 14)
             | ((static_cast<uint32_t>(c1) & 0x7Fu) <<  7)
             |  (static_cast<uint32_t>(c2) & 0x7Fu);
    };

    // Encode word24 → SymbolFrame via FEC pipeline, then decode.
    auto try_decode = [](uint32_t word24, bool& ok_out,
                         Golay::DecodeResult& fec_out, uint8_t& uv_out) {
        const GolayCoded coded = ALEFECCodec::encode_word(word24);
        const uint64_t   tx49  = ALEFECCodec::interleave_word(coded);
        const SymbolFrame frame = ALEEncoder::encode_tx49(tx49);
        ALEWord out;
        ok_out = ALEDecoder::decode(frame.data(), out, fec_out, &uv_out);
    };

    constexpr char V = 'S';  // valid Basic-38
    constexpr char X = 'a';  // invalid: lowercase letter, not in Basic-38

    // Criterion 5: invalid 1st character
    {
        bool ok; Golay::DecodeResult fec; uint8_t uv;
        try_decode(make_word24(X, V, V), ok, fec, uv);
        check(!ok, "criterion 5 negative: invalid 1st char must be rejected");
        std::printf("  invalid char[0]='%c': %s\n", X, !ok ? "PASS" : "FAIL");
    }

    // Criterion 6: invalid 2nd character
    {
        bool ok; Golay::DecodeResult fec; uint8_t uv;
        try_decode(make_word24(V, X, V), ok, fec, uv);
        check(!ok, "criterion 6 negative: invalid 2nd char must be rejected");
        std::printf("  invalid char[1]='%c': %s\n", X, !ok ? "PASS" : "FAIL");
    }

    // Criterion 7: invalid 3rd character
    {
        bool ok; Golay::DecodeResult fec; uint8_t uv;
        try_decode(make_word24(V, V, X), ok, fec, uv);
        check(!ok, "criterion 7 negative: invalid 3rd char must be rejected");
        std::printf("  invalid char[2]='%c': %s\n", X, !ok ? "PASS" : "FAIL");
    }

    // Positive: all valid Basic-38 characters
    {
        bool ok; Golay::DecodeResult fec; uint8_t uv;
        try_decode(make_word24(V, V, V), ok, fec, uv);
        check(ok, "criteria 5/6/7 positive: all valid Basic-38 chars must be accepted");
        std::printf("  all valid '%c%c%c': %s\n", V, V, V, ok ? "PASS" : "FAIL");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Criterion 8 — history / state / expectations / protocol (grid state)
//
// Gate: accept_word_() tracks grid_locked_ + grid_anchor_ + a min_spacing
//       window.  Without this gate, the sliding Goertzel window (COARSE stride
//       = 16 samples) would fire a callback for every pass over the same word
//       boundary — roughly WORD_SAMPLES/DECODE_STEP_COARSE = 196 times
//       for a single physical word.
//
// The min_spacing gate (WORD_SAMPLES - SAMPLES_PER_SYMBOL = 3072 samples) prevents
// re-accepting the same word boundary until almost one full word-interval
// has elapsed.
//
// Test: feed exactly one clean word (3136 samples + 16-sample silence =
// 3152 total) and assert that the callback fires exactly once.
//
// Criterion 9 — correct triple-redundant word phase
//
// Gate: (a) the unanimous-vote threshold acts as a simultaneous phase
//   discriminator — a window offset by even one symbol (64 samples)
//   scrambles the three redundant copies of every bit, dropping
//   unanimous_votes to near zero, which the threshold rejects.
//   (b) for grid-locked FEC-corrected words, accept_word_() additionally
//   applies an on_word_boundary check (± SAMPLES_PER_SYMBOL of expected boundary).
//
// These behaviours are verified end-to-end by AC-SYNC-002-001
// (tests/sync/integration/test_word_sync.cpp), which proves that only the
// correctly-aligned stride-49 window yields a decoded callback.  No new
// PCM is generated here; the cross-reference is recorded for traceability.
// ═══════════════════════════════════════════════════════════════════════════
void test_criteria_8_9_grid_state_and_phase()
{
    std::printf("[Criteria 8/9] grid state (no duplicate callbacks) + word-phase note\n");

    // Criterion 8: one physical word → exactly one callback.
    {
        const ALEWord word = WordParser::make_word(PreambleType::TO, "SAM");
        check(word.valid, "make_word(TO, SAM) must succeed");
        const std::vector<int16_t> pcm = make_pcm(word);

        ALE2GModem::Demodulator d;
        int count = 0;
        d.set_word_callback([&](const ALEWord&) { ++count; });
        feed(d, pcm);

        // Without the grid / min_spacing gate, the sliding Goertzel window
        // would fire ~(WORD_SAMPLES / DECODE_STEP_COARSE) = 196 times for this one word.
        check(count == 1,
              "criterion 8: one physical word must produce exactly 1 callback "
              "(grid state prevents duplicate decode of same boundary)");
        std::printf("  one-word PCM → %d callback(s) (expect 1): %s\n",
                    count, (count == 1) ? "PASS" : "FAIL");
    }

    // Criterion 9 cross-reference: AC-SYNC-002-001 (test_word_sync.cpp) verifies
    // that the stride-49 phase requirement is enforced — only the correctly-aligned
    // window produces a decoded word, because a window offset by even one symbol
    // drops unanimous_votes below threshold (criterion 1 doubles as phase gate).
    // The on_word_boundary check (continuing sync) is the secondary guard and is
    // exercised as part of the same E2E test.
    std::printf("  criterion 9 (word phase): verified by AC-SYNC-002-001"
                " (tests/sync/integration/test_word_sync.cpp)\n");
}

} // namespace

int main()
{
    std::printf("\n");
    std::printf("╔══════════════════════════════════════════════════════════════════╗\n");
    std::printf("║  AC-SYNC-003-001 — All 9 A.5.2.6.3 sync criteria are active     ║\n");
    std::printf("╚══════════════════════════════════════════════════════════════════╝\n\n");

    test_criterion_1_unanimous_vote_threshold();
    std::printf("\n");
    test_criteria_2_3_golay_decode_gate();
    std::printf("\n");
    test_criterion_4_acceptable_preamble();
    std::printf("\n");
    test_criteria_5_6_7_basic38_character_gate();
    std::printf("\n");
    test_criteria_8_9_grid_state_and_phase();
    std::printf("\n");

    std::printf("══════════════════════════════════════════════════════════════════\n");
    if (g_failures == 0) {
        std::printf("PASS  %d/%d checks — all 9 A.5.2.6.3 sync criteria active\n",
                    g_tests, g_tests);
        return 0;
    }
    std::fprintf(stderr, "FAIL  %d/%d passed — %d failure(s)\n",
                 g_tests - g_failures, g_tests, g_failures);
    return 1;
}
