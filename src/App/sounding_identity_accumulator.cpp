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
    // A.5.2.4.3 / AddressEncoder::chunk(): ext words alternate DATA,REP by
    // 1-based chunk index (odd=DATA, even=REP).
    return (slot_1based % 2 == 1) ? PreambleType::DATA : PreambleType::REP;
}

void SoundingIdentityAccumulator::fold_linear(Session& s, const ALEWord& word)
{
    // A.5.4.1.1: non_unanimous votes = 48 - unanimous_votes (correctable words);
    // uncorrectable half(s) -> 48. Same formulas as old rx_accumulate_sounding().
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
        // Max-weight wins; std::map iterates ascending by key, so strict '>'
        // keeps the lexicographically-first key on a tie — deterministic, no extra bookkeeping.
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
            // Same-station repeat (A.5.3.1 redundancy): only in-cycle position
            // resets; slot-vote maps and linear accumulators persist. The real
            // station reconfirming itself also retires any unconfirmed
            // mismatch seen since the last repeat — it was a one-off
            // miscorrection, not a second station (class doc point 3).
            session_->next_slot    = 1;
            session_->cycle_open   = true;
            session_->pending_anchor.clear();
            fold_linear(*session_, word);
            session_->last_word_ms = now_ms;
            return std::nullopt;
        }

        // Anchor content differs from the open session's. A single mismatched
        // word is never trusted as proof of a different station (it can be a
        // one-off Golay miscorrection of the real anchor) — only a REPEAT of
        // the same new content confirms it, mirroring the vote maps' own
        // "no single observation is authoritative" rule.
        if (!session_->pending_anchor.empty() && session_->pending_anchor == content) {
            // Confirmed: this content has now been seen twice. Flush the
            // previous (different) station and open a fresh session for the
            // newly confirmed one.
            Result flushed = build_result(*session_);
            session_.reset();
            open_session(word, content, frequency_hz, now_ms);
            if (flushed.station.empty()) return std::nullopt; // defensive: nothing usable was accumulated
            return flushed;
        }
        // First sighting of this mismatch: park it as the pending candidate.
        // The open session is left completely untouched — still counting
        // toward its own settle, still able to accept its real extension
        // word — so one stray word can neither truncate nor contaminate it.
        session_->pending_anchor = content;
        return std::nullopt;
    }

    if (!session_) return std::nullopt; // no session open, non-conclusion word: ignored entirely

    // Every word while a session is open refreshes the timer and folds into
    // the linear accumulators regardless of slot-assignability — direct fix
    // for the settle timer firing prematurely mid-burst on a noisy/invalid word.
    fold_linear(*session_, word);
    session_->last_word_ms = now_ms;

    if (session_->cycle_open) {
        if (session_->next_slot > kMaxExtSlots) {
            // Extension full (max 4 slots, MIL-STD-188-141B); nothing more to
            // assign, cycle intentionally left open.
        } else {
            const bool matches = word.valid
                && word.type == expected_type_for_slot(session_->next_slot);
            if (matches) {
                const std::string content = trim_ale_address(word.address);
                session_->slot_votes[session_->next_slot - 1][content]
                    += word.unanimous_votes;
                ++session_->next_slot;
            } else {
                // First mismatch (wrong type, golay_uncorrectable, or other
                // invalid): stop assigning further words this cycle. No
                // lookahead — cross-cycle redundancy recovers the gap via a later repeat.
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
