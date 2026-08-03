#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

namespace pal {

class Resampler {
public:
    Resampler(uint32_t input_rate,
              uint32_t output_rate,
              int taps_per_phase = 16,
              int num_phases = 64);

    size_t process(const float* input, size_t input_count, float* output);

    void reset();

private:
    void design_filter();
    float apply_filter(int phase_index) const;

private:
    uint32_t in_rate_;
    uint32_t out_rate_;

    uint64_t phase_;
    uint64_t phase_inc_;
    uint64_t phase_mod_;

    int taps_per_phase_;
    int num_phases_;
    int history_size_;

    std::vector<float> coeffs_;
    std::vector<float> history_;
    size_t history_pos_;
};

} // namespace pal