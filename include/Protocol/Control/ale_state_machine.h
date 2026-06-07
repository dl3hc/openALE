/**
 * \file ale_state_machine.h
 * \brief ALE link state machine
 *
 * Implements MIL-STD-188-141B Appendix A link establishment procedures:
 *  - Scanning for incoming calls
 *  - Initiating outbound calls (Individual / Net)
 *  - Full three-way handshake: Call → Response → ACK (REQ-LINK-008)
 *  - Listen-Before-Transmit (REQ-LINK-017 AC-1)
 *  - Tune delay before first TX (REQ-LINK-017 AC-2)
 *  - Multi-channel retry on no-reply (REQ-LINK-017 AC-8)
 *  - Emergency manual override (REQ-LINK-007)
 *  - Sounding/LQA operations
 *
 * Specification: MIL-STD-188-141B Appendix A
 */

#pragma once

#include "Protocol/Message/ale_message.h"
#include "Word/ale_word.h"
#include "Word/address_encoder.h"
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
 * \enum OperatorEvent
 * Asynchronous events reported to the operator via operator_callback.
 */
enum class OperatorEvent {
    CALL_REJECTED,     ///< Called station sent TWAS (rejection) — AC-LINK-019-10
    NO_CHANNELS_LEFT,  ///< All calling channels exhausted with no reply
    LINK_ESTABLISHED,  ///< Three-way handshake complete — link is up
    EMERGENCY_ACTIVE,  ///< emergency_manual_control() was invoked
};

/**
 * \enum CallingPhase
 * Sub-states within CALLING per MIL-STD-188-141B A.5.5.3.1 / Figure A-29
 *
 * Complete SAM-side calling sequence:
 *
 *   LBT               listen Twt (784 ms ALE-only) before TX — AC-LINK-017-1
 *   TUNING            tune delay Tt (1045 ms blind) — AC-LINK-017-2
 *   SCANNING_CALL     TO first-word × (C × 2 Trw)  — AC-LINK-017-5/6
 *   LEADING_CALL      full TO address × 2 (Tlc)    — AC-LINK-017-7
 *   MESSAGE           optional AMD/DTM/DBM orderwire (stub — AC-LINK-009-3)
 *   CONCLUSION        TIS SAM — frame terminator
 *   LISTENING         Twr/Twrt RX window for JOE's response
 *                       ├─ "TO SAM" detected → arm Tlww (AC-LINK-019-6)
 *                       └─ "TIS JOE" + Tlww → SENDING_ACK
 *   SENDING_ACK       TO JOE × 2 + TIS SAM — third handshake frame (REQ-LINK-008)
 *                       └─ complete → LINKED (HANDSHAKE_COMPLETE)
 *   NET_CALL_STUB     TODO: net call protocol per A.5.5.x
 */
enum class CallingPhase {
    LBT,            ///< Listen-Before-Transmit: wait Twt before first TX
    TUNING,         ///< Channel tune delay Tt
    SCANNING_CALL,  ///< Tsc: TO first word only per A.5.2.5.1, C×2 slots
    LEADING_CALL,   ///< Tlc: full TO address, sent twice (2×Tc)
    MESSAGE,        ///< Optional AMD/DTM orderwire (stub, see AC-LINK-009-3)
    CONCLUSION,     ///< TIS SAM — frame terminator, invites response
    LISTENING,      ///< Twr/Twrt: RX window, waiting for called station response
    SENDING_ACK,    ///< Third handshake frame: TO JOE × 2 + TIS SAM (REQ-LINK-008)
    NET_CALL_STUB   ///< TODO: net call protocol per A.5.5.x
};

/**
 * \enum HandshakePhase
 * Sub-states within HANDSHAKE (called station, JOE side) per A.5.5.3.2–4.
 *
 *   WAIT_CYCLE_END    Twce: listen for calling station's conclusion (TIS SAM)
 *                       ├─ TIS received → arm Tlww for last word wait
 *                       └─ Tlww elapsed → CHANNEL_CHECK
 *   CHANNEL_CHECK     2×Trw LBT before transmitting response — AC-LINK-019-1, A.5.5.3.3
 *                       ├─ any RX activity → channel busy → abort
 *                       └─ 2×Trw clear → SENDING_RESPONSE
 *   SENDING_RESPONSE  TO caller × 2 + TIS self — response frame (Figure A-30)
 *                       └─ all words sent → WAIT_ACK
 *   WAIT_ACK          Twr: wait for ACK frame from calling station (Figure A-31)
 *                       ├─ "TIS SAM" + Tlww → HANDSHAKE_COMPLETE → LINKED
 *                       └─ "TWAS SAM" → abort → IDLE
 */
enum class HandshakePhase {
    WAIT_CYCLE_END,    ///< Twce: listen for SAM's conclusion (A.5.5.3.2)
    CHANNEL_CHECK,     ///< 2×Trw LBT before response TX (AC-LINK-019-1, A.5.5.3.3)
    SENDING_RESPONSE,  ///< TO caller × 2 + TIS self — Figure A-30
    WAIT_ACK,          ///< Twr: wait for SAM's ACK — Figure A-31
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
    /** Pending orderwire message for the MESSAGE phase (AMD, stub). */
    struct PendingMessage {
        enum class Type { NONE, AMD } type;
        std::string content;
        PendingMessage() : type(Type::NONE) {}
    };

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

