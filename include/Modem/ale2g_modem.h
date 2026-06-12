/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — deterministic symbol generator (no time, no PCM, no audio knowledge)
 *
 * Encodes ALEWords into 49 8-FSK symbol values (0–7) for audio-layer consumption.
 *
 * ## Encoding pipeline
 *
 *   ALEWord → word.encode() → tx49 (49-bit) → build_symbols() → symbol_buf_[49]
 *
 * ## On-air word format (AC-WAVEFORM-008-2)
 *
 *   symbol[k] = word_bit(3k%49) as bit2, word_bit((3k+1)%49) as bit1, word_bit((3k+2)%49) as bit0
 *
 *   49 symbols × 8 ms/symbol = 392 ms = Trw (REQ-WAVEFORM-010).
 *
 * ## Pull API
 *
 *   Main thread  — enqueue_word() / enqueue_frame() / is_transmitting()
 *   Audio thread — pull_symbol_frame(out_49)  →  fills buffer from next pending frame
 *
 *   Both paths are protected by an internal mutex.
 *   The audio driver calls ToneGenerator.generate_symbols() on the pulled frame.
 *   All timing, PCM generation, and completion tracking belong to the audio layer.
 */

#pragma once

#include "Codec/ale_encoder.h"
#include "FSK/ale_waveform.h"
#include "FSK/symbol_decoder.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include "Word/ale_frame.h"
#include <mutex>
#include <queue>
#include <cstdint>
#include <cstring>

namespace ale {

class ALE2GModem {
public:
    ALE2GModem();

    /**
     * Enqueue one logical word for transmission (AC-WAVEFORM-008-2).
     *
     * If the modem is idle, the 49-symbol frame is ready immediately.
     * If the modem is busy, the word is appended to the internal queue and prepared
     * after the current frame is pulled by the audio thread.
     *
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Enqueue all words of a Frame for sequential transmission.
     * Equivalent to calling enqueue_word() for each word in the frame.
     *
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_frame(const Frame& frame);

    /**
     * Pull the next pending symbol frame.
     *
     * Copies SYMBOLS_PER_WORD (49) symbol values (0–7) into out_49 and returns true
     * if a frame was available.  Returns false when idle (caller renders silence).
     *
     * After a successful pull, the next queued word (if any) is prepared internally
     * so it is ready on the following call.
     *
     * Thread-safe: may be called concurrently with enqueue_word().
     */
    bool pull_symbol_frame(uint8_t* out_49);

    /**
     * True if the modem has at least one pending symbol frame (current or queued).
     * Thread-safe.
     */
    bool is_transmitting() const;

private:
    mutable std::mutex   mtx_;

    uint64_t             pending_tx49_  = 0;
    bool                 word_enqueued_ = false;
    std::queue<uint64_t> tx49_queue_;

    SymbolFrame symbol_buf_;   // 49 on-air symbols (0–7)

    // All helpers run under mtx_
    void enqueue_tx49_(uint64_t tx49);
    void advance_queue_();
};

} // namespace ale
