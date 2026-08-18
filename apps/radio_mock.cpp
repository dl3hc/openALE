/**
 * \file apps/radio_mock.cpp
 * \brief Mock Radio Server — rigctld-kompatibles Test-TRX
 *
 * Startet einen TCP-Listener und implementiert das Hamlib rigctld-
 * Netzwerkprotokoll (NET_RIGCTL, model 2).  PTT- und Frequenzänderungen
 * werden im Terminal als lesbares Ereignis ausgegeben.
 *
 * Verwendung:
 *   radio_mock.exe --port 8766
 *   radio_mock.exe --port 8766 --verbose    # zeigt jeden Rohbefehl
 *
 * openALE verbinden (GUI: Radio → CAT → hamlib:2:tcp://127.0.0.1:8766):
 *   openALE --radio hamlib:2:tcp://127.0.0.1:8766 ...
 *
 * Implementiertes rigctld-Protokoll (Hamlib 4.x, rigs/dummy/netrigctl.c):
 *
 *   \chk_vfo            → "0\n"         (kein VFO-Präfix, simple mode)
 *   \dump_state         → Capabilities-Dump
 *   \get_info           → Versionsstring + RPRT 0
 *   F <hz>              → setzt Frequenz   (kein VFO-Präfix, rigctld_vfo_mode=0)
 *   F <vfo> <hz>        → setzt Frequenz   (mit VFO-Präfix, rigctld_vfo_mode=1)
 *   f / \get_freq       → liefert Frequenz + RPRT 0
 *   M <mode> <bw>       → setzt Modus      (kein VFO-Präfix)
 *   M <vfo> <mode> <bw> → setzt Modus      (mit VFO-Präfix)
 *   m / \get_mode       → liefert Modus + Bandbreite + RPRT 0
 *   v / \get_vfo        → liefert VFO-Name ("currVFO") + RPRT 0
 *   V <name>            → setzt VFO (keine Aktion, RPRT 0)
 *   s / \get_split_vfo  → liefert Split-Status (0 + VFOA) + RPRT 0
 *   T <0|1> / \set_ptt  → schaltet PTT
 *   t / \get_ptt        → liefert PTT-Status
 *   L RFPOWER <0..1> / \set_level → setzt TX-Leistung (Bruchteil der Maximalleistung)
 *   l RFPOWER / \get_level        → liefert TX-Leistung + RPRT 0
 *   \get_lock_mode      → "RPRT -4" (Default, QUISK-artig) bzw. "0" + RPRT 0
 *                         mit --lock-mode ok (rigctld-artig)
 *   Q / q               → Verbindung beenden
 *   alles andere        → RPRT 0  (ignoriert, kein Fehler)
 *
 * PROTOKOLL-DETAILS (aus Hamlib rigs/dummy/netrigctl.c):
 *
 *   \chk_vfo muss "0\n" (oder "1\n") zurückgeben — KEINE RPRT-Zeile.
 *   hamlib liest genau eine Zeile und wertet sie mit atoi() aus.
 *   → 0 = simple mode (kein VFO-Präfix in nachfolgenden Befehlen)
 *   → 1 = VFO mode   (hamlib schickt "F currVFO 14000000\n" etc.)
 *
 *   Wenn der Mock "RPRT -1\n" zurückgibt, liest hamlib "RPRT -1" und
 *   wertet atoi("RPRT -1") = 0 aus — zufällig korrekt, aber falsch.
 *   Deshalb korrekt: "0\n" zurückgeben.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

// ── Global options ────────────────────────────────────────────────────────────

static bool g_verbose = false;   // --verbose: prints every raw command

// \get_lock_mode reply. Default mimics QUISK ("RPRT -4" = unimplemented),
// which leaves hamlib's uninitialized lock variable UNWRITTEN and lets
// rig_set_mode() elide mode commands based on stack garbage (hamlib bug).
// --lock-mode ok mimics real rigctld ("0\n" data line = unlocked).
static bool g_lock_mode_ok = false;

// ── Radio state ───────────────────────────────────────────────────────────────

static double      s_freq_hz = 14'000'000.0;
static std::string s_mode    = "USB";
static int         s_bw      = 2400;
static int         s_ptt     = 0;   // 0 = RX, 1 = TX
static double      s_power   = 1.0; // RIG_LEVEL_RFPOWER, fraction of max [0.0 .. 1.0]

// ── Logging ──────────────────────────────────────────────────────────────────

static void log_event(const char* msg)
{
    std::printf("%s\n", msg);
    std::fflush(stdout);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// Returns true if 'tok' looks like a hamlib VFO name (currVFO, VFOA, Main…)
// rather than a mode name (USB, LSB, AM…).  Used to skip optional VFO prefix.
static bool is_vfo_token(const std::string& tok)
{
    // Mode names start with a letter and are short alphabetic strings,
    // but VFO names are a closed set.  We check the known VFO identifiers
    // that hamlib emits when rigctld_vfo_mode=1 (rig_strvfo output).
    static const char* const vfos[] = {
        "currVFO", "VFOA", "VFOB", "VFO", "Main", "Sub",
        "TX",      "RX",   "MEM",  "VFO_A", "VFO_B", nullptr
    };
    for (int i = 0; vfos[i]; ++i)
        if (tok == vfos[i]) return true;
    return false;
}

// Minimal rigctld dump_state response.
// Format: rigs/dummy/netrigctl.c, protocol version 0 (Hamlib ≤4.5).
static std::string make_dump_state()
{
    return
        "0\n"                                            // protocol version
        "1\n"                                            // rig model
        "1\n"                                            // ITU region
        // RX freq ranges: 100 kHz – 30 MHz, all modes (0x1ff)
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        // TX freq ranges
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        // Tuning steps
        "0x1ff 1\n"
        "0 0\n"
        // Filters
        "2400 0x1ff\n"
        "500 0x04\n"
        "0 0\n"
        "0\n"              // max_rit
        "0\n"              // max_xit
        "0\n"              // max_ifshift
        "0\n"              // announces
        "0\n"              // preamp list end
        "0\n"              // attenuator list end
        "0x00000003\n"     // has_get_func
        "0x00000003\n"     // has_set_func
        "0x00001001\n"     // has_get_level (PREAMP | RFPOWER)
        "0x00001001\n"     // has_set_level (PREAMP | RFPOWER)
        "0\n"              // has_get_parm
        "0\n"              // has_set_parm
        "\n"
        "RPRT 0\n";
}

// ── Command handler ───────────────────────────────────────────────────────────
//
// Returns "" to signal "close connection" (quit command).
// Returns the response string otherwise.

static std::string handle_line(const std::string& raw)
{
    std::string line = trim(raw);
    if (line.empty()) return "RPRT 0\n";

    if (g_verbose) {
        std::printf("[RAW] %s\n", line.c_str());
        std::fflush(stdout);
    }

    // Strip optional leading '+' (extended response protocol) — we don't
    // implement extended mode, just parse the command name that follows.
    bool extended = (!line.empty() && (line[0] == '+' || line[0] == ';' || line[0] == '|'));
    if (extended) line = trim(line.substr(1));

    // Long-form commands start with backslash: "\set_freq 14000000"
    std::string cmd = line;
    if (!cmd.empty() && cmd[0] == '\\') cmd = cmd.substr(1);

    // ── Quit ─────────────────────────────────────────────────────────────
    if (cmd == "q" || cmd == "Q") return "";  // signal close

    // ── Protocol handshake ────────────────────────────────────────────────

    // chk_vfo: return integer 0 (simple mode, no VFO prefix needed).
    // hamlib reads exactly one line and calls atoi() on it.
    // "0\n" → rigctld_vfo_mode=0  → hamlib sends "F <hz>" / "M <mode> <bw>"
    // "1\n" → rigctld_vfo_mode=1  → hamlib sends "F <vfo> <hz>" / "M <vfo> <mode> <bw>"
    // Do NOT return "RPRT -1\n" here: atoi("RPRT -1") happens to give 0 too,
    // but some hamlib versions treat a RPRT response as a failure and may leave
    // rigctld_vfo_mode at its initialised value (which could be 1).
    if (cmd == "chk_vfo")
        return "0\n";

    if (cmd == "dump_state" || cmd == "dump_caps")
        return make_dump_state();

    if (cmd == "get_info")
        return "Mock Radio v1.0\nRPRT 0\n";

    // ── Frequency ─────────────────────────────────────────────────────────
    // Simple:   "F <hz>"        (rigctld_vfo_mode=0)
    // VFO mode: "F <vfo> <hz>"  (rigctld_vfo_mode=1)
    if (cmd.rfind("set_freq", 0) == 0 ||
        (cmd.size() >= 2 && cmd[0] == 'F' && cmd[1] == ' '))
    {
        const std::string args = trim(cmd.substr(cmd.find(' ') + 1));
        std::istringstream ss(args);
        std::string tok1, tok2;
        ss >> tok1;
        if (is_vfo_token(tok1)) {
            // VFO-prefixed: skip VFO name, read actual frequency
            ss >> tok2;
            std::printf("[WARN] hamlib sent VFO-prefixed freq command (rigctld_vfo_mode=1!): %s\n",
                        line.c_str());
        } else {
            tok2 = tok1;  // tok1 is the frequency
        }
        s_freq_hz = std::stod(tok2);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "[TRX] Freq  %.3f kHz", s_freq_hz / 1000.0);
        log_event(buf);
        return "RPRT 0\n";
    }
    if (cmd == "get_freq" || cmd == "f") {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f\nRPRT 0\n", s_freq_hz);
        return buf;
    }

    // ── Mode ──────────────────────────────────────────────────────────────
    // Simple:   "M <mode> <bw>"        (rigctld_vfo_mode=0)
    // VFO mode: "M <vfo> <mode> <bw>"  (rigctld_vfo_mode=1)
    if (cmd.rfind("set_mode", 0) == 0 ||
        (cmd.size() >= 2 && cmd[0] == 'M' && cmd[1] == ' '))
    {
        const std::string args = trim(cmd.substr(cmd.find(' ') + 1));
        std::istringstream ss(args);
        std::string tok1, tok2, tok3;
        ss >> tok1 >> tok2 >> tok3;

        if (is_vfo_token(tok1)) {
            // "M <vfo> <mode> <bw>"
            std::printf("[WARN] hamlib sent VFO-prefixed mode command (rigctld_vfo_mode=1!): %s\n",
                        line.c_str());
            s_mode = tok2;
            s_bw   = tok3.empty() ? 0 : std::atoi(tok3.c_str());
        } else {
            // "M <mode> <bw>"
            s_mode = tok1;
            s_bw   = tok2.empty() ? 0 : std::atoi(tok2.c_str());
        }

        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "[TRX] Mode  %-8s BW=%-5d  [raw: %s]",
                      s_mode.c_str(), s_bw, line.c_str());
        log_event(buf);
        return "RPRT 0\n";
    }
    if (cmd == "get_mode" || cmd == "m") {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s\n%d\nRPRT 0\n", s_mode.c_str(), s_bw);
        return buf;
    }

    // ── VFO ───────────────────────────────────────────────────────────────
    // hamlib NET_RIGCTL sends "v" (get_vfo) during rig_open() cache-init.
    // Returning just "RPRT 0\n" (no VFO name) causes rig_parse_vfo("") to
    // return RIG_VFO_NONE; strict hamlib builds treat that as an error and
    // rig_open() fails → start() returns false → GUI stays radio-ctrl-locked
    // → every VFO_SET_MODE is silently dropped before reaching the bridge.
    if (cmd == "get_vfo" || cmd == "v") {
        return "currVFO\nRPRT 0\n";
    }
    // "V <name>" (set_vfo) — explicit handler so --verbose can log it.
    if (cmd.size() >= 2 && cmd[0] == 'V' && cmd[1] == ' ') {
        if (g_verbose) {
            std::printf("[VFO] set → %s\n", trim(cmd.substr(2)).c_str());
            std::fflush(stdout);
        }
        return "RPRT 0\n";
    }

    // ── Split ─────────────────────────────────────────────────────────────
    // hamlib NET_RIGCTL sends "s" (get_split_vfo) during rig_open() cache-init.
    // Response format: <split_int>\n<split_vfo_name>\nRPRT 0\n
    if (cmd == "get_split_vfo" || cmd == "s") {
        return "0\nVFOA\nRPRT 0\n";
    }

    // ── Lock mode ─────────────────────────────────────────────────────────
    // hamlib sends "\get_lock_mode" before EVERY rig_set_mode. The reply shape
    // decides whether hamlib's (uninitialized) lock variable gets written:
    //   "0\nRPRT 0\n" (real rigctld)  → lock=0 parsed, set_mode proceeds
    //   "RPRT -4\n"   (QUISK)         → sscanf matches nothing, lock stays
    //                                    stack garbage → set_mode may be elided
    if (cmd == "get_lock_mode") {
        if (g_verbose) {
            std::printf("[LOCK] get_lock_mode -> %s\n",
                        g_lock_mode_ok ? "0 (rigctld-style)" : "RPRT -4 (QUISK-style)");
            std::fflush(stdout);
        }
        return g_lock_mode_ok ? "0\nRPRT 0\n" : "RPRT -4\n";
    }
    if (cmd.rfind("set_lock_mode", 0) == 0)
        return g_lock_mode_ok ? "RPRT 0\n" : "RPRT -4\n";

    // ── PTT ───────────────────────────────────────────────────────────────
    // Short: "T 1"   Long: "set_ptt 1"
    if (cmd.rfind("set_ptt", 0) == 0 ||
        (cmd.size() >= 2 && cmd[0] == 'T' && cmd[1] == ' '))
    {
        const std::string arg = trim(cmd.substr(cmd.find(' ') + 1));
        s_ptt = std::atoi(arg.c_str());
        log_event(s_ptt ? "[TRX] PTT ON  ←── TX" : "[TRX] PTT OFF ──► RX");
        return "RPRT 0\n";
    }
    if (cmd == "get_ptt" || cmd == "t") {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d\nRPRT 0\n", s_ptt);
        return buf;
    }

    // ── Levels (RFPOWER only) ────────────────────────────────────────────
    // Simple:   "L <level> <val>"  /  "l <level>"
    // VFO mode: "L <vfo> <level> <val>"  /  "l <vfo> <level>" (tolerated like F/M,
    // though chk_vfo="0" above means hamlib never actually sends the VFO form).
    if (cmd.rfind("set_level", 0) == 0 ||
        (cmd.size() >= 2 && cmd[0] == 'L' && cmd[1] == ' '))
    {
        const std::string args = trim(cmd.substr(cmd.find(' ') + 1));
        std::istringstream ss(args);
        std::string tok1, tok2, tok3;
        ss >> tok1;
        std::string level_name, val_str;
        if (is_vfo_token(tok1)) { ss >> tok2 >> tok3; level_name = tok2; val_str = tok3; }
        else                    { ss >> tok2;         level_name = tok1; val_str = tok2; }

        if (level_name == "RFPOWER") {
            s_power = std::stod(val_str);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[TRX] Power %.0f%%", s_power * 100.0);
            log_event(buf);
        } else if (g_verbose) {
            std::printf("[IGN] set_level %s (unhandled)\n", level_name.c_str());
            std::fflush(stdout);
        }
        return "RPRT 0\n";
    }
    if (cmd.rfind("get_level", 0) == 0 ||
        (cmd.size() >= 2 && cmd[0] == 'l' && cmd[1] == ' '))
    {
        const std::string args = trim(cmd.substr(cmd.find(' ') + 1));
        std::istringstream ss(args);
        std::string tok1, tok2;
        ss >> tok1;
        std::string level_name = tok1;
        if (is_vfo_token(tok1)) { ss >> tok2; level_name = tok2; }

        if (level_name == "RFPOWER") {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f\nRPRT 0\n", s_power);
            return buf;
        }
        return "RPRT 0\n";  // unhandled level, benign — same as the general catch-all
    }

    // ── Catch-all ─────────────────────────────────────────────────────────
    if (g_verbose) {
        std::printf("[IGN] %s\n", line.c_str());
        std::fflush(stdout);
    }
    return "RPRT 0\n";
}

// ── Connection handler ────────────────────────────────────────────────────────

static void serve(sock_t client)
{
    char buf[4096];
    std::string pending;

    log_event("[MOCK] Client verbunden");

    while (true) {
#ifdef _WIN32
        const int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n == SOCKET_ERROR || n == 0) break;
#else
        const ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
#endif
        buf[n] = '\0';
        pending += buf;

        // Process all complete lines
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, pos + 1);
            pending = pending.substr(pos + 1);

            const std::string resp = handle_line(line);
            if (resp.empty()) {
                // Quit command — close connection
                goto done;
            }
#ifdef _WIN32
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
#else
            send(client, resp.c_str(), resp.size(), 0);
#endif
        }
    }
done:
    log_event("[MOCK] Client getrennt\n");
}

// ── Main ─────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog)
{
    std::fprintf(stderr,
        "Mock Radio — rigctld-kompatibler Test-TRX\n"
        "\n"
        "Usage:\n"
        "  %s --port N [--verbose]\n"
        "\n"
        "Options:\n"
        "  --port N         TCP-Port des rigctld-Listeners (Pflichtfeld)\n"
        "  --verbose        Zeigt jeden Rohbefehl von hamlib\n"
        "  --lock-mode M    Antwort auf \\get_lock_mode: 'enimpl' (Default,\n"
        "                   QUISK-artig: RPRT -4) oder 'ok' (rigctld-artig: 0)\n"
        "\n"
        "Protokoll-Hinweise:\n"
        "  [WARN] VFO-prefixed … → hamlib sendet rigctld_vfo_mode=1-Befehle\n"
        "         (chk_vfo-Handshake fehlgeschlagen oder hamlib-Bug)\n"
        "  [TRX] Mode raw: …      → exakter Befehlstring zur Diagnose\n",
        prog);
}

int main(int argc, char* argv[])
{
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--lock-mode") == 0 && i + 1 < argc)
            g_lock_mode_ok = (std::strcmp(argv[++i], "ok") == 0);
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]); return 0;
        }
    }
    if (port <= 0) {
        std::fprintf(stderr, "ERROR: --port <N> ist Pflichtfeld.\n\n");
        print_usage(argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup fehlgeschlagen\n");
        return 1;
    }
#endif

    const sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID) {
        std::fprintf(stderr, "socket() fehlgeschlagen\n");
        return 1;
    }

    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "bind() auf Port %d fehlgeschlagen\n", port);
        close_sock(server);
        return 1;
    }
    listen(server, 1);

    std::printf("\n");
    std::printf("╔═══════════════════════════════════════════════════════╗\n");
    std::printf("║           Mock Radio — rigctld Server                 ║\n");
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Port    : %-44d ║\n", port);
    std::printf("║  Freq    : %-44.3f ║\n", s_freq_hz / 1000.0);
    std::printf("║  Mode    : %-44s ║\n", s_mode.c_str());
    std::printf("║  chk_vfo : 0  (simple mode — kein VFO-Präfix)        ║\n");
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  openALE / GUI verbinden:                          ║\n");
    std::printf("║    hamlib:2:tcp://127.0.0.1:%-26d ║\n", port);
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  Diagnose: [WARN] zeigt falls hamlib VFO-Präfix sendet║\n");
    std::printf("║            [TRX] Mode zeigt exakten Rohbefehl         ║\n");
    std::printf("╚═══════════════════════════════════════════════════════╝\n");
    if (g_verbose)
        std::printf("  --verbose: alle Rohbefehle werden angezeigt\n");
    std::printf("\nWarte auf Verbindung...\n\n");
    std::fflush(stdout);

    while (true) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int addrlen = sizeof(client_addr);
#else
        socklen_t addrlen = sizeof(client_addr);
#endif
        const sock_t client = accept(server,
                                     reinterpret_cast<sockaddr*>(&client_addr),
                                     &addrlen);
        if (client == INVALID) continue;
        serve(client);
        close_sock(client);
        std::printf("Warte auf nächste Verbindung...\n\n");
        std::fflush(stdout);
    }

    close_sock(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
