/**
 * \file Protocol/Control/ale_timing.h
 * \brief ALE protocol timing constants — MIL-STD-188-141D Appendix A, Annex B
 *
 * Derivation hierarchy (each level builds exclusively on the level above):
 *
 *  Level 0  Waveform fundamentals  (spec-fixed, cannot be derived further)
 *    TTONE_MS = 8 ms                    symbol period at 125 baud
 *
 *  Level 1  On-air word period  (Annex B §"Basic system timing")
 *    TW_MS = (49/3) × TTONE_MS = 130.666… ms
 *    (one ALE word = 49 bits ÷ 3 bits/symbol = 16.333… symbols on-air)
 *
 *  Level 2  Redundant word  ← the spec's smallest addressable unit
 *    TRW_MS = 3 × TW = 49 × TTONE = 392 ms   (exact integer — Trw)
 *
 *  Level 3  Protocol timing limits  (multiples of Trw, Annex B §"System timing limits")
 *    Tal     = Trw      =  392 ms   address first-word time
 *    Tlww    = Trw      =  392 ms   T₁ww — last-word-wait delay
 *    Ta_max  = 5×Trw    = 1960 ms   max individual/net address (5 words)
 *    Tc_max  = 12×Trw   = 4704 ms   max call time (12 one-word addresses)
 *    Tx_max  = Ta_max              max termination section (= Ta_max)
 *    Tm_max  = 30×Trw   = 11760 ms  max message section — basic (without AMD/DTM/DBM)
 *    Tdrw    = 2×Trw    =  784 ms   detect-redundant-word window (= 6×Tw)
 *    Tdrrw   = 3×Trw    = 1176 ms   detect-rotating-redundant-word window (= 9×Tw)
 *
 *  Level 4  Equipment-class timing  (multiples of Tw = Trw/3, §"Individual calling")
 *    T1d      = Tw      ≈  130.67 ms  T₁d — late-detect additional delay
 *    Tta_fast = 0                    turnaround, fast (solid-state) equipment
 *    Tta_slow = 2×Tw    ≈  261.33 ms turnaround, slow equipment
 *    Tt_fast  = Tw      ≈  130.67 ms tune time, solid-state tuner
 *    Tt_slow  = 8×Tw    = 1045 ms   tune time, relay tuner
 *    Twr_fast = 5×Tw    =  653 ms   Twr — wait-for-reply, fast equipment
 *    Twr_slow = 7×Tw    =  915 ms   Twr — wait-for-reply, slow equipment
 *    Twrt_fast = Twr_fast + Tt_fast = 6×Tw  =  784 ms
 *    Twrt_slow = Twr_slow + Tt_slow = 15×Tw = 1960 ms
 *
 *  Level 5  Programmable defaults  (network manager can override, §"Programmable timing")
 *    TD5_MS    = 200 ms   Td(5) — dwell, 5 chps basic scan rate
 *    TD2_MS    = 500 ms   Td(2) — dwell, 2 chps minimum scan rate
 *    Tds_ms    ≤ TD5_MS   detect-signaling period (≤ Td(5))
 *    Twt_ale   = Tdrw = 784 ms    LBT for ALE-only channels (spec: Twt = Tdrw)
 *    Twt_voice = 2000 ms           LBT for voice/general-purpose channels
 *    Tt_blind  = 8×Tw = 1045 ms   default blind tune, first call (unknown equipment)
 *    Tps       = 45 min            periodic sounding interval (Annex B §"Programmable")
 *    Twa       = 30 s              wait-for-activity after link/use
 *
 * Formula functions for protocol-variable quantities (Tsc, Tlc, Tcc, Twce, …)
 * are at the bottom of the ale:: namespace; they encode the Annex B formulas.
 *
 * State-machine code uses the ALETimingConstants:: namespace (integer aliases).
 */

#pragma once
#include <cstdint>

