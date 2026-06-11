/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — word-level TX per MIL-STD-188-141B A.5.1.3 / REQ-WAVEFORM-008
 *
 * Sits between the Link Establishment layer (ALEStateMachine) and the physical
 * FSK layer (ToneGenerator).
 *
 * ## On-air word format — 49 symbols per redundant word (AC-WAVEFORM-008-2)
 *
 *   The three redundant copies of the 49-bit encoded word are packed as a
 *   continuous 147-bit stream (3 × 49 bits) into 49 8-FSK symbol periods:
 *
 *     symbol[k] = word_bits[3k..3k+2]  read MSB-first  (indices mod 49)
 *
 *   That is: symbol[k] bit2 = word_bit(3k%49), bit1 = word_bit((3k+1)%49),
 *   bit0 = word_bit((3k+2)%49).  The MSB-first convention means every ALE
 *   decoder's bit-stream contains stream[i] = word_bit[i%49], so bits at
 *   stride-49 are identical and the majority vote succeeds (REQ-WAVEFORM-009).
 *   49 symbols × 8 ms/symbol = 392 ms = Trw  (exact, REQ-WAVEFORM-010).
 *
 * ## Integration loop
 *
 *     uint32_t now = get_time_ms();
 *     sm.update(now);     // may call transmit_callback → enqueue_word()
 *     modem.update(now);  // transmits the 49-symbol block on the first tick
 *                         // after enqueue, then fires done_cb_
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
#include "Word/ale_frame.h"
#include <functional>
#include <array>
#include <queue>
#include <cstdint>

namespace ale {

class ALE2GModem {
public:
    /** Audio output: called once per word with SAMPLES_PER_WORD PCM samples (8 kHz). */
    using TxCallback       = std::function<void(const int16_t* samples, uint32_t count)>;
    /** Completion: fired after the 49-symbol block for one word has been sent. */
    using WordDoneCallback = std::function<void()>;

    ALE2GModem();

    void set_tx_callback(TxCallback cb)           { tx_cb_   = std::move(cb); }
    void set_word_done_callback(WordDoneCallback cb) { done_cb_ = std::move(cb); }

    /**
     * Enqueue one logical word for transmission (AC-WAVEFORM-008-2).
     * If the modem is idle, the 49-symbol block starts on the next modem.update() call.
     * If the modem is busy, the word is appended to the internal queue and sent
     * after all preceding words have completed — preserving word order.
     * done_cb_ fires once per word after the 49-symbol block has been handed off.
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Enqueue all words of a Frame for sequential 3× transmission.
     * Equivalent to calling enqueue_word() for each word in frame.words(),
     * but uses the pre-encoded 49-bit representation from Frame::encode()
     * directly — encoding happens once at enqueue time.
     */
    void enqueue_frame(const Frame& frame);

    /**
     * Drive the modem forward in time.
     *
     * Call this in the same integration loop as ALEStateMachine::update(), passing
     * the same timestamp.  The modem starts transmitting the pending 49-symbol block
     * on the first tick after enqueue_word(); done_cb_ fires after Trw_ms (392 ms)
     * of airtime has elapsed, not immediately.
     *
     * \param current_time_ms  Absolute wall-clock time in milliseconds
     */
    void update(uint32_t current_time_ms);

    bool is_transmitting() const { return word_playing_ || word_enqueued_; }

private:
    // 49 symbols × 64 samples/symbol = 3136 samples per word = 392 ms = Trw (exact)
    static constexpr uint32_t SAMPLES_PER_WORD =
        SYMBOLS_PER_WORD * (SAMPLE_RATE_HZ / SYMBOL_RATE_BAUD);  // 3136

    uint64_t pending_tx49_  = 0;
    bool     word_enqueued_ = false;
    bool     word_playing_  = false;
    uint32_t word_tx_end_ms_ = 0;   // wall-clock time when current word's airtime expires
    std::queue<uint64_t> tx49_queue_;

    std::array<uint8_t, SYMBOLS_PER_WORD> symbol_buf_;   // 49 on-air symbols
    std::array<int16_t, SAMPLES_PER_WORD> sample_buf_;   // 3136 samples

    ToneGenerator    generator_;
    TxCallback       tx_cb_;
    WordDoneCallback done_cb_;

    // Common enqueue path for word and frame: stores tx49, starts or queues.
    void enqueue_tx49_(uint64_t tx49);

    // Map the bits of pending_tx49_ into symbol_buf_.
    void build_symbols();

    // Generate and deliver one word copy via tx_cb_.
    void send_one_copy();

    // Pop the next word from word_queue_ and start it; no-op if queue empty.
    void advance_queue_();
};

} // namespace ale
