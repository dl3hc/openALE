/**
 * @file test_scan_dwell_tcp.cpp
 * @brief Integration guard for scan dwell timing + stop-on-detection over the REAL
 *        async netrigctl TCP path (pal::HamlibRadio, model 2) driving ALEController.
 *
 * Reproduces the two field problems reported for §A.5.3.3 scanning:
 *
 *  1) DWELL MET: with an async radio the tune completes ~settle_latency after the hop.
 *     If the dwell were anchored at tune-issue the on-channel observation window would
 *     collapse to (dwell − settle_latency) — a 200 ms dwell with ~120 ms TCP settle
 *     leaves ~80 ms to detect, so signals get hopped over. The fix anchors the dwell at
 *     the settle edge, so the SETTLED window equals the configured dwell. This test
 *     injects a real F-command response delay in an in-process rigctld mock (so the
 *     HamlibRadio worker's is_tune_settled() really goes false for that long) and
 *     asserts: (hop period − settle_latency) ≈ configured dwell, for 200 and 500 ms.
 *
 *  2) STOP ON DETECTION: while feeding real 8-FSK audio the scanner must stop
 *     (SCAN_PAUSE) and not keep hopping over the signal.
 *
 * The mock emulates just enough rigctld for HamlibRadio: dump_state, f/F, m/M, ptt/vfo.
 */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using sock_t = SOCKET;
   static constexpr sock_t INVALID_SOCK = INVALID_SOCKET;
   static void close_sock(sock_t s) { closesocket(s); }
   // <wingdi.h> (via winsock2.h) defines ERROR as a macro, which clobbers the
   // ALEState::ERROR enumerator pulled in by the ALE headers below. We don't use
   // the Windows ERROR macro, so drop it.
#  ifdef ERROR
#    undef ERROR
#  endif
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   using sock_t = int;
   static constexpr sock_t INVALID_SOCK = -1;
   static void close_sock(sock_t s) { close(s); }
#endif

#include "App/ale_controller.h"
#include "PAL/radios/hamlib_radio.h"
#include "PAL/logger.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

using namespace ale;
using namespace std::chrono;

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++g_failures; }
    else       { std::printf("  PASS: %s\n", msg); }
}

// ── in-process rigctld mock (latency-injecting) ───────────────────────────────
namespace {

steady_clock::time_point g_t0;
static uint32_t now_ms() {
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - g_t0).count());
}

struct MockState {
    std::mutex         mtx;
    double             freq_hz = 14000000.0;
    std::string        mode    = "USB";
    int                bw      = 2400;
    std::atomic<int>   f_delay_ms{0};        // injected round-trip latency on F (tune)
    std::atomic<int>   m_delay_ms{0};        // injected round-trip latency on M (mode) — models 2nd RT
    std::atomic<int>   mode_sets{0};         // count of M (set_mode) commands
    std::vector<uint32_t> f_times;           // elapsed-ms of each F (freq set) = hop issue times
};
MockState         g_state;
std::atomic<bool> g_stop{false};
sock_t            g_server = INVALID_SOCK;

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string make_dump_state() {
    return "0\n" "1\n" "1\n"
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n" "0 0 0 0 0 0 0\n"
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n" "0 0 0 0 0 0 0\n"
        "0x1ff 1\n" "0 0\n" "2400 0x1ff\n" "500 0x04\n" "0 0\n"
        "0\n" "0\n" "0\n" "0\n" "0\n" "0\n"
        "0x00000003\n" "0x00000003\n" "0x00000001\n" "0x00000001\n"
        "0\n" "0\n" "\n" "RPRT 0\n";
}

