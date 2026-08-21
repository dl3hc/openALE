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
#include "Modem/channel_occupancy.h"
#include "App/audio_device.h"
#include "App/ale_station_config.h"
#include "PAL/events.h"
#include "App/ale_event_data.h"
#include "LQA/lqa_database.h"
#include "LQA/lqa_history.h"
#include "LQA/lqa_analyzer.h"
#include "LQA/lqa_metrics.h"
#include "LQA/lqa_exchange.h"
#include "App/freq_select_manager.h"
#include "App/sounding_identity_accumulator.h"
#include "Stores/ale_data_store.h"
#include <functional>
#include <iosfwd>
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
    /** Outbound channel list last pushed to the SM by initiate_call/group_call.
     *  Exposed so the active-net scoping can be inspected/tested. */
    const std::vector<Channel>& get_calling_channels() const { return sm_.get_calling_channels(); }

    /**
     * Add a channel to the calling list (replace if same rx_frequency_hz exists).
     * Immediately reconfigures the SM and auto-saves to station_file_ if set.
     */
    bool add_channel(const Channel& ch);

    /** Remove the channel with the given RX frequency.  Returns false if not found. */
    bool del_channel(uint32_t rx_hz);

    /** Rename a channel's id (old_id → new_id) and propagate to every net's
     *  membership list. Rejects empty/whitespace new_id, collisions with an
     *  existing channel id, and unknown old_id. No-op (true) if new == old.
     *  Reconfigures the SM and auto-saves station_file_ if set. */
    bool rename_channel(const std::string& old_id, const std::string& new_id);

    /** Toggle a channel's \c enabled flag by id. Disabling removes it from the
     *  scan list (start_scanning() skips !enabled channels). Reconfigures the SM
     *  and auto-saves station_file_ if set. Returns false if the id is unknown. */
    bool set_channel_enabled(const std::string& id, bool enabled);

    /** Override a channel's RX/TX mode by id (e.g. USB → USB-D for radios that
     *  route digital-mode audio to the PC). Reconfigures the SM, auto-saves
     *  station_file_ if set, and re-asserts the new mode on the radio live if it
     *  is the currently-active channel. Returns false if the id is unknown. */
    bool set_channel_mode(const std::string& id, const std::string& mode);

    /** Read-only access to the current channel list. */
    const std::vector<Channel>& channels() const { return calling_channels_; }

    /**
     * Load a station file (.ale): channels, nets, contacts, group rosters, allcall config.
     * Lines starting with '#' are ignored.  Unknown tags are silently skipped (forward compat).
     * Purely one-shot: does NOT arm auto-save (use load_state()/set_station_file() for that) —
     * loading a shared preset must not silently repoint auto-save at the preset file.
     * \return false if the file cannot be opened (non-fatal; existing state kept).
     */
    bool load_station_file(const std::string& path);
    bool load_channels(const std::string& path) { return load_station_file(path); }  // compat alias

    /**
     * Save all station data to a .ale file (channels, nets, contacts, group rosters, allcall).
     * \return false on I/O error.
     */
    bool save_station_file(const std::string& path) const;
    bool save_channels(const std::string& path) const { return save_station_file(path); }  // compat alias

    /**
     * Set the station file path for auto-save.
     * When set, any mutation (channels, contacts, rosters…) writes through to this file.
     */
    void set_station_file(const std::string& path) { station_file_ = path; }
    void set_channel_file(const std::string& path) { station_file_ = path; }  // compat alias

    /**
     * Load the unified auto-save state file: channels/nets/contacts/rosters/
     * allcall (station-file body) plus all settings (export_settings body),
     * from one file. Best-effort on both halves — a missing/partial file is
     * not an error (e.g. first run). Unconditionally arms auto-save at \p path
     * (every subsequent mutation and settings change is saved back here),
     * regardless of whether anything was actually found to load.
     */
    bool load_state(const std::string& path);

    /**
     * Save the unified auto-save state file (see load_state()): a version
     * header, then the station-file body, then the settings body, all in
     * one file. \return false on I/O error.
     */
    bool save_state(const std::string& path) const;

    // ── Nets (A.5.5.4.3 group membership / scanning-call sizing) ───────────
    // A net is a named subset of channel IDs (see Channel::id). Used to derive
    // target_scan_channels automatically for calls to a registered Contact
    // (see add_contact()) — see initiate_call()'s docs. Auto-saved to
    // station_file_ alongside the channel list (NET: lines), same as channels.

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

    /** Update per-net policy (dwell, scan/sound enables, interval, calling-length C). */
    bool update_net(const Net& updated);

    /** Rename a net (old_name → new_name) and propagate to every place that
     *  references a net by name: active_scan_net_, auto_sounding_net_, and every
     *  Contact::net_members list. Reconfigures the sounding timer if the renamed
     *  net was the auto-sounding net. Auto-saves station_file_ if set. */
    bool rename_net(const std::string& old_name, const std::string& new_name);

    /** Active scan/sound net — set by the GUI net picker; scopes start_scanning() to this net's
     *  channels and dwell. Falls back to all enabled channels when empty. */
    void        set_active_scan_net(const std::string& name) { active_scan_net_ = name; }
    std::string get_active_scan_net() const                  { return active_scan_net_; }

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

    // ── LQA history (append-only, ale_monitor Propagation Analysis) ───────────
    // Separate from the LQA database above: lqa_database_ blends every new
    // measurement into one row per (frequency, station) for live channel
    // scoring; lqa_history_ keeps every raw measurement forever (subject to
    // retention) purely to feed history/trend views. Neither store touches
    // the other.

    /** Configure retention window (days) / enable flag for LQA history recording. */
    void set_lqa_history_config(uint32_t retention_days, bool enabled);

    /**
     * Load LQA history from @p path (call at startup) and keep the file open
     * in append mode so subsequent measurements are persisted as they occur.
     * \return false if the file does not exist yet (not an error — no prior
     *         history) or could not be opened for appending.
     */
    bool load_lqa_history(const std::string& path);

    /**
     * Query recorded LQA history.
     * @param since_ms  0 = no lower time bound (epoch ms)
     * @param station   empty = no station filter
     * @param freq_hz   0 = no frequency filter
     * @param limit     0 = unlimited, else at most this many (most-recent-first)
     */
    std::vector<LQAHistorySample> get_lqa_history(uint64_t since_ms,
                                                   const std::string& station,
                                                   uint32_t freq_hz,
                                                   size_t limit) const;

    /** Wipe LQA history in memory and on disk (separate from clear_lqa()). */
    bool clear_lqa_history(const std::string& path);

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
     * over the net's scan/sounding-enabled channels every interval, whenever the
     * SM is IDLE or SCANNING. The interval is taken from the net's own
     * sounding_interval_sec policy (Nets panel "Auto-Sound every Xs"); it falls
     * back to the global config_.sounding_interval_sec when the net is missing or
     * its value is 0. Pass @p on=false (or an empty @p net_name) to stop. The
     * GUI's Sound-panel toggle drives this; the net's own interval drives the
     * cadence (live-updated via refresh_auto_sounding_interval on NET_UPDATE).
     */
    void set_automatic_sounding(bool on, const std::string& net_name);
    // Auto-sounding state getters — used by the bridge SOUND_AUTO_GET so the
    // GUI can reflect core state after a settings import (no clobbering push).
    bool        is_automatic_sounding() const        { return auto_sounding_on_; }
    uint32_t    get_auto_sounding_interval_sec() const { return auto_sounding_interval_ms_ / 1000u; }
    std::string get_auto_sounding_net() const          { return auto_sounding_net_; }

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

    /** Dispatch a group call to the named roster's members. */
    bool initiate_group_call(const std::string& roster_name);

    /**
     * Stub: initiate a broadcast AllCall. Builds TO address @?@ (global wildcard)
     * or @<selector>@ (selective). TX path not yet wired — logs a warning and
     * returns false. RX side (is_allcall_address → CALL_DETECTED → HANDSHAKE
     * silent link) already works.
     */
    bool initiate_all_call(char selector = '?');

    // ── Group-call rosters ───────────────────────────────────────────────────

    bool add_group_roster(const std::string& name);
    bool del_group_roster(const std::string& name);
    bool add_group_member(const std::string& roster_name, const std::string& callsign);
    bool del_group_member(const std::string& roster_name, const std::string& callsign);
    const std::vector<GroupCallRoster>& get_all_group_rosters() const { return group_call_store_.all(); }

    // ── AllCall config ───────────────────────────────────────────────────────

    void set_allcall_accept(bool on)  { all_call_config_.accept = on; }
    bool get_allcall_accept() const   { return all_call_config_.accept; }
    void set_allcall_selector(char s) { all_call_config_.selector = s; }
    char get_allcall_selector() const { return all_call_config_.selector; }

    /**
     * Reject an incoming call with a TWAS frame.
     * Only valid during HANDSHAKE state; no-op otherwise.
     */
    void reject_call();

    /** Terminate the current link (if any). */
    void terminate_link();

    /**
     * Reset the LINKED-state idle (Twa) timer to the current clock and re-arm
     * the idle warning.  Backs the GUI "reset timer" popup: restarts the full
     * configured idle countdown.  No-op when not linked.
     */
    void reset_link_idle_timer();

    /**
     * Send an AMD orderwire message (MIL-STD-188-141B A.5.7.2).
     *
     * Two paths, decided by link state:
     *  - LINKED: the AMD is carried in a linked-orderwire frame
     *    `TO[peer] (+DATA/REP ext) + CMD AMD + message DATA/REP + TIS self`,
     *    sent SINGLE (not doubled) so the peer decodes the text once. The active
     *    peer (get_to_address()/get_caller_address()) is used; @p target is ignored.
     *    @p link_after_send is meaningless here (already linked) and ignored.
     *  - not LINKED: the AMD is queued as the pending message and a call is
     *    initiated to @p target. Ion2G-style: the message rides the calling
     *    frame itself (delivered before the handshake completes), and the
     *    third handshake frame's TIS/TWAS conclusion — controlled by
     *    @p link_after_send — decides whether a link persists afterward.
     *    Default false: message delivered, no link (matches "just send a
     *    message" intent; a real link never forms). Pass true for "select
     *    peer, send this message, and link" in one step.
     *
     * @p target            Peer address to call when not LINKED (Basic-38, 3–15
     *                      chars). Ignored when LINKED.
     * @p text               Message text, max 90 chars Expanded-64 (0x20–0x5F);
     *                      sanitised and truncated by encode_amd().
     * @p link_after_send    Not-LINKED path only: true = frame 3 concludes with
     *                      TIS (link persists, normal LINKED outcome); false
     *                      (default) = TWAS (handshake concludes, no link).
     * @return "OK: ..." on success, "ERROR: ..." on validation failure.
     */
    std::string send_amd(const std::string& target, const std::string& text,
                          bool link_after_send = false);

    /**
     * Queue an AMD message on a group call: places it as the pending message
     * then initiates the group call, so the AMD rides the calling frame
     * exactly as send_amd()'s not-linked individual-call path does. Unlike
     * send_amd(), this makes a single attempt only — no retry-budget
     * bookkeeping (amd_retry_active_ etc.); a plain group call has no
     * auto-retry today either, so a single-attempt group AMD send matches
     * existing group-call semantics rather than adding a new capability.
     * @p link_after_send    Frame 3 conclusion: true = TIS (link persists),
     *                      false (default) = TWAS (fire-and-forget).
     * @return "OK: ..." on success, "ERROR: ..." on validation/state failure.
     */
    std::string send_amd_group(const std::vector<std::string>& members, const std::string& text,
                                bool link_after_send = false);

    /**
     * One-shot ALE-GPR/AMD position-report broadcast to the AllCall address
     * (A.5.5.4.4) — transmits once, concludes TWAS, no response wait, no
     * retry. send_amd() delegates here transparently when \p target is the
     * literal string "ALLCALL" (see ALEStationConfig::position_report_target),
     * so callers normally just call send_amd(); this is exposed separately
     * for direct use by tick_position_report() and the manual "Send Position"
     * broadcast path.
     * @return "OK: ..." on success, "ERROR: ..." on failure (no self address
     *         configured, not IDLE/SCANNING, or a broadcast already in flight).
     */
    std::string broadcast_position_report(const std::string& text, bool link_after_send = false);

    /**
     * The currently linked peer address (get_to_address() falling back to
     * get_caller_address()), or empty when not LINKED. Used by send_amd() and
     * exposed to the GUI via the bridge STATUS reply.
     */
    std::string active_peer() const;

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

    /// TEST-ONLY: feed a decoded word into the RX pipeline as if it had just
    /// arrived from the demodulator, bypassing audio/FEC. Lets unit tests
    /// drive the controller's RX-side accumulators (AMD, LQA, caller identity,
    /// …) without a real audio device. Not used by production code paths.
    void test_inject_rx_word(const ALEWord& word) { on_received_word(word); }

    /**
     * Feed PCM audio captured from the sound card (8 kHz, mono, 16-bit).
     * Passes samples to the RX pipeline; may trigger word_cb_ → SM word receive.
     */
    void feed_audio(const int16_t* samples, uint32_t count);

    // ── Event bus ───────────────────────────────────────────────────────────
    // All runtime events are dispatched via the global pal::IEventHandler.
    // Subscribe before calling start_available() / start_scanning():
    //   pal::get_event_handler()->on(pal::EventType::ALE_LINK_ESTABLISHED, cb);
    // Event types and their payload structs (ale::*Data in ale_event_data.h):
    //   ALE_STATUS          — message = status string
    //   ALE_LINK_ESTABLISHED — message = peer address
    //   ALE_CALL_RECEIVED   — message = caller address
    //   ALE_LINK_TERMINATED — message = reason string
    //   ALE_CALL_SENT       — message = target address
    //   PTT_ON / PTT_OFF    — (no payload)
    //   ALE_IDLE_WARNING    — code = remaining_sec
    //   CHANNEL_CHANGED     — data = const Channel* (synchronous)
    //   ALE_SOUNDING_WARNING — data = const SoundingWarningData* (synchronous)
    //   ALE_AMD_RECEIVED    — data = const AmdData* (synchronous)
    //   ALE_WORD_DECODED    — data = const WordData* (synchronous)
    //   ALE_WORD_TX         — data = const WordData* (synchronous)
    //   ALE_FRAME_DECODED   — data = const FrameData* (synchronous)
    //   ALE_SOUNDING        — message = net name

    // ── GUI interfaces (CLI uses process_command) ────────────────────────

    /**
     * Process a text command from operator (CLI, GUI, or remote control).
     *
     * Supported commands:
     *
     *   -- Call/link control --
     *   CMD:CALL <ADDR>              initiate individual call to ADDR
     *   CMD:SINGLE_CALL <ADDR>       force single-channel call (no scanning)
     *   CMD:TEST_CHANNEL <ADDR> [net] actively test all channels to a peer (LQA sweep)
     *   CMD:TEST_CHANNEL_STOP        abort an in-progress test-channel sweep
     *   CMD:GROUP_CALL <ROSTER>      call all members of named roster
     *   CMD:AMD <ADDR> <text>        send AMD orderwire; when LINKED, <ADDR> is
     *                                ignored and text goes to active peer
     *   CMD:TERMINATE                terminate current link
     *   CMD:ACCEPT                   accept incoming call (manual-accept mode)
     *   CMD:REJECT                   reject incoming call with TWAS
     *   CMD:RESET_IDLE_TIMER         reset the link idle watchdog
     *   CMD:EMERGENCY_STOP           abort TX and reset immediately
     *   CMD:SET_PTT on|off           manual PTT override
     *
     *   -- Scanning --
     *   CMD:START_SCANNING           start channel scanning (alias: CMD:SCAN)
     *   CMD:STOP_SCANNING            stop scanning, return to IDLE
     *                                (alias: CMD:AVAILABLE)
     *   CMD:SET_SCAN_NET <name>      set active scan net (empty = all channels)
     *   CMD:STATUS                   return current SM state name
     *
     *   -- Sounding --
     *   CMD:SOUND                    send a sounding on current channel
     *   CMD:SOUND_SWEEP <net>        sounding sweep over a net's channels
     *   CMD:SOUND_AUTO on|off [net]  enable/disable automatic sounding
     *   CMD:SOUND_INTERRUPT <net>    cancel an in-progress sounding sweep
     *
     *   -- Channels --
     *   CMD:ADD_CHANNEL rx_hz[:tx_hz] [mode] [label]
     *   CMD:DEL_CHANNEL rx_hz
     *   CMD:LIST_CHANNELS
     *   CMD:CLEAR_CHANNELS
     *   CMD:RENAME_CHANNEL <old_id> <new_id>
     *   CMD:SAVE_CHANNELS [path]
     *   CMD:LOAD_CHANNELS <path>
     *
     *   -- Nets --
     *   CMD:ADD_NET <name>
     *   CMD:DEL_NET <name>
     *   CMD:ASSIGN_CHANNEL <net> <id>
     *   CMD:UNASSIGN_CHANNEL <net> <id>
     *   CMD:LIST_NETS
     *
     *   -- Contacts --
     *   CMD:LIST_CONTACTS
     *   CMD:ADD_CONTACT <callsign> [name]
     *   CMD:DEL_CONTACT <callsign>
     *   CMD:SELECT_CONTACT <callsign>
     *
     *   -- Self addresses --
     *   CMD:LIST_SELF_ADDRS
     *   CMD:ADD_SELF_ADDR <addr>
     *   CMD:DEL_SELF_ADDR <addr>
     *   CMD:SET_PRIMARY_ADDR <addr>
     *
     *   -- LQA --
     *   CMD:CLEAR_LQA
     *
     *   CMD:HELP                     print full command list
     *
     * Returns a human-readable result string:
     *   "OK: ..."    on success
     *   "ERROR: ..."  on invalid command or wrong state
     *   "STATUS: ..." for CMD:STATUS
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
     * Direct access to the SM's AddressBook (callsign + name pairs only).
     * Prefer this over get_all_contacts() for bridge/GUI use.
     */
    AddressBook&       get_address_book()       { return sm_.get_address_book(); }
    const AddressBook& get_address_book() const { return sm_.get_address_book(); }

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
    bool     get_manual_accept_mode() const  { return config_.manual_accept_mode; }
    uint32_t get_accept_timeout_ms() const    { return config_.accept_timeout_ms; }

    // ── Station position + propagation context ────────────────────────────

    /** Set manual lat/lon and mark source as MANUAL. */
    void set_station_position_manual(double lat_deg, double lon_deg);
    /** Parse a Maidenhead grid locator, derive lat/lon, set source to MAIDENHEAD.
     *  @return false if the grid string is invalid (source and position unchanged). */
    bool set_station_position_grid(const std::string& grid);
    /** Change the active position source. */
    void set_position_source(ALEStationConfig::PositionSource src);
    /** Configure gpsd connection parameters (host:port). */
    void set_gpsd_config(const std::string& host, uint16_t port);
    /** Configure NMEA serial port parameters. */
    void set_nmea_config(const std::string& port, uint32_t baud);
    /** Called from the bridge main loop (never from a worker thread) to deliver a
     *  GPS fix update from GpsService. Updates the propagation context. */
    void set_gps_fix(bool valid, double lat_deg, double lon_deg);
    /** Called from the bridge main loop to deliver a new SFI value from SfiService. */
    void set_current_sfi(float sfi);
    /** Called from the bridge main loop (polled each tick, not event-driven —
     *  see apps/ale_bridge.cpp's GPS drain block) to mirror GpsService's
     *  altitude/raw-GGA state into the controller for tick_position_report().
     *  Scoped only to feed ALE-GPR/GGA report generation, same as GpsService's
     *  own has_altitude()/alt(); not wired into LQA/propagation scoring. */
    void set_gps_altitude(bool has_altitude, double altitude_m);
    void set_gps_raw_gga(const std::string& raw_gga);

    ALEStationConfig::PositionSource get_position_source() const { return config_.position_source; }
    double             get_station_lat()    const { return config_.station_lat_deg; }
    double             get_station_lon()    const { return config_.station_lon_deg; }
    const std::string& get_grid_locator()   const { return config_.grid_locator; }
    bool               has_gps_fix()        const { return gps_fix_valid_; }
    double             get_gps_lat()        const { return gps_lat_; }
    double             get_gps_lon()        const { return gps_lon_; }
    bool               has_gps_altitude()   const { return gps_fix_valid_ && gps_has_altitude_; }
    double             get_gps_altitude()   const { return gps_altitude_m_; }
    const std::string& get_gps_raw_gga()    const { return gps_raw_gga_; }
    float              get_current_sfi()    const { return current_sfi_; }

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

    /** True if a pal::IRadio is currently attached (see set_radio()). Use
     * this to distinguish "no radio" from "radio attached but at 0 Hz"
     * before trusting get_current_frequency() in a context that must not
     * silently treat 0 as a real reading (e.g. the rigctld-compat server). */
    bool has_radio() const;

    /** Get current radio mode as string (see get_current_channel()). */
    std::string get_current_mode() const;

    /// Look up a configured channel by RX frequency in calling_channels_.
    /// Returns nullptr if no configured channel matches. Use this to read the
    /// per-channel inhibit flags — get_current_channel() is radio-backed and
    /// does NOT carry them (it is rebuilt from radio_->get_channel()).
    const Channel* find_channel_by_freq(uint32_t rx_hz) const;

    /// True if the configured channel at rx_hz has inhibit_reporting set
    /// (bilateral LQA CMD 'a' exchange suppressed). False if the channel is
    /// not found or the flag is clear.
    bool reporting_inhibited(uint32_t rx_hz) const;

    /// True if the configured channel at rx_hz has inhibit_sounding set.
    bool sounding_inhibited(uint32_t rx_hz) const;

    /// True if the configured channel at rx_hz has inhibit_calling set.
    bool calling_inhibited(uint32_t rx_hz) const;

    /// True if the configured channel at rx_hz is rx_only (Direction=RX):
    /// all transmit is suppressed (sounding, calling, handshake response,
    /// manual PTT). False if not found or the flag is clear.
    bool tx_inhibited(uint32_t rx_hz) const;

    /// True if the configured channel at rx_hz is tx_only (Direction=TX):
    /// receive is suppressed (excluded from scan, RX kept disabled).
    bool rx_inhibited(uint32_t rx_hz) const;

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
     * Set TX RF power live, and persist it into the calling-channel entry the
     * radio is currently tuned to (frequency match), so the value survives the
     * next hop/scan back to this channel — power_pct is a real per-channel
     * setting, not an override independent of it. Clamped to [0,100]. If the
     * current frequency doesn't match any configured channel (e.g. free VFO
     * tuning), the change is live-only — nothing to persist it on. On a radio
     * backend that doesn't support power control (see
     * power_control_supported()), the command is rejected outright — check
     * power_control_supported() before presenting this as available
     * (RF-safety requirement). Either outcome (rejected as unsupported, or
     * accepted and sent) is announced via emit_status() so the operator sees
     * it in the ALE log, not just in the backend's CAT-level log/console output.
     * @param pct Power level 0-100%
     * @return false if no radio is attached, or the radio doesn't support power control
     */
    bool set_power(int pct);

    /**
     * Get current RF power in percent (see get_current_channel()).
     * @return radio_->get_channel().power if a radio is attached, else 100
     */
    uint8_t get_current_power() const;

    /**
     * True if the attached radio's CAT backend can actually apply RF power
     * commands to the hardware (e.g. hamlib rig_has_set_level(RFPOWER) was
     * true for the connected rig). False (including no radio attached) means
     * set_power()/a channel's power_pct are silently not applied — callers
     * (GUI, bridge) must reflect this rather than presenting a power control
     * that appears to work but doesn't.
     */
    bool power_control_supported() const;

    /**
     * Atomically tune the attached radio to an explicit frequency AND mode in a
     * SINGLE set_channel() call — the exact path scanning (sm_.set_channel_callback)
     * and step_channel() use. Manual channel selection must go through here, NOT
     * set_frequency()+set_mode() as two separate commands: over TCP an SDR
     * front-end (Quisk) restores a per-band saved mode on the frequency change, so
     * only one atomic freq-first/mode-last set reliably makes openALE authoritative.
     * Simplex (RX=TX). Empty mode keeps the radio's current mode.
     * @param hz   Frequency in Hz
     * @param mode Mode string (USB, LSB, USB-D/PKTUSB, LSB-D/PKTLSB, FM, …)
     * @return false if no radio is attached or hz == 0
     */
    bool set_vfo_channel(uint32_t hz, const std::string& mode);

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

    /**
     * @brief Read-only access to the LQA database (test/diagnostic introspection).
     *
     * Exposed so tests can assert the LQA-purity invariant (A.5.4.1.1/A.5.4.1.2): that
     * TX-side events (transmitting a sounding, initiating a call) create no LQA entries,
     * while received measurements do. Not used by the GUI bridge.
     */
    const LQADatabase& lqa_database() const { return lqa_database_; }

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

    /** Set the pre-sounding countdown lead time in seconds (config_.sounding_warning_lead_sec). */
    void set_sounding_warning_lead_sec(uint32_t sec);
    uint32_t get_sounding_warning_lead_sec() const { return config_.sounding_warning_lead_sec; }

    /** Link-intent defaults (TIS invites a link, TWAS = fire-and-forget) per destination type. */
    void set_link_default_individual(bool v) { config_.link_default_individual = v; }
    void set_link_default_group(bool v)      { config_.link_default_group = v; }
    void set_link_default_allcall(bool v)    { config_.link_default_allcall = v; }
    bool get_link_default_individual() const { return config_.link_default_individual; }
    bool get_link_default_group() const      { return config_.link_default_group; }
    bool get_link_default_allcall() const    { return config_.link_default_allcall; }

    /**
     * Interrupt the pending idle-sounding countdown on @p net. Re-arms that
     * net's timer to its full original interval (per spec: "set back to its
     * original value") and emits a "cancel" phase so the GUI hides the popup.
     * No-op if @p net is not the active sounding net. If the SM is already in
     * SOUNDING (sweep in flight) it is too late to interrupt — reports that.
     */
    void interrupt_sounding(const std::string& net);

    /**
     * Start an active Test-Channel sweep to @p target: link to the peer on each
     * configured channel in turn, run the bilateral LQA exchange, record the
     * resulting metrics into the LQA database, terminate the link, and advance
     * to the next channel until every channel in the net has been tested.
     *
     * Unlike periodic sounding (passive, peer-agnostic), this actively measures
     * the current per-channel quality to a specific peer, feeding the LQA
     * channel-selection logic with fresh data rather than relying on possibly
     * stale database entries.
     *
     * @p net selects the channel set (a net's callable channels). Empty @p net
     *        uses the active scan net; if that is also empty, all callable
     *        channels are used. Channels that are inhibit-calling or RX-only are
     *        skipped (they cannot place a call).
     * @return false if @p target is invalid, no callable channels resolve, or
     *         the SM is not in IDLE/SCANNING.
     */
    bool start_test_channel(const std::string& target, const std::string& net = "");

    /** Abort an in-progress Test-Channel sweep: emergency-stop the current call
     *  and end the routine. No-op if no sweep is in progress. */
    void stop_test_channel();

    /** Is a Test-Channel sweep currently in progress? */
    bool test_channel_active() const { return test_active_; }

    /**
     * Set link-idle timeout in seconds — Twa, ALEStateMachine::set_link_idle_timeout_ms().
     * Governs both LINKED-state inactivity auto-termination and the HANDSHAKE
     * safety-net timeout.
     */
    void set_link_idle_timeout_sec(uint32_t sec);

    /** Set the Test-Channel linked dwell time in seconds.
     *  The sweep stays LINKED on each channel for at least this long (floor: Tdrw = 784 ms)
     *  so bilateral LQA metrics commit before the link is terminated and the next
     *  channel begins. */
    void set_test_channel_link_hold_time(uint32_t sec) { config_.test_channel_link_hold_time = sec; }

    /** Set the blind-tune delay in milliseconds — ALEStateMachine::set_tune_delay_ms(). */
    void set_max_tune_time_ms(uint32_t ms);

    /** PTT lead time in ms — delay between PTT assertion and first audio TX word. */
    void set_ptt_lead_ms(uint32_t ms);
    /** PTT tail time in ms — delay between SM RX-enable and actual PTT release. */
    void set_ptt_tail_ms(uint32_t ms);

    // Timing/TWAS getters — read config_ so they reflect imported settings
    // (the setters above all write config_). Used by the bridge TIMING_GET.
    uint32_t  get_scan_dwell_ms() const          { return config_.scan_dwell_ms; }
    uint32_t  get_sounding_interval_sec() const { return config_.sounding_interval_sec; }
    uint32_t  get_link_idle_timeout_sec() const  { return config_.link_idle_timeout_sec; }
    /** AMD delivery-confirmation retry budget (attempts before "not heard"). */
    void      set_amd_send_max_attempts(uint32_t n) { config_.amd_send_max_attempts = n; }
    uint32_t  get_amd_send_max_attempts() const      { return config_.amd_send_max_attempts; }
    uint32_t  get_test_channel_link_hold_time() const { return config_.test_channel_link_hold_time; }
    uint32_t  get_max_tune_time_ms() const       { return config_.max_tune_time_ms; }
    uint32_t  get_ptt_lead_ms() const            { return config_.ptt_lead_ms; }
    uint32_t  get_ptt_tail_ms() const             { return config_.ptt_tail_ms; }
    bool      get_sounding_use_twas() const       { return config_.sounding_use_twas; }

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

    /** Query the radio for its live frequency/mode and update internal channel state.
     *  Returns true if anything changed.  No-op when no radio is attached. */
    bool sync_radio_state();

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
        bool word_locked = false;  // Demodulator word-grid lock (P1-11 diagnostics)
        bool decoding = false;     // word_locked AND a valid word committed within
                                    // DECODE_ACTIVE_WINDOW_MS (P1-11 diagnostics)
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

    /// True while the modem has a TX burst in progress: PTT lead, pending words,
    /// or modulator actively transmitting. Used by AudioTransport::arbitrate_tx_()
    /// to preempt voice passthrough and restore the symbol source.
    bool is_tx_active() const;

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

    /// Diagnostics: when on, relay the attached radio's CAT/rig traffic
    /// (rigctld/Hamlib commands, responses/errors, timing — see
    /// pal::IRadio::drain_cat_trace) into the status stream as "[CAT] ..."
    /// lines. Off by default; radios with no CAT trace of their own (mocks)
    /// simply never produce any.
    void set_cat_trace(bool on);
    bool cat_trace() const                       { return cat_trace_; }

    // ── LBT occupancy detection (A.5.4.7 listen-before-transmit) ─────────────
    // Broadband busy detector (ChannelOccupancyDetector) consulted by the SM in
    // all three LBT windows, in addition to the ALE-word busy path.  The busy
    // margin is operator-settable so high local noise never permanently blocks
    // TX (the EWMA floor self-adapts to steady noise; the margin covers
    // impulsive QRM; the A.5.4.7.3 override is the hard bypass).
    void  set_lbt_margin_db(float db);            ///< dB over tracked noise floor (default 6)
    float lbt_margin_db() const;
    void  set_lbt_occupancy_enabled(bool on)     { lbt_occupancy_enabled_ = on; }
    bool  lbt_occupancy_enabled() const          { return lbt_occupancy_enabled_; }

    // §A.5.3.3 stage-1 operator squelch (PR2): calibrated, audio-path-independent
    // sensitivity control for the scan-stop detector (default OFF ⇒ level-invariant
    // detector unchanged). margin_db = how far a signal must sit above the learned
    // global noise floor before scanning stops on it. See ALE2GModem::Demodulator.
    void  set_scan_squelch_enabled(bool on);
    bool  scan_squelch_enabled() const;
    void  set_scan_detect_margin_db(float db);
    float scan_detect_margin_db() const;
    float scan_floor_db() const;                  ///< live learned in-band noise floor (dB)
    float scan_floor_baseline_db() const;         ///< last operator-calibrated snapshot (dB)
    float calibrate_scan_detector();              ///< snapshot the live floor; returns it (dB)
    /// Gate occupancy detection off during voice PTT. While voice PTT is active
    /// the radio is transmitting and the VAC loopback would drive the occupancy
    /// detector to BUSY — identical to the ALE-TX case where the demodulator is
    /// disabled. Call with transport.media_tx_active() each main-loop tick.
    void  set_voice_tx_active(bool on)           { voice_tx_active_ = on; }
    void  set_lbt_override(bool on);              ///< A.5.4.7.3 operator override (emergency)
    bool  lbt_override() const;
    bool  lbt_busy() const;                       ///< current occupancy-busy state (diagnostics)
    float lbt_level_db() const;                   ///< last 100 ms block level (diagnostics)
    float lbt_floor_db() const;                   ///< tracked noise floor (diagnostics)

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

    // ── LQA bilateral exchange (StationConfig::lqa_exchange_enabled, A.5.4.2) ──
    /// Enable/disable the active bilateral CMD 'a' (LQA request) exchange sent
    /// during calling/handshake. false = EMCON/Debug: no CMD 'a' transmitted;
    /// FROM measurements (lqa_enabled above) continue unaffected.
    /// Disabling also drops any already-queued CMD 'a'/CMD 'r' still sitting in
    /// the SM's pending_lqa_cmd_/pending_lqa_report_seq_ slots (queued while the
    /// exchange was still on, e.g. by an earlier call attempt or a call-alert
    /// that fired moments before the operator flipped this off) — otherwise the
    /// gate here only stops *new* words from being queued and a stale one still
    /// goes out on the next transmitted frame.
    void set_lqa_exchange_enabled(bool on) {
        config_.lqa_exchange_enabled = on;
        if (!on) {
            sm_.clear_pending_lqa_cmd();
            sm_.clear_pending_lqa_report_seq();
        }
    }
    bool lqa_exchange_enabled() const            { return config_.lqa_exchange_enabled; }

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
    CallingPhase get_calling_phase() const { return sm_.get_calling_phase(); } ///< TEST-ONLY inspection (mirrors state())

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

    // Shared AMD accumulator core (A.5.7.2.2): CMD discrimination + accumulate,
    // instantiated once per standard-mandated AMD window below. See amd_step()/
    // amd_flush()/amd_dispatch(). A.5.7.2.2: "The receiving station shall be
    // capable of receiving an AMD message contained in any ALE frame, including
    // calls, responses, and acknowledgments" — hence four windows, not two.
    struct AmdAccumulator {
        bool        collecting = false;  // true between CMD AMD and TIS/next CMD
        std::string acc;                 // Expanded-64 chars (3 per word, untrimmed)
    };

    // Calling-frame AMD RX — active while HANDSHAKE/WAIT_CYCLE_END (A.5.7.2.2,
    // called-station side; also covers AllCall/AnyCall message sections, same
    // window). The peer isn't reliably known until the caller's TIS conclusion
    // settles (multi-word addresses), so completed messages are buffered here
    // and dispatched once, in on_sm_state_change() when HANDSHAKE is left —
    // independent of whether the handshake goes on to link or times out
    // (A.5.7.2.3 requires display "upon arrival", not upon link success).
    AmdAccumulator           call_amd_;
    std::vector<std::string> call_amd_pending_;
    // Snapshot of sm_.is_allcall_handshake() taken at HANDSHAKE entry — by the
    // time HANDSHAKE is left (where call_amd_pending_ is dispatched), the SM
    // has already reset allcall_silent_ on entering IDLE/SCANNING/LINKED, so
    // the flag must be captured up front instead of read at dispatch time.
    const char*              handshake_call_context_ = "INDIVIDUAL";

    // Response-frame AMD RX — active while CALLING/LISTENING (A.5.7.2.2,
    // calling-station side, receiving JOE's response). Like the calling-frame
    // window, the peer isn't reliably known until the responder's own TIS is
    // processed by the SM (sm_.get_to_address(), populated in react_calling_'s
    // TIS_CALLER case) — which happens *after* this accumulator sees the same
    // word (rx_accumulate_resp_amd runs before sm_.process_received_word() in
    // on_received_word()). So completed messages are buffered here and
    // dispatched once, in on_sm_state_change() when CALLING is left.
    AmdAccumulator           resp_amd_;
    std::vector<std::string> resp_amd_pending_;

    // ACK-frame AMD RX — active while in HANDSHAKE/WAIT_ACK (A.5.7.2.2,
    // responder side).  The caller's ACK frame is TO[self]×2 + CMD AMD + DATA/REP
    // + TIS[caller]; we ignore the TO prefix and reassemble from CMD AMD onward.
    AmdAccumulator ack_amd_;
    std::string    ack_amd_peer_;                // caller to attribute the message to

    // Linked AMD orderwire RX — active while LINKED (A.5.7.2 over an established link).
    // The peer's linked-orderwire frame is TO[peer]+CMD AMD+DATA/REP+TIS[self-peer];
    // we ignore the TO/address-extension prefix and reassemble from CMD AMD onward.
    AmdAccumulator linked_amd_;
    std::string    linked_amd_peer_;              // peer to attribute the message to
    uint32_t       linked_amd_settle_ms_ = 0;      // last-word time; Tdrw-silence commit deadline

    // ── AMD delivery confirmation + retry ────────────────────────────────────
    // Call→Response→ACK confirmation, both paths reusing the SM handshake:
    //   • Not-linked path: the SM's own CALLING→LISTENING→SENDING_ACK handshake IS
    //     the 3 frames; the Response is observed via on_operator_event()
    //     (LINK_ESTABLISHED / AMD_SENT_NO_LINK / CALL_REJECTED = heard;
    //     NO_CHANNELS_LEFT = not heard → retry). The fields below track that retry.
    //   • Linked path: owned entirely by the SM's LINKED-AMD confirm sub-phases
    //     (send_linked_amd / respond_to_linked_amd, see ale_state_machine.cpp) —
    //     no controller-side state; outcomes arrive as OperatorEvent::AMD_DELIVERED
    //     / AMD_RETRY / AMD_NOT_DELIVERED.
    bool        amd_retry_active_          = false;  ///< a not-linked send is awaiting its verdict
    uint32_t    amd_attempts_remaining_    = 0;      ///< counts down from amd_send_max_attempts
    // Not-linked resend context + reentrancy-safe deferred re-call (NO_CHANNELS_LEFT
    // fires on the SM callback stack — never re-call the SM inline; defer via this
    // pending flag checked in tick_amd_confirm, exactly like pending_relink_addr_):
    std::string amd_retry_target_;
    std::string amd_retry_text_;
    bool        amd_retry_link_after_send_ = false;
    bool        amd_retry_pending_recall_  = false;  ///< tick_amd_confirm resends when IDLE/SCANNING
    uint32_t    amd_retry_recall_after_ms_ = 0;      ///< inter-attempt gap deadline (TT_NEXT_TRY)

    // LQA
    LQADatabase              lqa_database_;
    LQAHistoryStore          lqa_history_;        // append-only, separate from lqa_database_ (see accessors above)
    LQAAnalyzer              lqa_analyzer_;
    LQAMetrics               lqa_metrics_;        // standalone noise-floor tracking (no DB)
    LQAMetrics               lqa_db_metrics_;     // connected to lqa_database_; fed into the SM
    std::vector<Channel>     calling_channels_;  // cached here so initiate_call() can reorder
    std::string              station_file_;       // auto-save path (empty = no auto-save)

    // Net / Contact / Self-address tables (GUI-facing address book + scanning-call sizing)
    NetStore                 net_store_;
    ContactStore             contact_store_;
    SelfAddressStore         self_address_store_;
    std::string              selected_contact_;
    GroupCallStore           group_call_store_;
    AllCallConfig            all_call_config_;

    // Bilateral LQA exchange (CMD 'a' / Block C5) — see LQA/lqa_exchange.h.
    // Declared after self_address_store_ so the is_self lambda is safe to call
    // for the full controller lifetime.
    LqaExchangeManager       lqa_exchange_;

    // Manual VFO bookkeeping (operator convenience, not radio state — see
    // "Radio / VFO control"; the actual frequency/mode lives in radio_).
    uint32_t                 tune_step_hz_       = 1000; // nudge_frequency() step size

    // Passive channel monitor (on_word_decoded / on_frame_decoded)
    uint32_t                 monitor_frame_id_ = 0; // increments each time a frame completes
    uint32_t                 tx_word_seq_      = 0; // TX monitor sequence (on_word_tx)

    // RX diagnostics (set_debug_rx)
    bool                     debug_rx_   = false;
    int                      dbg_peak_   = 0;   // running peak |sample| since last report
    uint32_t                 dbg_count_  = 0;   // samples accumulated since last report

    // Word-lock diagnostics (P1-11): timestamp of the last VALID committed word
    // (on_received_word), used to derive SignalQuality::decoding — "actively
    // decoding" vs. merely grid-locked between words. 0 = none yet this session.
    uint32_t                 last_word_decoded_ms_ = 0;

    // CAT-traffic diagnostics (set_cat_trace) — see tick_cat_trace()
    bool                     cat_trace_  = false;

    // LBT occupancy detection (A.5.4.7.2) — fed from feed_audio() while RX is
    // enabled; queried by the SM via set_channel_busy_query.
    ChannelOccupancyDetector occupancy_;
    bool                     lbt_occupancy_enabled_ = true;
    bool                     voice_tx_active_        = false;  // set by bridge during voice PTT
    bool                     lbt_busy_reported_     = false;  // edge-detect for status emission
    bool                     lbt_relearn_was_active_ = false; // edge-detect for relearn-settled diagnostic

    /// A.5.4.7.1: set SM shared/ALE-only LBT duration from the channels involved.
    void apply_lbt_policy_(const std::vector<Channel>& channels);

    /// Centralize every channel-change side effect: reset the LBT occupancy
    /// detector when the RX frequency actually changes (its EWMA floor is
    /// channel-specific — carrying it across a hop leaves a stale floor/busy
    /// from the previous channel), then forward to the on_channel_changed
    /// callback.  A same-frequency notification (e.g. a channel rename) does
    /// NOT reset, so no needless blind window.
    void notify_channel_changed_(const Channel& ch);

    uint32_t                 last_lbt_rx_hz_ = 0;  // RX freq at last detector reset

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

    // Automatic ALE-GPR/GGA position reporting (tick_position_report, see
    // ALEStationConfig::position_report_mode). Mirrors the auto-sounding
    // timer shape: last_report_ms_ re-arms only on a completed fire.
    uint32_t last_report_ms_               = 0;
    double   last_reported_lat_            = 0.0;
    double   last_reported_lon_            = 0.0;
    bool     has_last_reported_position_   = false;
    std::string              auto_sounding_net_;
    std::string              active_scan_net_;        ///< Active net for start_scanning() scoping
    uint32_t                 auto_sounding_last_ms_  = 0;
    // True while an auto-sounding sweep is in flight (SM in SOUNDING). The timer
    // is re-armed at cycle END (on_sm_state_change leaving SOUNDING), not at fire
    // time, so the configured interval is the gap AFTER the sounding cycle.
    bool                     sounding_cycle_active_  = false;
    // Pre-sounding countdown state (idle→sounding warning popup).
    uint32_t                 sounding_warning_lead_ms_  = 10000; ///< matches config_.sounding_warning_lead_sec * 1000
    bool                     sounding_warning_sent_     = false; ///< one-shot per cycle (prevents re-fire)
    bool                     sounding_warning_active_   = false; ///< popup is open; call pre-empt closes it

    // ── GPS / SFI state (propagation-aware LQA scoring) ──────────────────────
    double   gps_lat_       = 0.0;
    double   gps_lon_       = 0.0;
    bool     gps_fix_valid_ = false;
    bool        gps_has_altitude_ = false;
    double      gps_altitude_m_   = 0.0;
    std::string gps_raw_gga_;
    float    current_sfi_   = 0.0f;

    // Call/link timing (get_call_duration_seconds, is_link_active)
    uint32_t                 now_ms_        = 0;  // cached at top of update()
    uint32_t                 link_start_ms_ = 0;  // set at LINK_ESTABLISHED; 0 = not linked

    // PTT timing (set_manual_ptt, wire_callbacks, update)
    uint32_t                 ptt_lead_deadline_ms_ = 0;   // flush pending_tx_words_ when now_ms_ >= this; 0 = inactive
    uint32_t                 ptt_tail_deadline_ms_ = 0;   // release PTT when now_ms_ >= this; 0 = inactive
    // Delay armed into ptt_tail_deadline_ms_ above (config_.ptt_tail_ms +
    // output_latency_ms() at arm time) — replayed into the SM via
    // extend_peer_wait_window_for_ptt_release_delay() once the deferred
    // release actually fires, so the SM's own "waiting for peer" timers
    // account for it too.
    uint32_t                 ptt_tail_armed_delay_ms_ = 0;

    // Deferred radio mode verify (schedule_mode_verify, tick_mode_verify).
    // An SDR front-end can revert a just-commanded mode asynchronously after
    // set_channel() returns; these checks call radio_->sync_from_radio() a few
    // hundred ms later so the intended mode is re-asserted promptly (the radio
    // backend guards the actual re-send: freq-still-matches + recency window).
    //
    // Two modes: while SCANNING, a fixed-cadence BACKGROUND verify runs
    // decoupled from the per-hop re-arm (a 200 ms dwell would otherwise supersede
    // the +300 ms one-shot deadline before it ever fires, so the backstop never
    // ran while scanning). sync_from_radio is a CmdSync — it never touches
    // tunes_in_flight_, so it never gates hop_ready / the hop rate. Non-scanning
    // one-shot ops (manual step, net select, set_mode) keep the multi-check path.
    uint32_t                 mode_verify_deadline_ms_ = 0;      // one-shot: next check when now_ms_ >= this; 0 = inactive
    int                      mode_verify_checks_left_ = 0;      // one-shot: remaining checks for this activation
    uint32_t                 mode_verify_scan_deadline_ms_ = 0; // SCANNING bg cadence: next verify; 0 = arm on first scan tick
    bool                     scan_was_settled_     = false; // §A.5.3.3: last is_tune_settled() while SCANNING — arm stage-1 on the settle edge
    bool                     sm_rx_enabled_        = true; // mirrors SM's last rx_enabled_callback value
    bool                     manual_ptt_           = false;
    bool                     abort_tx_pending_     = false; // rx_only: abort SM TX on next update() tick (avoids re-entering SM inside set_rx_enabled_callback)
    std::vector<std::pair<ALEWord, bool>> pending_tx_words_; // word + had_audio_device flag, buffered during PTT lead

    // Last received word stats (get_current_signal_quality)
    float                    last_sinad_db_   = 0.0f;  ///< actual Goertzel SINAD (A.5.4.1.2)
    float                    last_snr_db_     = 0.0f;  ///< votes-based SNR proxy (internal use)
    float                    last_ber_        = 0.0f;
    uint8_t                  last_votes_      = 0;
    int                      last_fec_errors_ = 0;

    // Received-sounding address reassembly (A.5.3.1): a sounding transmission
    // repeats its self-address conclusion (TIS/TWAS anchor + DATA/REP
    // extension words) several times for redundancy. SoundingIdentityAccumulator
    // owns per-slot cross-cycle voting (a later repeat's lost extension word
    // no longer discards an earlier repeat's fully-assembled address) and
    // per-cycle positional acceptance (DATA/REP alternation by chunk index,
    // no arrival-order splicing). Full address committed to the LQA DB once
    // the burst settles (Tdrw of silence) — see commit_sounding_result() and
    // tick_frame_settle(). snr/ber/sinad are averaged across the whole
    // session's words (A.5.4.1.1 "linear average BER/LQA").
    SoundingIdentityAccumulator sounding_accumulator_;

    // Run-time tunables (Stores/ale_data_store.h). Only lqa_enabled is consulted
    // here — it gates the per-frame FROM-direction BER/SNR measurement into the
    // LQA Memory (A.5.4.1.1). The struct's other fields are covered by
    // ALEStationConfig / dedicated setters and remain unused in this instance.
    OperatingParameters      op_params_;

    // Auto-Relink: set in evaluate_relink() when a better channel is found;
    // consumed in update() once SM returns to IDLE/SCANNING after TWAS.
    std::string              pending_relink_addr_;

    // Enhanced Frequency-Select (CMD 'f', A.5.6.3.2) post-link bilateral negotiation.
    FreqSelectManager        freq_select_;

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
    // FrameQualityAccumulator defers Golay-uncorrectable words so a trailing
    // post-frame phantom (decoded during the Tlww settle) is excluded from the
    // BER/SNR/SINAD averages unless a later valid word flushes it.
    FrameQualityAccumulator  hs_resp_acc_;
    uint32_t                 hs_resp_freq_hz_   = 0;

    // A.5.4.1.1 FROM-direction BER for the SAM's calling frame
    // (HANDSHAKE/HANDSHAKE::WAIT_CYCLE_END). Committed to (get_caller_address(),
    // freq) in maybe_emit_call_alert() so compute_lqa_payload() can immediately
    // read the fresh measurement for the response CMD 'a'. Reset on entering
    // HANDSHAKE and when leaving HANDSHAKE without an alert. Gated on
    // op_params_.lqa_enabled.
    FrameQualityAccumulator  hs_call_acc_;
    uint32_t                 hs_call_freq_hz_   = 0;

    /// Rebuild and push a PropagationContext to lqa_analyzer_ from current config_/GPS/SFI state.
    void update_propagation_context();

    void wire_callbacks();
    void on_sm_state_change(ALEState from, ALEState to);
    void on_operator_event(OperatorEvent ev);

    // Assert/release PTT on the radio and notify the GUI via on_ptt_changed.
    // Centralizes the radio_->set_ptt() + GUI-callback pair so every PTT
    // transition (SM-driven and manual) reaches the display.
    void set_ptt_and_notify(bool on);

    /// Resolve a net's scan/sounding-enabled channels (by id, from
    /// calling_channels_) for a multi-channel sounding sweep. Empty if the net
    /// doesn't exist or has no enabled member channels.
    std::vector<Channel> resolve_net_sounding_channels(const std::string& net_name) const;

    /// Resolve the scan-channel count "C" for a sounding on @p net_name — the SAME
    /// call-width value calling uses (Tsc = C·2·Trw), taken from the net's
    /// calling_length_c policy.  Falls back to the auto-sounding net, then the active
    /// scan net, then config_.assumed_scan_channels when @p net_name is empty or the
    /// named net is absent.  The SM uses this as `n` in Tsrs = (n+2)·Ta (see
    /// handle_sounding), so sounding and calling share one configurable scan-channel
    /// count instead of the SM's own (arbitrary) scan-channel count.
    uint32_t resolve_sounding_C(const std::string& net_name) const;

    /// Resolve a net's *callable* channels (by id, from calling_channels_) for a
    /// Test-Channel sweep — filters inhibit_calling and rx_only (a test call must
    /// be able to transmit). Empty @p net_name falls back to active_scan_net_,
    /// then to all callable channels. Empty result if no callable channels.
    std::vector<Channel> resolve_net_call_channels(const std::string& net_name) const;

    // ── Test-Channel sweep (active per-peer LQA collection) ────────────────
    // Async driver advanced in tick_test_channel() from update(); reuses
    // initiate_single_channel_call() / terminate_link() / set_vfo_channel().
    enum class TestPhase {
        INACTIVE,
        TUNE,          ///< Tune radio to the next channel, then place a single-channel call
        CALLING,       ///< Wait for LINKED (success) or return to IDLE/SCANNING (fail/timeout)
        LINKED_SETTLE, ///< Link up; dwell one Tdrw so bilateral/FROM metrics settle
        TERMINATING,   ///< terminate_link() sent; wait for SM to leave LINKED
        NEXT,          ///< Wait for IDLE/SCANNING, advance index, re-enter TUNE or DONE
        DONE
    };
    bool                 test_active_          = false;
    TestPhase            test_phase_           = TestPhase::INACTIVE;
    std::string          test_target_;
    std::string          test_net_;            ///< Net the sweep is scoped to (for status)
    std::vector<Channel> test_channels_;
    size_t               test_idx_             = 0;
    uint32_t             test_phase_start_ms_  = 0;
    uint32_t             test_link_deadline_ms_ = 0;  ///< per-channel no-reply timeout
    bool                 test_was_scanning_    = false; ///< restore scanning on completion
    uint32_t             test_orig_freq_hz_     = 0;    ///< channel the radio was on at start
    std::string          test_orig_mode_;               ///< mode at start (for restore)
    struct TestResult {
        uint32_t    freq_hz = 0;
        std::string id;
        bool        linked = false;
        int         score  = -1;   ///< LQA score (-1 = no measurement)
    };
    std::vector<TestResult> test_results_;
    std::string test_summary_;      ///< built on DONE for the final event

    void emit_test_event_(const char* phase, const Channel* ch, int score, bool linked);
    /// Snapshot the LQA entry (freq, peer) score into test_results_[test_idx_].
    /// No-op if no entry exists (score stays -1). Called after LINKED_SETTLE.
    void snapshot_test_score_(const Channel& ch);
    /// Tune the radio back to the channel it was on when the sweep started.
    /// No-op when no original frequency was recorded (or no radio attached).
    void restore_test_channel_();

    /// Re-read the active sounding net's own sounding_interval_sec into
    /// auto_sounding_interval_ms_. Called when the active net's policy is
    /// updated live so the running timer reflects the new value immediately.
    void refresh_auto_sounding_interval();

    /// Body-writers shared between the standalone save_station_file()/
    /// export_settings() and the composed save_state() — write into an
    /// already-open stream so save_state() can concatenate both into one file.
    void write_station_body(std::ostream& f) const;
    void write_settings_body(std::ostream& f) const;
    void on_received_word(const ALEWord& word);

    // ── update() concern handlers ─────────────────────────────────────────────
    void tick_ptt_timing(uint32_t now_ms);      ///< PTT lead flush + PTT tail release
    void tick_sm(uint32_t now_ms);              ///< sm_.update() + maybe_emit_call_alert()
    void tick_frame_settle(uint32_t now_ms);    ///< sounding commit + RX-BER commit (Tdrw)
    void tick_relink(uint32_t now_ms);          ///< EFS evaluate + EFS tick + auto-relink fire
    void tick_amd_confirm(uint32_t now_ms);     ///< not-linked AMD deferred retry re-call (linked path lives in the SM)
    void tick_sounding_sweep(uint32_t now_ms);  ///< periodic multi-channel sounding sweep
    void tick_position_report(uint32_t now_ms); ///< automatic ALE-GPR/GGA on-change/interval reporting
    void tick_test_channel(uint32_t now_ms);    ///< active per-peer Test-Channel sweep driver
    void tick_offline_completion();             ///< pull symbol frames in offline (no-audio) mode
    void tick_lqa_update(uint32_t now_ms);      ///< throttled LQA DB prune + auto-sounding check
    void tick_mode_verify(uint32_t now_ms);     ///< deferred radio-mode verify after channel activation
    void tick_cat_trace(uint32_t now_ms);       ///< drains radio_->drain_cat_trace() into the status stream

    /// Arm the deferred mode-verify checks after any non-scanning radio
    /// channel/mode command. Re-arming on every command means checks only fire
    /// once activity pauses — a fast scan (dwell < first check delay) keeps
    /// superseding them, so while SCANNING this is a no-op and tick_mode_verify
    /// instead runs a fixed-cadence background verify decoupled from the hops.
    void schedule_mode_verify();

    // ── on_received_word() concern handlers ──────────────────────────────────
    void rx_log_word(const ALEWord& word);                   ///< debug_rx_ trace line
    void rx_track_signal_quality(const ALEWord& word);       ///< last_sinad_db_ (Goertzel) / last_snr_db_ (votes proxy) / ber_ + passive monitor tap
    void rx_accumulate_caller_identity(const ALEWord& word); ///< HANDSHAKE caller address reassembly
    void rx_handle_lqa_exchange(const ALEWord& word);        ///< CMD 'a' / CMD 'n' / CMD 'r'+DATA
    void rx_handle_freq_select(const ALEWord& word);         ///< CMD 'f' EFS sequence
    void rx_accumulate_sounding(const ALEWord& word);        ///< TIS/TWAS sounding while SCANNING/IDLE
    void rx_accumulate_frame_ber(const ALEWord& word);       ///< LINKED/CALLING/HANDSHAKE BER paths
    // Shared AMD core (A.5.7.2.2) — see AmdAccumulator above.
    std::string amd_step(AmdAccumulator& st, const ALEWord& word);            ///< CMD discrimination + accumulate; returns trimmed text when a message just concluded (TIS/TWAS or a superseding CMD)
    std::string amd_flush(AmdAccumulator& st);                                ///< force-conclude an in-progress accumulation (best-effort exit / silence fallback)
    void        amd_dispatch(const std::string& peer, const std::string& text, const char* call_context); ///< fires ALE_AMD_RECEIVED iff both are non-empty

    void rx_accumulate_call_amd(const ALEWord& word);        ///< calling-frame AMD reassembly (A.5.7.2.2, called-station side); dispatch deferred to on_sm_state_change()
    void rx_accumulate_resp_amd(const ALEWord& word);        ///< response-frame AMD reassembly (A.5.7.2.2, calling-station side); dispatch deferred to on_sm_state_change()
    void rx_accumulate_linked_amd(const ALEWord& word);      ///< LINKED-state AMD reassembly (A.5.7.2)
    void commit_linked_amd();                               ///< amd_dispatch(linked_amd_peer_, amd_flush(linked_amd_)) — Tdrw-silence fallback call site
    void rx_accumulate_ack_amd(const ALEWord& word);         ///< ACK-frame AMD reassembly (A.5.7.2.2, responder)
    void commit_ack_amd();                                  ///< amd_dispatch(ack_amd_peer_, amd_flush(ack_amd_))

    // AMD delivery confirmation
    std::string attempt_amd_send_(const std::string& target, const std::string& text,
                                  bool link_after_send);     ///< one not-linked call attempt (factored from send_amd)
    void reply_to_linked_amd_(const std::string& sender);    ///< receiver: kick off the SM's linked-AMD Response (supplies the optional CMD 'a')
    void amd_confirm_clear_();                               ///< reset the not-linked AMD retry state (linked path lives in the SM)

    /**
     * Emit the incoming-call alert (on_call_received / ALE_CALL_RECEIVED) and any
     * collected AMD exactly once per handshake, once the caller's conclusion has
     * fully settled (SM left WAIT_CYCLE_END) so the reported address is complete.
     * Called from tick_sm() after driving the state machine.
     */
    void maybe_emit_call_alert();
    /// Commit a finalized SoundingIdentityAccumulator::Result to the LQA DB:
    /// applies the self-address guard (never store our own callsign, per
    /// MIL-STD Fig. A-27) then calls LQAAnalyzer::process_sounding(). Called
    /// from tick_frame_settle() after sounding_accumulator_.finalize() (Tdrw
    /// silence), from rx_accumulate_sounding() when on_word() flushes a
    /// different station's finished session (anchor mismatch), and from the
    /// handshake call sites that flush partial sounding metrics before
    /// marking bilateral/call-concluded state.
    void commit_sounding_result(const SoundingIdentityAccumulator::Result& r);
    /// Evaluate whether the current link should be renegotiated to a better channel.
    /// Called from tick_relink() while LINKED + relink_enabled.
    void evaluate_relink(uint32_t now_ms);
    /// Commit the accumulated non-sounding RX-frame BER/SNR (A.5.4.1.1 linear
    /// average) to the LQA DB for the measured peer + channel, then reset the
    /// accumulator. No-op if no words are buffered or no peer is resolvable.
    /// Called from tick_frame_settle() once the frame has settled (Tdrw silence).
    void commit_rx_ber_sample();
    /// Append an LQAHistorySample for (freq_hz, station) to lqa_history_, reading
    /// the freshly-committed sinad/ber/score back from lqa_database_. Called right
    /// after every lqa_database_.update_entry_extended(freq_hz, station, ...) site.
    /// No-op if lqa_database_ has no entry for the key (should not happen, since
    /// update_entry_extended always creates one).
    void record_lqa_history(uint32_t freq_hz, const std::string& station);
    void emit_status(const std::string& msg);
    void dispatch(pal::EventType type, const std::string& msg = "",
                  int32_t code = 0, const void* data = nullptr, size_t data_size = 0);
};

} // namespace ale
