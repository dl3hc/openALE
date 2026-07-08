/**
 * \file ale_monitor/src/ale_monitor.cpp
 * \brief Passive ALE traffic monitor — RX-only WebSocket bridge.
 *
 * Decodes and logs all ALE traffic on the configured channels.
 * Never transmits.  All config (audio, radio, channel file) is via CLI;
 * the browser GUI is display-only.
 *
 * Usage:
 *   ale_monitor --port N --net-file path/to/nets/USA.ale
 *               [--remote] [--audio "IN:device"] [--rig "hamlib:2:tcp://host:4532"]
 *               [--lqa-file monitor_lqa.bin] [--webroot DIR]
 */

#include "App/ale_controller.h"
#include "App/audio_device.h"
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
#include <sstream>
#include <string>
#include <vector>

using namespace ale;
namespace mj = bridge::minijson;

// ── Signal handling ──────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};
static void sig_handler(int) { g_running = false; }

// ── Web-root resolution ──────────────────────────────────────────────────────

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
static std::string exe_dir() { return ""; }
#endif

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

// ── Small helpers ────────────────────────────────────────────────────────────

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

// ── Context ──────────────────────────────────────────────────────────────────

struct MonitorCtx {
    ALEController*                ctrl;
    std::unique_ptr<AudioDevice>* audio;
    std::string                   lqa_path;
    std::string                   audio_in;
};

// ── Command dispatch ─────────────────────────────────────────────────────────
//
// Wire protocol: GUI → bridge: {"id":N,"cmd":"...", ...args}   (text frame)
//                bridge → GUI: {"id":N,"ok":bool,...}          (reply)
//                              {"event":"...",...}              (async event)
//                              <float32 spectrum bins × N>      (binary frame)
//
// Monitor exposes a SUBSET of openALE's command set — only what is needed for
// passive display.  Commands that modify channels, load audio, or connect a
// radio at runtime are intentionally absent; all config is at CLI startup.

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
    if (cmd == "AUDIO_LEVEL") {
        mj::Value r = make_reply(msg, true);
        r.set("level", mj::Value::number(ctrl.get_audio_input_level()));
        return mj::dump(r);
    }
    // Accept but ignore — monitor has no TX, GUI may still call this
    if (cmd == "AUDIO_SET_VOL") {
        return mj::dump(make_reply(msg, true));
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
        if (!ctx.lqa_path.empty()) ctrl.save_lqa(ctx.lqa_path);
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

    mj::Value r = make_reply(msg, false);
    r.set("error", mj::Value::string("unknown command: " + cmd));
    return mj::dump(r);
}