    /**
     * Set the ordered list of channels SAM will try when calling.
     * On LISTENING timeout on one channel, SAM hops to the next.
     * If empty, SAM stays on the current channel (single-channel operation).
     * Must be called before initiate_call(); calling_channel_index resets to 0.
     */
    void set_calling_channels(const std::vector<Channel>& channels) {
        calling_channels = channels;
    }

    /**
     * Queue an orderwire message to be sent in the MESSAGE phase of the next call.
     * Calling set_pending_message with type=NONE removes any queued message.
     */
    void set_pending_message(const PendingMessage& msg) { pending_message = msg; }
    void clear_pending_message()                        { pending_message = PendingMessage(); }

    /**
     * Emergency manual override per REQ-LINK-007 / A.5.5.1.
     * Immediately aborts any ongoing ALE operation and transitions to IDLE,
     * allowing the operator to take direct control of the radio.
     * Fires operator_callback(OperatorEvent::EMERGENCY_ACTIVE).
     * The "always listening" (scanning/sounding) capability can be restored
     * by the operator via start_scan() / send_sounding() afterwards.
     */
    void emergency_manual_control();

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
    uint32_t       get_call_cycle_count()     const { return call_cycle_count; }
    uint32_t       get_call_cycles_in_phase() const { return call_cycles_in_phase; }
    CallingPhase   get_calling_phase()        const { return calling_phase; }
    HandshakePhase get_handshake_phase()      const { return handshake_phase; }
    uint32_t       get_words_pending()        const { return words_pending; }
    bool           is_emergency_active()      const { return emergency_active; }
    const std::string& get_joe_address()      const { return joe_address; }
    const std::string& get_caller_address()   const { return caller_address; }
    bool           is_hs_conclusion_rcvd()    const { return hs_conclusion_rcvd; }

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
     */
    void set_rx_enabled_callback(std::function<void(bool)> callback) {
        rx_enabled_callback = callback;
    }

