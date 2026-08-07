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
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <thread>  // std::thread::id — debug-only single-thread contract check

namespace ale { class LQAMetrics; }
namespace ale { class ALECallProcessor; }   // owns received-word processing (friend)

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
    CONCLUSION,         ///< TIS SAM — frame terminator, invites response
    LISTENING,          ///< Twr/Twrt: RX window, waiting for called station response
    SENDING_ACK,        ///< Third handshake frame: TO JOE × 2 + TIS SAM (REQ-LINK-008)
};

/**
 * \enum ScanningPhase
 * Sub-states within SCANNING per A.5.5.4.4/5 (T-10).
 *   HOPPING        — normal channel-hopping: hop once the dwell has elapsed AND
 *                    the radio is hop-ready (settled). Dwell is measured from the
 *                    hop, so the per-channel period is the spec dwell with tune
 *                    latency overlapping it; the hop-ready gate (an injected
 *                    predicate, see set_hop_ready_query) keeps at most one tune
 *                    in flight without the FSM knowing anything about the radio.
 *   SCAN_PAUSE  — valid ALE word received; dwell frozen until Tdrw silence,
 *                    then hop (allows full frame capture and TO_SELF detection)
 *
 * AllCall reception is deliberately NOT a scanning sub-state: an AllCall word
 * fires CALL_DETECTED and the SM leaves SCANNING for HANDSHAKE (allcall_silent_),
 * which handles the one-way broadcast and its conclusion, then returns here.
 */
enum class ScanningPhase {
    HOPPING,
    SCAN_PAUSE,
};

/**
 * \enum SoundingPhase
 * Sub-states within SOUNDING per A.5.3.4 / REQ-CHAN-031.
 *   LBT           — Listen-Before-Transmit: wait Twt_ms before TX
 *   TRANSMITTING  — TIS-Frame wird gerade gesendet
 *   LISTENING     — optionales RX-Fenster für eingehenden Handshake (Trw)
 */
enum class SoundingPhase {
    LBT,          ///< Listen-Before-Transmit: wait Twt_ms, abort if channel busy (AC-SOUND-001-001)
    TRANSMITTING,
    LISTENING,
};

/**
 * \enum HandshakePhase
 * Sub-states within HANDSHAKE (called station, JOE side) per A.5.5.3.2–4.
 *
 *   WAIT_CYCLE_END    Twce: listen for calling station's conclusion (TIS SAM)
 *                       ├─ TIS received → arm Tlww for last word wait
 *                       └─ Tlww elapsed → AWAIT_ACCEPT (manual-accept mode) or SLOT_WAIT
 *   AWAIT_ACCEPT      Operator decision gate (implementation-defined, not in spec figure).
 *                     Only entered when set_require_explicit_accept(true) is active;
 *                     otherwise WAIT_CYCLE_END goes straight to SLOT_WAIT as before.
 *                       ├─ accept_call() → SLOT_WAIT (normal accept path)
 *                       ├─ reject_call() → SLOT_WAIT (pending_reject_ already set)
 *                       └─ decision_timeout elapsed, no decision → SLOT_WAIT (auto-accept
 *                          fallback, so an unattended station never hangs here)
 *   SLOT_WAIT         Tswt: wait assigned slot before LBT (0 ms for Individual Call, T-09)
 *   CHANNEL_CHECK     Tdrw = 2×Trw LBT before transmitting response — AC-LINK-002-002, A.5.5.3.3
 *                       ├─ any RX activity → channel busy → abort
 *                       └─ Tdrw clear → SENDING_RESPONSE
 *   SENDING_RESPONSE  TO caller × 2 + TIS self — response frame (Figure A-30)
 *                       └─ all words sent → WAIT_ACK
 *   WAIT_ACK          Twr: wait for ACK frame from calling station (Figure A-31)
 *                       ├─ "TIS SAM" + Tlww → HANDSHAKE_COMPLETE → LINKED
 *                       └─ "TWAS SAM" → abort → IDLE
 */
