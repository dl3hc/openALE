/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem implementation
 */

#include "Modem/ale2g_modem.h"
#include <cassert>

namespace ale {

ALE2GModem::ALE2GModem()
    : copies_remaining_(0), word_enqueued_(false), next_copy_ms_(0)
{
    symbol_buf_.fill(0);
    sample_buf_.fill(0);
}

void ALE2GModem::enqueue_word(const ALEWord& word) {
    enqueue_tx49_(word.encode());
}

void ALE2GModem::enqueue_frame(const Frame& frame) {
    for (uint64_t tx49 : frame.encode())
        enqueue_tx49_(tx49);
}

void ALE2GModem::enqueue_tx49_(uint64_t tx49) {
    if (copies_remaining_ > 0) {
        // Modem busy — queue for later (normal for multi-word address sequences).
        // Max queue depth for one TX sequence (response or ACK):
        //   2 × wpa(5 words max) + wpa(5 words max) = 15 words total
        //   → 14 in queue + 1 in pending_tx49_ = 15.
        // See ALETimingConstants::MAX_TX_SEQUENCE_WORDS.
        assert(tx49_queue_.size() < ALETimingConstants::MAX_TX_SEQUENCE_WORDS &&
               "ALE2GModem queue overflow — SM enqueued more words than one full "
               "TX sequence (max 15 = 2x5 addr + 5 conclusion); "
               "likely a loop or double-transmit bug");
        tx49_queue_.push(tx49);
        return;
    }
    pending_tx49_     = tx49;
    copies_remaining_ = SYMBOL_REPETITION;  // 3 per A.5.2.2.4
    word_enqueued_    = true;
    build_symbols();
}

void ALE2GModem::build_symbols() {
    // Pack BITS_PER_SYMBOL=3 consecutive encoded bits into each 8-FSK symbol slot.
    // 49 encoded bits → 17 symbols carry data (ceil(49/3)); slots 17..48 stay 0.
    // ToneGenerator::generate_symbols() uses phase_increment[symbol_value], so a
    // symbol value of 0-7 directly selects one of the 8 ALE tones — no further
    // FREQ_TO_SYMBOL indirection needed here (already baked into ToneGenerator).
    symbol_buf_.fill(0);
    for (uint32_t s = 0; s < SYMBOLS_PER_WORD; ++s) {
        const uint32_t base_bit = s * BITS_PER_SYMBOL;
        if (base_bit >= SYMBOLS_PER_WORD) break;   // all 49 encoded bits consumed
        uint8_t sym = 0;
        for (uint32_t b = 0; b < BITS_PER_SYMBOL; ++b) {
            const uint32_t bit_pos = base_bit + b;
            if (bit_pos < SYMBOLS_PER_WORD)
                sym |= static_cast<uint8_t>(((pending_tx49_ >> bit_pos) & 1u) << b);
        }
        symbol_buf_[s] = sym;
    }
}

void ALE2GModem::send_one_copy() {
    generator_.generate_symbols(symbol_buf_.data(), SYMBOLS_PER_WORD,
                                 sample_buf_.data(), 0.7f);
    if (tx_cb_) tx_cb_(sample_buf_.data(), SAMPLES_PER_COPY);
}

void ALE2GModem::update(uint32_t current_time_ms) {
    if (copies_remaining_ == 0) return;

    if (word_enqueued_) {
        // First copy: send immediately and schedule the next
        word_enqueued_ = false;
        next_copy_ms_  = current_time_ms + TW_INT_MS;
        send_one_copy();
        if (--copies_remaining_ == 0) {
            if (done_cb_) done_cb_();
            advance_queue_();
        }
        return;
    }

    if (current_time_ms < next_copy_ms_) return;

    // Scheduled copy (2nd or 3rd)
    next_copy_ms_ += TW_INT_MS;
    send_one_copy();
    if (--copies_remaining_ == 0) {
        if (done_cb_) done_cb_();
        advance_queue_();
    }
}

void ALE2GModem::advance_queue_() {
    if (tx49_queue_.empty()) return;
    pending_tx49_     = tx49_queue_.front();
    tx49_queue_.pop();
    copies_remaining_ = SYMBOL_REPETITION;
    word_enqueued_    = true;
    build_symbols();
}

} // namespace ale
