/**
 * \file ale_state_machine.h
 * \brief ALE link state machine
 *
 * Implements MIL-STD-188-141B Appendix A link establishment procedures:
 *  - Scanning for incoming calls
 *  - Initiating outbound calls
 *  - Link handshake and establishment
 *  - Sounding/LQA operations
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Protocol/Message/ale_message.h"
#include "Word/ale_word.h"
#include "Stores/address_book.h"
#include "Protocol/Control/ale_timing.h"
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace ale {
enum class ALEState {
    IDLE,
    SCANNING,
    CALLING,
    HANDSHAKE,
    LINKED,
    SOUNDING,
    ERROR
};

/**
 * \enum ALEEvent
 * Events that trigger state transitions
 */
enum class ALEEvent {
    START_SCAN,
    STOP_SCAN,
    CALL_REQUEST,
    CALL_DETECTED,
    HANDSHAKE_COMPLETE,
    LINK_TIMEOUT,
    LINK_TERMINATED,
    SOUNDING_REQUEST,
    SOUNDING_COMPLETE,
    ERROR_OCCURRED
};

/**
 * \enum CallingPhase
 * Sub-states within CALLING per MIL-STD-188-141B A.5.5.3.1 / Figure A-29
 *
 * Individual call sequence (single channel):
 *
 *   SCANNING_CALL         LEADING_CALL      CONCLUSION    LISTENING
 *   ┌───────────────────┐ ┌──────────────┐ ┌──────────┐ ┌─────────┐
 *   │TO JOE … TO JOE    │ │TO JOE TO JOE│ │TIS SAM  │ │ (Twr)  │
 *   │ (2×Trw per ch.)   │ │  (Tlc=2×Tc) │ │         │ │        │
 *   └───────────────────┘ └──────────────┘ └──────────┘ └─────────┘
 *         Tsc                   Tlc            Tc          Twr
 *
 *   Response arrives in LISTENING via process_received_word() → HANDSHAKE_COMPLETE
 *   No response within Twr → LINK_TIMEOUT → IDLE (single ch.) / next ch. (TODO)
 *
 * Net call: NET_CALL_STUB — not yet implemented.
 */
enum class CallingPhase {
    SCANNING_CALL,  ///< Tsc: TO first word only per A.5.2.5.1, 1×Trw per slot, C×2 slots
    LEADING_CALL,   ///< Tlc: full TO address, sent twice (2×Tc)
    CONCLUSION,     ///< TIS SAM — frame terminator, invites response
    LISTENING,      ///< Twr: RX window, waiting for called station response
    NET_CALL_STUB   ///< TODO: net call protocol per A.5.5.x
};

/**
 * \struct Channel
 */
struct Channel {
    uint32_t frequency_hz;
    std::string mode;
    float lqa_score;
    uint32_t last_scan_time_ms;
    uint32_t call_count;

    Channel() : frequency_hz(0), mode("USB"), lqa_score(0.0f),
                last_scan_time_ms(0), call_count(0) {}

    Channel(uint32_t freq, const std::string& m = "USB")
        : frequency_hz(freq), mode(m), lqa_score(0.0f),
          last_scan_time_ms(0), call_count(0) {}
};

/**
 * \struct ScanConfig
 */
struct ScanConfig {
    std::vector<Channel> scan_list;
    uint32_t dwell_time_ms;
    uint32_t channel_index;
    bool enabled;

    ScanConfig() : dwell_time_ms(200), channel_index(0), enabled(false) {}
};

/**
 * \struct LinkQuality
 */
struct LinkQuality {
    float snr_db;
    float ber;
    uint32_t fec_errors;
    uint32_t total_words;
    uint32_t timestamp_ms;

    LinkQuality() : snr_db(0.0f), ber(0.0f), fec_errors(0),
                    total_words(0), timestamp_ms(0) {}
};

/**
 * \class ALEStateMachine
 */
class ALEStateMachine {
public:
    ALEStateMachine();

