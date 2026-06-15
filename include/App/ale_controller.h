/**
 * \file App/ale_controller.h
 * \brief ALEController — single operator-facing entry point for ALE 2G.
 *
 * Wires ALEStateMachine ↔ ALE2GModem::Modulator ↔ ALE2GModem::Demodulator.
 * The CLI or GUI only talks to this class; it never touches the state machine
 * or modem directly.
 *
 * Threading model
 * ───────────────
 * All methods (update, feed_audio, initiate_call, …) must be called from the
 * same thread, or the caller must provide external locking.
 *
 * Typical caller (SAM) usage:
 *   ALEController ctrl;
 *   ctrl.set_self_address("SAM");
 *   ctrl.set_audio_device(&audio);
 *   ctrl.on_link_established = [](auto& p) { puts("LINKED"); };
 *   ctrl.initiate_call("BOB");
 *   while (running) { std::vector<int16_t> rx; audio.tick(rx); ctrl.feed_audio(rx.data(), rx.size()); ctrl.update(now_ms()); }
 *
 * Typical responder (BOB) usage:
 *   ALEController ctrl;
 *   ctrl.set_self_address("BOB");
 *   ctrl.on_call_received    = [](auto& c) { printf("Call from %s\n", c.c_str()); };
 *   ctrl.on_link_established = [](auto& p) { puts("LINKED"); };
 *   ctrl.start_available();
 *   while (running) { ctrl.update(now_ms()); ctrl.feed_audio(pcm, n); }
 */

#pragma once
#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "App/audio_device.h"
#include "PAL/events.h"
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace pal { class IRadio; }

namespace ale {

class ALEController {
public:
    ALEController();

    // ── Configuration ───────────────────────────────────────────────────────
    void set_self_address(const std::string& addr);

    /**
     * Attach a radio for PTT and frequency-hopping control.
     *
     * When set, PTT follows the RX-enable signal (PTT on = RX off = TX active)
     * and every channel change fired by ALEStateMachine is forwarded to
     * radio->set_channel().  Pass nullptr to detach.  Ownership stays with the caller.
     * The radio must be initialized and started before this call.
     */
    void set_radio(pal::IRadio* r);

    /**
     * Attach the audio device used for real-time TX/RX.
     *
     * Registers the modem's pull function with the device (symbol source) so
     * the audio thread can render symbol frames autonomously.  Frame-completion
     * callbacks (SM::on_word_complete) are armed here and fired from tick().
     *
     * Call once after audio->open() and before the main loop.
     * Passing nullptr reverts to offline mode (update() drives completion).
     */
    void set_audio_device(AudioDevice* dev);

    /**
     * Set the assumed number of scan channels of the target station.
     * Used to size the scanning-call section.  0 = target on a fixed channel
     * (skips scanning section, only sends leading call + conclusion).
     * Default: 0.
     */
    void set_target_scan_channels(uint32_t n);

    /** Ordered list of channels to try on no-reply (multi-channel calling). */
    void set_calling_channels(const std::vector<Channel>& channels);

    /**
     * Attach a PAL event handler to receive structured ALE events.
     *
     * When set, the controller emits pal::Event objects alongside the individual
     * callbacks (on_link_established, on_call_received, etc.).  Both mechanisms
     * are active simultaneously; the handler does not replace the callbacks.
     *
     * Passing nullptr detaches the handler.  Ownership stays with the caller.
     */
    void set_event_handler(pal::IEventHandler* handler);

    // ── Operator actions ────────────────────────────────────────────────────

    /**
     * Enter IDLE/available state: enable RX on the current fixed channel.
     * The station stays in IDLE (no channel hopping); incoming calls are
     * detected and handled automatically.  This is the correct startup mode
     * when no scan channel list has been configured.
     */
    void start_available();

    /**
     * Enter SCANNING: start hopping through the configured channel list.
     * Use this when a scan channel list has been set via set_calling_channels().
     */
    void start_scanning();

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
     * Process a text command from operator (CLI, GUI, or remote control).
     *
     * Supported commands:
     *   CMD:CALL <ADDR>   — initiate individual call to ADDR
     *   CMD:TERMINATE     — terminate current link
     *   CMD:REJECT        — reject incoming call with TWAS
     *   CMD:LISTEN        — enter scanning / listening state
     *   CMD:STATUS        — return current SM state name
     *   CMD:HELP          — list available commands
     *
     * Returns a human-readable result string suitable for display:
     *   "OK: ..."   on success
     *   "ERROR: …"  on invalid command or wrong state
     *   "STATUS: …" for CMD:STATUS
     */
    std::string process_command(const std::string& cmd);

    // ── FEC / sync tuning (MIL-STD-188-141B A.5.2.6.3) ──────────────────────
    // Forwarded to the receive demodulator.  Defaults are the most tolerant
    // operating point (full Golay correction + MIN_UNANIMOUS_VOTES) so any
    // spec-compliant signal can be acquired; tune only for special conditions.

    /// Select the Golay correction power: Mode3_4 (default) / 2_5 / 1_6 / 0_7.
    void      set_golay_mode(GolayMode m)        { demodulator_.set_golay_mode(m); }
    GolayMode golay_mode() const                 { return demodulator_.golay_mode(); }

    /// Minimum unanimous 2/3-vote count to accept a word (0..49; default 33).
    void    set_min_unanimous_votes(uint8_t v)   { demodulator_.set_min_unanimous_votes(v); }
    uint8_t min_unanimous_votes() const          { return demodulator_.min_unanimous_votes(); }

    /// A.5.2.6.3 "DO": auto-adjust Golay mode + unanimous-vote threshold to the
    /// observed signal quality (off by default).
    void set_adaptive_fec(bool on)               { demodulator_.set_adaptive_fec(on); }
    bool adaptive_fec() const                    { return demodulator_.adaptive_fec(); }

    /// Diagnostics: when on, emit (via on_status_changed) the periodic RX peak
    /// level and every demodulated word (type/address/unanimous/fec).  Use to
    /// see what the receiver actually decodes (e.g. during the LISTENING window).
    void set_debug_rx(bool on)                   { debug_rx_ = on; }
    bool debug_rx() const                        { return debug_rx_; }

    // ── Inspection ──────────────────────────────────────────────────────────
    ALEState    state() const { return sm_.get_state(); }
    std::string self()  const { return self_addr_; }

private:
    ALEStateMachine          sm_;
    ALE2GModem::Modulator    modulator_;
    ALE2GModem::Demodulator  demodulator_;
    AudioDevice*             audio_device_   = nullptr;
    pal::IEventHandler*      event_handler_  = nullptr;
    pal::IRadio*             radio_          = nullptr;
    std::string              self_addr_;
    std::string              last_caller_;   // caller address as it arrives (TIS + DATA)

    // RX diagnostics (set_debug_rx)
    bool                     debug_rx_   = false;
    int                      dbg_peak_   = 0;   // running peak |sample| since last report
    uint32_t                 dbg_count_  = 0;   // samples accumulated since last report

    void wire_callbacks();
    void on_sm_state_change(ALEState from, ALEState to);
    void on_operator_event(OperatorEvent ev);
    void on_received_word(const ALEWord& word);
    void emit_status(const std::string& msg);
    void emit_event(pal::EventType type, const std::string& msg = "", int32_t code = 0);
};

} // namespace ale
