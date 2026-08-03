/**
 * \file App/freq_select_manager.h
 * \brief Enhanced Frequency-Select FSM (CMD 'f', MIL-STD-188-141B A.5.6.3.2).
 *
 * Owns the three-state machine (IDLE / PROPOSED / EXECUTING) for post-link
 * bilateral channel negotiation via CMD 'f'.  ALEController drives the three
 * entry points below; all EFS policy (proposal logic, collision tiebreak,
 * accept/reject, timeout, cooldown) lives here.
 *
 * ALEController usage (single-threaded, called from update() and on_received_word()):
 *   // update():
 *   if (efs_enabled && linked && idle_gate && freq_select_.is_idle())
 *       freq_select_.evaluate(now_ms, rx_ber_settle_ms_, threshold);
 *   freq_select_.tick(now_ms);
 *
 *   // on_received_word():
 *   if (config_.enhanced_freq_select)
 *       freq_select_.on_word(word, now_ms_, threshold);
 *   else
 *       freq_select_.reset_pending_cmd();
 */

#pragma once
#include "Protocol/Control/ale_state_machine.h"
#include "LQA/lqa_analyzer.h"
#include "Word/ale_word.h"
#include <cstdint>
#include <string>
#include <functional>

namespace ale {

class FreqSelectManager {
public:
    /**
     * @param sm            State machine — queried for channel/peer/state,
     *                      driven for terminate_link() and trigger_linked_orderwire().
     * @param lqa           LQA analyzer — queried to rank channels per station.
     * @param get_self_addr Returns this station's primary call sign (collision tiebreak).
     * @param is_self       Returns true for a local self-address (guards peer DB writes).
     * @param on_relink     Called with peer address when peer accepts a proposal;
     *                      controller sets pending_relink_addr_ so the TWAS → re-call
     *                      path fires on the next update() tick.
     * @param on_status     Emits a human-readable EFS status line.
     */
    FreqSelectManager(
        ALEStateMachine&                        sm,
        LQAAnalyzer&                            lqa,
        std::function<std::string()>            get_self_addr,
        std::function<bool(const std::string&)> is_self,
        std::function<void(const std::string&)> on_relink,
        std::function<void(const std::string&)> on_status);

    /**
     * Evaluate whether a better channel is worth proposing to the peer (A.5.6.3.2).
     * Call from update() while LINKED + EFS enabled + is_idle().
     *
     * @param now_ms        Current monotonic time.
     * @param ber_settle_ms Last non-sounding RX-frame settle timestamp (0 = no data).
     * @param threshold     Minimum score improvement to trigger a proposal.
     */
    void evaluate(uint32_t now_ms, uint32_t ber_settle_ms, float threshold);

    /**
     * Advance FSM timers — handles proposal timeout.
     * Call unconditionally from update() on every tick.
     */
    void tick(uint32_t now_ms);

    /**
     * Feed a received word to the CMD 'f' decoder (A.5.6.3.2).
     * Call from on_received_word() when EFS is enabled; checks LINKED state internally.
     *
     * @param threshold Acceptance threshold forwarded to handle_proposal().
     */
    void on_word(const ALEWord& word, uint32_t now_ms, float threshold);

    /** Clear CMD 'f' capture state. Call when EFS is disabled mid-flight. */
    void reset_pending_cmd();

    /** True when no EFS proposal is in flight (safe for the controller's IDLE guard). */
    bool is_idle() const { return phase_ == Phase::IDLE; }

private:
    enum class Phase : uint8_t { IDLE, PROPOSED, EXECUTING };

    void handle_response(uint32_t freq_hz, uint32_t now_ms);
    void handle_proposal(uint32_t freq_hz, const std::string& peer,
                         uint32_t now_ms, float threshold);
    void send_orderwire(uint32_t freq_hz);

    ALEStateMachine&                        sm_;
    LQAAnalyzer&                            lqa_;
    std::function<std::string()>            get_self_addr_;
    std::function<bool(const std::string&)> is_self_;
    std::function<void(const std::string&)> on_relink_;
    std::function<void(const std::string&)> on_status_;

    Phase       phase_          = Phase::IDLE;
    uint32_t    proposed_hz_    = 0;
    uint32_t    timeout_ms_     = 0;
    uint32_t    cooldown_ms_    = 0;
    std::string peer_;
    bool        pending_cmd_rx_ = false;
};

} // namespace ale
