/**
 * @file test_mode_reassert.cpp
 * @brief Regression guard: mode authority + scan-hot-path CAT-op count.
 *
 * Two things this test pins down:
 *
 * 1) Mode re-assertion must survive hamlib's lock-probe UB. hamlib's
 *    rig_set_mode() begins with an UNINITIALIZED `int locked_mode`, fills it
 *    via rig_get_lock_mode() (return code ignored) and silently returns
 *    RIG_OK WITHOUT transmitting when it is nonzero. Over netrigctl the
 *    \get_lock_mode transaction fails against servers that don't implement it
 *    (Quisk replies "RPRT -4"; sscanf on that buffer writes nothing), so
 *    whether ANY mode command reaches the radio depends on stack garbage
 *    (hamlib 4.5 rig.c:2218, still in upstream master). HamlibRadio::start()
 *    neutralizes the probe by nulling rig->caps->get_lock_mode, making
 *    rig_get_lock_mode() fall back to the zero-initialized rig->state.lock_mode.
 *
 * 2) The scan hot path must take the synchronous mode readback OFF the hot
 *    path. assert_mode() used to do a 3-pass rig_get_mode/rig_set_mode loop
 *    (1-3 extra "m" reads per hop); over netrigctl (~80-150 ms/rt) that blew
 *    the 200 ms dwell to 500-1000 ms. It now sends exactly ONE rig_set_mode
 *    and returns — the deferred background verify (sync_from_radio) catches
 *    async reverts. This test counts CAT ops per hop to lock that in.
 *
 * It drives the REAL pal::HamlibRadio (NET_RIGCTL, model 2) against an
 * in-process rigctld-protocol server that emulates Quisk:
 *   - "\get_lock_mode" -> "RPRT -4" (counted)
 *   - a frequency change schedules an ASYNC band-memory mode revert to LSB
 *     ~80 ms later (Quisk's BandFromFreq behaviour)
 * and asserts:
 *   A) a hop emits exactly one F + one M and ZERO per-hop mode readbacks;
 *   B) the intended USB mode converges despite the async revert (the
 *      sync_from_radio() background verify re-assert actually reaches the wire); and
 *   C) the client never sent "\get_lock_mode" — the deterministic detector:
 *      pre-fix this fails regardless of what the stack garbage happens to be.
 */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using sock_t = SOCKET;
   static constexpr sock_t INVALID = INVALID_SOCKET;
   static void close_sock(sock_t s) { closesocket(s); }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   using sock_t = int;
   static constexpr sock_t INVALID = -1;
   static void close_sock(sock_t s) { close(s); }
#endif

#include "PAL/radios/hamlib_radio.h"
#include "PAL/radio.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono;

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++g_failures; }
    else       { std::printf("  PASS: %s\n", msg); }
}

// ── Quisk-emulating rigctld server (in-process) ───────────────────────────────

namespace {

struct MockState {
    std::mutex  mtx;
    double      freq_hz = 14000000.0;
    std::string mode    = "USB";
    int         bw      = 2400;

    // Quisk band-memory emulation: a frequency change schedules a one-shot
    // asynchronous mode revert (BandFromFreq restores the band's saved mode).
    // Applied lazily before each command once the deadline has passed — the
    // client polls continuously, so laziness is equivalent to a timer.
    bool                              revert_pending = false;
    steady_clock::time_point          revert_at{};
    std::string                       revert_mode = "LSB";
    std::atomic<bool>                 revert_fired{false};

