/**
 * \file tests/waveform/e2e/test_sinad_rework.cpp
 * \brief A.5.4.1.2 true-SINAD verification for symbol_from_block.
 *
 * Verifies the SINAD rework (docs/SINAD_REWORK_HANDOFF.md):
 *   1. Clean loopback        — SINAD >= ~20 dB (was ~6–7 dB before the rework).
 *   2. AWGN sweep            — SINAD decreases monotonically and tracks the
 *                              injected SNR within a few dB.
 *   3. BER unchanged         — decode still succeeds (the symbol decision window
 *                              was not touched), unanimous votes stay high.
 *
 * And the word-boundary refinement (early-anchor regression):
 *   4. Contiguous words      — EVERY word of a contiguous multi-word transmission
 *                              reads a high SINAD, not just the first.  Before the
 *                              refinement, words 2..N were accepted ~28–32 samples
 *                              early (the Golay/vote pipeline is shift-tolerant),
 *                              which poisoned the SINAD sub-window to ~3–6 dB even
 *                              on a bit-perfect signal.
 *   5. Resampler path        — same assertion through the 8k→48k→8k round trip
 *                              (the live ale_bridge / virtual-cable path).
 *
 * Drives the real ALE2GModem::Demodulator via push_samples and reads sinad_db
 * back from the decoded ALEWord (try_decode averages per-symbol SINAD over the
 * 49-symbol word). Modeled on tests/sync/integration/test_word_sync.cpp.
 */

#include "Modem/ale2g_modem.h"
#include "App/resampler.h"
#include "Codec/ale_encoder.h"
#include "FSK/tone_generator.h"
#include "FSK/ale_waveform.h"
#include "Word/ale_word.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace ale;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

constexpr uint32_t SILENCE      = 16;
constexpr uint32_t WORD_SAMPLES = SYMBOLS_PER_WORD * SAMPLES_PER_SYMBOL;   // 3136

// Build N identical clean TO "SAM" words preceded by sub-symbol silence.
// Returns the PCM; also fills out_signal_rms_sq with the AC mean-square power
// of the word region (used to scale AWGN to a target SNR).
static std::vector<int16_t> build_clean_pcm(int n_words, double& out_signal_rms_sq)
{
    const ALEWord expected = WordParser::make_word(PreambleType::TO, "SAM");
    const SymbolFrame frame = ALEEncoder::encode(expected);

    ToneGenerator gen;
    std::vector<int16_t> pcm(SILENCE, int16_t(0));
    for (int i = 0; i < n_words; ++i) {
        const size_t off = pcm.size();
        pcm.resize(off + WORD_SAMPLES);
        gen.generate_symbols(frame.data(), SYMBOLS_PER_WORD,
                             pcm.data() + static_cast<ptrdiff_t>(off), TX_AMPLITUDE);
    }

    // AC mean-square over the word region (drop leading silence).
    double sum = 0.0, sumsq = 0.0;
    const size_t n = pcm.size() - SILENCE;
    for (size_t i = SILENCE; i < pcm.size(); ++i) {
        const double x = static_cast<double>(pcm[i]);
        sum  += x;
        sumsq += x * x;
    }
    const double mean = sum / static_cast<double>(n);
    out_signal_rms_sq = (sumsq / static_cast<double>(n)) - mean * mean;
    return pcm;
}

// Add gaussian AWGN to pcm so that noise_rms_sq / signal_rms_sq == 10^(-snr_db/10).
// Fixed seed → reproducible. Returns the modified PCM.
static std::vector<int16_t> add_awgn(std::vector<int16_t> pcm, double signal_rms_sq,
                                    double snr_db, uint32_t seed)
{
    const double signal_rms = std::sqrt(std::max(signal_rms_sq, 1e-12));
    const double noise_rms  = signal_rms * std::pow(10.0, -snr_db / 20.0);
    std::mt19937 rng(seed);
    std::normal_distribution<double> ndist(0.0, noise_rms);
    for (auto& s : pcm) {
        double v = static_cast<double>(s) + ndist(rng);
        if (v >  32767.0) v =  32767.0;
        if (v < -32768.0) v = -32768.0;
        s = static_cast<int16_t>(std::lround(v));
    }
    return pcm;
}

// Feed PCM to a fresh Demodulator in 16-sample chunks; capture the first decoded
// word. Returns whether a word was decoded, and its sinad_db / validity.
struct DecodeOutcome { bool decoded = false; float sinad_db = 0.0f; bool valid = false; };

