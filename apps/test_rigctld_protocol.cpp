/**
 * test_rigctld_protocol.cpp
 * Standalone smoke test for bridge::handle_rigctld_command() (rigctld_protocol.h).
 * Pure function, no sockets/Hamlib/ALEController — exercises every reply shape
 * the wire protocol depends on. Mirrors test_hamlib_mock.cpp's style: assert
 * via fprintf(stderr, "TEST ...") + a non-zero exit code on any failure.
 */
#include <cstdio>
#include <cstring>

#include "bridge/rigctld_protocol.h"

static int g_failures = 0;

static void expect_eq(const char* tag, const std::string& got, const std::string& want) {
    const bool ok = (got == want);
    std::fprintf(stderr, "TEST %-28s -> %s\n", tag, ok ? "OK" : "FAIL");
    if (!ok) {
        std::fprintf(stderr, "  got : %s\n  want: %s\n", got.c_str(), want.c_str());
        ++g_failures;
    }
}

static void expect_true(const char* tag, bool cond, const char* detail) {
    std::fprintf(stderr, "TEST %-28s -> %s\n", tag, cond ? "OK" : "FAIL");
    if (!cond) {
        std::fprintf(stderr, "  %s\n", detail);
        ++g_failures;
    }
}

int main() {
    using bridge::handle_rigctld_command;

    // ── chk_vfo: bare "0\n", never an RPRT line ─────────────────────────────
    expect_eq("chk_vfo", handle_rigctld_command("\\chk_vfo", true, 14000000), "0\n");

    // ── f / get_freq: happy path ─────────────────────────────────────────────
    expect_eq("f (attached)", handle_rigctld_command("f", true, 14109000), "14109000\nRPRT 0\n");
    expect_eq("get_freq (attached)", handle_rigctld_command("\\get_freq", true, 7040000), "7040000\nRPRT 0\n");

    // ── f / get_freq: no radio attached → protocol error, not 0 Hz ──────────
    const std::string no_radio = handle_rigctld_command("f", false, 0);
    expect_true("f (no radio)", no_radio.rfind("RPRT -", 0) == 0,
                ("expected a negative RPRT line, got: " + no_radio).c_str());

    // ── dump_state: non-empty, ends with RPRT 0 (load-bearing open() handshake) ──
    const std::string dump = handle_rigctld_command("\\dump_state", true, 14000000);
    expect_true("dump_state non-empty", !dump.empty(), "dump_state returned an empty string");
    expect_true("dump_state ends RPRT 0",
                dump.size() >= 7 && dump.compare(dump.size() - 7, 7, "RPRT 0\n") == 0,
                ("dump_state did not end with 'RPRT 0\\n': " + dump).c_str());
    expect_eq("dump_caps == dump_state", handle_rigctld_command("\\dump_caps", true, 14000000), dump);

    // ── open()-handshake insurance replies ───────────────────────────────────
    expect_eq("get_vfo", handle_rigctld_command("v", true, 14000000), "currVFO\nRPRT 0\n");
    expect_eq("get_split_vfo", handle_rigctld_command("s", true, 14000000), "0\nVFOA\nRPRT 0\n");

    // ── quit: no-op RPRT 0, connection stays open in this server's model ────
    expect_eq("q", handle_rigctld_command("q", true, 14000000), "RPRT 0\n");

    // ── write commands: every one must be refused (negative RPRT, never 0) ──
    const char* write_cmds[] = {
        "F 14000000", "\\set_freq 14000000",
        "M USB 2400", "\\set_mode USB 2400",
        "T 1", "\\set_ptt 1",
        "L RFPOWER 0.5", "\\set_level RFPOWER 0.5",
        "V VFOA", "\\set_vfo VFOA",
        "\\set_split_vfo 0 VFOA",
        "\\set_lock_mode 1",
    };
    for (const char* c : write_cmds) {
        const std::string r = handle_rigctld_command(c, true, 14000000);
        const bool rejected = r.rfind("RPRT -", 0) == 0;
        std::fprintf(stderr, "TEST write-rejected [%-24s] -> %s (%s)\n",
                     c, rejected ? "OK" : "FAIL", r.c_str());
        if (!rejected) ++g_failures;
    }

    // ── unknown command: fail-closed, not silently accepted ──────────────────
    {
        const std::string r = handle_rigctld_command("\\totally_unknown_cmd", true, 14000000);
        const bool rejected = r.rfind("RPRT -", 0) == 0;
        expect_true("unknown command rejected", rejected, ("got: " + r).c_str());
    }

    if (g_failures == 0) {
        std::fprintf(stderr, "\nALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d FAILURE(S)\n", g_failures);
    return 1;
}
