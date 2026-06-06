/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem implementation
 */

#include "Modem/ale2g_modem.h"
#include "FEC/ale_fec_codec.h"

namespace ale {

ALE2GModem::ALE2GModem()
    : copies_remaining_(0), word_enqueued_(false), next_copy_ms_(0)
{
    symbol_buf_.fill(0);
    sample_buf_.fill(0);
}

void ALE2GModem::enqueue_word(const ALEWord& word) {
    pending_word_     = word;
    copies_remaining_ = SYMBOL_REPETITION;  // 3 per A.5.2.2.4
    word_enqueued_    = true;
    build_symbols();
}

void ALE2GModem::build_symbols() {
    // Reconstruct 24-bit ALE word: 3-bit preamble | 21-bit payload
    const uint32_t raw24 = (static_cast<uint32_t>(pending_word_.type) << 21)
                         | (pending_word_.raw_payload & 0x1F'FFFFu);

    // Golay FEC encode + bit-level word interleaving → 49-bit transmitted word (A.5.2.2.3)
    const uint64_t tx49 = ALEFECCodec::interleave_word(raw24);

    // Map each bit to a 2-tone FSK symbol.
    // Decoder uses (symbol & 1u) → bit, so: bit 0 → sym 0 (750 Hz), bit 1 → sym 1 (1000 Hz).
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
        if (--copies_remaining_ == 0 && done_cb_)
            done_cb_();
        return;
    }

    if (current_time_ms < next_copy_ms_) return;

    // Scheduled copy (2nd or 3rd)
    next_copy_ms_ += TW_INT_MS;
    send_one_copy();
    if (--copies_remaining_ == 0 && done_cb_)
        done_cb_();
}

} // namespace ale