static DecodeOutcome run_demod(const std::vector<int16_t>& pcm)
{
    ALE2GModem::Demodulator demod;
    DecodeOutcome out;
    demod.set_word_callback([&](const ALEWord& w) {
        if (!out.decoded) {
            out.decoded = true;
            out.sinad_db = w.sinad_db;
            out.valid = w.valid;
        }
    });
    constexpr uint32_t STEP = 16;
    const auto total = static_cast<uint32_t>(pcm.size());
    for (uint32_t i = 0; i < total; i += STEP) {
        const uint32_t n = (i + STEP <= total) ? STEP : (total - i);
        demod.push_samples(pcm.data() + i, n);
    }
    return out;
}

// ── Test 1: clean loopback reads a high SINAD ───────────────────────────────
void test_clean_loopback_high_sinad()
{
    std::printf("[SINAD-1] Clean loopback — SINAD must be high (>= 20 dB)\n");
    double sig_sq = 0.0;
    const auto pcm = build_clean_pcm(2, sig_sq);
    const auto r = run_demod(pcm);

    check(r.decoded, "clean loopback must decode a word");
    if (!r.decoded) { std::printf("  => FAIL\n\n"); return; }

    std::printf("  clean SINAD = %.1f dB  (valid=%d)\n", r.sinad_db, r.valid);
    check(r.valid,         "clean loopback word must be valid (BER unchanged)");
    check(r.sinad_db >= 20.0f, "clean loopback SINAD >= 20 dB (was ~6-7 before rework)");
    std::printf("  => %s\n\n", (r.sinad_db >= 20.0f && r.valid) ? "PASS" : "FAIL");
}

// ── Test 2: AWGN sweep — SINAD tracks injected SNR, monotonic, BER stable ──
void test_awgn_tracking()
{
    std::printf("[SINAD-2] AWGN sweep — SINAD tracks injected SNR (monotonic, BER stable)\n");
    double sig_sq = 0.0;
    const auto clean_pcm = build_clean_pcm(3, sig_sq);   // 3 words: enough headroom

    // Inject at three SNR levels; expect SINAD to (a) be <= clean, (b) decrease
    // with lower SNR, (c) land within a few dB of the injected SNR, (d) keep
    // decoding (BER unchanged — symbol decision window untouched).
    const double snr_levels[3] = { 18.0, 12.0, 6.0 };
    float measured[3] = { 0.0f, 0.0f, 0.0f };
    bool  decoded[3]  = { false, false, false };

    for (int i = 0; i < 3; ++i) {
        auto noisy = add_awgn(clean_pcm, sig_sq, snr_levels[i], 0x53494Eu + i);
        const auto r = run_demod(noisy);
        measured[i] = r.sinad_db;
        decoded[i]  = r.decoded && r.valid;
        std::printf("  SNR=%4.1f dB  ->  SINAD=%5.1f dB  decoded=%d\n",
                    snr_levels[i], r.sinad_db, decoded[i] ? 1 : 0);
    }

    // (a) clean must beat all noisy cases (computed separately for reference)
    const auto clean_r = run_demod(clean_pcm);
    check(clean_r.decoded && clean_r.valid, "clean reference must decode");
    check(measured[0] <= clean_r.sinad_db + 1.0f, "noisy SINAD must be <= clean SINAD");

    // (b) monotonic: higher SNR -> higher SINAD
    check(measured[0] >= measured[1] - 1.0f, "SINAD(18dB) >= SINAD(12dB) (within 1 dB)");
    check(measured[1] >= measured[2] - 1.0f, "SINAD(12dB) >= SINAD(6dB) (within 1 dB)");

    // (c) tracks injected SNR within a tolerance (loose: quantization + dither
    //     + windowing add a few dB of spread)
    for (int i = 0; i < 3; ++i) {
        const float err = std::abs(measured[i] - static_cast<float>(snr_levels[i]));
        char msg[80];
        std::snprintf(msg, sizeof(msg), "SINAD(SNR=%.0f) within 4 dB of injected", snr_levels[i]);
        check(err <= 4.0f, msg);
    }

    // (d) BER unchanged — all noisy cases still decode validly down to 6 dB SNR
    for (int i = 0; i < 3; ++i) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "decode succeeds at SNR=%.0f dB (BER unchanged)", snr_levels[i]);
        check(decoded[i], msg);
    }

    (void)M_PI;
    std::printf("  => %s\n\n", (g_failures == 0) ? "PASS" : "FAIL");
}

// Feed PCM and capture EVERY decoded word's SINAD (not just the first).
struct MultiOutcome { int decoded = 0; int valid = 0; std::vector<float> sinads; };

