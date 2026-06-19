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
 *   ale_bridge --self SAM [--port 8765] [--in-device NAME] [--out-device NAME]
 *              [--radio SPEC] [--channels FILE] [--list-devices]
 *
 * Without --in-device/--out-device, the controller runs in the existing
 * "offline" mode (no AudioDevice attached) — useful for protocol-level GUI
 * testing without a sound card.
 *
 * Wire protocol (see docs comment block below dispatch_command()):
 *   GUI -> bridge : {"id":N,"cmd":"...", ...args}   (text frame)
 *   bridge -> GUI : {"id":N,"ok":bool,[...]}          (text frame, command reply)
 *                   {"event":"...", ...}              (text frame, async event)
 *                   <257 float32 LE>                  (binary frame, spectrum)
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"
#include "PAL/radio.h"
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

// "CALLSIGN|name|enabled|net1,net2|chan1,chan2" -> {callsign,name,status,net_members:[],valid_channels:[]/"ALL"}
static mj::Value contact_line_to_json(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) f.push_back(tok);
    f.resize(5);
    mj::Value v = mj::obj();
    v.set("callsign", mj::Value::string(f[0]));
    v.set("name", mj::Value::string(f[1]));
    v.set("status", mj::Value::string(f[2]));
    v.set("net_members", string_array(split_csv(f[3])));
    if (f[4] == "ALL") v.set("valid_channels", mj::Value::string("ALL"));
    else                v.set("valid_channels", string_array(split_csv(f[4])));
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

// "freq|station|snr|ber|sinad|score|age_ms" -> object (see ALEController::get_all_lqa_entries())
static mj::Value lqa_line_to_json(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '|')) f.push_back(tok);
    f.resize(7);
    mj::Value v = mj::obj();
    v.set("freq_hz", mj::Value::number(std::atof(f[0].c_str())));
    v.set("station", mj::Value::string(f[1]));
    v.set("snr_db", mj::Value::number(std::atof(f[2].c_str())));
    v.set("ber", mj::Value::number(std::atof(f[3].c_str())));
    v.set("sinad_db", mj::Value::number(std::atof(f[4].c_str())));
    v.set("score", mj::Value::number(std::atof(f[5].c_str())));
    v.set("age_ms", mj::Value::number(std::atof(f[6].c_str())));
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
    v.set("voice_use", mj::Value::boolean(c.voice_use));
    v.set("data_use", mj::Value::boolean(c.data_use));
    return v;
}

static mj::Value net_to_json(const Net& n) {
    mj::Value v = mj::obj();
    v.set("name", mj::Value::string(n.name));
    v.set("channel_ids", string_array(n.channel_ids));
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
    if (backend == "serial")
        return "hamlib:" + msg.get_string("model", "1") + ":" + msg.get_string("serial");
    // netrigctl (TCP rigctld) — hamlib model 2 = NET_RIGCTL
    return "hamlib:2:tcp://" + msg.get_string("host", "127.0.0.1")
         + ":" + msg.get_string("port", "4532");
}

// The bridge owns the audio device and radio (both runtime-settable from the
// GUI now that there are no startup flags). dispatch_command() needs to reach
// them — not just the controller — to open/close/connect on command. Pointers,
// because both can be null (offline) and get reset() at runtime.
struct BridgeCtx {
    ALEController*                 ctrl;
    std::unique_ptr<AudioDevice>*  audio;
    std::unique_ptr<pal::IRadio>*  radio;
};

