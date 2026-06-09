/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem implementation
 */

#include "Modem/ale2g_modem.h"
#include <cassert>

namespace ale {

ALE2GModem::ALE2GModem()
    : copies_remaining_(0), word_enqueued_(false)
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
        assert(tx49_queue_.size() < ALETimingConstants::MAX_TX_SEQUENCE_WORDS &&
               "ALE2GModem queue overflow — SM enqueued more words than one full "
               "TX sequence (max 15 = 2x5 addr + 5 conclusion); "
               "likely a loop or double-transmit bug");
        tx49_queue_.push(tx49);
        return;
    }
    pending_tx49_     = tx49;
    copies_remaining_ = 1;   // one Trw block of 49 symbols (A.5.1.3)
    word_enqueued_    = true;
    build_symbols();
}

void ALE2GModem::build_symbols() {
    // Pack the 49-bit word into 49 8-FSK symbol values (MSB-first per symbol).
    //
    // The 147-bit stream is three identical copies of the 49-bit word laid end-to-end.
    // Symbol k covers stream bits [3k, 3k+1, 3k+2], read MSB-first:
    //   symbol[k] = word_bit(3k%49) as bit2, word_bit((3k+1)%49) as bit1, word_bit((3k+2)%49) as bit0
    //
    // This guarantees stream[i] = word_bit[i % 49], so bits at stride 49 are identical
    // and the majority vote in every ALE decoder succeeds (REQ-WAVEFORM-009).
    symbol_buf_.fill(0);
    for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
        uint8_t sym = 0;
        for (uint32_t b = 0; b < BITS_PER_SYMBOL; ++b) {
            const uint32_t word_pos = (k * BITS_PER_SYMBOL + b) % SYMBOLS_PER_WORD;
            if ((pending_tx49_ >> word_pos) & 1u)
                sym |= static_cast<uint8_t>(1u << (BITS_PER_SYMBOL - 1u - b));
        }
        symbol_buf_[k] = sym;
    }
}

void ALE2GModem::send_one_copy() {
    generator_.generate_symbols(symbol_buf_.data(), SYMBOLS_PER_WORD,
                                 sample_buf_.data(), 0.7f);
    if (tx_cb_) tx_cb_(sample_buf_.data(), SAMPLES_PER_WORD);
}

void ALE2GModem::update(uint32_t /*current_time_ms*/) {
    if (!word_enqueued_) return;

    word_enqueued_    = false;
    copies_remaining_ = 0;
    send_one_copy();
    if (done_cb_) done_cb_();
    advance_queue_();
}

void ALE2GModem::advance_queue_() {
    if (tx49_queue_.empty()) return;
    pending_tx49_     = tx49_queue_.front();
    tx49_queue_.pop();
    copies_remaining_ = 1;
    word_enqueued_    = true;
    build_symbols();
}

} // namespace ale