enum class HandshakePhase {
    WAIT_CYCLE_END,    ///< Twce: listen for SAM's conclusion (A.5.5.3.2)
    AWAIT_ACCEPT,      ///< Operator accept/reject decision gate (manual-accept mode only)
    SLOT_WAIT,         ///< Tswt: warte zugewiesenen Slot vor LBT (0 ms bei Individual Call, T-09)
    CHANNEL_CHECK,     ///< Tdrw=2×Trw LBT before response TX (AC-LINK-002-002, A.5.5.3.3)
    SENDING_RESPONSE,  ///< TO caller × 2 + TIS self — Figure A-30
    WAIT_ACK,          ///< Twr: wait for SAM's ACK — Figure A-31
};

/**
 * \class ALEStateMachine
 */
class ALEStateMachine {
public:
    /** Pending AMD orderwire message — sent in the ACK frame per A.5.7.2.2. */
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
    /** Current scan configuration (for read-modify-write, e.g. changing just dwell_time_ms). */
    const ScanConfig& get_scan_config() const { return channel_manager_.config(); }
    void add_scan_channel(const Channel& channel);
    void set_self_address(const std::string& address);
    std::string get_self_address() const { return address_book.get_self_address(); }
    const Channel* get_current_channel() const;

    bool initiate_call(const std::string& to_address);
    bool initiate_net_call(const std::string& net_address);

    /**
     * Initiiert einen Star-Group-Call an eine ad-hoc Gruppe von Stationen (A.5.5.4.3).
     * members = vollständige Adressen aller Gruppenmitglieder (>=1, Reihenfolge
     *           bestimmt die Slot-Vergabe beim Empfänger, A.5.5.4.3.4).
     * Max. 12 Adresswörter gesamt über alle Mitglieder, max. 5 unique first words
     * im Scanning Call (Spec A.5.5.4.3) — Dedup/Cap erfolgt in
     * ALESequenceBuilder::scanning_call_group().
     * Returns false if members is empty.
     */
    bool initiate_group_call(const std::vector<std::string>& members);

    /**
     * Force-complete the incoming-call handshake.  The handshake otherwise
     * auto-advances (WAIT_CYCLE_END → … → WAIT_ACK → LINKED) on update() with no
     * manual step, so this is only a bounded escape hatch.
     *
     * Returns false (no-op) unless the SM is in HANDSHAKE/WAIT_ACK with a known
     * caller — i.e. our response frame has already been transmitted and we are
     * only waiting for the caller's ACK.  From any earlier phase (notably
     * WAIT_CYCLE_END, before the caller's conclusion was received or our
     * response sent) it MUST refuse, otherwise it would declare LINKED with an
     * empty caller identity and no response on the air (peer times out, we
     * believe the link is up).  The operator accept/reject decision itself is
     * applied post-link by ALEController (see accept_call()/reject_call()).
     *
     * \return true if the handshake was force-completed to LINKED, false otherwise.
     */
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

    /**
     * Accept an incoming call while it is paused in the AWAIT_ACCEPT gate
     * (see set_require_explicit_accept()). Resolves the gate so the handshake
     * proceeds to SLOT_WAIT/CHANNEL_CHECK and the normal accept response is sent.
     *
     * Returns false if not in HANDSHAKE+AWAIT_ACCEPT (e.g. manual-accept mode is
     * off, in which case the handshake already auto-accepts — nothing to do).
     */
    bool accept_call();

    /**
     * Enable/disable the manual-accept gate (AWAIT_ACCEPT) for incoming calls.
     *
     * Default (off): handshakes auto-accept exactly as before — WAIT_CYCLE_END
     * goes straight to SLOT_WAIT, no operator action needed.
     *
     * When on: after the calling station's conclusion is received, the SM pauses
     * in AWAIT_ACCEPT and waits for accept_call() or reject_call(). If neither is
     * called within decision_timeout_ms, it falls back to auto-accept so an
     * unattended station never hangs waiting for an operator.
     */
    void set_require_explicit_accept(bool on, uint32_t decision_timeout_ms = 10000);
    bool     requires_explicit_accept() const  { return require_explicit_accept_; }
    uint32_t accept_decision_timeout_ms() const { return accept_decision_timeout_ms_; }