namespace ale {

// ────────────────────────────────────────────────────────────────────────────
// Level 0 — Waveform fundamentals
// ────────────────────────────────────────────────────────────────────────────

constexpr double TTONE_MS           = 8.0;         // Ttone: symbol period, 125 baud
constexpr double T_SYMBOLS_PER_WORD = 49.0 / 3.0;  // 16.333… symbols per on-air word

// ────────────────────────────────────────────────────────────────────────────
// Level 1 — On-air word period
// ────────────────────────────────────────────────────────────────────────────

// Tw = (49/3) × Ttone = 130.666… ms
constexpr double TW_MS = T_SYMBOLS_PER_WORD * TTONE_MS;

// ────────────────────────────────────────────────────────────────────────────
// Level 2 — Redundant word (the spec's basic addressable unit)
// ────────────────────────────────────────────────────────────────────────────

// Trw = 3×Tw = 49×Ttone = 392 ms  (exact integer by waveform design)
constexpr uint32_t TRW_MS = 392u;
static_assert(TRW_MS == 392u, "Trw must be exactly 392 ms");

// ────────────────────────────────────────────────────────────────────────────
// Level 3 — Protocol timing limits  (Annex B §"System timing limits")
// All values are integer multiples of Trw = 392 ms.
// ────────────────────────────────────────────────────────────────────────────

// Address-related limits
constexpr uint32_t TAL_MS     = TRW_MS;         // Tal = Trw     =  392 ms   first-word address time
constexpr double   Ta_max_ms  = 5.0  * TRW_MS;  // Ta max = 5×Trw = 1960 ms  max address (5 words)
constexpr double   Tcl_max_ms = 5.0  * TRW_MS;  // Tcl max = 5×Trw = 1960 ms max group first-words
constexpr double   Tc_max_ms  = 12.0 * TRW_MS;  // Tc max = 12×Trw = 4704 ms max call (12 words)
constexpr double   Tx_max_ms  = Ta_max_ms;       // Tx max = Ta max = 1960 ms  max termination section

// Message section limit (basic — extended by AMD/DTM/DBM per Annex B note)
constexpr double   Tm_max_ms  = 30.0 * TRW_MS;  // Tm max = 30×Trw = 11760 ms

// Last-word-wait delay  (spec: T₁ww — subscript "1ww")
constexpr uint32_t Tlww_ms    = TRW_MS;          // T1ww = Trw = 392 ms

// Detection windows (used during ALE scanning)
// Tdrw = Trw + spare Trw = 6×Tw = 2×Trw = 784 ms
constexpr double   Tdrw_ms    = 2.0 * TRW_MS;   // detect-redundant-word window
// Tdrrw = 2×Trw + spare Trw = 9×Tw = 3×Trw = 1176 ms
constexpr double   Tdrrw_ms   = 3.0 * TRW_MS;   // detect-rotating-redundant-word window

// Detect-signaling period (≤ Td(5) = 200 ms — evaluated at start of dwell)
constexpr double   TD5_MS     = 200.0;           // Td(5) = 200 ms  (5 chps basic scan)
constexpr double   TD2_MS     = 500.0;           // Td(2) = 500 ms  (2 chps minimum scan)
constexpr double   Td_min_ms  = 100.0;           // Td(10) = 100 ms (10 chps, DO variant)
constexpr double   Tds_ms     = TD5_MS;          // Tds ≤ Td(5) = 200 ms
constexpr double   Ts_max_ms  = 50000.0;         // Ts max = 50 s  (max scan period)

// Scanning call examples  (C=10 channels, single 1-word address, Td=Tdrw)
constexpr double Tsc_c10_1word_ms = 20.0 * TRW_MS;              // Tsc = 20×Trw = 7840 ms
constexpr double Trc_min_ms       = 3.0  * TRW_MS;              // Trc min = 3×Trw = 1176 ms
constexpr double Tcc_c10_1word_ms = Tsc_c10_1word_ms + 2.0 * TRW_MS; // Tcc = 22×Trw = 8624 ms

// ────────────────────────────────────────────────────────────────────────────
// Level 4 — Equipment-class timing  (Annex B §"Individual calling")
// Expressed in multiples of Tw = Trw/3 = 130.666… ms.
// ────────────────────────────────────────────────────────────────────────────

// Late-detect additional delay  (spec: T₁d — subscript "1d")
// T1d = Tw = 130.666… ms
constexpr double T1d_ms = TW_MS;

// Turnaround time  (Tta = Trd + Tdek + Tenk + Ttc + Ttk + Ttd — see Annex B)
constexpr double Tta_fast_ms = 0.0;              // fast equipment: ≈ 0
constexpr double Tta_slow_ms = 2.0 * TW_MS;     // slow equipment: 2×Tw ≈ 261 ms

// Tune time
constexpr double Tt_fast_ms  = 1.0 * TW_MS;     // solid-state tuner: ≥ Tw ≈ 130.67 ms
constexpr double Tt_slow_ms  = 8.0 * TW_MS;     // relay tuner:      ≥ 8×Tw ≈ 1045 ms

// Wait-for-reply  (Twr = Ttd + Tp + T1ww + T1d + Tta + Trwp + Tp + Trd)
constexpr double Twr_fast_ms = 5.0 * TW_MS;     // fast equipment: 5×Tw ≈ 653 ms
constexpr double Twr_slow_ms = 7.0 * TW_MS;     // slow equipment: 7×Tw ≈ 915 ms

// Wait-for-response-and-tune  (Twrt = Twr + Tt)
constexpr double Twrt_fast_ms = Twr_fast_ms + Tt_fast_ms;  // 6×Tw ≈  784 ms
constexpr double Twrt_slow_ms = Twr_slow_ms + Tt_slow_ms;  // 15×Tw = 1960 ms

// ────────────────────────────────────────────────────────────────────────────
// Level 5 — Programmable defaults  (Annex B §"Programmable timing parameters")
// Network manager can override all values in this section.
// ────────────────────────────────────────────────────────────────────────────

// Listen-Before-Transmit
// Spec: Twt = Tdrw = 784 ms for ALE and data-only channels
constexpr uint32_t Twt_ale_ms   = 2u * TRW_MS;  // = Tdrw = 784 ms  (ALE-only channels)
constexpr uint32_t Twt_voice_ms = 2000u;         // 2 s  (voice / general-purpose channels)

// Default blind tune allowance (unknown equipment)
constexpr double   TT_BLIND_MS    = 8.0 * TW_MS; // first call:  8×Tw ≈ 1045 ms
constexpr uint32_t TT_NEXT_TRY_MS = 20000u;       // second call: 20 s

// Periodic sounding interval  (Annex B §"Programmable timing")
// Spec: Tps = 45 minutes when enabled.
constexpr uint32_t Tps_ms             = 45u * 60u * 1000u;  // 45 min = 2 700 000 ms
constexpr uint32_t SOUNDING_INTERVAL_MS = Tps_ms;           // legacy alias

// Wait-for-activity after link/use
constexpr uint32_t Twa_ms = 30000u;  // Twa = 30 s

// Link maintenance timeout  (implementation-defined — not a spec symbol)
constexpr uint32_t LINK_TIMEOUT_MS = 120000u;  // 120 s

// Idle-warning lead time: the SM fires on_idle_warning() this far before the
// configured Twa elapses, so the GUI can offer a "reset timer" popup.  Kept
// short relative to any realistic Twa (default GUI setting 360 s).
constexpr uint32_t IDLE_WARNING_LEAD_MS = 30000u;  // 30 s

// LINKED TX-drain safety-net timeout (implementation-defined — not a spec
// symbol).  terminate_link() and trigger_linked_orderwire() rely on
// on_word_complete() draining the TX burst; if that never happens (audio stall
// or a transmit_callback that doesn't arm the frame completion), the SM would
// otherwise hang in LINKED with RX disabled.  handle_linked() force-completes
// the termination (or abandons the orderwire burst) once this elapses, bounding
// the hang instead of waiting forever.  Generous vs. a short burst (max
// termination section Tx_max = 5×Trw ≈ 2 s), but AMD/EFS orderwire bursts and
// an AMD riding the ACK frame are NOT bounded to "a few words" — A.5.7.2.3's
// Tm_max incl. AMD is 59×Trw ≈ 23.1 s.  The orderwire and SENDING_ACK arm
// sites (ale_state_machine.cpp) scale ALEStateMachine::tx_drain_deadline_ms_
// to the actual queued word count instead of using this constant directly;
// this value remains the floor/default for bursts that can't carry AMD.
constexpr uint32_t TX_DRAIN_TIMEOUT_MS = 10000u;  // 10 s

// ────────────────────────────────────────────────────────────────────────────
// Star calling  (Annex B §"Star calling")
// ────────────────────────────────────────────────────────────────────────────

constexpr double Tsw_std_ms      = 14.0 * TW_MS;  // min slot, standard replies  = 14×Tw ≈ 1829 ms
constexpr double Tsw_lqa_ms      = 17.0 * TW_MS;  // min slot, LQA replies       = 17×Tw ≈ 2221 ms
constexpr double Tsw_tight_ms    = 9.0  * TW_MS;  // min slot, tight fixed        =  9×Tw = 1176 ms
constexpr double Twan_max_std_ms = 188.0 * TW_MS; // Twan max late arrival, std   = 188×Tw
constexpr double Twan_max_lqa_ms = 227.0 * TW_MS; // Twan max late arrival, LQA   = 227×Tw
constexpr double Tta_tt_slot0_ms = 360.0;          // Tta+Tt limit, slot 0         = 360 ms
constexpr double Tta_tt_slot1_ms = 2100.0;         // Tta+Tt limit, slot 1         = 2100 ms
constexpr double Tta_tt_other_ms = 1500.0;         // Tta+Tt limit, slots 2+       = 1500 ms

// ────────────────────────────────────────────────────────────────────────────
// Integer variants  (round-to-nearest; used internally by ALETimingConstants)
// ────────────────────────────────────────────────────────────────────────────

constexpr uint32_t Twr_fast_int  = static_cast<uint32_t>(0.5 + Twr_fast_ms);   //  653 ms
constexpr uint32_t Twr_slow_int  = static_cast<uint32_t>(0.5 + Twr_slow_ms);   //  915 ms
constexpr uint32_t Twrt_fast_int = static_cast<uint32_t>(0.5 + Twrt_fast_ms);  //  784 ms

// ────────────────────────────────────────────────────────────────────────────
// Formula functions for protocol-variable quantities
// ────────────────────────────────────────────────────────────────────────────

// Twce = 2 × own_Ts = 2 × C × Tdrw  (Table A-XV; depends on station's channel count)
constexpr uint32_t calc_twce_ms(uint32_t n_channels) {
    return 2u * n_channels * static_cast<uint32_t>(Tdrw_ms);
}

// Tsc = n_channels × scan_words_per_channel × Trw  (Annex B §"Individual calling")
constexpr double calc_tsc_ms(uint32_t n_channels, uint32_t scan_words_per_channel) {
    return static_cast<double>(n_channels) * scan_words_per_channel * TRW_MS;
}

// Tlc = 2 × leading_words_half × Trw  (leading_words_half = Tc in Trw units; Tlc = 2×Tc)
constexpr double calc_tlc_ms(uint32_t leading_words_half) {
    return 2.0 * leading_words_half * TRW_MS;
}

// Tcc = Tsc + Tlc  (complete call cycle, excluding termination section Tx)
constexpr double calc_tcc_ms(uint32_t n_channels, uint32_t scan_words_per_channel,
                              uint32_t leading_words_half) {
    return calc_tsc_ms(n_channels, scan_words_per_channel)
         + calc_tlc_ms(leading_words_half);
}

// Tx = conclusion_words × Trw  (termination section duration)
constexpr double calc_tx_ms(uint32_t conclusion_words) {
    return static_cast<double>(conclusion_words) * TRW_MS;
}

// ────────────────────────────────────────────────────────────────────────────
// Calling timeout budget  (Annex B, Table A-XV)
//
// Global safety net for the entire CALLING state.  It must be LOOSER than the
// sum of all per-phase windows on one channel, because the per-phase timeouts
// (LISTENING a/b, AC-LINK-019-6/8) handle channel hopping themselves:
//
//   per_ch = Twt + Tt                          pre-TX (LBT + blind tune)
//          + Tsc + Tlc + Tx                    full TX sequence on air
//          + (Twrt_slow + Tdrw)                LISTENING(a): no-response window
//          + 5×Trw                             LISTENING(b): conclusion collect
//          + Tlww                              LISTENING(c): settle
//
//   timeout = n_channels × per_ch + Tack + 2 s margin
//
// Tack = (Tlc + Tx) = SENDING_ACK frame (TO peer ×2 + TIS self), the 3rd
// handshake frame the caller transmits ONCE on the linking channel after
// LISTENING settles.  It is NOT part of per_ch (the per-channel *try* cost,
// which ends at LISTENING); it is added once, like the margin.  Omitting it
// made the n_channels=1 budget expire mid-ACK — single-channel calls received
// the response but the global timeout fired before SENDING_ACK → LINKED, so
// they dropped back to IDLE.  n_channels≥2 masked the gap via the ×n_channels
// factor (which over-counts the one-shot ACK across every channel).
//
// The LISTENING windows already include the SW-decoder detect time and the
// round-trip audio latency (see handle_calling LISTENING docs).
//
// leading_seq_words: already-doubled word count from leading_seq_.size() (→ Tlc).
// conclusion_words:  conclusion section size from conclusion_seq_.size() (→ Tx).
// ────────────────────────────────────────────────────────────────────────────

struct CallingBudgetParams {
    uint32_t n_channels;           // calling_channels.size(), min 1
    uint32_t target_scan_channels; // channels in the target's scan list
    uint32_t leading_seq_words;    // already-doubled leading seq size (→ Tlc)
    uint32_t conclusion_words;     // conclusion seq size (→ Tx)
    uint32_t message_words = 0;    // calling-frame MESSAGE section (FROM self-ID +
                                    // LQA CMD/report + AMD CMD/DATA/REP) — re-sent on
                                    // every channel retry alongside leading_seq_, so
                                    // it belongs in per_ch, not the one-shot tack term.
                                    // Default 0 preserves the exact prior budget for
                                    // any call with nothing queued in that section.
};

constexpr uint32_t calc_calling_timeout_ms(const CallingBudgetParams& p) {
    const uint32_t tsc    = p.target_scan_channels * 2u * TRW_MS;
    const uint32_t tlc    = p.leading_seq_words    * TRW_MS;
    const uint32_t tx     = p.conclusion_words     * TRW_MS;
    const uint32_t msg    = p.message_words        * TRW_MS;
    const uint32_t listen = static_cast<uint32_t>(0.5 + Twrt_slow_ms)   // 1960
                          + static_cast<uint32_t>(Tdrw_ms)              // + 784
                          + 5u * TRW_MS                                 // + 1960
                          + Tlww_ms;                                    // +  392
    const uint32_t per_ch = Twt_ale_ms
                          + static_cast<uint32_t>(TT_BLIND_MS + 0.5)
                          + tsc + tlc + msg + tx + listen;
    // SENDING_ACK frame (TO peer ×2 + TIS self) — transmitted once on the
    // linking channel after LISTENING settles.  Added once (not per channel):
    // without it the n_channels=1 budget fired mid-ACK and single-channel
    // links never reached LINKED.
    const uint32_t tack = tlc + tx;
    return per_ch * p.n_channels + tack + 2000u;
}

} // namespace ale

