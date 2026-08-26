/**
 * \file ale_monitor/src/ale_monitor.cpp
 * \brief Passive ALE traffic monitor — RX-only WebSocket bridge.
 *
 * Decodes and logs all ALE traffic on the configured channels.
 * Never transmits.  Configuration is read from ale_monitor.conf (resolved by
 * walking up from the exe directory, so the monitor runs with zero CLI args
 * from build/ale_monitor/Debug); CLI flags override the config file.  A
 * lightweight Settings panel in the GUI exposes audio device, radio, dwell,
 * the ALE/SEL channel filter and the mode override at runtime, persisting
 * changes back to the config file.
 *
 * Usage:
 *   ale_monitor                       # uses ale_monitor.conf / defaults
 *   ale_monitor --port N --net-file path/to/nets/USA.ale
 *               [--remote] [--audio "IN:device"] [--rig "hamlib:2:tcp://host:4532"]
 *               [--lqa-file monitor_lqa.bin] [--webroot DIR] [--config FILE]
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"
#include "App/http_poster.h"
#include "App/location_relay_service.h"
#include "PAL/events.h"
#include "PAL/crash_handler.h"
#include "PAL/logger.h"
#include "PAL/radio.h"
#include "PAL/radios/hamlib_radio.h"
#include "PAL/timer.h"
#include "Protocol/Message/ale_gpr.h"
#include "bridge/ws_server.h"
#include "bridge/minijson.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>   // readlink — exe_dir() via /proc/self/exe
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ale;
namespace mj = bridge::minijson;

// ── Signal handling ──────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};
static void sig_handler(int) { g_running = false; }

// ── Path helpers ──────────────────────────────────────────────────────────────

static bool file_exists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

#ifdef _WIN32
static std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                        s.data(), len, nullptr, nullptr);
    for (char& c : s) if (c == '\\') c = '/';
    const size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? "" : s.substr(0, slash);
}
#else
// Linux: resolve the running executable's directory from /proc/self/exe so the
// GUI web-root walk works regardless of CWD (launched from build/ or installed).
// macOS would use _NSGetExecutablePath / dladdr; this branch is Linux-only.
static std::string exe_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string s(buf);
    for (char& c : s) if (c == '\\') c = '/';
    const size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? "" : s.substr(0, slash);
}
#endif

// Resolve a repo-relative FILE path (e.g. "nets/USA.ale", "ale_monitor.conf")
// by walking up from the exe directory, with CWD fallbacks. Returns the first
// existing file, or "" if none. (Use resolve_web_root for a directory.)
static std::string resolve_relative(const std::string& rel) {
    std::vector<std::string> roots;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {       // exe dir + up to 5 parents
            roots.push_back(up + "/" + rel);
            up += "/..";
        }
    }
    roots.push_back(rel);
    roots.push_back("./" + rel);
    for (const auto& r : roots)
        if (file_exists(r)) return r;
    return "";
}

// Resolve the GUI directory by walking up from the exe dir looking for
// ale_monitor/gui/index.html (a directory is not a file, so file_exists on the
// directory itself would fail — test for the index.html marker instead).
static std::string resolve_web_root() {
    const std::string subdir = "ale_monitor/gui";
    std::vector<std::string> roots;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {
            roots.push_back(up + "/" + subdir);
            up += "/..";
        }
    }
    roots.push_back(subdir);
    roots.push_back("./" + subdir);
    for (const auto& r : roots)
        if (file_exists(r + "/index.html")) return r;
    return "";
}

// ── Config file ──────────────────────────────────────────────────────────────

// One watchlist entry: a callsign plus which alert modes it triggers in the GUI.
struct WatchlistEntry {
    std::string addr;
    bool        audible = true;
    bool        visible = true;
};

struct MonitorConfig {
    uint16_t    port = 8081;
    bool        remote = false;
    std::string net_file = "nets/USA.ale";
    std::string audio_in;                 // empty = no auto-open
    std::string rig_model;                // empty = no rig
    std::string rig_host = "127.0.0.1";
    std::string rig_port = "4532";
    std::string rig_serial;
    int         rig_baud = 0;
    // Relay-click workaround (see rig_avoid_relay_click doc in
    // ale_station_config.h) — puts the rig in Hamlib SPLIT mode while
    // scanning so the PA's band/lowpass-filter relays don't click on every
    // hop. This monitor never transmits, so unlike apps/ale_bridge.cpp there
    // is no PTT edge to drop SPLIT around — once armed it just stays on.
    bool        rig_avoid_relay_click = false;
    uint32_t    dwell_ms = 2000;
    std::string channel_filter = "all";   // all | ale | sel
    std::string mode_override;            // empty = use file mode
    std::string lqa_file = "monitor_lqa.bin";
    std::string lqa_history_file = "monitor_lqa_history.csv";
    uint32_t    history_retention_days = 90;
    bool        history_enabled = true;
    std::vector<WatchlistEntry> watchlist;  // repeatable "watchlist = ADDR:A+V" config lines

    // Location Relay (docs/LOCATION_SHARING_CONCEPT.md) — forwards received
    // ALE-GPR positions to a configured web API. This monitor has no self
    // address, so per the ALE handshake rules (WordRole::TO_SELF requires an
    // address match; only WordRole::ALLCALL is unconditional) it can only
    // ever observe ALLCALL-broadcast GPRs — individual/net/group/linked
    // exchanges between two other stations are never visible to a passive,
    // self-address-less listener. The GUI therefore exposes a single enable
    // toggle, not per-call-type checkboxes.
    bool        loc_share_enabled     = false;
    std::string loc_api_url;
    std::string loc_api_token;
    std::string loc_ca_cert_path;
    uint32_t    loc_min_interval_sec  = 30;
    uint8_t     loc_round_digits      = 6;
    bool        loc_include_comment   = false;
};

static std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// Parse "key = value" lines (# comments). Unknown keys ignored (forward-compat).
static MonitorConfig parse_config(const std::string& path) {
    MonitorConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if      (key == "port")           { try { cfg.port = static_cast<uint16_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "remote")         { cfg.remote = (val == "1" || val == "true"); }
        else if (key == "net_file")       { if (!val.empty()) cfg.net_file = val; }
        else if (key == "audio_in")       { cfg.audio_in = val; }
        else if (key == "rig_model")      { cfg.rig_model = val; }
        else if (key == "rig_host")       { cfg.rig_host = val; }
        else if (key == "rig_port")       { cfg.rig_port = val; }
        else if (key == "rig_serial")     { cfg.rig_serial = val; }
        else if (key == "rig_baud")       { try { cfg.rig_baud = std::stoi(val); } catch (...) {} }
        else if (key == "rig_avoid_relay_click") { cfg.rig_avoid_relay_click = (val == "1" || val == "true"); }
        else if (key == "dwell_ms")       { try { cfg.dwell_ms = static_cast<uint32_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "channel_filter") { cfg.channel_filter = val.empty() ? "all" : val; }
        else if (key == "mode_override")  { cfg.mode_override = val; }
        else if (key == "lqa_file")       { if (!val.empty()) cfg.lqa_file = val; }
        else if (key == "lqa_history_file")    { if (!val.empty()) cfg.lqa_history_file = val; }
        else if (key == "history_retention_days") { try { cfg.history_retention_days = static_cast<uint32_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "history_enabled") { cfg.history_enabled = (val == "1" || val == "true"); }
        else if (key == "location_sharing_enabled")     { cfg.loc_share_enabled = (val == "1" || val == "true"); }
        else if (key == "location_api_url")              { cfg.loc_api_url = val; }
        else if (key == "location_api_token")            { cfg.loc_api_token = val; }
        else if (key == "location_ca_cert_path")         { cfg.loc_ca_cert_path = val; }
        else if (key == "location_sharing_min_interval_sec") { try { cfg.loc_min_interval_sec = static_cast<uint32_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "location_sharing_round_digits") { try { cfg.loc_round_digits = static_cast<uint8_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "location_sharing_include_comment") { cfg.loc_include_comment = (val == "1" || val == "true"); }
        else if (key == "watchlist") {
            // Repeatable key — every occurrence APPENDS an entry (unlike every
            // other key here, which is last-value-wins). Format: ADDR:A+V
            // where A/V are "1"/"0" audible/visible flags, e.g. "K1ABC:1+1".
            const auto colon = val.find(':');
            WatchlistEntry we;
            we.addr = trim(colon == std::string::npos ? val : val.substr(0, colon));
            if (colon != std::string::npos) {
                const std::string flags = val.substr(colon + 1);
                const auto plus = flags.find('+');
                we.audible = plus == std::string::npos ? true : (trim(flags.substr(0, plus)) == "1");
                we.visible = plus == std::string::npos ? true : (trim(flags.substr(plus + 1)) == "1");
            }
            if (!we.addr.empty()) cfg.watchlist.push_back(we);
        }
    }
    return cfg;
}

static void save_config(const std::string& path, const MonitorConfig& cfg) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;
    f << "# ale_monitor configuration — written by the Settings panel.\n"
      << "# Lines: key = value  (# comments). Edit or delete a value to revert.\n"
      << "port = " << cfg.port << "\n"
      << "remote = " << (cfg.remote ? 1 : 0) << "\n"
      << "net_file = " << cfg.net_file << "\n"
      << "audio_in = " << cfg.audio_in << "\n"
      << "rig_model = " << cfg.rig_model << "\n"
      << "rig_host = " << cfg.rig_host << "\n"
      << "rig_port = " << cfg.rig_port << "\n"
      << "rig_serial = " << cfg.rig_serial << "\n"
      << "rig_baud = " << cfg.rig_baud << "\n"
      << "rig_avoid_relay_click = " << (cfg.rig_avoid_relay_click ? 1 : 0) << "\n"
      << "dwell_ms = " << cfg.dwell_ms << "\n"
      << "channel_filter = " << cfg.channel_filter << "\n"
      << "mode_override = " << cfg.mode_override << "\n"
      << "lqa_file = " << cfg.lqa_file << "\n"
      << "lqa_history_file = " << cfg.lqa_history_file << "\n"
      << "history_retention_days = " << cfg.history_retention_days << "\n"
      << "history_enabled = " << (cfg.history_enabled ? 1 : 0) << "\n"
      << "location_sharing_enabled = " << (cfg.loc_share_enabled ? 1 : 0) << "\n"
      << "location_api_url = " << cfg.loc_api_url << "\n"
      << "location_api_token = " << cfg.loc_api_token << "\n"
      << "location_ca_cert_path = " << cfg.loc_ca_cert_path << "\n"
      << "location_sharing_min_interval_sec = " << cfg.loc_min_interval_sec << "\n"
      << "location_sharing_round_digits = " << static_cast<unsigned>(cfg.loc_round_digits) << "\n"
      << "location_sharing_include_comment = " << (cfg.loc_include_comment ? 1 : 0) << "\n";
    for (const auto& w : cfg.watchlist)
        f << "watchlist = " << w.addr << ":" << (w.audible ? 1 : 0) << "+" << (w.visible ? 1 : 0) << "\n";
}

// Write a commented default template so a first run gives the user something
// to edit — values left at their in-code defaults.
static void write_config_template(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;
    f << "# ale_monitor configuration.\n"
      << "# key = value  (# comments). All keys optional; defaults shown.\n"
      << "# net_file is resolved by walking up from the exe dir, so it works\n"
      << "# from build/ale_monitor/Debug without an absolute path.\n"
      << "\n"
      << "port = 8081\n"
      << "remote = 0\n"
      << "net_file = nets/USA.ale\n"
      << "audio_in =\n"
      << "rig_model =\n"
      << "rig_host = 127.0.0.1\n"
      << "rig_port = 4532\n"
      << "rig_serial =\n"
      << "rig_baud = 0\n"
      << "rig_avoid_relay_click = 0\n"
      << "dwell_ms = 2000\n"
      << "# channel_filter: all | ale | sel\n"
      << "channel_filter = all\n"
      << "# mode_override: empty = use each channel's file mode; e.g. USB-D\n"
      << "mode_override =\n"
      << "lqa_file = monitor_lqa.bin\n"
      << "# lqa_history_file: append-only sounding/contact history for the\n"
      << "# Propagation Analysis GUI page (never overwritten, unlike lqa_file).\n"
      << "lqa_history_file = monitor_lqa_history.csv\n"
      << "history_retention_days = 90\n"
      << "history_enabled = 1\n"
      << "# watchlist: repeatable — one line per entry, ADDR:audible+visible (1/0 flags)\n"
      << "# watchlist = K1ABC:1+1\n"
      << "\n"
      << "# Location Relay: forward ALLCALL-broadcast ALE-GPR positions overheard\n"
      << "# on this channel to a configured HTTPS relay server for map display.\n"
      << "# Off by default. This monitor has no self address, so it can only ever\n"
      << "# see ALLCALL-broadcast position reports (see docs/LOCATION_SHARING_CONCEPT.md).\n"
      << "location_sharing_enabled = 0\n"
      << "location_api_url =\n"
      << "location_api_token =\n"
      << "# location_ca_cert_path: (optional) pinned server cert (PEM), required for a\n"
      << "# self-signed relay server; empty = system trust store.\n"
      << "location_ca_cert_path =\n"
      << "location_sharing_min_interval_sec = 30\n"
      << "location_sharing_round_digits = 6\n"
      << "location_sharing_include_comment = 0\n";
}

// ── Small helpers ────────────────────────────────────────────────────────────

static mj::Value string_array(const std::vector<std::string>& items) {
    mj::Value v = mj::arr();
    for (const auto& s : items) v.push_back(mj::Value::string(s));
    return v;
}

// "freq|station|snr|ber|sinad|score|age_ms|bilat_sinad|bilat_ber|bilat_mp|display_score|available"
static mj::Value lqa_line_to_json(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) f.push_back(tok);
    f.resize(12);
    mj::Value v = mj::obj();
    v.set("freq_hz",           mj::Value::number(std::atof(f[0].c_str())));
    v.set("station",           mj::Value::string(f[1]));
    v.set("snr_db",            mj::Value::number(std::atof(f[2].c_str())));
    v.set("ber",               mj::Value::number(std::atof(f[3].c_str())));
    v.set("sinad_db",          mj::Value::number(std::atof(f[4].c_str())));
    v.set("score",             mj::Value::number(std::atof(f[5].c_str())));
    v.set("age_ms",            mj::Value::number(std::atof(f[6].c_str())));
    v.set("bilateral_sinad_db",mj::Value::number(std::atof(f[7].c_str())));
    v.set("bilateral_ber",     mj::Value::number(std::atof(f[8].c_str())));
    v.set("bilateral_mp",      mj::Value::number(std::atof(f[9].c_str())));
    v.set("display_score",     mj::Value::number(std::atof(f[10].c_str())));
    v.set("available",         mj::Value::number(std::atoi(f[11].c_str())));
    return v;
}

static mj::Value channel_to_json(const Channel& c) {
    mj::Value v = mj::obj();
    v.set("id",                mj::Value::string(c.id));
    v.set("rx_hz",             mj::Value::number(c.rx_frequency_hz));
    v.set("tx_hz",             mj::Value::number(c.tx_frequency_hz));
    v.set("mode",              mj::Value::string(c.rx_mode));
    v.set("label",             mj::Value::string(c.label));
    v.set("enabled",           mj::Value::boolean(c.enabled));
    v.set("rx_only",           mj::Value::boolean(c.rx_only));
    v.set("tx_only",           mj::Value::boolean(c.tx_only));
    v.set("voice_use",         mj::Value::boolean(c.voice_use));
    v.set("data_use",          mj::Value::boolean(c.data_use));
    v.set("inhibit_calling",   mj::Value::boolean(c.inhibit_calling));
    v.set("inhibit_sounding",  mj::Value::boolean(c.inhibit_sounding));
    v.set("inhibit_reporting", mj::Value::boolean(c.inhibit_reporting));
    v.set("ale_only",          mj::Value::boolean(c.ale_only));
    return v;
}

static mj::Value make_reply(const mj::Value& msg, bool ok) {
    mj::Value r = mj::obj();
    if (msg.has("id")) r.set("id", *msg.find("id"));
    r.set("ok", mj::Value::boolean(ok));
    return r;
}

static mj::Value make_event(const std::string& name) {
    mj::Value e = mj::obj();
    e.set("event", mj::Value::string(name));
    return e;
}

// Assemble a pal::create_radio() spec from structured fields. The Hamlib rig
// MODEL is the single selector; its port type decides tcp:// vs serial device.
// Empty model → "" (no radio). Mirrors apps/ale_bridge.cpp build_radio_spec().
static std::string build_rig_spec(const std::string& model,
                                  const std::string& host, const std::string& port,
                                  const std::string& serial, int baud,
                                  bool avoid_relay_click = false) {
    if (model.empty()) return "";
    int model_id = 0;
    try { model_id = std::stoi(model); }
    catch (...) { return "hamlib:" + model + ":"; }
    const std::string ptype = pal::rig_port_type(model_id);
    const std::string split = avoid_relay_click ? "1" : "0";
    if (ptype == "network")
        return "hamlib:" + model + ":tcp://" + host + ":" + port + ",split=" + split;
    if (ptype == "serial")
        return "hamlib:" + model + ":" + serial
             + "," + (baud > 0 ? std::to_string(baud) : "0")
             + ",dtr=on,rts=on,stab=200,split=" + split;
    return "hamlib:" + model + ":";
}

// Channel-type classification from the HFLINK label suffix (e.g. 00ASEL/00BALE).
// "ALE" / "SEL" are the only two classes in the bundled net files; anything
// else is treated as "ale" (the class a passive ALE monitor cares about).
static std::string channel_class(const Channel& c) {
    const std::string& l = c.label;
    if (l.size() >= 3) {
        const std::string suf = l.substr(l.size() - 3);
        if (suf == "SEL") return "sel";
        if (suf == "ALE") return "ale";
    }
    return "ale";
}

// ── Context ──────────────────────────────────────────────────────────────────

struct MonitorCtx {
    ALEController*                ctrl;
    std::unique_ptr<AudioDevice>* audio;
    std::unique_ptr<pal::IRadio>* radio;
    MonitorConfig*                cfg;
    std::string                   config_path;   // empty = persistence disabled
    std::string                   net_file_path;  // resolved, for reload
    ale::LocationRelayService*    loc_svc;
};

// LocationRelayService::ConnState → GUI string. Mirrors apps/ale_bridge.cpp's
// loc_conn_state_name().
static const char* loc_conn_state_name(int cs) {
    switch (cs) {
        case ale::LocationRelayService::CS_CONNECTED:    return "connected";
        case ale::LocationRelayService::CS_DISCONNECTED: return "disconnected";
        case ale::LocationRelayService::CS_SERVER_ERROR: return "server_error";
        default:                                          return "unknown";
    }
}

// Build a throwaway ALEStationConfig carrying only the 6 location_sharing_*
// fields is_shareable() actually reads (verified against
// src/App/location_relay_service.cpp) — never touches ctrl's real config.
// location_sharing_allcall is hardcoded true / individual|net|group|linked
// false: this monitor has no self address, so ALLCALL is the only call type
// it can ever observe (see MonitorConfig's location fields doc comment).
static ale::ALEStationConfig build_gate_cfg(const MonitorConfig& cfg) {
    ale::ALEStationConfig g;
    g.location_sharing_enabled         = cfg.loc_share_enabled;
    g.location_sharing_allcall         = true;
    g.location_sharing_individual      = false;
    g.location_sharing_net             = false;
    g.location_sharing_group           = false;
    g.location_sharing_linked          = false;
    g.location_sharing_round_digits    = cfg.loc_round_digits;
    g.location_sharing_include_comment = cfg.loc_include_comment;
    return g;
}

// Stop (if running) and, if enabled+configured, start a fresh
// LocationRelayService worker thread from the current MonitorConfig.
// Location-only slice of apps/ale_bridge.cpp's restart_location_services().
static void restart_location_relay(MonitorCtx& ctx) {
    ctx.loc_svc->stop();   // always stop first; start() below spawns a fresh thread
    const auto& cfg = *ctx.cfg;
    if (cfg.loc_share_enabled && !cfg.loc_api_url.empty()) {
        ale::LocationRelayService::Config lcfg;
        lcfg.url             = cfg.loc_api_url;
        lcfg.token            = cfg.loc_api_token;
        lcfg.ca_cert_path     = cfg.loc_ca_cert_path;
        lcfg.min_interval_sec = cfg.loc_min_interval_sec;
        pal::log_info("ale_monitor", "Location Relay: starting (%s)", lcfg.url.c_str());
        ctx.loc_svc->start(lcfg);
    } else {
        pal::log_info("ale_monitor", "Location Relay: disabled");
    }
}

// Apply the channel filter (all|ale|sel) to the loaded channel list by
// toggling Channel::enabled. Idempotent.
static void apply_filter(ALEController& ctrl, const std::string& filter) {
    for (const auto& c : ctrl.channels()) {
        bool on = true;
        if (filter == "ale") on = (channel_class(c) == "ale");
        else if (filter == "sel") on = (channel_class(c) == "sel");
        ctrl.set_channel_enabled(c.id, on);
    }
}

// Override every channel's mode, or reload the net file (restoring file modes)
// when mode is empty. Filter is re-applied after a reload.
static void apply_mode_override(ALEController& ctrl, const std::string& net_file_path,
                                const std::string& filter, const std::string& mode) {
    if (mode.empty()) {
        ctrl.load_station_file(net_file_path);
        ctrl.set_station_file("");   // never auto-save the shipped net file
        apply_filter(ctrl, filter);
        return;
    }
    for (const auto& c : ctrl.channels())
        ctrl.set_channel_mode(c.id, mode);
}

// ── Command dispatch ─────────────────────────────────────────────────────────

static std::string dispatch_command(MonitorCtx& ctx, const mj::Value& msg) {
    ALEController& ctrl = *ctx.ctrl;
    const std::string cmd = msg.get_string("cmd");

    // ── Status ────────────────────────────────────────────────────────────
    if (cmd == "STATUS") {
        mj::Value r = make_reply(msg, true);
        r.set("state", mj::Value::string(ALEStateMachine::state_name(ctrl.state())));
        return mj::dump(r);
    }

    // ── Scan / Available ─────────────────────────────────────────────────
    if (cmd == "SCAN") {
        if (ctrl.channels().size() < 2) {
            mj::Value r = make_reply(msg, false);
            r.set("error", mj::Value::string("need >=2 channels to scan"));
            return mj::dump(r);
        }
        ctrl.start_scanning();
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AVAILABLE") {
        const std::string res = ctrl.process_command("CMD:AVAILABLE");
        return mj::dump(make_reply(msg, res.rfind("OK:", 0) == 0));
    }

    // ── Channels (read-only) ──────────────────────────────────────────────
    if (cmd == "CHANNELS_LIST") {
        mj::Value list = mj::arr();
        for (const auto& c : ctrl.channels())
            list.push_back(channel_to_json(c));
        mj::Value r = make_reply(msg, true);
        r.set("data", std::move(list));
        return mj::dump(r);
    }

    // ── Audio ─────────────────────────────────────────────────────────────
    if (cmd == "AUDIO_DEVICES") {
        // Enumerate via a throwaway device — works before any device is attached.
        std::vector<std::string> inputs, outputs;
        for (const auto& d : make_audio_device()->list_devices()) {
            if (d.rfind("IN:", 0) == 0)       inputs.push_back(d);
            else if (d.rfind("OUT:", 0) == 0) outputs.push_back(d);
        }
        mj::Value r = make_reply(msg, true);
        r.set("inputs",  string_array(inputs));
        r.set("outputs", string_array(outputs));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_OPEN") {
        // Close any open device first (detach from controller before close()).
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); }
        *ctx.audio = make_audio_device();
        const std::string in_dev = msg.get_string("in");
        const bool ok = (*ctx.audio)->open(in_dev, "");   // RX-only (empty out)
        if (ok) {
            ctrl.set_audio_device(ctx.audio->get());
            ctx.cfg->audio_in = in_dev;
        } else {
            ctx.audio->reset();
            ctx.cfg->audio_in.clear();
        }
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not open audio device"));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_CLOSE") {
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); ctx.audio->reset(); }
        ctx.cfg->audio_in.clear();
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AUDIO_LEVEL") {
        mj::Value r = make_reply(msg, true);
        r.set("level", mj::Value::number(ctrl.get_audio_input_level()));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_SET_VOL") {   // accepted, no-op (monitor has no TX)
        return mj::dump(make_reply(msg, true));
    }

    // ── Radio ──────────────────────────────────────────────────────────────
    if (cmd == "RIG_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value arr = mj::arr();
        for (const auto& e : pal::list_rigs()) {
            mj::Value v = mj::obj();
            v.set("id",    mj::Value::number(e.model));
            v.set("mfg",   mj::Value::string(e.mfg));
            v.set("macro", mj::Value::string(e.macro));
            v.set("port",  mj::Value::string(e.port_type));
            arr.push_back(std::move(v));
        }
        r.set("rigs", std::move(arr));
        return mj::dump(r);
    }
    if (cmd == "RIG_CONNECT") {
        if (*ctx.radio) { ctrl.set_radio(nullptr); (*ctx.radio)->stop(); ctx.radio->reset(); }
        const std::string model  = msg.get_string("model");
        const std::string host   = msg.get_string("host", ctx.cfg->rig_host);
        const std::string port   = msg.get_string("port", ctx.cfg->rig_port);
        const std::string serial = msg.get_string("serial", ctx.cfg->rig_serial);
        const int baud            = static_cast<int>(msg.get_number("baud", ctx.cfg->rig_baud));
        const bool split = msg.get_bool("split", ctx.cfg->rig_avoid_relay_click);
        const std::string spec   = build_rig_spec(model, host, port, serial, baud, split);
        if (spec.empty()) {  // None / Offline
            ctx.cfg->rig_model.clear();
            mj::Value r = make_reply(msg, true);
            r.set("connected", mj::Value::boolean(false));
            r.set("status", mj::Value::string("not attached"));
            return mj::dump(r);
        }
        *ctx.radio = pal::create_radio(spec);
        const bool ok = *ctx.radio && (*ctx.radio)->initialize() && (*ctx.radio)->start();
        if (ok) {
            ctrl.set_radio(ctx.radio->get());
            if (split != ctx.cfg->rig_avoid_relay_click) {
                pal::log_info("ale_monitor", "Relay-click avoidance (SPLIT mode while scanning) %s",
                               split ? "enabled" : "disabled");
            }
            ctx.cfg->rig_model  = model;
            ctx.cfg->rig_host   = host;
            ctx.cfg->rig_port   = port;
            ctx.cfg->rig_serial = serial;
            ctx.cfg->rig_baud   = baud;
            ctx.cfg->rig_avoid_relay_click = split;
        } else {
            ctx.radio->reset();
        }
        mj::Value r = make_reply(msg, ok);
        r.set("connected", mj::Value::boolean(ok && ctrl.test_rig_connection()));
        r.set("status", mj::Value::string(ctrl.get_rig_connection_status()));
        if (!ok) r.set("error", mj::Value::string("could not connect radio (" + spec + ")"));
        return mj::dump(r);
    }
    if (cmd == "RIG_DISCONNECT") {
        if (*ctx.radio) { ctrl.set_radio(nullptr); (*ctx.radio)->stop(); ctx.radio->reset(); }
        ctx.cfg->rig_model.clear();
        mj::Value r = make_reply(msg, true);
        r.set("connected", mj::Value::boolean(false));
        r.set("status", mj::Value::string("not attached"));
        return mj::dump(r);
    }
    if (cmd == "RIG_STATUS") {
        mj::Value r = make_reply(msg, true);
        r.set("connected", mj::Value::boolean(ctrl.test_rig_connection()));
        r.set("status", mj::Value::string(ctrl.get_rig_connection_status()));
        return mj::dump(r);
    }

    // ── LQA ──────────────────────────────────────────────────────────────
    if (cmd == "LQA_LIST") {
        mj::Value list = mj::arr();
        for (const auto& line : ctrl.get_all_lqa_entries())
            list.push_back(lqa_line_to_json(line));
        mj::Value r = make_reply(msg, true);
        r.set("data", std::move(list));
        return mj::dump(r);
    }
    if (cmd == "LQA_CLEAR") {
        ctrl.process_command("CMD:CLEAR_LQA");
        if (!ctx.cfg->lqa_file.empty()) ctrl.save_lqa(ctx.cfg->lqa_file);
        return mj::dump(make_reply(msg, true));
    }

    // ── LQA history (append-only — separate store, separate clear action) ──
    if (cmd == "LQA_HISTORY_LIST") {
        const uint64_t since_ms = static_cast<uint64_t>(msg.get_number("since_ms", 0));
        const std::string station = msg.get_string("station");
        const uint32_t freq_hz = static_cast<uint32_t>(msg.get_number("freq_hz", 0));
        const size_t limit = static_cast<size_t>(msg.get_number("limit", 0));
        mj::Value list = mj::arr();
        for (const auto& s : ctrl.get_lqa_history(since_ms, station, freq_hz, limit)) {
            mj::Value v = mj::obj();
            v.set("ts_ms",    mj::Value::number(static_cast<double>(s.ts_ms)));
            v.set("freq_hz",  mj::Value::number(s.frequency_hz));
            v.set("station",  mj::Value::string(s.station));
            v.set("sinad_db", mj::Value::number(s.sinad_db));
            v.set("ber",      mj::Value::number(s.ber));
            v.set("score",    mj::Value::number(s.score));
            list.push_back(std::move(v));
        }
        mj::Value r = make_reply(msg, true);
        r.set("data", std::move(list));
        return mj::dump(r);
    }
    if (cmd == "LQA_HISTORY_CLEAR") {
        const bool ok = ctrl.clear_lqa_history(ctx.cfg->lqa_history_file);
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not clear history file"));
        return mj::dump(r);
    }

    // ── Watchlist / alerting ─────────────────────────────────────────────
    if (cmd == "WATCHLIST_GET") {
        mj::Value list = mj::arr();
        for (const auto& w : ctx.cfg->watchlist) {
            mj::Value v = mj::obj();
            v.set("addr",    mj::Value::string(w.addr));
            v.set("audible", mj::Value::boolean(w.audible));
            v.set("visible", mj::Value::boolean(w.visible));
            list.push_back(std::move(v));
        }
        mj::Value r = make_reply(msg, true);
        r.set("data", std::move(list));
        return mj::dump(r);
    }
    if (cmd == "WATCHLIST_SET") {
        // Full replace — not auto-persisted; the GUI calls MON_CONFIG_SAVE
        // separately (reuses the existing "Save as startup" button).
        std::vector<WatchlistEntry> next;
        const mj::Value* list = msg.find("list");
        if (list && list->is_array()) {
            for (const auto& item : list->items()) {
                WatchlistEntry we;
                we.addr    = item.get_string("addr");
                we.audible = item.get_bool("audible", true);
                we.visible = item.get_bool("visible", true);
                if (!we.addr.empty()) next.push_back(we);
            }
        }
        ctx.cfg->watchlist = std::move(next);
        return mj::dump(make_reply(msg, true));
    }

    // ── VFO (read-only) ───────────────────────────────────────────────────
    if (cmd == "VFO_GET") {
        ctrl.sync_radio_state();
        mj::Value r = make_reply(msg, true);
        r.set("freq_hz",     mj::Value::number(ctrl.get_current_frequency()));
        r.set("mode",        mj::Value::string(ctrl.get_current_mode()));
        r.set("tune_step_hz",mj::Value::number(ctrl.get_tune_step()));
        r.set("ptt",         mj::Value::boolean(false));  // monitor never TX
        return mj::dump(r);
    }

    // ── Timing (dwell + the read-only subset the monitor needs) ───────────
    if (cmd == "TIMING_SET") {
        if (msg.has("scan_dwell_ms")) {
            ctrl.set_scan_dwell_ms(static_cast<uint32_t>(msg.get_number("scan_dwell_ms")));
            ctx.cfg->dwell_ms = ctrl.get_scan_dwell_ms();
        }
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "TIMING_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("scan_dwell_ms", mj::Value::number(ctrl.get_scan_dwell_ms()));
        return mj::dump(r);
    }

    // ── Monitor-only configuration (bug 4/5/6 + persistence) ───────────────
    if (cmd == "MON_CONFIG_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("dwell_ms",        mj::Value::number(ctx.cfg->dwell_ms));
        r.set("channel_filter",  mj::Value::string(ctx.cfg->channel_filter));
        r.set("mode_override",   mj::Value::string(ctx.cfg->mode_override));
        r.set("audio_in",        mj::Value::string(ctx.cfg->audio_in));
        r.set("rig_model",       mj::Value::string(ctx.cfg->rig_model));
        r.set("rig_host",        mj::Value::string(ctx.cfg->rig_host));
        r.set("rig_port",        mj::Value::string(ctx.cfg->rig_port));
        r.set("rig_serial",      mj::Value::string(ctx.cfg->rig_serial));
        r.set("rig_baud",        mj::Value::number(ctx.cfg->rig_baud));
        r.set("rig_avoid_relay_click", mj::Value::boolean(ctx.cfg->rig_avoid_relay_click));
        r.set("net_file",        mj::Value::string(ctx.cfg->net_file));
        return mj::dump(r);
    }
    if (cmd == "MON_FILTER") {
        const std::string f = msg.get_string("filter");
        if (f == "all" || f == "ale" || f == "sel") {
            ctx.cfg->channel_filter = f;
            apply_filter(ctrl, f);
            // Re-apply the mode override so every channel — including any
            // newly-(re)enabled one — adheres to it. Idempotent; no-op if unset.
            if (!ctx.cfg->mode_override.empty())
                apply_mode_override(ctrl, ctx.net_file_path, f, ctx.cfg->mode_override);
            return mj::dump(make_reply(msg, true));
        }
        mj::Value r = make_reply(msg, false);
        r.set("error", mj::Value::string("filter must be all|ale|sel"));
        return mj::dump(r);
    }
    if (cmd == "MON_MODE_OVERRIDE") {
        const std::string m = msg.get_string("mode");
        ctx.cfg->mode_override = m;
        apply_mode_override(ctrl, ctx.net_file_path, ctx.cfg->channel_filter, m);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "MON_CONFIG_SAVE") {
        if (ctx.config_path.empty()) {
            mj::Value r = make_reply(msg, false);
            r.set("error", mj::Value::string("no config path"));
            return mj::dump(r);
        }
        save_config(ctx.config_path, *ctx.cfg);
        return mj::dump(make_reply(msg, true));
    }

    // ── Location Relay (docs/LOCATION_SHARING_CONCEPT.md) ─────────────────
    // ale_monitor has no self address, so it only ever forwards ALLCALL-
    // broadcast GPRs — see MonitorConfig's location fields doc comment.
    // Config changes apply + restart the service immediately but, matching
    // this app's existing MON_FILTER/WATCHLIST_SET convention, are only
    // persisted to disk via the explicit MON_CONFIG_SAVE ("Save as startup").
    if (cmd == "LOCATION_SHARING_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("enabled",          mj::Value::boolean(ctx.cfg->loc_share_enabled));
        r.set("url",              mj::Value::string(ctx.cfg->loc_api_url));
        // Token is deliberately NOT echoed back — never log/expose it once
        // set. The GUI shows a "configured"/"not configured" hint instead.
        r.set("token_set",        mj::Value::boolean(!ctx.cfg->loc_api_token.empty()));
        r.set("ca_cert_path",     mj::Value::string(ctx.cfg->loc_ca_cert_path));
        r.set("min_interval_sec", mj::Value::number(ctx.cfg->loc_min_interval_sec));
        r.set("round_digits",     mj::Value::number(ctx.cfg->loc_round_digits));
        r.set("include_comment",  mj::Value::boolean(ctx.cfg->loc_include_comment));
        r.set("running",          mj::Value::boolean(ctx.loc_svc->is_running()));
        r.set("conn_state",       mj::Value::string(loc_conn_state_name(ctx.loc_svc->conn_state())));
        return mj::dump(r);
    }
    if (cmd == "LOCATION_SHARING_SET") {
        if (msg.has("url")) {
            const std::string url = msg.get_string("url");
            if (!url.empty() && !ale::location_url_allowed(url)) {
                mj::Value r = make_reply(msg, false);
                r.set("error", mj::Value::string("URL must be https://, or http://127.0.0.1 for local testing"));
                return mj::dump(r);
            }
            ctx.cfg->loc_api_url = url;
        }
        if (msg.has("enabled"))       ctx.cfg->loc_share_enabled = msg.get_bool("enabled");
        // Empty "token" is a no-op (keep the stored token) — only a non-empty
        // value overwrites, so the GUI can leave the field blank on re-save
        // without clobbering an already-configured token.
        if (msg.has("token") && !msg.get_string("token").empty())
            ctx.cfg->loc_api_token = msg.get_string("token");
        if (msg.has("ca_cert_path"))  ctx.cfg->loc_ca_cert_path = msg.get_string("ca_cert_path");
        if (msg.has("min_interval_sec"))
            ctx.cfg->loc_min_interval_sec = static_cast<uint32_t>(msg.get_number("min_interval_sec"));
        if (msg.has("round_digits"))
            ctx.cfg->loc_round_digits = static_cast<uint8_t>(msg.get_number("round_digits"));
        if (msg.has("include_comment")) ctx.cfg->loc_include_comment = msg.get_bool("include_comment");
        restart_location_relay(ctx);
        return mj::dump(make_reply(msg, true));
    }

    mj::Value r = make_reply(msg, false);
    r.set("error", mj::Value::string("unknown command: " + cmd));
    return mj::dump(r);
}

// ── Usage ────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::fprintf(stderr, // NOLINT(pal-logger)
        "ALE Traffic Monitor — passive RX-only WebSocket bridge\n"
        "\n"
        "Decodes all ALE traffic on the configured channels and streams it to a\n"
        "browser UI.  Never transmits.  Configuration is read from ale_monitor.conf\n"
        "(resolved by walking up from the exe dir); CLI flags override the file.\n"
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "Options (all optional — defaults/ale_monitor.conf suffice):\n"
        "  --port N           WebSocket listen port (default 8081)\n"
        "  --net-file FILE    .ale channel file (default nets/USA.ale)\n"
        "  --audio SPEC       Audio input device, e.g. \"IN:CABLE Output\"\n"
        "  --rig SPEC         Hamlib radio spec, e.g. \"hamlib:2:tcp://127.0.0.1:4532\"\n"
        "  --lqa-file FILE     LQA persistence file (default monitor_lqa.bin)\n"
        "  --config FILE       Config file (default ale_monitor.conf next to exe)\n"
        "  --remote            Bind to 0.0.0.0 instead of 127.0.0.1\n"
        "  --webroot DIR       Serve GUI from DIR instead of auto-detected path\n"
        "\n"
        "Examples:\n"
        "  %s                           # uses ale_monitor.conf / defaults\n"
        "  %s --port 8081 --net-file nets/USA.ale\n",
        prog, prog, prog);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    pal::set_logger(pal::create_logger());
    pal::install_crash_handler();
    pal::set_event_handler(pal::create_event_handler());

    // CLI overrides (applied on top of the config file below).
    uint16_t    cli_port        = 0;
    bool        cli_remote       = false;
    bool        cli_remote_set   = false;
    std::string cli_net_file;
    std::string cli_audio;
    std::string cli_rig_spec;     // raw hamlib spec override (auto-connect only)
    std::string cli_lqa;
    std::string cli_config;
    std::string web_root;

    for (int i = 1; i < argc; ++i) {
        if      (std::strcmp(argv[i], "--port")     == 0 && i + 1 < argc) { cli_port = static_cast<uint16_t>(std::atoi(argv[++i])); }
        else if (std::strcmp(argv[i], "--remote")   == 0)                  { cli_remote = true; cli_remote_set = true; }
        else if (std::strcmp(argv[i], "--net-file") == 0 && i + 1 < argc) { cli_net_file = argv[++i]; }
        else if (std::strcmp(argv[i], "--audio")    == 0 && i + 1 < argc) { cli_audio = argv[++i]; }
        else if (std::strcmp(argv[i], "--rig")      == 0 && i + 1 < argc) { cli_rig_spec = argv[++i]; }
        else if (std::strcmp(argv[i], "--lqa-file") == 0 && i + 1 < argc) { cli_lqa = argv[++i]; }
        else if (std::strcmp(argv[i], "--config")   == 0 && i + 1 < argc) { cli_config = argv[++i]; }
        else if (std::strcmp(argv[i], "--webroot")  == 0 && i + 1 < argc) { web_root = argv[++i]; }
        else if (std::strcmp(argv[i], "--help")     == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]); return 0;
        }
    }

    // ── Config file (walk-up from exe dir, then CWD) ───────────────────────
    std::string config_path = cli_config.empty()
        ? resolve_relative("ale_monitor.conf") : cli_config;
    MonitorConfig cfg;
    if (!config_path.empty()) {
        cfg = parse_config(config_path);
        pal::log_info("ale_monitor", "Config: %s", config_path.c_str());
    } else {
        // No config found — write a commented template next to the exe so the
        // user has something to edit. resolve_relative walked up from the exe
        // dir; write it one level up from the exe (the repo root) when possible.
        const std::string ed = exe_dir();
        const std::string tmpl = ed.empty() ? "ale_monitor.conf" : ed + "/../ale_monitor.conf";
        write_config_template(tmpl);
        pal::log_info("ale_monitor", "No config found — wrote template to %s", tmpl.c_str());
        config_path = tmpl;   // so MON_CONFIG_SAVE has somewhere to write
    }

    // CLI overrides on top of config.
    if (cli_port != 0)          cfg.port = cli_port;
    if (cli_remote_set)         cfg.remote = cli_remote;
    if (!cli_net_file.empty())  cfg.net_file = cli_net_file;
    if (!cli_audio.empty())     cfg.audio_in = cli_audio;
    if (!cli_lqa.empty())       cfg.lqa_file = cli_lqa;

    if (cfg.port == 0) cfg.port = 8081;

    if (web_root.empty()) web_root = resolve_web_root();

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    auto timer = pal::create_timer();

    // ── WebSocket server ─────────────────────────────────────────────────
    bridge::WsServer ws;
    ws.set_web_root(web_root);
    if (!ws.start(cfg.port, cfg.remote)) {
        pal::log_error("ale_monitor", "Failed to start WebSocket server on port %u.", cfg.port);
        return 1;
    }

    // ── ALE controller ───────────────────────────────────────────────────
    // No self-address: the SM will never respond to calls — purely passive.
    ALEController ctrl;

    if (ctrl.load_lqa(cfg.lqa_file))
        pal::log_info("ale_monitor", "LQA loaded from %s (%zu entries)",
                       cfg.lqa_file.c_str(), ctrl.get_all_lqa_entries().size());
    // failure path (missing vs. corrupt file) is already logged inside
    // ALEController::load_lqa() with the specific reason.

    ctrl.set_lqa_history_config(cfg.history_retention_days, cfg.history_enabled);
    if (ctrl.load_lqa_history(cfg.lqa_history_file))
        pal::log_info("ale_monitor", "LQA history loaded from %s", cfg.lqa_history_file.c_str());

    // ── Radio ────────────────────────────────────────────────────────────
    std::unique_ptr<pal::IRadio> radio;
    std::string rig_spec_to_use = cli_rig_spec;
    if (rig_spec_to_use.empty() && !cfg.rig_model.empty())
        rig_spec_to_use = build_rig_spec(cfg.rig_model, cfg.rig_host, cfg.rig_port,
                                         cfg.rig_serial, cfg.rig_baud,
                                         cfg.rig_avoid_relay_click);
    if (!rig_spec_to_use.empty()) {
        radio = pal::create_radio(rig_spec_to_use);
        if (radio && radio->initialize() && radio->start()) {
            ctrl.set_radio(radio.get());
            pal::log_info("ale_monitor", "Radio attached: %s", rig_spec_to_use.c_str());
        } else {
            pal::log_warn("ale_monitor", "Could not connect radio (%s) — continuing without rig",
                          rig_spec_to_use.c_str());
            radio.reset();
        }
    }

    // ── Net file (required) — walk-up resolved so it works from build/Debug ─
    std::string net_file_path = resolve_relative(cfg.net_file);
    if (net_file_path.empty()) net_file_path = cfg.net_file;  // last resort: CWD
    const bool loaded = ctrl.load_station_file(net_file_path);
    if (!loaded || ctrl.channels().empty()) {
        pal::log_error("ale_monitor", "No channels loaded from '%s'", net_file_path.c_str());
        return 1;
    }
    // The monitor must never auto-save the shipped net file: set_channel_enabled
    // / set_channel_mode auto-save to station_file_ when set, which would
    // overwrite nets/*.ale. Clear it so mutations stay in-memory only.
    ctrl.set_station_file("");
    pal::log_info("ale_monitor", "Loaded %zu channel(s) from %s",
                  ctrl.channels().size(), net_file_path.c_str());

    // Apply channel filter + mode override before scanning.
    apply_filter(ctrl, cfg.channel_filter);
    if (!cfg.mode_override.empty())
        apply_mode_override(ctrl, net_file_path, cfg.channel_filter, cfg.mode_override);
    ctrl.set_scan_dwell_ms(cfg.dwell_ms);

    // ── Audio (RX-only) — auto-open if configured ──────────────────────────
    std::unique_ptr<AudioDevice> audio;
    if (!cfg.audio_in.empty()) {
        audio = make_audio_device();
        if (audio->open(cfg.audio_in, "")) {   // empty out-device = RX-only
            ctrl.set_audio_device(audio.get());
            pal::log_info("ale_monitor", "Audio RX opened: %s", cfg.audio_in.c_str());
        } else {
            pal::log_warn("ale_monitor", "Could not open audio device '%s' — no audio input",
                          cfg.audio_in.c_str());
            audio.reset();
        }
    }

    ale::LocationRelayService loc_svc;
    MonitorCtx ctx{ &ctrl, &audio, &radio, &cfg, config_path, net_file_path, &loc_svc };

    // ── Event bus subscriptions ──────────────────────────────────────────
    {
        auto* bus = pal::get_event_handler();

        bus->on(pal::EventType::ALE_STATUS, [&](const pal::Event& ev) {
            mj::Value e = make_event("status");
            e.set("msg", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_WORD_DECODED, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::WordData*>(ev.data);
            mj::Value e = make_event("word_decoded");
            e.set("frame_id", mj::Value::number(d->frame_id));
            e.set("preamble", mj::Value::string(d->preamble));
            e.set("addr",     mj::Value::string(d->addr));
            e.set("votes",    mj::Value::number(d->votes));
            e.set("fec",      mj::Value::number(d->fec));
            e.set("ts_ms",    mj::Value::number(d->ts_ms));
            e.set("freq_hz",  mj::Value::number(d->freq_hz));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_FRAME_DECODED, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::FrameData*>(ev.data);
            mj::Value e = make_event("frame_decoded");
            e.set("frame_id",    mj::Value::number(d->frame_id));
            e.set("call_type",   mj::Value::string(d->call_type));
            e.set("from",        mj::Value::string(d->from_addr));
            e.set("word_count",  mj::Value::number(static_cast<double>(d->word_count)));
            e.set("start_ms",    mj::Value::number(d->start_ms));
            e.set("duration_ms", mj::Value::number(d->duration_ms));
            e.set("freq_hz",     mj::Value::number(d->freq_hz));
            mj::Value to_arr = mj::arr();
            for (const auto& a : *d->to_addrs)
                to_arr.push_back(mj::Value::string(a));
            e.set("to", std::move(to_arr));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::CHANNEL_CHANGED, [&](const pal::Event& ev) {
            const auto* ch = static_cast<const Channel*>(ev.data);
            mj::Value e = make_event("channel_changed");
            e.set("channel_id", mj::Value::string(ch->id));
            e.set("rx_hz",      mj::Value::number(ch->rx_frequency_hz));
            e.set("tx_hz",      mj::Value::number(ch->tx_frequency_hz));
            e.set("mode",       mj::Value::string(ch->rx_mode));
            ws.send_text(mj::dump(e));
        });

        // Location Relay (docs/LOCATION_SHARING_CONCEPT.md §4): only ALLCALL
        // GPRs ever reach this handler — see MonitorConfig's location fields
        // doc comment for why. Gate → dedup(in enqueue) → queue, mirroring
        // apps/ale_bridge.cpp's independent ALE_AMD_RECEIVED subscriber.
        bus->on(pal::EventType::ALE_AMD_RECEIVED, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::AmdData*>(ev.data);
            if (!ale::is_gpr(d->text)) return;

            const ale::AleGpr g = ale::parse_gpr(d->text);
            // Visibility into every incoming ALE-GPR, independent of whether
            // Location Relay is enabled/gates it — operators need to see what
            // was overheard (and why a malformed one wasn't relayed).
            if (g.valid_position) {
                const std::string alt_str = g.has_altitude
                    ? (" alt=" + std::to_string(static_cast<long>(g.altitude)) + g.altitude_unit)
                    : std::string();
                pal::log_info("GPR", "position report: object=%s via %s (peer=%s) lat=%.6f lon=%.6f%s",
                              g.object.c_str(), d->call_context, d->peer_addr,
                              g.latitude_deg, g.longitude_deg, alt_str.c_str());
            } else {
                pal::log_warn("GPR", "malformed/incomplete report from %s via %s: \"%s\"",
                              d->peer_addr, d->call_context, d->text);
            }

            if (!ctx.cfg->loc_share_enabled) return;
            const std::string source = g.object.empty() ? std::string(d->peer_addr) : g.object;
            const ale::ALEStationConfig gate_cfg = build_gate_cfg(*ctx.cfg);
            if (!ale::is_shareable(g, source, d->call_context, d->self_addr, gate_cfg)) return;

            ale::LocationReport r;
            r.observer     = d->self_addr;   // always empty — this monitor has no self address
            r.source       = source;
            r.relay        = (source != d->peer_addr) ? d->peer_addr : "";
            r.raw_gpr      = g.raw;
            r.has_position = g.valid_position;
            if (g.has_latitude && g.has_longitude) {
                const double scale = std::pow(10.0, ctx.cfg->loc_round_digits);
                r.lat = std::round(g.latitude_deg * scale) / scale;
                r.lon = std::round(g.longitude_deg * scale) / scale;
            }
            r.has_altitude  = g.has_altitude;
            r.altitude      = g.altitude;
            r.altitude_unit = g.altitude_unit;
            r.has_timestamp = g.has_timestamp;
            r.timestamp_utc = g.timestamp_utc;
            r.comment       = ctx.cfg->loc_include_comment ? g.comment : "";
            r.call_context  = d->call_context;
            r.received_at   = std::time(nullptr);
            r.frequency_hz  = ctrl.get_current_channel().rx_frequency_hz;
            r.dedup_key     = ale::make_dedup_key(g, source, ctx.cfg->loc_round_digits);

            ctx.loc_svc->enqueue(std::move(r));
            pal::log_info("LocationRelay", "queued %s (via %s)", source.c_str(), d->call_context);
        });
    }

    ctrl.set_spectrum_callback([&](const float* bins, size_t n, float /*hz_per_bin*/) {
        ws.send_binary(bins, n * sizeof(float));
    });

    // ── Startup ──────────────────────────────────────────────────────────
    restart_location_relay(ctx);   // start eagerly from loaded config (no GUI round-trip needed)

    if (ctrl.channels().size() >= 2) {
        ctrl.start_scanning();
        pal::log_info("ale_monitor", "Scanning started (%zu channels, dwell %ums, filter %s)",
                      ctrl.channels().size(), cfg.dwell_ms, cfg.channel_filter.c_str());
    } else {
        ctrl.start_available();
        pal::log_info("ale_monitor", "Single channel — monitoring in AVAILABLE state");
    }

    pal::log_info("ale_monitor", "Listening on %s:%u",
                  cfg.remote ? "0.0.0.0" : "127.0.0.1", cfg.port);
    if (web_root.empty())
        pal::log_info("ale_monitor", "GUI not found — open via file:// or pass --webroot");
    else
        pal::log_info("ale_monitor", "Open GUI:  http://localhost:%u/index.html", cfg.port);

    // ── Main loop ────────────────────────────────────────────────────────
    std::vector<int16_t> rx_buf;
    std::string last_state    = ctrl.display_state();
    bool        last_lbt_busy = false;

    while (g_running) {
        const uint32_t t = static_cast<uint32_t>(timer->get_time_ms());

        // Audio first: words delivered before dwell check (§A.5.3.3 Bug 2 fix).
        if (audio) {
            rx_buf.clear();
            audio->tick(rx_buf);
            if (!rx_buf.empty())
                ctrl.feed_audio(rx_buf.data(), static_cast<uint32_t>(rx_buf.size()));
        }

        ctrl.update(t);

        std::string raw;
        while (ws.pop_message(raw)) {
            const mj::Value parsed = mj::parse(raw);
            ws.send_text(dispatch_command(ctx, parsed));
        }

        const std::string s = ctrl.display_state();
        if (s != last_state) {
            mj::Value e = make_event("state");
            e.set("value", mj::Value::string(s));
            ws.send_text(mj::dump(e));
            last_state = s;
        }

        const bool cur_lbt_busy = ctrl.lbt_busy();
        if (cur_lbt_busy != last_lbt_busy) {
            mj::Value e = make_event("channel_busy");
            e.set("busy",     mj::Value::boolean(cur_lbt_busy));
            e.set("level_db", mj::Value::number(ctrl.lbt_level_db()));
            e.set("floor_db", mj::Value::number(ctrl.lbt_floor_db()));
            ws.send_text(mj::dump(e));
            last_lbt_busy = cur_lbt_busy;
        }

        // Location Relay: pull-based drain of the worker thread's status log
        // lines and connection-state transitions (mirrors apps/ale_bridge.cpp).
        std::string loc_status;
        while (loc_svc.pop_status(loc_status)) {
            mj::Value e = make_event("status");
            e.set("msg", mj::Value::string(loc_status));
            ws.send_text(mj::dump(e));
        }
        int loc_cs;
        while (loc_svc.pop_conn_state(loc_cs)) {
            mj::Value e = make_event("location_relay");
            e.set("running",    mj::Value::boolean(loc_svc.is_running()));
            e.set("conn_state", mj::Value::string(loc_conn_state_name(loc_cs)));
            ws.send_text(mj::dump(e));
        }

        timer->sleep_ms(1);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    ctrl.emergency_stop();
    loc_svc.stop();
    if (radio) radio->stop();
    if (audio) audio->close();
    ws.stop();
    if (ctrl.save_lqa(cfg.lqa_file))
        pal::log_info("ale_monitor", "LQA saved to %s", cfg.lqa_file.c_str());
    pal::log_info("ale_monitor", "Exiting.");
    return 0;
}