    bool send_sounding();

    /// Select conclusion type for all sounding transmissions.
    /// TIS (default, use_twas=false) invites return calls; TWAS (use_twas=true) is announce-only.
    void set_sounding_use_twas(bool use_twas) { sounding_use_twas_ = use_twas; }

    /**
     * Transmit a sounding on each channel in @p channels in turn (multi-channel
     * sounding sweep, §A.5.3). The sweep tunes the radio to each channel via the
     * channel callback, performs the normal LBT → TX (self-address conclusion
     * ×2) → LISTENING sounding cycle, then advances to the next channel. A
     * return call detected during any LISTENING window (T-08) interrupts the
     * sweep and links on that channel. When the last channel is sounded the SM
     * returns to its previous state (IDLE/SCANNING).
     * \return false if not in IDLE/SCANNING or @p channels is empty.
     */
    bool send_sounding_sweep(const std::vector<Channel>& channels);

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

    /** Current assumed scan-channel count "C" (see set_target_scan_channels()). */
    uint32_t get_target_scan_channels() const { return target_scan_channels; }

    /**
     * Override this instance's Level-5 "Programmable defaults" (Twa, Tt — see
     * TimingParameters in ale_timing.h). Defaults come from ALETimingConstants;
     * an override here is scoped to THIS state machine only — other instances
     * in the same process (e.g. a second station in the same test binary)
     * keep their own defaults/overrides untouched.
     */
    void                     set_timing_parameters(const TimingParameters& p) { timing_ = p; }
    const TimingParameters&  get_timing_parameters() const                   { return timing_; }

    /**
     * Correct for a deferred RX turnaround: rx_enabled_callback(true) fires —
     * and the "started waiting for the peer" timestamp (listening_start_ms /
     * hs_ack_start_ms / sounding_listening_start_ms_) is stamped — at the
     * moment THIS station's own audio finishes rendering, not at the moment
     * it can actually receive. When ptt_tail_ms/output_latency_ms defer the
     * real PTT release and demod re-enable (ALEController::tick_ptt_timing()),
     * that gap silently eats into this station's OWN turnaround budget (e.g.
     * WAIT_ACK's Twr window, spec-defined and otherwise untouched here) before
     * a single peer word could possibly have arrived — on top of, not instead
     * of, the peer's own unknown turnaround overhead. Call this once, right
     * after the deferred release actually happens, with the exact
     * ptt_release_delay_ms that was applied; it shifts whichever "waiting for
     * peer" timer is currently live forward by that amount so the
     * spec-mandated window is measured from when this station could truly
     * start listening, not from when it merely decided to. No-op if no such
     * timer is currently armed (e.g. plain PTT release with nothing pending).
     */
    void extend_peer_wait_window_for_ptt_release_delay(uint32_t ptt_release_delay_ms) {
        if (ptt_release_delay_ms == 0) return;
        if (calling_phase == CallingPhase::LISTENING && !response_to_detected)
            listening_start_ms += ptt_release_delay_ms;
        if (handshake_phase == HandshakePhase::WAIT_ACK && hs_ack_to_ms == 0)
            hs_ack_start_ms += ptt_release_delay_ms;
        if (sounding_phase_ == SoundingPhase::LISTENING)
            sounding_listening_start_ms_ += ptt_release_delay_ms;
    }

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
     * Must be called before initiate_call(); calling_channel_index resets to 0
     * so a retry sequence always starts from the first channel — without the
     * reset, a mid-retry reconfiguration (calling_channel_index already > 0)
     * would make the next try_next_calling_channel() increment past the new
     * (shorter) list and falsely report NO_CHANNELS_LEFT, skipping valid
     * channels.  initiate_call*() also reset it; this makes the contract hold
     * regardless of which is called first.
     */
    void set_calling_channels(const std::vector<Channel>& channels) {
        calling_channels      = channels;
        calling_channel_index = 0;
    }
    /** Ordered outbound channel list most recently set by set_calling_channels()
     *  (by initiate_call / initiate_group_call / manual VFO). Exposed for
     *  inspection/tests so the active-net scoping can be verified. */
    const std::vector<Channel>& get_calling_channels() const { return calling_channels; }

