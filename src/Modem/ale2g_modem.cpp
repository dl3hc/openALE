/**
 * \file Modem/ale2g_modem.cpp
 * \brief ALE 2G Modem implementation
 */

#include "Modem/ale2g_modem.h"
#include "Codec/ale_encoder.h"
#include <cassert>

namespace ale {

ALE2GModem::ALE2GModem()
{
    symbol_buf_.fill(0);
}

void ALE2GModem::enqueue_word(const ALEWord& word)
{
    std::lock_guard<std::mutex> lk(mtx_);
    enqueue_tx49_(word.encode());
}

void ALE2GModem::enqueue_frame(const Frame& frame)
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (uint64_t tx49 : frame.encode())
        enqueue_tx49_(tx49);
}

bool ALE2GModem::pull_symbol_frame(uint8_t* out)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!word_enqueued_) return false;
    std::memcpy(out, symbol_buf_.data(), SYMBOLS_PER_WORD);
    word_enqueued_ = false;
    advance_queue_();
    return true;
}

bool ALE2GModem::is_transmitting() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return word_enqueued_ || !tx49_queue_.empty();
}

// ── Internal helpers (all called under mtx_) ─────────────────────────────────

void ALE2GModem::enqueue_tx49_(uint64_t tx49)
{
    if (word_enqueued_) {
        assert(tx49_queue_.size() < ALETimingConstants::MAX_TX_SEQUENCE_WORDS &&
               "ALE2GModem queue overflow — SM enqueued more words than one full "
               "contiguous calling sequence (scanning + leading + conclusion); "
               "likely a loop or double-transmit bug");
        tx49_queue_.push(tx49);
        return;
    }
    pending_tx49_  = tx49;
    word_enqueued_ = true;
    symbol_buf_    = ALEEncoder::encode_tx49(pending_tx49_);
}

void ALE2GModem::advance_queue_()
{
    // Called under mtx_ after pull_symbol_frame() clears word_enqueued_.
    // If the re-entrant enqueue path somehow set it again, respect that.
    if (word_enqueued_) return;
    if (tx49_queue_.empty()) return;
    pending_tx49_  = tx49_queue_.front();
    tx49_queue_.pop();
    word_enqueued_ = true;
    symbol_buf_    = ALEEncoder::encode_tx49(pending_tx49_);
}

} // namespace ale
