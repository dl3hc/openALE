/**
 * \file Modem/channel_occupancy.h
 * \brief Broadband channel-occupancy (busy) detector for listen-before-transmit.
 *
 * MIL-STD-188-141B A.5.4.7.2 requires the LBT function to detect *any* traffic
 * per A.4.2.2 Table A-I — not just ALE: SSB voice (6 dB SNR → P≥0.80), serial-
 * tone PSK, STANAG 4285/4529, with ≤1 % false alarms over a 2 s dwell.  The
 * word-decode pipeline cannot see non-ALE signals (the vote gates reject them),
 * so this detector provides the "internal signal detection" leg of A.5.4.7.2:
 * a block-energy detector against an adaptively tracked noise floor.
 *
 * Principle (energy detection — all Table A-I waveforms are continuous-energy
 * signals well above the required SNRs):
 *
 *   - RX PCM is accumulated in 100 ms blocks (800 samples @ 8 kHz).
 *   - Per block: mean-removed average power → level in dB (arbitrary reference;
 *     only level-vs-floor differences matter).
 *   - Noise floor: asymmetric EWMA of block levels — tracks DOWN fast (a quiet
 *     block quickly restores the floor) but is pulled UP only by non-busy
 *     blocks, so a signal does not absorb itself into the floor.
 *   - Escape hatch against permanent blocking (operator requirement): the floor
 *     additionally drifts up unconditionally by FLOOR_DRIFT_DB per block, so a
 *     genuine step rise in local noise reads "busy" for at most
 *     margin_db / (FLOOR_DRIFT_DB × 10 blocks/s) seconds (≈60 s at the 6 dB
 *     default) and then adapts.  The A.5.4.7.3 operator override remains the
 *     hard bypass on top of this.
 *   - Busy decision: a block is "hot" when level > floor + margin_db; the
 *     channel is BUSY when ≥ 2 of the last 4 blocks are hot (N-of-M vote —
 *     rides through single impulse hits and voice syllable gaps alike).
 *
 * margin_db is operator-settable (default 6 dB) to match local noise
 * conditions; see ALEController::set_lbt_margin_db().
 *
 * Timescales: the LBT decision windows are 784 ms (ALE-only channels) to ≥2 s
 * (shared channels, A.5.4.7.1), i.e. 8–20 blocks — ample for the N-of-M vote.
 * Per-sample resolution is deliberately NOT needed here.
 *
 * Threading: single-producer — call push_samples() and the getters from the
 * same (audio) thread, or externally synchronise.  is_busy() is a relaxed
 * atomic read and safe from other threads (the SM update loop).
 */

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace ale {

class ChannelOccupancyDetector {
public:
    static constexpr uint32_t BLOCK_SAMPLES  = 800;    ///< 100 ms @ 8 kHz
    static constexpr float    DEFAULT_MARGIN_DB = 6.0f;

    /** Feed RX PCM (8 kHz mono int16). Only feed while RX actually listens on
     *  the channel — own-TX audio would poison the floor and busy state. */
    void push_samples(const int16_t* samples, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            const float x = static_cast<float>(samples[i]);
            sum_    += x;
            sum_sq_ += x * x;
            if (++acc_count_ >= BLOCK_SAMPLES)
                finish_block_();
        }
    }

    /** True when ≥2 of the last 4 completed 100 ms blocks exceeded
     *  floor + margin. Safe to call from the SM/update thread. */
    bool is_busy() const { return busy_.load(std::memory_order_relaxed); }

    void  set_margin_db(float db) { margin_db_ = (db < 0.0f) ? 0.0f : db; }
    float margin_db() const       { return margin_db_; }

    float level_db() const { return level_db_; }   ///< last completed block level
    float floor_db() const { return floor_db_; }   ///< tracked noise floor

    /** Forget floor and busy history (e.g. after a long RX-off period). */
    void reset() {
        acc_count_ = 0; sum_ = 0.0f; sum_sq_ = 0.0f;
        floor_valid_ = false;
        hot_ring_ = 0; blocks_seen_ = 0;
        busy_.store(false, std::memory_order_relaxed);
    }

private:
    // Floor tracking constants — see file header for the rationale.
    static constexpr float ALPHA_DOWN     = 0.30f;   ///< fast: quiet restores floor
    static constexpr float ALPHA_UP       = 0.05f;   ///< non-busy blocks pull floor up
    static constexpr float FLOOR_DRIFT_DB = 0.01f;   ///< unconditional up-drift per block

    void finish_block_() {
        const float n    = static_cast<float>(acc_count_);
        const float mean = sum_ / n;
        const float var  = (sum_sq_ / n) - mean * mean;   // mean-removed power
        acc_count_ = 0; sum_ = 0.0f; sum_sq_ = 0.0f;

        // +1.0 keeps log10 finite on digital silence (level 0 dB).
        level_db_ = 10.0f * std::log10(var + 1.0f);

        if (!floor_valid_) { floor_db_ = level_db_; floor_valid_ = true; }

        const bool hot = level_db_ > floor_db_ + margin_db_;

        // Asymmetric EWMA: down always fast; up only from non-hot blocks —
        // plus the small unconditional drift as the anti-lockout escape hatch.
        if (level_db_ < floor_db_)
            floor_db_ += ALPHA_DOWN * (level_db_ - floor_db_);
        else if (!hot)
            floor_db_ += ALPHA_UP * (level_db_ - floor_db_);
        floor_db_ += FLOOR_DRIFT_DB;

        // N-of-M vote over the last 4 blocks (2-of-4 → busy).
        hot_ring_ = static_cast<uint8_t>(((hot_ring_ << 1) | (hot ? 1u : 0u)) & 0x0Fu);
        if (blocks_seen_ < 4) ++blocks_seen_;
        int hot_count = 0;
        for (uint8_t b = hot_ring_; b; b >>= 1) hot_count += (b & 1u);
        busy_.store(hot_count >= 2, std::memory_order_relaxed);
    }

    // Block accumulation
    uint32_t acc_count_ = 0;
    float    sum_       = 0.0f;
    float    sum_sq_    = 0.0f;

    // Levels
    float level_db_    = 0.0f;
    float floor_db_    = 0.0f;
    bool  floor_valid_ = false;
    float margin_db_   = DEFAULT_MARGIN_DB;

    // Busy vote
    uint8_t           hot_ring_    = 0;   ///< last 4 block hot flags
    uint8_t           blocks_seen_ = 0;
    std::atomic<bool> busy_{false};
};

} // namespace ale
