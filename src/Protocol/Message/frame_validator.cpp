/**
 * \file Protocol/Message/frame_validator.cpp
 * \brief ALE frame-structure validator implementations (moved from Word/ale_word.cpp).
 *
 * Whole-frame conformance checks on std::vector<ALEWord>: frame-level protocol
 * validation, not signal/word decoding — hence Protocol/Message layer, not Word.
 */

#include "Protocol/Message/frame_validator.h"
#include <cstring>

namespace ale {

// ── OFS catalog-frame validation (docs/FRAMING_STANDARD.md §7, FR-09) ────────

std::optional<std::string> FrameValidator::validate_frame(FrameType type,
                                                           const std::vector<ALEWord>& words)
{
    // Catalog type is part of the signature so per-frame-type rules have a
    // place to live (§6 decision rules are reception-context, not grammar —
    // they stay on the RX side). The grammar below is type-independent.
    (void)type;

    if (words.empty())
        return std::string("empty frame");

    if (!tis_twas_mutually_exclusive(words))
        return std::string("TIS and TWAS in one frame (A.5.2.5.3)");

    constexpr size_t ADDR_MAX_WORDS = 5;   // ≤5 words / 15 chars (A.5.2.4.4)
    // A.5.7.2.3 Tm: 90 chars of message text = the CMD word's first 3 chars
    // + 29 DATA/REP words ("30 data words / 90 chars" counts the CMD-carrying
    // word; "59 words counting CMD" = 29 data + 30 CMD).
    constexpr size_t AMD_MAX_DATA   = 29;
    constexpr size_t CMD_MAX_WORDS  = 30;  // ≤30 CMD words per message section (A.5.7.2.3)

    // Single section-aware pass (FR-03/FR-04/FR-11):
    //   - Address run: anchor (TO/THRU/FROM/TIS/TWAS) + following DATA/REP
    //     extensions. Any anchor closes the previous run AND the message
    //     section (the conclusion ends it; a TO after a conclusion is a new
    //     frame copy — the doubled EFS/orderwire pattern).
    //   - Message section: from a CMD word until the next anchor. Payload
    //     DATA/REP never attach to address runs (FR-04).
    bool   in_message        = false;
    size_t cmd_count         = 0;
    size_t block_data        = 0;    // DATA/REP words since the last CMD
    bool   block_is_dtm_dbm  = false;

    PreambleType anchor      = PreambleType::UNKNOWN;
    size_t       run_len     = 0;
    PreambleType last_ext    = PreambleType::UNKNOWN;
    bool         run_stuffed = false;  // an extension word carried '@'

    for (const ALEWord& w : words) {
        switch (w.type) {
            case PreambleType::TO:
            case PreambleType::THRU:
            case PreambleType::FROM:
            case PreambleType::TIS:
            case PreambleType::TWAS:
                in_message = false;
                block_data = 0;
                anchor     = w.type;
                run_len     = 1;
                last_ext    = PreambleType::UNKNOWN;
                run_stuffed = false;
                break;

            case PreambleType::CMD: {
                in_message = true;
                ++cmd_count;
                if (cmd_count > CMD_MAX_WORDS)
                    return std::string(">30 CMD words in the message section (A.5.7.2.3)");
                block_data = 0;
                // FR-11: the CMD names the payload protocol that owns the
                // block. DTM/DBM carry their own size rules (partially
                // implemented protocols) and are exempt from the AMD cap.
                // An AMD message whose first three characters happen to be
                // "DTM"/"DBM" is misread as exempt here — fail-open and
                // harmless: encode_amd() already truncates to 90 chars, so
                // this cap is defense-in-depth for future encoders only.
                block_is_dtm_dbm =
                    (strncmp(w.address, "DTM", 3) == 0) ||
                    (strncmp(w.address, "DBM", 3) == 0);
                break;
            }

            case PreambleType::DATA:
            case PreambleType::REP: {
                if (in_message) {
                    // Payload word — owned by the block's CMD (FR-11); never
                    // an address extension (FR-04).
                    ++block_data;
                    if (!block_is_dtm_dbm && block_data > AMD_MAX_DATA)
                        return std::string("AMD block exceeds Tm max "
                                          "(90 chars / 29 data words, A.5.7.2.3)");
                    break;
                }
                // Address extension (FR-03).
                if (run_len == 0)
                    return std::string("DATA/REP extension before any anchor word");
                ++run_len;
                if (run_len > ADDR_MAX_WORDS)
                    return std::string("address run exceeds 5 words / 15 chars (A.5.2.4.4)");
                // Stuffing belongs in the run's LAST word only (A.5.2.4.3):
                // once an extension carried '@', no further extension may
                // follow in the same run (anchor words are exempt — special
                // addresses carry '@' in the anchor itself, A.5.2.4.7-12).
                if (run_stuffed)
                    return std::string("'@' stuffing only in the last address word (A.5.2.4.3)");
                if (memchr(w.address, '@', 3) != nullptr)
                    run_stuffed = true;
                // Alternation (A.5.2.4.4.2): DATA directly after DATA is
                // always illegal (AC-WORD-009-1). REP after REP is NOT
                // rejected here: in the group leading call REP is a
                // new-recipient marker and consecutive REPs are legal
                // (A.5.5.4.3.2 — encode_group() emits them, pinned by
                // test_ale_calling); in a conclusion the first-extension-
                // must-be-DATA rule below already rejects a REP before a
                // second one could occur.
                if (w.type == PreambleType::DATA && last_ext == PreambleType::DATA)
                    return std::string("DATA directly after DATA extension (A.5.2.4.4.2)");
                if (last_ext == PreambleType::UNKNOWN
                        && (anchor == PreambleType::TIS || anchor == PreambleType::TWAS)
                        && w.type == PreambleType::REP)
                    return std::string("REP directly after conclusion — "
                                      "first extension must be DATA (A.5.2.5.3)");
                last_ext = w.type;
                break;
            }

            default:
                break;
        }
    }
    return std::nullopt;
}

bool FrameValidator::tis_twas_mutually_exclusive(const std::vector<ALEWord>& words)
{
    // AC-WORD-004-5 / AC-WORD-005-5: TIS and TWAS are mutually exclusive within
    // a single ALE frame.  TIS invites protocol continuation; TWAS terminates it.
    bool has_tis = false, has_twas = false;
    for (const auto& w : words) {
        if (w.type == PreambleType::TIS)  has_tis  = true;
        if (w.type == PreambleType::TWAS) has_twas = true;
    }
    return !(has_tis && has_twas);
}

bool FrameValidator::from_count_valid(const std::vector<ALEWord>& words)
{
    int count = 0;
    for (const auto& w : words) {
        if (w.type == PreambleType::FROM) ++count;
    }
    return count <= 1;
}

bool FrameValidator::from_precedes_cmd_only(const std::vector<ALEWord>& words)
{
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].type != PreambleType::FROM) continue;
        // Skip optional DATA/REP address extension words
        size_t j = i + 1;
        while (j < words.size() &&
               (words[j].type == PreambleType::DATA || words[j].type == PreambleType::REP)) {
            ++j;
        }
        // FROM (+ optional DATA/REP) must be immediately followed by CMD
        if (j >= words.size() || words[j].type != PreambleType::CMD) {
            return false;
        }
    }
    return true;
}

