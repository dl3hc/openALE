/**
 * \file PAL/timer.cpp
 * \brief Platform timer implementation (std::chrono + OS sleep resolution).
 *
 * Windows: timeBeginPeriod(1) raises the system timer resolution to 1 ms so
 * sleep_ms(1) reliably sleeps ~1 ms instead of the default ~15.6 ms; period
 * released in the destructor.
 *
 * Other platforms: POSIX nanosleep via std::this_thread::sleep_for.
 */

#include "PAL/timer.h"
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace pal {

class SteadyTimer : public ITimer {
public:
    SteadyTimer()
        : epoch_(std::chrono::steady_clock::now())
    {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }

    ~SteadyTimer() override
    {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

    uint64_t get_time_ms() const override
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - epoch_).count());
    }

    uint64_t get_time_us() const override
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - epoch_).count());
    }

    void sleep_ms(uint32_t ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    void sleep_us(uint32_t us) override
    {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }

private:
    const std::chrono::steady_clock::time_point epoch_;
};

std::unique_ptr<ITimer> create_timer()
{
    return std::make_unique<SteadyTimer>();
}

} // namespace pal
