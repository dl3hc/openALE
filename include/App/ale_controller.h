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
#include "App/ale_station_config.h"
#include "PAL/events.h"
#include "LQA/lqa_database.h"
#include "LQA/lqa_analyzer.h"
#include "LQA/lqa_metrics.h"
#include "LQA/lqa_report.h"
#include "Stores/ale_data_store.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace pal { class IRadio; }

namespace ale {

class ALEController {
public:
    ALEController();

    // ── Configuration ───────────────────────────────────────────────────────

    /**
     * Set this station's call sign (self address).
     * Validates length (3–15 chars) and character set (Basic 38: A–Z, 0–9, @, ?).
     * \return false if the address is invalid; the previous address is kept.
     */
    bool set_self_address(const std::string& addr);

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

    /// Apply a complete station configuration in one call.
    void apply_config(const ALEStationConfig& cfg);
    /// Return the current station configuration (always in sync with subsystems).
    ALEStationConfig get_config() const { return config_; }

    /// Assumed scan channels of the called station — sets Tsc = C × 2 × Trw.
    /// This is the single authority for scanning-call length (default 10 = MIL-STD max).
    void     set_assumed_scan_channels(uint32_t n) { config_.assumed_scan_channels = n; }
    uint32_t get_assumed_scan_channels() const      { return config_.assumed_scan_channels; }

    /// @deprecated Use set_assumed_scan_channels() / apply_config().
    void     set_target_scan_channels(uint32_t n)  { set_assumed_scan_channels(n); sm_.set_target_scan_channels(n); }
    /// @deprecated Use get_assumed_scan_channels().
    uint32_t get_target_scan_channels() const       { return sm_.get_target_scan_channels(); }

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

    /** Clear all LQA entries from memory (does not touch the file on disk). */
    void clear_lqa();

    /**
     * Enable/disable automatic periodic sounding on channels already in the LQA DB.
     * When on, the main loop triggers sm_.send_sounding() whenever a channel's LQA
     * data is older than the sounding_interval (default 5 min) and the SM is
     * in SCANNING or IDLE state.
     */
    void enable_automatic_sounding(bool on);

    /**
     * Enable periodic multi-channel sounding on a named net's channels. When on,
     * the main loop starts a sounding sweep (ALEStateMachine::send_sounding_sweep)
     * over the net's scan/sounding-enabled channels every @p interval_sec, whenever
     * the SM is IDLE or SCANNING. Pass @p on=false (or an empty @p net_name) to
     * stop. This is the GUI's "multi-channel sounding" dropdown mode, driven by
     * the Sounding Interval setting (Settings ▸ Timing).
     */
    void set_automatic_sounding(bool on, uint32_t interval_sec,
                                const std::string& net_name);

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
     * Transmit a one-shot multi-channel sounding sweep over @p channels (each
     * channel sounded in turn). Independent of set_automatic_sounding(); fires
     * immediately when the operator asks. \return false if the SM is not in
     * IDLE/SCANNING or @p channels is empty.
     */
    bool send_sounding_sweep(const std::vector<Channel>& channels);

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
     * Initiate a call restricted to the currently tuned channel only.
     * No LQA reordering, no multi-channel iteration — stays on the current
     * channel regardless of scan-channel configuration.
     * \return false if the SM is not in IDLE or SCANNING state.
     */
    bool initiate_single_channel_call(const std::string& target_addr);

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

    /**
     * Operator-controlled PTT override.
     * When on=true, PTT is asserted and the SM cannot release it.
     * When on=false, SM-driven PTT control is restored.
     */
    void set_manual_ptt(bool on);

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

    /**
     * Passive channel monitor — fires for every successfully decoded ALE word
     * (valid == true), regardless of local protocol state or address match.
     * frame_id groups words that belong to the same assembled frame; it
     * increments each time a frame completes.  Use for the ALE Monitor display.
     */
    std::function<void(const ALEWord& word, uint32_t frame_id)> on_word_decoded;