    bool     process_event(ALEEvent event);
    void     update(uint32_t current_time_ms);
    ALEState get_state() const { return current_state; }

    static const char* state_name(ALEState state);
    static const char* event_name(ALEEvent event);

    void configure_scan(const ScanConfig& config);
    void add_scan_channel(const Channel& channel);
    void set_self_address(const std::string& address);
    const Channel* get_current_channel() const;

    bool initiate_call(const std::string& to_address);
    bool initiate_net_call(const std::string& net_address);
    bool respond_to_call();
    bool send_sounding();

    /**
     * Set assumed number of scan channels of the target station.
     * Used to compute Tsc = target_scan_channels × 2 × Trw.
     * Default = 1 (single channel, minimum scanning call).
     * Set to 0 to skip scanning call entirely (target known on fixed channel).
     */
    void set_target_scan_channels(uint32_t n) { target_scan_channels = n; }

    void process_received_word(const ALEWord& word);
    void update_link_quality(const LinkQuality& lq);
    const Channel* select_best_channel() const;

    /**
     * Called by ALE2GModem (via done_cb_) after all 3 copies of one logical
     * word have been sent.  Increments call_cycle_count and call_cycles_in_phase
     * by 1 and handles inner-state transitions (DD-009, DD-013).
     * Must be wired up by the integration layer:
     *   modem.set_word_done_callback([&sm]{ sm.on_word_complete(); });
     */
    void on_word_complete();

    // ── Test / inspection getters ─────────────────────────────────────────
    uint32_t     get_call_cycle_count()     const { return call_cycle_count; }
    uint32_t     get_call_cycles_in_phase() const { return call_cycles_in_phase; }
    CallingPhase get_calling_phase()        const { return calling_phase; }
    uint32_t     get_words_pending()        const { return words_pending; }

    // ── Callbacks ────────────────────────────────────────────────────────

    /** Called on every state transition: (from, to) */
    void set_state_callback(std::function<void(ALEState, ALEState)> callback) {
        state_callback = callback;
    }

    /** Called when SM needs to transmit a word */
    void set_transmit_callback(std::function<void(const ALEWord&)> callback) {
        transmit_callback = callback;
    }

    /** Called on channel change */
    void set_channel_callback(std::function<void(const Channel&)> callback) {
        channel_callback = callback;
    }

    /**
     * Called when the RX window opens or closes during CALLING.
     * true  = RX window open  (LISTENING phase — modem should receive)
     * false = RX window closed (TX phase — modem transmits)
     * Stub until RX stack is integrated.
     */
    void set_rx_enabled_callback(std::function<void(bool)> callback) {
        rx_enabled_callback = callback;
    }

private:
    // ── State machine ─────────────────────────────────────────────────────
    ALEState current_state;
    ALEState previous_state;

    // ── Configuration ─────────────────────────────────────────────────────
    ScanConfig   scan_config;
    AddressBook  address_book;

    // ── Active link state ─────────────────────────────────────────────────
    std::string      active_call_to;
    std::string      active_call_from;
    uint32_t         link_start_time_ms;
    uint32_t         last_word_time_ms;
    MessageAssembler message_assembler;

    // ── Calling sub-state (MIL-STD A.5.5.3.1) ────────────────────────────
    CallingPhase calling_phase;          ///< Current phase within CALLING
    bool         active_call_is_net;     ///< true = net call (TWAS), false = individual (TO)
    uint32_t     first_call_tx_ms;       ///< Global Trw-grid anchor (DD-006): set once on CALLING entry, never modified
    uint32_t     call_cycle_count;       ///< Total Trw-slots completed (incremented in on_word_complete() only)
    uint32_t     call_cycles_in_phase;   ///< Trw-slots completed within current inner phase (reset on transition)
    uint32_t     words_pending;          ///< Words enqueued but not yet acked by on_word_complete()
    uint32_t     listening_start_ms;     ///< Timestamp when LISTENING phase began (for Twr timeout)
    uint32_t     target_scan_channels;   ///< Assumed scan channels of target (for Tsc)

