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
 *   ale_bridge --port N [--remote] [--webroot DIR]
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
#include "App/gps_service.h"
#include "App/sfi_service.h"
#include "PAL/radio.h"
#include "PAL/radios/hamlib_radio.h"
#include "PAL/timer.h"
#include "bridge/ws_server.h"
#include "bridge/minijson.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static std::string exe_dir() { return ""; }
#endif

static std::string resolve_web_root() {
    std::vector<std::string> roots;
    const std::string ed = exe_dir();
    if (!ed.empty()) {
        std::string up = ed;
        for (int i = 0; i < 6; ++i) {       // exe dir + up to 5 parents
            roots.push_back(up + "/apps/gui");
            up += "/..";
        }
    }
    roots.push_back("apps/gui");
    roots.push_back("./apps/gui");
    for (const auto& r : roots)
        if (file_exists(r + "/index.html")) return r;
    return "";
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
    f.resize(12);
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

// Assemble the pal::create_radio() spec from structured GUI fields (so the GUI
// never needs to know the hamlib spec syntax). "" for the "none" backend.
static std::string build_radio_spec(const mj::Value& msg) {
    const std::string backend = msg.get_string("backend", "netrigctl");
    if (backend == "none") return "";
    if (backend == "serial") {
        const std::string model = msg.get_string("model", "1");
        const std::string port  = msg.get_string("serial");
        const int baud  = static_cast<int>(msg.get_number("baud", 0));
        // Line-state policy: defaults ON/ON/200 wenn nicht gesetzt
        const std::string dtr = msg.get_string("dtr", "on");
        const std::string rts = msg.get_string("rts", "on");
        const int stab = static_cast<int>(msg.get_number("stab", 200));
        // Format: "hamlib:model:port,baud,dtr=on,rts=on,stab=200"
        return "hamlib:" + model + ":" + port
             + "," + (baud > 0 ? std::to_string(baud) : "0")
             + ",dtr=" + dtr
             + ",rts=" + rts
             + ",stab=" + std::to_string(stab);
    }
    // netrigctl (TCP rigctld) — hamlib model 2 = NET_RIGCTL
    return "hamlib:2:tcp://" + msg.get_string("host", "127.0.0.1")
         + ":" + msg.get_string("port", "4532");
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
    std::string                    audio_in;   ///< last successfully opened RX device
    std::string                    audio_out;  ///< last successfully opened TX device
    float                          tx_volume = 0.25f; ///< persists across AUDIO_OPEN/CLOSE
    // GPS / SFI services and shared pending-update mailbox
    GpsService*       gps_svc  = nullptr;
    SfiService*       sfi_svc  = nullptr;
    PendingUpdate*    pending  = nullptr;
    bridge::WsServer* ws       = nullptr;  ///< for GPS/SFI push events from STATION_LOC_SET
};

// ── Command dispatch ────────────────────────────────────────────────────────
//
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
            std::printf("[ale_bridge] GPS service: starting (%s)\n",
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
            std::printf("[ale_bridge] SFI service: starting fetch thread\n");
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
            std::printf("[ale_bridge] SFI service: disabled\n");
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

    if (cmd == "CALL") {
        const bool single = msg.get_bool("single_channel", false);
        return pc(msg, ctrl, (single ? "CMD:SINGLE_CALL " : "CMD:CALL ") + msg.get_string("addr"));
    }
    if (cmd == "GROUP_CALL") {
        if (msg.find("roster"))
            return pc(msg, ctrl, "CMD:GROUP_CALL " + msg.get_string("roster"));
        const mj::Value* members = msg.find("members");
        const bool ok = members && ctrl.initiate_group_call(members->as_string_array());
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
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "ACCEPT")           { return pc(msg, ctrl, "CMD:ACCEPT"); }
    if (cmd == "REJECT")           { return pc(msg, ctrl, "CMD:REJECT"); }
    if (cmd == "TERMINATE")        { return pc(msg, ctrl, "CMD:TERMINATE"); }
    if (cmd == "RESET_IDLE_TIMER") { return pc(msg, ctrl, "CMD:RESET_IDLE_TIMER"); }
    if (cmd == "EMERGENCY_STOP")   { return pc(msg, ctrl, "CMD:EMERGENCY_STOP"); }
    if (cmd == "SET_PTT")          { return pc(msg, ctrl, msg.get_bool("on", false) ? "CMD:SET_PTT on" : "CMD:SET_PTT off"); }

    if (cmd == "AMD") {
        // Typed dispatch: if LINKED, send AMD over the established link; otherwise
        // queue it and place a call to `to`. `to` is the selected contact (or the
        // active peer when LINKED — ignored by send_amd in that case).
        const std::string to   = msg.get_string("to");
        const std::string text = msg.get_string("text");
        const std::string resp = ctrl.send_amd(to, text);
        mj::Value r = make_reply(msg, resp.rfind("OK:", 0) == 0);
        r.set("msg", mj::Value::string(resp));
        return mj::dump(r);
    }
    if (cmd == "MANUAL_ACCEPT_MODE") {
        ctrl.set_manual_accept_mode(msg.get_bool("on"),
            static_cast<uint32_t>(msg.get_number("timeout_ms", 10000)));
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
        const bool ok = ctrl.load_station_file(msg.get_string("path"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "STATION_SAVE" || cmd == "CHANNELS_SAVE") {
        const bool ok = ctrl.save_station_file(msg.get_string("path"));
        return mj::dump(make_reply(msg, ok));
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
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "LQA_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("lqa_enabled", mj::Value::boolean(ctrl.lqa_enabled()));
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
    if (cmd == "RELINK_SET") {
        // relink_enabled: auto-renegotiate channel via TWAS + re-call when a
        // better channel is known post-LINKED (A.5.4.5 bilateral selection).
        if (msg.has("relink_enabled"))
            ctrl.set_relink_enabled(msg.get_bool("relink_enabled"));
        if (msg.has("relink_threshold"))
            ctrl.set_relink_threshold(static_cast<float>(msg.get_number("relink_threshold")));
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
        pal::hamlib_set_log_level(level);
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "FREQ_SELECT_SET") {
        if (msg.has("enhanced_freq_select"))
            ctrl.set_enhanced_freq_select(msg.get_bool("enhanced_freq_select"));
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
        return mj::dump(r);
    }

    // ── VFO / radio ──────────────────────────────────────────────────────
    if (cmd == "VFO_GET") {
        // Sync from radio before reading so the reply reflects actual radio
        // state (freq/mode the operator may have changed in Quisk or on
        // hardware), not just the last value PC-ALE commanded.
        ctrl.sync_radio_state();
        mj::Value r = make_reply(msg, true);
        r.set("freq_hz", mj::Value::number(ctrl.get_current_frequency()));
        r.set("mode", mj::Value::string(ctrl.get_current_mode()));
        r.set("tune_step_hz", mj::Value::number(ctrl.get_tune_step()));
        r.set("ptt", mj::Value::boolean(ctrl.get_ptt_state()));
        return mj::dump(r);
    }
    if (cmd == "VFO_SET_FREQ")      { return mj::dump(make_reply(msg, ctrl.set_frequency(static_cast<uint32_t>(msg.get_number("hz"))))); }
    if (cmd == "VFO_SET_MODE")      { return mj::dump(make_reply(msg, ctrl.set_mode(msg.get_string("mode")))); }
    if (cmd == "VFO_SET_CHANNEL")   { return mj::dump(make_reply(msg, ctrl.set_vfo_channel(static_cast<uint32_t>(msg.get_number("hz")), msg.get_string("mode")))); }
    if (cmd == "VFO_STEP")          { return mj::dump(make_reply(msg, ctrl.step_channel(static_cast<int>(msg.get_number("direction"))))); }
    if (cmd == "VFO_NUDGE")         { ctrl.nudge_frequency(static_cast<int>(msg.get_number("direction"))); return mj::dump(make_reply(msg, true)); }
    if (cmd == "VFO_SET_TUNE_STEP") { ctrl.set_tune_step(static_cast<uint32_t>(msg.get_number("hz"))); return mj::dump(make_reply(msg, true)); }

    // ── Timing (Level-5 programmable defaults only — see docs/GUI_BRIDGE_GAPS.md) ──
    if (cmd == "TIMING_SET") {
        if (msg.has("scan_dwell_ms"))          ctrl.set_scan_dwell_ms(static_cast<uint32_t>(msg.get_number("scan_dwell_ms")));
        if (msg.has("sounding_interval_sec"))  ctrl.set_sounding_interval_sec(static_cast<uint32_t>(msg.get_number("sounding_interval_sec")));
        if (msg.has("link_idle_timeout_sec"))  ctrl.set_link_idle_timeout_sec(static_cast<uint32_t>(msg.get_number("link_idle_timeout_sec")));
        if (msg.has("max_tune_time_ms"))       ctrl.set_max_tune_time_ms(static_cast<uint32_t>(msg.get_number("max_tune_time_ms")));
        if (msg.has("ptt_lead_ms"))            ctrl.set_ptt_lead_ms(static_cast<uint32_t>(msg.get_number("ptt_lead_ms")));
        if (msg.has("ptt_tail_ms"))            ctrl.set_ptt_tail_ms(static_cast<uint32_t>(msg.get_number("ptt_tail_ms")));
        if (msg.has("assumed_scan_channels"))  ctrl.set_assumed_scan_channels(static_cast<uint32_t>(msg.get_number("assumed_scan_channels")));
        if (msg.has("sounding_use_twas"))          ctrl.set_sounding_use_twas(msg.get_bool("sounding_use_twas"));
        if (msg.has("sounding_warning_lead_sec"))  ctrl.set_sounding_warning_lead_sec(static_cast<uint32_t>(msg.get_number("sounding_warning_lead_sec")));
        return mj::dump(make_reply(msg, true));
    }
    if (cmd == "TIMING_GET") {
        mj::Value r = make_reply(msg, true);
        r.set("scan_dwell_ms",              mj::Value::number(ctrl.get_scan_dwell_ms()));
        r.set("sounding_interval_sec",      mj::Value::number(ctrl.get_sounding_interval_sec()));
        r.set("link_idle_timeout_sec",      mj::Value::number(ctrl.get_link_idle_timeout_sec()));
        r.set("max_tune_time_ms",           mj::Value::number(ctrl.get_max_tune_time_ms()));
        r.set("ptt_lead_ms",                mj::Value::number(ctrl.get_ptt_lead_ms()));
        r.set("ptt_tail_ms",                mj::Value::number(ctrl.get_ptt_tail_ms()));
        r.set("assumed_scan_channels",      mj::Value::number(ctrl.get_assumed_scan_channels()));
        r.set("sounding_use_twas",          mj::Value::boolean(ctrl.get_sounding_use_twas()));
        r.set("sounding_warning_lead_sec",  mj::Value::number(ctrl.get_sounding_warning_lead_sec()));
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
        r.set("sfi",          mj::Value::number(ctrl.get_current_sfi()));
        return mj::dump(r);
    }
    if (cmd == "STATION_LOC_SET") {
        using PS = ALEStationConfig::PositionSource;
        if (msg.has("position_source"))
            ctrl.set_position_source(static_cast<PS>(
                static_cast<int>(msg.get_number("position_source"))));
        if (msg.has("lat_deg") && msg.has("lon_deg"))
            ctrl.set_station_position_manual(msg.get_number("lat_deg"),
                                             msg.get_number("lon_deg"));
        if (msg.has("grid_locator"))
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
        return mj::dump(make_reply(msg, true));
    }

    // ── FEC ──────────────────────────────────────────────────────────────
    if (cmd == "FEC_SET") {
        if (msg.has("golay_mode"))          ctrl.set_golay_mode(static_cast<GolayMode>(static_cast<int>(msg.get_number("golay_mode"))));
        if (msg.has("min_unanimous_votes")) ctrl.set_min_unanimous_votes(static_cast<uint8_t>(msg.get_number("min_unanimous_votes")));
        if (msg.has("adaptive_fec"))        ctrl.set_adaptive_fec(msg.get_bool("adaptive_fec"));
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
        // Close any currently-open device first (detach from controller so the
        // symbol source / completion arming are torn down before close()).
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); }
        *ctx.audio = make_audio_device();
        const std::string in_dev  = msg.get_string("in");
        const std::string out_dev = msg.get_string("out");
        const bool ok = (*ctx.audio)->open(in_dev, out_dev);
        if (ok) {
            ctrl.set_audio_device(ctx.audio->get());
            (*ctx.audio)->set_tx_volume(ctx.tx_volume);
            ctx.audio_in = in_dev; ctx.audio_out = out_dev;
        }
        else    { ctx.audio->reset(); ctx.audio_in.clear(); ctx.audio_out.clear(); }
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not open audio device(s)"));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_CLOSE") {
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); ctx.audio->reset(); }
        return mj::dump(make_reply(msg, true));
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
            r.set("status", mj::Value::string("no backend selected"));
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
        // Tear down any existing radio first.
        if (*ctx.radio) { ctrl.set_radio(nullptr); (*ctx.radio)->stop(); ctx.radio->reset(); }
        const std::string spec = build_radio_spec(msg);
        if (spec.empty()) {  // "none" backend → stay disconnected, succeed
            mj::Value r = make_reply(msg, true);
            r.set("connected", mj::Value::boolean(false));
            r.set("status", mj::Value::string("not attached"));
            return mj::dump(r);
        }
        *ctx.radio = pal::create_radio(spec);
        bool ok = *ctx.radio && (*ctx.radio)->initialize() && (*ctx.radio)->start();
        if (ok) ctrl.set_radio(ctx.radio->get());
        else    ctx.radio->reset();

        mj::Value r = make_reply(msg, ok);
        r.set("connected", mj::Value::boolean(ok && ctrl.test_rig_connection()));
        r.set("status", mj::Value::string(ctrl.get_rig_connection_status()));
        if (!ok) r.set("error", mj::Value::string("could not connect radio (" + spec + ")"));
        return mj::dump(r);
    }
    if (cmd == "RIG_DISCONNECT") {
        if (*ctx.radio) { ctrl.set_radio(nullptr); (*ctx.radio)->stop(); ctx.radio->reset(); }
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
    if (cmd == "RIG_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value arr = mj::arr();
        for (const auto& e : pal::list_rigs()) {
            mj::Value v = mj::obj();
            v.set("id",    mj::Value::number(e.model));
            v.set("mfg",   mj::Value::string(e.mfg));
            v.set("macro", mj::Value::string(e.macro));
            arr.push_back(std::move(v));
        }
        r.set("rigs", std::move(arr));
        return mj::dump(r);
    }

    // ── Settings ─────────────────────────────────────────────────────────
    if (cmd == "SETTINGS_EXPORT") {
        const std::string path = msg.get_string("path");
        const bool ok = ctrl.export_settings(path);
        if (ok && (!ctx.audio_in.empty() || !ctx.audio_out.empty())) {
            std::ofstream fa(path, std::ios::app);
            fa << "audio_in="  << ctx.audio_in  << "\n";
            fa << "audio_out=" << ctx.audio_out << "\n";
        }
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "SETTINGS_IMPORT") {
        const std::string path = msg.get_string("path");
        // Extract bridge-level keys (audio devices) before handing off to controller.
        std::string audio_in, audio_out;
        {
            std::ifstream fa(path);
            std::string ln;
            while (std::getline(fa, ln)) {
                if (ln.rfind("audio_in=", 0) == 0)  audio_in  = ln.substr(9);
                else if (ln.rfind("audio_out=", 0) == 0) audio_out = ln.substr(10);
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
        if (ok) restart_location_services(ctx, ctrl);
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "DEBUG_RX")        { ctrl.set_debug_rx(msg.get_bool("on")); return mj::dump(make_reply(msg, true)); }

    mj::Value r = make_reply(msg, false);
    r.set("error", mj::Value::string("unknown command: " + cmd));
    return mj::dump(r);
}

// ── Usage ───────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "ALE 2G WebSocket bridge — connects apps/gui/ to a live ALEController\n"
        "\n"
        "Starts bare and waits for a GUI to connect; everything (self address,\n"
        "channels, audio device, radio, …) is configured from apps/gui/ over the\n"
        "WebSocket.\n"
        "\n"
        "Usage:\n"
        "  %s --port N [--remote] [--webroot DIR]\n"
        "\n"
        "Options:\n"
        "  --port N     WebSocket listen port (required; multiple instances may use\n"
        "               different ports simultaneously)\n"
        "  --remote     Bind to 0.0.0.0 instead of 127.0.0.1 (LAN-reachable)\n"
        "               Without this flag the server is localhost-only.\n"
        "  --webroot D  Serve static files from DIR instead of auto-detected apps/gui/\n"
        "\n"
        "LQA history is persisted to lqa.bin in the working directory — loaded on\n"
        "startup, saved on exit. Delete the file to reset channel quality history.\n",
        prog);
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    uint16_t    port        = 0;     // 0 = not set; --port is required
    bool        bind_remote = false;
    std::string web_root;            // empty → auto-resolve below

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--remote") == 0) {
            bind_remote = true;
        } else if (std::strcmp(argv[i], "--webroot") == 0 && i + 1 < argc) {
            web_root = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (port == 0) {
        std::fprintf(stderr, "ERROR: --port <N> is required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (web_root.empty()) web_root = resolve_web_root();

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    auto timer = pal::create_timer();

    // Audio device and radio start detached (offline). The GUI attaches them at
    // runtime via AUDIO_OPEN / RIG_CONNECT — there are no startup flags for them.
    std::unique_ptr<AudioDevice> audio;
    std::unique_ptr<pal::IRadio> radio;

    // ── WebSocket server ─────────────────────────────────────────────────
    bridge::WsServer ws;
    ws.set_web_root(web_root);   // serve apps/gui/ over HTTP on the same port
    if (!ws.start(port, bind_remote)) {
        std::fprintf(stderr, "ERROR: Failed to start WebSocket server on port %u.\n", port);
        return 1;
    }

    // ── ALE controller ───────────────────────────────────────────────────
    // Bare start: no self address yet (the GUI sets it via SELF_ADDR_ADD, which
    // makes the first entry primary → set_self_address). The SM won't recognise
    // calls until then, which is correct for an unconfigured station.
    ALEController ctrl;

    const std::string lqa_path = "lqa.bin";
    if (ctrl.load_lqa(lqa_path))
        std::printf("[ale_bridge] LQA loaded from %s\n", lqa_path.c_str());

    // GPS / SFI services — started on demand from STATION_LOC_SET
    GpsService     gps_svc;
    SfiService     sfi_svc;
    PendingUpdate  pending;

    BridgeCtx ctx{ &ctrl, &audio, &radio, lqa_path,
                   /*audio_in*/"", /*audio_out*/"",
                   /*tx_volume*/0.25f,
                   &gps_svc, &sfi_svc, &pending, &ws };

    // ── Event push ───────────────────────────────────────────────────────
    ctrl.on_status_changed = [&](const std::string& m) {
        mj::Value e = make_event("status");
        e.set("msg", mj::Value::string(m));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_link_established = [&](const std::string& peer) {
        mj::Value e = make_event("link_established");
        e.set("peer", mj::Value::string(peer));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_call_received = [&](const std::string& caller) {
        mj::Value e = make_event("call_received");
        e.set("caller", mj::Value::string(caller));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_link_terminated = [&](const std::string& reason) {
        mj::Value e = make_event("link_terminated");
        e.set("reason", mj::Value::string(reason));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_idle_warning = [&](uint32_t remaining_sec) {
        mj::Value e = make_event("idle_warning");
        e.set("remaining_sec", mj::Value::number(remaining_sec));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_sounding_warning = [&](const std::string& net, uint32_t remaining_sec,
                                   const std::string& phase) {
        mj::Value e = make_event("sounding_warning");
        e.set("net",           mj::Value::string(net));
        e.set("remaining_sec", mj::Value::number(remaining_sec));
        e.set("phase",         mj::Value::string(phase));
        ws.send_text(mj::dump(e));
    };
    ctrl.on_amd_received = [&](const std::string& self_address,
                               const std::string& peer_address,
                               const std::string& text) {
        mj::Value e = make_event("amd_received");
        e.set("self", mj::Value::string(self_address));
        e.set("peer", mj::Value::string(peer_address));
        e.set("text", mj::Value::string(text));
        ws.send_text(mj::dump(e));
    };
    // ── Passive channel monitor ──────────────────────────────────────────
    // word_decoded: fires for every successfully decoded ALE word, regardless
    // of whether it is locally relevant. frame_id groups words into frames.
    ctrl.on_word_decoded = [&](const ALEWord& w, uint32_t fid) {
        mj::Value e = make_event("word_decoded");
        e.set("frame_id", mj::Value::number(fid));
        e.set("preamble", mj::Value::string(WordParser::word_type_name(w.type)));
        e.set("addr",     mj::Value::string(std::string(w.address)));
        e.set("votes",    mj::Value::number(w.unanimous_votes));
        e.set("fec",      mj::Value::number(w.fec_errors));
        e.set("ts_ms",    mj::Value::number(w.timestamp_ms));
        e.set("freq_hz",  mj::Value::number(ctrl.get_current_channel().rx_frequency_hz));
        ws.send_text(mj::dump(e));
    };
    // ── Passive TX monitor ────────────────────────────────────────────────
    // word_tx: fires for every ALE word the SM hands to the modem for TX
    // (sounding/calling/ack/…). Mirrors word_decoded's payload (same preamble /
    // addr / votes / fec fields) so the GUI can render sent and received rows
    // with identical color coding; only freq_hz (tx) and the event name differ.
    // frame_id is the controller's TX sequence counter, kept in a separate
    // space from the RX monitor_frame_id_ so the two log streams don't collide.
    ctrl.on_word_tx = [&](const ALEWord& w, uint32_t fid) {
        mj::Value e = make_event("word_tx");
        e.set("frame_id", mj::Value::number(fid));
        e.set("preamble", mj::Value::string(WordParser::word_type_name(w.type)));
        e.set("addr",     mj::Value::string(std::string(w.address)));
        e.set("votes",    mj::Value::number(w.unanimous_votes));
        e.set("fec",      mj::Value::number(w.fec_errors));
        e.set("ts_ms",    mj::Value::number(w.timestamp_ms));
        e.set("freq_hz",  mj::Value::number(ctrl.get_current_channel().tx_frequency_hz));
        ws.send_text(mj::dump(e));
    };
    // frame_decoded: fires once per assembled ALE frame with semantic summary.
    ctrl.on_frame_decoded = [&](const ALEMessage& frame, uint32_t fid) {
        mj::Value e = make_event("frame_decoded");
        e.set("frame_id",    mj::Value::number(fid));
        e.set("call_type",   mj::Value::string(CallTypeDetector::call_type_name(frame.call_type)));
        e.set("from",        mj::Value::string(frame.from_address));
        e.set("word_count",  mj::Value::number(static_cast<double>(frame.words.size())));
        e.set("start_ms",    mj::Value::number(frame.start_time_ms));
        e.set("duration_ms", mj::Value::number(frame.duration_ms));
        e.set("freq_hz",     mj::Value::number(ctrl.get_current_channel().rx_frequency_hz));
        mj::Value to_arr = mj::arr();
        for (const auto& a : frame.to_addresses)
            to_arr.push_back(mj::Value::string(a));
        e.set("to", std::move(to_arr));
        ws.send_text(mj::dump(e));
    };
    // Active channel changed (scan hop, calling tune, or manual VFO change).
    // Push event so the GUI frequency/channel readout tracks the real hop
    // cadence instead of the 2 s VFO_GET poll. Fires from ctrl.update() on
    // this main-loop thread (same as the events above).
    ctrl.on_channel_changed = [&](const Channel& ch) {
        mj::Value e = make_event("channel_changed");
        e.set("channel_id", mj::Value::string(ch.id));
        e.set("rx_hz",      mj::Value::number(ch.rx_frequency_hz));
        e.set("tx_hz",      mj::Value::number(ch.tx_frequency_hz));
        e.set("mode",       mj::Value::string(ch.rx_mode));
        ws.send_text(mj::dump(e));
    };
    // PTT state changed (SM-driven TX or manual PTT) — replaces the VFO_GET
    // poll's ptt field for the GUI PTT indicator.
    ctrl.on_ptt_changed = [&](bool ptt_on) {
        mj::Value e = make_event("ptt_changed");
        e.set("ptt", mj::Value::boolean(ptt_on));
        ws.send_text(mj::dump(e));
    };
    // Fires inline from ctrl.feed_audio() below — i.e. on THIS main-loop thread,
    // not the WASAPI audio thread (the modem's spectrum FFT runs in
    // push_samples()). send_binary() is mutex-protected in WsServer regardless.
    // See docs/THREADING.md.
    ctrl.set_spectrum_callback([&](const float* bins, size_t n, float /*hz_per_bin*/) {
        ws.send_binary(bins, n * sizeof(float));
    });

    std::printf("[ale_bridge] listening on %s:%u — waiting for GUI to configure\n",
                bind_remote ? "0.0.0.0" : "127.0.0.1", port);
    if (web_root.empty())
        std::printf("[ale_bridge] (no apps/gui/ found — open the GUI via file:// or pass --webroot)\n");
    else
        std::printf("[ale_bridge] open GUI:  http://localhost:%u/index.html\n", port);
    std::fflush(stdout);

    // Start in "available" (IDLE, RX enabled) rather than scanning: scanning
    // only makes sense once >=2 channels are configured from the GUI, and the
    // GUI's Scan button drives SCANNING/IDLE explicitly (see Aufgabe 4/5).
    ctrl.start_available();

    // ── Main loop — mirrors ale_cli.cpp exactly, plus WS command drain ─────
    std::vector<int16_t> rx_buf;
    std::string last_state    = ctrl.display_state();
    bool        last_lbt_busy = false;
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
            if (pending.sfi_dirty) {
                ctrl.set_current_sfi(pending.sfi);
                pending.sfi_dirty = false;
            }
        }

        ctrl.update(t);

        if (audio) {
            rx_buf.clear();
            audio->tick(rx_buf);
            if (!rx_buf.empty())
                ctrl.feed_audio(rx_buf.data(), static_cast<uint32_t>(rx_buf.size()));
        }

        std::string raw;
        while (ws.pop_message(raw)) {
            const mj::Value parsed = mj::parse(raw);
            ws.send_text(dispatch_command(ctx, parsed));
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
    if (ctrl.save_lqa(lqa_path))
        std::printf("[ale_bridge] LQA saved to %s\n", lqa_path.c_str());
    std::printf("[ale_bridge] Exiting.\n");
    return 0;
}
