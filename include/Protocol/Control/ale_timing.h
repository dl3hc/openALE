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

constexpr double Tlww_ms    = static_cast<double>(TRW_MS); // Last word wait delay = Trw = 392 ms
constexpr double Tld_ms     = TW_MS;                       // Late detect delay = Tw = 130.66... ms
constexpr double Tlrw_ms    = 2.0 * TRW_MS;               // Leading redundant words = 2×Trw = 784 ms
// Example values for C=10 channels, single one-word address (Annex B p.222):
// Tsc and Tcc are variable formulas (Table A-XV), not fixed constants.
//   Tsc(n) = n × Tcl  (Tcl per channel, ≥ Ts)
//   Tcc(n) = Tsc(n) + Tlc  (Tlc = 2 × wpa × Trw, grows with address length)
constexpr double Tsc_c10_1word_ms = 20.0 * TRW_MS;              // Tsc example: C=10, 1-word addr = 20×Trw
constexpr double Trc_min_ms       = 3.0  * TRW_MS;              // Trc min = 2×Trw + Trw = 3×Trw = 1176 ms
constexpr double Tcc_c10_1word_ms = Tsc_c10_1word_ms + 2.0 * TRW_MS; // Tcc example: C=10, 1-word = 22×Trw

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
constexpr double Tds_ms    = TD5_MS;                      // Detect signaling period ≤ Td(5) = 200 ms
constexpr double Tdrw_ms   = 2.0 * TRW_MS;               // Detect redundant word = 2×Trw = 784 ms
constexpr double Tdrrw_ms  = 3.0 * TRW_MS;               // Detect rotating redundant word = 3×Trw = 1176 ms

// ── Sounding (Annex B, p.225) ──

constexpr double Trs_min_ms = 2.0 * TRW_MS;              // Trs min = 2×Trw = 784 ms (single-word)
// Tss  = n × Ta(caller) ≥ Ts  — variable, no constant
// Tsrs = Tss + Trs ≥ Ts + Trs — variable, no constant

// ── Operational Timing (non-standard, conservative defaults) ──

constexpr uint32_t Twa_ms     = 30000;                   // Activity timeout = 30 s (Table A-XV)
// Twce = 2 × own_Ts (Table A-XV).  own_Ts = n_channels × Tdrw_ms.
// Not a compile-time constant — depends on the station's scan-list length.
constexpr uint32_t calc_twce_ms(uint32_t n_channels) {
    return 2u * n_channels * static_cast<uint32_t>(Tdrw_ms);
}
constexpr uint32_t Twt_ms     = 2000;                    // voice/general LBT = 2 s — unused in ALE-only mode
constexpr uint32_t Twt_ale_ms = 2 * TRW_MS;              // ALE-only LBT = Tdrw = 784 ms
constexpr uint32_t LINK_TIMEOUT_MS = 120000;             // Link maintenance = 120 s
// Annex B says 45 min, Table A-XV says 30 min — Table A-XV used:
constexpr uint32_t SOUNDING_INTERVAL_MS = 30 * 60 * 1000; // Tps = 30 min

// ── Frame-Duration Formulas (MIL-STD-188-141B Table A-XV, Annex B) ──
//
// n_channels             = C: number of channels in the scan list
// scan_words_per_channel = words in one scanning iteration (1 for 1-word addr,
//                          2 for 2-word individual or group THRU+REP pair)
// leading_words_half     = words in one half of the leading-call section
//                          (Tlc = 2 × half; e.g. 1 for TO:SAM, 2 for TO:SAM DATA:UEL)
// conclusion_words       = words in the TIS conclusion (1 for ≤3-char self-id)

// Tsc: scanning-call duration  (sum over all channels, one scan pass)
constexpr double calc_tsc_ms(uint32_t n_channels, uint32_t scan_words_per_channel) {
    return static_cast<double>(n_channels) * scan_words_per_channel * TRW_MS;
}

// Tlc: leading-call duration  (full address × 2)
constexpr double calc_tlc_ms(uint32_t leading_words_half) {
    return 2.0 * leading_words_half * TRW_MS;
}

// Tcc: complete call cycle  (Tsc + Tlc, excluding Tx)
constexpr double calc_tcc_ms(uint32_t n_channels, uint32_t scan_words_per_channel,
                              uint32_t leading_words_half) {
    return calc_tsc_ms(n_channels, scan_words_per_channel)
         + calc_tlc_ms(leading_words_half);
}

// Tx: conclusion (termination) duration
constexpr double calc_tx_ms(uint32_t conclusion_words) {
    return static_cast<double>(conclusion_words) * TRW_MS;
}

// ── Integer Variants (round-to-nearest, for arrays/bounds) ──

constexpr uint32_t Twr_fast_int  = static_cast<uint32_t>(0.5 + Twr_fast_ms);
constexpr uint32_t Twr_slow_int  = static_cast<uint32_t>(0.5 + Twr_slow_ms);
constexpr uint32_t Twrt_fast_int = static_cast<uint32_t>(0.5 + Twrt_fast_ms);
constexpr uint32_t Tsc_c10_1word_int = static_cast<uint32_t>(0.5 + Tsc_c10_1word_ms);

