/**
 * \file Protocol/Control/ale_timing.h
 * \brief ALE protocol timing constants per MIL-STD-188-141B Annex A & B
 *
 * Base constants:
 *   - Trw = 392 ms (exact, integer: 49 × Ttone = 49 × 8)
 *   - Tw  = Trw/3 = 130.666... ms (non-integer, stored as double)
 *
 * All derived timings use formulas from Annex B.
 */

#pragma once
#include <cstdint>

namespace ale {

// ── Base Timing Constants (Annex B: Basic system timing) ──

constexpr uint32_t TRW_MS            = 392;                // Trw = 3×Tw = 49×Ttone (exact)
constexpr double   TW_MS             = TRW_MS / 3.0;       // Tw = Trw/3 = 130.666... ms
constexpr double   TTONE_MS          = 8.0;                // Ttone = 8 ms (125 baud)
constexpr double   T_SYMBOLS_PER_WORD = 49.0 / 3.0;        // 16.333... symbols/word

static_assert(TRW_MS == 392, "Trw must be exactly 392 ms");

// ── System Timing Limits (Annex B: System timing limits) ──

constexpr double Ta_max_ms  = 5.0  * TRW_MS;              // Max address time = 5×Trw = 1960 ms
constexpr double Tc_max_ms  = 12.0 * TRW_MS;              // Max call time = 12×Trw = 4704 ms
constexpr double Tlc_max_ms = 2.0  * Tc_max_ms;           // Max leading call = 2×Tc_max = 24×Trw
constexpr double Tcl_max_ms = 5.0  * TRW_MS;              // Max group first-words = 5×Trw = 1960 ms
constexpr double Tx_max_ms  = 5.0  * TRW_MS;              // Max termination = Ta_max = 1960 ms
constexpr double Tm_max_ms  = 30.0 * TRW_MS;              // Max message section = 30×Trw = 11760 ms

// ── Individual Calling (Annex B) ──

constexpr double T1ww_ms    = static_cast<double>(TRW_MS); // Last word wait = Trw = 392 ms
constexpr double T1d_ms     = TW_MS;                       // Late detect word = Tw = 130.66... ms
constexpr double Tlrw_ms    = 2.0 * TRW_MS;               // Leading redundant words = 2×Trw = 784 ms
constexpr double Tsc_min_ms = 20.0 * TRW_MS;              // Scanning call min (C=10, Td=Tdrw example)

// Minimum for single one-word address (Annex B p.222):
constexpr double Trc_min_ms = 3.0 * TRW_MS;               // Trc min = 2×Trw + Trw = 3×Trw = 1176 ms
constexpr double Tcc_min_ms = Tsc_min_ms + 2.0 * TRW_MS;  // Tcc min = 22×Trw = 8624 ms (C=10, 1-word)

// Fast equipment (solid-state tuner, Annex B):
constexpr double Twr_fast_ms  = 5.0 * TW_MS;              // Wait for reply = 5×Tw = 653.33 ms
constexpr double Tt_fast_ms   = 1.0 * TW_MS;              // Tune time (solid-state) ≥ Tw = 130.66 ms
constexpr double Twrt_fast_ms = Twr_fast_ms + Tt_fast_ms;  // Wait+tune = 6×Tw = 784 ms
constexpr double Tta_fast_ms  = 0.0;                       // Turnaround (fast) = 0

// Slow equipment (relay tuner, Annex B):
constexpr double Twr_slow_ms  = 7.0 * TW_MS;              // Wait for reply = 7×Tw = 914.66 ms
constexpr double Tt_slow_ms   = 8.0 * TW_MS;              // Tune time (relay) ≥ 8×Tw = 1045.33 ms
constexpr double Twrt_slow_ms = Twr_slow_ms + Tt_slow_ms;  // Wait+tune = 15×Tw = 1960 ms
constexpr double Tta_slow_ms  = 2.0 * TW_MS;              // Turnaround (slow) = 2×Tw = 261.33 ms

// Tune timing (Annex B, Table A-XV):
constexpr double   TT_BLIND_MS    = 8.0 * TW_MS;          // Tt blind first call = 8×Tw = 1045.33 ms
constexpr uint32_t TT_NEXT_TRY_MS = 20000;                // Tt next try = 20 s (blind calls)

// ── Scanning (Annex B) ──

constexpr double Ts_max_ms = 50000.0;                     // Max scan period = 50 s
constexpr double Td_min_ms = 100.0;                       // Min dwell = 100 ms (10 chps, DO)
constexpr double TD5_MS    = 200.0;                       // Td(5) = 200 ms (5 chps basic)
constexpr double TD2_MS    = 500.0;                       // Td(2) = 500 ms (2 chps min)
constexpr double Tdrw_ms   = 2.0 * TRW_MS;               // Detect redundant word = 2×Trw = 784 ms
constexpr double Tdrrw_ms  = 3.0 * TRW_MS;               // Detect rotating redundant word = 3×Trw = 1176 ms

// ── Sounding (Annex B, p.225) ──

constexpr double Trs_min_ms = 2.0 * TRW_MS;              // Trs min = 2×Trw = 784 ms (single-word)
// Tss  = n × Ta(caller) ≥ Ts  — variable, no constant
// Tsrs = Tss + Trs ≥ Ts + Trs — variable, no constant

// ── Operational Timing (non-standard, conservative defaults) ──

constexpr uint32_t Twa_ms     = 30000;                   // Activity timeout = 30 s (Table A-XV)
constexpr uint32_t Twce_ms    = 5 * TRW_MS;              // Channel exclusion ≈ 5×Trw (conservative)
constexpr uint32_t Twt_ms     = 2000;                    // Listen-before-TX = 2 s (voice/general)
constexpr uint32_t Twt_ale_ms = 2 * TRW_MS;              // ALE-only LBT = Tdrw = 784 ms
constexpr uint32_t LINK_TIMEOUT_MS = 120000;             // Link maintenance = 120 s
// Annex B says 45 min, Table A-XV says 30 min — Table A-XV used:
constexpr uint32_t SOUNDING_INTERVAL_MS = 30 * 60 * 1000; // Tps = 30 min

// ── Integer Variants (round-to-nearest, for arrays/bounds) ──

constexpr uint32_t Twr_fast_int  = static_cast<uint32_t>(0.5 + Twr_fast_ms);
constexpr uint32_t Twr_slow_int  = static_cast<uint32_t>(0.5 + Twr_slow_ms);
constexpr uint32_t Twrt_fast_int = static_cast<uint32_t>(0.5 + Twrt_fast_ms);
constexpr uint32_t Tsc_min_int   = static_cast<uint32_t>(0.5 + Tsc_min_ms);

// ── Star Calling (Annex B, pp.226-227) ──

constexpr double TAL_MS          = static_cast<double>(TRW_MS); // Tal = Trw = 392 ms
constexpr double Tsw_std_ms      = 14.0 * TW_MS;          // Min slot standard = 14×Tw = 1829.33 ms
constexpr double Tsw_lqa_ms      = 17.0 * TW_MS;          // Min slot LQA = 17×Tw = 2221.33 ms
constexpr double Tsw_tight_ms    = 9.0  * TW_MS;          // Min slot tight = 9×Tw = 1176 ms
constexpr double Twan_max_std_ms = 188.0 * TW_MS;         // Twan max late arrival standard = 188×Tw
// Annex B says 277 Tw, Table A-XV says 227 Tw — Annex B (277) used as authoritative:
constexpr double Twan_max_lqa_ms = 277.0 * TW_MS;         // Twan max late arrival LQA = 277×Tw
constexpr double Tta_tt_slot0_ms = 360.0;                  // Tta+Tt limit slot 0 = 360 ms
constexpr double Tta_tt_slot1_ms = 2100.0;                 // Tta+Tt limit slot 1 = 2100 ms
constexpr double Tta_tt_other_ms = 1500.0;                 // Tta+Tt limit other slots = 1500 ms

} // namespace ale