std::string handle_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) return "RPRT 0\n";
    std::string cmd = line;
    if (cmd[0] == '\\') cmd = cmd.substr(1);

    if (cmd == "q" || cmd == "Q") return "";
    if (cmd == "chk_vfo")         return "0\n";
    if (cmd == "dump_state" || cmd == "dump_caps") return make_dump_state();
    if (cmd == "get_info")        return "MockRig\nRPRT 0\n";
    if (cmd == "get_vfo" || cmd == "v")       return "currVFO\nRPRT 0\n";
    if (cmd == "get_split_vfo" || cmd == "s") return "0\nVFOA\nRPRT 0\n";
    if (cmd == "get_ptt" || cmd == "t")       return "0\nRPRT 0\n";
    if (cmd == "get_lock_mode")               return "RPRT -4\n";
    if (cmd.rfind("set_lock_mode", 0) == 0)   return "RPRT -4\n";

    if (cmd == "get_freq" || cmd == "f") {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        char buf[64]; std::snprintf(buf, sizeof(buf), "%.0f\nRPRT 0\n", g_state.freq_hz);
        return buf;
    }
    if (cmd.size() >= 2 && cmd[0] == 'F' && cmd[1] == ' ') {
        const double hz = std::atof(cmd.c_str() + 2);
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            if (hz != g_state.freq_hz) { g_state.freq_hz = hz; g_state.f_times.push_back(now_ms()); }
        }
        // Injected tune latency: the HamlibRadio worker blocks here, so its
        // is_tune_settled() stays false for f_delay_ms — exactly like a slow rig.
        const int d = g_state.f_delay_ms.load();
        if (d > 0) std::this_thread::sleep_for(milliseconds(d));
        return "RPRT 0\n";
    }
    if (cmd == "get_mode" || cmd == "m") {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        char buf[64]; std::snprintf(buf, sizeof(buf), "%s\n%d\nRPRT 0\n", g_state.mode.c_str(), g_state.bw);
        return buf;
    }
    if (cmd.size() >= 2 && cmd[0] == 'M' && cmd[1] == ' ') {
        const std::string args = trim(cmd.substr(2));
        const auto sp = args.find(' ');
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            g_state.mode = (sp == std::string::npos) ? args : args.substr(0, sp);
        }
        ++g_state.mode_sets;
        const int d = g_state.m_delay_ms.load();   // model the 2nd CAT round-trip
        if (d > 0) std::this_thread::sleep_for(milliseconds(d));
        return "RPRT 0\n";
    }
    return "RPRT 0\n";
}

void serve_client(sock_t client) {
    char buf[4096]; std::string pending;
    while (!g_stop) {
        const int n = static_cast<int>(recv(client, buf, sizeof(buf) - 1, 0));
        if (n <= 0) break;
        buf[n] = '\0'; pending += buf;
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            const std::string ln = pending.substr(0, pos + 1);
            pending = pending.substr(pos + 1);
            const std::string resp = handle_line(ln);
            if (resp.empty()) return;
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
        }
    }
}
void server_thread() {
    while (!g_stop) {
        const sock_t c = accept(g_server, nullptr, nullptr);
        if (c == INVALID_SOCK) break;
        serve_client(c);
        close_sock(c);
    }
}
int listen_on_ephemeral_port() {
    g_server = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server == INVALID_SOCK) return 0;
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return 0;
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

// Continuous-phase 8-FSK generator (random tones), amplitude 8000, ~clean (for detection).
constexpr uint32_t TONES[8] = {750,1000,1250,1500,1750,2000,2250,2500};
struct FskGen {
    double phase = 0; uint32_t sym_left = 0; double dphi = 0; uint32_t state = 0x12345;
    int16_t next() {
        if (sym_left == 0) {
            state ^= state<<13; state ^= state>>17; state ^= state<<5;
            const double f = TONES[state & 7];
            dphi = 2.0*M_PI*f/8000.0; sym_left = 64;
        }
        const double v = 8000.0*std::sin(phase);
        phase += dphi; if (phase > 2*M_PI) phase -= 2*M_PI; --sym_left;
        return static_cast<int16_t>(v);
    }
};

} // namespace

