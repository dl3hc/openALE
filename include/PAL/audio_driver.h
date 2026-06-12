/**
 * @file audio_driver.h
 * @brief Platform-agnostic audio driver interface for PC-ALE
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <functional>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace pal {

/**
 * @brief Audio driver interface for platform abstraction
 */
class IAudioDriver {
public:
    virtual ~IAudioDriver() = default;
    
    /**
     * @brief Audio processing callback type
     * 
     * @param rx_samples  Received audio (mono float, -1.0 to 1.0)
     * @param tx_samples  Transmit buffer to fill
     * @param num_samples Sample count
     * 
     * @note Runs in real-time thread - avoid allocations, locks, I/O
     */
    using AudioCallback = std::function<void(
        const float* rx_samples,
        float* tx_samples,
        size_t num_samples
    )>;
    
    /**
     * @brief Initialize audio driver
     * 
     * @param device_name Platform-specific device (e.g., "plughw:udrc,0")
     * @param sample_rate Sample rate in Hz
     * @param buffer_frames Frames per callback
     * @return true on success
     */
    virtual bool initialize(const std::string& device_name,
                           uint32_t sample_rate,
                           uint32_t buffer_frames) = 0;
    
    virtual void shutdown() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    
    virtual void set_audio_callback(AudioCallback callback) = 0;
    
    virtual bool is_running() const = 0;
    virtual uint32_t get_sample_rate() const = 0;
    virtual uint32_t get_buffer_frames() const = 0;
    virtual float get_latency_ms() const = 0;
};

/**
 * @brief Factory function - implemented per platform
 */
std::unique_ptr<IAudioDriver> create_audio_driver();

} // namespace pal
