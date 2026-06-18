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
 *
 * A few GUI-settings-shaped capabilities (timing beyond Twa/Tt, accept
 * policy, audio/rig device *selection*, sync-lock status) are deliberately
 * NOT here — see docs/GUI_BRIDGE_GAPS.md for why and where each belongs.
 */

#pragma once
#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "App/audio_device.h"
#include "PAL/events.h"
#include "LQA/lqa_database.h"
#include "LQA/lqa_analyzer.h"
#include "Stores/ale_data_store.h"
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
     * Add a channel to the calling list (replace if same rx_frequency_hz exists).
     * Immediately reconfigures the SM and auto-saves to channel_file_ if set.
     */
    bool add_channel(const Channel& ch);

    /** Remove the channel with the given RX frequency.  Returns false if not found. */
    bool del_channel(uint32_t rx_hz);

    /** Read-only access to the current channel list. */
    const std::vector<Channel>& channels() const { return calling_channels_; }

    /**
     * Load channel list from a .ale file.
     * Format: one channel per line — rx_hz [tx_hz] [mode] [label]
     * Lines starting with '#' are ignored.
     * \return false if the file cannot be opened (non-fatal; old list kept).
     */
    bool load_channels(const std::string& path);

    /**
     * Save the current channel list to a .ale file.
     * \return false on I/O error.
     */
    bool save_channels(const std::string& path) const;

    /**
     * Set the default channel file path for auto-save.
     * When set, add_channel / del_channel / clear_channels write to this path
     * automatically so channel changes survive restarts.
     */
    void set_channel_file(const std::string& path) { channel_file_ = path; }

    // ── Nets (A.5.5.4.3 group membership / scanning-call sizing) ───────────
    // A net is a named subset of channel IDs (see Channel::id). Used to derive
    // target_scan_channels automatically for calls to a registered Contact
    // (see add_contact()) — see initiate_call()'s docs. Auto-saved to
    // channel_file_ alongside the channel list (NET: lines), same as channels.

    /** Add an empty net.  Returns false if a net with this name already exists. */
    bool add_net(const std::string& name);

    /** Remove a net (and its channel assignments).  Returns false if not found. */
    bool del_net(const std::string& name);

    /** Read-only access to all configured nets. */
    const std::vector<Net>& nets() const { return net_store_.all(); }

    /** Assign channel_id to net.  Returns false if the net doesn't exist. */
    bool assign_channel_to_net(const std::string& net_name, const std::string& channel_id);

    /** Remove channel_id from net.  Returns false if the net doesn't exist. */
    bool unassign_channel_from_net(const std::string& net_name, const std::string& channel_id);

    // ── LQA ─────────────────────────────────────────────────────────────────

    /**
     * Load LQA database from file (call at startup to restore channel history).
     * \return false if file not found or format mismatch (non-fatal; empty DB used).
     */
    bool load_lqa(const std::string& path);

    /**
     * Save LQA database to file (call at shutdown or periodically).
     * \return false on I/O error.
     */
    bool save_lqa(const std::string& path) const;

    /**
     * Enable/disable automatic periodic sounding on channels already in the LQA DB.
     * When on, the main loop triggers sm_.send_sounding() whenever a channel's LQA
     * data is older than the sounding_interval (default 5 min) and the SM is
     * in SCANNING or IDLE state.
     */
    void enable_automatic_sounding(bool on);

    /** Access the LQA analyzer for channel quality queries. */
    LQAAnalyzer& lqa_analyzer() { return lqa_analyzer_; }
    const LQAAnalyzer& lqa_analyzer() const { return lqa_analyzer_; }

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
     * Transmit a single manual sounding (self-identification / LQA probe) on
     * the current channel. Independent of automatic periodic sounding
     * (enable_automatic_sounding()); fires immediately when the operator asks.
     * \return false if the SM is not in IDLE or SCANNING state.
     */
    bool send_sounding();

    /**
     * Initiate an individual call to target_addr.
     *
     * If target_addr matches a registered Contact (see add_contact()) that
     * belongs to a known Net (see add_net()), the scanning-call size ("C" in
     * Tsc = C × 2 × Trw) is derived automatically from that net's scan/sounding-
     * enabled channel count, overriding whatever set_target_scan_channels() had
     * configured. When no contact/net mapping is known, the previously
     * configured value is left untouched.
     *
     * \return false if the SM is not in IDLE or SCANNING state.
     */
    bool initiate_call(const std::string& target_addr);

    /**
     * Initiate a star group call (A.5.5.4.3) to an ad-hoc list of member
     * addresses. Same target_scan_channels auto-derivation as initiate_call(),
     * based on members.front()'s contact/net mapping (first-member-only
     * simplification — a group's members may belong to different nets).
     * \return false if members is empty or the SM is not in IDLE or SCANNING state.
     */
    bool initiate_group_call(const std::vector<std::string>& members);

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

    /**
     * AMD orderwire message received (AC-LINK-009-3).
     * Fires when a caller's MESSAGE section is decoded during handshake.
     * \param from  Calling station address (first 3 chars at time of TIS conclusion).
     * \param text  Assembled orderwire text (trailing padding stripped).
     */
    std::function<void(const std::string& from, const std::string& text)> on_amd_received;

    /** Human-readable status change for logging or display. */
    std::function<void(const std::string& msg)> on_status_changed;

    // ── GUI interfaces (CLI uses process_command) ────────────────────────

    /**
     * Process a text command from operator (CLI, GUI, or remote control).
     *
     * Supported commands:
     *   CMD:CALL <ADDR>   — initiate individual call to ADDR
     *   CMD:AMD <text>    — queue AMD orderwire for the next CMD:CALL (max 90 chars, Expanded-64)
     *   CMD:TERMINATE     — terminate current link
     *   CMD:REJECT        — reject incoming call with TWAS
     *   CMD:SCAN          — start scanning
     *   CMD:STATUS        — return current SM state name
     *   CMD:HELP          — list available commands
     *   CMD:ADD_CHANNEL   — add/update channel
     *   CMD:DEL_CHANNEL   — remove channel
     *   CMD:LIST_CHANNELS — list all channels
     *   CMD:CLEAR_CHANNELS — remove all channels
     *   CMD:SAVE_CHANNELS — save channels to file
     *   CMD:LOAD_CHANNELS — load channels from file
     *   CMD:ADD_NET       — add a net
     *   CMD:DEL_NET       — remove a net
     *   CMD:ASSIGN_CHANNEL   — assign a channel ID to a net
     *   CMD:UNASSIGN_CHANNEL — remove a channel ID from a net
     *   CMD:LIST_NETS     — list all nets and their channel assignments
     *
     * Returns a human-readable result string suitable for display:
     *   "OK: ..."   on success
     *   "ERROR: …"  on invalid command or wrong state
     *   "STATUS: …" for CMD:STATUS
     */
    std::string process_command(const std::string& cmd);

    // ── Contact/Address book (OtherAddrTable — GUI needs these) ─────────
    // Backed by ContactStore (Stores/ale_data_store.h). net_members ties a
    // contact to a Net (see add_net()) so initiate_call() can auto-size the
    // scanning call for calls to this contact.

    /**
     * Add a contact to the address book (or update if callsign already exists).
     * @param callsign Contact callsign (1-15 uppercase chars)
     * @param name Optional name/description
     * @param status "enabled" or "disabled"
     * @param net_members Optional net membership (comma-separated net names)
     * @param valid_channels Optional channel ID list (comma-separated, e.g. "C-1,C-3") or "ALL"
     * @return false if callsign is empty
     */
    bool add_contact(const std::string& callsign,
                     const std::string& name,
                     const std::string& status = "enabled",
                     const std::string& net_members = "",
                     const std::string& valid_channels = "ALL");

    /**
     * Remove a contact from the address book.
     * @param callsign Contact callsign to remove
     * @return true if contact was found and removed
     */
    bool remove_contact(const std::string& callsign);

    /**
     * Get all contacts (for GUI rendering).
     * @return one "CALLSIGN|name|enabled|net1,net2|chan1,chan2" string per
     *         contact (channels field is "ALL" when all_channels is set).
     */
    std::vector<std::string> get_all_contacts() const;

    /**
     * Set the currently selected contact (for outgoing calls).
     * @param callsign Contact callsign to select
     * @return true if contact exists
     */
    bool select_contact(const std::string& callsign);

    /**
     * Get the currently selected contact's callsign (or empty if none).
     */
    std::string get_selected_contact() const;

    // ── Self Address Table ────────────────────────────────────────────────
    // Backed by SelfAddressStore (Stores/ale_data_store.h). The primary entry
    // also drives the live protocol address via set_self_address()/sm_.

    /**
     * Add a self address entry (or update if addr already exists). The first
     * entry ever added becomes primary automatically (see set_primary_self_address()).
     * @param addr Self address (1-15 uppercase chars)
     * @param status "enabled" or "disabled"
     * @param valid_channels Channel ID list (comma-separated, e.g. "C-1,C-3") or "ALL"
     * @return false if addr is empty
     */
    bool add_self_address(const std::string& addr,
                          const std::string& status = "enabled",
                          const std::string& valid_channels = "ALL");

    /**
     * Remove a self address entry.
     * @param addr Self address to remove
     * @return true if address was found and removed
     */
    bool remove_self_address(const std::string& addr);

    /**
     * Get all self address entries (for GUI rendering).
     * @return one "ADDR|enabled|chan1,chan2" string per entry (channels
     *         field is "ALL" when all_channels is set)
     */
    std::vector<std::string> get_all_self_addresses() const;

    /**
     * Set primary self address (the one shown in header) and apply it as the
     * live protocol self-address (set_self_address()).
     * @param addr Self address to make primary
     * @return false if addr isn't a known entry
     */
    bool set_primary_self_address(const std::string& addr);

    /**
     * Get primary self address.
     */
    std::string get_primary_self_address() const;

    // ── Accept call (incoming) ───────────────────────────────────────────

    /**
     * Accept an incoming call that is paused in the manual-accept gate.
     * No-op (returns without effect) unless set_manual_accept_mode(true) is
     * active and a call is currently waiting for a decision — by default
     * (manual-accept mode off) every call is auto-accepted by the SM and
     * there is nothing to accept.
     */
    void accept_call();

    /**
     * Enable/disable the manual-accept gate for incoming calls (AWAIT_ACCEPT,
     * see ALEStateMachine::set_require_explicit_accept()).
     *
     * Default (off): calls auto-accept exactly as before; accept_call() is a
     * no-op and reject_call() remains the only operator decision available.
     *
     * When on: after the caller's conclusion is received, the SM pauses and
     * waits for accept_call()/reject_call(). If neither arrives within
     * decision_timeout_ms, it falls back to auto-accept so an unattended
     * station never hangs waiting for an operator.
     */
    void set_manual_accept_mode(bool on, uint32_t decision_timeout_ms = 10000);

    // ── Settings export/import ────────────────────────────────────────────

    /**
     * Export settings to a configuration file (key=value pairs, one per line).
     * Covers only fields with a real backing store today: primary self
     * address, channel/LQA file paths, FEC settings (golay mode/unanimous
     * votes/adaptive), target_scan_channels, debug_rx, manual_accept_mode.
     * See docs/GUI_BRIDGE_GAPS.md for settings categories not yet persisted
     * (timing beyond Twa/Tt, policy, audio/rig device selection).
     * @param path File path to export to
     * @return true on success
     */
    bool export_settings(const std::string& path);

    /** Import settings written by export_settings(). @return true on success */
    bool import_settings(const std::string& path);

    // ── Radio / VFO control ───────────────────────────────────────────────
    // All of these go directly through the attached pal::IRadio (set_radio())
    // — there is no shadow VFO state here. Without an attached radio, queries
    // fall back to the SM's current scan/calling channel (if any) and the
    // mutating calls are no-ops (false/no-op return), since there is no real
    // hardware to tune.

    /**
     * Get current channel information.
     * With a radio attached: queries radio_->get_channel(). Without one: falls
     * back to the SM's current scan/calling channel, or a default Channel{}.
     */
    Channel get_current_channel() const;

    /** Get current frequency in Hz (see get_current_channel()). */
    uint32_t get_current_frequency() const;

    /** Get current radio mode as string (see get_current_channel()). */
    std::string get_current_mode() const;

    /**
     * Set frequency directly (VFO mode), simplex (RX=TX).
     * @param hz Frequency in Hz
     * @return false if no radio is attached
     */
    bool set_frequency(uint32_t hz);

    /**
     * Set radio mode.
     * @param mode Mode string (USB, LSB, FM, etc.)
     * @return false if no radio is attached or mode is not recognised
     */
    bool set_mode(const std::string& mode);

    /**
     * Step through the configured calling-channel list (forward = +1,
     * backward = -1) and tune the attached radio to it.
     * @param direction +1 for next, -1 for previous
     * @return false if no radio is attached or the channel list is empty
     */
    bool step_channel(int direction);

    /**
     * Set VFO/tune step in Hz (for frequency nudge). Pure operator
     * convenience setting — not radio state, works without a radio attached.
     * @param hz Step size in Hz
     */
    void set_tune_step(uint32_t hz);

    /**
     * Get current tune step.
     */
    uint32_t get_tune_step() const;

    /**
     * Nudge frequency up/down by current tune step. No-op without an
     * attached radio.
     * @param direction +1 for up, -1 for down
     */
    void nudge_frequency(int direction);

    /**
     * Get PTT state.
     * @return radio_->is_transmitting() if a radio is attached, else false
     */
    bool get_ptt_state() const;

    // ── LQA data access ───────────────────────────────────────────────────

    /**
     * Get all LQA entries (for GUI rendering).
     * @return vector of LQA entry strings (GUI-facing format)
     */
    std::vector<std::string> get_all_lqa_entries() const;

    // ── Timing parameters ─────────────────────────────────────────────────
    // Only the values ale_timing.h itself marks as "Level 5 — Programmable
    // defaults" (network-manager-overridable) are exposed here. Call timeout,
    // handshake timeout, AMD wait, turn-around and listen time map onto
    // Level 3/4 spec-fixed protocol-limit / equipment-class constants instead —
    // see docs/GUI_BRIDGE_GAPS.md for why those are intentionally not settable.

    /** Set scan dwell time in milliseconds (ALEStateMachine::configure_scan()). */
    void set_scan_dwell_ms(uint32_t ms);

    /** Set periodic sounding interval in seconds (LQAAnalyzer::AnalyzerConfig). */
    void set_sounding_interval_sec(uint32_t sec);

    /**
     * Set link-idle timeout in seconds — Twa, ALEStateMachine::set_link_idle_timeout_ms().
     * Governs both LINKED-state inactivity auto-termination and the HANDSHAKE
     * safety-net timeout.
     */
    void set_link_idle_timeout_sec(uint32_t sec);

    /** Set the blind-tune delay in milliseconds — ALEStateMachine::set_tune_delay_ms(). */
    void set_max_tune_time_ms(uint32_t ms);

    // ── Audio device management ──────────────────────────────────────────
    // set_audio_input_device()/set_audio_output_device() are intentionally
    // NOT here — see docs/GUI_BRIDGE_GAPS.md (AudioDevice's lifecycle is owned
    // by whoever calls set_audio_device(), not by the controller).

    /** Enumerate available audio input devices (AudioDevice::list_devices(), "IN: " entries). */
    std::vector<std::string> enumerate_audio_inputs() const;

    /** Enumerate available audio output devices (AudioDevice::list_devices(), "OUT: " entries). */
    std::vector<std::string> enumerate_audio_outputs() const;

    /**
     * Get current input audio level (RMS-ish peak, 0.0–1.0).
     * Tracks the same RX peak used by set_debug_rx()'s diagnostics, but
     * unconditionally (not gated on debug_rx_).
     */
    float get_audio_input_level() const;

    // ── Rig / Hamlib management ──────────────────────────────────────────
    // set_rig_backend()/set_rig_tcp_config()/set_rig_serial_config() are
    // intentionally NOT here — see docs/GUI_BRIDGE_GAPS.md (the IRadio
    // instance is constructed by whoever calls set_radio(), not by the
    // controller; rig backend/port is a create_radio() factory-string concern).

    /** Test rig connection — radio_->is_ready() if a radio is attached, else false. */
    bool test_rig_connection() const;

    /** Get rig connection status as a human-readable string. */
    std::string get_rig_connection_status() const;

    // ── Signal quality ────────────────────────────────────────────────────
    // get_sync_lock_status() is intentionally NOT here — see
    // docs/GUI_BRIDGE_GAPS.md (no defined "frequency lock" concept in the
    // demodulator to report on).

    /**
     * Get current signal quality metrics.
     * snr_db/ber/votes/fec_errors come from the most recently received valid
     * word. sinad_db/multipath_ms come from the LQA database entry for the
     * current channel+peer, when one exists (0 otherwise — genuinely unknown,
     * not a placeholder); multipath_ms is approximated from LQAEntry's
     * multipath_score (0.0–1.0 severity), not a measured delay.
     */
    struct SignalQuality {
        float snr_db = 0.0f;       // Estimated SNR in dB
        float sinad_db = 0.0f;     // SINAD in dB
        float ber = 0.0f;          // Bit error rate
        float multipath_ms = 0.0f; // Multipath delay in ms
        int8_t votes = 0;          // Unanimous votes (0-48)
        int fec_errors = 0;        // FEC correction count
    };
    SignalQuality get_current_signal_quality() const;

    /**
     * Get call duration in seconds (0 if not in link).
     */
    uint32_t get_call_duration_seconds() const;

    /**
     * Get overall link status.
     * @return true if link is active
     */
    bool is_link_active() const;

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

    // ── Spectrum / waterfall ────────────────────────────────────────────────
    using SpectrumCallback = ALE2GModem::Demodulator::SpectrumCallback;

    /**
     * Register a callback for FFT spectrum data (waterfall / GUI).
     *
     * Fired ~10 times/second from the audio capture thread with 257 magnitude
     * bins covering 0–4000 Hz (15.625 Hz/bin).  The ALE channel (750–2500 Hz)
     * maps to bins ≈ 48–160.
     *
     * Pass nullptr to unregister.  The callback must be short or hand the data
     * off to a lock-free queue — it runs on the audio thread.
     *
     * Typical consumer (browser WebSocket server):
     *   ctrl.set_spectrum_callback([&ws](const float* b, size_t n, float hz) {
     *       ws.send_binary(b, n * sizeof(float));
     *   });
     */
    void set_spectrum_callback(SpectrumCallback cb) {
        demodulator_.set_spectrum_callback(std::move(cb));
    }

    // ── Inspection ──────────────────────────────────────────────────────────
    ALEState    state() const { return sm_.get_state(); }
    std::string self()  const { return self_addr_; }

    /** Current assumed scan-channel count "C" (see initiate_call()'s auto-derivation). */
    uint32_t get_target_scan_channels() const { return sm_.get_target_scan_channels(); }

