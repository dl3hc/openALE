/**
 * \file apps/ale_cli.cpp
 * \brief ALE 2G command-line interface — idle station / individual call.
 *
 * Every instance starts in the idle (scanning) state and runs the full
 * protocol — call, 3-way handshake, linked — automatically.  Whether an
 * instance calls or just listens is decided purely by the options:
 *
 *   No --call    → idle: scan and auto-respond to calls addressed to --self.
 *   --call ADDR  → calling: initiate an individual call to ADDR, then idle.
 *
 * --self is always required: even an idle station needs its own callsign so
 * the state machine can recognise calls addressed to it.
 *
 * Options
 *   --self       ADDR    Own ALE address (3–15 Basic 38 uppercase chars)  [required]
 *   --call       ADDR    Target address to call (omit to stay idle)
 *   --device     NAME    Audio device substring for both RX and TX
 *   --in-device  NAME    Audio input  device substring (RX, waveIn)
 *   --out-device NAME    Audio output device substring (TX, waveOut)
 *   --list-devices       Print available audio devices and exit
 *   --no-scan            Skip scanning section (target is on a fixed channel)
 *
 * Single-PC full-duplex loopback with VB-Audio CABLE A+B
 * ───────────────────────────────────────────────────────
 *   Idle (BOB):  ale_cli --self BOB --in-device "CABLE-A Output" --out-device "CABLE-B Input"
 *   Call (SAM):  ale_cli --self SAM --call BOB --in-device "CABLE-B Output" --out-device "CABLE-A Input"
 *
 *   CABLE-A carries SAM→BOB audio, CABLE-B carries BOB→SAM audio.
 *   Use --list-devices to find exact device name substrings.
 *
 * Two-PC loopback (physical or virtual cable)
 * ────────────────────────────────────────────
 *   PC 1 (BOB):  ale_cli --self BOB
 *   PC 2 (SAM):  ale_cli --self SAM --call BOB
 *   Connect speaker out of PC 2 to mic in of PC 1 and vice versa.
 *
 * The device runs at its native rate (48 kHz); the 8 kHz modem audio is
 * resampled internally, so no manual sample-rate setup is required.
 *
 * Timing note: LBT=784 ms + tune=1045 ms before first TX → first word at ~1.8 s.
 *              Full 3-way handshake (3-char addresses, no scanning) ≈ 6–8 s.
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <thread>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>

using namespace ale;

// ── Signal handling ───────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void sig_handler(int) { g_running = false; }

// ── Monotonic time helper ─────────────────────────────────────────────────────

static uint32_t now_ms()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
}

// ── Usage ─────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog)
{
    std::fprintf(stderr,
        "ALE 2G CLI — MIL-STD-188-141B individual call / 3-way handshake\n"
        "\n"
        "Every instance starts idle and runs the full protocol automatically.\n"
        "Add --call to initiate a call; omit it to just listen.\n"
        "\n"
        "Usage:\n"
        "  %s --self ADDR [--call TARGET] [audio options]\n"
        "\n"
        "Options:\n"
        "  --self       ADDR   Own ALE address (3-15 uppercase alphanumeric)  [required]\n"
        "  --call       ADDR   Target address to call (omit to stay idle)\n"
        "  --device     NAME   Audio device substring for both RX and TX\n"
        "  --in-device  NAME   Audio input  device substring (RX, waveIn)\n"
        "  --out-device NAME   Audio output device substring (TX, waveOut)\n"
        "  --list-devices      Print available audio devices\n"
        "  --no-scan           Skip scanning (fixed channel, shorter frames)\n"
        "\n"
        "Single-PC loopback with VB-Audio CABLE A+B:\n"
        "  %s --self BOB --in-device \"CABLE-A Output\" --out-device \"CABLE-B Input\"\n"
        "  %s --self SAM --call BOB --in-device \"CABLE-B Output\" --out-device \"CABLE-A Input\"\n",
        prog, prog, prog);
}

// ── Status banner ─────────────────────────────────────────────────────────────

static void print_banner(const std::string& self,
                          bool               call_mode,
                          const std::string& target,
                          const std::string& in_device,
                          const std::string& out_device)
{
    std::printf("\n");
    std::printf("╔═══════════════════════════════════════════════════════╗\n");
    std::printf("║             ALE 2G CLI  —  MIL-STD-188-141B          ║\n");
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Self   : %-44s ║\n", self.c_str());
    std::printf("║  Mode   : %-44s ║\n",
                call_mode ? ("CALL → " + target).c_str() : "IDLE — scanning (auto-accept)");
    if (!in_device.empty() || !out_device.empty()) {
        const std::string rx_label = in_device.empty()  ? "(default)" : in_device;
        const std::string tx_label = out_device.empty() ? "(default)" : out_device;
        if (rx_label == tx_label) {
            std::printf("║  Device : %-44s ║\n", rx_label.c_str());
        } else {
            std::printf("║  RX in  : %-44s ║\n", rx_label.c_str());
            std::printf("║  TX out : %-44s ║\n", tx_label.c_str());
        }
    }
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Ctrl+C to quit                                       ║\n");
    std::printf("╚═══════════════════════════════════════════════════════╝\n");
    std::printf("\n");
    std::fflush(stdout);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string self_addr;
    std::string target_addr;
    std::string in_device;
    std::string out_device;
    bool list_devs    = false;
    bool no_scan      = true;   // default: skip scanning, use leading-call only

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self") == 0 && i + 1 < argc) {
            self_addr = argv[++i];
        } else if ((std::strcmp(argv[i], "--call")   == 0 ||
                    std::strcmp(argv[i], "--target") == 0) && i + 1 < argc) {
            target_addr = argv[++i];
        } else if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            // --device sets both in and out to the same name
            in_device = out_device = argv[++i];
        } else if (std::strcmp(argv[i], "--in-device") == 0 && i + 1 < argc) {
            in_device = argv[++i];
        } else if (std::strcmp(argv[i], "--out-device") == 0 && i + 1 < argc) {
            out_device = argv[++i];
        } else if (std::strcmp(argv[i], "--list-devices") == 0) {
            list_devs = true;
        } else if (std::strcmp(argv[i], "--no-scan") == 0) {
            no_scan = true;
        }
    }

    // Mode is decided purely by whether a call target was given.
    const bool call_mode = !target_addr.empty();

    // ── List devices ─────────────────────────────────────────────────────
    if (list_devs) {
        auto dev = make_audio_device();
        auto names = dev->list_devices();
        std::printf("Available audio devices:\n");
        for (const auto& n : names)
            std::printf("  %s\n", n.c_str());
        return 0;
    }

    // ── Validate ──────────────────────────────────────────────────────────
    // --self is required in both modes: an idle station needs its own callsign
    // to recognise calls addressed to it.
    if (self_addr.empty()) {
        std::fprintf(stderr, "ERROR: --self ADDR is required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    // ── Setup audio device ────────────────────────────────────────────────
    auto audio = make_audio_device();
    if (!audio->open(in_device, out_device)) {
        std::fprintf(stderr, "ERROR: Failed to open audio device(s).\n");
        if (!in_device.empty())  std::fprintf(stderr, "  RX in  : %s\n", in_device.c_str());
        if (!out_device.empty()) std::fprintf(stderr, "  TX out : %s\n", out_device.c_str());
        std::fprintf(stderr, "Run with --list-devices to see available devices.\n");
        return 1;
    }

    // ── Setup ALE controller ──────────────────────────────────────────────
    ALEController ctrl;
    ctrl.set_self_address(self_addr);
    ctrl.set_target_scan_channels(no_scan ? 0u : 1u);

    // Wire TX audio: controller PCM → audio output
    ctrl.on_tx_audio = [&](const int16_t* s, uint32_t n) {
        audio->write_tx(s, n);
    };

    // Status messages
    ctrl.on_status_changed = [](const std::string& msg) {
        std::printf("[  ] %s\n", msg.c_str());
        std::fflush(stdout);
    };

    // Incoming call notification
    ctrl.on_call_received = [](const std::string& caller) {
        std::printf("[>>] Incoming call from: %s\n", caller.c_str());
        std::printf("[>>] Protocol response in progress...\n");
        std::fflush(stdout);
    };

    // Link established
    ctrl.on_link_established = [&](const std::string& peer) {
        std::printf("\n");
        std::printf("╔══════════════════════════════════════════════════════╗\n");
        std::printf("║          LINK ESTABLISHED                            ║\n");
        std::printf("║  Peer : %-44s ║\n", peer.c_str());
        std::printf("║  Self : %-44s ║\n", self_addr.c_str());
        std::printf("╠══════════════════════════════════════════════════════╣\n");
        std::printf("║  Press Ctrl+C to terminate.                          ║\n");
        std::printf("╚══════════════════════════════════════════════════════╝\n\n");
        std::fflush(stdout);
    };

    // Link terminated
    ctrl.on_link_terminated = [](const std::string& reason) {
        std::printf("[xx] Link terminated: %s\n\n", reason.c_str());
        std::fflush(stdout);
    };

    print_banner(self_addr, call_mode, target_addr, in_device, out_device);

    // ── Start ALE operation ───────────────────────────────────────────────
    if (call_mode) {
        ctrl.initiate_call(target_addr);
        std::printf("[>>] LBT %.0f ms + tune %.0f ms before first TX...\n",
                    static_cast<double>(ALETimingConstants::Twt_ms),
                    static_cast<double>(ALETimingConstants::Tt_ms));
        std::fflush(stdout);
    } else {
        ctrl.start_listening();
        std::printf("[>>] Scanning — waiting for incoming calls addressed to %s\n\n",
                    self_addr.c_str());
        std::fflush(stdout);
    }

    // ── Main event loop ───────────────────────────────────────────────────
    std::vector<int16_t> rx_buf;
    while (g_running) {
        const uint32_t t = now_ms();

        // Drive state machine and modem
        ctrl.update(t);

        // Collect captured audio and feed to RX pipeline
        rx_buf.clear();
        audio->tick(rx_buf);
        if (!rx_buf.empty())
            ctrl.feed_audio(rx_buf.data(), static_cast<uint32_t>(rx_buf.size()));

        // ~1 ms sleep: keeps CPU usage low while meeting all ALE timing windows.
        // The tightest timing window is Tlww = 392 ms; 1 ms resolution is fine.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // ── Clean up ──────────────────────────────────────────────────────────
    ctrl.emergency_stop();
    audio->close();
    std::printf("[ALE CLI] Exiting.\n");
    return 0;
}
