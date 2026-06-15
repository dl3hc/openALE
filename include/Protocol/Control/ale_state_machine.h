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

#include "Protocol/Control/ale_channel_types.h"
#include "Protocol/Control/ale_channel_manager.h"
#include "Protocol/Message/ale_message.h"
#include "Word/ale_word.h"
#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
#include "Stores/address_book.h"
#include "Protocol/Control/ale_timing.h"
#include "Word/ale_word_decoder.h"
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace ale { class LQAMetrics; }

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
 *
 * Net calls (A.5.5.4.2.1): SAM-seitig identischer Ablauf wie Individual Call.
 */
enum class CallingPhase {
    LBT,                ///< Listen-Before-Transmit: wait Twt before first TX
    TUNING,             ///< Channel tune delay Tt
    SCANNING_CALL,      ///< Tsc: TO first word only per A.5.2.5.1, C×2 slots
    GROUP_SCANNING_CALL,///< Tsc: THRU/REP-Paare für Star-Group-Call, rotieren bis Tsc (T-11)
    LEADING_CALL,       ///< Tlc: full TO address, sent twice (2×Tc)
    MESSAGE,            ///< Optional AMD/DTM orderwire (stub, see AC-LINK-009-3)
    CONCLUSION,         ///< TIS SAM — frame terminator, invites response
    LISTENING,          ///< Twr/Twrt: RX window, waiting for called station response
    SENDING_ACK,        ///< Third handshake frame: TO JOE × 2 + TIS SAM (REQ-LINK-008)
};

/**
 * \enum ScanningPhase
 * Sub-states within SCANNING per A.5.5.4.4/5 (T-10).
 *   HOPPING        — normales Channel-Hopping
 *   ALLCALL_PAUSE  — Channel-Hopping eingefroren; warte auf Message/Conclusion
 */
enum class ScanningPhase {
    HOPPING,
    ALLCALL_PAUSE,
};

/**
 * \enum SoundingPhase
 * Sub-states within SOUNDING per A.5.3.4.
 *   TRANSMITTING  — TIS-Frame wird gerade gesendet
 *   LISTENING     — optionales RX-Fenster für eingehenden Handshake (Trw)
 */
enum class SoundingPhase {
    TRANSMITTING,
    LISTENING,
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
    SLOT_WAIT,         ///< Tswt: warte zugewiesenen Slot vor LBT (0 ms bei Individual Call, T-09)
    CHANNEL_CHECK,     ///< 1×Trw LBT before response TX (AC-LINK-019-1, A.5.5.3.3)
    SENDING_RESPONSE,  ///< TO caller × 2 + TIS self — Figure A-30
    WAIT_ACK,          ///< Twr: wait for SAM's ACK — Figure A-31
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

    /**
     * Initiiert einen Star-Group-Call (A.5.5.4.3).
     * relay  = Relay-Adresse (THRU-Anker)
     * dest   = Ziel-Adresse (REP-Folge)
     * Max. 12 Adresswörter gesamt, max. 5 unique first words (Spec A.5.5.4.3).
     */
    bool initiate_group_call(const std::string& relay, const std::string& dest);

    bool respond_to_call();

    /**
     * Signal call rejection while in HANDSHAKE state (JOE side).
     *
     * Sets the pending_reject_ flag; the response frame built in
     * CHANNEL_CHECK → SENDING_RESPONSE will use TWAS instead of
     * TO×2 + TIS (AC-FRAME-010-1 / FEAT-FRAME-005).
     * After the TWAS frame is sent the handshake ends immediately —
     * no WAIT_ACK phase, because SAM expects no further reply.
     *
     * Must be called before SENDING_RESPONSE begins.
     * Returns false if not in HANDSHAKE state.
     */
    bool reject_call();

    bool send_sounding();

    /**
     * Terminate the current link per A.5.5.3.5:
     * Sends TO [peer] × 2 + TWAS [self] before transitioning to pre_link_state_.
     * No-op if not in LINKED state.
     * Transition happens in on_word_complete() after the frame is fully sent.
     */
    void terminate_link();

