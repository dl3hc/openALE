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
    /// AMD delivery-confirmation retry budget: how many times an AMD send is
    /// attempted before giving up and notifying "not heard". Applies to both the
    /// not-linked path (each attempt = one full call sequence, whose Response is
    /// the delivery indicator) and the linked path (each attempt = one AMD burst,
    /// confirmed by the peer's Response frame → sender ACK). Default 3.
    uint32_t  amd_send_max_attempts  = 3;
    /// Test-Channel sweep: time to stay LINKED on each channel before terminating
    /// and advancing. Tdrw (784 ms) is always enforced as a floor so bilateral
    /// LQA metrics can commit before snapshot. Default 1 s.
    uint32_t  test_channel_link_hold_time = 1;
    /// Maximale Tuning-Zeit Tt (ms); Standard = 1045 ms (Blindabstimmung).
    uint32_t  max_tune_time_ms       = 1045;

    // ── PTT-Timing ────────────────────────────────────────────────────────
    /// Zeit nach PTT-Assertion bevor Audio gesendet wird (ms).
    /// Kompensiert CAT/CI-V-Latenz beim Sounding (Calling hat bereits Tt als Lead).
    /// 0 = kein Delay. Standard 10 ms — deckt die CAT-PTT-Latenz gaengiger SDR-
    /// Transceiver ab; ohne diese Marge wird das erste Wort (der volle eigene
    /// Rufname) teilweise abgeschnitten, bevor das Funkgeraet tatsaechlich
    /// sendet, was die Gegenstation verwirrt. Werte > ~10ms verletzen die
    /// knappen Trw-Zeitfenster des Handshakes und koennen Verbindungen zu
    /// anderen Stationen vollstaendig verhindern; nur bei nachweislich
    /// langsamerer CAT/PTT-Hardware erhoehen.
    uint32_t  ptt_lead_ms             = 10;
    /// Zeit nach SM-RX-Freigabe bevor PTT deasserted wird (ms).
    /// Laesst den Audio-Buffer vollstaendig leerlaufen bevor auf RX umgeschaltet.
    /// 0 = sofortige PTT-Freigabe. Standard 200 ms — deckt die WASAPI/CAT-
    /// Ausgangslatenz gaengiger SDR-Hardware ab; siehe ptt_lead_ms fuer die
    /// gleiche Trw-Zeitfenster-Warnung bei weiterer Erhoehung.
    uint32_t  ptt_tail_ms             = 200;

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

    // ── ALE-GPR automatic position reporting (docs/ALE_GPR_SPEC.md) ───────
    enum class PositionReportMode : uint8_t {
        NONE     = 0,  ///< automatic reporting off — manual "Send Position" still works
        ON_CHANGE= 1,  ///< report when the position moves >= position_report_change_m
        INTERVAL = 2,  ///< report every position_report_interval_min minutes
    };
    enum class PositionReportFormat : uint8_t {
        GPR = 0,  ///< HFLINK ALE-GPR encoded report (default, works from any position source)
        GGA = 1,  ///< raw $GPGGA passthrough — only available for a live NMEA-serial fix
    };

    PositionReportMode   position_report_mode         = PositionReportMode::NONE;
    std::string          position_report_target       = "";  ///< address, or "ALLCALL"
    std::string          position_report_net          = "";  ///< net for channel selection (ALLCALL only)
    uint32_t              position_report_change_m     = 1000; ///< on-change threshold, meters
    uint32_t              position_report_interval_min = 15;
    PositionReportFormat position_report_format       = PositionReportFormat::GPR;
    std::string           position_report_comment      = "";  ///< standing comment, <=25 chars,
                                                                 ///< used verbatim by automatic sends

    // ── Location Relay (forward received ALE-GPR positions to a web API) ──
    // docs/LOCATION_SHARING_CONCEPT.md §13. Off by default except allcall,
    // which is only effective once location_sharing_enabled (master) is set.
    bool        location_sharing_enabled         = false; ///< master opt-in
    std::string location_api_url                 = "";    ///< https://.../api/v1/locations
    std::string location_api_token                = "";    ///< Bearer token, never logged
    std::string location_ca_cert_path            = "";    ///< pinned server cert (PEM); empty = system trust store
    bool        location_sharing_allcall         = true;  ///< forward ALLCALL-received GPRs
    bool        location_sharing_individual      = false; ///< forward individual-call GPRs
    bool        location_sharing_net             = false; ///< forward net-call GPRs
    bool        location_sharing_group           = false; ///< forward group-call GPRs
    bool        location_sharing_linked          = false; ///< forward GPRs over an established link
    uint32_t    location_sharing_min_interval_sec = 30;    ///< throttle, per source
    uint8_t     location_sharing_round_digits    = 6;      ///< lat/lon rounding (privacy + dedup); 6 = full GPS precision
    bool        location_sharing_include_comment = false;  ///< forward the GPR comment field
    uint16_t    location_sharing_queue_size      = 64;      ///< bounded outbound queue

    // ── Link-intent defaults (TIS invites a link, TWAS = fire-and-forget) ──
    // Settings-level defaults per destination type; the GUI's per-send "Link"
    // checkbox is pre-filled from these but always overridable. All default
    // false (TWAS/no-link) — matches current hardcoded behavior exactly, so
    // this ships with zero behavior change until an operator opts in.
    bool link_default_individual = false;
    bool link_default_group      = false;
    bool link_default_allcall    = false;

    // ── Rig connection (Hamlib CAT) ────────────────────────────────────────
    // Mirrors the fields the GUI's rigArgs() sends to RIG_CONNECT (see
    // build_radio_spec() in apps/ale_bridge.cpp). Saved on every successful
    // RIG_CONNECT so the operator's rig selection and port settings survive
    // a restart instead of reverting to "None / Offline" every time.
    std::string rig_model         = "";     ///< Hamlib model ID as string; "" = None/Offline
    std::string rig_host          = "127.0.0.1"; ///< network backends (NET rigctl, FLRig, ...)
    std::string rig_port          = "4532";      ///< network backends' TCP port (string, not rigctld_server_port)
    std::string rig_serial        = "";     ///< serial device, e.g. "COM3" / "/dev/ttyUSB0"
    uint32_t    rig_baud          = 0;      ///< 0 = rig-caps default
    std::string rig_dtr           = "on";   ///< "on"/"off"/"unset"
    std::string rig_rts           = "on";   ///< "on"/"off"/"unset"
    uint32_t    rig_stab          = 200;    ///< post-open line-state settle time (ms)
    std::string rig_ptt           = "normal"; ///< "normal"/"mic"/"data" CAT PTT audio-input select
    /// true = re-attach rig_model with the saved settings automatically on
    /// bridge startup (mirrors rigctld_server_enabled's own-config-driven
    /// auto-start). Set whenever RIG_CONNECT succeeds; cleared on an explicit
    /// RIG_DISCONNECT so a deliberate disconnect does not silently reconnect
    /// on the next launch.
    bool        rig_auto_connect  = false;

    // ── Audio device ────────────────────────────────────────────────────────
    // Mirrors the fields the GUI's AUDIO_OPEN sends (see apps/ale_bridge.cpp).
    // Saved on every successful AUDIO_OPEN so the operator's device selection
    // survives a restart instead of reverting to "no device" every time.
    std::string audio_in           = "";    ///< RX (capture) device, bare name (no "IN: " prefix)
    std::string audio_out          = "";    ///< TX (render) device, bare name (no "OUT: " prefix)
    /// true = re-open audio_in/audio_out automatically on bridge startup
    /// (mirrors rig_auto_connect). Set whenever AUDIO_OPEN succeeds; cleared
    /// on an explicit AUDIO_CLOSE so a deliberate close does not silently
    /// reopen on the next launch.
    bool        audio_auto_open    = false;

    // ── rigctld/netrigctl-compatible read-only TCP server (Tuner) ─────────
    /// true = accept local netrigctl clients (Hamlib RIG_MODEL_NETRIGCTL)
    /// that poll the current RX frequency read-only (CMD 'f'). Does NOT open
    /// a second CAT/serial connection to the radio — serves openALE's own
    /// cached frequency. Disabled by default (opt-in), same tier as cat_trace.
    bool        rigctld_server_enabled     = false;
    /// TCP port for the read-only rigctld-compat listener.
    /// Distinct from rig_port (the OUTBOUND port openALE connects to as a
    /// rigctld/hamlib CLIENT) — do not confuse the two.
    uint16_t    rigctld_server_port        = 4532;
    /// false (default) = bind 127.0.0.1 only; true = bind 0.0.0.0 (LAN-reachable).
    bool        rigctld_server_bind_remote = false;
};

} // namespace ale
