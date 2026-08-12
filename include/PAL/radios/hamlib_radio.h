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
#include <queue>
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
                SerialLinePolicy policy = {});

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

    // ── Async-Worker-Kommandotypen ────────────────────────────────────────────
    struct CmdSetChannel   { Channel ch; };
    struct CmdSetFrequency { uint32_t hz; };
    struct CmdSetMode      { RadioMode mode; };
    struct CmdSetPtt       { bool on; };
    struct CmdSync         {};
    struct CmdFlush        { std::shared_ptr<std::promise<void>> done; };

    using RadioCommand = std::variant<
        CmdSetChannel, CmdSetFrequency, CmdSetMode, CmdSetPtt, CmdSync, CmdFlush>;

    void enqueue(RadioCommand cmd);    ///< thread-sicher; no-op wenn Worker nicht läuft
    void worker_main();
    void worker_dispatch(const RadioCommand& cmd);
    void stop_worker_();               ///< signalisiert Exit, joined, leert Queue

    // Blockierende Hamlib-Implementierungen — ausschließlich vom Worker-Thread:
    bool impl_set_channel(const Channel& ch);
    bool impl_set_frequency(uint32_t hz);
    bool impl_set_mode(RadioMode mode);
    void impl_set_ptt(bool on);
    bool impl_sync_from_radio();

    // ── Konfiguration (unveränderlich nach Konstruktion) ─────────────────────
    std::string      model_;
    std::string      port_;
    int              baud_;
    SerialLinePolicy policy_;

    RIG* rig_ = nullptr;

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

    // ── Async Worker ──────────────────────────────────────────────────────────
    std::thread              worker_;
    std::queue<RadioCommand> cmd_queue_;
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
