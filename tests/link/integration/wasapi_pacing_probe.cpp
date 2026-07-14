/**
 * @file wasapi_pacing_probe.cpp
 * @brief Probe: do the REAL WASAPI driver's frame completions fire at on-air
 *        pace (~392 ms/word) or early (buffer-fill pace)?
 *
 * Opens the platform audio driver on the named devices (default: CABLE-A as
 * render, CABLE-B as capture), serves 10 symbol frames through the pull
 * source, arms 10 completions and timestamps each firing. Early completions
 * make ALEController's state machine race through its TX phases and drop PTT
 * while audio is still queued — the "brief TX" symptom.
 */

#include "App/audio_device.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;

int main(int argc, char* argv[])
{
    const int WORDS = 10;

    auto dev = ale::make_audio_device();

    // Pick devices: prefer CABLE for render so nothing audible plays.
    std::string in_name, out_name;
    for (const auto& d : dev->list_devices()) {
        std::printf("device: %s\n", d.c_str());
        if (d.rfind("IN:", 0) == 0 && in_name.empty()
            && d.find("CABLE-B Output") != std::string::npos)
            in_name = d.substr(5);
        if (d.rfind("OUT:", 0) == 0 && out_name.empty()
            && d.find("CABLE-A Input") != std::string::npos)
            out_name = d.substr(5);
    }
    if (argc > 2) { in_name = argv[1]; out_name = argv[2]; }
    std::printf("using in='%s' out='%s'\n", in_name.c_str(), out_name.c_str());

    if (!dev->open(in_name, out_name)) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }

    int served = 0;
    dev->set_symbol_source([&](uint8_t* out) {
        if (served >= WORDS) return false;
        for (int i = 0; i < 49; ++i) out[i] = static_cast<uint8_t>(i % 8);
        ++served;
        return true;
    });

    const auto t0 = steady_clock::now();
    auto ms = [&]() {
        return static_cast<long long>(
            duration_cast<milliseconds>(steady_clock::now() - t0).count());
    };

    int completed = 0;
    long long last_ms = 0;
    for (int i = 0; i < WORDS; ++i)
        dev->arm_frame_complete([&, i]() {
            const long long t = ms();
            std::printf("completion #%d at %lld ms  (delta %lld ms)\n",
                        i + 1, t, t - last_ms);
            last_ms = t;
            ++completed;
        });

    std::vector<int16_t> rx;
    while (completed < WORDS && ms() < 15000) {
        rx.clear();
        dev->tick(rx);
        std::this_thread::sleep_for(milliseconds(2));
    }

    std::printf("served=%d completed=%d elapsed=%lld ms  (expected ~%d ms for %d words)\n",
                served, completed, ms(), 392 * WORDS, WORDS);
    dev->close();
    return 0;
}
