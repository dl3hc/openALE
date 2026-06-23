// hamlib_radio.h
#pragma once

#include "pal/radio.h"
#include <hamlib/rig.h>
#include <cstdint>
#include <string>

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
     * @brief Bestimmt das Hamlib-Modus-Enum aus der Radio-Mode-Zeichenkette.
     * @param mode Modusname, z. B. "USB" oder "LSB"
     * @return Passendes Hamlib rmode_t
     */
    rmode_t to_hamlib_mode(RadioMode mode) const;

    /**
     * @brief Wendet die Portkonfiguration auf den Hamlib-Handle an.
     * @return true bei Erfolg
     */
    bool configure_port();

    // Setzt DTR/RTS nach erfolgreichem rig_open() gemäß policy_.
    // Versucht erst hamlib-Token-API, dann direkten Windows-HANDLE-Fallback.
    void apply_line_policy();

    bool is_serial_port() const;

    std::string      model_;
    std::string      port_;
    int              baud_;
    SerialLinePolicy policy_;

    RIG* rig_ = nullptr;

    Channel current_channel_;
    bool transmitting_ = false;
    bool ready_ = false;

    SendCommandCallback send_callback_;
    AckCallback ack_callback_;
};

} // namespace pal