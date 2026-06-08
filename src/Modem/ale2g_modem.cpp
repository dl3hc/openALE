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
    if (copies_remaining_ > 0) {
        // Modem busy — queue for later (normal for multi-word address sequences).
        // Max queue depth for one TX sequence (response or ACK):
        //   2 × wpa(5 words max) + wpa(5 words max) = 15 words total
        //   → 14 in queue + 1 in pending_word_ = 15.
        // See ALETimingConstants::MAX_TX_SEQUENCE_WORDS.
        assert(word_queue_.size() < ALETimingConstants::MAX_TX_SEQUENCE_WORDS &&
               "ALE2GModem queue overflow — SM enqueued more words than one full "
               "TX sequence (max 15 = 2x5 addr + 5 conclusion); "
               "likely a loop or double-transmit bug");
        word_queue_.push(word);
        return;
    }
    pending_word_     = word;
    copies_remaining_ = SYMBOL_REPETITION;  // 3 per A.5.2.2.4
    word_enqueued_    = true;
    build_symbols();
}

void ALE2GModem::build_symbols() {
    // Reconstruct 24-bit ALE word: 3-bit preamble | 21-bit payload
    const uint32_t raw24 = (static_cast<uint32_t>(pending_word_.type) << 21)
                         | (pending_word_.raw_payload & 0x1F'FFFFu);

    // Step 1 — Golay (24,12) encode both halves (A.5.2.2.2):
    //   coded.coder_a = [W1..W12  | G1..G12 ]    24 bit — Coder A, check bits normal
    //   coded.coder_b = [W13..W24 | ~G13..~G24]  24 bit — Coder B, check bits inverted
    const GolayCoded coded = ALEFECCodec::encode_word(raw24);

    // Step 2 — Bit-interleave coder_a and coder_b, append S49=0 (A.5.2.2.3):
    //   [A1,B1, A2,B2, ..., A24,B24, S49=0]  → 49-bit transmitted word
    const uint64_t tx49 = ALEFECCodec::interleave_word(coded);

    // Map each bit to a 2-tone FSK symbol (bit 0 → 750 Hz, bit 1 → 1000 Hz).
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i)
        symbol_buf_[i] = static_cast<uint8_t>((tx49 >> i) & 1u);
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
    if (word_queue_.empty()) return;
    pending_word_     = word_queue_.front();
    word_queue_.pop();
    copies_remaining_ = SYMBOL_REPETITION;
    word_enqueued_    = true;
    build_symbols();
}

} // namespace ale
