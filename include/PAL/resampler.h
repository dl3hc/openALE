/**
 * @file resampler.h
 * @brief Sample rate conversion for PC-ALE (48kHz <-> 8kHz)
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace pal {

/**
 * @brief Polyphase FIR resampler for integer ratio conversion
 * 
 * Designed for 6:1 ratio (48 kHz <-> 8 kHz).
 * Uses windowed-sinc lowpass to prevent aliasing.
 */
class Resampler {
public:
    /**
     * @brief Construct resampler
     * 
     * @param ratio Resampling ratio (default 6 for 48kHz <-> 8kHz)
     * @param taps_per_phase Filter taps per polyphase branch
     */
    explicit Resampler(int ratio = 6, int taps_per_phase = 8);
    
    /**
     * @brief Decimate: high rate -> low rate (48kHz -> 8kHz)
     * 
     * @param input Input samples at high rate
     * @param input_count Number of input samples
     * @param output Output buffer (must hold input_count/ratio samples)
     * @return Number of output samples produced
     */
    size_t decimate(const float* input, size_t input_count, float* output);
    
    /**
     * @brief Interpolate: low rate -> high rate (8kHz -> 48kHz)
     * 
     * @param input Input samples at low rate
     * @param input_count Number of input samples
     * @param output Output buffer (must hold input_count*ratio samples)
     * @return Number of output samples produced
     */
    size_t interpolate(const float* input, size_t input_count, float* output);
    
    /**
     * @brief Reset filter state (clear history)
     */
    void reset();
    
    /**
     * @brief Get resampling ratio
     */
    int get_ratio() const { return ratio_; }

private:
    void design_filter();
    float apply_filter() const;
    
    int ratio_;
    int taps_per_phase_;
    int total_taps_;
    
    std::vector<float> coeffs_;
    std::vector<float> history_;
    size_t history_pos_;
};

} // namespace pal
