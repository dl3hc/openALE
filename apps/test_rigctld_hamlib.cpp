/**
 * test_rigctld_hamlib.cpp
 * Real Hamlib RIG_MODEL_NETRIGCTL client against an in-process
 * bridge::RigctldServer — verifies the client-visible contract ck-netctrl
 * actually depends on (Hamlib.Rig / RIG_MODEL_NETRIGCTL), not just raw wire
 * text. Mirrors test_hamlib_mock.cpp's style: fprintf(stderr, "TEST ...")
 * plus a non-zero exit code on any failure.
 *
 * The server's I/O thread only ever ships/receives text lines (see
 * rigctld_server.h) — protocol interpretation normally happens on openALE's
 * main thread. Here a small background "drain" thread plays that role so a
 * real, blocking Hamlib client can run on main() without deadlocking against
 * its own request.
 */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <hamlib/rig.h>

#include "bridge/rigctld_protocol.h"
#include "bridge/rigctld_server.h"

namespace {

std::atomic<bool>     g_attached{true};
std::atomic<uint32_t> g_freq_hz{14109000};
std::atomic<bool>     g_run_drain{true};
int                   g_failures = 0;

void drain_loop(bridge::RigctldServer* srv) {
    while (g_run_drain.load()) {
        uint64_t conn_id;
        std::string line;
        bool any = false;
        while (srv->pop_request(conn_id, line)) {
            any = true;
            const bool attached = g_attached.load();
            const uint32_t freq = attached ? g_freq_hz.load() : 0;
            srv->send_reply(conn_id, bridge::handle_rigctld_command(line, attached, freq));
        }
        if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void check(const char* tag, bool cond, const char* detail = nullptr) {
    std::fprintf(stderr, "TEST %-34s -> %s\n", tag, cond ? "OK" : "FAIL");
    if (!cond) {
        if (detail) std::fprintf(stderr, "  %s\n", detail);
        ++g_failures;
    }
}

RIG* open_client(const std::string& host) {
    RIG* rig = rig_init(2);  // 2 = NET_RIGCTL / RIG_MODEL_NETRIGCTL
    if (!rig) return nullptr;
    rig->state.rigport.type.rig = RIG_PORT_NETWORK;
#ifdef _MSC_VER
    strncpy_s(rig->state.rigport.pathname, HAMLIB_FILPATHLEN, host.c_str(), HAMLIB_FILPATHLEN - 1);
#else
    std::strncpy(rig->state.rigport.pathname, host.c_str(), HAMLIB_FILPATHLEN - 1);
#endif
    return rig;
}

} // namespace

int main() {
    rig_load_all_backends();

    const uint16_t port = 18780;
    bridge::RigctldServer srv;
    if (!srv.start(port, /*bind_remote=*/false)) {
        std::fprintf(stderr, "FATAL: RigctldServer failed to bind port %u\n", port);
        return 1;
    }
    std::thread drain(drain_loop, &srv);
    const std::string host = "127.0.0.1:" + std::to_string(port);

    // ── rig_open() succeeds — the \dump_state handshake regression guard ────
    // (if this fails, the dump_state blob or one of chk_vfo/get_vfo/
    // get_split_vfo is malformed and NOTHING else in this feature works)
    RIG* rig1 = open_client(host);
    check("rig_init (client 1)", rig1 != nullptr);
    int ret = rig_open(rig1);
    check("rig_open (client 1)", ret == RIG_OK, rigerror(ret));
    // Disable Hamlib's own client-side freq cache so every rig_get_freq()
    // below actually hits the wire (mirrors test_hamlib_mock.cpp) — this
    // test is about our server's behavior per request, not Hamlib's cache.
    rig_set_cache_timeout_ms(rig1, HAMLIB_CACHE_ALL, 0);

    // ── rig_get_freq(): live value, then a live change is visible ──────────
    freq_t f = 0;
    ret = rig_get_freq(rig1, RIG_VFO_CURR, &f);
    check("rig_get_freq (attached)",
          ret == RIG_OK && static_cast<uint32_t>(f) == g_freq_hz.load(), rigerror(ret));

    g_freq_hz = 7040000;
    ret = rig_get_freq(rig1, RIG_VFO_CURR, &f);
    check("rig_get_freq (freq change visible)",
          ret == RIG_OK && static_cast<uint32_t>(f) == 7040000, rigerror(ret));

    // ── rig_set_freq(): must be rejected through the client API, not just wire ──
    ret = rig_set_freq(rig1, RIG_VFO_CURR, 14000000.0);
    check("rig_set_freq rejected (read-only)", ret != RIG_OK);

    // ── multiple simultaneous clients ────────────────────────────────────────
    RIG* rig2 = open_client(host);
    check("rig_init (client 2)", rig2 != nullptr);
    ret = rig_open(rig2);
    check("rig_open (client 2)", ret == RIG_OK, rigerror(ret));
    rig_set_cache_timeout_ms(rig2, HAMLIB_CACHE_ALL, 0);

    freq_t f2 = 0;
    ret = rig_get_freq(rig2, RIG_VFO_CURR, &f2);
    check("rig_get_freq (client 2)",
          ret == RIG_OK && static_cast<uint32_t>(f2) == 7040000, rigerror(ret));
    check("both clients tracked", srv.client_count() >= 2);

    ret = rig_get_freq(rig1, RIG_VFO_CURR, &f);
    check("rig_get_freq (client 1 unaffected by client 2)",
          ret == RIG_OK && static_cast<uint32_t>(f) == 7040000, rigerror(ret));

    rig_close(rig2);
    rig_cleanup(rig2);

    // ── no radio attached → clean client-side error, not a hang/crash ───────
    g_attached = false;
    ret = rig_get_freq(rig1, RIG_VFO_CURR, &f);
    check("rig_get_freq (no radio) errors cleanly", ret != RIG_OK);
    g_attached = true;

    rig_close(rig1);
    rig_cleanup(rig1);

    // ── clean shutdown must not hang ─────────────────────────────────────────
    g_run_drain = false;
    drain.join();
    srv.stop();
    check("server stopped cleanly", !srv.is_running());

    if (g_failures == 0) {
        std::fprintf(stderr, "\nALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d FAILURE(S)\n", g_failures);
    return 1;
}
