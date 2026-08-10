/**
 * \file App/sounding_identity_accumulator.cpp
 * \brief SoundingIdentityAccumulator implementation.
 */

#include "App/sounding_identity_accumulator.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/address_encoder.h"

namespace ale {

const uint32_t SoundingIdentityAccumulator::kTdrwMs = ALETimingConstants::Tdrw_ms;

PreambleType SoundingIdentityAccumulator::expected_type_for_slot(int slot_1based)
{
    // A.5.2.4.3 / AddressEncoder::chunk(): extension words alternate
    // DATA,REP,DATA,REP by 1-based chunk index (odd=DATA, even=REP).
    return (slot_1based % 2 == 1) ? PreambleType::DATA : PreambleType::REP;
}

void SoundingIdentityAccumulator::fold_linear(Session& s, const ALEWord& word)
{
    // A.5.4.1.1: non_unanimous votes = 48 - unanimous_votes for correctable
    // words; uncorrectable half(s) -> contribute 48. Unchanged from the
    // original rx_accumulate_sounding() formulas.
    constexpr float kMaxVotes = 48.0f;
    const float snr_db = word.valid
        ? (word.unanimous_votes / kMaxVotes) * 31.0f : 0.0f;
    const float ber = word.golay_uncorrectable
        ? 48.0f
        : static_cast<float>(48u - word.unanimous_votes);

    s.snr_sum    += snr_db;
    s.ber_sum    += ber;
    s.sinad_sum  += word.sinad_db;
    s.word_count += 1;
}

void SoundingIdentityAccumulator::open_session(const ALEWord& word,
                                                const std::string& anchor_content,
                                                uint32_t frequency_hz,
                                                uint32_t now_ms)
{
    Session s;
    s.anchor       = anchor_content;
    s.freq_hz      = frequency_hz;
    s.twas         = (word.type == PreambleType::TWAS);
    s.next_slot    = 1;
    s.cycle_open   = true;
    s.last_word_ms = now_ms;
    fold_linear(s, word); // seeds word_count=1 + linear sums from the opening conclusion word
    session_ = std::move(s);
}

SoundingIdentityAccumulator::Result
SoundingIdentityAccumulator::build_result(const Session& s)
{
    Result r;
    r.station = s.anchor;
    for (int i = 0; i < kMaxExtSlots; ++i) {
        if (s.slot_votes[i].empty()) break; // stop at first slot with zero observations — never splice past a gap
        // Max-weight entry wins; std::map iterates ascending by key, so a
        // strict '>' comparison keeps the lexicographically-first key on a
        // tie (the map's natural deterministic order — no extra bookkeeping).
        std::string best;
        uint32_t    best_weight = 0;
        for (const auto& kv : s.slot_votes[i]) {
            if (kv.second > best_weight) {
                best_weight = kv.second;
                best        = kv.first;
            }
        }
        r.station += best;
    }
    r.frequency_hz = s.freq_hz;
    const float n = static_cast<float>(s.word_count);
    r.snr_db   = (n > 0.0f) ? s.snr_sum   / n : 0.0f;
    r.ber      = (n > 0.0f) ? s.ber_sum   / n : 0.0f;
    r.sinad_db = (n > 0.0f) ? s.sinad_sum / n : 0.0f;
    r.twas_conclusion = s.twas;
    return r;
}

std::optional<SoundingIdentityAccumulator::Result>
SoundingIdentityAccumulator::on_word(const ALEWord& word, uint32_t frequency_hz, uint32_t now_ms)
{
    const bool is_conclusion = word.valid
        && (word.type == PreambleType::TIS || word.type == PreambleType::TWAS);

    if (is_conclusion) {
        const std::string content = trim_ale_address(word.address);

        if (!session_) {
            open_session(word, content, frequency_hz, now_ms);
            return std::nullopt;
        }
        if (content == session_->anchor) {
            // Same-station repeat (A.5.3.1 redundancy): only the in-cycle
            // position resets; slot vote maps and linear accumulators persist.
            session_->next_slot    = 1;
            session_->cycle_open   = true;
            fold_linear(*session_, word);
            session_->last_word_ms = now_ms;
            return std::nullopt;
        }

        // Different station started talking: flush the finished session
        // first (this is the pre-existing same-vs-different-anchor branch,
        // fixed to flush-before-discard instead of silently discarding).
        Result flushed = build_result(*session_);
        session_.reset();
        open_session(word, content, frequency_hz, now_ms);
        if (flushed.station.empty()) return std::nullopt; // defensive: nothing usable was accumulated
        return flushed;
    }

    if (!session_) return std::nullopt; // no session open, non-conclusion word: ignored entirely

    // Every word event while a session is open refreshes the timer and
    // folds into the linear accumulators, regardless of whether it can be
    // slot-assigned — this is the direct fix for the settle timer firing
    // prematurely mid-burst on an ordinary noisy/invalid word.
    fold_linear(*session_, word);
    session_->last_word_ms = now_ms;

    if (session_->cycle_open) {
        if (session_->next_slot > kMaxExtSlots) {
            // Extension already full (max 4 slots per MIL-STD-188-141B);
            // nothing more to assign, cycle intentionally left open.
        } else {
            const bool matches = word.valid
                && word.type == expected_type_for_slot(session_->next_slot);
            if (matches) {
                const std::string content = trim_ale_address(word.address);
                session_->slot_votes[session_->next_slot - 1][content]
                    += word.unanimous_votes;
                ++session_->next_slot;
            } else {
                // First mismatch (wrong type, golay_uncorrectable, or
                // valid==false for any other reason): stop assigning
                // further words in THIS cycle to any slot. No lookahead —
                // cross-cycle redundancy recovers the gap via a later repeat.
                session_->cycle_open = false;
            }
        }
    }
    return std::nullopt;
}

bool SoundingIdentityAccumulator::timed_out(uint32_t now_ms) const
{
    return session_.has_value() && (now_ms - session_->last_word_ms) >= kTdrwMs;
}

std::optional<SoundingIdentityAccumulator::Result>
SoundingIdentityAccumulator::finalize()
{
    if (!session_) return std::nullopt;
    Result r = build_result(*session_);
    session_.reset();
    if (r.station.empty()) return std::nullopt; // defensive, mirrors old empty-acc guard
    return r;
}

} // namespace ale