// ── Usage ────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "ALE Traffic Monitor — passive RX-only WebSocket bridge\n"
        "\n"
        "Decodes all ALE traffic on the configured channels and streams it\n"
        "to a browser UI.  Never transmits.  All config is via CLI flags;\n"
        "the GUI is display-only (no audio/radio controls in the browser).\n"
        "\n"
        "Usage:\n"
        "  %s --port N --net-file FILE [options]\n"
        "\n"
        "Required:\n"
        "  --port N           WebSocket listen port\n"
        "  --net-file FILE    .ale channel file (e.g. nets/USA.ale)\n"
        "\n"
        "Options:\n"
        "  --remote           Bind to 0.0.0.0 instead of 127.0.0.1\n"
        "  --audio SPEC       Audio input device, e.g. \"IN:CABLE Output\"\n"
        "  --rig SPEC         Hamlib radio spec, e.g. \"hamlib:2:tcp://127.0.0.1:4532\"\n"
        "  --lqa-file FILE    LQA persistence file (default: monitor_lqa.bin)\n"
        "  --webroot DIR      Serve GUI from DIR instead of auto-detected path\n"
        "\n"
        "Examples:\n"
        "  %s --port 8081 --net-file nets/USA.ale\n"
        "  %s --port 8081 --net-file nets/EUR.ale --rig \"hamlib:2:tcp://localhost:4532\"\n"
        "       --audio \"IN:CABLE Output\" --remote\n",
        prog, prog, prog);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    uint16_t    port        = 0;
    bool        bind_remote = false;
    std::string net_file;
    std::string startup_audio;
    std::string rig_spec;
    std::string lqa_path   = "monitor_lqa.bin";
    std::string web_root;

    for (int i = 1; i < argc; ++i) {
        if      (std::strcmp(argv[i], "--port")     == 0 && i + 1 < argc) { port        = static_cast<uint16_t>(std::atoi(argv[++i])); }
        else if (std::strcmp(argv[i], "--remote")   == 0)                  { bind_remote = true; }
        else if (std::strcmp(argv[i], "--net-file") == 0 && i + 1 < argc) { net_file    = argv[++i]; }
        else if (std::strcmp(argv[i], "--audio")    == 0 && i + 1 < argc) { startup_audio = argv[++i]; }
        else if (std::strcmp(argv[i], "--rig")      == 0 && i + 1 < argc) { rig_spec    = argv[++i]; }
        else if (std::strcmp(argv[i], "--lqa-file") == 0 && i + 1 < argc) { lqa_path    = argv[++i]; }
        else if (std::strcmp(argv[i], "--webroot")  == 0 && i + 1 < argc) { web_root    = argv[++i]; }
        else if (std::strcmp(argv[i], "--help")     == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]); return 0;
        }
    }

    if (port == 0) {
        std::fprintf(stderr, "ERROR: --port <N> is required.\n\n");
        print_usage(argv[0]);
        return 1;
    }
    if (net_file.empty()) {
        std::fprintf(stderr, "ERROR: --net-file <FILE> is required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (web_root.empty()) web_root = resolve_web_root();

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    auto timer = pal::create_timer();

    // ── WebSocket server ─────────────────────────────────────────────────
    bridge::WsServer ws;
    ws.set_web_root(web_root);
    if (!ws.start(port, bind_remote)) {
        std::fprintf(stderr, "ERROR: Failed to start WebSocket server on port %u.\n", port);
        return 1;
    }

    // ── ALE controller ───────────────────────────────────────────────────
    // No self-address: the SM will never respond to calls — purely passive.
    ALEController ctrl;

    if (ctrl.load_lqa(lqa_path))
        std::printf("[ale_monitor] LQA loaded from %s\n", lqa_path.c_str());

    // ── Radio ────────────────────────────────────────────────────────────
    std::unique_ptr<pal::IRadio> radio;
    if (!rig_spec.empty()) {
        radio = pal::create_radio(rig_spec);
        if (radio && radio->initialize() && radio->start()) {
            ctrl.set_radio(radio.get());
            std::printf("[ale_monitor] Radio attached: %s\n", rig_spec.c_str());
        } else {
            std::fprintf(stderr, "WARNING: Could not connect radio (%s) — continuing without rig\n",
                         rig_spec.c_str());
            radio.reset();
        }
    }

    // ── Net file (required) ──────────────────────────────────────────────
    const bool loaded = ctrl.load_station_file(net_file);
    if (!loaded || ctrl.channels().empty()) {
        std::fprintf(stderr, "ERROR: No channels loaded from '%s'\n", net_file.c_str());
        return 1;
    }
    std::printf("[ale_monitor] Loaded %zu channel(s) from %s\n",
                ctrl.channels().size(), net_file.c_str());

    // ── Audio (RX-only) ──────────────────────────────────────────────────
    std::unique_ptr<AudioDevice> audio;
    if (!startup_audio.empty()) {
        audio = make_audio_device();
        if (audio->open(startup_audio, "")) {   // empty out-device = RX-only
            ctrl.set_audio_device(audio.get());
            std::printf("[ale_monitor] Audio RX opened: %s\n", startup_audio.c_str());
        } else {
            std::fprintf(stderr, "WARNING: Could not open audio device '%s' — no audio input\n",
                         startup_audio.c_str());
            audio.reset();
        }
    }

    MonitorCtx ctx{ &ctrl, &audio, lqa_path, startup_audio };

    // ── Callbacks ────────────────────────────────────────────────────────
    ctrl.on_status_changed = [&](const std::string& m) {
        mj::Value e = make_event("status");
        e.set("msg", mj::Value::string(m));
        ws.send_text(mj::dump(e));
    };

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

    ctrl.on_channel_changed = [&](const Channel& ch) {
        mj::Value e = make_event("channel_changed");
        e.set("channel_id", mj::Value::string(ch.id));
        e.set("rx_hz",      mj::Value::number(ch.rx_frequency_hz));
        e.set("tx_hz",      mj::Value::number(ch.tx_frequency_hz));
        e.set("mode",       mj::Value::string(ch.rx_mode));
        ws.send_text(mj::dump(e));
    };

    ctrl.set_spectrum_callback([&](const float* bins, size_t n, float /*hz_per_bin*/) {
        ws.send_binary(bins, n * sizeof(float));
    });

    // ── Startup ──────────────────────────────────────────────────────────
    if (ctrl.channels().size() >= 2) {
        ctrl.start_scanning();
        std::printf("[ale_monitor] Scanning started (%zu channels)\n",
                    ctrl.channels().size());
    } else {
        ctrl.start_available();
        std::printf("[ale_monitor] Single channel — monitoring in AVAILABLE state\n");
    }

    std::printf("[ale_monitor] Listening on %s:%u\n",
                bind_remote ? "0.0.0.0" : "127.0.0.1", port);
    if (web_root.empty())
        std::printf("[ale_monitor] GUI not found — open via file:// or pass --webroot\n");
    else
        std::printf("[ale_monitor] Open GUI:  http://localhost:%u/index.html\n", port);
    std::fflush(stdout);

    // ── Main loop ────────────────────────────────────────────────────────
    std::vector<int16_t> rx_buf;
    std::string last_state    = ctrl.display_state();
    bool        last_lbt_busy = false;

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

        timer->sleep_ms(1);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    ctrl.emergency_stop();
    if (radio) radio->stop();
    if (audio) audio->close();
    ws.stop();
    if (ctrl.save_lqa(lqa_path))
        std::printf("[ale_monitor] LQA saved to %s\n", lqa_path.c_str());
    std::printf("[ale_monitor] Exiting.\n");
    return 0;
}