    /**
     * Passive TX monitor — fires for every ALE word the state machine hands to
     * the modem for transmission (sounding, calling, ack, …), at SM emit time
     * (before PTT-lead buffering / flush).  frame_id is a per-instance TX
     * sequence counter, kept in a distinct space from monitor_frame_id_ so the
     * GUI can group sent and received log rows independently.  Companion to
     * on_word_decoded: same payload shape, opposite direction.  Use for the
     * ALE Monitor "sent" display so sent and received words render consistently.
     */
    std::function<void(const ALEWord& word, uint32_t frame_id)> on_word_tx;

    /**
     * Fires once per complete assembled ALE frame (after the concluding TIS or
     * equivalent word), carrying the full semantic classification (call_type,
     * from_address, to_addresses, words).  Companion to on_word_decoded:
     * the same frame_id links the individual word events to the frame summary.
     */
    std::function<void(const ALEMessage& frame, uint32_t frame_id)> on_frame_decoded;

    /**
     * Active channel changed (scan hop, calling-channel tune, OR a manual VFO
     * change from the operator).  Fires after the radio tune so the GUI can
     * update the frequency/channel readout in the real hop cadence instead of
     * polling.  channel_id is empty for pure VFO tuning (no named channel).
     */
    std::function<void(const Channel& ch)> on_channel_changed;

    /**
     * PTT state changed (SM-driven TX during a call/sounding, or manual PTT).
     * Fires after radio_->set_ptt() so the GUI PTT indicator tracks without
     * polling.  The existing emit_event(PTT_ON/OFF) path goes through a
     * pal::IEventHandler the bridge does not set; this callback is the GUI path.
     */
    std::function<void(bool ptt_on)> on_ptt_changed;

    // ── GUI interfaces (CLI uses process_command) ────────────────────────

    /**
     * Process a text command from operator (CLI, GUI, or remote control).
     *
     * Supported commands:
     *   CMD:CALL <ADDR>   — initiate individual call to ADDR
     *   CMD:AMD <text>    — queue AMD orderwire for the next CMD:CALL (max 90 chars, Expanded-64)
     *   CMD:TERMINATE        — terminate current link
     *   CMD:ACCEPT           — accept incoming call (manual-accept mode only)
     *   CMD:REJECT           — reject incoming call with TWAS
     *   CMD:START_SCANNING   — start channel scanning (alias: CMD:SCAN)
     *   CMD:STOP_SCANNING    — stop scanning, return to IDLE (available)
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
     * decision_timeout_ms, the call is dropped (no response sent) so the
     * caller runs into its own call timeout. Manual accept means manual —
     * an unanswered call must not silently link.
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

    /** Select sounding conclusion type: false = TIS (invites return calls), true = TWAS (announce-only). */
    void set_sounding_use_twas(bool use_twas);

    /**
     * Set link-idle timeout in seconds — Twa, ALEStateMachine::set_link_idle_timeout_ms().
     * Governs both LINKED-state inactivity auto-termination and the HANDSHAKE
     * safety-net timeout.
     */
    void set_link_idle_timeout_sec(uint32_t sec);

    /** Set the blind-tune delay in milliseconds — ALEStateMachine::set_tune_delay_ms(). */
    void set_max_tune_time_ms(uint32_t ms);

    /** PTT lead time in ms — delay between PTT assertion and first audio TX word. */
    void set_ptt_lead_ms(uint32_t ms);
    /** PTT tail time in ms — delay between SM RX-enable and actual PTT release. */
    void set_ptt_tail_ms(uint32_t ms);

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
    void      set_golay_mode(GolayMode m)        { config_.golay_mode = m; demodulator_.set_golay_mode(m); }
    GolayMode golay_mode() const                 { return demodulator_.golay_mode(); }

    /// Minimum unanimous 2/3-vote count to accept a word (0..49; default 33).
    void    set_min_unanimous_votes(uint8_t v)   { config_.min_unanimous_votes = v; demodulator_.set_min_unanimous_votes(v); }
    uint8_t min_unanimous_votes() const          { return demodulator_.min_unanimous_votes(); }

