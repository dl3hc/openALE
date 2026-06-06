/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — word-level TX with built-in 3× redundancy (A.5.2.2.4)
 *
 * Sits between the Link Establishment layer (ALEStateMachine) and the physical
 * FSK layer (ToneGenerator).
 *
 * ## Timing model (matches ALEStateMachine::update() convention)
 *
 *   The integration layer drives BOTH the state machine and the modem with the
 *   same absolute timestamp:
 *
 *     uint32_t now = get_time_ms();
 *     sm.update(now);     // may call transmit_callback → enqueue_word()
 *     modem.update(now);  // fires physical copies on schedule
 *
 *   enqueue_word() records the word and marks the first copy as pending.
 *   update() sends the first copy immediately on the same tick, then schedules
 *   the remaining two copies at TW_INT_MS = Trw_ms / 3 intervals each.
 *
 *   Copy schedule for a word enqueued at t = T:
 *     copy 0  →  t = T              (first update() tick after enqueue)
 *     copy 1  →  t = T + TW_INT_MS  (≈ Tw = 130 ms)
 *     copy 2  →  t = T + 2·TW_INT_MS (≈ 2·Tw = 261 ms)
 *   Total on-air time: 3 copies × TW_INT_MS ≈ Trw = 392 ms  (−2 ms rounding)
 *
 * ## IModem not used
 *   IModem::transmit(bytes*, size) operates on raw bytes and does not fit the
 *   word-oriented ALE protocol.  ALE2GModem uses the ALEWord abstraction directly.
 */

#pragma once

#include "FSK/tone_generator.h"
#include "FSK/ale_waveform.h"
#include "FSK/symbol_decoder.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include <functional>
#include <array>
#include <cstdint>

namespace ale {

class ALE2GModem {
public:
    /** Audio output: called once per copy with SAMPLES_PER_COPY PCM samples (8 kHz). */
    using TxCallback       = std::function<void(const int16_t* samples, uint32_t count)>;
    /** Completion: fired after all SYMBOL_REPETITION copies of a word have been sent. */
    using WordDoneCallback = std::function<void()>;

    ALE2GModem();

    void set_tx_callback(TxCallback cb)           { tx_cb_   = std::move(cb); }
    void set_word_done_callback(WordDoneCallback cb) { done_cb_ = std::move(cb); }

    /**
     * Enqueue one logical word for 3× transmission (A.5.2.2.4).
     * Typically called by the state machine via transmit_callback inside sm.update().
     * The first copy is sent on the very next modem.update() call.
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Drive the modem forward in time.
     *
     * Call this in the same integration loop as ALEStateMachine::update(), passing
     * the same timestamp.  The modem fires one physical copy whenever
     * current_time_ms >= next_copy_ms_ and copies are still pending.
     *
     * \param current_time_ms  Absolute wall-clock time in milliseconds
     */
    void update(uint32_t current_time_ms);

    bool is_transmitting() const { return copies_remaining_ > 0; }

private:
    // Integer approximation of Tw = Trw / 3  (130 ms; −0.67 ms rounding per copy)
    static constexpr uint32_t TW_INT_MS = ALETimingConstants::Trw_ms / SYMBOL_REPETITION;

    // 49 symbols × 64 samples/symbol = 3136 samples per copy
    static constexpr uint32_t SAMPLES_PER_COPY =
        SYMBOLS_PER_WORD * (SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD);

    ALEWord  pending_word_;
    uint8_t  copies_remaining_ = 0;
    bool     word_enqueued_    = false;  // first copy not yet sent
    uint32_t next_copy_ms_     = 0;

    std::array<uint8_t, SYMBOLS_PER_WORD> symbol_buf_;
    std::array<int16_t, SAMPLES_PER_COPY> sample_buf_;

    ToneGenerator    generator_;
    TxCallback       tx_cb_;
    WordDoneCallback done_cb_;

    // Encode pending_word_ into symbol_buf_ (called once per enqueue_word()).
    void build_symbols();

    // Generate and deliver one word copy via tx_cb_.
    void send_one_copy();
};

} // namespace ale
