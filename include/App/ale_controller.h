/**
 * \file App/ale_controller.h
 * \brief ALEController — single operator-facing entry point for ALE 2G.
 *
 * Wires ALEStateMachine ↔ ALE2GModem ↔ ALERxPipeline.
 * The CLI or GUI only talks to this class; it never touches the state machine
 * or modem directly.
 *
 * Threading model
 * ───────────────
 * All methods (update, feed_audio, initiate_call, …) must be called from the
 * same thread, or the caller must provide external locking.  The on_tx_audio
 * callback fires synchronously inside update() on the same thread.
 *
 * Typical caller (SAM) usage:
 *   ALEController ctrl;
 *   ctrl.set_self_address("SAM");
 *   ctrl.on_tx_audio         = [](auto* s, auto n) { audio.write(s, n); };
 *   ctrl.on_link_established = [](auto& p) { puts("LINKED"); };
 *   ctrl.initiate_call("BOB");
 *   while (running) { ctrl.update(now_ms()); ctrl.feed_audio(pcm, n); }
 *
 * Typical responder (BOB) usage:
 *   ALEController ctrl;
 *   ctrl.set_self_address("BOB");
 *   ctrl.on_call_received    = [](auto& c) { printf("Call from %s\n", c.c_str()); };
 *   ctrl.on_link_established = [](auto& p) { puts("LINKED"); };
 *   ctrl.start_listening();
 *   while (running) { ctrl.update(now_ms()); ctrl.feed_audio(pcm, n); }
 */

#pragma once
#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "App/ale_rx_pipeline.h"
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace ale {

class ALEController {
public:
    ALEController();

    // ── Configuration ───────────────────────────────────────────────────────
    void set_self_address(const std::string& addr);

    /**
     * Set the assumed number of scan channels of the target station.
     * Used to size the scanning-call section.  0 = target on a fixed channel
     * (skips scanning section, only sends leading call + conclusion).
     * Default: 0.
     */
    void set_target_scan_channels(uint32_t n);

    /** Ordered list of channels to try on no-reply (multi-channel calling). */
    void set_calling_channels(const std::vector<Channel>& channels);

    // ── Operator actions ────────────────────────────────────────────────────
    /** Enter SCANNING — listen for incoming individual calls. */
    void start_listening();

    /**
     * Initiate an individual call to target_addr.
     * \return false if the SM is not in IDLE or SCANNING state.
     */
    bool initiate_call(const std::string& target_addr);

    /**
     * Reject an incoming call with a TWAS frame.
     * Only valid during HANDSHAKE state; no-op otherwise.
     */
    void reject_call();

    /** Terminate the current link (if any). */
    void terminate_link();

    /** Emergency manual override: immediately abort all ALE operations. */
    void emergency_stop();

    // ── Main-loop drivers ───────────────────────────────────────────────────
    /**
     * Drive the state machine and modem.
     * \param now_ms  Monotonic wall-clock time in milliseconds since start.
     *                Use a steady clock; do not pass wall-clock absolute time.
     */
    void update(uint32_t now_ms);

    /**
     * Feed PCM audio captured from the sound card (8 kHz, mono, 16-bit).
     * Passes samples to the RX pipeline; may trigger word_cb_ → SM word receive.
     */
    void feed_audio(const int16_t* samples, uint32_t count);

    // ── Callbacks ───────────────────────────────────────────────────────────

    /** Three-way handshake complete.  peer_addr = remote station address. */
    std::function<void(const std::string& peer_addr)> on_link_established;

    /**
     * Incoming call detected.  Fires when the caller's TIS word is received
     * and the caller address is known.  The SM handles the response
     * automatically; the operator does not need to call accept_call().
     * To reject, call reject_call() before the response TX begins.
     */
    std::function<void(const std::string& caller_addr)> on_call_received;

    /** Link terminated (reason = informational string). */
    std::function<void(const std::string& reason)> on_link_terminated;

    /** Human-readable status change for logging or display. */
    std::function<void(const std::string& msg)> on_status_changed;

    /**
     * PCM audio ready to send to the sound card (8 kHz, mono, 16-bit).
     * Fires synchronously from update() on the same thread.
     */
    std::function<void(const int16_t* samples, uint32_t count)> on_tx_audio;

    // ── Inspection ──────────────────────────────────────────────────────────
    ALEState    state() const { return sm_.get_state(); }
    std::string self()  const { return self_addr_; }

private:
    ALEStateMachine sm_;
    ALE2GModem      modem_;
    ALERxPipeline   rx_pipeline_;
    std::string     self_addr_;
    std::string     last_caller_;   // caller address as it arrives (TIS + DATA)

    void wire_callbacks();
    void on_sm_state_change(ALEState from, ALEState to);
    void on_operator_event(OperatorEvent ev);
    void on_received_word(const ALEWord& word);
    void emit_status(const std::string& msg);
};

} // namespace ale