    /**
     * Queue an AMD orderwire message to be sent in the ACK frame of the next call
     * (A.5.7.2.2: AMD follows the complete calling+response cycle, not the calling frame).
     * Calling set_pending_message with type=NONE removes any queued message.
     */
    void set_pending_message(const PendingMessage& msg) { pending_message = msg; }
    void clear_pending_message()                        { pending_message = PendingMessage(); }

    /**
     * Queue a CMD LQA word (char 'a', Table A-XIV) to be inserted in the
     * MESSAGE section of the next call frame (caller) or response (responder).
     * raw24 is the full 24-bit encoded word from encode_lqa_cmd().
     */
    void set_pending_lqa_cmd(uint32_t raw24) {
        pending_lqa_cmd_raw_ = raw24; pending_lqa_cmd_set_ = true;
    }
    void clear_pending_lqa_cmd() { pending_lqa_cmd_set_ = false; }

    /**
     * Queue a pre-built LQA report sequence (CMD 'r' + DATA words) to be
     * inserted in the MESSAGE section of the next call frame.
     */
    void set_pending_lqa_report_seq(const ALESequence& seq) {
        pending_lqa_report_seq_ = seq; pending_lqa_report_set_ = true;
    }
    void clear_pending_lqa_report_seq() { pending_lqa_report_set_ = false; }

    /**
     * Queue a CMD NOISE word (char 'n', Figure A-26) to be appended after
     * the TIS frame in the next sounding cycle.
     * raw24 is the 24-bit word from ALESequenceBuilder::noise_cmd() encoding.
     */
    void set_pending_noise_cmd(uint32_t raw24) {
        pending_noise_cmd_raw_ = raw24; pending_noise_cmd_set_ = true;
    }
    void clear_pending_noise_cmd() { pending_noise_cmd_set_ = false; }

    /**
     * Queue a word sequence (e.g. CMD 'f' + DATA, or an AMD orderwire frame) to
     * be sent as a brief orderwire burst while LINKED, followed by TIS:SELF.
     * Used by Enhanced Frequency-Select (A.5.6.3.2) and by AMD-over-link (A.5.7.2).
     *
     * @p double_burst  EFS passes true (default) to send the payload+conclusion
     *                  twice for reliability (the historic behaviour). AMD text
     *                  passes false so the peer decodes the message exactly once
     *                  — a doubled AMD frame would concatenate the text twice.
     * No-op if not in LINKED state or termination already in progress.
     */
    void trigger_linked_orderwire(std::vector<ALEWord> words,
                                  bool double_burst = true);

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

    /**
     * Reset the Twa idle timer to the current SM clock and re-arm the idle
     * warning.  Same effect as on_link_activity() plus clearing the one-shot
     * warning flag, exposed as a named operation for the GUI "reset timer"
     * popup.  No-op outside LINKED (the timer is only meaningful while linked).
     */
    void reset_link_idle_timer();

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
    uint32_t       get_first_call_tx_ms()     const { return first_call_tx_ms; }
    CallingPhase   get_calling_phase()        const { return calling_phase; }
    bool           get_response_to_detected() const { return response_to_detected; }
    HandshakePhase get_handshake_phase()      const { return handshake_phase; }
    SoundingPhase  get_sounding_phase()       const { return sounding_phase_; }
    uint32_t       get_words_pending()        const { return words_pending; }
    bool           is_emergency_active()      const { return emergency_active; }
    const std::string& get_to_address()      const { return to_address; }
    const std::string& get_caller_address()   const { return caller_address; }
    bool           is_hs_conclusion_rcvd()    const { return hs_conclusion_rcvd; }
    bool           is_pending_reject()        const { return pending_reject_; }

