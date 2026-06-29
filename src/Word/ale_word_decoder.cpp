/**
 * \file ale_word_decoder.cpp
 */

#include "Word/ale_word_decoder.h"
#include "Word/address_encoder.h"

namespace ale {

// AllCall address recognition (A.5.5.4.4).  The AllCall address is the exclusive
// member of the scanning and leading call sections and never appears elsewhere.
// After trim_ale_address() strips trailing '@' padding (A.5.2.4.3):
//   "@?@" → "@"  is wrong — actually "@?" (trailing @ stripped, '?' stops the trim)
//   Global AllCall   "@?@" → trimmed "@?"  — pertinent to every station.
//   Selective AllCall "@A@" → trimmed "@A" — pertinent iff our self address ends
//                                            in the selector char 'A'.
//   AnyCall (A.5.5.4.5) "@@?" / "@@A" → trimmed starts with "@@" — NOT AllCall;
//   left as NONE so the SM ignores it (AnyCall handling is out of scope here).
// A leading '@' only occurs for these wildcard broadcast patterns — a real
// (short) address is right-padded with '@' and trims to its real characters, so
// it cannot start with '@'.  Returns false for the empty (all-padding) address.
static bool is_allcall_address(const std::string& addr, const std::string& self)
{
    if (addr.size() < 2 || addr[0] != '@' || addr[1] == '@') return false;
    if (addr[1] == '?') return true;                       // global AllCall
    return !self.empty() && self.back() == addr[1];        // selective AllCall
}

WordEvent ALEWordDecoder::decode(const ALEWord& word,
                                 const WordDecodeContext& ctx) const
{
    WordEvent ev;
    const std::string addr = trim_ale_address(word.address);

    // Jedes gültige Wort während LBT = Kanal belegt.
    if (ctx.lbt_active) {
        ev.type = WordEvent::Type::CHANNEL_BUSY;
        return ev;
    }

    // DATA/REP nach TIS → mehrteilige Adresse des Gegenübers.
    if (ctx.collecting_conclusion
        && (word.type == PreambleType::DATA || word.type == PreambleType::REP)) {
        ev.type    = WordEvent::Type::DATA_EXTENSION;
        ev.address = addr;
        return ev;
    }

    // AllCall (A.5.5.4.4): TO an die AllCall-Wildcard-Adresse.  Einweg-Broadcast —
    // der Empfänger antwortet NICHT, er lauscht nur auf die Conclusion und linkt
    // bei TIS / nimmt den Scan bei TWAS wieder auf.  Hier nur erkennen und als
    // ALLCALL an die SM durchreichen (selective-Pertinenz braucht self_address);
    // die SM kümmert sich um das Einfrieren und die Conclusion-Behandlung.
    if (word.type == PreambleType::TO && is_allcall_address(addr, ctx.self_address)) {
        ev.type    = WordEvent::Type::ALLCALL;
        ev.address = addr;
        return ev;
    }

    // TO oder TWAS an uns → Ruf an eigene Adresse erkannt.
    // Per A.5.2.5.1 trägt das Scanning-TO-Word nur die ersten ≤3 Zeichen der
    // Zieladresse; self_address kann länger sein → Präfix-Vergleich nötig.
    if ((word.type == PreambleType::TO || word.type == PreambleType::TWAS)
        && !addr.empty()
        && ctx.self_address.size() >= addr.size()
        && ctx.self_address.compare(0, addr.size(), addr) == 0) {
        ev.type    = WordEvent::Type::TO_SELF;
        ev.address = addr;
        return ev;
    }

    // TIS → Conclusion-Beginn; bei gesetztem expected_caller Adresse prüfen.
    if (word.type == PreambleType::TIS) {
        if (ctx.expected_caller.empty() || addr == ctx.expected_caller) {
            ev.type    = WordEvent::Type::TIS_CALLER;
            ev.address = addr;
        }
        return ev;
    }

    // TWAS nicht an uns → Rufablehnung.
    if (word.type == PreambleType::TWAS) {
        ev.type    = WordEvent::Type::TWAS_REJECTION;
        ev.address = addr;
        return ev;
    }

    return ev; // NONE
}

} // namespace ale
