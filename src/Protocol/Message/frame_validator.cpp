/**
 * \file Protocol/Message/frame_validator.cpp
 * \brief ALE frame-structure validator implementations (moved from Word/ale_word.cpp).
 *
 * These whole-frame conformance checks operate on a std::vector<ALEWord>; they
 * are frame-level protocol validation, not signal/word decoding, so they live
 * in the Protocol/Message layer.
 */

#include "Protocol/Message/frame_validator.h"

namespace ale {

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
    // Scanning section must consist of complete THRU, REP pairs.
    // expect_thru starts true; after each complete pair it returns to true.
    // Returning true requires expect_thru == true (all pairs complete).
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
    // AC-WORD-008-3: CMD must only appear after the address section has started.
    // Any CMD that precedes the first TO/FROM/TIS/TWAS word is a violation.
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
    // AC-WORD-008-4: a frame with CMD must have a preceding call and a following
    // conclusion.  Find the first CMD and check both sides.
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
    // AC-WORD-008-5: the first CMD marks the boundary between the Calling Cycle
    // and the Message section.  Calling-section word types (TO, FROM, TIS, THRU)
    // must not appear after the first CMD.
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
    // AC-WORD-010-2/3: consecutive words must have different preamble types so
    // receivers can distinguish data changes from repeated transmissions.
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i].type == words[i-1].type) return false;
    }
    return true;
}

bool FrameValidator::rep_not_used_in_multiple_sender_situation(const std::vector<ALEWord>& words)
{
    // AC-WORD-010-7: not enforceable at single-frame level.
    // "Multiple sender situation" is a network-state concept (net call, simultaneous
    // TX from several stations) that requires cross-station context unavailable here.
    // Enforcement deferred to ALEStateMachine net-call handling (NET_CALL_STUB).
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
    // AC-FRAME-006-002 / REQ-FRAME-013: Ta max = 5 words = 5 × Trw = 1960 ms (Table A-XII).
    // An address sequence begins on an anchor word (TO/TIS/TWAS/FROM/THRU) and continues
    // through immediately following DATA / REP extension words.  Every such sequence in
    // the frame must contain at most 5 words (= 15-char address / 3 chars per word).
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