    /// A.5.2.6.3 "DO": auto-adjust Golay mode + unanimous-vote threshold to the
    /// observed signal quality (off by default).
    void set_adaptive_fec(bool on)               { config_.adaptive_fec = on; demodulator_.set_adaptive_fec(on); }
    bool adaptive_fec() const                    { return demodulator_.adaptive_fec(); }

    /// Diagnostics: when on, emit (via on_status_changed) the periodic RX peak
    /// level and every demodulated word (type/address/unanimous/fec).  Use to
    /// see what the receiver actually decodes (e.g. during the LISTENING window).
    void set_debug_rx(bool on)                   { config_.debug_rx = on; debug_rx_ = on; }
    bool debug_rx() const                        { return debug_rx_; }

    // ── Auto-Relink (A.5.4.5 bilateral channel renegotiation) ────────────────
    /// Enable/disable post-link automatic channel renegotiation via TWAS + re-call.
    /// When on and a better-scoring channel is known (from LQA/bilateral data), the
    /// controller terminates the current link and immediately re-initiates the call
    /// on the best available channel. Requires lqa_enabled=true.
    void  set_relink_enabled(bool on)        { config_.relink_enabled = on; }
    bool  relink_enabled() const             { return config_.relink_enabled; }
    void  set_relink_threshold(float score)  { config_.relink_improvement_threshold = score; }
    float relink_threshold() const           { return config_.relink_improvement_threshold; }

    // ── Enhanced Frequency-Select (CMD 'f' bilateral post-link negotiation) ───
    /// When on, proposes better channels to the peer via standard CMD 'f' (A.5.6.3.2)
    /// before TWAS. The peer evaluates its LQA for the proposed channel and echoes
    /// CMD 'f' to accept or sends CMD 'f'(freq=0) to reject. Standard ALE 2G stations
    /// ignore CMD 'f' in LINKED state (per A.5.6.3.2d) — no relink occurs then.
    void set_enhanced_freq_select(bool on) { config_.enhanced_freq_select = on; }
    bool enhanced_freq_select() const      { return config_.enhanced_freq_select; }

    // ── LQA measurement (OperatingParameters::lqa_enabled, A.5.4.1.1) ───────
    /// Enable/disable recording of received-transmission link-quality into the
    /// LQA Memory: the per-frame FROM-direction BER (averaged non-unanimous
    /// 2/3-votes) and SNR, measured for every received transmission after word
    /// sync — soundings and handshake/linked frames alike.  On by default.
    /// Independent of config_.lqa_exchange_enabled, which governs the *active*
    /// CMD-LQA/NOISE/Report exchange (TX). With this off the receiver still
    /// links but stores no new channel-quality data.
    void set_lqa_enabled(bool on)                { op_params_.set_lqa_enabled(on); }
    bool lqa_enabled() const                     { return op_params_.lqa_enabled; }

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

    /**
     * Per-instance display state for the operator UI, derived from THIS station's
     * own state. The calling station never enters the HANDSHAKE state — its
     * 3-way handshake runs inside CALLING (sub-phases LISTENING / SENDING_ACK).
     * Report "HANDSHAKE" for both the called station's HANDSHAKE state and the
     * caller's response-exchange sub-phases, so each side shows
     * calling → handshake → linked from its own perspective. Otherwise returns
     * the raw ALEStateMachine::state_name().
     */
    std::string display_state() const;

private:
    ALEStationConfig         config_;
    ALEStateMachine          sm_;
    ALE2GModem::Modulator    modulator_;
    ALE2GModem::Demodulator  demodulator_;
    AudioDevice*             audio_device_   = nullptr;
    pal::IEventHandler*      event_handler_  = nullptr;
    pal::IRadio*             radio_          = nullptr;
    std::string              self_addr_;
    std::string              last_caller_;   // caller address as it arrives (TIS + DATA)
    bool                     call_alert_fired_ = false;  // on_call_received emitted once per handshake

