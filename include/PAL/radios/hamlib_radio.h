#pragma once

#include "PAL/radio.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>


// Forward-declare hamlib's RIG so this header compiles without <hamlib/rig.h>.
// The full definition is only needed in hamlib_radio.cpp, where it is included.
typedef struct s_rig RIG;

namespace pal {

/**
 * @brief Konfiguriert den physischen Leitungsstatus serieller Ports nach dem Öffnen.
 *
 * Viele CAT-Interfaces (z. B. TS-480 USB-Wandler) benötigen DTR=HIGH und RTS=HIGH
 * als Bias-Signal, unabhängig von Flow-Control.  Das ist kein Handshake-Requirement,
 * sondern ein physischer Line-State der vom Gerät/Interface benötigt wird.
 *
 *  ON   = Leitung dauerhaft HIGH setzen nach rig_open()
 *  OFF  = Leitung dauerhaft LOW setzen nach rig_open()
 *  AUTO = Hamlib/OS entscheiden (keine explizite Manipulation)
 */
struct SerialLinePolicy {
    enum class State { ON, OFF, AUTO };

    // CAT-seitige PTT-Audioquelle (z. B. Kenwood TX0=Mic / TX1=Data). Nur
    // Rigs mit Mic/Data-Unterscheidung in Hamlib (Kenwood-Familie: TS-480,
    // TS-590, TS-890, …) werten das aus; alle anderen ignorieren es und
    // bekommen weiterhin das generische PTT ON.
    enum class PttInput { NORMAL, MIC, DATA };

    State    dtr              = State::ON;   ///< DTR-Zustand nach Port-Open (default: HIGH)
    State    rts              = State::ON;   ///< RTS-Zustand nach Port-Open (default: HIGH)
    uint32_t stabilization_ms = 200;         ///< Wartezeit nach Port-Open vor erstem CAT-Befehl
    PttInput ptt_input        = PttInput::NORMAL;  ///< CAT-Audioeingang bei PTT ON

    // Relay-click workaround (see rig_avoid_relay_click doc in
    // ale_station_config.h). No-op on rigs whose Hamlib backend doesn't
    // implement split VFO control (see split_supported_).
    bool avoid_relay_click    = false;
};

/**
 * @brief Konfiguriert, über welchen Mechanismus PTT geschaltet wird.
 *
 * Separiert von SerialLinePolicy, weil das ein eigenständiges Hamlib-Konzept
 * ist: rig_state trägt einen eigenen pttport (eigener hamlib_port_t), getrennt
 * vom rigport (CAT-Verbindung). CAT = PTT via rig_set_ptt() über die CAT-
 * Verbindung selbst (Standardfall). RTS/DTR = PTT über eine serielle
 * Steuerleitung, ggf. auf einem separaten Gerät (Zwei-Kabel-Aufbau); bleibt
 * `port` leer, wird das Gerät des CAT-Ports (rigport) mitbenutzt (Ein-Kabel-
 * Aufbau über einen Adapter). NONE = Hamlib-No-Op (VOX-getastete Rigs).
 */
struct PttPolicy {
    enum class Type { CAT, RTS, DTR, NONE };
    Type        type = Type::CAT;
    std::string port;  // "" = teilt sich das Gerät des CAT-rigport
};

/**
 * @brief Hamlib-basierte IRadio-Implementierung.
 *
 * Unterstützt direkte Geräteansteuerung über serielle Schnittstellen
 * sowie TCP-Anbindung an einen rigctld-Server.
 *
 * Alle zeitkritischen Methoden (set_channel, set_ptt, sync_from_radio, …)
 * sind nicht-blockierend: sie geben Kommandos in eine interne Warteschlange und
 * kehren sofort zurück. Ein dedizierter Worker-Thread führt die eigentlichen
 * Hamlib-Aufrufe seriell aus, damit der ALE-Protokollkern (1 ms Tick-Schleife)
 * nie durch CAT-Latenz blockiert wird.
 *
 * Lifecycle (synchron, wie bisher):
 *   initialize() → start() → [Betrieb] → stop() → shutdown()
 * start() startet den Worker-Thread; stop() beendet ihn vor rig_close().
 */
class HamlibRadio : public IRadio {
public:
    /**
     * @brief Erzeugt einen Hamlib-Radio-Adapter.
     * @param model Hamlib-Modell-ID als String (z. B. "229")
     * @param port Verbindungsziel, z. B. "/dev/ttyUSB0" oder "127.0.0.1:4532"
     * @param baud Baud-Rate (0 = Hamlib-Default, typisch 19200)
     */
    HamlibRadio(const std::string& model, const std::string& port, int baud = 0,
                SerialLinePolicy policy = {}, PttPolicy ptt_policy = {});