    /**
     * Debug-only single-thread contract net.  Returns the number of times a
     * boundary method (update / process_received_word / on_word_complete / the
     * public mutators) was called from a thread other than the one that made the
     * first such call.  The SM has no internal synchronization — safety rests on
     * the caller-dispatched single-thread contract (see ale_controller.h and
     * docs/THREADING.md); this counter makes a violation visible in dev/CI.
     *
     * In release builds the check is compiled out, so this is always 0 and adds
     * no overhead.  In debug builds a non-zero value indicates a contract
     * violation (a caller is driving the SM from multiple threads without its
     * own locking).
     */
    uint32_t       thread_violations()        const { return thread_violations_; }

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
     * Fired once per idle period, IDLE_WARNING_LEAD_MS before the configured Twa
     * elapses, to let the GUI present a "reset timer" popup.  Re-arms on any link
     * activity (ALE word / TX orderwire / on_link_activity() / reset_link_idle_timer()).
     * \p remaining_sec is the whole seconds left before Twa fires (>=1).
     */
    void set_idle_warning_callback(std::function<void(uint32_t)> callback) {
        idle_warning_cb_ = callback;
    }

    /**
     * Called for operator-level events (REQ-LINK-007, REQ-LINK-008).
     * See OperatorEvent for event types.
     */
    void set_operator_callback(std::function<void(OperatorEvent)> callback) {
        operator_callback = callback;
    }

    /**
     * Broadband channel-occupancy query for LBT (A.5.4.7.2 / A.4.2.2).
     * Polled during the three LBT windows (calling LBT, sounding LBT, handshake
     * CHANNEL_CHECK) on every update() tick, in addition to the ALE-word busy
     * path.  Return true while the channel carries non-ALE traffic (energy over
     * the tracked noise floor — see ChannelOccupancyDetector).  Busy behavior
     * follows A.5.4.7: calling → next channel; sounding → abort (reschedule);
     * handshake → abort.  Unset = no occupancy detection (legacy behavior).
     */
    void set_channel_busy_query(std::function<bool()> q) {
        channel_busy_query_ = std::move(q);
    }

    /**
     * A.5.4.7.3 operator override: when true, occupancy-busy results are
     * ignored (the LBT pause itself still runs).  For emergency use.
     */
    void set_lbt_override(bool on) { lbt_override_ = on; }
    bool lbt_override() const      { return lbt_override_; }

    /**
     * A.5.4.7.1 LBT duration policy: shared channels require an LBT pause of
     * at least Twt_shared_ms (2 s); only channels known to carry ALE
     * exclusively may use the short Twt (784 ms).  The controller sets this
     * from the Channel::ale_only flags of the channels involved in the call /
     * sounding.  Default false preserves the short Twt (existing tests and
     * ALE-only deployments).  Applies to calling LBT and sounding LBT; the
     * handshake CHANNEL_CHECK keeps its protocol-defined Tdrw window.
     */
    void set_lbt_shared(bool shared) { lbt_shared_ = shared; }
    bool lbt_shared() const          { return lbt_shared_; }

    /**
     * Attach an LQAMetrics instance for quality tracking.
     * When set, every call to update_link_quality() also feeds the metrics
     * subsystem.  Pass nullptr to detach.  Ownership stays with the caller.
     */
    void set_lqa_metrics(LQAMetrics* m);

    /**
     * Optional trace sink for protocol-level debug events.
     * When set, SM_TRACE() calls forward the message string to \p cb.
     * Pass nullptr to detach.
     */
    void set_trace_callback(std::function<void(const std::string&)> cb) {
        trace_cb_ = std::move(cb);
    }

