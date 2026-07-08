#pragma once

#include "PAL/radio.h"
#include <chrono>
#include <cstdint>
#include <string>
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
    State    dtr              = State::ON;   ///< DTR-Zustand nach Port-Open (default: HIGH)
    State    rts              = State::ON;   ///< RTS-Zustand nach Port-Open (default: HIGH)
    uint32_t stabilization_ms = 200;         ///< Wartezeit nach Port-Open vor erstem CAT-Befehl
};

/**
 * @brief Hamlib-basierte IRadio-Implementierung.
 *
 * Unterstützt direkte Geräteansteuerung über serielle Schnittstellen
 * sowie TCP-Anbindung an einen rigctld-Server.
 *
 * Der Verbindungsstring kann je nach Anwendungskonvention entweder
 * eine serielle Schnittstelle oder ein Netzwerkziel beschreiben.
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

    /**
     * @brief Gibt Ressourcen frei und schließt die Verbindung.
     */
    ~HamlibRadio() override;

    /**
     * @brief Initialisiert den Hamlib-Handle und konfiguriert das Ziel.
     * @return true bei Erfolg
     */
    bool initialize() override;

    /**
     * @brief Trennt Verbindung und setzt den Adapter zurück.
     */
    void shutdown() override;

    /**
     * @brief Öffnet die Verbindung zum Gerät oder rigctld.
     * @return true bei Erfolg
     */
    bool start() override;

    /**
     * @brief Schließt die aktive Verbindung.
     */
    void stop() override;

    /**
     * @brief Setzt Frequenz und Modus auf den übergebenen Kanal.
     * @param channel Zielkanal
     * @return true bei Erfolg
     */
    bool set_channel(const Channel& channel) override;

    /**
     * @brief Liefert den zuletzt gesetzten Kanal.
     * @return Aktueller Kanalzustand
     */
    Channel get_channel() const override;

    // Direct single-attribute setters — over TCP these send exactly one CAT
    // command (rig_set_freq / rig_set_mode) without the read-modify-write that
    // set_channel() does, so a manual mode set never re-sends the frequency and
    // vice versa. See set_channel() for the VFO/passband/order rationale.
    bool set_frequency(uint32_t hz) override;
    bool set_mode(RadioMode mode) override;

    // Query the radio for its live frequency and mode and update current_channel_.
    // Returns true if either changed (caller may push a channel_changed event).
    // Over TCP/netrigctl this goes to the wire when the hamlib cache has expired
    // (500 ms timeout set in start()).
    bool sync_from_radio() override;

    /**
     * @brief Schaltet PTT auf Senden oder Empfang.
     * @param transmit true für TX, false für RX
     */
    void set_ptt(bool transmit) override;

    /**
     * @brief Prüft, ob aktuell gesendet wird.
     * @return true bei TX
     */
    bool is_transmitting() const override;

    /**
     * @brief Prüft, ob der Adapter initialisiert ist.
     * @return true wenn bereit
     */
    bool is_ready() const override;

    /**
     * @brief Gibt die konfigurierte Portbeschreibung zurück.
     * @return Port-Konfiguration als String
     */
    std::string get_port_config() const override;

    /**
     * @brief Registriert einen Send-Callback.
     * @param callback Callback für ausgehende Kommandos
     */
    void register_send_callback(SendCommandCallback callback) override;

    /**
     * @brief Registriert einen ACK-Callback.
     * @param callback Callback für Bestätigungen
     */
    void register_ack_callback(AckCallback callback) override;

    /**
     * @brief Verarbeitet eine eingehende Antwort.
     *
     * Hamlib kapselt die physische Transportebene; dieser Adapter
     * verarbeitet daher typischerweise keine Rohantworten.
     *
     * @param data Eingabepuffer
     * @param length Anzahl Bytes
     */
    void process_response(const uint8_t* data, size_t length) override;

private:
    /**
     * @brief Wendet die Portkonfiguration auf den Hamlib-Handle an.
     * @return true bei Erfolg
     */
    bool configure_port();

    // Setzt DTR/RTS nach erfolgreichem rig_open() gemäß policy_.
    // Versucht erst hamlib-Token-API, dann direkten Windows-HANDLE-Fallback.
    void apply_line_policy();

    bool is_serial_port() const;

    // Sends `mode`, then reads it back LIVE and re-sends until the rig reports the
    // intended mode (bounded, NO delay — must not perturb ALE core timing). Defeats
    // an SDR front-end (e.g. Quisk) that overrides mode on a band/frequency change.
    // Returns the last rig_set_mode() return code. Requires the mode cache be live
    // (HAMLIB_CACHE_MODE = 0, set in start()) or the readback just echoes the set.
    int assert_mode(RadioMode mode);

    std::string      model_;
    std::string      port_;
    int              baud_;
    SerialLinePolicy policy_;

    RIG* rig_ = nullptr;

    // When openALE last commanded a mode (assert_mode). sync_from_radio() only
    // re-asserts the intended mode within a short window after this, so a
    // deliberate external mode change (operator on the rig/SDR) is respected
    // once the dust from our own command has settled.
    std::chrono::steady_clock::time_point last_mode_cmd_{};

    Channel current_channel_;
    bool transmitting_ = false;
    bool ready_ = false;

    SendCommandCallback send_callback_;
    AckCallback ack_callback_;
};

// Set HamlibRadio log verbosity. Mirrors the GUI cfgLogLevel values:
//   0=Off  1=Error  2=Info  3=Debug  4=Trace
// Info: channel/freq transitions. Debug: assert_mode internals + sync detail.
void hamlib_set_log_level(int level);

struct RigEntry {
    int         model;
    std::string mfg;
    std::string macro; // RIG_MODEL_ prefix stripped
};

// Returns all rigs registered in the linked Hamlib, sorted by manufacturer then macro name.
std::vector<RigEntry> list_rigs();

} // namespace pal