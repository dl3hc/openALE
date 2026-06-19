/**
 * \file tests/test_decoder_robustness.cpp
 * \brief Spec-generality test for the demodulator's acquisition / grid-lock.
 *
 * Motivation
 * ──────────
 * Two ale_cli instances link because their signal is bit-exact end-to-end, so a
 * correctly-aligned decode yields a ZERO-error (DECODE_OK) Golay result and the
 * grid locks.  But MIL-STD-188-141B layers 3× symbol redundancy on top of a
 * Golay(24,12) FEC precisely so a receiver can ACQUIRE through errors: any real
 * or foreign-encoder signal (channel noise, resampling smear, sub-sample phase)
 * will usually present a few residual symbol errors at every alignment, so the
 * correctly-aligned decode is DECODE_CORRECTED — never DECODE_OK.
 *
 * A spec-compliant decoder must lock on such a word.  This test builds
 * spec-compliant frames that are FEC-stressed (aligned decode = DECODE_CORRECTED,
 * word still fully recoverable) and renders them as clean 8 kHz tones, so the
 * Goertzel reads back exactly the constructed symbols at the aligned phase.
 *
 * This makes NO assumption about any specific peer's sample rate or timing — it
 * exercises the *general* requirement: "our decoder must decode any compliant
 * encoder, not just the happy path between two ale_cli instances."
 *
 * Pass criterion: every transmitted word is delivered by the demodulator.
 */

#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "Modem/ale2g_modem.h"
#include "FSK/tone_generator.h"
#include "Word/ale_word.h"
#include "FEC/golay.h"

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

// Corrupt 2 of the 3 redundant copies of one tx49 bit, so the 2/3 majority vote
// flips that bit → exactly one post-vote bit error fed to Golay, and exactly one
// non-unanimous ("bad") vote.  The on-air stream is three copies of tx49:
// copies of tx49 bit i live at stream positions i, i+49, i+98; stream position p
// is symbol p/3, MSB-first sub-bit (2 - p%3).
void corrupt_tx49_bit(SymbolFrame& frame, int bit)
{
    const int copies[3] = { bit, bit + 49, bit + 98 };
    for (int c = 0; c < 2; ++c) {          // flip 2 of 3 copies → majority flips
        const int p = copies[c];
        frame[static_cast<size_t>(p / 3)] ^= static_cast<uint8_t>(1u << (2 - (p % 3)));
    }
}

// Build a spec-compliant frame for `word` that is FEC-stressed but realistic:
// two post-vote bit errors (one in the A half via an even tx49 position, one in
// the B half via an odd position).  Golay corrects both, so the correctly
// aligned decode is DECODE_CORRECTED — never DECODE_OK — with a high, in-spec
// unanimous-vote count (only the two corrupted positions are non-unanimous).
// This is the class of signal any real / foreign encoder produces over a
// channel; the demodulator must acquire on it.
SymbolFrame make_fec_stressed_frame(const ALEWord& word, uint8_t& unanimous_out)
{
    SymbolFrame frame = ALEEncoder::encode(word);
    corrupt_tx49_bit(frame, 4);   // even tx49 position → Coder-A half
    corrupt_tx49_bit(frame, 9);   // odd  tx49 position → Coder-B half

    ALEWord out;
    Golay::DecodeResult fec;
    uint8_t uv = 0xFF;
    const bool ok = ALEDecoder::decode(frame.data(), out, fec, &uv);
    unanimous_out = uv;

    // Self-check: the constructed frame must be recoverable AND DECODE_CORRECTED
    // (not DECODE_OK) so it genuinely exercises FEC-tolerant acquisition.
    check(ok && out.type == word.type && std::strcmp(out.address, word.address) == 0,
          "constructed frame must still decode to the original word");
    check(fec.flag == Golay::DECODE_CORRECTED,
          "constructed frame must be DECODE_CORRECTED (not DECODE_OK)");
    return frame;
}