// Build a scanning controller on the real HamlibRadio (NET_RIGCTL over TCP).
static bool make_scanner(ALEController& ctrl, pal::HamlibRadio& radio, uint32_t dwell_ms)
{
    if (!radio.initialize() || !radio.start()) return false;
    ctrl.set_self_address("SAM");
    ctrl.set_radio(&radio);
    const uint32_t freqs[3] = { 7100000u, 14100000u, 21050000u };
    for (uint32_t f : freqs) { Channel ch(f); ch.enabled = true; ch.ale_only = true; ctrl.add_channel(ch); }
    ctrl.set_scan_dwell_ms(dwell_ms);
    ctrl.start_scanning();
    return true;
}

// ── Test 1: settled dwell == configured dwell across the async TCP path ────────
static void test_dwell_met(int port, uint32_t dwell_ms, uint32_t settle_ms)
{
    std::printf("\n[dwell] dwell=%ums, injected settle=%ums — settled window must == dwell\n",
                dwell_ms, settle_ms);
    g_state.f_delay_ms.store(static_cast<int>(settle_ms));
    g_state.m_delay_ms.store(static_cast<int>(settle_ms));   // M is a 2nd round-trip when sent
    g_state.mode_sets.store(0);
    { std::lock_guard<std::mutex> lk(g_state.mtx); g_state.f_times.clear(); }

    ALEController ctrl;
    pal::HamlibRadio radio("2", "tcp://127.0.0.1:" + std::to_string(port));
    check(make_scanner(ctrl, radio, dwell_ms), "scanner started on netrigctl TCP");

    // Drive ~5 hop periods of scanning with a real clock.
    const uint32_t period_est = settle_ms + dwell_ms;
    const uint32_t duration   = period_est * 6 + 400;
    const uint32_t start_ms   = now_ms();
    while (now_ms() - start_ms < duration) {
        ctrl.update(now_ms());
        std::this_thread::sleep_for(milliseconds(2));
    }

    // Analyse hop (F-command) inter-arrival times. Skip the first two (rig_open +
    // initial channel settle) so we measure steady-state hops only.
    std::vector<uint32_t> t;
    { std::lock_guard<std::mutex> lk(g_state.mtx); t = g_state.f_times; }
    check(t.size() >= 5, "several steady-state hops observed");

    double sum = 0; int n = 0; uint32_t worst = 0;
    for (size_t i = 3; i < t.size(); ++i) {
        const uint32_t period = t[i] - t[i-1];
        const uint32_t settled = period > settle_ms ? period - settle_ms : 0;
        const uint32_t err = settled > dwell_ms ? settled - dwell_ms : dwell_ms - settled;
        if (err > worst) worst = err;
        sum += settled; ++n;
    }
    const double avg_settled = n ? sum / n : 0;
    std::printf("  hops=%zu  avg settled window=%.0fms (target %ums)  worst err=%ums\n",
                t.size(), avg_settled, dwell_ms, worst);

    // The settled observation window must be the full dwell (± timing jitter), NOT
    // collapsed to (dwell − settle). Pre-fix (tune-issue anchor) avg_settled ≈
    // max(0, dwell − settle) ≈ dwell − 120 ms here; post-fix it ≈ dwell (a small
    // systematic +~30 ms from the trailing M command + 2ms tick + worker latency).
    // The tolerance is tight enough that the pre-fix collapse fails this assertion.
    const double tol = 0.15 * dwell_ms + 25.0;
    check(std::fabs(avg_settled - dwell_ms) <= tol, "settled window ≈ configured dwell");

    // 2R→1R: all scan channels share a mode, so mode is forced only on the first hop;
    // every steady-state hop must be freq-only (F every hop, M ~once).
    const int m = g_state.mode_sets.load();
    std::printf("  mode_sets over the whole scan = %d (want ≤ 2; steady-state hops are freq-only)\n", m);
    check(m <= 2, "same-mode scan hops are freq-only (1 CAT round-trip/hop)");

    radio.stop(); radio.shutdown();
}