    /**
     * Register a monitor callback fired once per complete assembled ALE frame.
     * Fires unconditionally for every frame that MessageAssembler considers
     * complete — regardless of local protocol state or address match.
     * Pass nullptr to detach.
     */
    void set_frame_assembled_callback(std::function<void(const ALEMessage&)> cb);

    /** Address book accessor — exposes the SM's AddressBook to the controller layer. */
    AddressBook&       get_address_book()       { return address_book; }
    const AddressBook& get_address_book() const { return address_book; }

    /**
     * §A.5.3.3 stage 1: open SCAN_PAUSE when ALE energy is detected on this
     * channel before a fully-decoded word arrives.  No-op unless SCANNING and
     * currently HOPPING.  Traffic is only decodable once the radio has settled on
     * the channel, so the controller suppresses the energy callback while a tune
     * is in flight (see is_tune_settled()) — the FSM itself stays radio-agnostic.
     * Stage 2 (react_scanning_() on valid words) refreshes scan_pause_settle_ms_ so
     * the Tdrw depart timer counts from the last word, not the stage-1 trigger.
     */
    void begin_scan_pause(uint32_t t) {
        if (current_state != ALEState::SCANNING) return;
        if (scanning_phase_ == ScanningPhase::HOPPING) {
            scanning_phase_    = ScanningPhase::SCAN_PAUSE;
            scan_pause_settle_ms_ = t;
        }
    }

    /**
     * Hop-ready gate: an injected predicate answering "may the scanner hop now?"
     * The scanner hops only when the dwell has elapsed AND this returns true.
     * The controller wires it to the radio's is_tune_settled() so the next hop is
     * withheld until the current tune has settled — which restores the spec dwell
     * cadence (tune latency overlaps the dwell) and keeps at most one tune in
     * flight, all without the FSM knowing the radio is asynchronous.  Mirrors the
     * LBT set_channel_busy_query() pattern.  Unset (default) → always ready, i.e.
     * the pure wall-clock dwell behavior for synchronous backends and tests.
     */
    void set_hop_ready_query(std::function<bool()> q) {
        hop_ready_query_ = std::move(q);
    }

    /** Current scanning sub-phase (controller + tests). */
    ScanningPhase get_scanning_phase() const { return scanning_phase_; }

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
    // TX-drain safety net for terminate_link() / trigger_linked_orderwire():
    // both wait for on_word_complete() to drain words_pending to 0.  If that
    // never happens (audio stall / null transmit_callback that doesn't arm the
    // completion), the SM would hang in LINKED with RX disabled — handle_linked()
    // short-circuits while these flags are set, suppressing the Twa timer.  This
    // timestamp arms when a drain begins; handle_linked() force-completes the
    // transition / abandons the burst once TX_DRAIN_TIMEOUT_MS elapses.
    // 0 = no drain in progress (not armed).
    uint32_t         tx_drain_start_ms_ = 0;

    // ── Idle-warning (Twa) ────────────────────────────────────────────────
    // Fires on_idle_warning_cb_() once, IDLE_WARNING_LEAD_MS before Twa elapses.
    // Re-arms whenever last_word_time_ms advances (any link activity).  Tracked
    // against last_seen_word_time_ms_ so the re-arm logic lives entirely in
    // handle_linked() without touching every site that moves last_word_time_ms.
    bool             idle_warning_sent_    = false;
    uint32_t         last_seen_word_time_ms_ = 0;

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
    bool           allcall_silent_ = false; ///< true = current handshake is an AllCall (A.5.5.4.4):
                                           ///<  one-way broadcast — link on TIS conclusion,
                                           ///<  resume on TWAS, never send a response frame

    // ── Manual-accept gate (AWAIT_ACCEPT) ────────────────────────────────
    bool           require_explicit_accept_;   ///< set via set_require_explicit_accept(); default off
    bool           accept_decided_;            ///< true once accept_call()/reject_call() resolved the gate
    uint32_t       await_accept_start_ms_;     ///< timestamp AWAIT_ACCEPT was entered
    uint32_t       accept_decision_timeout_ms_;///< auto-accept fallback after this much silence from the operator