    // Manual-accept post-link gate (LINKED_PENDING_OPERATOR). When manual-accept
    // mode is on (config_.manual_accept_mode), the 3-way handshake still
    // completes automatically within Twr/Twrt and the link establishes; this
    // flag is then set so the operator's accept_call()/reject_call() act on the
    // *already-established* link instead of gating the handshake. Accept clears
    // it (normal LINKED); reject calls terminate_link() (TWAS → AVAILABLE).
    bool                     pending_operator_accept_ = false;

    // AMD orderwire tracking — active while in HANDSHAKE/WAIT_CYCLE_END
    int          amd_skip_count_ = 0;  // leading-call DATA/REP words to skip (2×(n-1))
    int          amd_seen_count_ = 0;  // DATA/REP words seen since HANDSHAKE entry
    std::string  amd_text_acc_;        // raw AMD chunk bytes (3 per word, untrimmed)

    // LQA
    LQADatabase              lqa_database_;
    LQAAnalyzer              lqa_analyzer_;
    LQAMetrics               lqa_metrics_;        // standalone noise-floor tracking (no DB)
    LQAMetrics               lqa_db_metrics_;     // connected to lqa_database_; fed into the SM
    std::vector<Channel>     calling_channels_;  // cached here so initiate_call() can reorder
    std::string              channel_file_;       // auto-save path (empty = no auto-save)

    // Bilateral LQA exchange (Block A — CMD LQA char 'a')
    LQACmdPayload            pending_bilateral_payload_{};
    bool                     pending_bilateral_valid_    = false;
    uint32_t                 pending_bilateral_freq_hz_  = 0;
    bool                     sent_ka1_                   = false;
    std::string              last_call_target_;
    uint32_t                 last_call_freq_hz_          = 0;

    // LQA Report decoder (Block C — CMD 'r' + DATA)
    LQAReportDecoder         lqa_report_decoder_;

    // Net / Contact / Self-address tables (GUI-facing address book + scanning-call sizing)
    NetStore                 net_store_;
    ContactStore             contact_store_;
    SelfAddressStore         self_address_store_;
    std::string              selected_contact_;

    // Manual VFO bookkeeping (operator convenience, not radio state — see
    // "Radio / VFO control"; the actual frequency/mode lives in radio_).
    int                      manual_channel_idx_ = -1;   // index into calling_channels_ set by step_channel(); -1 = none
    uint32_t                 tune_step_hz_       = 1000; // nudge_frequency() step size

    // Passive channel monitor (on_word_decoded / on_frame_decoded)
    uint32_t                 monitor_frame_id_ = 0; // increments each time a frame completes
    uint32_t                 tx_word_seq_      = 0; // TX monitor sequence (on_word_tx)

    // RX diagnostics (set_debug_rx)
    bool                     debug_rx_   = false;
    int                      dbg_peak_   = 0;   // running peak |sample| since last report
    uint32_t                 dbg_count_  = 0;   // samples accumulated since last report

    // Audio input level (get_audio_input_level) — independent of debug_rx_,
    // updated unconditionally on every feed_audio() call.
    float                    audio_input_level_ = 0.0f;

    uint32_t                 lqa_update_ms_ = 0;

    // Periodic multi-channel sounding (set_automatic_sounding). When enabled with
    // a net, update() starts a send_sounding_sweep() over the net's channels every
    // auto_sounding_interval_ms_, gated on IDLE/SCANNING (a sweep in progress shows
    // as SOUNDING, which naturally blocks a re-entry).
    bool                     auto_sounding_on_       = false;
    uint32_t                 auto_sounding_interval_ms_ = 0;
    std::string              auto_sounding_net_;
    uint32_t                 auto_sounding_last_ms_  = 0;

    // Call/link timing (get_call_duration_seconds, is_link_active)
    uint32_t                 now_ms_        = 0;  // cached at top of update()
    uint32_t                 link_start_ms_ = 0;  // set at LINK_ESTABLISHED; 0 = not linked

