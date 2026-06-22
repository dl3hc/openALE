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
#include "FEC/golay.h"

namespace ale {

struct ALEStationConfig {

    // ── Scanning-Call-Planung ──────────────────────────────────────────────
    /// Angenommene Kanal-Anzahl der Gegenstelle für Tsc = C × 2 × Trw.
    /// 0 = automatisch aus eigener Kanalliste (calling_channels_.size()).
    /// >0 = konservatives Maximum (z.B. wenn Gegenstelle mehr Kanäle hat).
    uint32_t  assumed_scan_channels  = 0;

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
    /// Link-Idle-Timeout: Verbindung wird nach dieser Zeit getrennt (Sekunden).
    uint32_t  link_idle_timeout_sec  = 30;
    /// Maximale Tuning-Zeit Tt (ms); Standard = 1045 ms (Blindabstimmung).
    uint32_t  max_tune_time_ms       = 1045;

    // ── LQA-Austausch ─────────────────────────────────────────────────────
    /// true = CMD LQA / CMD NOISE / LQA Report aktiv senden und auswerten.
    /// false = kein aktiver LQA-Austausch (EMCON / Debug); FROM-Messungen
    ///         (Sounding-Empfang, Kanalqualität) laufen weiter.
    bool      lqa_exchange_enabled   = true;

    // ── Diagnose ──────────────────────────────────────────────────────────
    /// RX-Diagnose-Ausgabe via on_status_changed (Peak-Level + jedes Wort).
    bool      debug_rx               = false;
};

} // namespace ale