    ~HamlibRadio() override;

    bool initialize() override;
    void shutdown()   override;
    bool start()      override;   ///< rig_open() + startet den Worker-Thread
    void stop()       override;   ///< beendet Worker-Thread, dann rig_close()

    // --- Nicht-blockierende Echtzeit-Methoden (Kommando → Warteschlange) ---

    /**
     * @brief Setzt Frequenz und Modus; kehrt sofort zurück.
     *
     * Der optimistische Rückgabewert ist immer true.  Der eigentliche
     * Hamlib-Aufruf erfolgt asynchron im Worker-Thread.
     */
    bool    set_channel(const Channel& channel) override;

    /**
     * @brief Gibt den zuletzt angeforderten Kanal zurück (thread-sicher, gecacht).
     */
    Channel get_channel()                       const override;

    /**
     * @brief Setzt nur die Frequenz; kehrt sofort zurück.
     */
    bool    set_frequency(uint32_t hz)          override;

    /**
     * @brief Setzt nur den Modus; kehrt sofort zurück.
     */
    bool    set_mode(RadioMode mode)            override;

    /**
     * @brief Setzt nur die TX-Leistung (0-100%); kehrt sofort zurück.
     *
     * No-op (returns true optimistically, hardware unchanged) if the
     * connected rig doesn't support RIG_LEVEL_RFPOWER — see
     * supports_power_control().
     */
    bool    set_power(int pct)                  override;

    /**
     * @brief Stellt eine Synchronisation vom Radio in die Warteschlange.
     *
     * Gibt immer false zurück; das Ergebnis wird asynchron im Worker-Thread
     * angewendet.  Alle Aufrufer ignorieren den Rückgabewert.
     */
    bool    sync_from_radio()                   override;

    /**
     * @brief Blockiert, bis alle bisher eingereihten Kommandos abgearbeitet sind.
     *
     * Nützlich in Tests, um nach set_channel() / sync_from_radio() auf das
     * tatsächliche Senden zu warten, bevor Seiteneffekte geprüft werden.
     * Ist Worker nicht aktiv, kehrt sofort zurück.
     */
    void    flush()                             override;

    /**
     * @brief Schaltet PTT; kehrt sofort zurück.
     *
     * transmitting() gibt optimistisch den gesetzten Wert zurück; der Worker
     * korrigiert bei Hamlib-Fehler.
     */
    void set_ptt(bool transmit) override;

    bool        is_transmitting() const override;  ///< atomarer Lesezugriff
    bool        is_ready()        const override;  ///< atomarer Lesezugriff
    std::string get_port_config() const override;

    // Async tune-settle tracking (see IRadio). All tune commands run on the I/O
    // worker; tunes_in_flight_ tracks how many are still outstanding, so the ALE
    // controller can relay "radio settled?" to the scanner as a bool hop gate.
    bool is_tune_settled() const override;

    void register_send_callback(SendCommandCallback callback) override;
    void register_ack_callback(AckCallback callback)          override;
    void process_response(const uint8_t* data, size_t length) override;

    // Optional CAT-traffic diagnostics (see IRadio). set_cat_trace_enabled()
    // is safe to call from any thread; drain_cat_trace() is a polling read
    // meant for the controller's own tick, mirroring get_channel()'s
    // cached-state pattern rather than a cross-thread callback into it.
    void set_cat_trace_enabled(bool on)         override;
    std::vector<std::string> drain_cat_trace()  override;

    // True once start() has confirmed the connected rig advertises
    // RIG_LEVEL_RFPOWER support (rig_has_set_level). Safe to read from any
    // thread — set exactly once in start(), before the worker launches.
    bool supports_power_control() const override;

private:
    bool configure_port();
    void apply_line_policy();
    bool is_serial_port() const;