    // PTT timing (set_manual_ptt, wire_callbacks, update)
    uint32_t                 ptt_lead_deadline_ms_ = 0;   // flush pending_tx_words_ when now_ms_ >= this; 0 = inactive
    uint32_t                 ptt_tail_deadline_ms_ = 0;   // release PTT when now_ms_ >= this; 0 = inactive
    bool                     sm_rx_enabled_        = true; // mirrors SM's last rx_enabled_callback value
    bool                     manual_ptt_           = false;
    std::vector<std::pair<ALEWord, bool>> pending_tx_words_; // word + had_audio_device flag, buffered during PTT lead

    // Last received word stats (get_current_signal_quality)
    float                    last_snr_db_     = 0.0f;
    float                    last_ber_        = 0.0f;
    uint8_t                  last_votes_      = 0;
    int                      last_fec_errors_ = 0;

    // Received-sounding address reassembly (A.5.3.1): a sounding frame is the
    // self-address conclusion (TIS [DATA|REP]*) sent twice. on_received_word()
    // captures the TIS (first 3 chars) and appends each DATA/REP extension word
    // (chars 4-15); the accumulated full address is committed to the LQA DB once
    // the frame settles (Tdrw of silence after the last word) — mirrors the
    // handshake caller-address reassembly in react_handshake(). Without this the
    // LQA entry was keyed by only the 3-char TIS, truncating >3-char self
    // addresses. snr/ber are averaged across the frame's words (A.5.4.1.1
    // "linear average BER/LQA").
    std::string              sounding_caller_acc_;
    uint32_t                 sounding_freq_hz_    = 0;
    uint32_t                 sounding_settle_ms_  = 0;
    uint32_t                 sounding_word_count_ = 0;
    float                    sounding_snr_sum_    = 0.0f;
    float                    sounding_ber_sum_    = 0.0f;
    float                    sounding_sinad_sum_  = 0.0f;  ///< A.5.4.1.2 SINAD accumulator

    // Run-time tunables (Stores/ale_data_store.h). Only lqa_enabled is consulted
    // here — it gates the per-frame FROM-direction BER/SNR measurement into the
    // LQA Memory (A.5.4.1.1). The struct's other fields are covered by
    // ALEStationConfig / dedicated setters and remain unused in this instance.
    OperatingParameters      op_params_;

    // Auto-Relink: set in evaluate_relink() when a better channel is found;
    // consumed in update() once SM returns to IDLE/SCANNING after TWAS.
    std::string              pending_relink_addr_;

    // Enhanced Frequency-Select (CMD 'f', A.5.6.3.2) post-link bilateral negotiation.
    enum class FreqSelectPhase : uint8_t { IDLE, PROPOSED, EXECUTING };
    FreqSelectPhase          fs_phase_          = FreqSelectPhase::IDLE;
    uint32_t                 fs_proposed_hz_    = 0;
    uint32_t                 fs_timeout_ms_     = 0;
    uint32_t                 fs_cooldown_ms_    = 0;
    std::string              fs_peer_;
    bool                     fs_pending_cmd_rx_ = false;  // CMD 'f' recvd, waiting for DATA

    // A.5.4.1.1 per-frame FROM-direction BER for in-link traffic (LINKED state).
    // Committed at frame settle (Tdrw of silence) via commit_rx_ber_sample().
    // Gated on op_params_.lqa_enabled.
    BerAccumulator           rx_ber_acc_;
    float                    rx_ber_snr_sum_    = 0.0f;
    float                    rx_ber_sinad_sum_  = 0.0f;
    uint32_t                 rx_ber_freq_hz_   = 0;
    uint32_t                 rx_ber_settle_ms_ = 0;

    // A.5.4.1.1 FROM-direction BER for the JOE's response frame
    // (CALLING/CALLING::LISTENING). Committed to (get_to_address(), freq) at
    // LINK_ESTABLISHED (or CALL_REJECTED when JOE sent TWAS). Reset on failed
    // calls and on entering CALLING. Gated on op_params_.lqa_enabled.
    BerAccumulator           hs_resp_ber_acc_;
    float                    hs_resp_snr_sum_   = 0.0f;
    float                    hs_resp_sinad_sum_ = 0.0f;
    uint32_t                 hs_resp_freq_hz_   = 0;

