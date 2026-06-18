/**
 * \file Protocol/ale_orderwire_protocols.h
 * \brief ALE orderwire message protocols (MIL-STD-188-141B A.5.7)
 *
 * Encoding for Basic Orderwire message types transmitted in the
 * Message section of an ALE calling frame (CallingPhase::MESSAGE)
 * or in the ORDERWIRE sub-phase of an established link (LinkedPhase::ORDERWIRE).
 *
 *   AMD — Automatic Message Display (A.5.7.2)
 */

#pragma once
#include "Word/ale_word.h"
#include <string>
#include <vector>

namespace ale {

/**
 * \enum LinkedPhase
 * Sub-states within the LINKED state (A.5.5.3.5).
 *
 *   IDLE       — Link established, no active orderwire session.
 *   ORDERWIRE  — Orderwire message exchange in progress (A.5.7).
 *                AMD/DTM/DBM may only be initiated from this sub-state
 *                (or from CallingPhase::MESSAGE for in-call messages).
 */
enum class LinkedPhase {
    IDLE,       ///< No active orderwire session
    ORDERWIRE,  ///< Orderwire exchange active (A.5.7)
};

/**
 * Encode an AMD text message into ALE words (A.5.7.2.2).
 *
 * Frame layout:
 *   Word 0 : CMD AMD  — preamble=CMD, Expanded-64 payload (first 3 chars)
 *   Word 1 : DATA     — chars 4–6
 *   Word 2 : REP      — chars 7–9
 *   Word 3 : DATA     — chars 10–12  (alternating DATA/REP)
 *   …
 *
 * Rules:
 * - Characters outside Expanded-64 (0x20–0x5F) are replaced with '?'.
 * - The last triplet is padded with SP (0x20) if 1 or 2 chars are missing
 *   (A.5.7.2.2 — no operator intervention required).
 * - Maximum 30 words / 90 characters (A.5.7.2.3); excess is silently truncated.
 * - Returns an empty vector if \p text is empty.
 *
 * This function is the single authoritative AMD encoder for the stack.
 * ALEStateMachine::enqueue_call_sequence_() delegates to it exclusively so
 * that AMD words are guaranteed to be placed only in message_seq_ — never
 * in the scanning, leading, or conclusion sections.
 *
 * \param text  ASCII text to encode (up to 90 characters).
 * \return      Ordered list of ALEWords for the Message section.
 */
std::vector<ALEWord> encode_amd(const std::string& text);

} // namespace ale