    // ── Emergency control (REQ-LINK-007) ─────────────────────────────────
    bool emergency_active;

    // ── Orderwire message state ───────────────────────────────────────────
    PendingMessage pending_message;   ///< set via set_pending_message(); consumed by initiate_call()
    PendingMessage active_message_;   ///< snapshot taken at initiate_call() time; survives channel retries

    // ── CMD LQA (char 'a', Table A-XIV) ──────────────────────────────────
    uint32_t    pending_lqa_cmd_raw_ = 0;
    bool        pending_lqa_cmd_set_ = false;
    ALESequence active_lqa_cmd_seq_;  ///< Snapshot at initiate_call(); survives channel retries

    // ── LQA Report sequence (CMD 'r' + DATA) ─────────────────────────────
    ALESequence pending_lqa_report_seq_;
    bool        pending_lqa_report_set_ = false;
    ALESequence active_lqa_report_seq_; ///< Snapshot at initiate_call(); survives channel retries

    // ── CMD NOISE (char 'n', Figure A-26) ────────────────────────────────
    uint32_t    pending_noise_cmd_raw_ = 0;
    bool        pending_noise_cmd_set_ = false;

    // ── Linked Orderwire (Enhanced Frequency-Select, A.5.6.3.2) ──────────
    std::vector<ALEWord> pending_orderwire_words_;
    bool                 orderwire_pending_      = false;
    bool                 orderwire_transmitting_ = false;
    bool                 orderwire_double_burst_ = true;  ///< EFS=true; AMD=false (single)

    // ── Scanning sub-state ───────────────────────────────────────────────
    ScanningPhase scanning_phase_;           ///< Aktuelle Phase innerhalb SCANNING
    uint32_t      scan_pause_settle_ms_ = 0;   ///< Last-word time during SCAN_PAUSE (A.5.3.1)
    bool          prev_hop_ready_ = true;      ///< last hop_ready_() while scanning — detect the settle edge to re-anchor the dwell

    // Hop-ready gate (see set_hop_ready_query). Unset → always ready (pure
    // wall-clock dwell); the controller wires it to radio is_tune_settled().
    std::function<bool()> hop_ready_query_;
    bool hop_ready_() const { return !hop_ready_query_ || hop_ready_query_(); }

    // ── Sounding sub-state (T-08) ─────────────────────────────────────────
    SoundingPhase sounding_phase_;        ///< Aktuelle Phase innerhalb SOUNDING
    uint32_t      sounding_lbt_start_ms_; ///< When SOUNDING LBT started; 0 = not active
    // Anchor for the SOUNDING LISTENING window (A.5.3.4) — when TRANSMITTING
    // drains and the optional RX window opens.  Kept separate from
    // state_entry_time_ms (which records when the STATE was entered) so a future
    // check_link_timeout() case for SOUNDING compares against the real entry
    // time, not the LISTENING sub-phase start.
    uint32_t      sounding_listening_start_ms_ = 0;

    // ── Multi-channel sounding sweep (send_sounding_sweep) ────────────────
    // Active only during a sweep; the channel-manager override (see
    // ALEChannelManager::set_override) pins current() to the sweep channel.
    std::vector<Channel> sounding_sweep_chs_;
    size_t               sounding_sweep_idx_     = 0;
    bool                 sounding_sweep_active_  = false;
    bool                 sounding_use_twas_      = false; ///< TIS (invite) vs TWAS (announce-only)

    // ── Target scan channels ──────────────────────────────────────────────
    uint32_t target_scan_channels;       ///< Assumed scan channels of target (for Tsc)

    // ── Level-5 "Programmable defaults" (per-instance, see TimingParameters) ──
    TimingParameters timing_;

