/**
 * \file ale_timing.h
 * \brief ALE timing constants per MIL-STD-188-141B Tabelle A-XV (DD-010)
 *
 * Alle Werte entsprechen den benannten Timern aus Tabelle A-XV.
 * Namen folgen der Spec verbatim; Kommentare geben die Bedeutung ohne Spec-Kenntnis.
 *
 * Grundstruktur:
 *   Tw   = Dauer einer einzelnen ALE-Wort-Übertragung (~130,66 ms)
 *   Trw  = 3 × Tw  (jedes Wort wird dreifach redundant gesendet)
 */

#pragma once
#include <cstdint>

namespace ale {
namespace ALETimingConstants {

    // ── Tabelle A-XV ─────────────────────────────────────────────────────────
    /// Tw — Dauer einer einzelnen Wort-Übertragung (~130,66 ms gerundet)
    constexpr uint32_t Tw_ms       = 130;

    /// Trw — Redundant Word Period: jedes Wort wird 3× gesendet = 3 × Tw
    constexpr uint32_t Trw_ms      = 392;

    /// Tlrw — Leading/Response Window: 2 Redundant Word Periods = 2 × Trw
    constexpr uint32_t Tlrw_ms     = 784;

    /// Ts_max — maximale Scan-Zyklusdauer
    constexpr uint32_t Ts_max_ms   = 50000;

    /// Tm_max — maximale Modem-Link-Setup-Zeit: 30 × Trw
    constexpr uint32_t Tm_max_ms   = 11760;

    /// Tx_max — maximales Sendefenster vor Pflicht-Empfangsfenster: 5 × Trw
    constexpr uint32_t Tx_max_ms   = 1960;

    /// Twr — Antwort-Empfangsfenster, konservativ (kein Hardware-Delay modelliert)
    ///   Tlww (= Trw) + Tld (≈ Tw) + Trwp_max (= Trw) = 915 ms
    constexpr uint32_t Twr_ms      = 915;

    /// Twrt — Antwort- + Sende-Fenster: Twr + Tt (Standard Tt = 8 × Tw)
    constexpr uint32_t Twrt_ms     = 1960;

    /// Twa — Activity Timeout: Station bricht Call-Versuch nach Stille ab
    constexpr uint32_t Twa_ms      = 30000;

    /// Tlww — Last Word Wait Delay: Station hält RX ein Extra-Wort offen = Trw
    constexpr uint32_t Tlww_ms     = 392;

    /// Twce — Channel-Exclusion-Fenster nach eigener Sendung: 2 × Ts (Standard)
    constexpr uint32_t Twce_ms     = 1960;

    /// Twt — Listen-Before-Transmit Guard (Standard-Kanäle)
    constexpr uint32_t Twt_ms      = 2000;

    /// Twt_ale — Listen-Before-Transmit Guard (ALE-only-Kanäle) = Tlrw
    constexpr uint32_t Twt_ale_ms  = 784;

    /// Tc_max — maximale Call-Gesamtdauer: 12 × Trw
    constexpr uint32_t Tc_max_ms   = 4704;

    // ── Implementierungskonstanten (nicht in Tabelle A-XV) ────────────────────

    /// Link-Maintenance-Timeout — nicht standardisiert, konservativ 120 s
    constexpr uint32_t LINK_TIMEOUT_MS      = 120000;

    /// Sounding-Intervall — implementierungsdefiniert
    constexpr uint32_t SOUNDING_INTERVAL_MS = 60000;

} // namespace ALETimingConstants
} // namespace ale