// ── Test 2: scanning stops when 8-FSK traffic is present ──────────────────────
static void test_stop_on_detection(int port)
{
    std::printf("\n[stop] scanning must SCAN_PAUSE on 8-FSK traffic (stop hopping)\n");
    const uint32_t dwell = 300u, settle = 60u;
    g_state.f_delay_ms.store(static_cast<int>(settle));
    { std::lock_guard<std::mutex> lk(g_state.mtx); g_state.f_times.clear(); }

    ALEController ctrl;
    pal::HamlibRadio radio("2", "tcp://127.0.0.1:" + std::to_string(port));
    check(make_scanner(ctrl, radio, dwell), "scanner started on netrigctl TCP");

    auto hop_count = []{ std::lock_guard<std::mutex> lk(g_state.mtx); return g_state.f_times.size(); };

    // Phase A — silence for ~1.5s: the scanner hops every ~(settle+dwell)=360ms.
    const uint32_t A_MS = 1500u;
    const uint32_t a0 = now_ms(); const size_t hops_before = hop_count();
    while (now_ms() - a0 < A_MS) { ctrl.update(now_ms()); std::this_thread::sleep_for(milliseconds(5)); }
    const size_t hops_silence = hop_count() - hops_before;
    check(hops_silence >= 3, "scanner hops freely while silent (>= 3 in 1.5s)");

    // Phase B — feed real 8-FSK at ~8 kHz for ~1.6s. Stage-1 detection engages
    // SCAN_PAUSE and stops the hopping. (This random 8-FSK isn't decodable ALE, so
    // the pause is not refreshed by stage-2 words and releases after Tdrw≈784ms, then
    // re-detects — so ~1-2 hops over 1.6s, vs ~4 while silent. Real ALE traffic, whose
    // words refresh the pause, would hold indefinitely.)
    FskGen gen;
    const uint32_t B_MS = 1600u;
    const uint32_t b0 = now_ms(); const size_t hops_b_before = hop_count();
    uint32_t audio_clock = now_ms();
    while (now_ms() - b0 < B_MS) {
        const uint32_t t = now_ms();
        int to_feed = static_cast<int>(t - audio_clock) * 8;   // ~8 kHz real-time
        if (to_feed > 0) {
            static std::vector<int16_t> buf; buf.clear();
            for (int i = 0; i < to_feed; ++i) buf.push_back(gen.next());
            ctrl.feed_audio(buf.data(), static_cast<uint32_t>(buf.size()));
            audio_clock = t;
        }
        ctrl.update(t);
        std::this_thread::sleep_for(milliseconds(3));
    }
    const size_t hops_signal = hop_count() - hops_b_before;
    std::printf("  hops(silent %ums)=%zu   hops(8-FSK %ums)=%zu\n",
                A_MS, hops_silence, B_MS, hops_signal);

    // Scanning must effectively stop: far fewer hops with traffic present, and no more
    // than the Tdrw-timeout re-detections (≤ 2 over 1.6s) can leak through.
    check(hops_signal <= 2, "8-FSK traffic stops the scan (≤ 2 hops in 1.6s)");
    check(hops_signal * A_MS < hops_silence * B_MS,
          "hop RATE with traffic is well below the silent scan rate");

    radio.stop(); radio.shutdown();
}

int main()
{
    pal::set_logger(pal::create_logger());
    g_t0 = steady_clock::now();

#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
#endif
    std::printf("==============================================================\n");
    std::printf("  test_scan_dwell_tcp — scan dwell + stop-on-detection (netrigctl)\n");
    std::printf("==============================================================\n");

    const int port = listen_on_ephemeral_port();
    if (!port) { std::fprintf(stderr, "FAIL: could not open mock rigctld port\n"); return 1; }
    std::thread srv(server_thread);

    test_dwell_met(port, 200u, 120u);
    test_dwell_met(port, 500u, 120u);
    test_stop_on_detection(port);

    g_stop = true;
    close_sock(g_server);
    srv.join();
#ifdef _WIN32
    WSACleanup();
#endif

    if (g_failures == 0) { std::printf("\nAll scan-dwell TCP tests passed.\n"); return 0; }
    std::fprintf(stderr, "\n%d test(s) FAILED.\n", g_failures);
    return 1;
}