    // ── Timing ────────────────────────────────────────────────────────────
    uint32_t state_entry_time_ms;
    uint32_t current_time_ms;

    // ── Call processor ─────────────────────────────────────────────────────
    // ALECallProcessor owns ALL received-word processing LOGIC (classification,
    // per-state reactions, LQA update, frame-assembly driving) as stateless friend
    // functions, so the SM itself contains no word-processing logic — only states +
    // transitions (+ time evolution / TX).  The stateful parts (MessageAssembler,
    // lqa_metrics_, frame_assembled_cb_) stay SM members so the SM remains
    // copyable; ALECallProcessor::process_received_word(*this, …) drives them.
    friend class ALECallProcessor;            // accesses SM privates + drives process_event

    // ── LQA ───────────────────────────────────────────────────────────────
    LQAMetrics* lqa_metrics_ = nullptr;  ///< Optional; set via set_lqa_metrics()

    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(ALEState, ALEState)>  state_callback;
    std::function<void(const ALEWord&)>      transmit_callback;
    std::function<void(bool)>                rx_enabled_callback;
    std::function<void(OperatorEvent)>       operator_callback;
    std::function<bool()>                    channel_busy_query_;  ///< LBT occupancy (A.5.4.7.2)
    bool                                     lbt_override_ = false; ///< A.5.4.7.3 operator override
    bool                                     lbt_shared_   = false; ///< A.5.4.7.1: shared-channel LBT (≥2 s)
    std::function<void(const std::string&)>  trace_cb_;
    std::function<void(const ALEMessage&)>   frame_assembled_cb_;
    std::function<void(uint32_t)>           idle_warning_cb_;

    // ── Internals ─────────────────────────────────────────────────────────
    void enter_state(ALEState new_state);
    void exit_state(ALEState old_state);
    bool transition_to(ALEState new_state);

    // ── Single-thread contract (debug-only) ──────────────────────────────
    // The SM has no internal synchronization; the caller must drive it from one
    // thread (or provide its own locking).  check_thread_() captures the owner
    // thread on the first boundary call and counts any call from a different
    // thread.  Compiled out in release (zero overhead); the members are kept so
    // the class layout stays stable across configs.
    std::thread::id owner_thread_id_;
    bool           owner_thread_captured_ = false;
    uint32_t       thread_violations_    = 0;
#ifndef NDEBUG
    void check_thread_() {
        const auto cur = std::this_thread::get_id();
        if (!owner_thread_captured_) {
            owner_thread_id_   = cur;
            owner_thread_captured_ = true;
        } else if (cur != owner_thread_id_) {
            ++thread_violations_;
        }
    }
#else
    void check_thread_() {}  // release: compiled out
#endif

    // ── Word-receive helpers ──────────────────────────────────────────────
    // (Received-word processing now lives in ALECallProcessor — see above.
    //  handle_invalid_word / detect_incoming_call / react_* were moved there.)

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

    /// Occupancy-busy during an LBT window? (query set, not overridden, busy)
    bool lbt_channel_busy_() const {
        return channel_busy_query_ && !lbt_override_ && channel_busy_query_();
    }
    /// Effective LBT pause (A.5.4.7.1): short Twt only on ALE-only channels.
    uint32_t effective_twt_ms_() const {
        return (lbt_shared_ && ALETimingConstants::Twt_shared_ms > ALETimingConstants::Twt_ms)
            ? ALETimingConstants::Twt_shared_ms
            : ALETimingConstants::Twt_ms;
    }

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
     * SENDING_ACK: third handshake frame §A.5.5.3.4 / Figure A-31 + A.5.7.2.2.
     * Sequence: TO [to_address] × 2 + [CMD 'a'] + [CMD 'r'+DATA...] + [CMD AMD+DATA/REP...] + TIS [self]
     * Message section (LQA CMD/Report + AMD) inserted before TIS conclusion when present.
     * A.5.7.2.2: complete calling+response cycle precedes all message content.
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
