/**
 * \file apps/radio_mock.cpp
 * \brief Mock Radio Server — rigctld-kompatibles Test-TRX
 *
 * Startet einen TCP-Listener auf Port 4532 (default) und implementiert
 * das Hamlib rigctld-Netzwerkprotokoll.  Jede PTT- und Frequenzänderung,
 * die ale_cli über das IRadio/HamlibRadio-Interface schickt, wird im
 * Terminal als lesbares Ereignis ausgegeben — genau wie bei einem echten
 * TRX mit CAT-Kabel, nur ohne Hardware.
 *
 * Verwendung (zwei Terminals):
 *
 *   Terminal 1 — Mock-TRX starten (--port ist Pflichtfeld):
 *     radio_mock.exe --port 4532
 *
 *   Terminal 2 — ALE-CLI verbinden (Hamlib model 2 = NET_RIGCTL):
 *     ale_cli --self SAM --radio hamlib:2:tcp://127.0.0.1:4532
 *
 *   Mehrere Instanzen auf unterschiedlichen Ports:
 *     radio_mock.exe --port 4532
 *     radio_mock.exe --port 4533
 *
 * Implementiertes rigctld-Protokoll (Hamlib 4.x, Subset):
 *   \chk_vfo            → RPRT -1     (einfacher Modus, kein VFO-Präfix)
 *   \dump_state         → Capabilities-Dump (minimal, aber parsebar)
 *   \get_info           → Versionsstring
 *   F <hz>  / \set_freq → setzt Frequenz, gibt LOG-Zeile aus
 *   f       / \get_freq → liefert aktuelle Frequenz
 *   M <mode> <bw>       → setzt Modus
 *   m       / \get_mode → liefert aktuellen Modus
 *   T <0|1> / \set_ptt  → schaltet PTT, gibt LOG-Zeile aus
 *   t       / \get_ptt  → liefert PTT-Status
 *   alles andere        → RPRT 0  (ignoriert, kein Fehler)
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
#include <string>
#include <sstream>
#include <algorithm>

// ── Radio state ───────────────────────────────────────────────────────────────

static double      s_freq_hz = 14'000'000.0;
static std::string s_mode    = "USB";
static int         s_bw      = 2400;
static int         s_ptt     = 0;   // 0 = RX, 1 = TX

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

// Minimal rigctld dump_state response.
// Format documented in Hamlib src/iofunc.c and rigs/netrigctl.c.
// Protocol version 0 (Hamlib ≤4.5).  All modes = 0x1ff.
static std::string make_dump_state()
{
    return
        "0\n"                                          // protocol version
        "1\n"                                          // rig model
        "1\n"                                          // ITU region
        // RX freq ranges: 100 kHz – 30 MHz, all modes
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        // TX freq ranges
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"
        "0 0 0 0 0 0 0\n"
        // Tuning steps (mode step) — one catch-all entry
        "0x1ff 1\n"
        "0 0\n"
        // Filters — 2400 Hz for all modes
        "2400 0x1ff\n"
        "500 0x04\n"       // 500 Hz for CW
        "0 0\n"
        "0\n"              // max_rit
        "0\n"              // max_xit
        "0\n"              // max_ifshift
        "0\n"              // announces
        "0\n"              // preamp (0 = end of list)
        "0\n"              // attenuator (0 = end)
        "0x00000003\n"     // has_get_func
        "0x00000003\n"     // has_set_func
        "0x00000001\n"     // has_get_level (STRENGTH)
        "0x00000001\n"     // has_set_level
        "0\n"              // has_get_parm
        "0\n"              // has_set_parm
        "\n"
        "RPRT 0\n";
}

// ── Command handler ───────────────────────────────────────────────────────────

static std::string handle_line(const std::string& raw)
{
    std::string line = trim(raw);
    if (line.empty()) return {};

    // Strip leading backslash (long-form \set_freq etc.)
    std::string cmd = line;
    if (!cmd.empty() && cmd[0] == '\\') cmd = cmd.substr(1);

    // ── Protocol handshake ────────────────────────────────────────────────
    if (cmd == "chk_vfo")
        return "RPRT -1\n";   // simple mode: no per-VFO prefix in commands

    if (cmd == "dump_state" || cmd == "dump_caps")
        return make_dump_state();

    if (cmd == "get_info" || cmd == "\\get_info")
        return "Mock Radio v1.0\nRPRT 0\n";

    // ── Frequency ─────────────────────────────────────────────────────────
    // Short: "F 14150000"   Long: "set_freq 14150000"
    if (cmd.rfind("set_freq ", 0) == 0 || (cmd.size() >= 2 && cmd[0] == 'F' && cmd[1] == ' ')) {
        const std::string arg = trim(cmd.substr(cmd.find(' ') + 1));
        s_freq_hz = std::stod(arg);
        char buf[64];
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
    // Short: "M USB 2400"   Long: "set_mode USB 2400"
    if (cmd.rfind("set_mode ", 0) == 0 || (cmd.size() >= 2 && cmd[0] == 'M' && cmd[1] == ' ')) {
        std::istringstream ss(cmd.substr(cmd.find(' ') + 1));
        ss >> s_mode >> s_bw;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[TRX] Mode  %s  BW=%d Hz", s_mode.c_str(), s_bw);
        log_event(buf);
        return "RPRT 0\n";
    }
    if (cmd == "get_mode" || cmd == "m") {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s\n%d\nRPRT 0\n", s_mode.c_str(), s_bw);
        return buf;
    }

    // ── PTT ───────────────────────────────────────────────────────────────
    // Short: "T 1"   Long: "set_ptt 1"
    if (cmd.rfind("set_ptt ", 0) == 0 || (cmd.size() >= 2 && cmd[0] == 'T' && cmd[1] == ' ')) {
        s_ptt = std::stoi(trim(cmd.substr(cmd.find(' ') + 1)));
        log_event(s_ptt ? "[TRX] PTT ON  ←── TX" : "[TRX] PTT OFF ──► RX");
        return "RPRT 0\n";
    }
    if (cmd == "get_ptt" || cmd == "t") {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d\nRPRT 0\n", s_ptt);
        return buf;
    }

    // ── Catch-all ─────────────────────────────────────────────────────────
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
            if (!resp.empty()) {
#ifdef _WIN32
                send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
#else
                send(client, resp.c_str(), resp.size(), 0);
#endif
            }
        }
    }

    log_event("[MOCK] Client getrennt\n");
}

// ── Main ─────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog)
{
    std::fprintf(stderr,
        "Mock Radio — rigctld-kompatibler Test-TRX\n"
        "\n"
        "Usage:\n"
        "  %s --port N\n"
        "\n"
        "Options:\n"
        "  --port N   TCP-Port des rigctld-Listeners (Pflichtfeld)\n",
        prog);
}

int main(int argc, char* argv[])
{
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = std::atoi(argv[++i]);
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

    // Allow port reuse after restart
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
    std::printf("╠═══════════════════════════════════════════════════════╣\n");
    std::printf("║  ale_cli verbinden:                                   ║\n");
    std::printf("║    --radio hamlib:2:tcp://127.0.0.1:%-18d ║\n", port);
    std::printf("╚═══════════════════════════════════════════════════════╝\n");
    std::printf("\nWarte auf Verbindung...\n\n");
    std::fflush(stdout);

    // Accept loop — one client at a time (ALE is single-station)
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