    /**
     * Set assumed number of scan channels of the target station.
     * Used to compute Tsc = target_scan_channels × 2 × Trw.
     * Default = 1 (single channel, minimum scanning call).
     * Set to 0 to skip scanning call entirely (target known on fixed channel).
     */
    void set_target_scan_channels(uint32_t n) { target_scan_channels = n; }

    /**
     * Setze Slot-Nummer und Wartezeit für One-to-Many-Protokolle (A.5.5.4.1.3).
     * slot = 0 und tswt_ms = 0 für Individual Calls (kein Warten).
     * Muss vor dem HANDSHAKE-Eintritt gesetzt werden.
     */
    void set_slot_number(uint32_t slot, uint32_t tswt_ms) {
        slot_number_ = slot;
        tswt_ms_     = tswt_ms;
    }

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

    /**
     * Signal any user-layer activity (voice PTT, data TX/RX, AMD) to the
     * ALE controller.  Resets the Twa inactivity timer (§A.5.5.3.5.2) so
     * that automatic link termination is deferred.
     *
     * No-op when not in LINKED state.  The application layer must call this
     * whenever traffic flows on the link — ALE itself only sees ALE words,
     * so it cannot observe voice or data activity on its own.
     */
    void on_link_activity();

    void process_received_word(const ALEWord& word);
    void update_link_quality(const LinkQuality& lq);
    const Channel* select_best_channel() const;

    /**
     * Called once per transmitted symbol frame to advance the calling phase.
     * Increments call_cycle_count / call_cycles_in_phase and handles inner-state
     * transitions (DD-009, DD-013).
     *
     * In RT mode: fired from ALEController via AudioDevice::arm_frame_complete().
     * In offline mode: fired from ALEController::update() after pull_symbol_frame().
     */
    void on_word_complete();

    // ── Test / inspection getters ─────────────────────────────────────────
    uint32_t       get_call_cycle_count()     const { return call_cycle_count; }
    uint32_t       get_call_cycles_in_phase() const { return call_cycles_in_phase; }
    CallingPhase   get_calling_phase()        const { return calling_phase; }
    HandshakePhase get_handshake_phase()      const { return handshake_phase; }
    SoundingPhase  get_sounding_phase()       const { return sounding_phase_; }
    uint32_t       get_words_pending()        const { return words_pending; }
    bool           is_emergency_active()      const { return emergency_active; }
    const std::string& get_to_address()      const { return to_address; }
    const std::string& get_caller_address()   const { return caller_address; }
    bool           is_hs_conclusion_rcvd()    const { return hs_conclusion_rcvd; }
    bool           is_pending_reject()        const { return pending_reject_; }

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
        channel_manager_.set_channel_callback(std::move(callback));
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

    /**
     * Attach an LQAMetrics instance for quality tracking.
     * When set, every call to update_link_quality() also feeds the metrics
     * subsystem.  Pass nullptr to detach.  Ownership stays with the caller.
     */
    void set_lqa_metrics(LQAMetrics* m) { lqa_metrics_ = m; }

    /**
     * Optional trace sink for protocol-level debug events.
     * When set, SM_TRACE() calls forward the message string to \p cb.
     * Pass nullptr to detach.
     */
    void set_trace_callback(std::function<void(const std::string&)> cb) {
        trace_cb_ = std::move(cb);
    }

private:
    // ── State machine ─────────────────────────────────────────────────────
    ALEState current_state;
    ALEState previous_state;
    ALEState pre_link_state_;   ///< Rückkehrzustand nach gescheitertem/beendetem Link (T-01)

    // ── Configuration ─────────────────────────────────────────────────────
    ALEChannelManager channel_manager_;
    AddressBook       address_book;

    // ── Active link state ─────────────────────────────────────────────────
    std::string      active_call_to;
    std::string      active_call_from;
    uint32_t         link_start_time_ms;
    uint32_t         last_word_time_ms;
    MessageAssembler message_assembler;
    bool             linked_terminating_;  ///< true = TWAS-Terminierungsframe läuft (T-07)

