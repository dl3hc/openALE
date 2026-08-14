/**
 * \file apps/bridge/rigctld_protocol.h
 * \brief Pure rigctld/netrigctl protocol handler — read-only frequency query.
 *
 * Takes plain data (no sockets, no ALEController, no pal::IRadio) so it can
 * be called from the main thread only (see rigctld_server.h for why) and
 * unit-tested standalone. Implements the minimal subset of the Hamlib
 * NET_RIGCTL wire protocol (rigs/dummy/netrigctl.c) needed for a real
 * Hamlib netrigctl client (RIG_MODEL_NETRIGCTL) to open() successfully and
 * poll 'f' (get_freq). Every write/set command is rejected — this is a
 * fail-closed allowlist, not a blocklist, so any command not explicitly
 * recognized here (current or future) is refused rather than silently
 * applied.
 *
 * Reply bodies for \\chk_vfo / \\dump_state / v / s are ported verbatim from
 * apps/radio_mock.cpp, which is already exercised against a real Hamlib
 * netrigctl client (openALE's own HamlibRadio connects to radio_mock as a
 * client in normal dev/test use) — see that file's header comment for the
 * protocol-quirk rationale behind each exact reply shape.
 */
#pragma once

#include <cstdint>
#include <string>

namespace bridge {

namespace detail {

inline std::string rigctld_trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// Minimal rigctld dump_state response (protocol version 0, Hamlib ≤4.5).
// Verbatim copy of apps/radio_mock.cpp's make_dump_state() — this handshake
// is load-bearing: Hamlib's netrigctl client sends \dump_state during
// rig_open() to populate its capability model. A malformed reply makes
// rig_open() fail outright and 'f' is never reached.
inline const char* rigctld_dump_state() {
    return
        "0\n"                                            // protocol version
        "1\n"                                            // rig model
        "1\n"                                            // ITU region
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"    // RX freq range
        "0 0 0 0 0 0 0\n"
        "100000 30000000 0x1ff -1 -1 0x10000003 0x1\n"    // TX freq range
        "0 0 0 0 0 0 0\n"
        "0x1ff 1\n"                                       // tuning steps
        "0 0\n"
        "2400 0x1ff\n"                                    // filters
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

} // namespace detail

/**
 * Interpret one rigctld command line and produce the reply text.
 *
 * \p radio_attached — whether openALE currently owns a live pal::IRadio.
 * \p freq_hz — current RX frequency in Hz, meaningful only if attached.
 *
 * Read-only: 'f'/\\get_freq is the only radio-state query answered from live
 * data. Every write command (F, \\set_freq, M, T, L, V, ...) and every
 * unrecognized command returns a negative RPRT rather than being applied.
 */
inline std::string handle_rigctld_command(const std::string& raw_line,
                                           bool radio_attached,
                                           uint32_t freq_hz) {
    std::string line = detail::rigctld_trim(raw_line);
    if (line.empty()) return "RPRT -1\n";

    // Strip optional leading extended-response marker ('+', ';', '|').
    if (line[0] == '+' || line[0] == ';' || line[0] == '|')
        line = detail::rigctld_trim(line.substr(1));

    // Long-form commands start with a backslash: "\get_freq".
    std::string cmd = line;
    if (!cmd.empty() && cmd[0] == '\\') cmd = cmd.substr(1);

    if (cmd == "q" || cmd == "Q") return "RPRT 0\n";

    // chk_vfo: bare "0\n" (simple mode, no VFO prefix) — NOT an RPRT line.
    // Hamlib reads exactly one line and calls atoi() on it; an RPRT line
    // here would be misparsed (see apps/radio_mock.cpp for the full story).
    if (cmd == "chk_vfo") return "0\n";

    if (cmd == "dump_state" || cmd == "dump_caps") return detail::rigctld_dump_state();

    if (cmd == "get_info") return "openALE rigctld-compat v1\nRPRT 0\n";

    // Sent by Hamlib's netrigctl client during rig_open() cache-init; fixed
    // harmless replies keep that handshake happy (apps/radio_mock.cpp:279-301).
    if (cmd == "get_vfo" || cmd == "v") return "currVFO\nRPRT 0\n";
    if (cmd == "get_split_vfo" || cmd == "s") return "0\nVFOA\nRPRT 0\n";

    if (cmd == "get_freq" || cmd == "f") {
        if (!radio_attached) return "RPRT -1\n";
        return std::to_string(freq_hz) + "\nRPRT 0\n";
    }

    // Everything else — set_freq/F, set_mode/M, set_ptt/T, set_level/L,
    // set_vfo/V, set_split_vfo, set_lock_mode, get_lock_mode, get_mode/m,
    // get_ptt/t, get_level/l, and any unknown command — is refused.
    return "RPRT -1\n";
}

} // namespace bridge