// ────────────────────────────────────────────────────────────────────────────
// ALETimingConstants — named integer aliases for state machine consumption
//
// All values are derived from ale:: constants above.
// State machine code uses these names; the ale:: namespace is the single
// source of truth for formulas and documentation.
// ────────────────────────────────────────────────────────────────────────────
namespace ALETimingConstants {

    // ── Fundamental ──────────────────────────────────────────────────────────
    constexpr uint32_t Trw_ms = ale::TRW_MS;     // 392 ms — redundant word (Trw)
    constexpr double   Tw_ms  = ale::TW_MS;       // 130.666… ms — one word (Tw)

    // ── Equipment defaults (fast SW-decoder profile) ──────────────────────
    constexpr uint32_t Twr_ms  = ale::Twr_fast_int;   //  653 ms  (5×Tw, fast equipment)
    constexpr uint32_t Twrt_ms = ale::Twrt_fast_int;  //  784 ms  (6×Tw = Twr+Tt fast)
    constexpr uint32_t Twt_ms  = ale::Twt_ale_ms;     //  784 ms  (= Tdrw, ALE-only LBT)
    // A.5.4.7.1: Twt = 2×Trw suffices only on channels known to carry ALE
    // exclusively; on shared channels the LBT pause shall be at least 2 s.
    constexpr uint32_t Twt_shared_ms = 2000;          // 2000 ms — shared-channel LBT
    constexpr uint32_t Tt_ms   = static_cast<uint32_t>(ale::TT_BLIND_MS + 0.5);  // 1045 ms
    constexpr uint32_t Tlww_ms = ale::Tlww_ms;        //  392 ms  (= Trw, T1ww)
    // Detect-following-(redundant)-word window (= Tdrw = 2×Trw).  Used as the
    // settle after a conclusion's last word: it must exceed one on-grid word
    // period (Trw) so a trailing DATA/REP address-extension word is collected
    // before the receiver leaves the collecting phase, independent of the
    // update()/word-arrival ordering at the boundary.
    constexpr uint32_t Tdrw_ms = 2u * ale::TRW_MS;    //  784 ms  (= 2×Trw, Tdrw)
    constexpr uint32_t Twa_ms  = ale::Twa_ms;         // 30 000 ms
    constexpr uint32_t Tps_ms  = ale::Tps_ms;         // 2 700 000 ms (45 min)
    constexpr uint32_t LINK_TIMEOUT_MS = ale::LINK_TIMEOUT_MS;  // 120 000 ms
    constexpr uint32_t IDLE_WARNING_LEAD_MS = ale::IDLE_WARNING_LEAD_MS;  // 30 000 ms
    constexpr uint32_t TX_DRAIN_TIMEOUT_MS = ale::TX_DRAIN_TIMEOUT_MS;  // 10 000 ms