    // ── Calling sub-state (MIL-STD A.5.5.3.1) ────────────────────────────
    CallingPhase calling_phase;          ///< Current phase within CALLING
    bool         active_call_is_net;     ///< true = net call (TWAS), false = individual (TO)
    uint32_t     first_call_tx_ms;       ///< Informational: TX-sequence start, set after LBT+Tune
    uint32_t     call_cycle_count;       ///< Total Trw-slots completed (incremented in on_word_complete() only)
    uint32_t     call_cycles_in_phase;   ///< Trw-slots completed within current inner phase (reset on transition)
    uint32_t     words_pending;          ///< Words enqueued but not yet acked by on_word_complete()
    uint32_t     listening_start_ms;     ///< Timestamp when LISTENING phase began (for Twr timeout)

    bool     active_call_is_group;     ///< true = Star-Group-Call (T-11)

    // ── Pre-computed TX sequences ────────────────────────────────────────────
    // Built once in initiate_call() / initiate_net_call() via ALESequenceBuilder.
    // The raw address string is never re-processed after that point.
    //
    // scanning_seq_   — scan_channels×2 words (§A.5.2.5.1, first 3 chars only)
    // leading_seq_    — full TO address × 2 (Tlc = 2×Tc, §A.5.5.3.1)
    //                   ALESequenceBuilder::leading_call() pre-doubles the sequence;
    //                   on_word_complete() counts leading_seq_.size() total slots.
    // conclusion_seq_ — TIS own address, sent once (§A.5.2.3.2.2)
    ALESequence scanning_seq_;    ///< scan_channels×2 words — TO first-word repeated
    ALESequence leading_seq_;     ///< 2×wpa words — full TO address doubled (Tlc)
    ALESequence conclusion_seq_;  ///< TIS/TWAS own address — sent once
    ALESequence group_scan_seq_;  ///< THRU/REP pairs for GROUP_SCANNING_CALL (T-11)

    // ── LBT and tuning (AC-LINK-017-1/2) ─────────────────────────────────
    uint32_t     lbt_start_ms;           ///< When LBT phase started (for Twt timeout)
    uint32_t     tune_start_ms;          ///< When TUNING phase started (for Tt timeout)

    // ── Multi-channel calling (AC-LINK-017-8) ────────────────────────────
    uint32_t             calling_channel_index;  ///< Current channel index within calling_channels
    std::vector<Channel> calling_channels;        ///< Ordered list of channels to try (may be empty)

    // ── Response tracking: LISTENING → SENDING_ACK ───────────────────────
    bool         response_to_detected;         ///< true once "TO SAM" received from JOE
    uint32_t     response_rx_start_ms;         ///< When "TO SAM" was first seen (diagnostic; the AC-LINK-019-8 conclusion wait is silence-based on last_word_time_ms)
    uint32_t     tlww_start_ms;                ///< When "TIS JOE" conclusion was received; 0 = not yet
    bool         collecting_remote_conclusion; ///< TIS received; still collecting DATA/REP (Fix 5)
    std::string  to_address;                  ///< Identity of responding station (from TIS word)

    // ── Handshake sub-state (MIL-STD A.5.5.3.2–4, JOE side) ─────────────
    HandshakePhase handshake_phase;       ///< Current sub-phase within HANDSHAKE
    uint32_t       slot_number_;          ///< Slot-Nummer für SLOT_WAIT (T-09); 0 = Individual
    uint32_t       tswt_ms_;             ///< Berechnete Slot-Wartezeit in ms (T-09)
    uint32_t       slot_wait_start_ms_;  ///< Startzeit des SLOT_WAIT (T-09)
    uint32_t       twce_ms;              ///< Twce = 2 × own Ts, computed on HANDSHAKE entry
    uint32_t       twce_start_ms;        ///< Timestamp when HANDSHAKE was entered (diagnostic; the Twce abort is silence-based on last_word_time_ms)
    uint32_t       hs_tlww_start_ms;     ///< Tlww in WAIT_CYCLE_END and WAIT_ACK; 0 = not yet
    bool           hs_conclusion_rcvd;   ///< TIS [caller] received in WAIT_CYCLE_END
    std::string    caller_address;       ///< Calling station identity (from TIS word)
    uint32_t       hs_words_in_phase;    ///< TX word counter for SENDING_RESPONSE
    uint32_t       hs_ack_start_ms;      ///< Timestamp when WAIT_ACK started
    uint32_t       hs_ack_to_ms;         ///< "TO JOE" (ACK start) detected in WAIT_ACK; 0 = not yet
    bool           hs_ack_tis_rcvd;      ///< TIS [caller] received in WAIT_ACK
    uint8_t        contiguous_errors;    ///< Contiguous FEC-fail count (A.5.5.3.2, Fix 6)
    uint32_t       hs_lbt_start_ms;      ///< When CHANNEL_CHECK LBT started; 0 = not active
    uint32_t       hs_message_start_ms;  ///< When first message word (DATA/REP before TIS) arrived; 0 = none
    bool           pending_reject_;      ///< true = send TWAS rejection frame (set via reject_call())