    // A.5.4.1.1 FROM-direction BER for the SAM's calling frame
    // (HANDSHAKE/HANDSHAKE::WAIT_CYCLE_END). Committed to (get_caller_address(),
    // freq) in maybe_emit_call_alert() so compute_lqa_payload() can immediately
    // read the fresh measurement for the response CMD 'a'. Reset on entering
    // HANDSHAKE and when leaving HANDSHAKE without an alert. Gated on
    // op_params_.lqa_enabled.
    BerAccumulator           hs_call_ber_acc_;
    float                    hs_call_snr_sum_   = 0.0f;
    float                    hs_call_sinad_sum_ = 0.0f;
    uint32_t                 hs_call_freq_hz_   = 0;

    void wire_callbacks();
    void on_sm_state_change(ALEState from, ALEState to);
    void on_operator_event(OperatorEvent ev);

    // Assert/release PTT on the radio and notify the GUI via on_ptt_changed.
    // Centralizes the radio_->set_ptt() + GUI-callback pair so every PTT
    // transition (SM-driven and manual) reaches the display.
    void set_ptt_and_notify(bool on);

    // Returns a CMD LQA payload for freq_hz, reporting our FROM-direction
    // measurement of target_station (falls back to channel aggregate when no
    // station-specific entry exists). Fields default to "no-value" sentinels.
    LQACmdPayload compute_lqa_payload(uint32_t freq_hz,
                                       const std::string& target_station = "") const;

    /// Resolve a net's scan/sounding-enabled channels (by id, from
    /// calling_channels_) for a multi-channel sounding sweep. Empty if the net
    /// doesn't exist or has no enabled member channels.
    std::vector<Channel> resolve_net_sounding_channels(const std::string& net_name) const;
    void on_received_word(const ALEWord& word);
    /**
     * Emit the incoming-call alert (on_call_received / ALE_CALL_RECEIVED) and any
     * collected AMD exactly once per handshake, once the caller's conclusion has
     * fully settled (SM left WAIT_CYCLE_END) so the reported address is complete.
     * Called from update() after driving the state machine.
     */
    void maybe_emit_call_alert();
    /// Commit the accumulated received-sounding frame (full address + averaged
    /// snr/ber) to the LQA DB and clear the reassembly buffer. No-op if nothing
    /// is buffered. Called from update() once the frame has settled (Tdrw silence).
    void commit_sounding_sample();
    /// Evaluate whether the current link should be renegotiated to a better channel.
    /// Called from update() while LINKED + relink_enabled. Sets pending_relink_addr_
    /// and calls sm_.terminate_link() when a significantly better channel is found.
    void evaluate_relink(uint32_t now_ms);
    /// EFS: Propose a better channel to the peer via CMD 'f' (A.5.6.3.2).
    /// Called from update() while LINKED + enhanced_freq_select + IDLE phase.
    void evaluate_freq_proposal(uint32_t now_ms);
    /// EFS: Handle the peer's CMD 'f' + DATA response (accept or reject).
    void handle_freq_select_response(uint32_t freq_hz, uint32_t now_ms);
    /// EFS: Handle an incoming CMD 'f' proposal from the peer.
    void handle_freq_select_proposal(uint32_t freq_hz, const std::string& peer, uint32_t now_ms);
    /// EFS: Queue CMD 'f' + DATA(freq_hz) as a linked orderwire via the SM.
    void send_freq_select_orderwire(uint32_t freq_hz);
    /// Commit the accumulated non-sounding RX-frame BER/SNR (A.5.4.1.1 linear
    /// average) to the LQA DB for the measured peer + channel, then reset the
    /// accumulator. No-op if no words are buffered or no peer is resolvable.
    /// Called from update() once the frame has settled (Tdrw silence).
    void commit_rx_ber_sample();
    void emit_status(const std::string& msg);
    void emit_event(pal::EventType type, const std::string& msg = "", int32_t code = 0);
};

} // namespace ale
