/**
 * \file tone_generator.cpp
 * \brief Implementation of NCO-based 8-FSK tone generator
 */

#define _USE_MATH_DEFINES
#include "fsk/tone_generator.h"
#include <cmath>
#include <algorithm>

namespace ale {

static_assert(SAMPLE_RATE_HZ % SYMBOL_RATE_BAUD == 0,
              "ALE2G requires an integer number of samples per symbol");

ToneGenerator::ToneGenerator() {
    init_sine_table();
    init_phase_increments();
    reset();
}

void ToneGenerator::init_sine_table() {
    // Pre-compute sine table for one complete cycle
    for (uint32_t i = 0; i < SINE_TABLE_SIZE; ++i) {
        double angle = 2.0 * M_PI * i / SINE_TABLE_SIZE;
        sine_table[i] = std::sin(angle);
    }
}

void ToneGenerator::init_phase_increments() {
    // Walk tones in ascending-frequency order (rank). FREQ_TO_SYMBOL[rank] gives the
    // symbol value carried by that tone, so we store its increment in the matching
    // symbol slot. phase_increment thus stays indexed by SYMBOL VALUE.
    for (uint32_t rank = 0; rank < NUM_TONES; ++rank) {
        uint8_t  symbol  = FREQ_TO_SYMBOL[rank];
        uint32_t freq_hz = TONE_FREQS_HZ[rank];
        
        // Exact integer fixed-point increment in Q32.
        // For ALE2G frequencies and 8 kHz, 64 samples complete an integer number of cycles.
        uint64_t increment = (static_cast<uint64_t>(freq_hz) << 32) / SAMPLE_RATE_HZ;
        phase_increment[symbol] = static_cast<uint32_t>(increment);
    }
}

void ToneGenerator::reset() {
    // 0x40000000 = pi/2 in 32-bit phase: sin = +1, slope = 0.
    // Symbol boundaries are aligned to waveform maxima/minima.
    phase_ = 0x40000000u;
}

float ToneGenerator::sine_interpolate(uint32_t phase) const {
    // phase is 32-bit, use upper bits as table index
    uint32_t index = (phase >> 24) & 0xFF;  // Use bits 24-31 for table index
    uint32_t frac = phase & 0xFFFFFF;       // Fractional part
    
    float frac_norm = frac / static_cast<float>(1 << 24);
    
    uint32_t next_index = (index + 1) & (SINE_TABLE_SIZE - 1);
    float s0 = sine_table[index];
    float s1 = sine_table[next_index];
    
    // Linear interpolation
    return s0 * (1.0f - frac_norm) + s1 * frac_norm;
}

uint32_t ToneGenerator::generate_symbols(const uint8_t* symbols, uint32_t num_symbols,
                                         int16_t* output, float amplitude) {
    uint32_t samples_written = 0;
    
    for (uint32_t sym_idx = 0; sym_idx < num_symbols; ++sym_idx) {
        uint8_t symbol = symbols[sym_idx];
        if (symbol >= NUM_TONES) {
            symbol = NUM_TONES - 1;  // Clamp invalid symbols
        }
        
        uint32_t phase_inc = phase_increment[symbol];
        
        // Generate one symbol = 64 samples at this frequency.
        // The phase accumulator is continuous across symbol boundaries.
        for (uint32_t sample_idx = 0; sample_idx < SAMPLES_PER_SYMBOL; ++sample_idx) {
            float sine_val = sine_interpolate(phase_);
            
            // Convert to int16 with amplitude scaling
            int32_t sample = static_cast<int32_t>(sine_val * amplitude * 32767.0f);
            sample = std::max(-32768, std::min(32767, sample));
            
            output[samples_written++] = static_cast<int16_t>(sample);
            
            // Update phase accumulator
            phase_ += phase_inc;
        }
    }
    
    return samples_written;
}

uint32_t ToneGenerator::generate_tone(uint8_t symbol_value, uint32_t num_samples,
                                       int16_t* output, float amplitude) {
    if (symbol_value >= NUM_TONES) {
        symbol_value = NUM_TONES - 1;
    }
    
    uint32_t phase_inc = phase_increment[symbol_value];
    
    for (uint32_t i = 0; i < num_samples; ++i) {
        float sine_val = sine_interpolate(phase_);
        
        int32_t sample = static_cast<int32_t>(sine_val * amplitude * 32767.0f);
        sample = std::max(-32768, std::min(32767, sample));
        
        output[i] = static_cast<int16_t>(sample);
        
        phase_ += phase_inc;
    }
    
    return num_samples;
}

} // namespace ale