    // ── Derived protocol limits (integer, for SM comparisons) ────────────
    constexpr uint32_t Tx_max_ms  = 5u  * Trw_ms;  // max termination  = 5×Trw  = 1960 ms
    constexpr uint32_t Tm_max_ms  = 30u * Trw_ms;  // max message      = 30×Trw = 11760 ms
    // Trc_min: minimum receiving-call window (Annex B: 2×Tc + Tx = 3×Trw for 1-word addr).
    // Also used as the LISTENING / WAIT_ACK base window for this SW-decoder implementation.
    constexpr uint32_t Trc_min_ms = 3u  * Trw_ms;  // 1176 ms

    // ── Scanning sounding timing (A.5.3.3 / AC-SOUND-002-001) ───────────
    // Ts_max: maximum scan period across all receivers = 50 000 ms.
    // Tss >= Ts_max ensures every scanning receiver catches at least one
    // complete self-address word on each sounded channel.
    constexpr uint32_t Ts_max_ms = static_cast<uint32_t>(ale::Ts_max_ms);  // 50 000 ms

    // Minimum self-address words for the Tss scanning phase.
    // Returns the smallest multiple of addr_word_count such that
    //   result × Trw_ms >= Ts_max_ms.
    // Example (1-word addr "SAM"): ceil(50000/392) = 128 words → 50 176 ms.
    constexpr uint32_t tss_word_count(uint32_t addr_word_count) {
        if (addr_word_count == 0u) return 0u;
        const uint32_t block_ms    = addr_word_count * Trw_ms;
        const uint32_t repetitions = (Ts_max_ms + block_ms - 1u) / block_ms;
        return repetitions * addr_word_count;
    }

