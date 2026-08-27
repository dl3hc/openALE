/**
 * \file apps/ale_bridge.cpp
 * \brief WebSocket bridge — exposes ale::ALEController to the browser GUI (apps/gui/).
 *
 * Mirrors apps/ale_cli.cpp's structure and main loop almost exactly; the
 * difference is purely the command source and the addition of event/spectrum
 * push: stdin + printf become a WebSocket connection (apps/bridge/ws_server.h)
 * speaking a small JSON protocol (apps/bridge/minijson.h).
 *
 * Usage:
 *   openALE --port N [--remote] [--webroot DIR]
 *
 * Without --in-device/--out-device, the controller runs in the existing
 * "offline" mode (no AudioDevice attached) — useful for protocol-level GUI
 * testing without a sound card.
 *
 * Wire protocol (see docs comment block below dispatch_command()):
 *   GUI -> bridge : {"id":N,"cmd":"...", ...args}   (text frame)
 *   bridge -> GUI : {"id":N,"ok":bool,[...]}          (text frame, command reply)
 *                   {"event":"...", ...}              (text frame, async event)
 *                   <4097 float32 LE>                 (binary frame, spectrum — dBFS values)
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"
#include "App/audio_monitor.h"
#include "App/audio_transport.h"
#include "App/gps_service.h"
#include "App/http_poster.h"
#include "App/location_relay_service.h"
#include "App/sfi_service.h"
#include "App/voice_path_manager.h"
#include "Protocol/Message/ale_gpr.h"
#include "PAL/crash_handler.h"
#include "PAL/events.h"
#include "PAL/logger.h"
#include "PAL/paths.h"
#include "PAL/radio.h"
#include "PAL/radios/hamlib_radio.h"
#include "PAL/timer.h"
#include "bridge/ws_server.h"
#include "bridge/rigctld_server.h"
#include "bridge/rigctld_protocol.h"
#include "bridge/minijson.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>   // readlink — exe_dir() via /proc/self/exe
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace ale;
namespace mj = bridge::minijson;

// ── Signal handling ─────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};
static void sig_handler(int) { g_running = false; }

// ── Web-root resolution ─────────────────────────────────────────────────────
//
// The GUI's static files live in the repo at apps/gui/, but the exe runs from
// build/Debug/ (or possibly the repo root). Resolve apps/gui/ robustly by
// walking up from the exe directory looking for apps/gui/index.html, with a few
// CWD fallbacks. Returns "" if not found (static serving then 404s — the GUI
// can still be opened via file:// as before).

static bool file_exists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

#ifdef _WIN32
static std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    for (char& c : s) if (c == '\\') c = '/';
    const size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? "" : s.substr(0, slash);
}
#else
// Linux: resolve the running executable's directory from /proc/self/exe so the
// GUI web-root walk (resolve_web_root) works regardless of CWD — e.g. when
// launched from build/ or an installed location. macOS would use
// _NSGetExecutablePath / dladdr instead; this branch is Linux-only (ALSA build).
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

static std::string resolve_web_root(bool mobile = false) {
    const std::string subdir = mobile ? "apps/gui/mobile" : "apps/gui";
    std::vector<std::string> roots;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {       // exe dir + up to 5 parents
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

// Resolve a relative resource path (e.g. an .ale station/channel file) the
// same way resolve_web_root() resolves apps/gui/: try it as given first (so
// an already-correct relative or absolute path, e.g. "run from repo root",
// keeps working unchanged), then walk up from the exe directory looking for
// it — covers the common case of launching the built binary from build/ (or
// build/Debug/), where a bare "nets/USA.ale" would otherwise only resolve
// against build/, not the repo root where nets/ actually lives. Falls back
// to the original path unchanged if nothing is found, so the caller's own
// ifstream-open error handling still applies.
static std::string resolve_data_path(const std::string& path) {
    if (path.empty() || file_exists(path)) return path;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {       // exe dir + up to 5 parents
            const std::string candidate = up + "/" + path;
            if (file_exists(candidate)) return candidate;
            up += "/..";
        }
    }
    return path;
}

// Lists the "nets/<name>.ale" files available to STATION_LOAD, for the setup
// wizard's channel-file dropdown. Walks the same candidate roots as
// resolve_data_path()/resolve_web_root() (exe dir + up to 5 parents, then
// CWD) so it finds the real nets/ directory regardless of whether openALE is
// launched from the repo root or a build/ subdirectory, then stops at the
// first root that actually contains a nets/ directory.
static std::vector<std::string> list_net_files() {
    std::vector<std::string> roots;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {       // exe dir + up to 5 parents
            roots.push_back(up + "/nets");
            up += "/..";
        }
    }
    roots.push_back("nets");
    roots.push_back("./nets");

    std::vector<std::string> out;
    for (const auto& dir : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".ale") continue;
            out.push_back("nets/" + entry.path().filename().string());
        }
        if (!out.empty()) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ── Small helpers ───────────────────────────────────────────────────────────

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        const auto a = tok.find_first_not_of(" \t");
        const auto b = tok.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(tok.substr(a, b - a + 1));
    }
    return out;
}

static mj::Value string_array(const std::vector<std::string>& items) {
    mj::Value v = mj::arr();
    for (const auto& s : items) v.push_back(mj::Value::string(s));
    return v;
}


// "ADDR|enabled|chan1,chan2" -> {addr,status,valid_channels:[]/"ALL"}
static mj::Value self_addr_line_to_json(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) f.push_back(tok);
    f.resize(3);
    mj::Value v = mj::obj();
    v.set("addr", mj::Value::string(f[0]));
    v.set("status", mj::Value::string(f[1]));
    if (f[2] == "ALL") v.set("valid_channels", mj::Value::string("ALL"));
    else                v.set("valid_channels", string_array(split_csv(f[2])));
    return v;
}

// "freq|station|snr|ber|sinad|score|age_ms|bilat_sinad|bilat_ber|bilat_mp|display_score|available"
// -> object (see ALEController::get_all_lqa_entries()). The bilateral_* fields are
// the peer-reported CMD-LQA metrics (A.5.4.2): SINAD code [0-30] dB higher=better
// (31 = no measurement), BER code [0-30] 2/3-vote count lower=better (31 = no
// value), MP code [0-6] ms (7 = not measured). Shipped so the GUI can display a
// real measurement when no local FROM-direction snr/sinad exists. available is
// the sounding-conclusion flag: 1 = TIS (available), 0 = TWAS (not available),
// -1 = no sounding heard from this station.
static mj::Value lqa_line_to_json(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) f.push_back(tok);
    f.resize(13);
    mj::Value v = mj::obj();
    v.set("freq_hz", mj::Value::number(std::atof(f[0].c_str())));
    v.set("station", mj::Value::string(f[1]));
    v.set("snr_db", mj::Value::number(std::atof(f[2].c_str())));
    v.set("ber", mj::Value::number(std::atof(f[3].c_str())));
    v.set("sinad_db", mj::Value::number(std::atof(f[4].c_str())));
    v.set("score", mj::Value::number(std::atof(f[5].c_str())));
    v.set("age_ms", mj::Value::number(std::atof(f[6].c_str())));
    v.set("bilateral_sinad_db", mj::Value::number(std::atof(f[7].c_str())));
    v.set("bilateral_ber", mj::Value::number(std::atof(f[8].c_str())));
    v.set("bilateral_mp", mj::Value::number(std::atof(f[9].c_str())));
    v.set("display_score", mj::Value::number(std::atof(f[10].c_str())));
    v.set("available", mj::Value::number(std::atoi(f[11].c_str())));
    // P1-12: raw LQA-DB timestamp (ms since epoch, 32-bit wrapped — see
    // ALEController::get_all_lqa_entries()) so the GUI can show an absolute
    // "received at" time alongside the relative age_ms.
    v.set("last_activity_ms", mj::Value::number(std::atof(f[12].c_str())));
    return v;
}

static mj::Value channel_to_json(const Channel& c) {
    mj::Value v = mj::obj();
    v.set("id", mj::Value::string(c.id));
    v.set("rx_hz", mj::Value::number(c.rx_frequency_hz));
    v.set("tx_hz", mj::Value::number(c.tx_frequency_hz));
    v.set("mode", mj::Value::string(c.rx_mode));
    v.set("label", mj::Value::string(c.label));
    v.set("enabled", mj::Value::boolean(c.enabled));
    v.set("rx_only", mj::Value::boolean(c.rx_only));
    v.set("tx_only", mj::Value::boolean(c.tx_only));
    v.set("voice_use", mj::Value::boolean(c.voice_use));
    v.set("data_use", mj::Value::boolean(c.data_use));
    v.set("inhibit_calling",   mj::Value::boolean(c.inhibit_calling));
    v.set("inhibit_sounding",  mj::Value::boolean(c.inhibit_sounding));
    v.set("inhibit_reporting", mj::Value::boolean(c.inhibit_reporting));
    v.set("ale_only",          mj::Value::boolean(c.ale_only));
    v.set("power_pct",         mj::Value::number(c.power_pct));
    return v;
}

static mj::Value net_to_json(const Net& n) {
    mj::Value v = mj::obj();
    v.set("name",                 mj::Value::string(n.name));
    v.set("channel_ids",          string_array(n.channel_ids));
    v.set("dwell_ms",             mj::Value::number(n.dwell_ms));
    v.set("scanning_enabled",     mj::Value::boolean(n.scanning_enabled));
    v.set("sounding_enabled",     mj::Value::boolean(n.sounding_enabled));
    v.set("sounding_interval_sec",mj::Value::number(n.sounding_interval_sec));
    v.set("calling_length_c",     mj::Value::number(n.calling_length_c));
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

// LocationRelayService::ConnState → GUI string. Mirrors the enum order in
// App/location_relay_service.h.
static const char* loc_conn_state_name(int s) {
    switch (s) {
        case ale::LocationRelayService::CS_CONNECTED:    return "connected";
        case ale::LocationRelayService::CS_DISCONNECTED: return "disconnected";
        case ale::LocationRelayService::CS_SERVER_ERROR: return "server_error";
        default:                                          return "unknown";
    }
}

// LocationRelayService::RegState → GUI string. Mirrors the enum order in
// App/location_relay_service.h.
static const char* loc_reg_state_name(int s) {
    switch (s) {
        case ale::LocationRelayService::REG_PENDING:  return "pending";
        case ale::LocationRelayService::REG_APPROVED: return "approved";
        case ale::LocationRelayService::REG_REJECTED: return "rejected";
        case ale::LocationRelayService::REG_REVOKED:  return "revoked";
        default:                                       return "unknown";
    }
}

// Assemble the pal::create_radio() spec from structured GUI fields (so the GUI
// never needs to know the hamlib spec syntax). The Hamlib rig MODEL is the single
// selector; its port type (derived from rig_caps::port_type via pal::rig_port_type)
// decides whether the spec carries a tcp:// endpoint or a serial device + line
// state. Empty model → "" (None / Offline → no radio attached).
static std::string build_radio_spec(const mj::Value& msg) {
    const std::string model = msg.get_string("model", "");
    if (model.empty()) return "";
    int model_id = 0;
    try { model_id = std::stoi(model); } catch (...) { return "hamlib:" + model + ":"; }
    const std::string ptype = pal::rig_port_type(model_id);
    // ptt=normal|mic|data — CAT PTT audio-input select (Kenwood TX0/TX1 etc.),
    // relevant regardless of transport, so it's read once and appended to
    // every branch below.
    const std::string ptt = msg.get_string("ptt", "normal");
    // split=1 arms the relay-click workaround (SerialLinePolicy::avoid_relay_click) —
    // relevant regardless of transport, so it's read once like ptt above.
    const std::string split = msg.get_bool("split", false) ? "1" : "0";
    if (ptype == "network") {
        // "hamlib:<model>:tcp://<host>:<port>" — works for any network backend
        // (NET rigctl #2, FLRig #4, Quisk #10, GQRX #11, …), not just model 2.
        return "hamlib:" + model + ":tcp://" + msg.get_string("host", "127.0.0.1")
             + ":" + msg.get_string("port", "4532")
             + ",ptt=" + ptt
             + ",split=" + split;
    }
    if (ptype == "serial") {
        const std::string port  = msg.get_string("serial", "");
        const int baud  = static_cast<int>(msg.get_number("baud", 0));
        // Line-state policy: defaults ON/ON/200 wenn nicht gesetzt
        const std::string dtr = msg.get_string("dtr", "on");
        const std::string rts = msg.get_string("rts", "on");
        const int stab = static_cast<int>(msg.get_number("stab", 200));
        // Format: "hamlib:model:port,baud,dtr=on,rts=on,stab=200,ptt=normal,split=0"
        return "hamlib:" + model + ":" + port
             + "," + (baud > 0 ? std::to_string(baud) : "0")
             + ",dtr=" + dtr
             + ",rts=" + rts
             + ",stab=" + std::to_string(stab)
             + ",ptt=" + ptt
             + ",split=" + split;
    }
    // other (Dummy / USB / Audio / …) — no connection parameters.
    return "hamlib:" + model + ":";
}

// Thread-safe mailbox: GPS/SFI worker callbacks write here; the main loop
// drains it and calls the single-threaded ALEController from the main thread.
struct PendingUpdate {
    std::mutex mtx;
    bool   gps_dirty = false;
    bool   gps_acq   = false;
    double gps_lat   = 0.0;
    double gps_lon   = 0.0;
    bool   sfi_dirty = false;
    float  sfi       = 0.0f;
};

// The bridge owns the audio device and radio (both runtime-settable from the
// GUI now that there are no startup flags). dispatch_command() needs to reach
// them — not just the controller — to open/close/connect on command. Pointers,
// because both can be null (offline) and get reset() at runtime.
struct BridgeCtx {
    ALEController*                 ctrl;
    std::unique_ptr<AudioDevice>*  audio;
    std::unique_ptr<pal::IRadio>*  radio;
    std::string                    lqa_path;   ///< empty = persistence disabled
    std::string                    state_path; ///< unified auto-save file (channels/nets/settings)
    std::string                    audio_in;   ///< last successfully opened RX device
    std::string                    audio_out;  ///< last successfully opened TX device
    float                          tx_volume = 0.25f; ///< persists across AUDIO_OPEN/CLOSE
    // GPS / SFI services and shared pending-update mailbox
    GpsService*            gps_svc  = nullptr;
    SfiService*            sfi_svc  = nullptr;
    LocationRelayService*  loc_svc  = nullptr;
    PendingUpdate*    pending  = nullptr;
    bridge::WsServer* ws       = nullptr;  ///< for GPS/SFI push events from STATION_LOC_SET
    // Dynamic audio-path owner (ALE-modem ↔ voice passthrough on the VAC).
    VoicePathManager* voice      = nullptr;
    bool              voice_armed = false;  ///< persisted in settings (voice_armed=)
    // Link-independent RX monitor ("listen in" on the channel). Session-only —
    // never persisted, always defaults off on bridge start/restart.
    AudioMonitor*     audio_monitor = nullptr;
    bridge::RigctldServer* rigctld = nullptr;  ///< netrigctl-compat read-only frequency server
};

// ── Command dispatch ────────────────────────────────────────────────────────
//
// Attach `spec` as the live radio, tearing down any existing one first. Shared
// by RIG_CONNECT and the startup auto-reconnect path (restart_radio_connection)
// so both go through one create/initialize/start/set_radio sequence. Empty
// spec just tears down and returns false (None/Offline — not an error, caller
// decides how to report that).
static bool connect_radio(BridgeCtx& ctx, ALEController& ctrl, const std::string& spec) {
    if (*ctx.radio) { ctrl.set_radio(nullptr); (*ctx.radio)->stop(); ctx.radio->reset(); }
    if (spec.empty()) return false;
    *ctx.radio = pal::create_radio(spec);
    bool ok = *ctx.radio && (*ctx.radio)->initialize() && (*ctx.radio)->start();
    if (ok) ctrl.set_radio(ctx.radio->get());
    else    ctx.radio->reset();
    return ok;
}

// Rebuild the mj::Value build_radio_spec() expects, from persisted config
// instead of a live GUI message — used only by restart_radio_connection().
static mj::Value rig_config_to_msg(const ALEStationConfig& cfg) {
    mj::Value m = mj::obj();
    m.set("model",  mj::Value::string(cfg.rig_model));
    m.set("host",   mj::Value::string(cfg.rig_host));
    m.set("port",   mj::Value::string(cfg.rig_port));
    m.set("serial", mj::Value::string(cfg.rig_serial));
    m.set("baud",   mj::Value::number(cfg.rig_baud));
    m.set("dtr",    mj::Value::string(cfg.rig_dtr));
    m.set("rts",    mj::Value::string(cfg.rig_rts));
    m.set("stab",   mj::Value::number(cfg.rig_stab));
    m.set("ptt",    mj::Value::string(cfg.rig_ptt));
    m.set("split",  mj::Value::boolean(cfg.rig_avoid_relay_click));
    return m;
}

// Re-attach the last-connected rig automatically at startup, mirroring how
// restart_rigctld_server() below auto-starts from persisted config. Opt-in:
// only armed when a prior RIG_CONNECT succeeded and no RIG_DISCONNECT
// followed (see rig_auto_connect doc in ale_station_config.h).
static void restart_radio_connection(BridgeCtx& ctx, ALEController& ctrl) {
    const auto& cfg = ctrl.get_config();
    if (!cfg.rig_auto_connect || cfg.rig_model.empty()) return;
    const std::string spec = build_radio_spec(rig_config_to_msg(cfg));
    if (connect_radio(ctx, ctrl, spec)) {
        pal::log_info("openALE", "Rig auto-reconnect: attached model %s (%s)",
                      cfg.rig_model.c_str(), ctrl.get_rig_connection_status().c_str());
    } else {
        pal::log_error("openALE", "Rig auto-reconnect: failed to attach model %s (%s)",
                       cfg.rig_model.c_str(), spec.c_str());
    }
}

// Open (or reopen) the audio device with the given RX/TX device names, tearing
// down any existing device first. Shared by AUDIO_OPEN and the startup
// auto-reopen path (restart_audio_connection). Identical to AUDIO_OPEN's
// former inline body — just factored out so both call sites stay in sync.
static bool open_audio(BridgeCtx& ctx, ALEController& ctrl,
                        const std::string& in_dev, const std::string& out_dev) {
    if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); }
    *ctx.audio = make_audio_device();
    const bool ok = (*ctx.audio)->open(in_dev, out_dev);
    if (ok) {
        ctrl.set_audio_device(ctx.audio->get());
        (*ctx.audio)->set_tx_volume(ctx.tx_volume);
        ctx.audio_in = in_dev; ctx.audio_out = out_dev;
    } else {
        ctx.audio->reset(); ctx.audio_in.clear(); ctx.audio_out.clear();
    }
    return ok;
}

// Re-open the last-used audio device automatically at startup, mirroring
// restart_radio_connection() above. Opt-in: only armed when a prior
// AUDIO_OPEN succeeded and no AUDIO_CLOSE followed (see audio_auto_open doc
// in ale_station_config.h).
static void restart_audio_connection(BridgeCtx& ctx, ALEController& ctrl) {
    const auto& cfg = ctrl.get_config();
    if (!cfg.audio_auto_open) return;
    if (cfg.audio_in.empty() && cfg.audio_out.empty()) return;
    if (open_audio(ctx, ctrl, cfg.audio_in, cfg.audio_out)) {
        pal::log_info("openALE", "Audio auto-reopen: in='%s' out='%s'",
                      cfg.audio_in.c_str(), cfg.audio_out.c_str());
    } else {
        pal::log_error("openALE", "Audio auto-reopen: failed to open in='%s' out='%s'",
                       cfg.audio_in.c_str(), cfg.audio_out.c_str());
    }
}

// Restart GPS and/or SFI services to match the current ALEController config.
// Call after any config change that may affect position source or sfi_enabled.
// Safe to call on a fresh BridgeCtx (stop() on un-started services is no-op).
static void restart_location_services(BridgeCtx& ctx, ALEController& ctrl) {
    using PS = ALEStationConfig::PositionSource;
    const auto& cfg = ctrl.get_config();

    // ── GPS ──────────────────────────────────────────────────────────────────
    if (ctx.gps_svc && ctx.pending) {
        ctx.gps_svc->stop();
        const PS src = ctrl.get_position_source();
        if (src == PS::GPSD || src == PS::NMEA_SERIAL) {
            GpsService::Config gcfg;
            gcfg.gpsd_enabled = (src == PS::GPSD);
            gcfg.gpsd_host    = cfg.gpsd_host;
            gcfg.gpsd_port    = cfg.gpsd_port;
            gcfg.nmea_enabled = (src == PS::NMEA_SERIAL);
            gcfg.nmea_port    = cfg.nmea_port;
            gcfg.nmea_baud    = cfg.nmea_baud;
            pal::log_info("openALE", "GPS service: starting (%s)",
                          src == PS::GPSD ? "gpsd" : "NMEA serial");
            PendingUpdate*    pend   = ctx.pending;
            bridge::WsServer* ws_ptr = ctx.ws;
            ctx.gps_svc->start(gcfg, [pend, ws_ptr](bool acq, double lat, double lon) {
                { std::lock_guard<std::mutex> g(pend->mtx);
                  pend->gps_dirty = true; pend->gps_acq = acq;
                  pend->gps_lat = lat;   pend->gps_lon = lon; }
                if (ws_ptr) {
                    mj::Value e = make_event("gps_fix");
                    e.set("acquired", mj::Value::boolean(acq));
                    if (acq) {
                        e.set("lat", mj::Value::number(lat));
                        e.set("lon", mj::Value::number(lon));
                    }
                    ws_ptr->send_text(mj::dump(e));
                }
            });
        }
    }

    // ── SFI ──────────────────────────────────────────────────────────────────
    if (ctx.sfi_svc && ctx.pending) {
        ctx.sfi_svc->stop();   // always stop first; start() below will spawn fresh thread
        if (cfg.sfi_enabled) {
            pal::log_info("openALE", "SFI service: starting fetch thread");
            PendingUpdate*    pend   = ctx.pending;
            bridge::WsServer* ws_ptr = ctx.ws;
            ctx.sfi_svc->start([pend, ws_ptr](float sfi) {
                { std::lock_guard<std::mutex> g(pend->mtx);
                  pend->sfi_dirty = true; pend->sfi = sfi; }
                if (ws_ptr) {
                    mj::Value e = make_event("sfi_update");
                    e.set("sfi", mj::Value::number(sfi));
                    ws_ptr->send_text(mj::dump(e));
                }
            });
        } else {
            pal::log_info("openALE", "SFI service: disabled");
        }
    }

    // ── Location Relay ───────────────────────────────────────────────────────
    if (ctx.loc_svc) {
        ctx.loc_svc->stop();   // always stop first; start() below spawns a fresh thread
        if (cfg.location_sharing_enabled && !cfg.location_api_url.empty()) {
            LocationRelayService::Config lcfg;
            lcfg.url               = cfg.location_api_url;
            lcfg.callsign           = ctrl.get_primary_self_address();
            // Default: next to station.state, not inside it — the identity
            // file is a secret (the seed), station.state is not.
            lcfg.identity_key_path = !cfg.location_relay_identity_path.empty()
                ? cfg.location_relay_identity_path
                : (ctx.state_path.empty() ? "location_relay_identity.key"
                                           : ctx.state_path.substr(0, ctx.state_path.find_last_of("/\\") + 1)
                                             + "location_relay_identity.key");
            lcfg.ca_cert_path       = cfg.location_ca_cert_path;
            lcfg.queue_size         = cfg.location_sharing_queue_size;
            lcfg.min_interval_sec   = cfg.location_sharing_min_interval_sec;
            if (lcfg.callsign.empty()) {
                pal::log_warn("openALE",
                               "Location Relay: no self address configured — cannot create a "
                               "signing identity, service not started");
                ctx.loc_svc->report_config_error(
                    "Location Relay: not started — configure this station's own callsign "
                    "(Self Address) first, it's needed to sign relay requests");
            } else {
                pal::log_info("openALE", "Location Relay: starting (%s, identity=%s)",
                               lcfg.url.c_str(), lcfg.callsign.c_str());
                ctx.loc_svc->start(lcfg);
            }
        } else {
            pal::log_info("openALE", "Location Relay: disabled");
        }
    }
}

// Restart the rigctld-compat server to match current config. Safe to call on
// a fresh BridgeCtx (stop() on a never-started RigctldServer is a no-op).
static void restart_rigctld_server(BridgeCtx& ctx, ALEController& ctrl) {
    if (!ctx.rigctld) return;
    ctx.rigctld->stop();
    const auto& cfg = ctrl.get_config();
    if (cfg.rigctld_server_enabled) {
        if (ctx.rigctld->start(cfg.rigctld_server_port, cfg.rigctld_server_bind_remote)) {
            pal::log_info("openALE", "rigctld-compat server: listening on %s:%u",
                          cfg.rigctld_server_bind_remote ? "0.0.0.0" : "127.0.0.1",
                          cfg.rigctld_server_port);
        } else {
            pal::log_error("openALE", "rigctld-compat server: failed to bind port %u",
                           cfg.rigctld_server_port);
        }
    }
}

// Helper: call process_command() and map its text result to a JSON bool reply.
// All operator CMDs go through this — single code path, no duplicated dispatch.
static std::string pc(const mj::Value& msg, ALEController& ctrl, const std::string& cmd) {
    const std::string r = ctrl.process_command(cmd);
    return mj::dump(make_reply(msg, r.rfind("OK:", 0) == 0));
}

// Every command's reply is {"id":<echo>,"ok":bool,...}. "id" is whatever the
// GUI sent (number or absent — JS request counters are the expected case).
// Operator CMDs route through ctrl.process_command() via the pc() helper above.
// Commands that return structured data or own bridge-level state (AUDIO_OPEN,
// RIG_CONNECT, TIMING_SET, channel/contact/net construction, etc.) call the
// controller directly here.
//
// Runs on the main-loop thread (commands drained in the loop), so AUDIO_OPEN's
// close+reopen is sequential with audio->tick() — no race.
static std::string dispatch_command(BridgeCtx& ctx, const mj::Value& msg) {
    ALEController& ctrl = *ctx.ctrl;   // keeps every existing `ctrl.` call below unchanged
    const std::string cmd = msg.get_string("cmd");
    pal::log_trace("CMD", "%s", cmd.c_str());

    if (cmd == "STATUS") {
        mj::Value r = make_reply(msg, true);
        r.set("state", mj::Value::string(ALEStateMachine::state_name(ctrl.state())));
        r.set("self", mj::Value::string(ctrl.self()));
        r.set("link_active", mj::Value::boolean(ctrl.is_link_active()));
        r.set("peer", mj::Value::string(ctrl.active_peer()));
        r.set("call_duration_s", mj::Value::number(ctrl.get_call_duration_seconds()));
        return mj::dump(r);
    }
    if (cmd == "SCAN") {
        // Scanning hops over a channel list — needs >=2. Bounce non-GUI clients
        // cleanly too (the GUI already greys the Scan button out below 2).
        if (ctrl.channels().size() < 2) {
            mj::Value r = make_reply(msg, false);
            r.set("error", mj::Value::string("need >=2 channels to scan"));
            return mj::dump(r);
        }
        ctrl.start_scanning();
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AVAILABLE")       { return pc(msg, ctrl, "CMD:AVAILABLE"); }
    if (cmd == "SOUND")           { return pc(msg, ctrl, "CMD:SOUND"); }
    if (cmd == "SOUND_SWEEP") {
        return pc(msg, ctrl, "CMD:SOUND_SWEEP " + msg.get_string("net"));
    }
    if (cmd == "SOUND_AUTO") {
        return pc(msg, ctrl, std::string("CMD:SOUND_AUTO ")
            + (msg.get_bool("on") ? "on" : "off")
            + (msg.get_string("net").empty() ? "" : " " + msg.get_string("net")));
    }
    if (cmd == "SOUND_AUTO_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("on",           mj::Value::boolean(ctrl.is_automatic_sounding()));
        r.set("interval_sec", mj::Value::number(ctrl.get_auto_sounding_interval_sec()));
        r.set("net",          mj::Value::string(ctrl.get_auto_sounding_net()));
        return mj::dump(r);
    }
    if (cmd == "SOUND_INTERRUPT") { return pc(msg, ctrl, "CMD:SOUND_INTERRUPT " + msg.get_string("net")); }
    if (cmd == "TEST_CHANNEL") {
        // Actively link to a peer on each configured channel, record LQA, terminate,
        // advance. Net is optional (defaults to active scan net / all callable).
        return pc(msg, ctrl, "CMD:TEST_CHANNEL " + msg.get_string("addr")
                       + (msg.get_string("net").empty() ? "" : " " + msg.get_string("net")));
    }
    if (cmd == "TEST_CHANNEL_STOP") { return pc(msg, ctrl, "CMD:TEST_CHANNEL_STOP"); }

    if (cmd == "CALL") {
        const bool single = msg.get_bool("single_channel", false);
        return pc(msg, ctrl, (single ? "CMD:SINGLE_CALL " : "CMD:CALL ") + msg.get_string("addr"));
    }
    if (cmd == "GROUP_CALL") {
        if (msg.find("roster"))
            return pc(msg, ctrl, "CMD:GROUP_CALL " + msg.get_string("roster"));
        const mj::Value* members = msg.find("members");
        if (!members) return mj::dump(make_reply(msg, false));
        // Same optional {text, link} typed-dispatch pattern as "AMD": when text
        // is present, queue an AMD on the group call instead of a plain call.
        if (msg.has("text")) {
            const std::string text = msg.get_string("text");
            const bool link        = msg.get_bool("link", false);
            const std::string resp = ctrl.send_amd_group(members->as_string_array(), text, link);
            mj::Value r = make_reply(msg, resp.rfind("OK:", 0) == 0);
            r.set("msg", mj::Value::string(resp));
            return mj::dump(r);
        }
        const bool ok = ctrl.initiate_group_call(members->as_string_array());
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "GROUP_ROSTERS_LIST") {
        const auto& rosters = ctrl.get_all_group_rosters();
        mj::Value r = make_reply(msg, true);
        mj::Value arr = mj::arr();
        for (const auto& roster : rosters) {
            mj::Value v = mj::obj();
            v.set("name", mj::Value::string(roster.name));
            v.set("members", string_array(roster.members));
            arr.push_back(std::move(v));
        }
        r.set("rosters", std::move(arr));
        return mj::dump(r);
    }
    if (cmd == "GROUP_ROSTER_ADD") {
        return mj::dump(make_reply(msg, ctrl.add_group_roster(msg.get_string("name"))));
    }
    if (cmd == "GROUP_ROSTER_DEL") {
        return mj::dump(make_reply(msg, ctrl.del_group_roster(msg.get_string("name"))));
    }
    if (cmd == "GROUP_ROSTER_MEMBER_ADD") {
        return mj::dump(make_reply(msg, ctrl.add_group_member(msg.get_string("name"), msg.get_string("callsign"))));
    }
    if (cmd == "GROUP_ROSTER_MEMBER_DEL") {
        return mj::dump(make_reply(msg, ctrl.del_group_member(msg.get_string("name"), msg.get_string("callsign"))));
    }
    if (cmd == "ALLCALL_INITIATE") {
        const std::string sel = msg.get_string("selector");
        const char s = sel.empty() ? '?' : sel[0];
        return mj::dump(make_reply(msg, ctrl.initiate_all_call(s)));
    }
    if (cmd == "ALLCALL_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("accept",   mj::Value::boolean(ctrl.get_allcall_accept()));
        r.set("selector", mj::Value::string(std::string(1, ctrl.get_allcall_selector())));
        return mj::dump(r);
    }
    if (cmd == "ALLCALL_SET") {
        if (msg.find("accept"))
            ctrl.set_allcall_accept(msg.get_bool("accept", true));
        if (msg.find("selector")) {
            const std::string sel = msg.get_string("selector");
            if (!sel.empty()) ctrl.set_allcall_selector(sel[0]);
        }
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "ACCEPT")           { return pc(msg, ctrl, "CMD:ACCEPT"); }
    if (cmd == "REJECT")           { return pc(msg, ctrl, "CMD:REJECT"); }
    if (cmd == "TERMINATE") {
        // AudioTransport's TX arbiter sees is_tx_active()=true when the SM
        // queues the TWAS burst and immediately switches the VAC to the symbol
        // path, so no pre-release of the voice path is needed here.
        return pc(msg, ctrl, "CMD:TERMINATE");
    }
    if (cmd == "RESET_IDLE_TIMER") { return pc(msg, ctrl, "CMD:RESET_IDLE_TIMER"); }
    if (cmd == "EMERGENCY_STOP")   { return pc(msg, ctrl, "CMD:EMERGENCY_STOP"); }
    if (cmd == "SET_PTT") {
        // Context-routed: while the voice path owns the VAC (LINKED + armed),
        // SET_PTT is the operator's voice PTT (half-duplex direction). Otherwise
        // it is the legacy ALE manual-PTT override (keys the radio with modem
        // output). The mobile PTT button sends SET_PTT in both cases.
        const bool on = msg.get_bool("on", false);
        if (ctx.voice && ctx.voice->passthrough_active()) {
            ctx.voice->set_ptt(on);
            return mj::dump(make_reply(msg, true));
        }
        return pc(msg, ctrl, on ? "CMD:SET_PTT on" : "CMD:SET_PTT off");
    }

    if (cmd == "AMD") {
        // Typed dispatch: if LINKED, send AMD over the established link; otherwise
        // queue it and place a call to `to`. `to` is the selected contact (or the
        // active peer when LINKED — ignored by send_amd in that case).
        // `link` (optional, default false): not-LINKED path only — whether the
        // third handshake frame concludes with TIS (link persists) or TWAS (no
        // link, message already delivered from the calling frame). Omitting the
        // field no longer forces a link, matching the Ion2G-style default.
        const std::string to   = msg.get_string("to");
        const std::string text = msg.get_string("text");
        const bool link        = msg.get_bool("link", false);
        const std::string resp = ctrl.send_amd(to, text, link);
        mj::Value r = make_reply(msg, resp.rfind("OK:", 0) == 0);
        r.set("msg", mj::Value::string(resp));
        return mj::dump(r);
    }
    if (cmd == "GPR_BUILD") {
        // Read-only: builds and returns an ALE-GPR payload string, sends nothing.
        // The caller sends it via the existing "AMD" command ({to, text, link}).
        // position_report_enabled is the master gate for BOTH automatic sends
        // (tick_position_report()) and this manual "Send Position Now" path —
        // blocking it here (before any position is even looked up) stops the
        // GUI's Send flow at its one common choke point.
        if (!ctrl.get_config().position_report_enabled) {
            mj::Value r = make_reply(msg, false);
            r.set("msg", mj::Value::string("position reporting is disabled — enable it in Settings"));
            return mj::dump(r);
        }
        const std::string object = msg.has("object") ? msg.get_string("object")
                                                       : ctrl.get_primary_self_address();

        double lat_deg = 0.0, lon_deg = 0.0;
        bool have_position = false;
        if (ctrl.has_gps_fix()) {
            lat_deg = ctrl.get_gps_lat();
            lon_deg = ctrl.get_gps_lon();
            have_position = true;
        } else {
            lat_deg = ctrl.get_station_lat();
            lon_deg = ctrl.get_station_lon();
            have_position = (ctrl.get_position_source() != ALEStationConfig::PositionSource::NONE);
        }
        if (!have_position) {
            mj::Value r = make_reply(msg, false);
            r.set("msg", mj::Value::string("no station position available (set a position source first)"));
            return mj::dump(r);
        }

        // Altitude: only ever available from a live GPS fix (not manual/grid).
        bool has_altitude = false;
        double altitude = 0.0;
        if (msg.has("altitude")) {
            has_altitude = true;
            altitude = msg.get_number("altitude");
        } else if (ctrl.has_gps_fix() && ctx.gps_svc && ctx.gps_svc->has_altitude()) {
            has_altitude = true;
            altitude = ctx.gps_svc->alt();
        }
        const char altitude_unit = msg.has("altitude_unit")
            ? msg.get_string("altitude_unit", "M")[0] : 'M';

        const std::string comment = msg.get_string("comment");
        const std::time_t now = std::time(nullptr);

        const std::string gpr_text = ale::generate_gpr(
            object, lat_deg, lon_deg, has_altitude, altitude, altitude_unit,
            now, 'Z', comment);

        mj::Value r = make_reply(msg, true);
        r.set("gpr_text", mj::Value::string(gpr_text));
        return mj::dump(r);
    }
    if (cmd == "MANUAL_ACCEPT_MODE") {
        ctrl.set_manual_accept_mode(msg.get_bool("on"),
            static_cast<uint32_t>(msg.get_number("timeout_ms", 10000)));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "MANUAL_ACCEPT_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("on",         mj::Value::boolean(ctrl.get_manual_accept_mode()));
        r.set("timeout_ms", mj::Value::number(ctrl.get_accept_timeout_ms()));
        return mj::dump(r);
    }

    // ── Channels ─────────────────────────────────────────────────────────
    if (cmd == "CHANNELS_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& c : ctrl.channels()) list.push_back(channel_to_json(c));
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "CHANNEL_ADD") {
        Channel ch(static_cast<uint32_t>(msg.get_number("rx_hz")),
                   static_cast<uint32_t>(msg.get_number("tx_hz", msg.get_number("rx_hz"))),
                   msg.get_string("mode", "USB"), msg.get_string("mode", "USB"));
        ch.id = msg.get_string("id");
        ch.label = msg.get_string("label");
        if (msg.has("enabled"))           ch.enabled           = msg.get_bool("enabled", true);
        if (msg.has("rx_only"))           ch.rx_only           = msg.get_bool("rx_only", false);
        if (msg.has("tx_only"))           ch.tx_only           = msg.get_bool("tx_only", false);
        if (msg.has("voice_use"))         ch.voice_use         = msg.get_bool("voice_use", true);
        if (msg.has("data_use"))          ch.data_use          = msg.get_bool("data_use", true);
        if (msg.has("inhibit_calling"))   ch.inhibit_calling   = msg.get_bool("inhibit_calling", false);
        if (msg.has("inhibit_sounding"))  ch.inhibit_sounding  = msg.get_bool("inhibit_sounding", false);
        if (msg.has("inhibit_reporting")) ch.inhibit_reporting = msg.get_bool("inhibit_reporting", false);
        if (msg.has("ale_only"))          ch.ale_only          = msg.get_bool("ale_only", false);
        if (msg.has("power_pct"))
            ch.power_pct = static_cast<uint8_t>(
                std::clamp(msg.get_number("power_pct", 100.0), 0.0, 100.0));
        const bool ok = ctrl.add_channel(ch);
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "CHANNEL_DEL") {
        return pc(msg, ctrl, "CMD:DEL_CHANNEL "
            + std::to_string(static_cast<uint32_t>(msg.get_number("rx_hz"))));
    }
    if (cmd == "CHANNEL_RENAME") {
        return pc(msg, ctrl, "CMD:RENAME_CHANNEL "
            + msg.get_string("old_id") + " " + msg.get_string("new_id"));
    }
    if (cmd == "STATION_LOAD" || cmd == "CHANNELS_LOAD") {
        const std::string resolved = resolve_data_path(msg.get_string("path"));
        // load_channel_file (not load_station_file): arms overlay persistence so
        // station.state references this file instead of duplicating its channels.
        const bool ok = ctrl.load_channel_file(resolved);
        if (ok) {
            pal::log_info("openALE", "Station file loaded from %s", resolved.c_str());
            // Persist the reference immediately so the loaded preset survives a
            // restart even if no other mutation follows (station.state records
            // channel_file= + the operator's nets/contacts/settings, not the
            // channel rows — see ALEController::save_state's overlay mode).
            if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        }
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string(
            file_exists(resolved) ? "invalid or corrupt channel file" : "file not found"));
        return mj::dump(r);
    }
    if (cmd == "STATION_SAVE" || cmd == "CHANNELS_SAVE") {
        const bool ok = ctrl.save_station_file(msg.get_string("path"));
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not write file (check path/permissions)"));
        return mj::dump(r);
    }
    if (cmd == "NETS_FILES_LIST") {
        mj::Value r = make_reply(msg, true);
        r.set("data", string_array(list_net_files()));
        return mj::dump(r);
    }

    // ── Nets ─────────────────────────────────────────────────────────────
    if (cmd == "NETS_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& n : ctrl.nets()) list.push_back(net_to_json(n));
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "NET_ADD")      { return pc(msg, ctrl, "CMD:ADD_NET " + msg.get_string("name")); }
    if (cmd == "NET_DEL")      { return pc(msg, ctrl, "CMD:DEL_NET " + msg.get_string("name")); }
    if (cmd == "NET_RENAME") {
        return pc(msg, ctrl, "CMD:RENAME_NET " + msg.get_string("old_name") + " " + msg.get_string("new_name"));
    }
    if (cmd == "NET_ASSIGN") {
        return pc(msg, ctrl, "CMD:ASSIGN_CHANNEL " + msg.get_string("net") + " " + msg.get_string("channel_id"));
    }
    if (cmd == "NET_UNASSIGN") {
        return pc(msg, ctrl, "CMD:UNASSIGN_CHANNEL " + msg.get_string("net") + " " + msg.get_string("channel_id"));
    }
    if (cmd == "NET_UPDATE") {
        Net updated;
        updated.name                 = msg.get_string("name");
        updated.dwell_ms             = static_cast<uint32_t>(msg.get_number("dwell_ms", 200));
        updated.scanning_enabled     = msg.get_bool("scanning_enabled", true);
        updated.sounding_enabled     = msg.get_bool("sounding_enabled", false);
        updated.sounding_interval_sec= static_cast<uint32_t>(msg.get_number("sounding_interval_sec", 300));
        updated.calling_length_c     = static_cast<uint32_t>(msg.get_number("calling_length_c", 10));
        return mj::dump(make_reply(msg, ctrl.update_net(updated)));
    }
    if (cmd == "SCAN_NET_SET")     { return pc(msg, ctrl, "CMD:SET_SCAN_NET " + msg.get_string("net")); }
    if (cmd == "SCAN_NET_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("net", mj::Value::string(ctrl.get_active_scan_net()));
        return mj::dump(r);
    }

    // ── Contacts ─────────────────────────────────────────────────────────
    if (cmd == "CONTACTS_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& [addr, name] : ctrl.get_address_book().all_stations()) {
            mj::Value v = mj::obj();
            v.set("callsign", mj::Value::string(addr));
            v.set("name",     mj::Value::string(name));
            list.push_back(std::move(v));
        }
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "CONTACT_ADD") {
        const auto cs   = msg.get_string("callsign");
        const auto name = msg.get_string("name");
        if (cs.empty()) return mj::dump(make_reply(msg, false));
        return mj::dump(make_reply(msg, ctrl.add_contact(cs, name)));
    }
    if (cmd == "CONTACT_DEL")    { return pc(msg, ctrl, "CMD:DEL_CONTACT "    + msg.get_string("callsign")); }
    if (cmd == "CONTACT_SELECT") { return pc(msg, ctrl, "CMD:SELECT_CONTACT " + msg.get_string("callsign")); }

    // ── Self addresses ───────────────────────────────────────────────────
    if (cmd == "SELF_ADDR_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& line : ctrl.get_all_self_addresses()) list.push_back(self_addr_line_to_json(line));
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "SELF_ADDR_ADD") {
        const bool ok = ctrl.add_self_address(msg.get_string("addr"),
            msg.get_string("status", "enabled"), msg.get_string("valid_channels", "ALL"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "SELF_ADDR_DEL")         { return pc(msg, ctrl, "CMD:DEL_SELF_ADDR "     + msg.get_string("addr")); }
    if (cmd == "SELF_ADDR_SET_PRIMARY") { return pc(msg, ctrl, "CMD:SET_PRIMARY_ADDR " + msg.get_string("addr")); }

    // ── LQA / signal quality ─────────────────────────────────────────────
    if (cmd == "LQA_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& line : ctrl.get_all_lqa_entries()) list.push_back(lqa_line_to_json(line));
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "LQA_CLEAR") {
        ctrl.process_command("CMD:CLEAR_LQA");
        if (!ctx.lqa_path.empty()) ctrl.save_lqa(ctx.lqa_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "LQA_SET") {
        // lqa_enabled: record per-frame FROM-direction BER/SNR for every received
        // transmission after word sync into the LQA Memory (A.5.4.1.1).
        if (msg.has("lqa_enabled")) ctrl.set_lqa_enabled(msg.get_bool("lqa_enabled"));
        // lqa_exchange_enabled: active bilateral CMD 'a' (LQA request) exchange
        // sent during calling/handshake (A.5.4.2). false = EMCON/Debug.
        if (msg.has("lqa_exchange_enabled"))
            ctrl.set_lqa_exchange_enabled(msg.get_bool("lqa_exchange_enabled"));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "LQA_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("lqa_enabled", mj::Value::boolean(ctrl.lqa_enabled()));
        r.set("lqa_exchange_enabled", mj::Value::boolean(ctrl.lqa_exchange_enabled()));
        return mj::dump(r);
    }

    // ── LBT occupancy detection (A.5.4.7) ────────────────────────────────
    // margin_db: busy threshold in dB over the tracked noise floor (operator-
    // settable for local noise conditions). occupancy_enabled: master switch
    // for the broadband busy detector. override: A.5.4.7.3 emergency override
    // (busy results ignored; the LBT pause itself still runs).
    if (cmd == "LBT_SET") {
        if (msg.has("margin_db"))
            ctrl.set_lbt_margin_db(static_cast<float>(msg.get_number("margin_db")));
        if (msg.has("occupancy_enabled"))
            ctrl.set_lbt_occupancy_enabled(msg.get_bool("occupancy_enabled"));
        if (msg.has("override"))
            ctrl.set_lbt_override(msg.get_bool("override"));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "LBT_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("margin_db",         mj::Value::number(ctrl.lbt_margin_db()));
        r.set("occupancy_enabled", mj::Value::boolean(ctrl.lbt_occupancy_enabled()));
        r.set("override",          mj::Value::boolean(ctrl.lbt_override()));
        r.set("busy",              mj::Value::boolean(ctrl.lbt_busy()));
        r.set("level_db",          mj::Value::number(ctrl.lbt_level_db()));
        r.set("floor_db",          mj::Value::number(ctrl.lbt_floor_db()));
        return mj::dump(r);
    }
    // §A.5.3.3 stage-1 operator squelch: calibrated sensitivity for the scan-stop
    // detector. enabled: opt-in (OFF ⇒ level-invariant detector, default). margin_db:
    // how far a signal must sit above the learned noise floor to stop scanning.
    if (cmd == "SCAN_DETECT_SET") {
        if (msg.has("enabled"))
            ctrl.set_scan_squelch_enabled(msg.get_bool("enabled"));
        if (msg.has("margin_db"))
            ctrl.set_scan_detect_margin_db(static_cast<float>(msg.get_number("margin_db")));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "SCAN_DETECT_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("enabled",      mj::Value::boolean(ctrl.scan_squelch_enabled()));
        r.set("margin_db",    mj::Value::number(ctrl.scan_detect_margin_db()));
        r.set("floor_db",     mj::Value::number(ctrl.scan_floor_db()));
        r.set("baseline_db",  mj::Value::number(ctrl.scan_floor_baseline_db()));
        return mj::dump(r);
    }
    if (cmd == "SCAN_DETECT_CALIBRATE") {
        const float snap = ctrl.calibrate_scan_detector();
        mj::Value r = make_reply(msg, true);
        r.set("baseline_db", mj::Value::number(snap));
        return mj::dump(r);
    }
    if (cmd == "RELINK_SET") {
        // relink_enabled: auto-renegotiate channel via TWAS + re-call when a
        // better channel is known post-LINKED (A.5.4.5 bilateral selection).
        if (msg.has("relink_enabled"))
            ctrl.set_relink_enabled(msg.get_bool("relink_enabled"));
        if (msg.has("relink_threshold"))
            ctrl.set_relink_threshold(static_cast<float>(msg.get_number("relink_threshold")));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "RELINK_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("relink_enabled", mj::Value::boolean(ctrl.relink_enabled()));
        r.set("relink_threshold", mj::Value::number(ctrl.relink_threshold()));
        return mj::dump(r);
    }
    if (cmd == "LOG_LEVEL_SET") {
        // level: 0=Off, 1=Error, 2=Info, 3=Debug, 4=Trace  (matches GUI cfgLogLevel)
        const int level = static_cast<int>(msg.get_number("level"));
        pal::hamlib_set_log_level(level);  // also updates pal::get_logger()->set_level()
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "FREQ_SELECT_SET") {
        if (msg.has("enhanced_freq_select"))
            ctrl.set_enhanced_freq_select(msg.get_bool("enhanced_freq_select"));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "FREQ_SELECT_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("enhanced_freq_select", mj::Value::boolean(ctrl.enhanced_freq_select()));
        return mj::dump(r);
    }
    if (cmd == "SIGNAL_QUALITY") {
        const auto q = ctrl.get_current_signal_quality();
        mj::Value r = make_reply(msg, true);
        r.set("snr_db", mj::Value::number(q.snr_db));
        r.set("sinad_db", mj::Value::number(q.sinad_db));
        r.set("ber", mj::Value::number(q.ber));
        r.set("multipath_ms", mj::Value::number(q.multipath_ms));
        r.set("votes", mj::Value::number(q.votes));
        r.set("fec_errors", mj::Value::number(q.fec_errors));
        r.set("word_locked", mj::Value::boolean(q.word_locked));
        r.set("decoding", mj::Value::boolean(q.decoding));
        return mj::dump(r);
    }

    // ── VFO / radio ──────────────────────────────────────────────────────
    if (cmd == "VFO_GET") {
        // Sync from radio before reading so the reply reflects actual radio
        // state (freq/mode the operator may have changed in Quisk or on
        // hardware), not just the last value openALE commanded.
        ctrl.sync_radio_state();
        mj::Value r = make_reply(msg, true);
        r.set("freq_hz", mj::Value::number(ctrl.get_current_frequency()));
        r.set("mode", mj::Value::string(ctrl.get_current_mode()));
        r.set("tune_step_hz", mj::Value::number(ctrl.get_tune_step()));
        r.set("ptt", mj::Value::boolean(ctrl.get_ptt_state()));
        r.set("power_pct", mj::Value::number(ctrl.get_current_power()));
        r.set("power_supported", mj::Value::boolean(ctrl.power_control_supported()));
        return mj::dump(r);
    }
    if (cmd == "VFO_SET_FREQ")      { return mj::dump(make_reply(msg, ctrl.set_frequency(static_cast<uint32_t>(msg.get_number("hz"))))); }
    if (cmd == "VFO_SET_MODE")      { return mj::dump(make_reply(msg, ctrl.set_mode(msg.get_string("mode")))); }
    if (cmd == "VFO_SET_CHANNEL")   { return mj::dump(make_reply(msg, ctrl.set_vfo_channel(static_cast<uint32_t>(msg.get_number("hz")), msg.get_string("mode")))); }
    if (cmd == "VFO_SET_POWER")     { return mj::dump(make_reply(msg, ctrl.set_power(static_cast<int>(msg.get_number("pct"))))); }
    if (cmd == "VFO_STEP")          { return mj::dump(make_reply(msg, ctrl.step_channel(static_cast<int>(msg.get_number("direction"))))); }
    if (cmd == "VFO_NUDGE")         { ctrl.nudge_frequency(static_cast<int>(msg.get_number("direction"))); return mj::dump(make_reply(msg, true)); }
    if (cmd == "VFO_SET_TUNE_STEP") { ctrl.set_tune_step(static_cast<uint32_t>(msg.get_number("hz"))); return mj::dump(make_reply(msg, true)); }

    // ── Timing (Level-5 programmable defaults only — see docs/GUI_BRIDGE_GAPS.md) ──
    if (cmd == "TIMING_SET") {
        if (msg.has("scan_dwell_ms"))          ctrl.set_scan_dwell_ms(static_cast<uint32_t>(msg.get_number("scan_dwell_ms")));
        if (msg.has("sounding_interval_sec"))  ctrl.set_sounding_interval_sec(static_cast<uint32_t>(msg.get_number("sounding_interval_sec")));
        if (msg.has("link_idle_timeout_sec"))  ctrl.set_link_idle_timeout_sec(static_cast<uint32_t>(msg.get_number("link_idle_timeout_sec")));
        if (msg.has("amd_send_max_attempts"))  ctrl.set_amd_send_max_attempts(static_cast<uint32_t>(msg.get_number("amd_send_max_attempts")));
        if (msg.has("max_tune_time_ms"))       ctrl.set_max_tune_time_ms(static_cast<uint32_t>(msg.get_number("max_tune_time_ms")));
        if (msg.has("ptt_lead_ms"))            ctrl.set_ptt_lead_ms(static_cast<uint32_t>(msg.get_number("ptt_lead_ms")));
        if (msg.has("ptt_tail_ms"))            ctrl.set_ptt_tail_ms(static_cast<uint32_t>(msg.get_number("ptt_tail_ms")));
        if (msg.has("assumed_scan_channels"))  ctrl.set_assumed_scan_channels(static_cast<uint32_t>(msg.get_number("assumed_scan_channels")));
        if (msg.has("sounding_use_twas"))          ctrl.set_sounding_use_twas(msg.get_bool("sounding_use_twas"));
        if (msg.has("sounding_warning_lead_sec"))        ctrl.set_sounding_warning_lead_sec(static_cast<uint32_t>(msg.get_number("sounding_warning_lead_sec")));
        if (msg.has("test_channel_link_hold_time"))      ctrl.set_test_channel_link_hold_time(static_cast<uint32_t>(msg.get_number("test_channel_link_hold_time")));
        if (msg.has("link_default_individual"))          ctrl.set_link_default_individual(msg.get_bool("link_default_individual"));
        if (msg.has("link_default_group"))                ctrl.set_link_default_group(msg.get_bool("link_default_group"));
        if (msg.has("link_default_allcall"))              ctrl.set_link_default_allcall(msg.get_bool("link_default_allcall"));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "TIMING_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("scan_dwell_ms",                    mj::Value::number(ctrl.get_scan_dwell_ms()));
        r.set("sounding_interval_sec",            mj::Value::number(ctrl.get_sounding_interval_sec()));
        r.set("link_idle_timeout_sec",            mj::Value::number(ctrl.get_link_idle_timeout_sec()));
        r.set("amd_send_max_attempts",            mj::Value::number(ctrl.get_amd_send_max_attempts()));
        r.set("max_tune_time_ms",                 mj::Value::number(ctrl.get_max_tune_time_ms()));
        r.set("ptt_lead_ms",                      mj::Value::number(ctrl.get_ptt_lead_ms()));
        r.set("ptt_tail_ms",                      mj::Value::number(ctrl.get_ptt_tail_ms()));
        r.set("assumed_scan_channels",            mj::Value::number(ctrl.get_assumed_scan_channels()));
        r.set("sounding_use_twas",                mj::Value::boolean(ctrl.get_sounding_use_twas()));
        r.set("sounding_warning_lead_sec",        mj::Value::number(ctrl.get_sounding_warning_lead_sec()));
        r.set("test_channel_link_hold_time",      mj::Value::number(ctrl.get_test_channel_link_hold_time()));
        r.set("link_default_individual",          mj::Value::boolean(ctrl.get_link_default_individual()));
        r.set("link_default_group",               mj::Value::boolean(ctrl.get_link_default_group()));
        r.set("link_default_allcall",             mj::Value::boolean(ctrl.get_link_default_allcall()));
        return mj::dump(r);
    }

    // ── Station location / propagation context ───────────────────────────
    if (cmd == "STATION_LOC_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("position_source", mj::Value::number(
            static_cast<double>(static_cast<int>(ctrl.get_position_source()))));
        r.set("lat_deg",      mj::Value::number(ctrl.get_station_lat()));
        r.set("lon_deg",      mj::Value::number(ctrl.get_station_lon()));
        r.set("grid_locator", mj::Value::string(ctrl.get_grid_locator()));
        r.set("gpsd_host",    mj::Value::string(ctrl.get_config().gpsd_host));
        r.set("gpsd_port",    mj::Value::number(ctrl.get_config().gpsd_port));
        r.set("nmea_port",    mj::Value::string(ctrl.get_config().nmea_port));
        r.set("nmea_baud",    mj::Value::number(ctrl.get_config().nmea_baud));
        r.set("sfi_enabled",  mj::Value::boolean(ctrl.get_config().sfi_enabled));
        r.set("has_fix",      mj::Value::boolean(ctrl.has_gps_fix()));
        r.set("fix_lat",      mj::Value::number(ctrl.get_gps_lat()));
        r.set("fix_lon",      mj::Value::number(ctrl.get_gps_lon()));
        r.set("receiving",    mj::Value::boolean(ctx.gps_svc && ctx.gps_svc->is_receiving()));
        r.set("sfi",          mj::Value::number(ctrl.get_current_sfi()));
        return mj::dump(r);
    }
    if (cmd == "STATION_LOC_SET") {
        using PS = ALEStationConfig::PositionSource;
        PS src = ctrl.get_position_source();
        if (msg.has("position_source"))
            src = static_cast<PS>(static_cast<int>(msg.get_number("position_source")));
        // Apply the resolved source first — set_station_position_manual()/
        // set_station_position_grid() below also assign position_source as a
        // side effect, so only invoke them when src actually selects that mode.
        // Otherwise (GPS/GPSD/NONE) this call is what makes src stick.
        ctrl.set_position_source(src);
        if (src == PS::MANUAL && msg.has("lat_deg") && msg.has("lon_deg"))
            ctrl.set_station_position_manual(msg.get_number("lat_deg"),
                                             msg.get_number("lon_deg"));
        if (src == PS::MAIDENHEAD && msg.has("grid_locator"))
            ctrl.set_station_position_grid(msg.get_string("grid_locator"));
        if (msg.has("gpsd_host") || msg.has("gpsd_port")) {
            const auto& cfg = ctrl.get_config();
            ctrl.set_gpsd_config(
                msg.has("gpsd_host") ? msg.get_string("gpsd_host") : cfg.gpsd_host,
                msg.has("gpsd_port") ? static_cast<uint16_t>(msg.get_number("gpsd_port"))
                                     : cfg.gpsd_port);
        }
        if (msg.has("nmea_port") || msg.has("nmea_baud")) {
            const auto& cfg = ctrl.get_config();
            ctrl.set_nmea_config(
                msg.has("nmea_port") ? msg.get_string("nmea_port") : cfg.nmea_port,
                msg.has("nmea_baud") ? static_cast<uint32_t>(msg.get_number("nmea_baud"))
                                     : cfg.nmea_baud);
        }
        if (msg.has("sfi_enabled")) {
            ALEStationConfig cfg = ctrl.get_config();
            cfg.sfi_enabled = msg.get_bool("sfi_enabled");
            ctrl.apply_config(cfg);
        }
        restart_location_services(ctx, ctrl);
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }

    // ── Automatic ALE-GPR/GGA position reporting (docs/ALE_GPR_SPEC.md) ────
    if (cmd == "POSITION_REPORT_GET") {
        const auto& cfg = ctrl.get_config();
        mj::Value r = make_reply(msg, true);
        r.set("enabled",       mj::Value::boolean(cfg.position_report_enabled));
        r.set("mode",          mj::Value::number(static_cast<double>(static_cast<int>(cfg.position_report_mode))));
        r.set("target",        mj::Value::string(cfg.position_report_target));
        r.set("net",           mj::Value::string(cfg.position_report_net));
        r.set("change_m",      mj::Value::number(cfg.position_report_change_m));
        r.set("interval_min",  mj::Value::number(cfg.position_report_interval_min));
        r.set("format",        mj::Value::number(static_cast<double>(static_cast<int>(cfg.position_report_format))));
        r.set("comment",       mj::Value::string(cfg.position_report_comment));
        return mj::dump(r);
    }
    if (cmd == "POSITION_REPORT_SET") {
        using Mode   = ALEStationConfig::PositionReportMode;
        using Format = ALEStationConfig::PositionReportFormat;
        ALEStationConfig cfg = ctrl.get_config();
        if (msg.has("enabled"))      cfg.position_report_enabled = msg.get_bool("enabled");
        if (msg.has("mode"))
            cfg.position_report_mode = static_cast<Mode>(static_cast<int>(msg.get_number("mode")));
        if (msg.has("target"))       cfg.position_report_target = msg.get_string("target");
        if (msg.has("net"))          cfg.position_report_net = msg.get_string("net");
        if (msg.has("change_m"))
            cfg.position_report_change_m = static_cast<uint32_t>(msg.get_number("change_m"));
        if (msg.has("interval_min"))
            cfg.position_report_interval_min = static_cast<uint32_t>(msg.get_number("interval_min"));
        if (msg.has("format"))
            cfg.position_report_format = static_cast<Format>(static_cast<int>(msg.get_number("format")));
        if (msg.has("comment"))      cfg.position_report_comment = msg.get_string("comment");
        ctrl.apply_config(cfg);
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }

    // ── Location Relay (docs/LOCATION_SHARING_CONCEPT.md) ──────────────────
    if (cmd == "LOCATION_SHARING_GET") {
        const auto& cfg = ctrl.get_config();
        mj::Value r = make_reply(msg, true);
        r.set("enabled",          mj::Value::boolean(cfg.location_sharing_enabled));
        r.set("url",              mj::Value::string(cfg.location_api_url));
        // Ed25519 identity replaces the shared bearer token — nothing secret
        // to hide here; the public key is meant to be read aloud/pasted to
        // the relay operator, and the seed never leaves location_relay_identity.key.
        r.set("identity_exists",  mj::Value::boolean(ctx.loc_svc && !ctx.loc_svc->identity_callsign().empty()));
        r.set("callsign",         mj::Value::string(ctx.loc_svc ? ctx.loc_svc->identity_callsign() : std::string()));
        r.set("public_key",       mj::Value::string(ctx.loc_svc ? ctx.loc_svc->identity_public_key_b64() : std::string()));
        r.set("registration_status", mj::Value::string(loc_reg_state_name(
                                    ctx.loc_svc ? ctx.loc_svc->registration_state()
                                                 : ale::LocationRelayService::REG_UNKNOWN)));
        r.set("ca_cert_path",     mj::Value::string(cfg.location_ca_cert_path));
        r.set("allcall",          mj::Value::boolean(cfg.location_sharing_allcall));
        r.set("individual",       mj::Value::boolean(cfg.location_sharing_individual));
        r.set("net",              mj::Value::boolean(cfg.location_sharing_net));
        r.set("group",            mj::Value::boolean(cfg.location_sharing_group));
        r.set("linked",           mj::Value::boolean(cfg.location_sharing_linked));
        r.set("min_interval_sec", mj::Value::number(cfg.location_sharing_min_interval_sec));
        r.set("round_digits",     mj::Value::number(cfg.location_sharing_round_digits));
        r.set("include_comment",  mj::Value::boolean(cfg.location_sharing_include_comment));
        r.set("queue_size",       mj::Value::number(cfg.location_sharing_queue_size));
        r.set("running",          mj::Value::boolean(ctx.loc_svc && ctx.loc_svc->is_running()));
        r.set("conn_state",       mj::Value::string(loc_conn_state_name(
                                    ctx.loc_svc ? ctx.loc_svc->conn_state()
                                                 : ale::LocationRelayService::CS_UNKNOWN)));
        return mj::dump(r);
    }
    if (cmd == "LOCATION_SHARING_SET") {
        ALEStationConfig cfg = ctrl.get_config();
        if (msg.has("enabled"))    cfg.location_sharing_enabled = msg.get_bool("enabled");
        if (msg.has("url")) {
            const std::string url = msg.get_string("url");
            if (!url.empty() && !ale::location_url_allowed(url)) {
                mj::Value r = make_reply(msg, false);
                r.set("error", mj::Value::string("URL must be https://, or http://127.0.0.1 for local testing"));
                return mj::dump(r);
            }
            cfg.location_api_url = url;
        }
        if (msg.has("ca_cert_path"))      cfg.location_ca_cert_path = msg.get_string("ca_cert_path");
        if (msg.has("allcall"))          cfg.location_sharing_allcall = msg.get_bool("allcall");
        if (msg.has("individual"))       cfg.location_sharing_individual = msg.get_bool("individual");
        if (msg.has("net"))              cfg.location_sharing_net = msg.get_bool("net");
        if (msg.has("group"))            cfg.location_sharing_group = msg.get_bool("group");
        if (msg.has("linked"))           cfg.location_sharing_linked = msg.get_bool("linked");
        if (msg.has("min_interval_sec"))
            cfg.location_sharing_min_interval_sec = static_cast<uint32_t>(msg.get_number("min_interval_sec"));
        if (msg.has("round_digits"))
            cfg.location_sharing_round_digits = static_cast<uint8_t>(msg.get_number("round_digits"));
        if (msg.has("include_comment"))  cfg.location_sharing_include_comment = msg.get_bool("include_comment");
        if (msg.has("queue_size"))
            cfg.location_sharing_queue_size = static_cast<uint16_t>(msg.get_number("queue_size"));
        ctrl.apply_config(cfg);
        // "Regenerate Identity" (GUI button): delete the persisted key file
        // so restart_location_services()'s start() generates a fresh Ed25519
        // keypair and re-registers as pending. Used when an operator wants
        // to rotate a compromised or ambiguous identity without touching the
        // callsign.
        if (msg.has("regenerate_identity") && msg.get_bool("regenerate_identity")) {
            const std::string path = !cfg.location_relay_identity_path.empty()
                ? cfg.location_relay_identity_path
                : (ctx.state_path.empty() ? "location_relay_identity.key"
                                           : ctx.state_path.substr(0, ctx.state_path.find_last_of("/\\") + 1)
                                             + "location_relay_identity.key");
            std::remove(path.c_str());
            pal::log_info("openALE", "Location Relay: identity regenerated (old key file removed)");
        }
        restart_location_services(ctx, ctrl);
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }

    // ── Tuner (rigctld/netrigctl-compat read-only frequency server) ────────
    if (cmd == "RIGCTLD_GET") {
        mj::Value r = make_reply(msg, true);
        const auto& cfg = ctrl.get_config();
        r.set("enabled",     mj::Value::boolean(cfg.rigctld_server_enabled));
        r.set("port",        mj::Value::number(cfg.rigctld_server_port));
        r.set("bind_remote", mj::Value::boolean(cfg.rigctld_server_bind_remote));
        r.set("running",     mj::Value::boolean(ctx.rigctld && ctx.rigctld->is_running()));
        r.set("clients",     mj::Value::number(ctx.rigctld ? static_cast<double>(ctx.rigctld->client_count()) : 0));
        return mj::dump(r);
    }
    if (cmd == "RIGCTLD_SET") {
        ALEStationConfig cfg = ctrl.get_config();
        if (msg.has("enabled"))     cfg.rigctld_server_enabled = msg.get_bool("enabled");
        if (msg.has("port"))        cfg.rigctld_server_port = static_cast<uint16_t>(msg.get_number("port"));
        if (msg.has("bind_remote")) cfg.rigctld_server_bind_remote = msg.get_bool("bind_remote");
        ctrl.apply_config(cfg);
        restart_rigctld_server(ctx, ctrl);
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }

    // ── FEC ──────────────────────────────────────────────────────────────
    if (cmd == "FEC_SET") {
        if (msg.has("golay_mode"))          ctrl.set_golay_mode(static_cast<GolayMode>(static_cast<int>(msg.get_number("golay_mode"))));
        if (msg.has("min_unanimous_votes")) ctrl.set_min_unanimous_votes(static_cast<uint8_t>(msg.get_number("min_unanimous_votes")));
        if (msg.has("adaptive_fec"))        ctrl.set_adaptive_fec(msg.get_bool("adaptive_fec"));
        if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "FEC_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("golay_mode", mj::Value::number(static_cast<int>(ctrl.golay_mode())));
        r.set("min_unanimous_votes", mj::Value::number(ctrl.min_unanimous_votes()));
        r.set("adaptive_fec", mj::Value::boolean(ctrl.adaptive_fec()));
        return mj::dump(r);
    }

    // ── Audio / rig ──────────────────────────────────────────────────────
    if (cmd == "AUDIO_DEVICES") {
        // Enumerate via a throwaway device — must work BEFORE any device is
        // attached (ctrl.enumerate_audio_*() returns empty when none attached).
        std::vector<std::string> inputs, outputs;
        for (const auto& d : make_audio_device()->list_devices()) {
            if (d.rfind("IN:", 0) == 0)       inputs.push_back(d);
            else if (d.rfind("OUT:", 0) == 0) outputs.push_back(d);
        }
        mj::Value r = make_reply(msg, true);
        r.set("inputs", string_array(inputs));
        r.set("outputs", string_array(outputs));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_OPEN") {
        const std::string in_dev  = msg.get_string("in");
        const std::string out_dev = msg.get_string("out");
        const bool ok = open_audio(ctx, ctrl, in_dev, out_dev);
        if (ok) {
            // Persist the device selection so it survives a restart — see
            // audio_auto_open doc in ale_station_config.h. Same pattern as
            // RIG_CONNECT / RIGCTLD_SET: start from the current config so
            // unrelated settings are untouched.
            ALEStationConfig cfg = ctrl.get_config();
            cfg.audio_in        = in_dev;
            cfg.audio_out       = out_dev;
            cfg.audio_auto_open = true;
            ctrl.apply_config(cfg);
            if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        }
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not open audio device(s)"));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_CLOSE") {
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); ctx.audio->reset(); }
        // An explicit close disarms auto-reopen-on-startup — keep the saved
        // device names (AUDIO_CONFIG_GET still hydrates the GUI with them,
        // and SETTINGS_EXPORT still records "last successfully opened") but
        // don't silently reopen them again on the next launch.
        if (ctrl.get_config().audio_auto_open) {
            ALEStationConfig cfg = ctrl.get_config();
            cfg.audio_auto_open = false;
            ctrl.apply_config(cfg);
            if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        }
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AUDIO_CONFIG_GET") {
        // Saved device selection for the GUI to hydrate the Audio panel on
        // load — see audio_auto_open doc in ale_station_config.h. Distinct
        // from AUDIO_DEVICES (enumerates what's available) and AUDIO_LEVEL
        // (live meter) — this reports the saved configuration plus whether
        // it's currently attached.
        mj::Value r = make_reply(msg, true);
        const auto& cfg = ctrl.get_config();
        r.set("in",           mj::Value::string(cfg.audio_in));
        r.set("out",          mj::Value::string(cfg.audio_out));
        r.set("auto_open",    mj::Value::boolean(cfg.audio_auto_open));
        r.set("connected",    mj::Value::boolean(static_cast<bool>(*ctx.audio)));
        return mj::dump(r);
    }
    // ── Voice passthrough (dynamic audio-path management) ───────────────────
    // VOICE_ARM arms/disarms voice capability. When armed, a link_established
    // event flips the VAC from modem-exclusive to transparent voice passthrough;
    // link_terminated flips it back. See docs/VOICE_AUDIO_ROUTING.md.
    if (cmd == "VOICE_ARM") {
        ctx.voice_armed = msg.get_bool("on", false);
        if (ctx.voice) ctx.voice->arm(ctx.voice_armed);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "VOICE_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("armed", mj::Value::boolean(ctx.voice_armed));
        if (ctx.voice) {
            const auto m = ctx.voice->mode();
            r.set("mode", mj::Value::string(m == VoicePathManager::Mode::VOICE_PASSTHROUGH ? "voice" : "ale"));
            r.set("ptt",  mj::Value::boolean(ctx.voice->ptt()));
        }
        return mj::dump(r);
    }
    // AUDIO_MONITOR arms/disarms the link-independent RX "listen in" tap — lets
    // the operator hear the channel from the Operator Audio Interface regardless
    // of ALE link state or Voice Passthrough arm state. Session-only: never
    // persisted, always defaults off on bridge start/restart (see AudioMonitor).
    if (cmd == "AUDIO_MONITOR") {
        if (ctx.audio_monitor) ctx.audio_monitor->arm(msg.get_bool("on", false));
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AUDIO_MONITOR_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("on", mj::Value::boolean(ctx.audio_monitor && ctx.audio_monitor->armed()));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_SET_VOL") {
        float vol = static_cast<float>(msg.get_number("vol", 0.25));
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        ctx.tx_volume = vol;
        if (*ctx.audio) (*ctx.audio)->set_tx_volume(vol);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "AUDIO_LEVEL") {
        mj::Value r = make_reply(msg, true);
        r.set("level", mj::Value::number(ctrl.get_audio_input_level()));
        return mj::dump(r);
    }
    if (cmd == "RIG_TEST") {
        // Non-committal reachability probe — does NOT attach the radio to the
        // controller or change the live connection. If a radio is already
        // attached, test that one (a 2nd connection to the same port/host would
        // just fail); otherwise bring up a throwaway radio, probe, tear it down.
        if (*ctx.radio) {
            mj::Value r = make_reply(msg, true);
            r.set("reachable", mj::Value::boolean(ctrl.test_rig_connection()));
            r.set("status", mj::Value::string(ctrl.get_rig_connection_status()));
            return mj::dump(r);
        }
        const std::string spec = build_radio_spec(msg);
        if (spec.empty()) {
            mj::Value r = make_reply(msg, true);
            r.set("reachable", mj::Value::boolean(false));
            r.set("status", mj::Value::string("no model selected"));
            return mj::dump(r);
        }
        auto probe = pal::create_radio(spec);
        const bool reachable = probe && probe->initialize() && probe->start() && probe->is_ready();
        if (probe) probe->stop();
        mj::Value r = make_reply(msg, true);
        r.set("reachable", mj::Value::boolean(reachable));
        r.set("status", mj::Value::string(reachable ? "reachable" : "unreachable"));
        if (!reachable) r.set("error", mj::Value::string("radio not reachable (" + spec + ")"));
        return mj::dump(r);
    }
    if (cmd == "RIG_CONNECT") {
        const std::string spec = build_radio_spec(msg);
        if (spec.empty()) {  // empty model → None / Offline, stay disconnected, succeed
            connect_radio(ctx, ctrl, spec);  // still tears down any existing radio
            mj::Value r = make_reply(msg, true);
            r.set("connected", mj::Value::boolean(false));
            r.set("status", mj::Value::string("not attached"));
            return mj::dump(r);
        }
        const bool ok = connect_radio(ctx, ctrl, spec);
        if (ok) {
            // Persist the connection so it survives a restart — see
            // rig_auto_connect doc in ale_station_config.h. Started from the
            // current config (not defaults) so unrelated settings are untouched,
            // same pattern as RIGCTLD_SET below.
            ALEStationConfig cfg = ctrl.get_config();
            cfg.rig_model        = msg.get_string("model", "");
            cfg.rig_host         = msg.get_string("host", "127.0.0.1");
            cfg.rig_port         = msg.get_string("port", "4532");
            cfg.rig_serial       = msg.get_string("serial", "");
            cfg.rig_baud         = static_cast<uint32_t>(msg.get_number("baud", 0));
            cfg.rig_dtr          = msg.get_string("dtr", "on");
            cfg.rig_rts          = msg.get_string("rts", "on");
            cfg.rig_stab         = static_cast<uint32_t>(msg.get_number("stab", 200));
            cfg.rig_ptt          = msg.get_string("ptt", "normal");
            cfg.rig_avoid_relay_click = msg.get_bool("split", false);
            cfg.rig_auto_connect = true;
            ctrl.apply_config(cfg);
            if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        }

        mj::Value r = make_reply(msg, ok);
        r.set("connected", mj::Value::boolean(ok && ctrl.test_rig_connection()));
        r.set("status", mj::Value::string(ctrl.get_rig_connection_status()));
        if (!ok) r.set("error", mj::Value::string("could not connect radio (" + spec + ")"));
        return mj::dump(r);
    }
    if (cmd == "RIG_DISCONNECT") {
        connect_radio(ctx, ctrl, "");  // tears down any existing radio, no-op if none attached
        // An explicit disconnect disarms auto-reconnect-on-startup — keep the
        // saved model/port fields (RIG_GET still hydrates the GUI with them)
        // but don't silently key the radio up again on the next launch.
        if (ctrl.get_config().rig_auto_connect) {
            ALEStationConfig cfg = ctrl.get_config();
            cfg.rig_auto_connect = false;
            ctrl.apply_config(cfg);
            if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path);
        }
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
    if (cmd == "RIG_GET") {
        // Saved connection settings (model/port/dtr/rts/ptt/...) for the GUI to
        // hydrate the Radio/CAT panel on load — see rig_auto_connect doc in
        // ale_station_config.h. Distinct from RIG_STATUS, which reports the
        // live attach state, not the saved configuration.
        mj::Value r = make_reply(msg, true);
        const auto& cfg = ctrl.get_config();
        r.set("model",        mj::Value::string(cfg.rig_model));
        r.set("host",         mj::Value::string(cfg.rig_host));
        r.set("port",         mj::Value::string(cfg.rig_port));
        r.set("serial",       mj::Value::string(cfg.rig_serial));
        r.set("baud",         mj::Value::number(cfg.rig_baud));
        r.set("dtr",          mj::Value::string(cfg.rig_dtr));
        r.set("rts",          mj::Value::string(cfg.rig_rts));
        r.set("stab",         mj::Value::number(cfg.rig_stab));
        r.set("ptt",          mj::Value::string(cfg.rig_ptt));
        r.set("split",        mj::Value::boolean(cfg.rig_avoid_relay_click));
        r.set("auto_connect", mj::Value::boolean(cfg.rig_auto_connect));
        return mj::dump(r);
    }
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

    // ── Settings ─────────────────────────────────────────────────────────
    if (cmd == "SETTINGS_EXPORT") {
        const std::string path = msg.get_string("path");
        const bool ok = ctrl.export_settings(path);
        if (ok && (!ctx.audio_in.empty() || !ctx.audio_out.empty() || ctx.voice_armed)) {
            std::ofstream fa(path, std::ios::app);
            fa << "audio_in="  << ctx.audio_in  << "\n";
            fa << "audio_out=" << ctx.audio_out << "\n";
            fa << "voice_armed=" << (ctx.voice_armed ? "1" : "0") << "\n";
        }
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not write file (check path/permissions)"));
        return mj::dump(r);
    }
    if (cmd == "SETTINGS_IMPORT") {
        const std::string path = resolve_data_path(msg.get_string("path"));
        // Extract bridge-level keys (audio devices, voice arm) before handing off to controller.
        std::string audio_in, audio_out;
        bool voice_armed = false;
        {
            std::ifstream fa(path);
            std::string ln;
            while (std::getline(fa, ln)) {
                if (ln.rfind("audio_in=", 0) == 0)       audio_in  = ln.substr(9);
                else if (ln.rfind("audio_out=", 0) == 0) audio_out = ln.substr(10);
                else if (ln.rfind("voice_armed=", 0) == 0) voice_armed = (ln.substr(12) == "1");
            }
        }
        const bool ok = ctrl.import_settings(path);
        if (ok && !audio_in.empty()) {
            if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); }
            *ctx.audio = make_audio_device();
            const bool aok = (*ctx.audio)->open(audio_in, audio_out);
            if (aok) { ctrl.set_audio_device(ctx.audio->get()); ctx.audio_in = audio_in; ctx.audio_out = audio_out; }
            else      { ctx.audio->reset(); ctx.audio_in.clear(); ctx.audio_out.clear(); }
        }
        // Restore voice-arm state (re-evaluates passthrough against current link).
        if (ok) {
            ctx.voice_armed = voice_armed;
            if (ctx.voice) ctx.voice->arm(voice_armed);
        }
        if (ok) restart_location_services(ctx, ctrl);
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string(
            file_exists(path) ? "invalid or corrupt configuration file" : "file not found"));
        return mj::dump(r);
    }

    // ── LQA database file ───────────────────────────────────────────────
    if (cmd == "LQA_EXPORT") {
        const std::string path = msg.get_string("path");
        const bool ok = ctrl.save_lqa(path);
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not write file (check path/permissions)"));
        return mj::dump(r);
    }
    if (cmd == "LQA_IMPORT") {
        const std::string path = resolve_data_path(msg.get_string("path"));
        const bool ok = ctrl.load_lqa(path);
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string(
            file_exists(path) ? "invalid or corrupt LQA file" : "file not found"));
        return mj::dump(r);
    }
    if (cmd == "DEBUG_RX")        { ctrl.set_debug_rx(msg.get_bool("on")); if (!ctx.state_path.empty()) ctrl.save_state(ctx.state_path); return mj::dump(make_reply(msg, true)); }
    if (cmd == "CAT_TRACE")       { ctrl.set_cat_trace(msg.get_bool("on")); return mj::dump(make_reply(msg, true)); }

    mj::Value r = make_reply(msg, false);
    r.set("error", mj::Value::string("unknown command: " + cmd));
    return mj::dump(r);
}

// ── Usage ───────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::fprintf(stderr, // NOLINT(pal-logger)
        "ALE 2G WebSocket bridge — connects apps/gui/ to a live ALEController\n"
        "\n"
        "Usage: %s --port N [--remote] [--webroot DIR]\n"
        "                    [--tls [--cert FILE --key FILE] [--tls-san HOST]...]\n"
        "\n"
        "  --port N     WebSocket listen port (required)\n"
        "  --remote     Bind to 0.0.0.0 (LAN-reachable)\n"
        "  --mobile     Serve apps/gui/mobile/ (for smartphones)\n"
        "  --webroot D  Serve static files from DIR\n"
        "  --data-dir D Store station.state/lqa.bin/Location Relay identity in DIR\n"
        "               instead of auto-resolving (portable dir if station.state\n"
        "               already exists there, else %%APPDATA%%\\openALE or\n"
        "               ~/.config/openALE)\n"
        "  --tls        Serve HTTPS/WSS instead of plain HTTP/WS (TLS-only on this\n"
        "               port). Required for getUserMedia/AudioWorklet/device listing\n"
        "               to work when the GUI is opened from another device (--remote) —\n"
        "               browsers only grant those APIs on a secure context. A\n"
        "               self-signed certificate is generated automatically on first\n"
        "               run (openale_cert.pem/openale_key.pem) unless --cert/--key\n"
        "               point at your own. See docs/TLS_SETUP.md.\n"
        "  --cert FILE  Certificate PEM path (default: openale_cert.pem)\n"
        "  --key FILE   Private key PEM path (default: openale_key.pem)\n"
        "  --tls-san H  Extra hostname/IP to embed as a certificate Subject\n"
        "               Alternative Name (repeatable). All local interface IPs plus\n"
        "               localhost/127.0.0.1/::1 are already included automatically —\n"
        "               only needed for addresses not visible as a local interface\n"
        "               (e.g. a reverse-proxy hostname). Only affects first-run\n"
        "               generation; ignored once cert/key files already exist.\n",
        prog);
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    pal::set_logger(pal::create_logger());
    pal::install_crash_handler();
    pal::set_event_handler(pal::create_event_handler());

    uint16_t         port        = 0;     // 0 = not set; --port is required
    bool             bind_remote = false;
    bool             serve_mobile = false;
    std::string      web_root;            // empty → auto-resolve below
    std::string      data_dir_flag;       // empty → auto-resolve (portable vs. stable dir)
    bridge::TlsOptions tls_opts;           // enabled=false by default (plain HTTP/WS)

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir_flag = argv[++i];
        } else if (std::strcmp(argv[i], "--remote") == 0) {
            bind_remote = true;
        } else if (std::strcmp(argv[i], "--mobile") == 0) {
            serve_mobile = true;
        } else if (std::strcmp(argv[i], "--webroot") == 0 && i + 1 < argc) {
            web_root = argv[++i];
        } else if (std::strcmp(argv[i], "--tls") == 0) {
            tls_opts.enabled = true;
        } else if (std::strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
            tls_opts.cert_path = argv[++i];
        } else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            tls_opts.key_path = argv[++i];
        } else if (std::strcmp(argv[i], "--tls-san") == 0 && i + 1 < argc) {
            tls_opts.extra_san.emplace_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (port == 0) {
        pal::log_error("openALE", "ERROR: --port <N> is required.");
        print_usage(argv[0]);
        return 1;
    }

    if (web_root.empty()) web_root = resolve_web_root(serve_mobile);

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    auto timer = pal::create_timer();

    // Audio device and radio both start detached (offline) here — there are
    // no startup flags for either. restart_audio_connection() /
    // restart_radio_connection() below re-attach them from persisted config a
    // few lines down if audio_auto_open / rig_auto_connect was armed by a
    // prior AUDIO_OPEN / RIG_CONNECT.
    std::unique_ptr<AudioDevice> audio;
    std::unique_ptr<pal::IRadio> radio;

    // ── WebSocket server ─────────────────────────────────────────────────
    bridge::WsServer ws;
    ws.set_web_root(web_root);   // serve apps/gui/ over HTTP on the same port
    if (!ws.start(port, bind_remote, tls_opts)) {
        pal::log_error("openALE", "Failed to start WebSocket server on port %u.", port);
        return 1;
    }

    // ── ALE controller ───────────────────────────────────────────────────
    // Bare start: no self address yet (the GUI sets it via SELF_ADDR_ADD, which
    // makes the first entry primary → set_self_address). The SM won't recognise
    // calls until then, which is correct for an unconfigured station.
    ALEController ctrl;

    // Where station.state/lqa.bin/the Location Relay identity key live.
    // Precedence: --data-dir override > portable mode (a station.state
    // already sitting in the launch directory — today's behavior, preserved
    // so existing installs see zero change on upgrade) > a stable per-user
    // directory (%APPDATA%\openALE or ~/.config/openALE). Only a genuinely
    // fresh install lands in the stable directory — that's the case that
    // actually needs one, since it's the one every future rebuild or
    // launch-from-elsewhere would otherwise silently orphan (most visibly:
    // Location Relay generating a brand-new signing identity that needs
    // re-approval every time).
    std::string base_dir = data_dir_flag;
    if (base_dir.empty()) {
        std::ifstream portable_probe("station.state");
        if (!portable_probe.good()) base_dir = pal::user_config_dir();
    }
    const std::string lqa_path   = base_dir.empty() ? "lqa.bin"       : base_dir + "/lqa.bin";
    const std::string state_path = base_dir.empty() ? "station.state" : base_dir + "/station.state";
    pal::log_info("openALE", "Config directory: %s",
                   base_dir.empty() ? "(portable — current working directory)" : base_dir.c_str());

    if (ctrl.load_lqa(lqa_path))
        pal::log_info("openALE", "LQA loaded from %s (%zu entries)",
                       lqa_path.c_str(), ctrl.get_all_lqa_entries().size());
    // failure path (missing vs. corrupt file) is already logged inside
    // ALEController::load_lqa() with the specific reason.

    // Unified auto-save file: channels/nets/contacts/rosters/allcall + all
    // settings. Unconditional, like LQA above — arms auto-save for the rest
    // of the session regardless of whether the file existed yet (first run).
    ctrl.load_state(state_path);
    pal::log_info("openALE", "Station state loaded from %s (auto-save armed)", state_path.c_str());

    // ── rigctld/netrigctl-compat read-only frequency server (Tuner) ────────
    // Opt-in (disabled by default); config loaded above via load_state().
    // Never touches ALEController/pal::IRadio directly — see rigctld_server.h.
    bridge::RigctldServer rigctld;
    if (ctrl.get_config().rigctld_server_enabled) {
        const auto& rcfg = ctrl.get_config();
        if (rigctld.start(rcfg.rigctld_server_port, rcfg.rigctld_server_bind_remote)) {
            pal::log_info("openALE", "rigctld-compat server: listening on %s:%u",
                          rcfg.rigctld_server_bind_remote ? "0.0.0.0" : "127.0.0.1",
                          rcfg.rigctld_server_port);
        } else {
            pal::log_error("openALE", "rigctld-compat server: failed to bind port %u",
                           rcfg.rigctld_server_port);
        }
    }

    // GPS / SFI / Location-Relay services — started on demand from
    // STATION_LOC_SET / LOCATION_SHARING_SET
    GpsService            gps_svc;
    SfiService            sfi_svc;
    LocationRelayService  loc_svc;
    PendingUpdate    pending;
    AudioTransport   transport;   // declared first → destroyed after voice_mgr / audio_monitor
    VoicePathManager voice_mgr;   // declared second → destroyed before transport
    AudioMonitor     audio_monitor; // link-independent RX "listen in" tap

    // Voice PTT counts as link activity: reset the SM idle (Twa) timer so a QSO
    // does not auto-terminate mid-conversation.
    voice_mgr.on_ptt_activity = [&]() {
        if (ctrl.is_link_active()) ctrl.reset_link_idle_timer();
    };

    // Decoder: always feed the ALE demodulator (including during voice links so
    // remote TWAS terminations are decoded). Spectrum callback fires from here.
    transport.set_decoder_sink([&](const int16_t* buf, size_t n) {
        ctrl.feed_audio(buf, static_cast<uint32_t>(n));
    });
    // Both the voice-passthrough speaker path and the link-independent RX
    // monitor forward captured RX PCM to the browser using the same wire
    // framing (tag 0x01 + int16 LE PCM) — the GUI just plays whatever 0x01
    // frames arrive, without needing to know which sink produced them.
    auto send_rx_pcm_frame = [&](const int16_t* buf, size_t n) {
        std::vector<uint8_t> f;
        f.reserve(1 + n * sizeof(int16_t));
        f.push_back(0x01);  // stream tag: voice/monitor PCM (int16 LE)
        const auto* p = reinterpret_cast<const uint8_t*>(buf);
        f.insert(f.end(), p, p + n * sizeof(int16_t));
        ws.send_binary(f.data(), f.size());
    };
    // Speaker: VoicePathManager self-registers as an RxSink on passthrough
    // entry and delegates here. The transport gates TX suppression; VPM gates
    // passthrough state via add/remove_rx_sink. One callback, no coupling.
    voice_mgr.on_speaker_pcm = send_rx_pcm_frame;
    voice_mgr.set_transport(&transport);
    transport.set_media_producer(&voice_mgr);
    transport.set_protocol_tx_query([&ctrl]() { return ctrl.is_tx_active(); });

    // Monitor: independent RX tap, armed/disarmed via AUDIO_MONITOR regardless
    // of link state. Suppresses itself while voice_mgr is already streaming
    // (see AudioMonitor::on_rx_audio) so the browser never gets duplicate PCM.
    audio_monitor.attach(&transport);
    audio_monitor.set_voice_manager(&voice_mgr);
    audio_monitor.on_pcm = send_rx_pcm_frame;

    BridgeCtx ctx{ &ctrl, &audio, &radio, lqa_path, state_path,
                   /*audio_in*/"", /*audio_out*/"",
                   /*tx_volume*/0.25f,
                   &gps_svc, &sfi_svc, &loc_svc, &pending, &ws,
                   &voice_mgr, /*voice_armed*/false,
                   &audio_monitor,
                   &rigctld };

    // Resume GPS / SFI / Location-Relay from persisted config (station.state,
    // loaded above). Previously these only ever started from a live
    // STATION_LOC_SET/LOCATION_SHARING_SET call, so a previously-enabled
    // service sat inactive after every bridge restart until the GUI's
    // connect-time sync happened to push a SET — unlike rigctld, which
    // already starts inline from persisted config a few lines above. This
    // call makes the three behave the same way.
    restart_location_services(ctx, ctrl);

    // Re-attach the last-connected rig from persisted config (station.state,
    // loaded above), if a prior RIG_CONNECT armed rig_auto_connect and no
    // RIG_DISCONNECT followed since. See restart_radio_connection() for details.
    restart_radio_connection(ctx, ctrl);

    // Re-open the last-used audio device from persisted config, if a prior
    // AUDIO_OPEN armed audio_auto_open and no AUDIO_CLOSE followed since.
    // See restart_audio_connection() for details.
    restart_audio_connection(ctx, ctrl);

    // ── Event bus subscriptions ──────────────────────────────────────────
    // All ALEController events arrive via the global PAL event handler.
    // Data pointer lifetime: valid only for the synchronous callback duration.
    {
        auto* bus = pal::get_event_handler();

        bus->on(pal::EventType::ALE_STATUS, [&](const pal::Event& ev) {
            mj::Value e = make_event("status");
            e.set("msg", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_LINK_ESTABLISHED, [&](const pal::Event& ev) {
            mj::Value e = make_event("link_established");
            e.set("peer", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
            // Hand the VAC to the voice path (if armed) and notify the GUI.
            voice_mgr.on_link_state(true);
            if (voice_mgr.passthrough_active()) {
                mj::Value v = make_event("voice_path");
                v.set("mode", mj::Value::string("voice"));
                v.set("peer", mj::Value::string(ev.message));
                ws.send_text(mj::dump(v));
            }
        });

        bus->on(pal::EventType::ALE_AMD_NO_LINK, [&](const pal::Event& ev) {
            mj::Value e = make_event("amd_no_link");
            e.set("peer", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_AMD_NOT_SENT, [&](const pal::Event& ev) {
            mj::Value e = make_event("amd_not_sent");
            e.set("peer", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_AMD_DELIVERED, [&](const pal::Event& ev) {
            mj::Value e = make_event("amd_delivered");
            e.set("peer", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_CALL_RECEIVED, [&](const pal::Event& ev) {
            mj::Value e = make_event("call_received");
            e.set("caller", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_LINK_TERMINATED, [&](const pal::Event& ev) {
            mj::Value e = make_event("link_terminated");
            e.set("reason", mj::Value::string(ev.message));
            ws.send_text(mj::dump(e));
            // Return the VAC to the modem and notify the GUI.
            const bool was_passthrough = voice_mgr.passthrough_active();
            voice_mgr.on_link_state(false);
            if (was_passthrough) {
                mj::Value v = make_event("voice_path");
                v.set("mode",   mj::Value::string("ale"));
                v.set("reason", mj::Value::string(ev.message));
                ws.send_text(mj::dump(v));
            }
        });

        bus->on(pal::EventType::ALE_IDLE_WARNING, [&](const pal::Event& ev) {
            mj::Value e = make_event("idle_warning");
            e.set("remaining_sec", mj::Value::number(static_cast<uint32_t>(ev.code)));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_SOUNDING_WARNING, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::SoundingWarningData*>(ev.data);
            mj::Value e = make_event("sounding_warning");
            e.set("net",           mj::Value::string(d->net));
            e.set("remaining_sec", mj::Value::number(d->remaining_sec));
            e.set("phase",         mj::Value::string(d->phase));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_TEST_CHANNEL, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::TestChannelData*>(ev.data);
            mj::Value e = make_event("test_channel");
            e.set("peer",       mj::Value::string(d->peer));
            e.set("phase",      mj::Value::string(d->phase));
            e.set("channel_id", mj::Value::string(d->channel_id));
            e.set("freq_hz",    mj::Value::number(d->freq_hz));
            e.set("index",      mj::Value::number(d->index));
            e.set("total",      mj::Value::number(d->total));
            e.set("score",      mj::Value::number(d->score));
            e.set("linked",     mj::Value::boolean(d->linked));
            e.set("summary",    mj::Value::string(d->summary));
            ws.send_text(mj::dump(e));
        });

        bus->on(pal::EventType::ALE_AMD_RECEIVED, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::AmdData*>(ev.data);
            mj::Value e = make_event("amd_received");
            e.set("self", mj::Value::string(d->self_addr));
            e.set("peer", mj::Value::string(d->peer_addr));
            e.set("text", mj::Value::string(d->text));
            e.set("call_context", mj::Value::string(d->call_context));
            if (ale::is_gpr(d->text)) {
                const ale::AleGpr g = ale::parse_gpr(d->text);
                mj::Value gv = mj::Value::object();
                gv.set("valid_structure",  mj::Value::boolean(g.valid_gpr_structure));
                gv.set("valid_position",   mj::Value::boolean(g.valid_position));
                gv.set("valid_timestamp",  mj::Value::boolean(g.valid_timestamp));
                gv.set("manual_or_invalid",mj::Value::boolean(g.manual_or_invalid_position));
                gv.set("object",  mj::Value::string(g.object));
                gv.set("comment", mj::Value::string(g.comment));
                if (g.has_latitude)  gv.set("lat_deg", mj::Value::number(g.latitude_deg));
                if (g.has_longitude) gv.set("lon_deg", mj::Value::number(g.longitude_deg));
                if (g.has_altitude) {
                    gv.set("altitude",      mj::Value::number(g.altitude));
                    gv.set("altitude_unit", mj::Value::string(std::string(1, g.altitude_unit)));
                }
                if (g.has_timestamp)
                    gv.set("timestamp_utc", mj::Value::number(static_cast<double>(g.timestamp_utc)));
                e.set("gpr", gv);

                // Visibility into every incoming ALE-GPR, independent of whether
                // Location Relay is enabled/gates it — operators need to see what
                // arrived (and why a malformed one wasn't relayed) in the log.
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
            }
            ws.send_text(mj::dump(e));
        });

        // Location Relay (docs/LOCATION_SHARING_CONCEPT.md §4): independent
        // subscriber on the same event, gate → dedup(in enqueue) → queue. The
        // Core stays GPR-agnostic beyond call_context; parsing happens here,
        // exactly like the GUI-forward handler above (Konzept §4 recommendation).
        bus->on(pal::EventType::ALE_AMD_RECEIVED, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::AmdData*>(ev.data);
            if (!ale::is_gpr(d->text)) return;
            const auto& cfg = ctrl.get_config();
            if (!cfg.location_sharing_enabled) return;

            const ale::AleGpr g = ale::parse_gpr(d->text);
            const std::string source = g.object.empty() ? std::string(d->peer_addr) : g.object;
            if (!ale::is_shareable(g, source, d->call_context, d->self_addr, cfg)) return;

            ale::LocationReport r;
            r.observer     = d->self_addr;
            r.source       = source;
            r.relay        = (source != d->peer_addr) ? d->peer_addr : "";
            r.raw_gpr      = g.raw;
            r.has_position = g.valid_position;
            if (g.has_latitude && g.has_longitude) {
                const double scale = std::pow(10.0, cfg.location_sharing_round_digits);
                r.lat = std::round(g.latitude_deg * scale) / scale;
                r.lon = std::round(g.longitude_deg * scale) / scale;
            }
            r.has_altitude   = g.has_altitude;
            r.altitude       = g.altitude;
            r.altitude_unit  = g.altitude_unit;
            r.has_timestamp  = g.has_timestamp;
            r.timestamp_utc  = g.timestamp_utc;
            r.comment        = cfg.location_sharing_include_comment ? g.comment : "";
            r.call_context   = d->call_context;
            r.received_at    = std::time(nullptr);
            // RX frequency the GPR arrived on — lets the relay map filter
            // stations by band. 0 when no channel is active (offline/no radio).
            r.frequency_hz   = ctrl.get_current_channel().rx_frequency_hz;
            r.dedup_key      = ale::make_dedup_key(g, source, cfg.location_sharing_round_digits);

            ctx.loc_svc->enqueue(std::move(r));
            pal::log_info("LocationRelay", "queued %s (via %s)", source.c_str(), d->call_context);
        });

        // Passive channel monitor: every decoded RX ALE word.
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

        // Passive TX monitor: every ALE word emitted by the SM for TX.
        bus->on(pal::EventType::ALE_WORD_TX, [&](const pal::Event& ev) {
            const auto* d = static_cast<const ale::WordData*>(ev.data);
            mj::Value e = make_event("word_tx");
            e.set("frame_id", mj::Value::number(d->frame_id));
            e.set("preamble", mj::Value::string(d->preamble));
            e.set("addr",     mj::Value::string(d->addr));
            e.set("votes",    mj::Value::number(d->votes));
            e.set("fec",      mj::Value::number(d->fec));
            e.set("ts_ms",    mj::Value::number(d->ts_ms));
            e.set("freq_hz",  mj::Value::number(d->freq_hz));
            ws.send_text(mj::dump(e));
        });

        // Assembled ALE frame: fires once per complete frame with semantic summary.
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

        // Channel changed: scan hop, calling tune, or manual VFO change.
        bus->on(pal::EventType::CHANNEL_CHANGED, [&](const pal::Event& ev) {
            const auto* ch = static_cast<const Channel*>(ev.data);
            mj::Value e = make_event("channel_changed");
            e.set("channel_id", mj::Value::string(ch->id));
            e.set("rx_hz",      mj::Value::number(ch->rx_frequency_hz));
            e.set("tx_hz",      mj::Value::number(ch->tx_frequency_hz));
            e.set("mode",       mj::Value::string(ch->rx_mode));
            ws.send_text(mj::dump(e));
        });

        // PTT state changed (SM-driven TX or manual PTT).
        auto ptt_handler = [&](const pal::Event& ev) {
            mj::Value e = make_event("ptt_changed");
            e.set("ptt", mj::Value::boolean(ev.type == pal::EventType::PTT_ON));
            ws.send_text(mj::dump(e));
        };
        bus->on(pal::EventType::PTT_ON,  ptt_handler);
        bus->on(pal::EventType::PTT_OFF, ptt_handler);
    }
    // Fires inline from ctrl.feed_audio() below — i.e. on THIS main-loop thread,
    // not the WASAPI audio thread (the modem's spectrum FFT runs in
    // push_samples()). send_binary() is mutex-protected in WsServer regardless.
    // See docs/THREADING.md.
    ctrl.set_spectrum_callback([&](const float* bins, size_t n, float /*hz_per_bin*/) {
        // Tag binary frames so the browser can demux the spectrum stream (0x00)
        // from the voice-PCM stream (0x01) on the same WebSocket.
        std::vector<uint8_t> f;
        f.reserve(1 + n * sizeof(float));
        f.push_back(0x00);
        const auto* p = reinterpret_cast<const uint8_t*>(bins);
        f.insert(f.end(), p, p + n * sizeof(float));
        ws.send_binary(f.data(), f.size());
    });

    pal::log_info("openALE", "listening on %s:%u — waiting for GUI to configure",
                  bind_remote ? "0.0.0.0" : "127.0.0.1", port);
    if (web_root.empty())
        pal::log_info("openALE", "(no GUI found — open via file:// or pass --webroot)");
    else
        pal::log_info("openALE", "open GUI (%s):  %s://%s:%u/index.html",
                      serve_mobile ? "mobile" : "desktop", tls_opts.enabled ? "https" : "http",
                      (tls_opts.enabled && bind_remote) ? "<this-machine's-LAN-IP>" : "localhost", port);

    // Start in "available" (IDLE, RX enabled) rather than scanning: scanning
    // only makes sense once >=2 channels are configured from the GUI, and the
    // GUI's Scan button drives SCANNING/IDLE explicitly (see Aufgabe 4/5).
    ctrl.start_available();

    // ── Main loop — mirrors ale_cli.cpp exactly, plus WS command drain ─────
    std::string last_state      = ctrl.display_state();
    bool        last_lbt_busy   = false;
    bool        last_gps_receiving = false;  // → live "gps_link" pill (see drain below)
    std::string last_voice_state;   // "" = not in voice session
    while (g_running) {
        const uint32_t t = static_cast<uint32_t>(timer->get_time_ms());

        // Drain GPS/SFI updates from worker threads — MUST happen on main thread
        // before ctrl.update() so the propagation context is fresh each tick.
        {
            std::lock_guard<std::mutex> g(pending.mtx);
            if (pending.gps_dirty) {
                ctrl.set_gps_fix(pending.gps_acq, pending.gps_lat, pending.gps_lon);
                pending.gps_dirty = false;
            }
            if (ctx.gps_svc) {
                // Altitude/raw-GGA are supplementary (ALE-GPR report generation
                // only, see set_gps_altitude()'s doc comment) — polled directly
                // each tick rather than threaded through the fix-changed
                // callback/mailbox above, which exists for the propagation-
                // context-affecting lat/lon signal.
                ctrl.set_gps_altitude(ctx.gps_svc->has_altitude(), ctx.gps_svc->alt());
                ctrl.set_gps_raw_gga(ctx.gps_svc->raw_gga());
                // Data-liveness pill (independent of has_fix() — see
                // GpsService::is_receiving() doc comment): distinguishes
                // "connected, still acquiring a fix" from "no data at all"
                // in the GUI, which otherwise look identical.
                const bool cur_gps_receiving = ctx.gps_svc->is_receiving();
                if (cur_gps_receiving != last_gps_receiving) {
                    last_gps_receiving = cur_gps_receiving;
                    mj::Value e = make_event("gps_link");
                    e.set("receiving", mj::Value::boolean(cur_gps_receiving));
                    ws.send_text(mj::dump(e));
                }
            }
            if (pending.sfi_dirty) {
                ctrl.set_current_sfi(pending.sfi);
                pending.sfi_dirty = false;
            }
        }

        // Drain Location Relay worker status lines → ALE-Log (Konzept §17:
        // "Bridge-Main-Loop drain → ALE-Log + WS-Event", same pull-based
        // pattern as the mailbox above, but for fire-and-forget log text
        // rather than state ctrl.update() depends on).
        {
            std::string loc_status;
            while (ctx.loc_svc->pop_status(loc_status)) {
                mj::Value e = make_event("status");
                e.set("msg", mj::Value::string(loc_status));
                ws.send_text(mj::dump(e));
            }
        }

        // Drain Location Relay endpoint connection-state transitions → live
        // GUI pill update (the "Running" pill under Privacy / Network —
        // Location Relay). Pushed on initial probe and on every loss/restore.
        {
            int cs;
            while (ctx.loc_svc->pop_conn_state(cs)) {
                mj::Value e = make_event("location_relay");
                e.set("running", mj::Value::boolean(ctx.loc_svc->is_running()));
                e.set("conn_state", mj::Value::string(loc_conn_state_name(cs)));
                ws.send_text(mj::dump(e));
            }
        }

        // Audio first: words must be delivered to the SM before the dwell check
        // so a word decoded in the same tick as expiry sets SCAN_PAUSE on the
        // correct channel rather than the one hopped to (§A.5.3.3 Bug 2 fix).
        voice_mgr.attach(audio.get(), radio.get());
        transport.attach(audio.get());
        transport.tick();

        // WS drain BEFORE ctrl.update() so operator commands (SOUND, CALL, etc.)
        // are visible to the SM in the same tick they arrive — not deferred one
        // tick, which caused ~50ms brief TX abort on high-latency WS connections.
        std::string raw;
        while (ws.pop_message(raw)) {
            const mj::Value parsed = mj::parse(raw);
            ws.send_text(dispatch_command(ctx, parsed));
        }

        // rigctld-compat: drain pending requests, answer synchronously — main
        // thread only (see rigctld_server.h for why ALEController/pal::IRadio
        // are never touched from the server's own I/O thread).
        {
            uint64_t conn_id;
            std::string line;
            while (rigctld.pop_request(conn_id, line)) {
                const bool attached = ctrl.has_radio();
                const uint32_t freq = attached ? ctrl.get_current_frequency() : 0;
                rigctld.send_reply(conn_id, bridge::handle_rigctld_command(line, attached, freq));
            }
        }

        ctrl.update(t);
        // Gate occupancy detection off during voice PTT: the VAC loopback of
        // our own TX would drive the detector BUSY just like ALE TX does.
        // set_voice_tx_active syncs state into update()'s gate on the next tick.
        ctrl.set_voice_tx_active(transport.media_tx_active());

        // Drain voice mic uplink (binary frames, stream tag 0x01) from the
        // browser -> voice manager. Discard when not in passthrough-TX so frames
        // don't accumulate in the WS receive queue.
        {
            std::vector<uint8_t> bin;
            const bool want_mic = voice_mgr.passthrough_active() && voice_mgr.ptt();
            while (ws.pop_binary(bin)) {
                if (want_mic && bin.size() >= 1 && bin[0] == 0x01) {
                    const size_t n = (bin.size() - 1) / sizeof(int16_t);
                    if (n) voice_mgr.push_mic_pcm(
                        reinterpret_cast<const int16_t*>(bin.data() + 1), n);
                }
            }
        }

        // Per-instance display state: derives "HANDSHAKE" for the caller's
        // response-exchange sub-phases too (see ALEController::display_state()),
        // so each side shows calling → handshake → linked from its own state.
        std::string s = ctrl.display_state();
        if (s != last_state) {
            mj::Value e = make_event("state");
            e.set("value", mj::Value::string(s));
            ws.send_text(mj::dump(e));
            last_state = std::move(s);
        }

        // Voice session sub-state: push on every sub-state transition so the GUI
        // can distinguish receiving / transmitting / protocol-burst / inactive.
        {
            const std::string vst =
                transport.protocol_pending()   ? "protocol" :
                transport.transmitting_voice() ? "transmitting" :
                transport.receiving_voice()    ? "receiving" : "";
            if (vst != last_voice_state) {
                mj::Value e = make_event("voice_session");
                e.set("state", mj::Value::string(vst));
                ws.send_text(mj::dump(e));
                last_voice_state = vst;
            }
        }

        // Channel-occupancy indicator: push on every busy→idle / idle→busy edge
        // so the GUI always reflects the detector's current state, not just
        // the transitions that happen to fall inside an LBT window.
        const bool cur_lbt_busy = ctrl.lbt_busy();
        if (cur_lbt_busy != last_lbt_busy) {
            mj::Value e = make_event("channel_busy");
            e.set("busy",     mj::Value::boolean(cur_lbt_busy));
            e.set("level_db", mj::Value::number(ctrl.lbt_level_db()));
            e.set("floor_db", mj::Value::number(ctrl.lbt_floor_db()));
            ws.send_text(mj::dump(e));
            last_lbt_busy = cur_lbt_busy;
        }

        timer->sleep_ms(1);
    }

    ctrl.emergency_stop();
    gps_svc.stop();
    sfi_svc.stop();
    if (radio) radio->stop();
    if (audio) audio->close();
    ws.stop();
    rigctld.stop();
    if (ctrl.save_lqa(lqa_path))
        pal::log_info("openALE", "LQA saved to %s", lqa_path.c_str());
    if (ctrl.save_state(state_path))
        pal::log_info("openALE", "Station state saved to %s", state_path.c_str());
    pal::log_info("openALE", "Exiting.");
    return 0;
}