    std::atomic<int> lock_probes{0};   // "\get_lock_mode" received
    std::atomic<int> mode_sets{0};     // "M <mode> <bw>" received
    std::atomic<int> mode_reads{0};    // "m"/"get_mode" received (per-hop readback detector)
    std::atomic<int> freq_sets{0};     // "F <hz>" received
    std::atomic<int> freq_reads{0};    // "f"/"get_freq" received
};

MockState        g_state;
std::atomic<bool> g_stop{false};
sock_t           g_server = INVALID;

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// Minimal rigctld dump_state (protocol version 0) — same shape as radio_mock.
std::string make_dump_state()
{
    return
        "0\n" "1\n" "1\n"
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        "0x1ff 1\n" "0 0\n"
        "2400 0x1ff\n" "500 0x04\n" "0 0\n"
        "0\n" "0\n" "0\n" "0\n" "0\n" "0\n"
        "0x00000003\n" "0x00000003\n"
        "0x00000001\n" "0x00000001\n"
        "0\n" "0\n" "\n" "RPRT 0\n";
}

void apply_due_revert()
{
    std::lock_guard<std::mutex> lk(g_state.mtx);
    if (g_state.revert_pending && steady_clock::now() >= g_state.revert_at) {
        g_state.mode = g_state.revert_mode;
        g_state.revert_pending = false;
        g_state.revert_fired = true;
        std::printf("  [mock] band-memory revert fired -> %s\n", g_state.mode.c_str());
    }
}

// Returns "" to signal connection close (quit command).
std::string handle_line(const std::string& raw)
{
    std::string line = trim(raw);
    if (line.empty()) return "RPRT 0\n";

    std::string cmd = line;
    if (cmd[0] == '\\') cmd = cmd.substr(1);

    apply_due_revert();

    if (cmd == "q" || cmd == "Q") return "";
    if (cmd == "chk_vfo")         return "0\n";
    if (cmd == "dump_state" || cmd == "dump_caps") return make_dump_state();
    if (cmd == "get_info")        return "MockQuisk\nRPRT 0\n";
    if (cmd == "get_vfo" || cmd == "v")       return "currVFO\nRPRT 0\n";
    if (cmd == "get_split_vfo" || cmd == "s") return "0\nVFOA\nRPRT 0\n";
    if (cmd == "get_ptt" || cmd == "t")       return "0\nRPRT 0\n";

    // QUISK behaviour: get/set_lock_mode unimplemented -> "RPRT -4".
    if (cmd == "get_lock_mode") {
        ++g_state.lock_probes;
        return "RPRT -4\n";
    }
    if (cmd.rfind("set_lock_mode", 0) == 0) return "RPRT -4\n";

    if (cmd == "get_freq" || cmd == "f") {
        ++g_state.freq_reads;
        std::lock_guard<std::mutex> lk(g_state.mtx);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f\nRPRT 0\n", g_state.freq_hz);
        return buf;
    }
    if (cmd.size() >= 2 && cmd[0] == 'F' && cmd[1] == ' ') {
        const double hz = std::atof(cmd.c_str() + 2);
        std::lock_guard<std::mutex> lk(g_state.mtx);
        if (hz != g_state.freq_hz) {
            g_state.freq_hz = hz;
            g_state.revert_pending = true;
            g_state.revert_at = steady_clock::now() + milliseconds(80);
        }
        ++g_state.freq_sets;
        return "RPRT 0\n";
    }
    if (cmd == "get_mode" || cmd == "m") {
        ++g_state.mode_reads;
        std::lock_guard<std::mutex> lk(g_state.mtx);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s\n%d\nRPRT 0\n",
                      g_state.mode.c_str(), g_state.bw);
        return buf;
    }
    if (cmd.size() >= 2 && cmd[0] == 'M' && cmd[1] == ' ') {
        const std::string args = trim(cmd.substr(2));
        const auto sp = args.find(' ');
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.mode = (sp == std::string::npos) ? args : args.substr(0, sp);
        ++g_state.mode_sets;
        return "RPRT 0\n";
    }

    return "RPRT 0\n";  // catch-all (V, T, levels, ...)
}

void serve_client(sock_t client)
{
    char buf[4096];
    std::string pending;
    while (!g_stop) {
        const int n = static_cast<int>(recv(client, buf, sizeof(buf) - 1, 0));
        if (n <= 0) break;
        buf[n] = '\0';
        pending += buf;
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, pos + 1);
            pending = pending.substr(pos + 1);
            const std::string resp = handle_line(line);
            if (resp.empty()) return;  // quit
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
        }
    }
}

void server_thread()
{
    while (!g_stop) {
        const sock_t client = accept(g_server, nullptr, nullptr);
        if (client == INVALID) break;   // listen socket closed -> shut down
        serve_client(client);
        close_sock(client);
    }
}

int listen_on_ephemeral_port()
{
    g_server = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server == INVALID) return 0;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = 0;                    // ephemeral
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return 0;
    listen(g_server, 1);

    sockaddr_in bound{};
#ifdef _WIN32
    int len = sizeof(bound);
#else
    socklen_t len = sizeof(bound);