// ── Calling-Timeout Budget (MIL-STD-188-141B Annex B, Table A-XV) ──────────
//
// Encodes the protocol-level formula:
//   timeout = n_channels × (Twt + Tt + Tsc + Tlc + Twr/Twrt) + 2 s margin
//
// leading_frame_words: already-doubled word count (size of leading_frame_ vector),
//   so one multiply by Trw directly yields Tlc.
// multi_channel: true  → use Twrt (Twr + Tt, fast equip.)
//                false → use Twr alone (single channel, no retune)
struct CallingBudgetParams {
    uint32_t n_channels;           // calling_channels.size(), min 1
    uint32_t target_scan_channels; // channels in the scan list
    uint32_t leading_frame_words;  // already-doubled leading frame size
    bool     multi_channel;        // Twrt vs Twr
};

constexpr uint32_t calc_calling_timeout_ms(const CallingBudgetParams& p) {
    const uint32_t tsc    = p.target_scan_channels * 2u * TRW_MS;
    const uint32_t tlc    = p.leading_frame_words * TRW_MS;
    // Use Twr_slow_int: our SW decoder needs Trd_sw = Trw (full word), which is
    // equivalent to 7×Tw = Twr_slow in the Table A-XV formula.
    const uint32_t twr    = p.multi_channel ? (Twr_slow_int + static_cast<uint32_t>(Tt_fast_ms + 0.5))
                                             : Twr_slow_int;
    const uint32_t per_ch = Twt_ale_ms
                          + static_cast<uint32_t>(TT_BLIND_MS + 0.5)
                          + tsc + tlc + twr;
    return per_ch * p.n_channels + 2000u;
}

// ── Star Calling (Annex B, pp.226-227) ──

constexpr double TAL_MS          = static_cast<double>(TRW_MS); // Tal = Trw = 392 ms
constexpr double Tsw_std_ms      = 14.0 * TW_MS;          // Min slot standard = 14×Tw = 1829.33 ms
constexpr double Tsw_lqa_ms      = 17.0 * TW_MS;          // Min slot LQA = 17×Tw = 2221.33 ms
constexpr double Tsw_tight_ms    = 9.0  * TW_MS;          // Min slot tight = 9×Tw = 1176 ms
constexpr double Twan_max_std_ms = 188.0 * TW_MS;         // Twan max late arrival standard = 188×Tw
// Table A-XV: 227 Tw (mit LQA).  Annex B text says 277 Tw — Table A-XV governs.
constexpr double Twan_max_lqa_ms = 227.0 * TW_MS;         // Twan max late arrival LQA = 227×Tw
constexpr double Tta_tt_slot0_ms = 360.0;                  // Tta+Tt limit slot 0 = 360 ms
constexpr double Tta_tt_slot1_ms = 2100.0;                 // Tta+Tt limit slot 1 = 2100 ms
constexpr double Tta_tt_other_ms = 1500.0;                 // Tta+Tt limit other slots = 1500 ms

} // namespace ale

// ── ALETimingConstants — named aliases for the state machine and tests ────────
//
// Defined in the global namespace so ale-internal code can write
// ALETimingConstants::Trw_ms without ambiguity with ale::TRW_MS.
// Twr_ms uses Twr_fast_int (solid-state equipment, 5×Tw ≈ 653 ms).
namespace ALETimingConstants {
    constexpr uint32_t Trw_ms          = ale::TRW_MS;                                        // 392 ms
    constexpr double   Tw_ms           = ale::TW_MS;                                          // 130.666... ms
    constexpr uint32_t Twr_ms          = ale::Twr_fast_int;                                   // 653 ms (fast equip.)
    constexpr uint32_t Twrt_ms         = ale::Twrt_fast_int;                                  // 784 ms (Twr+Tt fast)
    constexpr uint32_t Twt_ms          = ale::Twt_ale_ms;                                     // 784 ms (ALE-only LBT)
    constexpr uint32_t Tt_ms           = static_cast<uint32_t>(ale::TT_BLIND_MS + 0.5);       // 1045 ms (blind tune)
    constexpr uint32_t Tlww_ms         = ale::TRW_MS;                                         // 392 ms (= Trw)
    constexpr uint32_t Twa_ms          = ale::Twa_ms;                                         // 30 000 ms
    constexpr uint32_t LINK_TIMEOUT_MS = ale::LINK_TIMEOUT_MS;                                // 120 000 ms

    // ── Derived protocol limits (integer, for SM comparisons) ────────────────
    constexpr uint32_t Tx_max_ms  = 5u  * Trw_ms;   // Max conclusion length  = 5×Trw = 1960 ms
    constexpr uint32_t Tm_max_ms  = 30u * Trw_ms;   // Max message section    = 30×Trw = 11760 ms
    // Trc_min: minimum receiving call time (Annex B: Tlww + LBT + Trd_sw = 3×Trw = 1176 ms).
    // Used as the LISTENING and WAIT_ACK base window for this SW-decoder implementation.
    constexpr uint32_t Trc_min_ms = 3u  * Trw_ms;   // 1176 ms

    // ── Protocol count constants (spec-defined, non-timing) ──────────────────
    // A.5.5.3.2: up to this many contiguous FEC-uncorrectable words tolerated
    // during receipt of a scanning call before the frame is rejected.
    constexpr uint32_t MAX_SCANNING_CALL_ERRORS = 3u;

    // Max words in one response or ACK TX sequence:
    //   2 × max_addr_words(5) + max_addr_words(5) = 15
    // Used to size / assert the modem word queue.
    constexpr uint32_t MAX_TX_SEQUENCE_WORDS = 15u;
}