    // ── Sounding redundancy time (A.5.3.1/A.5.3.2 / AC-SOUND-003-002) ──
    // Trs = 2 × Ta(caller) = 2 × addr_word_count × Trw_ms.
    // For a 1-word address: Trs_min = 2 × Trw = 784 ms.
    constexpr uint32_t Trs_min_ms = 2u * Trw_ms;  // 784 ms

    // Minimum conclusion words for the sounding Trs phase.
    // Returns 2 × addr_word_count so that the on-air duration equals
    // 2 × addr_word_count × Trw_ms = 2 × Ta(caller) ≥ Trs_min.
    constexpr uint32_t trs_word_count(uint32_t addr_word_count) {
        return 2u * addr_word_count;
    }

    // ── Protocol count constants (spec-defined, non-timing) ──────────────
    // Ta max = 5 × Trw = 1960 ms (Table A-XII / A.5.2.4.2): maximum words in
    // one individual/net address section.  AddressEncoder::chunk() enforces this
    // at encoding time (max 15 chars = 5 × 3-char words); FrameValidator checks
    // received sequences via address_section_word_count_valid().
    constexpr uint32_t TA_MAX_WORDS             = 5u;
    // A.5.5.3.2: max contiguous FEC-uncorrectable words before frame rejection.
    constexpr uint32_t MAX_SCANNING_CALL_ERRORS = 3u;
    // Max words in one contiguous TX sequence handed to the modem.
    // The full calling sequence is enqueued in one piece at tune-complete:
    //   scanning section (C×2 slots; group calls C×2×pair words, C ≤ 10)
    //   + leading call (2 × max_addr(5) = 10) + conclusion (max_addr(5)).
    // Individual C=10 → 20+10+5 = 35; group headroom → 64.
    constexpr uint32_t MAX_TX_SEQUENCE_WORDS    = 64u;

} // namespace ALETimingConstants

namespace ale {

// ────────────────────────────────────────────────────────────────────────────
// Per-instance override of Level 5 "Programmable defaults" (§"Programmable
// timing parameters" — "network manager can override all values in this
// section"). ALETimingConstants::Twa_ms / Tt_ms above remain the immutable
// spec-derived defaults; each ALEStateMachine holds its own TimingParameters
// (default-initialised from those constants) so overriding one instance
// (e.g. via ALEController::set_link_idle_timeout_sec()) never affects any
// other instance in the same process — no global/shared mutable state.
// ────────────────────────────────────────────────────────────────────────────
struct TimingParameters {
    uint32_t Twa_ms = ALETimingConstants::Twa_ms;  ///< wait-for-activity / link-idle timeout
    uint32_t Tt_ms  = ALETimingConstants::Tt_ms;   ///< blind-tune delay (TUNING phase)
};

} // namespace ale
