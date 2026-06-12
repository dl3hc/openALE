/**
 * @file timer.h
 * @brief Platform-agnostic timer interface for PC-ALE
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdint>
#include <memory>

namespace pal {

/**
 * @brief Timer interface for timing operations
 */
class ITimer {
public:
    virtual ~ITimer() = default;
    
    /**
     * @brief Get current time in milliseconds
     * @return Monotonic time in ms
     */
    virtual uint64_t get_time_ms() const = 0;
    
    /**
     * @brief Get current time in microseconds
     * @return Monotonic time in us
     */
    virtual uint64_t get_time_us() const = 0;
    
    /**
     * @brief Sleep for specified milliseconds
     * @param ms Milliseconds to sleep
     */
    virtual void sleep_ms(uint32_t ms) = 0;
    
    /**
     * @brief Sleep for specified microseconds
     * @param us Microseconds to sleep
     */
    virtual void sleep_us(uint32_t us) = 0;
};

/**
 * @brief Factory function - implemented per platform
 */
std::unique_ptr<ITimer> create_timer();

} // namespace pal
