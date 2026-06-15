/**
 * \file apps/ale_cli.cpp
 * \brief ALE 2G command-line interface — idle station / individual call.
 *
 * Every instance starts in the idle (scanning) state and runs the full
 * protocol — call, 3-way handshake, linked — automatically.  Whether an
 * instance calls or just listens is decided by --call at startup or by a
 * CMD:CALL command sent to stdin at runtime.
 *
 *   No --call    → idle: scan and auto-respond to calls addressed to --self.
 *   --call ADDR  → calling: initiate an individual call to ADDR on startup.
 *
 * --self is always required: even an idle station needs its own callsign so
 * the state machine can recognise calls addressed to it.
 *
 * Runtime commands (sent to stdin while running):
 *   CMD:CALL <ADDR>  — initiate individual call
 *   CMD:TERMINATE    — terminate current link
 *   CMD:REJECT       — reject incoming call (TWAS)
 *   CMD:LISTEN       — re-enter scanning state
 *   CMD:STATUS       — print current SM state
 *   CMD:HELP         — list commands
 *
 * Options
 *   --self       ADDR    Own ALE address (3–15 Basic 38 uppercase chars)  [required]
 *   --call       ADDR    Target address to call on startup (optional)
 *   --in-device  NAME    Audio input  device substring (RX, waveIn)
 *   --out-device NAME    Audio output device substring (TX, waveOut)
 *   --list-devices       Print available audio devices and exit
 *   --no-scan            Skip scanning section (target is on a fixed channel)
 *
 * Single-PC full-duplex loopback with VB-Audio CABLE A+B
 * ───────────────────────────────────────────────────────
 *   Station BOB:  ale_cli --self BOB --in-device "CABLE-A Output" --out-device "CABLE-B Input"
 *   Station SAM:  ale_cli --self SAM --in-device "CABLE-B Output" --out-device "CABLE-A Input"
 *
 *   CABLE-A carries SAM→BOB audio, CABLE-B carries BOB→SAM audio.
 *   Use --list-devices to find exact device name substrings.
 *   Initiate the call at runtime: type  CMD:CALL BOB  in SAM's terminal.
 *
 * Two-PC test (physical or virtual cable)
 * ────────────────────────────────────────
 *   PC 1 (BOB):  ale_cli --self BOB
 *   PC 2 (SAM):  ale_cli --self SAM
 *   Connect speaker out of PC 2 to mic in of PC 1 and vice versa.
 *   Type  CMD:CALL BOB  in SAM's terminal to initiate.
 *
 * The device runs at its native rate (48 kHz); the 8 kHz modem audio is
 * resampled internally, so no manual sample-rate setup is required.
 *
 * Timing note: LBT=784 ms + tune=1045 ms before first TX → first word at ~1.8 s.
 *              Full 3-way handshake (3-char addresses, no scanning) ≈ 6–8 s.
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"
#include "PAL/radio.h"
#include "PAL/timer.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <thread>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <iostream>

using namespace ale;

// ── Signal handling ───────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void sig_handler(int) { g_running = false; }

// ── Stdin command reader ───────────────────────────────────────────────────────

static std::mutex              g_cmd_mutex;
static std::queue<std::string> g_cmd_queue;

static void stdin_reader()
{
    std::string line;
    while (g_running && std::getline(std::cin, line))
        if (!line.empty()) {
            std::lock_guard<std::mutex> lk(g_cmd_mutex);
            g_cmd_queue.push(std::move(line));
        }
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
        "  --in-device  NAME   Audio input  device substring (RX, waveIn)\n"
        "  --out-device NAME   Audio output device substring (TX, waveOut)\n"
        "  --list-devices      Print available audio devices\n"
        "  --no-scan           Skip scanning (fixed channel, shorter frames)\n"
        "  --radio      SPEC   Radio CAT/PTT: hamlib:<model_id>:<port>\n"
        "                        e.g. hamlib:229:COM3  hamlib:229:tcp://127.0.0.1:4532\n"
        "                        (run 'rigctl -l' to find your radio's model ID)\n"
        "\n"
        "Receiver FEC / sync tuning (MIL-STD-188-141B A.5.2.6.3):\n"
        "  --golay-mode N      Golay correction power: 3=3/4 (default), 2=2/5, 1=1/6, 0=0/7\n"
        "                      (n/m = up to n errors corrected, m detected)\n"
        "  --unanimous  N      Min unanimous 2/3-votes to accept a word, 0-49 (default 33)\n"
        "  --adaptive          Auto-adjust Golay mode + unanimous threshold to signal quality\n"
        "  --debug-rx          Log RX peak level + every demodulated word (diagnostics)\n"
        "\n"
        "Single-PC loopback with VB-Audio CABLE A+B:\n"
        "  %s --self BOB --in-device \"CABLE-A Output\" --out-device \"CABLE-B Input\"\n"
        "  %s --self SAM --call BOB --in-device \"CABLE-B Output\" --out-device \"CABLE-A Input\"\n",
        prog, prog, prog);
}

// ── Status banner ─────────────────────────────────────────────────────────────

static void print_banner(const std::string& self,
                          bool               call_mode,
                          bool               no_scan,
                          const std::string& target,
                          const std::string& in_device,
                          const std::string& out_device,
                          const std::string& radio_spec)
{
    const char* mode_str;
    if (call_mode)
        mode_str = ("CALL → " + target).c_str();
    else if (no_scan)
        mode_str = "IDLE — available (auto-accept)";
    else
        mode_str = "IDLE → SCANNING (auto-accept)";

    std::printf("\n");
    std::printf("╔═══════════════════════════════════════════════════════╗\n");
    std::printf("║             ALE 2G CLI  —  MIL-STD-188-141B          ║\n");
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Self   : %-44s ║\n", self.c_str());
    std::printf("║  Mode   : %-44s ║\n", mode_str);
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
    if (!radio_spec.empty())
        std::printf("║  Radio  : %-44s ║\n", radio_spec.c_str());
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Runtime commands (stdin):                            ║\n");
    std::printf("║    CMD:CALL <ADDR>  CMD:ADD_CHANNEL <CH>  CMD:STATUS  ║\n");
    std::printf("║    CMD:TERMINATE    CMD:REJECT       CMD:SCAN         ║\n");
    std::printf("║    CMD:HELP                                           ║\n");
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
    std::string radio_spec;
    bool list_devs    = false;
    bool no_scan      = true;   // default: skip scanning, use leading-call only

    // Receiver FEC / sync tuning (A.5.2.6.3); defaults = most tolerant point.
    int  golay_mode_arg = 3;    // 3=3/4 (full correction)
    int  unanimous_arg  = -1;   // -1 → keep demodulator default (33)
    bool adaptive_arg   = false;
    bool debug_rx_arg   = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self") == 0 && i + 1 < argc) {
            self_addr = argv[++i];
        } else if ((std::strcmp(argv[i], "--call")   == 0 ||
                    std::strcmp(argv[i], "--target") == 0) && i + 1 < argc) {
            target_addr = argv[++i];
        } else if (std::strcmp(argv[i], "--in-device") == 0 && i + 1 < argc) {
            in_device = argv[++i];
        } else if (std::strcmp(argv[i], "--out-device") == 0 && i + 1 < argc) {
            out_device = argv[++i];
        } else if (std::strcmp(argv[i], "--list-devices") == 0) {
            list_devs = true;
        } else if (std::strcmp(argv[i], "--no-scan") == 0) {
            no_scan = true;
        } else if (std::strcmp(argv[i], "--golay-mode") == 0 && i + 1 < argc) {
            golay_mode_arg = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--unanimous") == 0 && i + 1 < argc) {
            unanimous_arg = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--adaptive") == 0) {
            adaptive_arg = true;
        } else if (std::strcmp(argv[i], "--debug-rx") == 0) {
            debug_rx_arg = true;
        } else if (std::strcmp(argv[i], "--radio") == 0 && i + 1 < argc) {
            radio_spec = argv[++i];
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

    // Validate receiver FEC / sync tuning (A.5.2.6.3).
    if (golay_mode_arg < 0 || golay_mode_arg > 3) {
        std::fprintf(stderr, "ERROR: --golay-mode must be 0..3 (3=3/4, 2=2/5, 1=1/6, 0=0/7).\n");
        return 1;
    }
    if (unanimous_arg > 49) {
        std::fprintf(stderr, "ERROR: --unanimous must be 0..49.\n");
        return 1;
    }

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    auto timer = pal::create_timer();

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

    // Receiver FEC tuning (MIL-STD-188-141B A.5.2.6.3).
    ctrl.set_golay_mode(static_cast<GolayMode>(golay_mode_arg));
    if (unanimous_arg >= 0)
        ctrl.set_min_unanimous_votes(static_cast<uint8_t>(unanimous_arg));
    if (adaptive_arg)
        ctrl.set_adaptive_fec(true);
    if (debug_rx_arg) {
        ctrl.set_debug_rx(true);
        std::printf("[>>] RX diagnostics ON (peak level + decoded words)\n");
    }
    if (adaptive_arg)
        std::printf("[>>] Adaptive FEC ON — auto Golay mode + unanimous threshold (A.5.2.6.3)\n");
    else if (golay_mode_arg != 3 || unanimous_arg >= 0)
        std::printf("[>>] FEC: Golay %d/%d, min unanimous votes = %u\n",
                    golay_mode_arg, 7 - golay_mode_arg, ctrl.min_unanimous_votes());

    // Wire audio device: TX routing and sample-accurate completion tracking.
    ctrl.set_audio_device(audio.get());

    // ── Setup radio control (optional) ────────────────────────────────────
    std::unique_ptr<pal::IRadio> radio;
    if (!radio_spec.empty()) {
        radio = pal::create_radio(radio_spec);
        if (!radio) {
            std::fprintf(stderr,
                "ERROR: Unknown radio spec '%s'.\n"
                "       Supported format: hamlib:<model_id>:<port>\n"
                "       Examples: hamlib:229:COM3  hamlib:229:tcp://127.0.0.1:4532\n"
                "       Run 'rigctl -l' to list Hamlib model IDs.\n"
                "       (Hamlib support requires building with -DHAVE_HAMLIB)\n",
                radio_spec.c_str());
            return 1;
        }
        if (!radio->initialize() || !radio->start()) {
            std::fprintf(stderr,
                "ERROR: Cannot connect to radio '%s'.\n"
                "       Check port name, baud rate, and cable.\n",
                radio_spec.c_str());
            return 1;
        }
        ctrl.set_radio(radio.get());
        std::printf("[>>] Radio connected: %s\n", radio_spec.c_str());
        std::fflush(stdout);
    }

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

    print_banner(self_addr, call_mode, no_scan, target_addr, in_device, out_device, radio_spec);

    // ── Start ALE operation ───────────────────────────────────────────────
    if (call_mode) {
        ctrl.initiate_call(target_addr);
        std::printf("[>>] LBT %.0f ms + tune %.0f ms before first TX...\n",
                    static_cast<double>(ALETimingConstants::Twt_ms),
                    static_cast<double>(ALETimingConstants::Tt_ms));
        std::fflush(stdout);
    } else if (no_scan) {
        ctrl.start_available();
        std::printf("[>>] Available — waiting for calls addressed to %s\n\n",
                    self_addr.c_str());
        std::fflush(stdout);
    } else {
        ctrl.start_scanning();
        std::printf("[>>] Scanning — waiting for calls addressed to %s\n\n",
                    self_addr.c_str());
        std::fflush(stdout);
    }

    // ── Start stdin command reader thread ────────────────────────────────
    std::thread cmd_thread(stdin_reader);
    cmd_thread.detach();

    // ── Main event loop ───────────────────────────────────────────────────
    std::vector<int16_t> rx_buf;
    while (g_running) {
        const uint32_t t = static_cast<uint32_t>(timer->get_time_ms());

        // Drive state machine and modem
        ctrl.update(t);

        // Collect captured audio and feed to RX pipeline
        rx_buf.clear();
        audio->tick(rx_buf);
        if (!rx_buf.empty())
            ctrl.feed_audio(rx_buf.data(), static_cast<uint32_t>(rx_buf.size()));

        // Dispatch pending stdin commands
        {
            std::lock_guard<std::mutex> lk(g_cmd_mutex);
            while (!g_cmd_queue.empty()) {
                const std::string resp = ctrl.process_command(g_cmd_queue.front());
                g_cmd_queue.pop();
                if (!resp.empty()) {
                    std::printf("[CMD] %s\n", resp.c_str());
                    std::fflush(stdout);
                }
            }
        }

        timer->sleep_ms(1);
    }

    // ── Clean up ──────────────────────────────────────────────────────────
    ctrl.emergency_stop();
    if (radio) radio->stop();
    audio->close();
    std::printf("[ALE CLI] Exiting.\n");
    return 0;
}
