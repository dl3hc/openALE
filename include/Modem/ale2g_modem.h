/**
 * \file Modem/ale2g_modem.h
 * \brief ALE 2G Modem — Modulator and Demodulator.
 *
 * namespace ale::ALE2GModem contains two classes:
 *
 *   Modulator   — TX side: word-queue + 49-symbol pull API (no PCM, no timing)
 *   Demodulator — RX side: streaming PCM → ALEWord via Goertzel + Grid-Lock
 *
 * ── Modulator ────────────────────────────────────────────────────────────────
 *
 * Encodes ALEWords into 49 8-FSK symbol values (0–7) for audio-layer pull.
 *
 *   ALEWord → word.encode() → tx49 (49-bit) → ALEEncoder → symbol_buf_[49]
 *
 * On-air word format (AC-WAVEFORM-008-2):
 *   symbol[k] = word_bit(3k%49) as bit2, word_bit((3k+1)%49) as bit1,
 *               word_bit((3k+2)%49) as bit0
 *   49 symbols × 8 ms/symbol = 392 ms = Trw (REQ-WAVEFORM-010).
 *
 * Pull API (thread-safe):
 *   Main thread  — enqueue_word() / enqueue_frame() / is_transmitting()
 *   Audio thread — pull_symbol_frame(out_49)
 *
 * ── Demodulator ──────────────────────────────────────────────────────────────
 *
 * Accepts 8 kHz / mono / int16 PCM via push_samples().  Every DECODE_STEP (16)
 * samples it attempts to decode the last 3136 samples (one ALE word block) via
 * Goertzel detection + ALEDecoder (majority vote + Golay FEC).  Accepted words
 * are delivered via word_cb_.
 *
 * Word-boundary sync:
 *   - DECODE_OK anchors / re-anchors the word grid.
 *   - DECODE_CORRECTED accepted only on the anchored grid (±1 symbol).
 *   - Silence-gap reset: 100 ms below threshold releases the grid lock so the
 *     next transmission re-anchors at its own sub-symbol phase.
 */

#pragma once

#include "Codec/ale_decoder.h"
#include "Codec/ale_encoder.h"
#include "FSK/ale_waveform.h"
#include "FEC/golay.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word.h"
#include "Word/ale_frame.h"
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstring>

namespace ale {
namespace ALE2GModem {

// ── Modulator ─────────────────────────────────────────────────────────────────

class Modulator {
public:
    Modulator();

    /**
     * Enqueue one logical word for transmission (AC-WAVEFORM-008-2).
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_word(const ALEWord& word);

    /**
     * Enqueue all words of a Frame for sequential transmission.
     * Thread-safe: may be called concurrently with pull_symbol_frame().
     */
    void enqueue_frame(const Frame& frame);

    /**
     * Pull the next pending symbol frame (49 symbol values, 0–7) into out_49.
     * Returns false when idle; caller renders silence.
     * Thread-safe: may be called concurrently with enqueue_word().
     */
    bool pull_symbol_frame(uint8_t* out_49);

    /** True if at least one symbol frame is pending. Thread-safe. */
    bool is_transmitting() const;

private:
    mutable std::mutex   mtx_;
    uint64_t             pending_tx49_  = 0;
    bool                 word_enqueued_ = false;
    std::queue<uint64_t> tx49_queue_;
    SymbolFrame          symbol_buf_;

    void enqueue_tx49_(uint64_t tx49);
    void advance_queue_();
};

// ── Demodulator ───────────────────────────────────────────────────────────────

class Demodulator {
public:
    using WordCallback = std::function<void(const ALEWord&)>;

    Demodulator();

    void set_word_callback(WordCallback cb) { word_cb_ = std::move(cb); }

    /**
     * Enable / disable decoding.  Disabling resets the ring buffer and
     * word-grid lock so stale signal cannot leak after re-enable.
     */
    void set_enabled(bool en) {
        if (!en && enabled_) reset();
        enabled_ = en;
    }
    bool enabled() const { return enabled_; }

    /** Feed PCM samples (8 kHz, mono, 16-bit). May invoke word_cb_. */
    void push_samples(const int16_t* samples, uint32_t count);

    void reset();

private:
    static constexpr uint32_t WORD_SAMPLES         = SYMBOLS_PER_WORD * FFT_SIZE; // 3136
    static constexpr uint32_t BUF_CAP              = WORD_SAMPLES + FFT_SIZE;     // 3200
    static constexpr uint32_t DECODE_STEP          = FFT_SIZE / 4;                // 16
    static constexpr uint32_t SILENCE_RESET_SAMPLES = 800;  // 100 ms at 8 kHz
    static constexpr int16_t  SILENCE_THRESHOLD    = 200;

    WordCallback         word_cb_;
    bool                 enabled_       = true;
    std::vector<int16_t> ring_;
    uint32_t             write_pos_     = 0;
    uint32_t             step_accum_    = 0;
    bool                 grid_locked_   = false;
    uint32_t             grid_anchor_   = 0;
    uint32_t             silence_count_ = 0;

    static float   goertzel_power(const int16_t* block, float freq_hz);
    static uint8_t symbol_from_block(const int16_t* block);
    int16_t        ring_at(uint32_t abs_pos) const { return ring_[abs_pos % BUF_CAP]; }
    bool           try_decode(ALEWord& out, Golay::DecodeResult& fec) const;
    bool           accept_word_(const Golay::DecodeResult& fec);
};

} // namespace ALE2GModem
} // namespace ale
