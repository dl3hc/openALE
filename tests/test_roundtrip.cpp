/**
 * \file tests/test_roundtrip.cpp
 * \brief TX→PCM→Demodulation→Decoding roundtrip test for ALE Frame c.
 *
 * Proves that all 5 Frame-c words survive the full physical-layer pipeline:
 *   ALEStateMachine → ALE2GModem → PCM → Goertzel → majority-vote → FEC → ALEWord
 *
 * Expected words (MIL-STD-188-141B Figure A-14c, call SAMUEL from JOE):
 *   0  TO:SAM    1  DATA:UEL   2  TO:SAM   3  DATA:UEL   4  TIS:JOE
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include "Modem/ale2g_modem.h"
#include "FEC/ale_fec_codec.h"
#include "Word/ale_word.h"
#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ale;

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

// ── Goertzel single-bin power estimate ───────────────────────────────────────
static float goertzel(const int16_t* s, uint32_t N, float freq_hz, float sr)
{
    float w     = 2.0f * static_cast<float>(M_PI) * freq_hz / sr;
    float coeff = 2.0f * std::cos(w);
    float q1 = 0.0f, q2 = 0.0f;
    for (uint32_t n = 0; n < N; ++n) {
        float q0 = coeff * q1 - q2 + static_cast<float>(s[n]);
        q2 = q1;
        q1 = q0;
    }
    return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

// ── Detect one ALE symbol from a 64-sample chunk ─────────────────────────────
static uint8_t detect_symbol(const int16_t* chunk)
{
    float   best_mag  = -1.0f;
    uint8_t best_rank = 0;
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        float mag = goertzel(chunk, FFT_SIZE, static_cast<float>(TONE_FREQS_HZ[r]), 8000.0f);
        if (mag > best_mag) { best_mag = mag; best_rank = r; }
    }
    return FREQ_TO_SYMBOL[best_rank];
}

// ── Decode one 3136-sample word block → ALEWord ───────────────────────────────
struct WordDecodeResult {
    ALEWord           word;
    Golay::DecodeResult fec;
    bool              valid;
};

static WordDecodeResult decode_word_block(const int16_t* block)
{
    // Step 1: detect 49 symbols via Goertzel on each 64-sample chunk
    uint8_t sym[SYMBOLS_PER_WORD];
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k)
        sym[k] = detect_symbol(block + k * FFT_SIZE);

    // Step 2: extract 147-bit stream MSB-first from symbol values
    //   stream[3k+0] = bit2 (MSB), stream[3k+1] = bit1, stream[3k+2] = bit0 (LSB)
    //   Invariant: stream[i] == word_bit[i % 49]  (matches build_symbols MSB-first packing)
    uint8_t stream[SYMBOLS_PER_WORD * BITS_PER_SYMBOL]; // 147
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        stream[3*k + 0] = (sym[k] >> 2) & 1u;
        stream[3*k + 1] = (sym[k] >> 1) & 1u;
        stream[3*k + 2] =  sym[k]       & 1u;
    }

    // Step 3: stride-49 majority vote → 49-bit tx49 (bit i stored at position i)
    uint64_t tx49 = 0;
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i) {
        uint8_t b0 = stream[i];
        uint8_t b1 = stream[i + SYMBOLS_PER_WORD];
        uint8_t b2 = stream[i + 2 * SYMBOLS_PER_WORD];
        if ((b0 + b1 + b2) >= 2)
            tx49 |= (1ULL << i);
    }

    // Step 4: deinterleave + Golay FEC decode → 24-bit ALE word
    Golay::DecodeResult fec;
    uint32_t word24 = ALEFECCodec::deinterleave_word(tx49, fec);

    // Step 5: reconstruct ALEWord from 24-bit word
    WordParser parser;
    ALEWord    word;
    bool valid = parser.parse_from_bits(word24, word);

    return { word, fec, valid };
}

// ── Expected Frame-c words (MIL-STD-188-141B Figure A-14c) ───────────────────
struct Expected { PreambleType type; char addr[4]; };
static const Expected EXPECTED[5] = {
    { PreambleType::TO,   "SAM" },
    { PreambleType::DATA, "UEL" },
    { PreambleType::TO,   "SAM" },
    { PreambleType::DATA, "UEL" },
    { PreambleType::TIS,  "JOE" },
};

int main()
{
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Roundtrip test: TX→PCM→Demod→Decode  (Frame c)\n");
    std::printf("  Calling: SAMUEL   Self: JOE\n");
    std::printf("═══════════════════════════════════════════════════════\n\n");

    // ── TX side (identical to test_frame_c.cpp fixture) ─────────────────────
    std::vector<int16_t> pcm;

    ALEStateMachine sm;
    ALE2GModem      modem;
    ToneGenerator   gen;

    sm.set_transmit_callback([&](const ALEWord& w) {
        modem.enqueue_word(w);
    });

    sm.set_self_address("JOE");
    sm.set_target_scan_channels(0);  // 0 = no scanning → Frame c (1-ch nonscan)
    sm.initiate_call("SAMUEL");

    const uint32_t tick_ms    = 1;
    const uint32_t timeout_ms = ALETimingConstants::Twt_ms
                              + ALETimingConstants::Tt_ms
                              + 5u * ALETimingConstants::Trw_ms
                              + ALETimingConstants::Trw_ms;   // margin
    uint32_t t = 0;
    while (t < timeout_ms) {
        sm.update(t);
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modem.pull_symbol_frame(syms)) {
            const size_t off = pcm.size();
            pcm.resize(off + SYMBOLS_PER_WORD * FFT_SIZE);
            gen.generate_symbols(syms, SYMBOLS_PER_WORD, pcm.data() + off, 0.7f);
            sm.on_word_complete();
        }
        t += tick_ms;
        if (sm.get_calling_phase() == CallingPhase::LISTENING)
            break;
    }

    constexpr size_t WORD_SAMPLES = SYMBOLS_PER_WORD * FFT_SIZE;  // 49 × 64 = 3136
    constexpr size_t TOTAL_WORDS  = 5;
    constexpr size_t EXPECTED_PCM = TOTAL_WORDS * WORD_SAMPLES;   // 15680

    std::printf("  PCM samples: %zu  (expected %zu)\n\n",
                pcm.size(), EXPECTED_PCM);

    if (pcm.size() < EXPECTED_PCM) {
        std::fprintf(stderr, "FAIL: PCM too short (%zu < %zu) — TX side did not"
                             " complete\n", pcm.size(), EXPECTED_PCM);
        return 1;
    }

    // The modem only emits samples for actual words (no silence for LBT/tuning),
    // so the last 5 × 3136 samples are exactly the 5 Frame-c word blocks.
    const int16_t* base = pcm.data() + (pcm.size() - EXPECTED_PCM);

    // ── RX side: Goertzel + majority-vote + FEC per word block ──────────────
    int pass = 0;
    for (uint32_t i = 0; i < TOTAL_WORDS; ++i) {
        WordDecodeResult r = decode_word_block(base + i * WORD_SAMPLES);

        const char* fec_str =
            (r.fec.flag == Golay::DECODE_OK)       ? "OK" :
            (r.fec.flag == Golay::DECODE_CORRECTED) ? "CORRECTED" : "FAILED";

        bool type_match = (r.word.type == EXPECTED[i].type);
        bool addr_match = (std::strncmp(r.word.address, EXPECTED[i].addr, 3) == 0);
        bool fec_ok     = (r.fec.flag != Golay::DECODE_DETECTED &&
                           r.fec.flag != Golay::DECODE_UNCORRECTABLE);

        bool ok = type_match && addr_match && fec_ok;
        if (ok) ++pass;

        std::printf("  word[%u]: %-4s:%-3s  fec=%-9s  %s\n",
            i,
            WordParser::word_type_name(r.word.type),
            r.word.address,
            fec_str,
            ok ? "\xE2\x9C\x93" : "\xE2\x9C\x97 MISMATCH");
    }

    std::printf("\n");
    if (pass == static_cast<int>(TOTAL_WORDS)) {
        std::printf("  All 5 words decoded correctly.  OK\n");
        std::printf("═══════════════════════════════════════════════════════\n");
        return 0;
    }

    std::fprintf(stderr, "  FAIL: only %d/5 words decoded correctly.\n", pass);
    std::printf("═══════════════════════════════════════════════════════\n");
    return 1;
}
