/**
 * @file events.h
 * @brief Event callback interface
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pal {

/**
 * @brief Event types
 */
enum class EventType {
    // Radio events
    RADIO_READY,
    RADIO_ERROR,
    PTT_ON,
    PTT_OFF,
    CHANNEL_CHANGED,
    
    // Audio events
    AUDIO_STARTED,
    AUDIO_STOPPED,
    AUDIO_ERROR,
    AUDIO_OVERRUN,
    AUDIO_UNDERRUN,
    
    // ALE events
    ALE_CALL_RECEIVED,
    ALE_CALL_SENT,
    ALE_LINK_ESTABLISHED,
    ALE_LINK_TERMINATED,
    ALE_SOUNDING,
    ALE_LQA_UPDATE,
    ALE_STATUS,           ///< Human-readable status/log line
    ALE_IDLE_WARNING,     ///< Link idle-timeout warning; code = remaining_sec
    ALE_SOUNDING_WARNING, ///< Pre-sounding countdown; data = SoundingWarningData*
    ALE_AMD_RECEIVED,     ///< AMD orderwire decoded; data = AmdData*
    ALE_WORD_DECODED,     ///< Every decoded RX ALE word; data = WordData*
    ALE_WORD_TX,          ///< Every transmitted ALE word; data = WordData*
    ALE_FRAME_DECODED,    ///< Complete assembled ALE frame; data = FrameData*
    ALE_TEST_CHANNEL,     ///< Test-channel sweep progress/result; data = TestChannelData*

    // Data events
    DATA_RECEIVED,
    DATA_SENT,
    DATA_FAILED,
    
    // System events
    SYSTEM_ERROR,
    SYSTEM_WARNING
};

/**
 * @brief Event data structure
 */
struct Event {
    EventType type;
    uint64_t timestamp_ms;
    std::string source;      ///< Module that generated event
    std::string message;     ///< Human-readable description
    int32_t code;            ///< Event-specific code
    void* data;              ///< Optional event-specific data
    size_t data_size;
};

/**
 * @brief Event callback type
 */
using EventCallback = std::function<void(const Event& event)>;

/**
 * @brief Event handler interface
 */
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    
    /**
     * @brief Register callback for specific event type
     */
    virtual void on(EventType type, EventCallback callback) = 0;
    
    /**
     * @brief Register callback for all events
     */
    virtual void on_any(EventCallback callback) = 0;
    
    /**
     * @brief Emit an event
     */
    virtual void emit(const Event& event) = 0;
    
    /**
     * @brief Emit a simple event
     */
    virtual void emit(EventType type, const std::string& message = "") = 0;
};

/**
 * @brief Factory function
 */
std::unique_ptr<IEventHandler> create_event_handler();

/**
 * @brief Global event handler
 */
IEventHandler* get_event_handler();
void set_event_handler(std::unique_ptr<IEventHandler> handler);

} // namespace pal
