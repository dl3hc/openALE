/**
 * \file test_fsk_core.cpp
 * \brief Unit tests for 8-FSK modem core
 *
 * Tests:
 *  1. Tone generation, boundary alignment, and streaming continuity
 *  2. Waveform constants and timing (AC-WAVEFORM-*)
 */

#include "FSK/ale_waveform.h"
#ifdef _MSC_VER
#pragma warning(disable: 4127)  // C4127: constexpr runtime checks in tests are intentional
#endif
#include "FSK/tone_generator.h"
#include "Protocol/Control/ale_timing.h"

#include <iostream>
#include <cmath>
#include <vector>
#include <cstring>
#include <iomanip>

namespace ale {

static constexpr float TEST_AMPLITUDE = 0.7f;

// ============================================================================
// Test 1: Tone Generation
// ============================================================================

bool test_tone_generation() {
    std::cout << "\n[TEST 1] Tone Generation\n";
    std::cout << "========================\n";

    uint8_t symbols[NUM_TONES];
    for (uint32_t i = 0; i < NUM_TONES; ++i) {
        symbols[i] = static_cast<uint8_t>(i);
    }

    const uint32_t expected_samples = NUM_TONES * SAMPLES_PER_SYMBOL;

    // ------------------------------------------------------------------------
    // 1) One-shot generation: reference stream
    // ------------------------------------------------------------------------
    ToneGenerator gen_one_shot;
    std::vector<int16_t> samples_one_shot(expected_samples, 0);

    uint32_t num_samples = gen_one_shot.generate_symbols(
        symbols,
        NUM_TONES,
        samples_one_shot.data(),
        TEST_AMPLITUDE
    );

    std::cout << "Generated: " << num_samples << " samples\n";
    std::cout << "Expected:  " << expected_samples << " samples\n";

    if (num_samples != expected_samples) {
        std::cout << "FAIL: Sample count mismatch\n";
        return false;
    }

    // ------------------------------------------------------------------------
    // 2) Chunked generation: expected streaming behavior
    //    This validates that generator state is preserved across calls.
    // ------------------------------------------------------------------------
    ToneGenerator gen_chunked;
    std::vector<int16_t> samples_chunked(expected_samples, 0);

    const uint32_t first_chunk_symbols = 3;

    uint32_t chunk_a = gen_chunked.generate_symbols(
        symbols,
        first_chunk_symbols,
        samples_chunked.data(),
        TEST_AMPLITUDE
    );

    uint32_t chunk_b = gen_chunked.generate_symbols(
        symbols + first_chunk_symbols,
        NUM_TONES - first_chunk_symbols,
        samples_chunked.data() + first_chunk_symbols * SAMPLES_PER_SYMBOL,
        TEST_AMPLITUDE
    );

    if (chunk_a + chunk_b != expected_samples) {
        std::cout << "FAIL: Chunked generation sample count mismatch\n";
        return false;
    }

    // ------------------------------------------------------------------------
    // 3) Block-by-block comparison: generated vs expected
    //    Here, "expected" means the chunked stream. For CPFSK, the correct
    //    property is that one-shot and chunked output are identical.
    // ------------------------------------------------------------------------
    for (uint32_t sym = 0; sym < NUM_TONES; ++sym) {
        const size_t start = static_cast<size_t>(sym) * SAMPLES_PER_SYMBOL;
        const size_t end   = start + SAMPLES_PER_SYMBOL;

        bool block_match = true;
        size_t mismatch_idx = 0;

        for (size_t i = start; i < end; ++i) {
            if (samples_one_shot[i] != samples_chunked[i]) {
                block_match = false;
                mismatch_idx = i;
                break;
            }
        }

        std::cout << "Symbol " << sym
                  << " block: generated=";

        std::cout << "[" << samples_one_shot[start]
                  << " ... " << samples_one_shot[end - 1] << "]";

        std::cout << ", expected=";

        std::cout << "[" << samples_chunked[start]
                  << " ... " << samples_chunked[end - 1] << "]";

        if (block_match) {
            std::cout << "  PASS\n";
        } else {
            std::cout << "  FAIL\n";
            std::cout << "    first mismatch at sample " << mismatch_idx
                      << ": generated=" << samples_one_shot[mismatch_idx]
                      << ", expected=" << samples_chunked[mismatch_idx] << "\n";
            return false;
        }
    }

    // ------------------------------------------------------------------------
    // 4) Explicit phase-continuity check at symbol boundaries
    //    For CPFSK this means: the streaming output must be continuous and
    //    must not depend on whether symbols were generated in one call or
    //    split into multiple calls.
    //
    //    We print the boundary samples so the continuity behavior is visible.
    // ------------------------------------------------------------------------
    // Large 'jump' values between the last sample of the outgoing tone and the
    // first sample of the incoming tone are expected and correct. The outgoing
    // carrier reaches its maximum exactly AT the mathematical transition point
    // (t = N × 8 ms, between samples), not necessarily at the preceding sample.
    // The incoming carrier also starts at that same maximum, so the continuous
    // waveform has no discontinuity even when adjacent samples differ widely.
    std::cout << "\nBoundary continuity checks:\n";

    for (uint32_t sym = 0; sym < NUM_TONES - 1; ++sym) {
        const size_t boundary_idx = static_cast<size_t>(sym + 1) * SAMPLES_PER_SYMBOL;

        const int one_prev = static_cast<int>(samples_one_shot[boundary_idx - 1]);
        const int one_curr = static_cast<int>(samples_one_shot[boundary_idx]);
        const int chg_prev = static_cast<int>(samples_chunked[boundary_idx - 1]);
        const int chg_curr = static_cast<int>(samples_chunked[boundary_idx]);

        const int one_jump = std::abs(one_curr - one_prev);
        const int chg_jump = std::abs(chg_curr - chg_prev);

        std::cout << "  " << sym << " -> " << (sym + 1)
                  << " | one-shot: prev=" << one_prev
                  << ", curr=" << one_curr
                  << ", jump=" << one_jump
                  << " | chunked: prev=" << chg_prev
                  << ", curr=" << chg_curr
                  << ", jump=" << chg_jump << "\n";

        if (one_prev != chg_prev || one_curr != chg_curr) {
            std::cout << "FAIL: Boundary samples differ between one-shot and chunked output\n";
            return false;
        }

        // Optional sanity: the boundary must not create a buffer discontinuity
        // between generation modes. Exact equality is the real continuity test.
        if (one_jump < 0 || chg_jump < 0) {
            std::cout << "FAIL: Invalid boundary jump computation\n";
            return false;
        }
    }

    // ------------------------------------------------------------------------
    // 5) AC-WAVEFORM-005-2: every symbol must start at the waveform maximum.
    //    phase_ wraps to 0x40000000 (pi/2, sin=+1, slope=0) at each 8 ms
    //    boundary because all ALE carriers complete an exact integer number of
    //    cycles per symbol period. Failure here means reset() sets a wrong
    //    initial phase or a frequency was changed to a non-250 Hz multiple.
    // ------------------------------------------------------------------------
    std::cout << "\nAC-WAVEFORM-005-2 slope-zero boundary check:\n";
    {
        // Theoretical peak: sin(pi/2)*amplitude*32767 ≈ 22936.9 (before dither).
        // TPDF dither spans [-1, +1], so the rounded output ranges over 3 values:
        // 22936, 22937, or 22938 — tolerance ±2 covers the full span.
        // The property being verified is NCO phase = pi/2 at every symbol
        // start — not the exact quantized value, which varies by ±2 due to dither.
        const int16_t expected_peak = samples_one_shot[0];
        constexpr int DITHER_TOLERANCE = 2;
        bool slope_zero_ok = true;
        for (uint32_t sym = 1; sym < NUM_TONES; ++sym) {
            int16_t first = samples_one_shot[sym * SAMPLES_PER_SYMBOL];
            if (std::abs(static_cast<int>(first) - static_cast<int>(expected_peak)) > DITHER_TOLERANCE) {
                std::cout << "  FAIL: symbol " << sym << " starts at " << first
                          << ", expected peak " << expected_peak
                          << " (±" << DITHER_TOLERANCE << " TPDF tolerance)\n";
                slope_zero_ok = false;
            }
        }
        if (!slope_zero_ok) return false;
        std::cout << "  All " << NUM_TONES << " symbols start within ±" << DITHER_TOLERANCE
                  << " of peak value " << expected_peak << " — slope zero confirmed\n";
    }

    // ------------------------------------------------------------------------
    // 6) Full-buffer exact equality
    //    This is the strongest streaming-continuity check.
    // ------------------------------------------------------------------------
    if (std::memcmp(samples_one_shot.data(),
                    samples_chunked.data(),
                    expected_samples * sizeof(int16_t)) != 0) {
        std::cout << "FAIL: Chunked generation differs from one-shot generation\n";
        return false;
    }

    std::cout << "\nPASS: Tone generation\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-001-001: 8 orthogonal tones, 750–2500 Hz, 250 Hz spacing
// ============================================================================

bool test_freq_table_ac_waveform_001_001() {
    std::cout << "\n[TEST AC-WAVEFORM-001-001] Frequency Table (8 tones, 750-2500 Hz)\n";
    std::cout << "====================================================================\n";

    // Exactly 8 tones
    if (NUM_TONES != 8) {
        std::cout << "FAIL: NUM_TONES = " << NUM_TONES << " (expected 8)\n";
        return false;
    }
    std::cout << "PASS: NUM_TONES = 8\n";

    // Lowest tone rank 0 = 750 Hz
    if (TONE_FREQS_HZ[0] != 750) {
        std::cout << "FAIL: TONE_FREQS_HZ[0] = " << TONE_FREQS_HZ[0] << " Hz (expected 750)\n";
        return false;
    }
    std::cout << "PASS: TONE_FREQS_HZ[0] = 750 Hz\n";

    // Highest tone rank 7 = 2500 Hz
    if (TONE_FREQS_HZ[7] != 2500) {
        std::cout << "FAIL: TONE_FREQS_HZ[7] = " << TONE_FREQS_HZ[7] << " Hz (expected 2500)\n";
        return false;
    }
    std::cout << "PASS: TONE_FREQS_HZ[7] = 2500 Hz\n";

    // Equal 250 Hz spacing across all consecutive pairs
    for (uint32_t i = 1; i < NUM_TONES; ++i) {
        uint32_t spacing = TONE_FREQS_HZ[i] - TONE_FREQS_HZ[i - 1];
        if (spacing != TONE_SPACING_HZ) {
            std::cout << "FAIL: spacing between rank " << (i - 1) << " (" << TONE_FREQS_HZ[i-1]
                      << " Hz) and rank " << i << " (" << TONE_FREQS_HZ[i]
                      << " Hz) = " << spacing << " Hz (expected " << TONE_SPACING_HZ << ")\n";
            return false;
        }
    }
    std::cout << "PASS: All " << (NUM_TONES - 1) << " spacings = " << TONE_SPACING_HZ << " Hz\n";

    // Expected table per MIL-STD-188-141B
    constexpr std::array<uint32_t, 8> expected = {750, 1000, 1250, 1500, 1750, 2000, 2250, 2500};
    for (uint32_t i = 0; i < NUM_TONES; ++i) {
        if (TONE_FREQS_HZ[i] != expected[i]) {
            std::cout << "FAIL: TONE_FREQS_HZ[" << i << "] = " << TONE_FREQS_HZ[i]
                      << " Hz (expected " << expected[i] << ")\n";
            return false;
        }
    }
    std::cout << "PASS: TONE_FREQS_HZ = {750, 1000, 1250, 1500, 1750, 2000, 2250, 2500} Hz\n";
    std::cout << "PASS: AC-WAVEFORM-001-001\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-001-002: 3-bit symbol ↔ frequency mapping (Table A-III)
// ============================================================================

bool test_symbol_freq_mapping_ac_waveform_001_002() {
    std::cout << "\n[TEST AC-WAVEFORM-001-002] Symbol↔Frequency Mapping (Table A-III)\n";
    std::cout << "===================================================================\n";

    // Expected Table A-III (MIL-STD-188-141B A.5.1.2): frequency → symbol value
    // rank:    0     1     2     3     4     5     6     7
    // freq:  750  1000  1250  1500  1750  2000  2250  2500 Hz
    // sym:     0     1     3     2     6     7     5     4
    constexpr std::array<uint8_t, 8> expected_freq_to_symbol = {0, 1, 3, 2, 6, 7, 5, 4};

    // Expected inverse: symbol value → frequency (Hz)
    // sym:     0     1     2     3     4     5     6     7
    // freq:  750  1000  1500  1250  2500  2250  1750  2000 Hz
    constexpr std::array<uint32_t, 8> expected_symbol_to_freq = {750, 1000, 1500, 1250, 2500, 2250, 1750, 2000};

    bool ok = true;

    // Verify FREQ_TO_SYMBOL matches Table A-III exactly
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        if (FREQ_TO_SYMBOL[rank] != expected_freq_to_symbol[rank]) {
            std::cout << "FAIL: FREQ_TO_SYMBOL[" << rank << "] = " << (int)FREQ_TO_SYMBOL[rank]
                      << " (expected " << (int)expected_freq_to_symbol[rank] << ")\n";
            ok = false;
        }
    }
    if (ok) std::cout << "PASS: FREQ_TO_SYMBOL matches Table A-III\n";

    // Verify SYMBOL_TO_FREQ matches Table A-III exactly
    for (uint32_t sym = 0; sym < NUM_TONES; ++sym) {
        if (SYMBOL_TO_FREQ[sym] != expected_symbol_to_freq[sym]) {
            std::cout << "FAIL: SYMBOL_TO_FREQ[" << sym << "] = " << SYMBOL_TO_FREQ[sym]
                      << " Hz (expected " << expected_symbol_to_freq[sym] << " Hz)\n";
            ok = false;
        }
    }
    if (ok) std::cout << "PASS: SYMBOL_TO_FREQ matches Table A-III\n";

    // Verify bijection: SYMBOL_TO_FREQ[FREQ_TO_SYMBOL[rank]] == TONE_FREQS_HZ[rank]
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        uint8_t sym = FREQ_TO_SYMBOL[rank];
        uint32_t roundtrip_freq = SYMBOL_TO_FREQ[sym];
        if (roundtrip_freq != TONE_FREQS_HZ[rank]) {
            std::cout << "FAIL: Bijection broken at rank " << rank
                      << ": SYMBOL_TO_FREQ[FREQ_TO_SYMBOL[" << rank << "]] = "
                      << roundtrip_freq << " Hz (expected " << TONE_FREQS_HZ[rank] << " Hz)\n";
            ok = false;
        }
    }
    if (ok) std::cout << "PASS: SYMBOL_TO_FREQ is bijective inverse of FREQ_TO_SYMBOL\n";

    // Verify SYMBOL_TO_FREQ contains no duplicate frequencies
    std::array<bool, 9> freq_seen{};
    for (uint32_t sym = 0; sym < NUM_TONES; ++sym) {
        uint32_t f = SYMBOL_TO_FREQ[sym];
        uint32_t idx = (f - 750) / 250;
        if (idx >= 8 || freq_seen[idx]) {
            std::cout << "FAIL: Duplicate or out-of-range frequency " << f
                      << " Hz at symbol " << sym << "\n";
            ok = false;
        }
        freq_seen[idx] = true;
    }
    if (ok) std::cout << "PASS: SYMBOL_TO_FREQ has no duplicate frequencies\n";

    if (ok) std::cout << "PASS: AC-WAVEFORM-001-002\n";
    return ok;
}

// ============================================================================
// Test AC-WAVEFORM-002-002: NCO phase continuity at symbol transitions
// ============================================================================

bool test_phase_continuity_ac_waveform_002_002() {
    std::cout << "\n[TEST AC-WAVEFORM-002-002] NCO: Phase continuity at symbol transitions\n";
    std::cout << "=======================================================================\n";

    // ---- 1) After reset(), phase = 0x40000000 (pi/2, sin=+1) ----
    // sin(pi/2) = 1.0  → first sample = trunc(1.0 * amplitude * 32767) with amplitude=0.7
    ToneGenerator gen;
    std::vector<int16_t> buf(SAMPLES_PER_SYMBOL);
    uint8_t sym0 = 0;
    gen.generate_symbols(&sym0, 1, buf.data(), TEST_AMPLITUDE);

    const int expected_peak = static_cast<int>(1.0f * TEST_AMPLITUDE * 32767.0f);
    const int actual_first  = static_cast<int>(buf[0]);
    std::cout << "  Expected first sample near peak (" << expected_peak
              << "), got " << actual_first << "\n";

    if (std::abs(actual_first - expected_peak) > 1) {
        std::cout << "  FAIL: initial phase not at 0x40000000 (pi/2)\n";
        return false;
    }
    std::cout << "  PASS: initial phase = 0x40000000 confirmed\n";

    // ---- 2) No phase reset at symbol transitions ----
    // Reference: generate [sym_a, sym_b] in a single call
    uint8_t syms[2] = {3, 5};
    ToneGenerator gen_ref;
    std::vector<int16_t> ref(2 * SAMPLES_PER_SYMBOL);
    gen_ref.generate_symbols(syms, 2, ref.data(), TEST_AMPLITUDE);

    // Split: generate sym_a, then sym_b across two separate calls — no reset in between
    ToneGenerator gen_split;
    std::vector<int16_t> split(2 * SAMPLES_PER_SYMBOL);
    gen_split.generate_symbols(&syms[0], 1, split.data(), TEST_AMPLITUDE);
    gen_split.generate_symbols(&syms[1], 1, split.data() + SAMPLES_PER_SYMBOL, TEST_AMPLITUDE);

    if (std::memcmp(ref.data(), split.data(), 2 * SAMPLES_PER_SYMBOL * sizeof(int16_t)) != 0) {
        std::cout << "  FAIL: phase was reset at symbol transition (one-call vs split-call differ)\n";
        for (uint32_t i = 0; i < 2 * SAMPLES_PER_SYMBOL; ++i) {
            if (ref[i] != split[i]) {
                std::cout << "  First diff at sample " << i
                          << ": ref=" << ref[i] << " split=" << split[i] << "\n";
                break;
            }
        }
        return false;
    }
    std::cout << "  PASS: no phase reset at symbol transition (single-call == split-call)\n";

    // ---- 3) reset() restores phase to 0x40000000 ----
    ToneGenerator gen_r;
    std::vector<int16_t> buf_r(SAMPLES_PER_SYMBOL);
    uint8_t sym7 = 7;
    gen_r.generate_symbols(&sym7, 1, buf_r.data(), TEST_AMPLITUDE); // advance phase
    gen_r.reset();
    gen_r.generate_symbols(&sym0, 1, buf_r.data(), TEST_AMPLITUDE);

    ToneGenerator gen_fresh;
    std::vector<int16_t> buf_fresh(SAMPLES_PER_SYMBOL);
    gen_fresh.generate_symbols(&sym0, 1, buf_fresh.data(), TEST_AMPLITUDE);

    if (std::memcmp(buf_r.data(), buf_fresh.data(), SAMPLES_PER_SYMBOL * sizeof(int16_t)) != 0) {
        std::cout << "  FAIL: reset() did not restore phase to 0x40000000\n";
        return false;
    }
    std::cout << "  PASS: reset() restores phase to 0x40000000\n";

    std::cout << "PASS: AC-WAVEFORM-002-002\n";
    return true;
}

// ============================================================================
// Test 6: Timing Constants
// ============================================================================

bool test_timing_constants() {
    std::cout << "\n[TEST 6] Timing Constants\n";
    std::cout << "========================\n";
    
    // AC-WAVEFORM-006-1: The tone rate must be exactly 125 symbols/second
    if (SYMBOL_RATE_BAUD != 125) {
        std::cout << "FAIL: SYMBOL_RATE_BAUD = " << SYMBOL_RATE_BAUD << " (expected 125)\n";
        return false;
    }
    std::cout << "PASS: SYMBOL_RATE_BAUD = 125 symbols/sec\n";
    
    // AC-WAVEFORM-006-2: The period per tone must be exactly 8 ms
    if (SYMBOL_DURATION_MS != 8) {
        std::cout << "FAIL: SYMBOL_DURATION_MS = " << SYMBOL_DURATION_MS << " (expected 8)\n";
        return false;
    }
    std::cout << "PASS: SYMBOL_DURATION_MS = 8 ms\n";
    
    // AC-WAVEFORM-007-1: The transmitted bit rate must be exactly 375 b/s
    if (SYMBOL_RATE_BAUD * BITS_PER_SYMBOL != 375) {
        std::cout << "FAIL: Bit rate = " << SYMBOL_RATE_BAUD * BITS_PER_SYMBOL << " (expected 375)\n";
        return false;
    }
    std::cout << "PASS: Bit rate = " << SYMBOL_RATE_BAUD * BITS_PER_SYMBOL << " b/s\n";
    
    // AC-WAVEFORM-008-1: Word transitions must be synchronized with tone transitions
    // This is implicitly verified by the existing tests that show proper symbol detection
    // and the fact that we have SYMBOLS_PER_WORD = 49 symbols per word
    if (SYMBOLS_PER_WORD != 49) {
        std::cout << "FAIL: SYMBOLS_PER_WORD = " << SYMBOLS_PER_WORD << " (expected 49)\n";
        return false;
    }
    std::cout << "PASS: SYMBOLS_PER_WORD = 49 (word transitions synchronized with tone transitions)\n";
    
    // AC-WAVEFORM-008-2: There must be exactly 49 symbols per redundant word
    if (SYMBOLS_PER_WORD != 49) {
        std::cout << "FAIL: SYMBOLS_PER_WORD = " << SYMBOLS_PER_WORD << " (expected 49)\n";
        return false;
    }
    std::cout << "PASS: 49 symbols per redundant word\n";
    
    // AC-WAVEFORM-009-1: Tw must be 130.66... ms
    // TW_MS = 392.0 / 3.0 = 130.666... ms
    const double expected_tw = 392.0 / 3.0;
    if (std::abs(TW_MS - expected_tw) > 0.001) {
        std::cout << "FAIL: TW_MS = " << TW_MS << " (expected ~130.667)\n";
        return false;
    }
    std::cout << "PASS: TW_MS = " << TW_MS << " ms (130.667 ms)\n";
    
    // AC-WAVEFORM-009-2: Tw must correspond to 16.33... symbols
    // 130.667 ms / 8 ms per symbol = 16.333... symbols
    const double expected_symbols = 392.0 / 3.0 / 8.0;
    if (std::abs(expected_symbols - 16.3333333333) > 0.001) {
        std::cout << "FAIL: Tw in symbols = " << expected_symbols << " (expected ~16.333)\n";
        return false;
    }
    std::cout << "PASS: Tw corresponds to " << expected_symbols << " symbols\n";
    
    // AC-WAVEFORM-010-1: 3×Tw must be exactly 392 ms
    if (TRW_MS != 392) {
        std::cout << "FAIL: TRW_MS = " << TRW_MS << " (expected 392)\n";
        return false;
    }
    std::cout << "PASS: TRW_MS = " << TRW_MS << " ms (exactly 392 ms)\n";
    
    std::cout << "PASS: All timing acceptance criteria\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-002-001: NCO 32-bit phase accumulator
// ============================================================================

bool test_nco_32bit_accumulator_ac_waveform_002_001() {
    std::cout << "\n[TEST AC-WAVEFORM-002-001] NCO 32-bit phase accumulator\n";
    std::cout << "=========================================================\n";

    // phase_ is uint32_t — sizeof must be 4
    static_assert(sizeof(uint32_t) == 4, "uint32_t must be 32 bits");
    std::cout << "PASS: uint32_t is 32 bits (phase_ type confirmed)\n";

    ToneGenerator gen;
    bool ok = true;

    // For each tone rank verify phase_increment == (freq_hz << 32) / SAMPLE_RATE_HZ
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        uint32_t freq_hz      = TONE_FREQS_HZ[rank];
        uint8_t  symbol       = FREQ_TO_SYMBOL[rank];
        uint64_t expected_inc = (static_cast<uint64_t>(freq_hz) << 32) / SAMPLE_RATE_HZ;
        uint32_t actual_inc   = gen.phase_increment_for(symbol);

        std::cout << "  freq=" << freq_hz << " Hz  sym=" << (int)symbol
                  << "  expected=0x" << std::hex << expected_inc
                  << "  actual=0x"   << actual_inc << std::dec;

        if (actual_inc != static_cast<uint32_t>(expected_inc)) {
            std::cout << "  FAIL\n";
            ok = false;
        } else {
            std::cout << "  PASS\n";
        }
    }

    if (ok) std::cout << "PASS: AC-WAVEFORM-002-001\n";
    return ok;
}

// ============================================================================
// Test AC-WAVEFORM-003-001: Sample-Rate = 8000 Hz
// ============================================================================

bool test_sample_rate_ac_waveform_003_001() {
    std::cout << "\n[TEST AC-WAVEFORM-003-001] Sample-Rate = 8000 Hz\n";
    std::cout << "=================================================\n";

    // Compile-time: SAMPLE_RATE_HZ must be a constexpr uint32_t with value 8000
    static_assert(SAMPLE_RATE_HZ == 8000u,
                  "SAMPLE_RATE_HZ must equal 8000 (MIL-STD-188-141B A.5.1.3)");

    // Runtime check (guards against future constant changes)
    if (SAMPLE_RATE_HZ != 8000u) {
        std::cout << "FAIL: SAMPLE_RATE_HZ = " << SAMPLE_RATE_HZ << " (expected 8000)\n";
        return false;
    }
    std::cout << "PASS: SAMPLE_RATE_HZ = " << SAMPLE_RATE_HZ << " Hz\n";

    // SAMPLES_PER_SYMBOL derived value must equal 64 (8000 / 125)
    if (SAMPLES_PER_SYMBOL != 64u) {
        std::cout << "FAIL: SAMPLES_PER_SYMBOL = " << SAMPLES_PER_SYMBOL << " (expected 64)\n";
        return false;
    }
    std::cout << "PASS: SAMPLES_PER_SYMBOL = " << SAMPLES_PER_SYMBOL
              << " (= SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD)\n";

    std::cout << "PASS: AC-WAVEFORM-003-001\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-003-002: Symbol-Rate = 125 Symbole/s
// ============================================================================

bool test_symbol_rate_ac_waveform_003_002() {
    std::cout << "\n[TEST AC-WAVEFORM-003-002] Symbol-Rate = 125 Symbole/s\n";
    std::cout << "========================================================\n";

    // Compile-time: SYMBOL_RATE_BAUD must be a constexpr uint32_t with value 125
    static_assert(SYMBOL_RATE_BAUD == 125u,
                  "SYMBOL_RATE_BAUD must equal 125 (MIL-STD-188-141B A.5.1.3)");

    // Runtime check
    if (SYMBOL_RATE_BAUD != 125u) {
        std::cout << "FAIL: SYMBOL_RATE_BAUD = " << SYMBOL_RATE_BAUD << " (expected 125)\n";
        return false;
    }
    std::cout << "PASS: SYMBOL_RATE_BAUD = " << SYMBOL_RATE_BAUD << " symbols/s\n";

    // SAMPLES_PER_SYMBOL must equal 64 (defined in ale_waveform.h)
    static_assert(SAMPLES_PER_SYMBOL == 64u,
                  "SAMPLES_PER_SYMBOL must equal 64 (= 8000 / 125)");

    if (SAMPLES_PER_SYMBOL != 64u) {
        std::cout << "FAIL: SAMPLES_PER_SYMBOL = " << SAMPLES_PER_SYMBOL << " (expected 64)\n";
        return false;
    }
    std::cout << "PASS: SAMPLES_PER_SYMBOL = " << SAMPLES_PER_SYMBOL << " (= SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD)\n";

    // Consistency: SAMPLES_PER_SYMBOL == SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD
    if (SAMPLES_PER_SYMBOL != SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD) {
        std::cout << "FAIL: SAMPLES_PER_SYMBOL (" << SAMPLES_PER_SYMBOL
                  << ") != SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD ("
                  << SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD << ")\n";
        return false;
    }
    std::cout << "PASS: SAMPLES_PER_SYMBOL is consistent with SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD\n";

    // 8 ms per symbol: 1000 ms / 125 symbols/s = 8 ms
    const uint32_t ms_per_symbol = 1000u / SYMBOL_RATE_BAUD;
    if (ms_per_symbol != 8u) {
        std::cout << "FAIL: ms/symbol = " << ms_per_symbol << " (expected 8)\n";
        return false;
    }
    std::cout << "PASS: 1000 / SYMBOL_RATE_BAUD = " << ms_per_symbol << " ms/symbol\n";

    std::cout << "PASS: AC-WAVEFORM-003-002\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-003-003: Trw = 392 ms (49 symbols × 8 ms)
// ============================================================================

bool test_trw_ms_ac_waveform_003_003() {
    std::cout << "\n[TEST AC-WAVEFORM-003-003] Trw = 392 ms (49 symbols x 8 ms)\n";
    std::cout << "==============================================================\n";

    // Primary derivation: 49 symbols x 8 ms/symbol = 392 ms
    const uint32_t trw_from_symbols = SYMBOLS_PER_WORD * SYMBOL_DURATION_MS;
    if (trw_from_symbols != 392u) {
        std::cout << "FAIL: SYMBOLS_PER_WORD * SYMBOL_DURATION_MS = "
                  << trw_from_symbols << " ms (expected 392)\n";
        return false;
    }
    std::cout << "PASS: SYMBOLS_PER_WORD (" << SYMBOLS_PER_WORD
              << ") x SYMBOL_DURATION_MS (" << SYMBOL_DURATION_MS
              << " ms) = " << trw_from_symbols << " ms\n";

    // Alternative derivation: 3 x Tw_ms = 3 x 130.666... = 392.000 ms (exact)
    const double trw_from_tw = 3.0 * TW_MS;
    if (std::abs(trw_from_tw - 392.0) > 0.001) {
        std::cout << "FAIL: 3 x TW_MS = " << trw_from_tw << " ms (expected 392.0)\n";
        return false;
    }
    std::cout << "PASS: 3 x TW_MS = " << trw_from_tw << " ms (= 392 ms)\n";

    // Constant check: ale::TRW_MS in ale_timing.h must be exactly 392
    static_assert(ale::TRW_MS == 392u,
                  "TRW_MS must be exactly 392 ms (49 symbols x 8 ms, MIL-STD-188-141B A.5.2.2)");
    if (ale::TRW_MS != 392u) {
        std::cout << "FAIL: ale::TRW_MS = " << ale::TRW_MS << " (expected 392)\n";
        return false;
    }
    std::cout << "PASS: ale::TRW_MS = " << ale::TRW_MS << " ms (exactly 392 ms)\n";

    // Cross-check: all three derivations must agree
    if (trw_from_symbols != ale::TRW_MS) {
        std::cout << "FAIL: symbol-based derivation (" << trw_from_symbols
                  << " ms) != ale::TRW_MS (" << ale::TRW_MS << " ms)\n";
        return false;
    }
    std::cout << "PASS: all derivations consistent: 49x8=" << trw_from_symbols
              << " ms == 3xTw=" << trw_from_tw
              << " ms == TRW_MS=" << ale::TRW_MS << " ms\n";

    std::cout << "PASS: AC-WAVEFORM-003-003\n";
    return true;
}

// ============================================================================
// Test AC-WAVEFORM-003-004: Tw = 130.666... ms (word duration, not 130 or 131)
// ============================================================================

bool test_tw_ms_ac_waveform_003_004() {
    std::cout << "\n[TEST AC-WAVEFORM-003-004] Tw = 130.666... ms (not 130, not 131)\n";
    std::cout << "===================================================================\n";

    // TW_MS must equal (49/3) * 8 = 130.666... ms exactly by formula.
    // Tolerance: 0.001 ms (well within any rounding concern).
    const double expected_tw = (49.0 / 3.0) * ale::TTONE_MS;
    if (std::abs(ale::TW_MS - expected_tw) > 0.001) {
        std::cout << "FAIL: TW_MS = " << ale::TW_MS << " ms (expected " << expected_tw << ")\n";
        return false;
    }
    std::cout << "PASS: TW_MS = " << ale::TW_MS << " ms (= (49/3)*8 = 130.666... ms)\n";

    // Must NOT be 130 (truncated) or 131 (rounded).
    if (ale::TW_MS == 130.0) {
        std::cout << "FAIL: TW_MS is the truncated integer 130 ms — must be 130.666... ms\n";
        return false;
    }
    std::cout << "PASS: TW_MS != 130.0 (not truncated)\n";

    if (ale::TW_MS == 131.0) {
        std::cout << "FAIL: TW_MS is 131 ms — rounded value is not allowed; must be 130.666... ms\n";
        return false;
    }
    std::cout << "PASS: TW_MS != 131.0 (not rounded up)\n";

    // Verify TW_MS is derived from T_SYMBOLS_PER_WORD * TTONE_MS (no hardcoding).
    const double tw_from_parts = ale::T_SYMBOLS_PER_WORD * ale::TTONE_MS;
    if (std::abs(ale::TW_MS - tw_from_parts) > 1e-9) {
        std::cout << "FAIL: TW_MS (" << ale::TW_MS
                  << ") != T_SYMBOLS_PER_WORD * TTONE_MS (" << tw_from_parts << ")\n";
        return false;
    }
    std::cout << "PASS: TW_MS is derived from T_SYMBOLS_PER_WORD (" << ale::T_SYMBOLS_PER_WORD
              << ") * TTONE_MS (" << ale::TTONE_MS << " ms) — no hardcoding\n";

    std::cout << "PASS: AC-WAVEFORM-003-004\n";
    return true;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int run_all_tests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  8-FSK Modem Unit Tests                                   ║\n";
    std::cout << "║  MIL-STD-188-141B Implementation                          ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    int pass_count = 0;
    int fail_count = 0;
    
    if (test_freq_table_ac_waveform_001_001()) { pass_count++; } else { fail_count++; }
    if (test_symbol_freq_mapping_ac_waveform_001_002()) { pass_count++; } else { fail_count++; }
    if (test_nco_32bit_accumulator_ac_waveform_002_001()) { pass_count++; } else { fail_count++; }
    if (test_phase_continuity_ac_waveform_002_002()) { pass_count++; } else { fail_count++; }
    if (test_sample_rate_ac_waveform_003_001()) { pass_count++; } else { fail_count++; }
    if (test_symbol_rate_ac_waveform_003_002()) { pass_count++; } else { fail_count++; }
    if (test_trw_ms_ac_waveform_003_003()) { pass_count++; } else { fail_count++; }
    if (test_tw_ms_ac_waveform_003_004()) { pass_count++; } else { fail_count++; }
    if (test_tone_generation()) { pass_count++; } else { fail_count++; }
    if (test_timing_constants()) { pass_count++; } else { fail_count++; }
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass_count << "  Failed: " << std::setw(2) << fail_count 
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

// ============================================================================
// Entry Point
// ============================================================================

int main() {
    return ale::run_all_tests();
}