static MultiOutcome run_demod_all(const std::vector<int16_t>& pcm)
{
    ALE2GModem::Demodulator demod;
    MultiOutcome out;
    demod.set_word_callback([&](const ALEWord& w) {
        ++out.decoded;
        if (w.valid) ++out.valid;
        out.sinads.push_back(w.sinad_db);
    });
    constexpr uint32_t STEP = 16;
    const auto total = static_cast<uint32_t>(pcm.size());
    for (uint32_t i = 0; i < total; i += STEP) {
        const uint32_t n = (i + STEP <= total) ? STEP : (total - i);
        demod.push_samples(pcm.data() + i, n);
    }
    return out;
}

// ── Test 3: contiguous words — every word reads a high SINAD ────────────────
// Regression for the early-anchor bug: without boundary refinement the decode
// grid re-anchors ~28–32 samples before the true boundary of every word after
// the first, and their SINAD collapses to ~3–6 dB despite BER = 0.
void test_contiguous_words_all_high_sinad()
{
    std::printf("[SINAD-3] Contiguous words — EVERY word SINAD >= 20 dB\n");
    constexpr int N_WORDS = 5;
    double sig_sq = 0.0;
    auto pcm = build_clean_pcm(N_WORDS, sig_sq);
    // Trailing quiet tail: the boundary refinement needs up to one symbol of
    // lookahead past the last word before committing it (real audio streams
    // never stop dead at the last sample either).
    pcm.insert(pcm.end(), 2 * SAMPLES_PER_SYMBOL, 0);

    const auto r = run_demod_all(pcm);
    std::printf("  decoded %d/%d words (valid %d)\n", r.decoded, N_WORDS, r.valid);
    check(r.decoded == N_WORDS, "all contiguous words must decode");
    check(r.valid   == N_WORDS, "all contiguous words must be valid (BER unchanged)");

    bool all_high = true;
    for (size_t i = 0; i < r.sinads.size(); ++i) {
        std::printf("  word %zu: SINAD = %.1f dB\n", i + 1, r.sinads[i]);
        if (r.sinads[i] < 20.0f) all_high = false;
    }
    check(all_high, "every contiguous word SINAD >= 20 dB (words 2..N were ~3 dB "
                    "before boundary refinement)");
    std::printf("  => %s\n\n", (r.decoded == N_WORDS && all_high) ? "PASS" : "FAIL");
}

// ── Test 4: resampler round trip — the live ale_bridge / virtual-cable path ─
void test_resampler_path_all_high_sinad()
{
    std::printf("[SINAD-4] 8k->48k->8k resampler path — EVERY word SINAD >= 20 dB\n");
    constexpr int N_WORDS = 5;
    double sig_sq = 0.0;
    auto pcm8 = build_clean_pcm(N_WORDS, sig_sq);
    pcm8.insert(pcm8.end(), 2 * SAMPLES_PER_SYMBOL, 0);

    Resampler up(8000, 48000), down(48000, 8000);
    std::vector<int16_t> dev48, rx8;
    up.process(pcm8.data(), pcm8.size(), dev48);
    down.process(dev48.data(), dev48.size(), rx8);

    const auto r = run_demod_all(rx8);
    std::printf("  decoded %d/%d words (valid %d)\n", r.decoded, N_WORDS, r.valid);
    check(r.decoded == N_WORDS, "all words must decode through the resampler path");
    check(r.valid   == N_WORDS, "all resampled words must be valid");

    bool all_high = true;
    for (size_t i = 0; i < r.sinads.size(); ++i) {
        std::printf("  word %zu: SINAD = %.1f dB\n", i + 1, r.sinads[i]);
        if (r.sinads[i] < 20.0f) all_high = false;
    }
    check(all_high, "every resampled word SINAD >= 20 dB (live VAC path regression)");
    std::printf("  => %s\n\n", (r.decoded == N_WORDS && all_high) ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    std::printf("\n");
    std::printf("===================================================================\n");
    std::printf("  test_sinad_rework — A.5.4.1.2 true-SINAD verification\n");
    std::printf("===================================================================\n\n");

    test_clean_loopback_high_sinad();
    test_awgn_tracking();
    test_contiguous_words_all_high_sinad();
    test_resampler_path_all_high_sinad();

    if (g_failures == 0) {
        std::printf("PASS  true-SINAD: clean loopback reads high dB, AWGN tracks SNR, BER stable\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d failure(s)\n", g_failures);
    return 1;
}