private:
    ALEStateMachine          sm_;
    ALE2GModem::Modulator    modulator_;
    ALE2GModem::Demodulator  demodulator_;
    AudioDevice*             audio_device_   = nullptr;
    pal::IEventHandler*      event_handler_  = nullptr;
    pal::IRadio*             radio_          = nullptr;
    std::string              self_addr_;
    std::string              last_caller_;   // caller address as it arrives (TIS + DATA)
    bool                     call_alert_fired_ = false;  // on_call_received emitted once per handshake

    // AMD orderwire tracking — active while in HANDSHAKE/WAIT_CYCLE_END
    int          amd_skip_count_ = 0;  // leading-call DATA/REP words to skip (2×(n-1))
    int          amd_seen_count_ = 0;  // DATA/REP words seen since HANDSHAKE entry
    std::string  amd_text_acc_;        // raw AMD chunk bytes (3 per word, untrimmed)

    // LQA
    LQADatabase              lqa_database_;
    LQAAnalyzer              lqa_analyzer_;
    std::vector<Channel>     calling_channels_;  // cached here so initiate_call() can reorder
    std::string              channel_file_;       // auto-save path (empty = no auto-save)

    // Net / Contact / Self-address tables (GUI-facing address book + scanning-call sizing)
    NetStore                 net_store_;
    ContactStore             contact_store_;
    SelfAddressStore         self_address_store_;
    std::string              selected_contact_;

    // Manual VFO bookkeeping (operator convenience, not radio state — see
    // "Radio / VFO control"; the actual frequency/mode lives in radio_).
    int                      manual_channel_idx_ = -1;   // index into calling_channels_ set by step_channel(); -1 = none
    uint32_t                 tune_step_hz_       = 1000; // nudge_frequency() step size

    // RX diagnostics (set_debug_rx)
    bool                     debug_rx_   = false;
    int                      dbg_peak_   = 0;   // running peak |sample| since last report
    uint32_t                 dbg_count_  = 0;   // samples accumulated since last report

    // Audio input level (get_audio_input_level) — independent of debug_rx_,
    // updated unconditionally on every feed_audio() call.
    float                    audio_input_level_ = 0.0f;

    uint32_t                 lqa_update_ms_ = 0;

    // Call/link timing (get_call_duration_seconds, is_link_active)
    uint32_t                 now_ms_        = 0;  // cached at top of update()
    uint32_t                 link_start_ms_ = 0;  // set at LINK_ESTABLISHED; 0 = not linked

    // Last received word stats (get_current_signal_quality)
    float                    last_snr_db_     = 0.0f;
    float                    last_ber_        = 0.0f;
    uint8_t                  last_votes_      = 0;
    int                      last_fec_errors_ = 0;

    void wire_callbacks();
    void on_sm_state_change(ALEState from, ALEState to);
    void on_operator_event(OperatorEvent ev);

    /**
     * If target_addr is a registered Contact with a net membership that
     * resolves to a known Net, derive target_scan_channels from that net's
     * scan/sounding-enabled channel count (net_scan_channel_count()) and push
     * it to sm_. No-op when no contact/net mapping is known.
     */
    void apply_target_scan_channels_for(const std::string& target_addr);
    void on_received_word(const ALEWord& word);
    /**
     * Emit the incoming-call alert (on_call_received / ALE_CALL_RECEIVED) and any
     * collected AMD exactly once per handshake, once the caller's conclusion has
     * fully settled (SM left WAIT_CYCLE_END) so the reported address is complete.
     * Called from update() after driving the state machine.
     */
    void maybe_emit_call_alert();
    void emit_status(const std::string& msg);
    void emit_event(pal::EventType type, const std::string& msg = "", int32_t code = 0);
};

} // namespace ale