// Drive the pure AdaptiveFec policy and the demodulator's runtime FEC config
// (A.5.2.6.3 modes + "DO" auto-adjust).
void test_fec_modes_and_adaptive()
{
    using ALE2GModem::AdaptiveFec;
    using ALE2GModem::Demodulator;

    // ── AdaptiveFec mapping ────────────────────────────────────────────────
    AdaptiveFec a;
    a.reset();
    // Neutral start: marginal/unknown → full correction power, moderate threshold.
    check(a.mode() == GolayMode::Mode3_4, "fresh AdaptiveFec must start at Mode3_4");

    AdaptiveFec clean;  clean.reset();
    for (int i = 0; i < 80; ++i) clean.observe(SYMBOLS_PER_WORD);   // perfectly clean words
    check(clean.mode() == GolayMode::Mode1_6, "very clean link → Mode1_6");
    check(clean.threshold() >= 39, "very clean link → high unanimous threshold");

    AdaptiveFec good;   good.reset();
    for (int i = 0; i < 80; ++i) good.observe(46);                  // clean-ish
    check(good.mode() == GolayMode::Mode2_5, "clean link → Mode2_5");

    AdaptiveFec marginal; marginal.reset();
    for (int i = 0; i < 80; ++i) marginal.observe(38);             // marginal
    check(marginal.mode() == GolayMode::Mode3_4, "marginal link → Mode3_4 (full power)");
    check(marginal.threshold() >= 30, "threshold must stay at/above its floor");

    // ── Demodulator runtime configuration ──────────────────────────────────
    Demodulator d;
    check(d.golay_mode() == GolayMode::Mode3_4, "demod default mode = Mode3_4");
    check(d.min_unanimous_votes() == 33, "demod default threshold = 33");
    check(d.adaptive_fec() == false, "adaptive FEC default off");

    d.set_golay_mode(GolayMode::Mode2_5);
    d.set_min_unanimous_votes(36);
    check(d.golay_mode() == GolayMode::Mode2_5, "set_golay_mode reflected");
    check(d.min_unanimous_votes() == 36, "set_min_unanimous_votes reflected");

    d.set_adaptive_fec(true);
    check(d.adaptive_fec() == true, "set_adaptive_fec(true) reflected");
    // Disabling restores the configured base operating point.
    d.set_adaptive_fec(false);
    check(d.golay_mode() == GolayMode::Mode2_5, "disabling adaptive restores base mode");
    check(d.min_unanimous_votes() == 36, "disabling adaptive restores base threshold");

    if (g_failures == 0)
        std::printf("PASS  FEC modes available + adaptive policy behaves per A.5.2.6.3\n\n");
}

} // namespace

int main()
{
    test_fec_modes_and_adaptive();

    std::printf("══════════════════════════════════════════════════════════════\n");
    std::printf("  Decoder robustness: acquire on FEC-corrected (not-clean) words\n");
    std::printf("══════════════════════════════════════════════════════════════\n\n");

    struct W { PreambleType type; const char* addr; };
    const W seq[] = {
        { PreambleType::TO,   "SAM" },
        { PreambleType::TO,   "SAM" },
        { PreambleType::TIS,  "JOE" },
        { PreambleType::TWAS, "BOB" },
        { PreambleType::TO,   "XYZ" },
    };
    const size_t n_words = sizeof(seq) / sizeof(seq[0]);

    // Build the FEC-stressed frames and render them as clean 8 kHz tones.
    ToneGenerator gen;
    std::vector<int16_t> pcm;
    pcm.assign(64, 0);   // small leading silence (multiple of DECODE_STEP and 64)

    uint8_t min_unanimous = SYMBOLS_PER_WORD;
    for (const auto& w : seq) {
        const ALEWord word = WordParser::make_word(w.type, w.addr);
        check(word.valid, "make_word failed");

        uint8_t uv = SYMBOLS_PER_WORD;
        const SymbolFrame frame = make_fec_stressed_frame(word, uv);
        if (uv < min_unanimous) min_unanimous = uv;
        std::printf("  built %-4s '%s'  aligned-decode=DECODE_CORRECTED  unanimous=%u/%u\n",
                    WordParser::word_type_name(w.type), w.addr, uv, SYMBOLS_PER_WORD);

        const size_t off = pcm.size();
        pcm.resize(off + SYMBOLS_PER_WORD * FFT_SIZE);
        gen.generate_symbols(frame.data(), SYMBOLS_PER_WORD, pcm.data() + off, TX_AMPLITUDE);
    }
    pcm.insert(pcm.end(), 4096, 0);   // trailing silence

    std::printf("\n  rendered %zu words, min unanimous votes among aligned decodes = %u/%u\n\n",
                n_words, min_unanimous, SYMBOLS_PER_WORD);

    // Feed the full stream through the demodulator and count delivered words.
    ALE2GModem::Demodulator demod;
    int got = 0;
    std::vector<ALEWord> decoded;
    demod.set_word_callback([&](const ALEWord& w) { ++got; decoded.push_back(w); });
    demod.push_samples(pcm.data(), static_cast<uint32_t>(pcm.size()));

    std::printf("  demodulator delivered %d / %zu words\n", got, n_words);
    for (const auto& w : decoded)
        std::printf("    -> %-4s '%s'\n", WordParser::word_type_name(w.type), w.address);

    check(got >= static_cast<int>(n_words),
          "demodulator must acquire and decode FEC-corrected (spec-compliant) words");

    // Content check: every transmitted word must appear among the decoded words
    // (so a pass cannot be faked by emitting the right COUNT of garbage words).
    for (const auto& expect : seq) {
        const ALEWord ew = WordParser::make_word(expect.type, expect.addr);
        bool found = false;
        for (const auto& w : decoded)
            if (w.type == ew.type && std::strcmp(w.address, ew.address) == 0) {
                found = true;
                break;
            }
        check(found, "each transmitted word must be decoded with correct content");
    }

    if (g_failures == 0) {
        std::printf("\nPASS  decoder acquires through FEC errors (spec-compliant).\n");
        return 0;
    }
    std::fprintf(stderr,
        "\n%d failure(s): demodulator cannot acquire on FEC-corrected words.\n"
        "This is the PCALE->ale_cli class of failure: acquisition bypasses the FEC.\n",
        g_failures);
    return 1;
}