    /**
     * Called for operator-level events (REQ-LINK-007, REQ-LINK-008).
     * See OperatorEvent for event types.
     */
    void set_operator_callback(std::function<void(OperatorEvent)> callback) {
        operator_callback = callback;
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
    uint32_t     first_call_tx_ms;       ///< Trw-grid anchor (DD-006): set after LBT+Tune, never modified
    uint32_t     call_cycle_count;       ///< Total Trw-slots completed (incremented in on_word_complete() only)
    uint32_t     call_cycles_in_phase;   ///< Trw-slots completed within current inner phase (reset on transition)
    uint32_t     words_pending;          ///< Words enqueued but not yet acked by on_word_complete()
    uint32_t     listening_start_ms;     ///< Timestamp when LISTENING phase began (for Twr timeout)

    // ── Pre-computed TX word sequences ───────────────────────────────────────
    // Computed once in initiate_call() / initiate_net_call(); the raw address
    // string is not re-processed after that point.
    //
    // SCANNING_CALL phase (A.5.2.5.1):
    //   scanning_word_ is transmitted once per Trw slot for C × 2 slots total
    //   (Tsc = target_scan_channels × 2 × Trw).  The word always carries only
    //   the first 3 chars of the address with preamble TO — regardless of the
    //   full address length.  DATA / REP extension words are strictly forbidden
    //   in the scanning section; they appear only in the leading call.
    //
    // LEADING_CALL phase (A.5.5.3.1):
    //   leading_words_ is transmitted twice (Tlc = 2 × Tc).  It contains the
    //   complete address: 1 word for ≤3-char addresses, up to 5 words for
    //   15-char addresses (TO + DATA/REP alternation per A.5.2.3.2.1).
    //
    // CONCLUSION phase (A.5.2.3.2.2):
    //   conclusion_words_ is transmitted once.  Same encoding rule as
    //   leading_words_ but with preamble TIS instead of TO.
    ALEWord              scanning_word_;     ///< TO first-word only — one copy per scanning slot
    std::vector<ALEWord> leading_words_;     ///< Full TO address — sent twice in LEADING_CALL
    std::vector<ALEWord> conclusion_words_;  ///< Full TIS address — sent once in CONCLUSION

    // ── LBT and tuning (AC-LINK-017-1/2) ─────────────────────────────────
    uint32_t     lbt_start_ms;           ///< When LBT phase started (for Twt timeout)
    uint32_t     tune_start_ms;          ///< When TUNING phase started (for Tt timeout)

    // ── Multi-channel calling (AC-LINK-017-8) ────────────────────────────
    uint32_t             calling_channel_index;  ///< Current channel index within calling_channels
    std::vector<Channel> calling_channels;        ///< Ordered list of channels to try (may be empty)

    // ── Response tracking: LISTENING → SENDING_ACK ───────────────────────
    bool         response_to_detected;         ///< true once "TO SAM" received from JOE
    uint32_t     response_rx_start_ms;         ///< When "TO SAM" was first seen (for AC-LINK-019-8)
    uint32_t     tlww_start_ms;                ///< When "TIS JOE" conclusion was received; 0 = not yet
    bool         collecting_remote_conclusion; ///< TIS received; still collecting DATA/REP (Fix 5)
    std::string  joe_address;                  ///< Identity of responding station (from TIS word)

    // ── Handshake sub-state (MIL-STD A.5.5.3.2–4, JOE side) ─────────────
    HandshakePhase handshake_phase;       ///< Current sub-phase within HANDSHAKE
    uint32_t       twce_ms;              ///< Twce = 2 × own Ts, computed on HANDSHAKE entry
    uint32_t       twce_start_ms;        ///< Timestamp when HANDSHAKE was entered
    uint32_t       hs_tlww_start_ms;     ///< Tlww in WAIT_CYCLE_END and WAIT_ACK; 0 = not yet
    bool           hs_conclusion_rcvd;   ///< TIS [caller] received in WAIT_CYCLE_END
    std::string    caller_address;       ///< Calling station identity (from TIS word)
    uint32_t       hs_words_in_phase;    ///< TX word counter for SENDING_RESPONSE
    uint32_t       hs_ack_start_ms;      ///< Timestamp when WAIT_ACK started
    bool           hs_ack_tis_rcvd;      ///< TIS [caller] received in WAIT_ACK
    uint8_t        contiguous_errors;    ///< Contiguous FEC-fail count (A.5.5.3.2, Fix 6)
    uint32_t       hs_lbt_start_ms;      ///< When CHANNEL_CHECK LBT started; 0 = not active
    uint32_t       hs_message_start_ms;  ///< When first message word (DATA/REP before TIS) arrived; 0 = none

    // ── Emergency control (REQ-LINK-007) ─────────────────────────────────
    bool emergency_active;

    // ── Pending orderwire message (MESSAGE phase, stub) ───────────────────
    PendingMessage pending_message;

    // ── Target scan channels ──────────────────────────────────────────────
    uint32_t target_scan_channels;       ///< Assumed scan channels of target (for Tsc)

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
    std::function<void(OperatorEvent)>      operator_callback;

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

    /**
     * Called on LISTENING timeout or abort conditions AC-LINK-019-6/8/9.
     * Hops to the next entry in calling_channels and restarts from LBT.
     * If no more channels remain, notifies operator and transitions to IDLE.
     */
    void try_next_calling_channel();

    /**
     * Compute the maximum allowed duration for the entire CALLING state.
     * Accounts for all channels: n × (Twt + Tt + Tsc + Tlc + Twr).
     * Replaces the fixed Twa_ms safety net (AC-LINK-009-1).
     */
    uint32_t compute_calling_timeout_ms() const;

    // ── Word builders per calling phase ──────────────────────────────────
    //
    // Each builder transmits words from the pre-computed vectors filled by
    // initiate_call().  None of them re-processes address strings.
    //
    // For ACK and response, addresses are known only during the receive path,
    // so they are encoded on-the-fly via AddressEncoder::encode() at that point.

    /**
     * SCANNING_CALL: transmit scanning_word_ once for the current slot.
     * Called once per Trw slot; on_word_complete() counts slots and transitions
     * to LEADING_CALL after C × 2 slots (A.5.2.5.1).
     */
    void build_scanning_word();

    /**
     * LEADING_CALL: transmit the full leading_words_ sequence once.
     * Called twice by the slot counter (Tlc = 2 × Tc) per A.5.5.3.1.
     */
    void build_leading_call_word();

    /** CONCLUSION: transmit conclusion_words_ once (A.5.2.3.2.2). */
    void build_conclusion_words();

    /**
     * SENDING_ACK: third handshake frame per REQ-LINK-008 / A.5.5.3.4.
     * Frame: TO [joe_address] × 2 + TIS [own addr]  (Figure A-31).
     * Encoded via AddressEncoder::encode() at send time (joe_address is set
     * during the LISTENING phase, not at initiate_call() time).
     */
    void build_ack_words();

    /**
     * SENDING_RESPONSE: JOE's response frame per A.5.5.3.3 / Figure A-30.
     * Frame: TO [caller_address] × 2 + TIS [own addr].
     * Encoded via AddressEncoder::encode() at send time (caller_address is set
     * during WAIT_CYCLE_END, not at initiate_call() time).
     */
    void build_response_words();

    /**
     * Transmit one word: stamps the current timestamp and fires transmit_callback.
     * This is the single exit point for all TX words; all build_* functions
     * route through here.
     */
    void transmit_word(const ALEWord& word);

    /**
     * Transmit all words in a pre-encoded sequence.
     * Convenience wrapper used by build_* functions to iterate vectors from
     * AddressEncoder without repeating the loop at every call site.
     */
    void transmit_words(const std::vector<ALEWord>& words);
};

} // namespace ale