    // Formats and (when tracing is enabled) buffers one CAT-trace line.
    // Worker-thread-only call sites; hand-off to drain_cat_trace() is via
    // trace_mtx_. Bounded so an enabled-but-unpolled buffer cannot grow
    // without limit.
    void trace_cat(const char* fmt, ...);

    // Sendet `mode` genau einmal als autoritativen Per-Hop-Force (kein Readback-
    // Loop, kein sleep). Asynchrone Band-Mode-Reverts fängt der verzögerte
    // Hintergrund-Verify (tick_mode_verify -> sync_from_radio). Nur vom Worker.
    int  assert_mode(RadioMode mode);

    // Sends `pct` (0-100%) as RIG_LEVEL_RFPOWER exactly once — the power
    // analogue of assert_mode(). No-op (returns RIG_OK without touching the
    // wire) if power_supported_ is false. Only vom Worker.
    int  assert_power(int pct);

    // Relay-click workaround: puts the rig into (or out of) SPLIT mode via
    // rig_set_split_vfo(). No-op if split_supported_ is false. Only vom Worker.
    void assert_split(bool on, const char* reason);

    // ── Async-Worker-Kommandotypen ────────────────────────────────────────────
    struct CmdSetChannel   { Channel ch; };
    struct CmdSetFrequency { uint32_t hz; };
    struct CmdSetMode      { RadioMode mode; };
    struct CmdSetPower     { int pct; };
    struct CmdSetPtt       { bool on; };
    struct CmdSync         {};
    struct CmdFlush        { std::shared_ptr<std::promise<void>> done; };

    using RadioCommand = std::variant<
        CmdSetChannel, CmdSetFrequency, CmdSetMode, CmdSetPower, CmdSetPtt, CmdSync, CmdFlush>;

    // Thread-safe; no-op if the worker isn't running. Time-critical commands
    // (SetChannel/SetFrequency/SetMode/SetPtt — the ones a scan hop or the SM's
    // TX timing is waiting on) jump the queue ahead of any already-queued but
    // not-yet-started CmdSync/CmdSetPower, instead of sitting behind it. See the
    // .cpp for why: without this, the ~400 ms background sync_from_radio() poll
    // (2 blocking CAT round-trips) can occasionally still be queued when a scan
    // hop's dwell timer expires, adding an unpredictable extra 1-2 round-trips of
    // settle latency to that one hop — read by the operator as the configured
    // dwell time randomly fluctuating.
    void enqueue(RadioCommand cmd);

    // SetChannel/SetFrequency/SetMode gate scan-hop readiness (tunes_in_flight_);
    // SetPtt gates the SM's TX timing (Twt/Tt). CmdSync (background mode/freq
    // readback) and CmdSetPower do not — nothing is waiting on them.
    static bool is_urgent(const RadioCommand& cmd);

    void worker_main();
    void worker_dispatch(const RadioCommand& cmd);
    void stop_worker_();               ///< signalisiert Exit, joined, leert Queue

    // Blockierende Hamlib-Implementierungen — ausschließlich vom Worker-Thread:
    bool impl_set_channel(const Channel& ch);
    bool impl_set_frequency(uint32_t hz);
    bool impl_set_mode(RadioMode mode);
    bool impl_set_power(int pct);
    void impl_set_ptt(bool on);
    bool impl_sync_from_radio();

    // ── Konfiguration (unveränderlich nach Konstruktion) ─────────────────────
    std::string      model_;
    std::string      port_;
    int              baud_;
    SerialLinePolicy policy_;
    PttPolicy        ptt_policy_;

    RIG* rig_ = nullptr;

    // Set once in start() via rig_has_set_level(RFPOWER) — a capability check
    // only, no I/O — before the worker thread launches. Read from any thread
    // afterward (never written again). See IRadio::supports_power_control().
    // Gates the WRITE path (assert_power/set_power/impl_set_channel) only.
    std::atomic<bool> power_supported_{false};

    // Set once in start() via rig_has_get_level(RFPOWER) — separate from
    // power_supported_ above because Hamlib tracks set- and get-capability as
    // independent bits, and plenty of real rig backends can SET RFPOWER over
    // CAT but never implement reading it back (most rigs don't report actual
    // TX power via CAT at all). Gates ONLY the periodic readback in
    // impl_sync_from_radio() — reusing power_supported_ there would poll a
    // GET the backend never claimed to support, failing forever.
    std::atomic<bool> power_readback_supported_{false};

