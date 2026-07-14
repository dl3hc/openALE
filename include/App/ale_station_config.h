/**
 * \file App/ale_station_config.h
 * \brief ALEStationConfig — alle Operator-konfigurierbaren Verhaltensparameter.
 *
 * Kanonische Quelle für persistierbare Stationseinstellungen.
 * Persistiert via ALEController::export_settings() / import_settings().
 *
 * Was NICHT hier steht:
 *  - Channel / Net / Contact / SelfAddressEntry  — eigene Datenstores
 *  - TimingParameters in ale_timing.h            — Protokoll-Konstanten
 *  - Audiogerät, Rig-Backend                     — Laufzeit-Session-Parameter
 */
#pragma once
#include <cstdint>
#include <string>
#include "FEC/golay.h"

namespace ale {

struct ALEStationConfig {

    // ── Scanning-Call-Planung ──────────────────────────────────────────────
    /// Angenommene Scan-Kanal-Anzahl der Gegenstelle (Tsc = C × 2 × Trw).
    /// Einzige Quelle für die Scanning-Call-Länge — wird nie automatisch
    /// überschrieben. 10 = MIL-STD-Maximum (PCALE-Default, interoperabel).
    uint32_t  assumed_scan_channels  = 10;

    // ── Eingehende Anrufe ──────────────────────────────────────────────────
    /// true = Anruf erst nach accept_call() weitergeben (kein Auto-Accept).
    bool      manual_accept_mode     = false;
    /// Timeout für manuelle Accept-Entscheidung (ms).
    uint32_t  accept_timeout_ms      = 10'000;

    // ── FEC / Demodulator (A.5.2.6.3) ─────────────────────────────────────
    /// Golay-Korrektur-Modus: Mode3_4 = maximale Korrektur (Standard).
    GolayMode golay_mode             = GolayMode::Mode3_4;
    /// Mindest-Unanimous-Votes für Wort-Akzeptanz (0..49; Standard 33).
    uint8_t   min_unanimous_votes    = 33;
    /// Automatische Anpassung von Golay-Modus und Votes an Signalqualität.
    bool      adaptive_fec           = true;

    // ── Timing (Operator-justierbar, keine Protokoll-Konstanten) ──────────
    /// Verweildauer pro Kanal beim Scannen (ms); Standard = 200 (5 chps).
    uint32_t  scan_dwell_ms          = 200;
    /// Sounding-Intervall (Sekunden); Standard = 300 s.
    uint32_t  sounding_interval_sec  = 300;
    /// Sounding conclusion type: false = TIS (invites return calls), true = TWAS (announce-only).
    bool      sounding_use_twas      = false;
    /// Pre-sounding countdown lead time (Sekunden). Wenn die automatische
    /// Sounding einer Net-Aktivierung von IDLE aus kurz bevorsteht (innerhalb
    /// dieses Vorlaufs), feuert on_sounding_warning(phase="warn"), damit die GUI
    /// einen Countdown-Popup mit Interrupt-Möglichkeit anzeigt. 0 = kein Popup.
    /// Wirkt NUR beim Übergang IDLE→SOUNDING; während SCANNING feuert die Sounding
    /// still (A.5.3.1 Sweep kehrt nach SCANNING zurück).
    uint32_t  sounding_warning_lead_sec = 10;
    /// Link-Idle-Timeout: Verbindung wird nach dieser Zeit getrennt (Sekunden).
    /// Default 360 s — programmierbarer Twa-Override gem. MIL-STD-188-141B Level-5
    /// (entspricht PCALE TWA=360000 ms). Der SM-Spec-Default bleibt 30 s
    /// (ale_timing.h Twa_ms, AC-LINK-003-001); dies ist nur der Stations-Default.
    uint32_t  link_idle_timeout_sec  = 360;
    /// Test-Channel sweep: time to stay LINKED on each channel before terminating
    /// and advancing. Tdrw (784 ms) is always enforced as a floor so bilateral
    /// LQA metrics can commit before snapshot. Default 1 s.
    uint32_t  test_channel_link_hold_time = 1;
    /// Maximale Tuning-Zeit Tt (ms); Standard = 1045 ms (Blindabstimmung).
    uint32_t  max_tune_time_ms       = 1045;

    // ── PTT-Timing ────────────────────────────────────────────────────────
    /// Zeit nach PTT-Assertion bevor Audio gesendet wird (ms).
    /// Kompensiert CAT/CI-V-Latenz beim Sounding (Calling hat bereits Tt als Lead).
    /// 0 = kein Delay.
    uint32_t  ptt_lead_ms             = 100;
    /// Zeit nach SM-RX-Freigabe bevor PTT deasserted wird (ms).
    /// Lässt den Audio-Buffer vollständig leerlaufen bevor auf RX umgeschaltet.
    /// 0 = sofortige PTT-Freigabe.
    uint32_t  ptt_tail_ms             = 350;

    // ── LQA-Austausch ─────────────────────────────────────────────────────
    /// true = CMD LQA / CMD NOISE / LQA Report aktiv senden und auswerten.
    /// false = kein aktiver LQA-Austausch (EMCON / Debug); FROM-Messungen
    ///         (Sounding-Empfang, Kanalqualität) laufen weiter.
    bool      lqa_exchange_enabled   = true;

    // ── Diagnose ──────────────────────────────────────────────────────────
    /// RX-Diagnose-Ausgabe via on_status_changed (Peak-Level + jedes Wort).
    bool      debug_rx               = false;

    // ── Auto-Relink (A.5.4.5 bilateral channel selection) ─────────────────
    /// true = nach LINKED auf besseren Kanal umschwenken: TWAS + Neulink.
    /// Erfordert lqa_enabled=true; nutzt bilaterale LQA-Scores aus CMD-Austausch.
    bool      relink_enabled             = false;
    /// Mindest-Score-Verbesserung (0–30 Skala) um Relink auszulösen.
    /// Hysterese gegen Thrashing: kleinere Werte → aggressiver, größere → stabiler.
    float     relink_improvement_threshold = 5.0f;

    // ── Enhanced Frequency-Select (CMD 'f' post-link bilateral negotiation) ───
    /// true = CMD 'f' Verhandlung vor TWAS (A.5.6.3.2, post-link adaptation).
    /// Schlägt der Gegenstation einen besseren Kanal vor; bei Standard-ALE-2G-
    /// Stationen läuft der Vorschlag in einen Timeout (kein Relink = Link bleibt).
    /// Nur sinnvoll wenn beide Stationen Enhanced-Mode aktiviert haben.
    bool      enhanced_freq_select = false;

    // ── Station position (propagation-aware LQA scoring) ──────────────────
    enum class PositionSource : uint8_t {
        NONE        = 0,  ///< no position → propagation scoring disabled
        MANUAL      = 1,  ///< user-entered lat/lon
        MAIDENHEAD  = 2,  ///< derived from grid locator
        GPSD        = 3,  ///< live from gpsd daemon
        NMEA_SERIAL = 4,  ///< live from NMEA serial port
    };

    PositionSource position_source  = PositionSource::NONE;
    double   station_lat_deg        = 0.0;        ///< positive = North
    double   station_lon_deg        = 0.0;        ///< positive = East
    std::string grid_locator        = "";         ///< Maidenhead, e.g. "IO91wm"
    std::string gpsd_host           = "127.0.0.1";
    uint16_t    gpsd_port           = 2947;
    std::string nmea_port           = "";         ///< "COM3" / "/dev/ttyUSB0"
    uint32_t    nmea_baud           = 4800;
    bool        sfi_enabled         = false;      ///< enable NOAA SFI background fetch
};

} // namespace ale