// ── Command dispatch ────────────────────────────────────────────────────────
//
// Every command's reply is {"id":<echo>,"ok":bool,...}. "id" is whatever the
// GUI sent (number or absent — JS request counters are the expected case).
// AMD is the one command that goes through process_command() instead of a
// typed method, because AMD-queueing has no dedicated typed entry point on
// ALEController (see process_command()'s CMD:AMD handling).
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
    if (cmd == "AVAILABLE") { ctrl.start_available();  return mj::dump(make_reply(msg, true)); }
    if (cmd == "SOUND")     { return mj::dump(make_reply(msg, ctrl.send_sounding())); }

    if (cmd == "CALL") {
        const bool ok = ctrl.initiate_call(msg.get_string("addr"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "GROUP_CALL") {
        const mj::Value* members = msg.find("members");
        const bool ok = members && ctrl.initiate_group_call(members->as_string_array());
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "ACCEPT")          { ctrl.accept_call();     return mj::dump(make_reply(msg, true)); }
    if (cmd == "REJECT")          { ctrl.reject_call();     return mj::dump(make_reply(msg, true)); }
    if (cmd == "TERMINATE")       { ctrl.terminate_link();  return mj::dump(make_reply(msg, true)); }
    if (cmd == "EMERGENCY_STOP")  { ctrl.emergency_stop();  return mj::dump(make_reply(msg, true)); }

    if (cmd == "AMD") {
        const std::string resp = ctrl.process_command("CMD:AMD " + msg.get_string("text"));
        return mj::dump(make_reply(msg, resp.rfind("OK", 0) == 0));
    }
    if (cmd == "MANUAL_ACCEPT_MODE") {
        ctrl.set_manual_accept_mode(msg.get_bool("on"),
            static_cast<uint32_t>(msg.get_number("timeout_ms", 10000)));
        return mj::dump(make_reply(msg, true));
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
        if (msg.has("enabled"))   ch.enabled   = msg.get_bool("enabled", true);
        if (msg.has("rx_only"))   ch.rx_only   = msg.get_bool("rx_only", false);
        if (msg.has("voice_use")) ch.voice_use = msg.get_bool("voice_use", true);
        if (msg.has("data_use"))  ch.data_use  = msg.get_bool("data_use", true);
        const bool ok = ctrl.add_channel(ch);
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "CHANNEL_DEL") {
        const bool ok = ctrl.del_channel(static_cast<uint32_t>(msg.get_number("rx_hz")));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "CHANNELS_LOAD") {
        const bool ok = ctrl.load_channels(msg.get_string("path"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "CHANNELS_SAVE") {
        const bool ok = ctrl.save_channels(msg.get_string("path"));
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
    if (cmd == "NET_ADD") { return mj::dump(make_reply(msg, ctrl.add_net(msg.get_string("name")))); }
    if (cmd == "NET_DEL") { return mj::dump(make_reply(msg, ctrl.del_net(msg.get_string("name")))); }
    if (cmd == "NET_ASSIGN") {
        const bool ok = ctrl.assign_channel_to_net(msg.get_string("net"), msg.get_string("channel_id"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "NET_UNASSIGN") {
        const bool ok = ctrl.unassign_channel_from_net(msg.get_string("net"), msg.get_string("channel_id"));
        return mj::dump(make_reply(msg, ok));
    }

    // ── Contacts ─────────────────────────────────────────────────────────
    if (cmd == "CONTACTS_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& line : ctrl.get_all_contacts()) list.push_back(contact_line_to_json(line));
        r.set("data", list);
        return mj::dump(r);
    }
    if (cmd == "CONTACT_ADD") {
        const bool ok = ctrl.add_contact(msg.get_string("callsign"), msg.get_string("name"),
            msg.get_string("status", "enabled"), msg.get_string("net_members"),
            msg.get_string("valid_channels", "ALL"));
        return mj::dump(make_reply(msg, ok));
    }
    if (cmd == "CONTACT_DEL")    { return mj::dump(make_reply(msg, ctrl.remove_contact(msg.get_string("callsign")))); }
    if (cmd == "CONTACT_SELECT") { return mj::dump(make_reply(msg, ctrl.select_contact(msg.get_string("callsign")))); }

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
    if (cmd == "SELF_ADDR_DEL")         { return mj::dump(make_reply(msg, ctrl.remove_self_address(msg.get_string("addr")))); }
    if (cmd == "SELF_ADDR_SET_PRIMARY") { return mj::dump(make_reply(msg, ctrl.set_primary_self_address(msg.get_string("addr")))); }

    // ── LQA / signal quality ─────────────────────────────────────────────
    if (cmd == "LQA_LIST") {
        mj::Value r = make_reply(msg, true);
        mj::Value list = mj::arr();
        for (const auto& line : ctrl.get_all_lqa_entries()) list.push_back(lqa_line_to_json(line));
        r.set("data", list);
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
        mj::Value r = make_reply(msg, true);
        r.set("freq_hz", mj::Value::number(ctrl.get_current_frequency()));
        r.set("mode", mj::Value::string(ctrl.get_current_mode()));
        r.set("tune_step_hz", mj::Value::number(ctrl.get_tune_step()));
        r.set("ptt", mj::Value::boolean(ctrl.get_ptt_state()));
        return mj::dump(r);
    }
    if (cmd == "VFO_SET_FREQ")      { return mj::dump(make_reply(msg, ctrl.set_frequency(static_cast<uint32_t>(msg.get_number("hz"))))); }
    if (cmd == "VFO_SET_MODE")      { return mj::dump(make_reply(msg, ctrl.set_mode(msg.get_string("mode")))); }
    if (cmd == "VFO_STEP")          { return mj::dump(make_reply(msg, ctrl.step_channel(static_cast<int>(msg.get_number("direction"))))); }
    if (cmd == "VFO_NUDGE")         { ctrl.nudge_frequency(static_cast<int>(msg.get_number("direction"))); return mj::dump(make_reply(msg, true)); }
    if (cmd == "VFO_SET_TUNE_STEP") { ctrl.set_tune_step(static_cast<uint32_t>(msg.get_number("hz"))); return mj::dump(make_reply(msg, true)); }

    // ── Timing (Level-5 programmable defaults only — see docs/GUI_BRIDGE_GAPS.md) ──
    if (cmd == "TIMING_SET") {
        if (msg.has("scan_dwell_ms"))          ctrl.set_scan_dwell_ms(static_cast<uint32_t>(msg.get_number("scan_dwell_ms")));
        if (msg.has("sounding_interval_sec"))  ctrl.set_sounding_interval_sec(static_cast<uint32_t>(msg.get_number("sounding_interval_sec")));
        if (msg.has("link_idle_timeout_sec"))  ctrl.set_link_idle_timeout_sec(static_cast<uint32_t>(msg.get_number("link_idle_timeout_sec")));
        if (msg.has("max_tune_time_ms"))       ctrl.set_max_tune_time_ms(static_cast<uint32_t>(msg.get_number("max_tune_time_ms")));
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
        const bool ok = (*ctx.audio)->open(msg.get_string("in"), msg.get_string("out"));
        if (ok) ctrl.set_audio_device(ctx.audio->get());
        else    ctx.audio->reset();
        mj::Value r = make_reply(msg, ok);
        if (!ok) r.set("error", mj::Value::string("could not open audio device(s)"));
        return mj::dump(r);
    }
    if (cmd == "AUDIO_CLOSE") {
        if (*ctx.audio) { ctrl.set_audio_device(nullptr); (*ctx.audio)->close(); ctx.audio->reset(); }
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

    // ── Settings ─────────────────────────────────────────────────────────
    if (cmd == "SETTINGS_EXPORT") { return mj::dump(make_reply(msg, ctrl.export_settings(msg.get_string("path")))); }
    if (cmd == "SETTINGS_IMPORT") { return mj::dump(make_reply(msg, ctrl.import_settings(msg.get_string("path")))); }
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
        "WebSocket. The only option is the listen port.\n"
        "\n"
        "Usage:\n"
        "  %s [--port N]\n"
        "\n"
        "Options:\n"
        "  --port N   WebSocket listen port (default 8765)\n",
        prog);
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    uint16_t port = 8765;
    std::string web_root;   // empty → auto-resolve below

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--webroot") == 0 && i + 1 < argc) {
            web_root = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
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
    if (!ws.start(port)) {
        std::fprintf(stderr, "ERROR: Failed to start WebSocket server on port %u.\n", port);
        return 1;
    }

    // ── ALE controller ───────────────────────────────────────────────────
    // Bare start: no self address yet (the GUI sets it via SELF_ADDR_ADD, which
    // makes the first entry primary → set_self_address). The SM won't recognise
    // calls until then, which is correct for an unconfigured station.
    ALEController ctrl;

    BridgeCtx ctx{ &ctrl, &audio, &radio };

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
    ctrl.on_amd_received = [&](const std::string& from, const std::string& text) {
        mj::Value e = make_event("amd_received");
        e.set("from", mj::Value::string(from));
        e.set("text", mj::Value::string(text));
        ws.send_text(mj::dump(e));
    };
    // Fires inline from ctrl.feed_audio() below — i.e. on THIS main-loop thread,
    // not the WASAPI audio thread (the modem's spectrum FFT runs in
    // push_samples()). send_binary() is mutex-protected in WsServer regardless.
    // See docs/THREADING.md.
    ctrl.set_spectrum_callback([&](const float* bins, size_t n, float /*hz_per_bin*/) {
        ws.send_binary(bins, n * sizeof(float));
    });

    std::printf("[ale_bridge] listening on port %u — waiting for GUI to configure\n", port);
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
    std::string last_state = ctrl.display_state();
    while (g_running) {
        const uint32_t t = static_cast<uint32_t>(timer->get_time_ms());
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

        timer->sleep_ms(1);
    }

    ctrl.emergency_stop();
    if (radio) radio->stop();
    if (audio) audio->close();
    ws.stop();
    std::printf("[ale_bridge] Exiting.\n");
    return 0;
}