    // Set once in start() from rig_->caps->set_split_vfo != nullptr — a
    // capability check only, no I/O. Gates the relay-click workaround
    // (assert_split() call sites in impl_set_channel/impl_set_frequency/
    // impl_set_ptt); a rig whose backend doesn't implement split VFO control
    // is never sent split commands.
    std::atomic<bool> split_supported_{false};

    // ── Worker-only State (nach start() kein konkurrierender Zugriff nötig) ──
    // last_mode_cmd_ und current_channel_ werden ausschließlich vom Worker-Thread
    // gelesen und geschrieben; kein Mutex erforderlich.
    std::chrono::steady_clock::time_point last_mode_cmd_{};
    Channel current_channel_;  ///< zuletzt tatsächlich gesendeter Kanal (Worker-Zustand)
    // §A.5.3.3 scan-hop latency: mode last actually sent to the rig. impl_set_channel
    // skips rig_set_mode when the channel mode is unchanged (1 CAT round-trip/hop instead
    // of 2 over netrigctl); drift is still corrected by the background sync_from_radio verify.
    RadioMode last_sent_mode_{};
    bool      mode_ever_sent_ = false;
    // Intended SPLIT state on the wire, tracked so assert_split() call sites
    // only issue a command when it actually needs to change. Worker-only.
    bool      split_state_ = false;

    // ── Async Worker ──────────────────────────────────────────────────────────
    std::thread              worker_;
    std::deque<RadioCommand> cmd_queue_;
    std::mutex               queue_mtx_;
    std::condition_variable  queue_cv_;
    std::atomic<bool>        worker_running_{false};

    // ── Haupt-Thread-lesbarer Kanal-Cache (mutex-geschützt) ──────────────────
    mutable std::mutex channel_mtx_;
    Channel            cached_channel_;  ///< optimistisch + nach jedem Worker-Dispatch aktualisiert

    // ── Atomare Statusindikatoren (thread-sicher lesbar) ─────────────────────
    std::atomic<bool>   transmitting_{false};
    std::atomic<bool>   ready_{false};

    // ── Tune-in-flight counter (main thread ++, worker --) ──────────────────
    // Incremented on the main thread when a tune command (set_channel /
    // set_frequency / set_mode) is enqueued, decremented by the worker after the
    // tune completes. is_tune_settled() == (tunes_in_flight_ == 0), a
    // level-triggered signal with no sequence/edge to mis-read. CmdSync /
    // CmdSetPtt / CmdFlush never touch it, so a sync poll or PTT toggle can never
    // fake a settle. Reset to 0 in start(). The scanner (via the controller's
    // hop-ready gate) only issues the next hop once this reaches 0, which upholds
    // "at most one tune in flight".
    std::atomic<int> tunes_in_flight_{0};

    SendCommandCallback send_callback_;
    AckCallback         ack_callback_;

    // ── CAT-traffic trace buffer (see set_cat_trace_enabled/drain_cat_trace) ──
    std::atomic<bool>       cat_trace_enabled_{false};
    std::mutex               trace_mtx_;
    std::deque<std::string>  trace_lines_;   // capped at kCatTraceCap in trace_cat()
};

// Set HamlibRadio log verbosity. Mirrors the GUI cfgLogLevel values:
//   0=Off  1=Error  2=Info  3=Debug  4=Trace
// Info: channel/freq transitions. Debug: assert_mode internals + sync detail.
void hamlib_set_log_level(int level);

struct RigEntry {
    int         model;
    std::string mfg;
    std::string macro;     // RIG_MODEL_ prefix stripped
    std::string port_type;  // coarse connection kind: "network" | "serial" | "other"
};

// Returns all rigs registered in the linked Hamlib, sorted by manufacturer then macro name.
std::vector<RigEntry> list_rigs();

// Coarse port type for a single model number, derived from rig_caps::port_type
// (the single source of truth for whether a rig connects over the network or a
// serial device). Returns "network" | "serial" | "other"; "other" if unknown.
std::string rig_port_type(int model);

} // namespace pal
