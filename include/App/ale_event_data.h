/**
 * @file ale_event_data.h
 * @brief Typed payload structs for ALEController PAL events.
 *
 * Pointers are only valid for the duration of the synchronous callback —
 * the SimpleEventHandler dispatches inline; copy any data you need to retain.
 *
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ale {

/// Payload for EventType::ALE_SOUNDING_WARNING
struct SoundingWarningData {
    const char* net;           ///< Net name
    uint32_t    remaining_sec; ///< Seconds until sounding fires
    const char* phase;         ///< "warn" | "fire" | "cancel"
};

/// Payload for EventType::ALE_AMD_RECEIVED
struct AmdData {
    const char* self_addr; ///< Local station address (receiver)
    const char* peer_addr; ///< Remote station address (sender)
    const char* text;      ///< Assembled orderwire text
};

/// Payload for EventType::ALE_WORD_DECODED and ALE_WORD_TX
struct WordData {
    const char* preamble; ///< PreambleType name string (e.g. "TIS", "TO", "TWS")
    const char* addr;     ///< 3-char ALE address (null-terminated)
    uint32_t    frame_id;
    uint8_t     votes;    ///< unanimous_votes (0-48)
    uint8_t     fec;      ///< fec_errors corrected
    uint32_t    ts_ms;    ///< timestamp_ms from ALEWord
    uint32_t    freq_hz;  ///< RX freq (word_decoded) or TX freq (word_tx) at dispatch time
};

/// Payload for EventType::ALE_FRAME_DECODED
struct FrameData {
    uint32_t    frame_id;
    const char* call_type;  ///< CallType name string
    const char* from_addr;  ///< from_address
    size_t      word_count; ///< words.size()
    uint32_t    start_ms;   ///< start_time_ms
    uint32_t    duration_ms;
    uint32_t    freq_hz;    ///< RX freq at dispatch time
    const std::vector<std::string>* to_addrs; ///< to_addresses — valid during callback only
};

/// Payload for EventType::ALE_TEST_CHANNEL
///
/// Carries per-channel progress and the final ranked summary of a Test-Channel
/// sweep (actively link to a peer on each configured channel, record LQA,
/// terminate, advance). Pointers are valid only for the synchronous callback
/// duration — copy any data you need to retain.
struct TestChannelData {
    const char* peer;         ///< Target peer address
    const char* phase;        ///< "start"|"tune"|"linked"|"failed"|"terminate"|"done"|"stop"
    const char* channel_id;   ///< Current channel id ("" on start/done/stop)
    uint32_t    freq_hz;      ///< Current channel RX frequency (0 on start/done/stop)
    uint32_t    index;        ///< 1-based current channel index (0 on start/done/stop)
    uint32_t    total;        ///< Total channels in the sweep
    int         score;        ///< LQA score for this channel, -1 = not available
    bool        linked;       ///< Did this channel establish a link?
    const char* summary;      ///< Multiline ranked result table on "done"; "" otherwise
};

} // namespace ale
