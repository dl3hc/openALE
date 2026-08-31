/**
 * \file frame_catalog.h
 * \brief openALE Framing Standard (OFS) frame-type catalog — docs/FRAMING_STANDARD.md §6.
 *
 * Every frame openALE transmits or interprets is one of these types. TX frames
 * are created only through the ALESequenceBuilder catalog constructors and are
 * tagged with their type (FR-09); RX decisions are gated by the §8 context
 * matrix on the same types (FR-07). Payload protocols (AMD/DTM/DBM/LQA,
 * §6.1 P-1..P-4) are NOT frame types — they ride the message section of a
 * carrying frame.
 *
 * F-09 (in-link keep-alive, TO ?@? + TIS member) is in the catalog for
 * completeness but not constructed by openALE today; the enum value exists so
 * the RX side can name the frame type when one is received.
 */

#pragma once

namespace ale {

enum class FrameType {
    UNTAGGED,      ///< Section / fragment — not a complete frame (no catalog row)
    F_CALL,        ///< F-01/F-02: individual/net/group call (calling cycle + conclusion)
    F_RESPONSE,    ///< F-03: callee → caller, TO×2 caller + TIS/TWAS self
    F_ACK,         ///< F-04: caller → callee, TO×2 peer + TIS/TWAS self
    F_TERMINATION, ///< F-05: either peer, TO×2 peer + TWAS self (A.5.5.3.5)
    F_SOUND,       ///< F-06: conclusion-only frame, repeated for Tsrs (A.5.3)
    F_ALLCALL,     ///< F-07: TO @?@ … + TIS/TWAS self one-way broadcast (A.5.5.4.4)
    F_ORDERWIRE,   ///< F-08: linked in-frame exchange, payload + TIS/TWAS self
    F_INLINK,      ///< F-09: keep-alive, TO ?@? + TIS member (A.5.2.4.12) — RX-named only
    F_LQA,         ///< F-10: LQA/noise CMD message content (P-4 carrier fragment)
};

/// Catalog name for logging/tests ("F_TERMINATION", …).
inline const char* frame_type_name(FrameType t) {
    switch (t) {
    case FrameType::UNTAGGED:      return "UNTAGGED";
    case FrameType::F_CALL:        return "F_CALL";
    case FrameType::F_RESPONSE:    return "F_RESPONSE";
    case FrameType::F_ACK:         return "F_ACK";
    case FrameType::F_TERMINATION: return "F_TERMINATION";
    case FrameType::F_SOUND:       return "F_SOUND";
    case FrameType::F_ALLCALL:     return "F_ALLCALL";
    case FrameType::F_ORDERWIRE:   return "F_ORDERWIRE";
    case FrameType::F_INLINK:      return "F_INLINK";
    case FrameType::F_LQA:         return "F_LQA";
    }
    return "??";
}

} // namespace ale