    // ── Emergency control (REQ-LINK-007) ─────────────────────────────────
    bool emergency_active;

    // ── Pending orderwire message (MESSAGE phase, stub) ───────────────────
    PendingMessage pending_message;

    // ── Scanning sub-state (T-10) ────────────────────────────────────────
    ScanningPhase scanning_phase_;           ///< Aktuelle Phase innerhalb SCANNING
    uint32_t      allcall_pause_start_ms_;   ///< Startzeit der AllCall-Pause (T-10)

    // ── Sounding sub-state (T-08) ─────────────────────────────────────────
    SoundingPhase sounding_phase_;       ///< Aktuelle Phase innerhalb SOUNDING

    // ── Target scan channels ──────────────────────────────────────────────
    uint32_t target_scan_channels;       ///< Assumed scan channels of target (for Tsc)

    // ── Timing ────────────────────────────────────────────────────────────
    uint32_t state_entry_time_ms;
    uint32_t current_time_ms;

    // ── Word decoder ──────────────────────────────────────────────────────
    ALEWordDecoder decoder_;

    // ── LQA ───────────────────────────────────────────────────────────────
    LQAMetrics* lqa_metrics_ = nullptr;  ///< Optional; set via set_lqa_metrics()

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(ALEState, ALEState)>  state_callback;
    std::function<void(const ALEWord&)>      transmit_callback;
    std::function<void(bool)>                rx_enabled_callback;
    std::function<void(OperatorEvent)>       operator_callback;
    std::function<void(const std::string&)>  trace_cb_;

    // ── Internals ─────────────────────────────────────────────────────────
    void enter_state(ALEState new_state);
    void exit_state(ALEState old_state);
    bool transition_to(ALEState new_state);

    // ── Word-receive helpers ──────────────────────────────────────────────
    void handle_invalid_word();
    void react_scanning(const WordEvent& ev);
    void react_calling(const WordEvent& ev);
    void react_handshake(const WordEvent& ev, const ALEWord& word);

    void handle_scanning();
    void handle_calling();
    void handle_handshake();
    void handle_linked();
    void handle_sounding();

    bool check_link_timeout();

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

    // ── TX sequence transmitters ─────────────────────────────────────────
    //
    // These functions transmit words from the pre-computed sequences built by
    // initiate_call() via ALESequenceBuilder.  None re-processes address strings.
    //
    // For ACK and response, the peer address is only known during the receive
    // path, so those sequences are built on-the-fly at send time.

    /**
     * Enqueue the complete calling TX sequence back-to-back at tune-complete:
     *   scanning_seq_ (scan_channels×2 words) + leading_seq_ + conclusion_seq_.
     *
     * The audio layer renders these as one contiguous transmission; the Trw
     * grid is a property of the sample stream (49 symbols × 8 ms per word).
     * on_word_complete() advances calling phases against these word counts.
     */
    void enqueue_call_sequence_();

    /**
     * SENDING_ACK: third handshake frame §A.5.5.3.4 / Figure A-31.
     * Sequence: TO [to_address] × 2 + TIS [self]
     * Built at send time because to_address is set during LISTENING phase.
     */
    void build_ack_words();

    /**
     * SENDING_RESPONSE: JOE's response frame §A.5.5.3.3 / Figure A-30.
     * Sequence: TO [caller_address] × 2 + TIS [self]  (or TWAS [self] if reject).
     * Built at send time because caller_address is set during WAIT_CYCLE_END.
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