bool FrameValidator::thru_in_scanning_section_only(const std::vector<ALEWord>& words)
{
    bool past_scanning = false;
    for (const auto& w : words) {
        switch (w.type) {
            case PreambleType::TO:
            case PreambleType::TIS:
            case PreambleType::TWAS:
            case PreambleType::FROM:
            case PreambleType::CMD:
                past_scanning = true;
                break;
            case PreambleType::THRU:
                if (past_scanning) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

bool FrameValidator::thru_rep_alternates(const std::vector<ALEWord>& scanning_words)
{
    // Scanning section must be complete THRU,REP pairs; expect_thru toggles
    // per word and must end true (all pairs complete) for a valid frame.
    bool expect_thru = true;
    for (const auto& w : scanning_words) {
        if (w.type != PreambleType::THRU && w.type != PreambleType::REP) continue;
        if (expect_thru  && w.type != PreambleType::THRU) return false;
        if (!expect_thru && w.type != PreambleType::REP)  return false;
        expect_thru = !expect_thru;
    }
    return expect_thru; // true only after an even number of THRU/REP words
}

bool FrameValidator::group_call_target_count_valid(const std::vector<ALEWord>& scanning_words)
{
    // Accumulate distinct THRU target addresses; maximum is 5 per spec.
    std::vector<std::string> seen;
    for (const auto& w : scanning_words) {
        if (w.type != PreambleType::THRU) continue;
        std::string addr(w.address, 3);
        bool found = false;
        for (const auto& s : seen) {
            if (s == addr) { found = true; break; }
        }
        if (!found) {
            if (seen.size() >= 5) return false;
            seen.push_back(addr);
        }
    }
    return true;
}

bool FrameValidator::cmd_not_before_address_section(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-3: CMD must only appear after the address section has started;
    // any CMD preceding the first TO/FROM/TIS/TWAS word is a violation.
    bool address_seen = false;
    for (const auto& word : words) {
        switch (word.type) {
            case PreambleType::TO:
            case PreambleType::FROM:
            case PreambleType::TIS:
            case PreambleType::TWAS:
                address_seen = true;
                break;
            case PreambleType::CMD:
                if (!address_seen) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

bool FrameValidator::cmd_has_call_and_conclusion(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-4: a frame with CMD must have a preceding call and a following conclusion.
    size_t cmd_idx = words.size();
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].type == PreambleType::CMD) { cmd_idx = i; break; }
    }
    if (cmd_idx == words.size()) return true;  // no CMD — no constraint

    bool has_call = false;
    for (size_t i = 0; i < cmd_idx; ++i) {
        if (words[i].type == PreambleType::TO   ||
            words[i].type == PreambleType::FROM  ||
            words[i].type == PreambleType::TIS   ||
            words[i].type == PreambleType::TWAS) {
            has_call = true;
            break;
        }
    }
    if (!has_call) return false;

    for (size_t i = cmd_idx + 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::TIS || words[i].type == PreambleType::TWAS)
            return true;
    }
    return false;
}

bool FrameValidator::message_sections_begin_with_cmd(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-1: THRU must not appear inside the message section (after CMD).
    bool in_message_section = false;
    for (const auto& word : words) {
        if (word.type == PreambleType::CMD) {
            in_message_section = true;
        } else if (in_message_section &&
                   (word.type == PreambleType::THRU || word.type == PreambleType::UNKNOWN)) {
            return false;
        }
    }
    return true;
}

bool FrameValidator::first_cmd_begins_message_section(const std::vector<ALEWord>& words)
{
    // AC-WORD-008-5: first CMD is the boundary between Calling Cycle and Message
    // section; calling-section word types (TO, FROM, TIS, THRU) must not recur after it.
    bool past_first_cmd = false;
    for (const auto& word : words) {
        if (word.type == PreambleType::CMD) {
            past_first_cmd = true;
        } else if (past_first_cmd) {
            if (word.type == PreambleType::TO   ||
                word.type == PreambleType::FROM  ||
                word.type == PreambleType::TIS   ||
                word.type == PreambleType::THRU) {
                return false;
            }
        }
    }
    return true;
}

bool FrameValidator::rep_not_preceded_by_self_tis_twas(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-6: REP must not be directly preceded by REP, TIS, or TWAS.
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::REP) {
            if (words[i-1].type == PreambleType::REP ||
                words[i-1].type == PreambleType::TIS  ||
                words[i-1].type == PreambleType::TWAS) {
                return false;
            }
        }
    }
    return true;
}

