/**
 * \file ale_message.cpp
 * \brief Implementation of ALE message assembly
 */

#include "Protocol/Message/ale_message.h"
#include "Word/address_encoder.h"
#include <algorithm>

namespace ale {

static const char* CALL_TYPE_NAMES[] = {
    "INDIVIDUAL", "NET", "GROUP", "ALL_CALL", "SOUNDING", 
    "AMD", "INDIVIDUAL_ACK", "NET_ACK", "UNKNOWN"
};

// ============================================================================
// MessageAssembler Implementation
// ============================================================================

MessageAssembler::MessageAssembler() 
    : active(false), last_word_time_ms(0), word_timeout_ms(5000) {}

bool MessageAssembler::add_word(const ALEWord& word) {
    if (!word.valid) {
        return false;  // Ignore invalid words
    }
    
    uint32_t current_time = word.timestamp_ms;
    
    // Check for timeout if assembly active
    if (active && check_timeout(current_time)) {
        reset();  // Timeout, start fresh
    }
    
    // Start new message or add to existing
    if (!active) {
        current_message.start_time_ms = current_time;
        active = true;
    }
    
    current_words.push_back(word);
    last_word_time_ms = current_time;
    
    // Check if sequence is complete
    if (is_sequence_complete(current_words)) {
        // Finalize message
        current_message.words = current_words;
        current_message.duration_ms = current_time - current_message.start_time_ms;
        current_message.call_type = determine_call_type(current_words);
        current_message.complete = true;
        
        extract_addresses(current_words, current_message);
        extract_data(current_words, current_message);
        
        return true;  // Message complete
    }
    
    return false;  // Still assembling
}

bool MessageAssembler::get_message(ALEMessage& output) {
    if (!current_message.complete) {
        return false;
    }
    
    output = current_message;
    reset();  // Clear for next message
    return true;
}

void MessageAssembler::reset() {
    current_words.clear();
    current_message = ALEMessage();
    active = false;
    last_word_time_ms = 0;
}

CallType MessageAssembler::determine_call_type(const std::vector<ALEWord>& words) {
    return CallTypeDetector::detect(words);
}

bool MessageAssembler::is_sequence_complete(const std::vector<ALEWord>& words) {
    if (words.empty()) return false;

    // A.5.2.3 / A.5.5.4.4: a frame concludes only on TIS or TWAS. FROM is a
    // mid-frame quick-ID (e.g. an AllCall/AnyCall's "address beginning with a
    // FROM word immediately after the calling cycle"), never a terminator —
    // a prior `has_to && has_from` fallback treated it as one and finalized
    // the frame the instant a FROM word appeared, mid-frame, before any
    // CMD/DATA/REP message content (notably AMD payloads, A.5.7.2.2) or the
    // real conclusion had arrived. That split single frames into two bogus
    // fragments (e.g. an AMD-in-calling-frame got misclassified as
    // INDIVIDUAL + SOUNDING instead of AMD). Removed; add_word()'s existing
    // timeout/reset already reclaims a frame that never concludes.
    for (const auto& w : words) {
        if (w.type == PreambleType::TIS || w.type == PreambleType::TWAS)
            return true;
    }
    return false;
}

void MessageAssembler::extract_addresses(const std::vector<ALEWord>& words, ALEMessage& msg) {
    // ALE 2G multi-word addresses: a preamble word (TO/TIS/etc.) carries the
    // first 3 chars; consecutive DATA/REP words carry chars 4-6, 7-9, etc.
    // Sounding exception: DATA suffix words appear BEFORE the TIS prefix word,
    // so we buffer them as "orphan_data" and prepend once TIS arrives.
    // AMD exception: after CMD the DATA/REP words are message content, not
    // address extension — the past_any_preamble flag prevents them from
    // contaminating orphan_data.
    enum class Ctx { NONE, DEST, SRC } ctx = Ctx::NONE;
    std::string current_addr;
    std::string orphan_data;    // DATA/REP before first preamble (sounding suffix)
    bool past_any_preamble = false;

    auto add_dest = [&](const std::string& a) {
        if (a.empty()) return;
        for (auto& ex : msg.to_addresses) {
            if (ex.size() <= a.size() && a.substr(0, ex.size()) == ex) {
                ex = a;   // Replace shorter prefix-only entry with full address
                return;
            }
            if (a.size() < ex.size() && ex.substr(0, a.size()) == a)
                return;   // New entry is a prefix of an existing longer entry — skip
        }
        msg.to_addresses.push_back(a);
    };

    auto finalize_current = [&]() {
        if (ctx == Ctx::DEST)
            add_dest(current_addr);
        else if (ctx == Ctx::SRC && !current_addr.empty())
            msg.from_address = current_addr;
        current_addr.clear();
    };

    for (const auto& word : words) {
        switch (word.type) {
            case PreambleType::TO:
            case PreambleType::TWAS:
                finalize_current();
                current_addr = trim_ale_address(word.address);
                ctx = Ctx::DEST;
                past_any_preamble = true;
                break;
            case PreambleType::FROM:
            case PreambleType::TIS:
                finalize_current();
                // Prepend orphaned DATA only if we haven't seen a dest preamble
                // (sounding sends DATA suffix before TIS; AMD sends DATA after CMD)
                current_addr = trim_ale_address(word.address) + orphan_data;
                orphan_data.clear();
                ctx = Ctx::SRC;
                past_any_preamble = true;
                break;
            case PreambleType::DATA:
            case PreambleType::REP:
                if (ctx == Ctx::NONE && !past_any_preamble)
                    orphan_data += trim_ale_address(word.address);  // sounding suffix
                else if (ctx != Ctx::NONE)
                    current_addr += trim_ale_address(word.address); // address extension
                // ctx==NONE && past_any_preamble: AMD/LQA content, not an address
                break;
            case PreambleType::CMD:
                finalize_current();
                ctx = Ctx::NONE;  // DATA after CMD = message content, not address
                break;
            default:
                break;
        }
    }
    finalize_current();
}

void MessageAssembler::extract_data(const std::vector<ALEWord>& words, ALEMessage& msg) {
    // Collect orderwire message content: CMD starts the message section;
    // subsequent DATA/REP words continue it. DATA/REP before any CMD are
    // address extensions and must not be included.
    bool collecting = false;
    for (const auto& word : words) {
        if (word.type == PreambleType::CMD) {
            collecting = true;
            std::string data(word.address, 3);
            data.erase(data.find_last_not_of(' ') + 1);
            if (!data.empty()) msg.data_content.push_back(data);
        } else if ((word.type == PreambleType::DATA || word.type == PreambleType::REP)
                   && collecting) {
            std::string data(word.address, 3);
            data.erase(data.find_last_not_of(' ') + 1);
            if (!data.empty()) msg.data_content.push_back(data);
        }
    }
}

bool MessageAssembler::check_timeout(uint32_t current_time_ms) {
    if (current_time_ms < last_word_time_ms) {
        return false;  // Time wrapped, ignore
    }
    
    return (current_time_ms - last_word_time_ms) > word_timeout_ms;
}

// ============================================================================
// CallTypeDetector Implementation
// ============================================================================

CallType CallTypeDetector::detect(const std::vector<ALEWord>& words) {
    if (words.empty())             return CallType::UNKNOWN;
    if (is_sounding(words))        return CallType::SOUNDING;
    if (is_amd(words))             return CallType::AMD;
    if (is_individual_call(words)) return CallType::INDIVIDUAL;
    // NET requires address-book context to distinguish from INDIVIDUAL; see is_net_call().
    return CallType::UNKNOWN;
}

bool CallTypeDetector::is_individual_call(const std::vector<ALEWord>& words) {
    bool has_to  = false;
    bool has_tis = false;
    bool has_from = false;
    bool has_twas = false;

    for (const auto& word : words) {
        if (word.type == PreambleType::TO)   has_to   = true;
        if (word.type == PreambleType::TIS)  has_tis  = true;
        if (word.type == PreambleType::FROM) has_from = true;
        if (word.type == PreambleType::TWAS) has_twas = true;
    }

    // TO + TIS (or TO + FROM): individual call, response, or ACK — wire-identical.
    return has_to && (has_tis || has_from) && !has_twas;
}

bool CallTypeDetector::is_net_call(const std::vector<ALEWord>& words) {
    // Net calls and individual calls are structurally identical at the frame level
    // (TO[addr] × N + TIS[self] in both cases).  Distinguishing them requires
    // comparing the TO address against a net-address registry — context not
    // available here.  The previous TWAS heuristic was wrong: TWAS also appears
    // in rejection responses and link-termination frames, not just net conclusions.
    (void)words;
    return false;
}

bool CallTypeDetector::is_sounding(const std::vector<ALEWord>& words) {
    bool has_tis  = false;
    bool has_to   = false;
    bool has_from = false;
    bool has_twas = false;

    for (const auto& word : words) {
        if (word.type == PreambleType::TIS)  has_tis  = true;
        if (word.type == PreambleType::TO)   has_to   = true;
        if (word.type == PreambleType::FROM) has_from = true;
        if (word.type == PreambleType::TWAS) has_twas = true;
    }

    // Sounding = self-ID only: TIS present with no destination (no TO/TWAS/FROM).
    return has_tis && !has_to && !has_from && !has_twas;
}

bool CallTypeDetector::is_amd(const std::vector<ALEWord>& words) {
    bool has_to   = false;
    bool has_tis  = false;
    bool has_from = false;
    bool has_cmd  = false;

    for (const auto& word : words) {
        if (word.type == PreambleType::TO)   has_to   = true;
        if (word.type == PreambleType::TIS)  has_tis  = true;
        if (word.type == PreambleType::FROM) has_from = true;
        if (word.type == PreambleType::CMD)  has_cmd  = true;
    }

    // AMD = addressed call (TO) with source (TIS or FROM) and a CMD orderwire word.
    // Individual calls with long callsigns also carry DATA words (address continuation)
    // but never CMD — using has_cmd prevents misclassifying them as AMD.
    return has_to && (has_tis || has_from) && has_cmd;
}

const char* CallTypeDetector::call_type_name(CallType type) {
    uint8_t index = static_cast<uint8_t>(type);
    if (index > 8) index = 8;  // UNKNOWN
    return CALL_TYPE_NAMES[index];
}

} // namespace ale