    // ── Timing ────────────────────────────────────────────────────────────
    uint32_t state_entry_time_ms;
    uint32_t last_scan_hop_time_ms;
    uint32_t current_time_ms;

    // ── LQA ───────────────────────────────────────────────────────────────
    std::vector<LinkQuality> channel_quality;

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(ALEState, ALEState)> state_callback;
    std::function<void(const ALEWord&)>     transmit_callback;
    std::function<void(const Channel&)>     channel_callback;
    std::function<void(bool)>               rx_enabled_callback;

    // ── Internals ─────────────────────────────────────────────────────────
    void enter_state(ALEState new_state);
    void exit_state(ALEState old_state);
    bool transition_to(ALEState new_state);

    void handle_idle();
    void handle_scanning();
    void handle_calling();
    void handle_handshake();
    void handle_linked();
    void handle_sounding();

    void hop_to_next_channel();
    void set_channel(uint32_t index);

    bool check_link_timeout();
    bool check_scan_dwell_timeout();

    // ── Word builders per calling phase ──────────────────────────────────

    /**
     * SCANNING_CALL: send only the first word of the called address (TO only).
     * Per A.5.2.5.1: "The scanning call shall be composed of TO words...
     * which contain only the first word(s) of the called station address."
     * Per A.5.2.4.3: Stuffed words (DATA carrying '@'-padded chars) "should
     * appear only in the leading call (Tlc)" — explicitly excluded from Tsc.
     * One TO word per scanning slot regardless of address length.
     * Slot width = 1 × Trw.
     */
    void build_scanning_word(const std::string& to_addr);

    /**
     * LEADING_CALL: send full TO address once (one complete sequence).
     * Called twice by handle_calling() to complete Tlc = 2 × Tc.
     * Tc = words_for_address(addr) × Trw.
     * Per A.5.5.3.1: "entire called station address shall be used in
     * leading call section, and shall be sent twice."
     * Includes DATA + REP extension for addresses > 3 chars.
     */
    void build_leading_call_word(const std::string& to_addr, bool is_net);

    /**
     * CONCLUSION: send TIS with full own address.
     * Per A.5.2.3.2.2: TIS terminates the frame and invites response.
     * Includes DATA + REP extension for own addresses > 3 chars.
     */
    void build_conclusion_words();

    void transmit_word(const ALEWord& word);

    // ── Multi-word address helpers ────────────────────────────────────────

    /**
     * Split address into 3-char chunks with trailing '@' stuffing on last chunk.
     * Per A.5.2.4.3: empty positions stuffed with utility symbol '@' (0x40).
     * Maximum 5 chunks (15 chars).
     *
     * "K6KB"  → ["K6K", "B@@"]
     * "MIAMI" → ["MIA", "MI@"]
     * "W1AW"  → ["W1A", "W@@"]
     * "ABC"   → ["ABC"]           (no stuffing needed, fits exactly)
     */
    static std::vector<std::string> chunk_address(const std::string& addr);

    /**
     * Number of ALE words needed to transmit an address once.
     *  1.. 3 chars → 1 word  (TO/TIS only)
     *  4.. 6 chars → 2 words (TO/TIS + DATA)
     *  7.. 9 chars → 3 words (TO/TIS + DATA + REP)
     * 10..12 chars → 4 words (TO/TIS + DATA + REP + DATA)
     * 13..15 chars → 5 words (TO/TIS + DATA + REP + DATA + REP)
     */
    static uint32_t words_for_address(const std::string& addr);

    /**
     * Transmit all words for one complete address sequence.
     * first_type : WordType::TO, TWAS, or TIS for the first word.
     * Subsequent words alternate DATA / REP per A.5.2.3.2.1 / A.5.2.3.2.2.
     *
     * REP note: per spec, REP must not follow TIS/TWAS directly — the sequence
     * is always TO/TIS first, then DATA, then REP, alternating. The first
     * extension word is always DATA regardless of first_type.
     */
    void transmit_address_words(WordType first_type, const std::string& addr);
};

} // namespace ale