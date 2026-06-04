/**
 * \file fsk/ale_waveform.h
 * \brief ALE-2G waveform parameters and physical-layer infrastructure
 *
 * Physical layer constants (tone frequencies, timing, FFT parameters),
 * the tone-to-symbol mapping, and the FFT sliding-window buffer used by
 * the demodulator.
 *
 * Specification: MIL-STD-188-141B Appendix A
 *  - 8-FSK modulation: 8 tones, 125 baud, 250 Hz spacing
 *  - Bandwidth: 1.75 kHz (tones 750-2500 Hz)
 */

#pragma once

#include <cstdint>
#include <complex>
#include <array>
#include <vector>

namespace ale {

// Physical layer constants per MIL-STD-188-141B
constexpr uint32_t SAMPLE_RATE_HZ      = 8000;
constexpr uint32_t SYMBOL_RATE_BAUD    = 125;
constexpr uint32_t TONE_SPACING_HZ     = 250;
constexpr uint32_t NUM_TONES           = 8;
constexpr uint32_t BITS_PER_SYMBOL     = 3;
constexpr uint32_t BANDWIDTH_HZ        = 1750;
constexpr uint32_t SYMBOL_DURATION_MS  = 8;
constexpr uint32_t TRW_MS              = 392;
constexpr double   TW_MS               = 392.0 / 3.0;
static_assert(TRW_MS == 392,         "TRW must be exactly 392 ms");
static_assert(TW_MS * 3 == TRW_MS,  "3 × TW must equal TRW");

// Word timing (number of symbols that form one ALE word on the air)
constexpr uint32_t SYMBOLS_PER_WORD    = 49;

// (1) Tone frequencies (Hz) in ASCENDING order, indexed by frequency rank (0 = lowest).
//     A pure physical list — carries NO symbol assignment.
constexpr std::array<uint32_t, NUM_TONES> TONE_FREQS_HZ = {
    750, 1000, 1250, 1500, 1750, 2000, 2250, 2500
};

// (2) MIL-STD-188-141B A.5.1.2 symbol assignment: frequency rank -> symbol value.
//     Single source of truth for the Gray-coded tone-to-symbol mapping.
constexpr std::array<uint8_t, NUM_TONES> FREQ_TO_SYMBOL = {
    0, 1, 3, 2, 6, 7, 5, 4
};

constexpr bool freq_to_symbol_is_permutation() {
    std::array<bool, NUM_TONES> seen{};
    for (uint8_t r = 0; r < NUM_TONES; ++r) {
        uint8_t s = FREQ_TO_SYMBOL[r];
        if (s >= NUM_TONES || seen[s]) return false;
        seen[s] = true;
    }
    return true;
}
static_assert(freq_to_symbol_is_permutation(),
              "FREQ_TO_SYMBOL must list each symbol value 0..7 exactly once");

// FFT parameters
constexpr uint32_t FFT_SIZE        = 64;
constexpr uint32_t FFT_BIN_OFFSET  = 6;
constexpr uint32_t FFT_BIN_STEP    = 2;
constexpr uint32_t FFT_BIN_SPAN    = 15;

// Redundancy parameters
constexpr uint32_t SYMBOL_REPETITION  = 3;
constexpr uint32_t VOTE_BUFFER_LENGTH = 48;
constexpr uint32_t VOTE_THRESHOLD_BAD = 25;

using ComplexFloat  = std::complex<float>;
using ComplexDouble = std::complex<double>;

/**
 * \struct Symbol
 * Decoded FSK symbol with confidence metrics
 */
struct Symbol {
    uint8_t  bits[BITS_PER_SYMBOL];
    float    magnitude;
    float    signal_to_noise;
    uint32_t sample_index;
};

/**
 * \class FFTBuffer
 * Circular buffer for sliding FFT analysis.
 *
 * Implements O(N) per-bin DFT computation for streaming audio.
 */
class FFTBuffer {
public:
    FFTBuffer();

    /**
     * Add new sample and return updated FFT magnitudes.
     * \param sample Audio sample value (-32768 to +32767)
     * \return Reference to magnitude array [FFT_SIZE]
     */
    const std::array<float, FFT_SIZE>& push_sample(int16_t sample);

    /** Get current FFT magnitudes without advancing. */
    const std::array<float, FFT_SIZE>& get_magnitudes() const;

    /** Reset buffer to zero. */
    void reset();

private:
    std::array<float, FFT_SIZE> fft_cs_twiddle;
    std::array<float, FFT_SIZE> fft_ss_twiddle;
    std::array<float, FFT_SIZE> s0, s1, s2;
    std::array<float, FFT_SIZE> coeff;
    std::array<float, FFT_SIZE> magnitude;

    uint32_t sample_count;
    uint32_t fft_history_offset;
    std::array<float, FFT_SIZE> sample_buffer;
    uint32_t buffer_index;

    void compute_magnitudes();
    void compute_magnitudes_from_buffer(const std::array<float, FFT_SIZE>& samples);
};

} // namespace ale