#endif
    getsockname(g_server, reinterpret_cast<sockaddr*>(&bound), &len);
    return ntohs(bound.sin_port);
}

} // namespace

// ── Test ──────────────────────────────────────────────────────────────────────

static void test_mode_survives_async_band_restore(int port)
{
    std::printf("\n[mode re-assert] USB must converge despite async LSB band-restore\n");

    pal::HamlibRadio radio("2", "tcp://127.0.0.1:" + std::to_string(port));
    check(radio.initialize(), "HamlibRadio::initialize()");
    check(radio.start(),      "HamlibRadio::start() (rig_open over TCP)");

    // rig_open()'s cache-init handshake issues its own m/f reads; reset the
    // counters so the post-hop counts reflect ONLY the set_channel hot path.
    g_state.mode_sets.store(0);
    g_state.mode_reads.store(0);
    g_state.freq_sets.store(0);
    g_state.freq_reads.store(0);
    g_state.lock_probes.store(0);

    pal::Channel ch;
    ch.tx_frequency = ch.rx_frequency = 7050000;   // freq change -> schedules revert
    ch.tx_mode      = ch.rx_mode      = pal::RadioMode::USB;
    radio.set_channel(ch);
    radio.flush();  // wait for async worker to complete impl_set_channel()

    // ── Per-hop CAT-op count: the scan hot path must be exactly ONE F + ONE M
    //    with ZERO synchronous mode readbacks. Pre-fix, assert_mode() did a
    //    3-pass rig_get_mode/rig_set_mode loop — 1-3 extra "m" reads per hop —
    //    which over netrigctl (~80-150 ms/rt) blew the 200 ms dwell to 500-
    //    1000 ms. With the readback loop removed, a hop is freq-set + mode-
    //    force = 2 round-trips, no readback.
    check(g_state.freq_sets.load() == 1, "one F (freq set) per hop");
    check(g_state.mode_sets.load() == 1, "one M (mode force) per hop");
    check(g_state.mode_reads.load() == 0,
          "ZERO per-hop mode readbacks (synchronous readback loop removed from hot path)");
    check(g_state.lock_probes.load() == 0,
          "no \\get_lock_mode on the hop (lock-probe neutralized in start())");
    check(g_state.mode_sets.load() > 0, "at least one M command reached the wire");

    // Emulates ALEController::tick_mode_verify: deferred sync_from_radio()
    // polls after the channel command. Converged = the one-shot revert fired
    // AND the server ended up back on the intended USB.
    bool converged = false;
    for (int i = 0; i < 30 && !converged; ++i) {
        std::this_thread::sleep_for(milliseconds(100));
        radio.sync_from_radio();
        radio.flush();  // wait for async worker to complete impl_sync_from_radio()
        std::lock_guard<std::mutex> lk(g_state.mtx);
        converged = g_state.revert_fired && g_state.mode == "USB";
    }
    check(g_state.revert_fired, "emulated band-memory revert fired");
    check(converged, "mode converged back to USB after async revert (backstop re-assert transmitted)");

    // Deterministic bug detector: with caps->get_lock_mode neutralized in
    // start(), hamlib must never probe the lock state. Pre-fix, every
    // rig_set_mode sends \get_lock_mode — this fails regardless of whether
    // the stack garbage happened to elide the mode command.
    check(g_state.lock_probes == 0,
          "client never sent \\get_lock_mode (lock-probe neutralized in start())");

    radio.stop();
    radio.shutdown();
}

int main()
{
    std::printf("==============================================================\n");
    std::printf("  test_mode_reassert — hamlib lock-probe UB / Quisk band-restore\n");
    std::printf("==============================================================\n");

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    const int port = listen_on_ephemeral_port();
    if (port == 0) {
        std::fprintf(stderr, "FAIL: could not bind loopback listen socket\n");
        return 1;
    }
    std::printf("  [mock] Quisk-emulating rigctld server on 127.0.0.1:%d\n", port);
    std::thread server(server_thread);

    test_mode_survives_async_band_restore(port);

    g_stop = true;
    close_sock(g_server);   // unblocks accept()
    server.join();
#ifdef _WIN32
    WSACleanup();
#endif

    if (g_failures == 0) { std::printf("\nPASS  all mode re-assert tests\n"); return 0; }
    std::fprintf(stderr, "\n%d failure(s)\n", g_failures);
    return 1;
}