bool FrameValidator::no_consecutive_same_preamble(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-2/3: consecutive words must differ in preamble type so
    // receivers can distinguish data changes from repeated transmissions.
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == words[i-1].type) return false;
    }
    return true;
}

bool FrameValidator::rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-7: not enforceable at single-frame level. "Multiple sender
    // situation" is a network-state concept (net call, simultaneous TX from
    // several stations) needing cross-station context unavailable here;
    // deferred to ALEStateMachine net-call handling (NET_CALL_STUB).
    (void)words;
    return true;
}

bool FrameValidator::data_not_after_data(const std::vector<ALEWord>& words)
{
    // AC-WORD-009-1: DATA must not extend a DATA word (only non-DATA words may be extended).
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == PreambleType::DATA && words[i-1].type == PreambleType::DATA)
            return false;
    }
    return true;
}

bool FrameValidator::address_section_word_count_valid(const std::vector<ALEWord>& words)
{
    // AC-FRAME-006-002 / REQ-FRAME-013: Ta max = 5 words = 5×Trw = 1960 ms (Table A-XII).
    // An address sequence begins on an anchor word (TO/TIS/TWAS/FROM/THRU) and continues
    // through immediately-following DATA/REP extension words; every such sequence must be
    // ≤5 words (15-char address, 3 chars/word).
    constexpr size_t MAX = 5u;
    size_t current = 0;
    for (const auto& w : words) {
        switch (w.type) {
            case PreambleType::TO:
            case PreambleType::TIS:
            case PreambleType::TWAS:
            case PreambleType::FROM:
            case PreambleType::THRU:
                if (current > MAX) return false;
                current = 1;
                break;
            case PreambleType::DATA:
            case PreambleType::REP:
                if (++current > MAX) return false;
                break;
            case PreambleType::CMD:
                if (current > MAX) return false;
                current = 0;
                break;
            default:
                break;
        }
    }
    return current <= MAX;
}

// Strip trailing '@' padding characters (A.5.2.4.3) from a 3-char address field.
static std::string strip_padding(const char* addr)
{
    std::string s(addr);
    while (!s.empty() && s.back() == '@')
        s.pop_back();
    return s;
}

std::vector<std::string> FrameValidator::reconstruct_to_addresses(const std::vector<ALEWord>& words)
{
    // REP repeats the function of the previous non-REP word:
    //   last_non_rep == TO   → REP acts as TO   → new recipient
    //   last_non_rep == DATA → REP acts as DATA → extend current address
    std::vector<std::string> addresses;
    std::string current;
    PreambleType last_non_rep = PreambleType::UNKNOWN;

    for (const auto& word : words) {
        switch (word.type) {
            case PreambleType::TO:
                if (!current.empty()) addresses.push_back(current);
                current = strip_padding(word.address);
                last_non_rep = PreambleType::TO;
                break;
            case PreambleType::DATA:
                current += strip_padding(word.address);
                last_non_rep = PreambleType::DATA;
                break;
            case PreambleType::REP:
                if (last_non_rep == PreambleType::TO) {
                    addresses.push_back(current);
                    current = strip_padding(word.address);
                    // last_non_rep stays TO: next REP would also be a new recipient
                } else {
                    current += strip_padding(word.address);
                    // last_non_rep stays DATA: REP after DATA acts as DATA
                }
                break;
            default:
                break;
        }
    }

    if (!current.empty()) addresses.push_back(current);
    return addresses;
}

} // namespace ale