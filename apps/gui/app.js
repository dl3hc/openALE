/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   ICON SYSTEM  — inline Lucide-style SVG, stroke:currentColor
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const ICONS = {
  mic:       '<path d="M12 2a3 3 0 0 1 3 3v7a3 3 0 0 1-6 0V5a3 3 0 0 1 3-3z"/><path d="M19 10v2a7 7 0 0 1-14 0v-2"/><line x1="12" y1="19" x2="12" y2="22"/><line x1="8" y1="22" x2="16" y2="22"/>',
  radio:     '<path d="M4.9 19.1C1 15.2 1 8.8 4.9 4.9"/><path d="M7.8 16.2c-2.3-2.3-2.3-6.1 0-8.5"/><circle cx="12" cy="12" r="2"/><line x1="12" y1="12" x2="12" y2="22"/><line x1="8" y1="22" x2="16" y2="22"/>',
  volume2:   '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/>',
  phone:     '<path d="M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92z"/>',
  phoneOff:  '<path d="M10.68 13.31a16 16 0 0 0 3.41 2.6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7 2 2 0 0 1 1.72 2v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.42 19.42 0 0 1-3.33-2.67m-2.67-3.34a19.79 19.79 0 0 1-3.07-8.63A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91"/><line x1="23" y1="1" x2="1" y2="23"/>',
  power:     '<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>',
  settings:  '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>',
  user:      '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
  userPlus:  '<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><line x1="19" y1="8" x2="19" y2="14"/><line x1="22" y1="11" x2="16" y2="11"/>',
  headphones:'<path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/>',
  layers:    '<polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/>',
  share2:    '<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><line x1="8.59" y1="13.51" x2="15.42" y2="17.49"/><line x1="15.41" y1="6.51" x2="8.59" y2="10.49"/>',
  clock:     '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>',
  shield:    '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>',
  barChart2: '<line x1="18" y1="20" x2="18" y2="10"/><line x1="12" y1="20" x2="12" y2="4"/><line x1="6" y1="20" x2="6" y2="14"/>',
  zap:       '<polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>',
  sliders:   '<line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/><line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/><line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/><line x1="1" y1="14" x2="7" y2="14"/><line x1="9" y1="8" x2="15" y2="8"/><line x1="17" y1="16" x2="23" y2="16"/>',
  pencil:    '<path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/>',
  square:    '<rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>',
  wifi:      '<path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/>',
};

function icon(name, size) {
  const paths = ICONS[name] || '';
  const s = size || 14;
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${s}" height="${s}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths}</svg>`;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   BRIDGE CONNECTION  (apps/ale_bridge.cpp — WebSocket ↔ ALEController)

   Every action in this file requires an active bridge connection.
   The UI is locked behind an overlay while the bridge is offline.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let bridgeWs            = null;
let bridgeConnected     = false;
let bridgeReconnectTimer = null;  // guard: at most one pending reconnect
let bridgeReqId      = 0;
const bridgePending  = new Map();   // id -> callback(reply)
let latestSpectrum   = null;        // Float32Array(257) from the last binary frame, or null

const wfMarkers      = [];          // ALE frame markers; aged each waterfall row in drawWaterfall()

let locGpsFix = false;   // last GPS fix state from gps_fix push events
let locGpsLat = 0.0;
let locGpsLon = 0.0;
let locSfi    = 0.0;     // last SFI value from sfi_update push events

function bridgeWsUrl() {
  return 'ws://' + window.location.host;
}

function setBridgeOverlay(show) {
  const el = document.getElementById('bridgeOverlay');
  el.classList.toggle('hidden', !show);
  if (show) {
    const cmd = document.getElementById('bridgeOverlayCmd');
    if (window.location.protocol === 'file:') {
      cmd.textContent = 'Open via http://localhost:PORT/index.html (not file://)';
    } else {
      const port = window.location.port || '…';
      cmd.textContent = 'openALE --port ' + port;
    }
  }
}

// Send {"id":N,"cmd":cmd,...args}; onReply(replyObj) fires when a message
// with that id comes back (replies and async events share the same socket
// and are not guaranteed to arrive in send order — see dispatch_command()'s
// doc comment in apps/ale_bridge.cpp).
function bridgeSend(cmd, args, onReply) {
  if (!bridgeConnected || !bridgeWs) return false;
  const id = ++bridgeReqId;
  if (onReply) bridgePending.set(id, onReply);
  bridgeWs.send(JSON.stringify(Object.assign({ id, cmd }, args || {})));
  return true;
}

function connectBridge() {
  let ws;
  try { ws = new WebSocket(bridgeWsUrl()); }
  catch {
    if (!bridgeReconnectTimer)
      bridgeReconnectTimer = setTimeout(() => { bridgeReconnectTimer = null; connectBridge(); }, 1000);
    return;
  }
  bridgeWs = ws;
  // Spectrum frames arrive as binary; without this they'd be Blobs and the
  // `ev.data instanceof ArrayBuffer` check below would never match → waterfall
  // would never see real FFT data.
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    bridgeConnected = true;
    setBridgeOverlay(false);
    aleLogInfo('Bridge connected — syncing live station state');
    syncAllFromBridge();
  };

  ws.onmessage = (ev) => {
    if (ev.data instanceof ArrayBuffer) { onBinaryFrame(ev.data); return; }
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg.event) { onBridgeEvent(msg); return; }
    if (msg.id != null && bridgePending.has(msg.id)) {
      const cb = bridgePending.get(msg.id);
      bridgePending.delete(msg.id);
      cb(msg);
    }
  };

  ws.onerror = () => {};  // onclose always follows; let it handle cleanup + retry

  ws.onclose = () => {
    if (bridgeConnected) aleLogInfo('Bridge disconnected');
    bridgeConnected = false;
    bridgeWs = null;
    bridgePending.clear();
    applyRigState(false);
    setBridgeOverlay(true);
    if (!bridgeReconnectTimer) {
      bridgeReconnectTimer = setTimeout(() => { bridgeReconnectTimer = null; connectBridge(); }, 1000);
    }
  };
}

function syncAllFromBridge() {
  bridgeSend('STATUS', {}, applyStatusReply);
  syncChannelsFromBridge();
  syncNetsFromBridge();
  syncActiveNetFromBridge();   // restore the header Network pill selection from core
  syncContactsFromBridge();
  syncAllCallAcceptFromBridge();
  syncSelfAddrsFromBridge();
  syncLqaFromBridge();
  syncVfoFromBridge();
  syncFecFromBridge();              // pull FEC (Golay/votes/adaptive) state from core
  syncRelinkFromBridge();           // pull Auto-Relink state from core
  syncLbtFromBridge();              // pull LBT occupancy state from core (A.5.4.7)
  syncScanDetectFromBridge();       // pull scan-stop squelch state + noise floor (A.5.3.3)
  syncEnhFreqSelectFromBridge();    // pull Enhanced Freq-Select state from core
  syncVoiceFromBridge();            // pull voice-passthrough arm/mode state from core
  syncLocationFromBridge();         // pull Station Location & Propagation from core
  pollRigStatus();   // establish initial radio-control lock state
  populateRigDropdown();
  // Always PULL core state into the GUI on connect — never push stale DOM
  // values here. The DOM only reflects whatever the browser last rendered
  // (page-load HTML defaults, or leftovers from a previous session); pushing
  // it back on every connect/reconnect would silently clobber the core's
  // real, persisted config (e.g. PTT lead/tail, auto-accept) with those
  // defaults. Explicit user edits still push via saveSettings()/onchange.
  syncTimingFromBridge();
  syncManualAcceptFromBridge();
  syncSoundAutoFromBridge();
}

// VFO/PTT frequency+mode+PTT display is now event-driven via the
// `channel_changed` / `ptt_changed` push events (see onBridgeEvent). This
// slow poll only covers what has no push event: rig-connection state (an
// external signal openALE can't be notified about — needs a heartbeat) and the
// signal-quality panel. syncVfoFromBridge() is still called on connect
// (syncAllFromBridge) and as a per-command ACK from manual VFO actions.
setInterval(() => {
  if (bridgeConnected) {
    pollRigStatus();
    pollSignalQuality();
    // Pull live freq/mode from the radio on every heartbeat tick so manual
    // operator tuning (Quisk VFO, hardware dial) is reflected in the GUI
    // within ~2 s.  Only relevant when a rig is actually connected.
    if (rigConnected) syncVfoFromBridge();
  }
}, 2000);

// BER-led quality blend, mirroring C++ from_direction_quality()
// (MIL-STD-188-141B A.5.4.1.1 BER primary + A.5.4.1.2 SINAD secondary). berQ and
// sinadQ must already be on the [0,30] "higher = better" scale; pass -1 when
// unknown. BER leads so a flawless decode is never rated "Poor" even when the raw
// leakage-SINAD proxy reads only ~6 dB; SINAD still separates two clean channels.
const BER_LEAD_WEIGHT = 0.7;
function blendQuality(berQ, sinadQ) {
  const hasBer = berQ >= 0, hasSinad = sinadQ >= 0;
  if (hasBer && hasSinad) return BER_LEAD_WEIGHT * berQ + (1 - BER_LEAD_WEIGHT) * sinadQ;
  if (hasBer) return berQ;
  if (hasSinad) return sinadQ;
  return -1;
}
function qualityLabel(q) {
  // Buckets match LQAAnalyzer::score_to_quality_level (lqa_analyzer.cpp:517-522)
  // so the GUI and the ALE log label the same score identically (A.5.4.1: higher=better).
  return q >= 25 ? 'Excellent' : q >= 20 ? 'Good' : q >= 15 ? 'Fair'
       : q >= 10 ? 'Poor' : 'Very Poor';
}

// Reflects the modem's word-grid lock (P1-11) — SIGNAL_QUALITY's word_locked
// + decoding fields — as the header chip next to the main ALE-state status
// chip. Three states: still hunting (idle, scanning, or noise) -> "Acquiring
// lock"; grid-locked but between words -> "Locked"; a valid word landed in
// the last ~1s (see DECODE_ACTIVE_WINDOW_MS server-side) -> "Decoding".
function applyWordLock(locked, decoding) {
  const dot = document.getElementById('wordLockDot');
  const txt = document.getElementById('wordLockText');
  const state = decoding ? 'decoding' : (locked ? 'locked' : 'acquiring');
  const label = decoding ? 'Decoding'  : (locked ? 'Locked'  : 'Acquiring lock');
  if (dot) dot.className = 'dot ' + state;
  if (txt) txt.textContent = label;
}

// Poll SIGNAL_QUALITY for the active-link quality panel (bars + label) and
// the word-lock chip.
function pollSignalQuality() {
  bridgeSend('SIGNAL_QUALITY', {}, (r) => {
    if (!r.ok) return;
    applyWordLock(!!r.word_locked, !!r.decoding);
    const sinad = Math.max(0, Math.min(30, Math.round(r.sinad_db)));
    // Rating is BER-led (A.5.4.1.1): votes = unanimous 2/3 count (0–48, 48=clean),
    // higher=better. SINAD (shown in dB) only refines it, so a flawless decode is
    // never labelled "Poor" just because the leakage-SINAD proxy is low.
    const berQ   = (typeof r.votes === 'number' && r.votes >= 0)
      ? Math.min(48, r.votes) / 48 * 30 : -1;
    const sinadQ = (r.sinad_db > 0) ? Math.min(30, r.sinad_db) : -1;
    const q = blendQuality(berQ, sinadQ);
    const qtext = q >= 0 ? qualityLabel(q) : 'Poor';
    const lbl = document.getElementById('qualityLbl');
    if (lbl) lbl.textContent = qtext + ' · +' + sinad + ' dB';
    const activeBars = q >= 30 ? 5 : Math.max(0, Math.floor(q / 6));
    const bars = document.querySelectorAll('#qbars .qbar');
    bars.forEach((b, i) => b.classList.toggle('inactive', i >= activeBars));
  });
}

function updateLinkQualityFromLqa(peerAddr) {
  const best = [...lqaEntries]
    .filter(e => e.addr === peerAddr)
    .sort((a, b) => b.score - a.score)[0];
  if (!best) return;
  // Rating is BER-led (A.5.4.1.1): ber_from = non-unanimous 2/3 count (0–48,
  // 0=clean), lower=better. SINAD (A.5.4.1.2, still shown in dB) only refines it,
  // mirroring the C++ from_direction_quality(), so a flawless decode is never
  // rated "Poor" purely because the leakage-SINAD proxy reads ~6 dB.
  const sinad = Math.max(0, Math.min(30, Math.round(best.sinad_db || 0)));
  const berQ   = (typeof best.ber_from === 'number' && best.ber_from >= 0)
    ? (1 - Math.min(1, best.ber_from / 48)) * 30 : -1;
  const sinadQ = (best.sinad_db > 0) ? Math.min(30, best.sinad_db) : -1;
  const q = blendQuality(berQ, sinadQ);
  const qt = q >= 0 ? qualityLabel(q) : 'Poor';
  const sinadPart = sinad > 0 ? ' · +' + sinad + ' dB' : '';
  const lbl = document.getElementById('qualityLbl');
  if (lbl) lbl.textContent = qt + sinadPart;
  const bars = q >= 30 ? 5 : Math.max(0, Math.floor(q / 6));
  document.querySelectorAll('#qbars .qbar')
    .forEach((b, i) => b.classList.toggle('inactive', i >= bars));
}

function applyStatusReply(r) {
  if (!r.ok) return;
  applyBridgeState(r.state);
  // Recover/clear the linked peer from the poll (covers reconnect/reload
  // before a link_established event arrives). link_established is authoritative.
  if (typeof r.peer === 'string') linkedPeer = r.peer;
}

// Map the bridge's per-instance display state -> pill. The bridge reports
// "HANDSHAKE" for both the called station's HANDSHAKE state and the caller's
// response-exchange sub-phases (LISTENING/SENDING_ACK), so each side shows
// calling → handshake → linked from its own perspective. LINKED is driven by
// the link_established event instead (it carries the peer address).
function applyBridgeState(state) {
  if (state === 'IDLE') goIdle();
  else if (state === 'SCANNING') goScanning();
  else if (state === 'CALLING') { setStatus('Calling…', 'calling'); hideSoundingWarn(); }
  else if (state === 'INCOMING') setStatus('Incoming', 'incoming');
  else if (state === 'HANDSHAKE') { setStatus('Handshake…', 'handshake'); hideSoundingWarn(); }
  else if (state === 'LINKED') { setStatus('Linked', 'linked'); hideSoundingWarn(); }
}

function onBridgeEvent(e) {
  switch (e.event) {
    case 'state': applyBridgeState(e.value); break;
    case 'status': aleLogInfo(e.msg); break;
    case 'call_received':
      isIncomingCall = true;
      stopTimer();
      // The pill is driven by the `state` push (INCOMING during WAIT_CYCLE_END /
      // AWAIT_ACCEPT, HANDSHAKE during the response exchange) — don't override
      // it here. This handler only fills the incoming-call panel with the now
      // fully reassembled caller address.
      document.getElementById('incCs').textContent   = e.caller;
      document.getElementById('incName').textContent = 'accept connection to';
      // Auto-accept: don't surface the Accept/Decline panel at all — the link
      // handler below completes the call without operator input. Showing it
      // here (even briefly, before link_established) would let the operator
      // click Accept/Decline on a call that's already being auto-answered.
      if (!autoAcceptOn()) {
        showInc(true);
        showCallPanel(false);
      }
      break;
    case 'link_established':
      hideIdleWarn();   // fresh link — clear any stale idle-warning popup
      linkedPeer = e.peer;
      document.getElementById('callCs').textContent = e.peer;
      updateLinkQualityFromLqa(e.peer);
      pollSignalQuality();
      if (autoAcceptOn()) {
        // Auto-accept: the link is live — show the active-call panel + timer.
        stopTimer();
        setStatus('Linked', 'linked');
        showInc(false);
        showCallPanel(true);
        callStart = Date.now();
        timerId   = setInterval(tickTimer, 1000);
        tickTimer();
        pendingAccept = false;
      } else {
        // Manual mode: operator already decided during the INCOMING phase?
        if (preClickedAccept) {
          preClickedAccept = false;
          showInc(false); showCallPanel(true);
          setStatus('Linked', 'linked');
          callStart = Date.now();
          timerId   = setInterval(tickTimer, 1000); tickTimer();
          if (bridgeConnected) bridgeSend('ACCEPT', {});
        } else if (preClickedDecline) {
          preClickedDecline = false;
          if (bridgeConnected) bridgeSend('REJECT', {});
          else goScanning();
        } else if (isIncomingCall) {
          // Called station: present "Linked — accept?" for operator decision
          stopTimer();
          setStatus('Linked · pending', 'incoming');
          document.getElementById('incName').textContent = 'Linked — accept?';
          showInc(true);
          showCallPanel(false);
          pendingAccept = true;
        } else {
          // Calling station: remote responded, link is live — no operator action needed
          stopTimer();
          setStatus('Linked', 'linked');
          showInc(false);
          showCallPanel(true);
          callStart = Date.now();
          timerId   = setInterval(tickTimer, 1000);
          tickTimer();
        }
      }
      break;
    case 'link_terminated':
      aleLogInfo('Link terminated: ' + e.reason);
      hideIdleWarn();   // don't leave a stale idle-warning over the next state
      stopTimer();
      goScanning();
      break;
    case 'amd_received':
      messages.unshift({ self: e.self || primarySelfAddr(), peer: e.peer,
                         time: nowZulu(), text: e.text, own: false });
      renderMessages();
      break;
    case 'idle_warning':
      // SM fires this once, ~30s before the configured Twa elapses. Show a modal
      // with a live countdown; Reset Timer restarts the full idle period.
      showIdleWarn(e.remaining_sec);
      break;
    case 'sounding_warning':
      if (e.phase === 'warn') showSoundingWarn(e.net, e.remaining_sec);
      else hideSoundingWarn();
      break;
    case 'test_channel':
      onTestChannelEvent(e);
      break;
    case 'word_decoded':  onAleLogWord(e, 'rx'); break;
    case 'word_tx':       onAleLogWord(e, 'tx'); break;
    case 'frame_decoded': onAleLogFrame(e);  break;
    case 'channel_changed':
      // Push event from the core on every channel hop / manual VFO change —
      // replaces the 2 s VFO_GET poll for the frequency/channel readout so the
      // display tracks the real scan cadence (dwell-time, CAT-bound).
      radioFreqHz = e.rx_hz;
      radioMode   = e.mode;
      updateRadioDisplay();
      break;
    case 'ptt_changed':
      // Push event from the core on every PTT transition (SM-driven TX or
      // manual PTT) — replaces the VFO_GET poll's ptt field for the indicator.
      pttOn = !!e.ptt;
      applyPttUi();
      break;
    case 'voice_path':
      // Bridge switched the VAC owner between ALE-modem and voice passthrough.
      voicePassthrough = (e.mode === 'voice');
      if (!voicePassthrough) { Voice.pttMuted = false; voiceMicStop(); }
      applyVoicePathUi();
      break;
    case 'channel_busy':
      { const chip = document.getElementById('busyChip');
        if (chip) {
          chip.classList.toggle('busy', !!e.busy);
          chip.querySelector('span').textContent = e.busy ? 'FREQ BUSY' : 'FREQ CLEAR';
          chip.title = e.busy
            ? `Channel occupied — level ${Math.round(e.level_db)} dB, floor ${Math.round(e.floor_db)} dB (A.5.4.7.2)`
            : 'Channel clear (A.5.4.7.2)';
        }
      }
      break;
    case 'gps_fix':
      locGpsFix = !!e.acquired;
      if (e.acquired) { locGpsLat = e.lat || 0; locGpsLon = e.lon || 0; }
      updateLocStatus(locGpsFix, locGpsLat, locGpsLon, locSfi);
      break;
    case 'sfi_update':
      locSfi = e.sfi || 0;
      updateLocStatus(locGpsFix, locGpsLat, locGpsLon, locSfi);
      break;
  }
}

// Binary frames carry a 1-byte stream tag (see apps/ale_bridge.cpp):
// 0x00 = spectrum FFT (float32 LE), 0x01 = voice PCM (int16 LE, 8 kHz mono).
function onBinaryFrame(buf) {
  const u8 = new Uint8Array(buf);
  if (u8.length < 1) return;
  const tag = u8[0];
  if (tag === 0x00) onSpectrumFrame(buf.slice(1));
  else if (tag === BIN_TAG_VOICE) onVoiceRxFrame(buf.slice(1));
}

function onSpectrumFrame(buf) {
  latestSpectrum = new Float32Array(buf);
}

/* ━━━ VOICE PASSTHROUGH (browser-mediated operator audio) ━━━━━━━━━━━━━━━━━
   See docs/VOICE_AUDIO_ROUTING.md. While an ALE link is active and voice is
   armed, the bridge turns the VAC into a transparent radio↔browser pipe. Mic
   PCM goes up as binary frames tagged 0x01 (8 kHz mono int16 LE); radio RX
   comes down the same way. PTT selects direction (half-duplex). The ALE modem
   is silent while linked. Ported from apps/gui/mobile/app.js for desktop/mobile
   settings lockstep. */
const BIN_TAG_VOICE = 0x01;
let voicePassthrough = false;   // mirror of bridge voice_path mode ("voice")
let voiceArmed       = false;   // voice capability armed in the bridge
let voiceMicOn       = false;   // mic streaming active (PTT held)
let voiceMicTestId   = null;    // mic-test timer
const SPK_RING_N = 4096;        // ~0.5 s @ 8 kHz
const Voice = {
  micCtx: null, micStream: null, micNode: null, micRate: 48000,
  spkCtx: null, spkNode: null, spkRate: 48000,
  spkRing: null, spkRead: 0.0, spkAvail: 0,
  pttMuted: false,
};

function _audioCtxCtor() {
  return window.AudioContext || window.webkitAudioContext || null;
}

// ── Speaker (radio RX → browser) ────────────────────────────────────────────
function voiceInitSpeaker() {
  if (Voice.spkCtx) return;
  const Ctx = _audioCtxCtor();
  if (!Ctx) return;
  try {
    Voice.spkCtx  = new Ctx();
    Voice.spkRate = Voice.spkCtx.sampleRate;
    Voice.spkRing = new Float32Array(SPK_RING_N);
    Voice.spkRead = 0.0;
    Voice.spkAvail = 0;
    // 1024-frame output-only ScriptProcessor; input channel count 0.
    Voice.spkNode = Voice.spkCtx.createScriptProcessor(1024, 0, 1);
    Voice.spkNode.onaudioprocess = voiceSpkProcess;
    Voice.spkNode.connect(Voice.spkCtx.destination);
  } catch (e) { aleLogInfo('Voice speaker init failed: ' + e.message); }
}

function voiceSpkProcess(e) {
  const out = e.outputBuffer.getChannelData(0);
  if (Voice.pttMuted || Voice.spkAvail <= 0 || !Voice.spkRing) { out.fill(0); return; }
  // Upsample 8 kHz ring → device rate using linear interpolation.
  // ratio = deviceRate / 8000 (e.g. 6 at 48 kHz). Per output sample the 8 kHz
  // read position advances by 1/ratio; from spkAvail input samples we can fill
  // spkAvail * ratio output frames.
  const ratio    = Voice.spkRate / 8000;
  const invRatio = 1 / ratio;
  const nFill = Math.min(out.length, Math.floor(Voice.spkAvail * ratio));
  let pos = Voice.spkRead;
  for (let i = 0; i < nFill; ++i) {
    const i0 = Math.floor(pos);
    const frac = pos - i0;
    const a = Voice.spkRing[i0 % SPK_RING_N];
    const b = Voice.spkRing[(i0 + 1) % SPK_RING_N];
    out[i] = a + (b - a) * frac;
    pos += invRatio;
  }
  for (let i = nFill; i < out.length; ++i) out[i] = 0;   // underrun → silence
  const consumed = Math.ceil(nFill / ratio);  // input (8 kHz) samples consumed
  Voice.spkRead = (Voice.spkRead + consumed) % SPK_RING_N;
  Voice.spkAvail = Math.max(0, Voice.spkAvail - consumed);
}

function onVoiceRxFrame(payload) {
  if (!voicePassthrough) return;
  voiceInitSpeaker();
  if (Voice.spkCtx && Voice.spkCtx.state === 'suspended') Voice.spkCtx.resume();
  if (!Voice.spkRing) return;
  const i16 = new Int16Array(payload);
  for (let k = 0; k < i16.length; ++k) {
    Voice.spkRing[Math.floor(Voice.spkRead + Voice.spkAvail) % SPK_RING_N] = i16[k] / 32768;
    if (Voice.spkAvail < SPK_RING_N) Voice.spkAvail++;
    else Voice.spkRead = (Voice.spkRead + 1) % SPK_RING_N;
  }
}

// ── Microphone (browser → radio TX) ─────────────────────────────────────────
async function voiceMicStart() {
  if (voiceMicOn) return;
  const Ctx = _audioCtxCtor();
  if (!Ctx || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return;
  try {
    const audioCfg = { echoCancellation: true, noiseSuppression: true, autoGainControl: true };
    if (_voiceMicSel) audioCfg.deviceId = { exact: _voiceMicSel };
    const stream = await navigator.mediaDevices.getUserMedia({ audio: audioCfg });
    Voice.micCtx    = new Ctx();
    Voice.micRate   = Voice.micCtx.sampleRate;
    Voice.micStream = stream;
    const src = Voice.micCtx.createMediaStreamSource(stream);
    Voice.micNode   = Voice.micCtx.createScriptProcessor(2048, 1, 0);
    Voice.micNode.onaudioprocess = voiceMicProcess;
    src.connect(Voice.micNode);
    Voice.micNode.connect(Voice.micCtx.destination);  // required for ScriptProcessor to fire
    voiceMicOn = true;
  } catch (e) { aleLogInfo('Voice mic start failed: ' + e.message); }
}

function voiceMicProcess(e) {
  if (!voiceMicOn || !bridgeWs || bridgeWs.readyState !== 1) return;
  const in0 = e.inputBuffer.getChannelData(0);
  const ratio = Voice.micRate / 8000;
  const nOut = Math.floor(in0.length / ratio);
  if (nOut <= 0) return;
  const i16 = new Int16Array(nOut);
  for (let i = 0; i < nOut; ++i) {
    const start = Math.floor(i * ratio), end = Math.floor((i + 1) * ratio);
    let sum = 0;
    for (let j = start; j < end; ++j) sum += in0[j];
    let v = Math.round((sum / Math.max(1, end - start)) * 32767);
    if (v > 32767) v = 32767; if (v < -32768) v = -32768;
    i16[i] = v;
  }
  const buf = new ArrayBuffer(1 + i16.byteLength);
  const u8 = new Uint8Array(buf);
  u8[0] = BIN_TAG_VOICE;
  new Uint8Array(buf, 1).set(new Uint8Array(i16.buffer));
  bridgeWs.send(buf);
}

function voiceMicStop() {
  if (!voiceMicOn && !Voice.micCtx) return;
  voiceMicOn = false;
  try { if (Voice.micNode)   Voice.micNode.disconnect(); } catch (_) {}
  try { if (Voice.micStream) Voice.micStream.getTracks().forEach(t => t.stop()); } catch (_) {}
  try { if (Voice.micCtx)    Voice.micCtx.close(); } catch (_) {}
  Voice.micNode = null; Voice.micStream = null; Voice.micCtx = null;
}

// ── UI / settings ───────────────────────────────────────────────────────────
// Single-source PTT indicator: derives label + icon from pttOn + voicePassthrough
// and writes the hidden anchor #pttBtn (innerHTML + .ptt-on). The mobile pill
// #pttBtnMob (if present) is updated too — guarded so this is safe on desktop.
function applyPttUi() {
  const label = pttOn ? 'TX' : (voicePassthrough ? 'TALK' : 'PTT');
  const ic    = pttOn ? 'zap' : 'mic';
  const b = document.getElementById('pttBtn');
  if (b) { b.classList.toggle('ptt-on', pttOn); b.innerHTML = `${icon(ic,14)} ${label}`; }
  const m = document.getElementById('pttBtnMob');
  if (m) {
    m.classList.toggle('ptt-on', pttOn);
    const lbl = m.querySelector('.mob-ptt-lbl');
    if (lbl) lbl.textContent = label;
  }
}

function applyVoicePathUi() {
  const badge = document.getElementById('voiceBadge');
  if (badge) badge.classList.toggle('hidden', !voicePassthrough);
  applyPttUi();
}

function syncVoiceFromBridge() {
  bridgeSend('VOICE_GET', {}, (r) => {
    voiceArmed = !!r.armed;
    const arm = document.getElementById('cfgVoiceArm');
    if (arm) arm.checked = voiceArmed;
    voicePassthrough = (r.mode === 'voice');
    applyVoicePathUi();
  });
}

function onVoiceArmChange(on) {
  voiceArmed = !!on;
  if (bridgeConnected) bridgeSend('VOICE_ARM', { on });
}

let _voiceMicSel = '', _voiceSpkSel = '';
function onVoiceMicChange() {
  _voiceMicSel = document.getElementById('voiceMic').value;
  // sinkId for output is set on the speaker side; mic selection is applied on
  // next voiceMicStart via getUserMedia({deviceId:{exact:_voiceMicSel}}).
}
function onVoiceSpkChange() {
  _voiceSpkSel = document.getElementById('voiceSpk').value;
}

async function enumVoiceDevices() {
  const micSel = document.getElementById('voiceMic');
  const spkSel = document.getElementById('voiceSpk');
  if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) return;
  try {
    // Request permission once so labels become available.
    try { await navigator.mediaDevices.getUserMedia({ audio: true }); } catch (_) {}
    const devs = await navigator.mediaDevices.enumerateDevices();
    const mkOpt = d => `<option value="${d.deviceId}">${d.label || (d.kind + ' ' + d.deviceId.slice(0,6))}</option>`;
    micSel.innerHTML  = '<option value="">— default —</option>' + devs.filter(d => d.kind === 'audioinput').map(mkOpt).join('');
    spkSel.innerHTML  = '<option value="">— default —</option>' + devs.filter(d => d.kind === 'audiooutput').map(mkOpt).join('');
  } catch (e) { aleLogInfo('Voice device enum failed: ' + e.message); }
}

async function voiceMicTest() {
  if (voiceMicTestId) { clearInterval(voiceMicTestId); voiceMicTestId = null; aleLogInfo('Mic test stopped'); return; }
  aleLogInfo('Mic test: speaking for 3s shows input level in the console');
  // Reuse the level meter: start a short capture and report RMS.
  // Lightweight — does not require the full voice path.
  // (Kept minimal; full mic metering is out of scope for this first cut.)
  voiceMicTestId = setTimeout(() => { voiceMicTestId = null; aleLogInfo('Mic test done'); }, 3000);
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   WATERFALL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const canvas = document.getElementById('waterfallCanvas');
const ctx    = canvas.getContext('2d');
// ── Waterfall configuration ───────────────────────────────────────────────────
//
// ❶  FREQUENCY WINDOW  (BW_LO / BW_HI)
//    Visible Hz range of the waterfall canvas.  Must equal 0 / (sample_rate/2)
//    so the bin mapping in genRowFromSpectrum() stays correct; do not change
//    without also updating the modem sample rate and SPEC_FFT_N.
//
// ❷  ROW INTERVAL  (nextRowAt + 100, in drawWaterfall())
//    How often a new spectrum row is inserted, in ms.  Must match the modem's
//    SPEC_INTERVAL: row_ms = 1000 * SPEC_INTERVAL / SAMPLE_RATE_HZ.
//    Default: 100 ms = 10 rows/s (SPEC_INTERVAL = 800, sample rate = 8000 Hz).
//    Lower = more rows/s and finer time resolution; also more CPU for rendering.
//
// ❸  ADAPTIVE NORMALISATION  (in genRowFromSpectrum())
//    Noise floor (emaFloor): slow EMA of per-frame average.
//      α_floor  = 0.03  — time constant ≈ 1 / (2 × α × fps) s; increase to
//                         track a rising noise floor faster, decrease to smooth it.
//    Signal peak (emaPeak): asymmetric EMA of per-frame maximum.
//      attack   = 0.20  — rises quickly when a new tone appears (~0.5 s to 90 %).
//      decay    = 0.002 — falls slowly after the signal ends (~25 s to 10 %).
//    Range minimum: Math.max(20, …) — enforces at least 20 dB of display range
//                   even when peak ≈ floor (quiet channel, no signal).
//    To disable adaptive normalisation and use a fixed range instead, replace
//    genRowFromSpectrum()'s ema lines with: const floor = -90, range = 70;
//
// ❹  COLOUR MAP  (energy2rgb())
//    SDR rainbow: black → blue → cyan → green → yellow → red.
//    Transition thresholds (0–1 normalised energy): 0.20 / 0.40 / 0.60 / 0.80.
//    Raise the first threshold to compress the black/silent region; lower the
//    last to make strong signals reach red earlier.
//
// ─────────────────────────────────────────────────────────────────────────────

const TONES  = [750, 1000, 1250, 1500, 1750, 2000, 2250, 2500]; // ALE 8-FSK tones (Hz)
const BW_LO  = 0, BW_HI = 3500;       // ❶ displayed Hz range (0–3.5 kHz)
const ALE_LO = 750, ALE_HI = 2500;    // ALE sub-band for the band-frame overlay only
const ALE_GUARD = 125;                // frame padding beyond the edge tones (Hz)
const AXIS_MAJOR = [0, 1000, 2000, 3000, 3500];  // labelled gridlines (Hz)
const AXIS_MINOR = [500, 1500, 2500];             // unlabelled gridlines (Hz)

let rows = [];
let wfState  = 'scanning';

function resizeCanvas() {
  const el = canvas.parentElement;
  canvas.width  = el.clientWidth;
  canvas.height = el.clientHeight;
  rows = [];
  buildTicks();
}

function buildTicks() {
  const el = document.getElementById('wfTicks');
  el.innerHTML = '';
  const span = BW_HI - BW_LO;

  // Minor (unlabelled) gridlines
  AXIS_MINOR.forEach(hz => {
    const t = document.createElement('div');
    t.className = 'wf-tick minor';
    t.style.left = ((hz - BW_LO) / span * 100) + '%';
    t.style.transform = 'translateX(-50%)';
    el.appendChild(t);
  });

  // Major (labelled) ticks; endpoints pinned to the edges so 0 / 4k stay visible
  // and line up with the top band-edge labels.
  const last = AXIS_MAJOR.length - 1;
  AXIS_MAJOR.forEach((hz, i) => {
    const pct = (hz - BW_LO) / span * 100;
    const xform = i === 0    ? 'translateX(0)'
                : i === last ? 'translateX(-100%)'
                :              'translateX(-50%)';
    const tick = document.createElement('div');
    tick.className = 'wf-tick';
    tick.style.left = pct + '%';
    tick.style.transform = xform;
    el.appendChild(tick);
    const lbl = document.createElement('div');
    lbl.className = 'wf-tick-lbl';
    lbl.style.left = pct + '%';
    lbl.style.transform = xform;
    lbl.textContent = hz === 0 ? '0' : (hz / 1000).toFixed(1) + 'k';
    el.appendChild(lbl);
  });

  // ALE 8-FSK band frame inside the 0–3.5 kHz window, padded by ALE_GUARD on each
  // side (625–2625 Hz) so it encloses the full waveform, not just the edge tones.
  const bandL = (ALE_LO - ALE_GUARD - BW_LO) / span * 100;
  const bandR = (ALE_HI + ALE_GUARD - BW_LO) / span * 100;
  const region = document.getElementById('wfBandRegion');
  if (region) { region.style.left = bandL + '%'; region.style.width = (bandR - bandL) + '%'; }
  // Tone marker: every ALE tone number sits at its true frequency, with en-dash
  // separators filling every gap (continuous scale across the band).
  const marker = document.getElementById('wfBandLabel');
  if (marker) {
    marker.innerHTML = '';
    for (let i = 0; i < TONES.length - 1; i++) {
      const sep = document.createElement('span');
      sep.className = 'wf-band-sep';
      sep.style.left = (((TONES[i] + TONES[i + 1]) / 2 - BW_LO) / span * 100) + '%';
      sep.textContent = '–';
      marker.appendChild(sep);
    }
    TONES.forEach(hz => {
      const s = document.createElement('span');
      s.className = 'wf-band-tone';
      s.style.left = ((hz - BW_LO) / span * 100) + '%';
      s.textContent = hz;
      marker.appendChild(s);
    });
  }
  // "ALE 8-FSK" caption at the bottom edge of the band frame, centred on the band.
  const cap = document.getElementById('wfBandCap');
  if (cap) cap.style.left = (((ALE_LO + ALE_HI) / 2 - BW_LO) / span * 100) + '%';
}

new ResizeObserver(resizeCanvas).observe(canvas.parentElement);
resizeCanvas();

// Real spectrum (bridge connected): 1025 bins, 0–4000 Hz, ≈3.9 Hz/bin — values in dBFS.
// Adaptive peak+floor tracking with fast-attack / slow-decay (inspired by waterfall.c AGC).
let emaFloor  = -80;  // tracks per-frame average (noise floor proxy)
let emaPeak   = -20;  // tracks per-frame maximum (signal peak proxy)
let nextRowAt = 0;    // timestamp-throttle: add waterfall row every 100 ms (10 fps)

function genRowFromSpectrum(spec) {
  const W = canvas.width || 1;
  const row = new Float32Array(W);

  // Per-frame stats: average (floor proxy) and max (peak proxy).
  let sum = 0, frameMax = -200;
  for (let i = 0; i < spec.length; i++) {
    sum += spec[i];
    if (spec[i] > frameMax) frameMax = spec[i];
  }
  // Floor: slow EMA of average — dominated by noise bins, not the few tone bins.
  emaFloor = 0.97 * emaFloor + 0.03 * (sum / spec.length);
  // Peak: fast attack (0.20), slow decay (0.002) — reacts to signal appearance quickly.
  emaPeak = emaPeak + (frameMax > emaPeak ? 0.20 : 0.002) * (frameMax - emaPeak);

  const floor = emaFloor;
  const range = Math.max(20, emaPeak - emaFloor);  // adaptive; min 20 dB

  const lastBin = spec.length - 1;
  for (let x = 0; x < W; x++) {
    const hz      = BW_LO + (x / (W - 1)) * (BW_HI - BW_LO);
    const binExact = (hz / 4000) * lastBin;
    const lo      = Math.floor(binExact);
    const hi      = Math.min(lo + 1, lastBin);
    const frac    = binExact - lo;
    const val     = (1 - frac) * spec[lo] + frac * spec[hi];  // dBFS
    row[x] = Math.max(0, Math.min(1, (val - floor) / range));
  }
  return row;
}

function genRow() {
  if (latestSpectrum) return genRowFromSpectrum(latestSpectrum);
  return new Float32Array(canvas.width || 1);
}

// Map energy 0-1 → RGB using the classic SDR rainbow:
// black → blue → cyan → green → yellow → red
function energy2rgb(v) {
  v = Math.max(0, Math.min(1, v));
  if (v < 0.20) {
    const t = v / 0.20;
    return [0, 0, Math.round(t * 255)];
  } else if (v < 0.40) {
    const t = (v - 0.20) / 0.20;
    return [0, Math.round(t * 255), 255];
  } else if (v < 0.60) {
    const t = (v - 0.40) / 0.20;
    return [0, 255, Math.round((1 - t) * 255)];
  } else if (v < 0.80) {
    const t = (v - 0.60) / 0.20;
    return [Math.round(t * 255), 255, 0];
  } else {
    const t = (v - 0.80) / 0.20;
    return [255, Math.round((1 - t) * 255), 0];
  }
}

function drawWaterfall() {
  const W = canvas.width, H = canvas.height;
  if (W < 4 || H < 4) { requestAnimationFrame(drawWaterfall); return; }

  const now = performance.now();
  if (now >= nextRowAt) {
    rows.unshift(genRow());
    if (rows.length > H) rows.length = H;
    nextRowAt = now + 100;
    for (const m of wfMarkers) m.age++;
  }

  const img = ctx.createImageData(W, H);
  const d   = img.data;
  for (let y = 0; y < rows.length; y++) {
    const r = rows[y];
    for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 4;
      const [rr, gg, bb] = energy2rgb(r[x]);
      d[i] = rr; d[i+1] = gg; d[i+2] = bb; d[i+3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);

  // Draw frame markers as 4 px-wide colored ticks on the left edge
  for (const m of wfMarkers) {
    if (m.age >= H) continue;
    ctx.fillStyle = m.color;
    ctx.fillRect(0, m.age, 4, 3);
  }

  requestAnimationFrame(drawWaterfall);
}
drawWaterfall();

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CHANNEL LOOKUP HELPERS  (frequency is the primary LQA key per MIL-STD)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function chFromFreq(freqHz) {
  return channels.find(c => Math.abs(parseInt(c.rx, 10) - freqHz) < 500) || null;
}
function aleChLabel(freqHz) {
  const ch = chFromFreq(freqHz);
  if (ch) return ch.id.replace('C-', 'C');
  return freqHz ? '(' + (freqHz / 1e6).toFixed(3) + ')' : '?';
}
function chLabelForFreq(freqHz) {
  const ch = chFromFreq(freqHz);
  return ch ? ch.id.replace('C-', 'C') : '';
}
// Exact configured channel frequency in MHz (Hz-precise, 6 decimals — no rounding).
// Looks the channel up by the LQA entry's freq_hz and formats its configured RX Hz,
// so the display matches the channel's "RX Hz" field exactly instead of the old
// kHz-rounded (freq_hz/1e6).toFixed(3) that turned 10.1455 MHz into "10.146".
function fmtChFreqExact(freqHz) {
  const ch = chFromFreq(freqHz);
  const hz = ch ? parseInt(ch.rx, 10) : freqHz;
  return hz ? (hz / 1e6).toFixed(6) : '?';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   HEARD STATIONS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let heardStations = [];  // { addr, freq_hz, ts, score, available, sinad_from, sinad_to,
                         //   ber_from, ber_to_code, ber_to, mp_to, ageMin } — full FROM/TO
                         //   breakdown, same columns as the Settings/LQA table.
// Keys ('addr|freq_hz') the user removed from the heard list. Kept for the
// session so syncLqaFromBridge's mirror step doesn't re-add them on the next
// LQA_LIST sync. Reset only by clearLqa() (full DB clear).
let heardDeleted = new Set();

function upsertHeard(e) {
  const idx = heardStations.findIndex(h => h.addr === e.addr && h.freq_hz === e.freq_hz);
  // Full FROM/TO breakdown mirroring the lqaEntry, so the heard panel can render the
  // same 10-column table as Settings/LQA (sinad_from/to, ber_from/to, mp_to, …).
  const metrics = {
    score:       e.score,
    available:   (typeof e.available === 'number') ? e.available : -1,
    sinad_from:  e.sinad_from,
    sinad_to:    e.sinad_to,
    ber_from:    e.ber_from,
    ber_to_code: e.ber_to_code,
    ber_to:      e.ber_to,
    mp_to:       e.mp_to,
    ageMin:      e.ageMin,
    // Raw LQA-DB age in ms — the authoritative "when last heard" signal. The
    // heard panel sorts on this (ascending = most-recently-heard first), so a
    // station heard again bubbles back to the top regardless of where it sits
    // in the array. ageMin (rounded minutes) is too coarse to order by.
    age_ms:      (typeof e.age_ms === 'number') ? e.age_ms : 0,
    // Raw LQA-DB timestamp (ms since epoch) — P1-12 "received at" display.
    ts_ms:       (typeof e.ts_ms === 'number') ? e.ts_ms : 0,
  };
  if (idx >= 0) {
    // Refresh metrics but keep the original "first heard" timestamp.
    heardStations[idx] = { ...heardStations[idx], ...metrics };
  } else {
    const ts = new Date().toTimeString().slice(0, 8);
    heardStations.unshift({ addr: e.addr, freq_hz: e.freq_hz, ts, ...metrics });
  }
  renderHeard();
}

function deleteHeard(addr, freqHz) {
  heardDeleted.add(addr + '|' + freqHz);
  heardStations = heardStations.filter(h => !(h.addr === addr && h.freq_hz === freqHz));
  renderHeard();
}

function clearHeard() {
  heardStations.forEach(h => heardDeleted.add(h.addr + '|' + h.freq_hz));
  heardStations = [];
  renderHeard();
}

// One-click "add heard station to address book" from a heard-row button.
// No-op if the callsign is already a contact or if the row has no real callsign.
function addHeardToContacts(addr) {
  if (!addr || addr === '(sounding)') return;
  const cs = addr.toUpperCase();
  if (contacts.some(c => c.cs.toUpperCase() === cs)) {
    aleLogInfo(`${cs} is already in the address book`);
    return;
  }
  const c = { cs, name: '', fav: false };
  contacts.push(c);
  selectedContact = c;
  renderContacts();
  renderHeard();   // flip the row's button to the ✓/already-added state
  if (bridgeConnected) {
    bridgeSend('CONTACT_ADD', { callsign: c.cs, name: c.name }, () => syncContactsFromBridge());
  }
  aleLogInfo(`Added ${cs} to the address book`);
}

function renderHeard() {
  const el = document.getElementById('heardList');
  if (!el) return;
  if (!heardStations.length) {
    el.innerHTML = '<div class="heard-empty">No stations heard yet</div>';
    return;
  }
  // Same column layout and gradient math as renderLqa() (Settings/LQA), so the
  // main-window heard panel reads identically to the Settings table. qCell/
  // availBadge are hoisted declarations below; cfgLqaAge lives in the settings
  // DOM (always present, just hidden) and drives the age gradient.
  const ageLimit = Math.max(1, Number(document.getElementById('cfgLqaAge')?.value) || 60);
  // Sort by recency: most-recently-heard first. age_ms is the LQA-DB age
  // (smaller = fresher), so ascending puts the latest-heard station on top;
  // ts (first-heard "HH:MM:SS") is a stable tie-break for equal ages.
  const rows = [...heardStations].sort((a, b) =>
    (a.age_ms ?? 0) - (b.age_ms ?? 0) || (b.ts || '').localeCompare(a.ts || ''));
  const body = rows.map(h => {
    const sinadFromG = (h.sinad_from != null) ? h.sinad_from / 30 : null;
    const sinadToG   = (h.sinad_to   != null) ? h.sinad_to   / 30 : null;
    const berFromG   = (h.ber_from   != null) ? 1 - Math.min(1, h.ber_from / 48) : null;
    const berToCode  = (h.ber_to_code != null && h.ber_to_code <= 30) ? h.ber_to_code : null;
    const berToG     = (berToCode != null) ? 1 - berToCode / 30 : null;
    const mpG        = (h.mp_to != null) ? 1 - Math.min(1, h.mp_to / 6) : null;
    const scoreG     = Math.min(1, Math.max(0, h.score / 30));
    const ageG       = 1 - Math.min(1, h.ageMin / ageLimit);
    // Add-to-address-book button: only for rows that carry a real callsign.
    // Shows ✓/disabled once the callsign is already a contact.
    const canAdd = h.addr && h.addr !== '(sounding)';
    const inBook = canAdd && contacts.some(c => c.cs.toUpperCase() === h.addr.toUpperCase());
    const addBtn = !canAdd ? '' : inBook
      ? `<span class="heard-add heard-added" title="Already in address book">${icon('user',11)}</span>`
      : `<button class="heard-add" onclick='addHeardToContacts(${JSON.stringify(h.addr)})' title="Add to address book">${icon('userPlus',11)}</button>`;
    return `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${escapeHtml(h.addr)}</td>` +
      `<td class="lqa-cell">${fmtChFreqExact(h.freq_hz)}</td>` +
      availBadge(h.available) +
      qCell(h.score, scoreG) +
      qCell(h.sinad_from != null ? `+${Math.round(h.sinad_from)}` : null, sinadFromG) +
      qCell(h.sinad_to   != null ? `+${Math.round(h.sinad_to)}`   : null, sinadToG) +
      qCell(h.ber_from   != null ? h.ber_from.toFixed(1)          : null, berFromG) +
      qCell(berToCode    != null ? h.ber_to                       : null, berToG) +
      qCell(h.mp_to      != null ? h.mp_to.toFixed(0) + 'ms'      : null, mpG) +
      qCell(h.ageMin >= 60 ? '>60m' : h.ageMin + 'm', ageG) +
      tsCell(h.ts_ms) +
      `<td class="lqa-cell heard-actions">${addBtn}<button class="heard-del" onclick='deleteHeard(${JSON.stringify(h.addr)},${h.freq_hz})' title="Remove">×</button></td>` +
      `</tr>`;
  }).join('');
  el.innerHTML =
    `<table class="ch-table heard-table">` +
      `<thead><tr>` +
        `<th>Callsign</th><th>Ch</th><th>Avail</th><th>Score</th>` +
        `<th>SINAD<br>FROM</th><th>SINAD<br>TO</th>` +
        `<th>BER<br>FROM</th><th>BER<br>TO</th><th>MP</th><th>Age</th><th>Received</th>` +
        `<th></th>` +
      `</tr></thead>` +
      `<tbody>${body}</tbody>` +
    `</table>`;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   ALE LOG
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const ALE_LOG_CAP = 1000;
let aleLogCount = 0;
const aleLogFrameCh = new Map();  // frame_id → freq_hz

function aleLogAppend(html) {
  const body = document.getElementById('aleLogBody');
  if (!body) return;
  const empty = body.querySelector('.ale-log-empty');
  if (empty) empty.remove();
  body.insertAdjacentHTML('beforeend', html);
  body.scrollTop = body.scrollHeight;
  aleLogCount++;
  if (aleLogCount > ALE_LOG_CAP) { body.removeChild(body.firstChild); aleLogCount--; }
  document.getElementById('aleLogCount').textContent =
    aleLogCount + (aleLogCount === 1 ? ' entry' : ' entries');
}

function onAleLogWord(e, dir) {
  dir = dir || 'rx';
  const isTx = dir === 'tx';
  const fid  = e.frame_id;
  // RX words of one frame share a freq via aleLogFrameCh (later words may omit
  // freq_hz). TX words always carry their own freq and live in a separate
  // frame_id space — don't touch the RX map to avoid cross-stream collisions.
  let freqHz;
  if (isTx) {
    freqHz = e.freq_hz || 0;
  } else {
    if (!aleLogFrameCh.has(fid) && e.freq_hz)
      aleLogFrameCh.set(fid, e.freq_hz);
    freqHz = aleLogFrameCh.get(fid) || e.freq_hz || 0;
  }
  const chDisp = aleChLabel(freqHz);
  const ts  = new Date().toTimeString().slice(0, 8);
  const p   = (e.preamble || '').toLowerCase();
  const pill = p === 'to' ? 'pill-to' : p === 'tis' ? 'pill-tis' :
               p === 'twas' ? 'pill-twas' : p === 'thru' ? 'pill-thru' :
               p === 'rep' ? 'pill-rep' : 'pill-data';
  const fec = e.fec || 0;
  const berCls = fec === 0 ? 'ale-ber-ok' : fec <= 1 ? 'ale-ber-warn' : 'ale-ber-bad';
  const dirCls = isTx ? 'dir-tx' : 'dir-rx';
  const dirSym = isTx ? '▶' : '◀';
  aleLogAppend(
    `<div class="ale-entry${isTx ? ' ale-entry-tx' : ''}">` +
    `<span class="ale-entry-ts">${ts}</span>` +
    `<span class="ale-entry-ch">[${escapeHtml(chDisp)}]</span>` +
    `<span class="ale-entry-mode">[ALE]</span>` +
    `<span class="ale-entry-dir ${dirCls}">${dirSym}</span>` +
    `<span class="ale-entry-word pill ${pill}">${escapeHtml(e.preamble)}</span>` +
    `<span class="ale-entry-addr">[${escapeHtml(e.addr)}]</span>` +
    `<span class="ale-entry-ber ${berCls}">BER: ${fec}</span>` +
    `</div>`);
  // Waterfall markers are RX-only (received frames). Our own TX does not mark
  // the RX spectrum.
  if (!isTx && !wfMarkers.some(m => m.frameId === fid)) {
    wfMarkers.unshift({ frameId: fid, age: 0, color: '#7a9ab8' });
    if (wfMarkers.length > 500) wfMarkers.length = 500;
  }
}

const ALE_LOG_COLORS = { SOUNDING:'#00dc8c', INDIVIDUAL:'#4dc8ff',
                         NET:'#ffca28', AMD:'#ff8a65', UNKNOWN:'#7a9ab8' };

function onAleLogFrame(e) {
  const m = wfMarkers.find(x => x.frameId === e.frame_id);
  if (m) m.color = ALE_LOG_COLORS[e.call_type] || ALE_LOG_COLORS.UNKNOWN;
  if (aleLogFrameCh.size > 200)
    aleLogFrameCh.delete(aleLogFrameCh.keys().next().value);
  aleLogAppend(`<div class="ale-frame-sep"></div>`);
}

function onAleLogLqa(e) {
  upsertHeard(e);
  const ts      = new Date().toTimeString().slice(0, 8);
  const freqStr = e.freq_hz ? ` ${fmtChFreqExact(e.freq_hz)} MHz` : '';
  const lbl     = e.freq_hz ? chLabelForFreq(e.freq_hz) : '';
  const lblStr  = lbl ? ` [${lbl}]` : '';
  const scoreStr = e.score != null ? ` score=${e.score}` : '';
  // Collapsed SINAD for the one-line log (FROM preferred, TO fallback).
  const sinadDb = e.sinad_from != null ? e.sinad_from
                : e.sinad_to   != null ? e.sinad_to : 0;
  const sinadStr = sinadDb > 0 ? ` SINAD=+${Math.round(sinadDb)}dB` : '';
  aleLogAppend(
    `<div class="ale-entry ale-info">` +
    `<span class="ale-entry-ts">${ts}</span>` +
    `<span class="ale-entry-ch"></span>` +
    `<span class="ale-entry-mode">[INFO]</span>` +
    `<span class="ale-entry-addr">LQA record: ${escapeHtml(e.addr)}` +
    `${escapeHtml(freqStr + lblStr + scoreStr + sinadStr)}</span>` +
    `</div>`);
}

function aleLogInfo(text) {
  const ts = new Date().toTimeString().slice(0, 8);
  aleLogAppend(
    `<div class="ale-entry ale-info">` +
    `<span class="ale-entry-ts">${ts}</span>` +
    `<span class="ale-entry-ch"></span>` +
    `<span class="ale-entry-mode">[INFO]</span>` +
    `<span class="ale-entry-addr">${escapeHtml(text)}</span>` +
    `</div>`);
}

function clearAleLog() {
  const body = document.getElementById('aleLogBody');
  if (body) body.innerHTML = '<div class="ale-log-empty">No ALE activity yet</div>';
  aleLogCount = 0;
  document.getElementById('aleLogCount').textContent = '0 entries';
  aleLogFrameCh.clear();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CALL TIMER
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let callStart = null, timerId = null;
let idleWarnIntervalId = null, idleWarnRemaining = 0;
let soundingWarnIntervalId = null, soundingWarnRemaining = 0, soundingWarnNet = '';
function tickTimer() {
  if (!callStart) return;
  const s = Math.floor((Date.now() - callStart) / 1000);
  const m = String(Math.floor(s / 60)).padStart(2,'0');
  const ss = String(s % 60).padStart(2,'0');
  document.getElementById('callTimer').textContent = m + ':' + ss;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   STATE HELPERS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function setStatus(label, cls) {
  document.getElementById('statusText').textContent = label;
  document.getElementById('statusDot').className    = 'dot ' + cls;
  wfState = cls;
  // Keep the header Scan toggle in sync with whatever drove the state change.
  const b = document.getElementById('scanBtn');
  if (b) {
    const on = cls === 'scanning';
    b.textContent = on ? '■ Stop' : '▶ Scan';
    b.classList.toggle('scan-on', on);
  }
  if (typeof updateScanBtn === 'function') updateScanBtn();  // keep disabled state in sync with wfState
  if (typeof updateSoundBtn === 'function') updateSoundBtn();
}

function showInc(show) {
  document.getElementById('incEmpty').classList.toggle('hidden',  show);
  document.getElementById('incAlert').classList.toggle('hidden', !show);
}

function showCallPanel(show) {
  document.getElementById('callPanel').classList.toggle('hidden', !show);
  document.getElementById('callZone').classList.toggle('hidden',   show);
}


function stopTimer() {
  if (timerId) clearInterval(timerId);
  timerId = null;
  callStart = null;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   LINK IDLE-TIMEOUT WARNING POPUP
   The core fires `idle_warning` once, ~IDLE_WARNING_LEAD_MS (30s) before the
   configured Twa (Link Idle Timeout) elapses. The popup shows a live countdown
   derived from the SM's remaining_sec at event time. Reset Timer sends
   CMD:RESET_IDLE_TIMER, which restarts the full idle period and re-arms the
   warning. Dismiss closes the popup but the link still times out; the warning
   re-appears next cycle (the SM re-arms on the next activity/reset).
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function showIdleWarn(remainingSec) {
  idleWarnRemaining = Math.max(0, Math.floor(Number(remainingSec) || 0));
  document.getElementById('idleWarnPeer').textContent = linkedPeer || '—';
  document.getElementById('idleWarnRemaining').textContent = idleWarnRemaining;
  document.getElementById('idleWarnModal').classList.remove('hidden');
  if (idleWarnIntervalId) clearInterval(idleWarnIntervalId);
  idleWarnIntervalId = setInterval(() => {
    if (idleWarnRemaining > 0) idleWarnRemaining -= 1;
    document.getElementById('idleWarnRemaining').textContent = idleWarnRemaining;
  }, 1000);
}

function hideIdleWarn() {
  if (idleWarnIntervalId) { clearInterval(idleWarnIntervalId); idleWarnIntervalId = null; }
  document.getElementById('idleWarnModal').classList.add('hidden');
}

function resetIdleTimer() {
  if (bridgeConnected) bridgeSend('RESET_IDLE_TIMER', {});
  hideIdleWarn();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   PRE-SOUNDING COUNTDOWN POPUP
   The core fires `sounding_warning` (phase="warn") once the auto-sounding
   timer enters the configured lead window while the SM is IDLE. The popup
   shows a live countdown and offers Interrupt (resets timer to full interval)
   or Dismiss (popup closes, sounding still fires). Phase "fire" or "cancel"
   auto-dismiss the popup.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function showSoundingWarn(net, remainingSec) {
  soundingWarnNet       = net || '';
  soundingWarnRemaining = Math.max(0, Math.floor(Number(remainingSec) || 0));
  document.getElementById('soundingWarnNet').textContent       = soundingWarnNet || '—';
  document.getElementById('soundingWarnRemaining').textContent = soundingWarnRemaining;
  document.getElementById('soundingWarnModal').classList.remove('hidden');
  if (soundingWarnIntervalId) clearInterval(soundingWarnIntervalId);
  soundingWarnIntervalId = setInterval(() => {
    if (soundingWarnRemaining > 0) soundingWarnRemaining -= 1;
    document.getElementById('soundingWarnRemaining').textContent = soundingWarnRemaining;
  }, 1000);
}

function hideSoundingWarn() {
  if (soundingWarnIntervalId) { clearInterval(soundingWarnIntervalId); soundingWarnIntervalId = null; }
  document.getElementById('soundingWarnModal').classList.add('hidden');
}

function interruptSounding() {
  if (bridgeConnected) bridgeSend('SOUND_INTERRUPT', { net: soundingWarnNet });
  hideSoundingWarn();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   TEST-CHANNEL sweep panel
   Actively links to a peer on each configured channel, records LQA, terminates,
   advances. Progress + final ranked summary arrive as `test_channel` events.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let testChannelActive = false;
let testChannelRows = [];   // [{id, freq, linked, score, status}]

function testChannelQualLabel(score) {
  if (score < 0) return '—';
  if (score >= 25) return 'Excellent';
  if (score >= 20) return 'Good';
  if (score >= 15) return 'Fair';
  if (score >= 10) return 'Poor';
  return 'Very Poor';
}

function showTestChannelPanel() {
  document.getElementById('testChannelModal').classList.remove('hidden');
}
function hideTestChannelPanel() {
  document.getElementById('testChannelModal').classList.add('hidden');
}

// Clears the previous run's peer, address, progress, summary and per-channel
// rows. Only called from user-initiated open paths (header button, contact
// context menu) — never from onTestChannelEvent's 'start' handler, which
// already writes the new run's values immediately before it reveals the
// panel via showTestChannelPanel().
function resetTestChannelPanel() {
  testChannelActive = false;
  testChannelRows = [];
  const peerEl = document.getElementById('testChannelPeer');
  if (peerEl) peerEl.textContent = '—';
  const progEl = document.getElementById('testChannelProgress');
  if (progEl) progEl.textContent = '0 / 0';
  const sumEl = document.getElementById('testChannelSummary');
  if (sumEl) sumEl.textContent = '';
  const addrEl = document.getElementById('testChannelAddr');
  if (addrEl) addrEl.value = '';
  renderTestChannelRows();
}

// Generic "Test Ch" header-button entry point — no peer context yet, so
// reset before showing.
function openTestChannelPanel() {
  resetTestChannelPanel();
  showTestChannelPanel();
}

function renderTestChannelRows() {
  const body = document.getElementById('testChannelBody');
  if (!body) return;
  if (!testChannelRows.length) { body.innerHTML = '<tr><td colspan="5">—</td></tr>'; return; }
  let html = '';
  for (const r of testChannelRows) {
    const linked = r.linked ? '✓' : '✗';
    const score = r.score < 0 ? '—' : String(r.score);
    html += `<tr>`
         + `<td>${r.id || '—'}</td>`
         + `<td>${r.freq ? r.freq.toLocaleString() : '—'}</td>`
         + `<td class="${r.linked ? 'tc-ok' : 'tc-no'}">${linked}</td>`
         + `<td>${score}</td>`
         + `<td>${testChannelQualLabel(r.score)}</td>`
         + `</tr>`;
  }
  body.innerHTML = html;
}

function onTestChannelEvent(e) {
  const phase = e.phase || '';
  if (phase === 'start') {
    testChannelActive = true;
    testChannelRows = [];
    const total = Number(e.total) || 0;
    for (let i = 0; i < total; ++i)
      testChannelRows.push({ id: '', freq: 0, linked: false, score: -1, status: 'pending' });
    const peerEl = document.getElementById('testChannelPeer');
    if (peerEl) peerEl.textContent = e.peer || '—';
    const progEl = document.getElementById('testChannelProgress');
    if (progEl) progEl.textContent = `0 / ${total}`;
    const sumEl = document.getElementById('testChannelSummary');
    if (sumEl) sumEl.textContent = '';
    renderTestChannelRows();
    showTestChannelPanel();
    aleLogInfo(`Test-Channel sweep to ${e.peer} started (${total} channels)`);
    return;
  }
  if (phase === 'stop') {
    testChannelActive = false;
    aleLogInfo('Test-Channel sweep stopped');
    const sumEl = document.getElementById('testChannelSummary');
    if (sumEl) sumEl.textContent = 'Stopped.';
    return;
  }
  if (phase === 'done') {
    testChannelActive = false;
    const sumEl = document.getElementById('testChannelSummary');
    if (sumEl) sumEl.textContent = e.summary || 'Complete';
    aleLogInfo('Test-Channel sweep complete');
    return;
  }
  // Per-channel progress: tune / linked / failed / terminate
  const idx = (Number(e.index) || 1) - 1;
  if (idx >= 0 && idx < testChannelRows.length) {
    const row = testChannelRows[idx];
    row.id = e.channel_id || row.id;
    row.freq = Number(e.freq_hz) || row.freq;
    if (phase === 'linked') row.linked = true;
    if (phase === 'failed') row.linked = false;
    if (phase === 'terminate' && Number(e.score) >= 0) row.score = Number(e.score);
    row.status = phase;
  }
  const progEl = document.getElementById('testChannelProgress');
  if (progEl) progEl.textContent = `${e.index} / ${e.total}`;
  renderTestChannelRows();
}

function startTestChannel() {
  const addr = (document.getElementById('testChannelAddr') || {}).value || '';
  if (!addr) return;
  if (bridgeConnected) bridgeSend('TEST_CHANNEL', { addr: addr.trim() });
}

function stopTestChannel() {
  if (bridgeConnected) bridgeSend('TEST_CHANNEL_STOP', {});
}

function closeTestChannelPanel() {
  hideTestChannelPanel();
}

// Contextual entry point: right-click a contact in the address book opens the
// Test Channel modal with that callsign pre-filled (no typing). Waits for Start.
function testChannelFromContact(idx) {
  const c = contacts[idx];
  if (!c) return;
  resetTestChannelPanel();
  const inp = document.getElementById('testChannelAddr');
  if (inp) inp.value = c.cs;
  showTestChannelPanel();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   STATE HELPERS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function goIdle() {
  pendingAccept = preClickedAccept = preClickedDecline = isIncomingCall = false;
  stopTimer();
  setStatus('Idle', 'idle');
  showInc(false);
  showCallPanel(false);
}

function goScanning() {
  pendingAccept = preClickedAccept = preClickedDecline = isIncomingCall = false;
  linkedPeer = '';
  stopTimer();
  setStatus('Scanning', 'scanning');
  showInc(false);
  showCallPanel(false);
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CONTACTS / ADDRESS BOOK  (OtherAddr* — A.4.3.4)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
// chans: comma-separated Core channel ids ("C-1,C-2") or "ALL" — matches
// ALEController::add_contact()'s valid_channels format.
let contacts = [];
let selectedContact  = contacts[0];
let editingContactCs = null;

function renderContacts() {
  const q  = (document.getElementById('contactSearch')?.value || '').toUpperCase();
  const el = document.getElementById('contactList');
  // Favorites first, then existing order — honours "pin to top".
  const list = contacts
    .filter(c => !q || c.cs.toUpperCase().includes(q) || (c.name||'').toUpperCase().includes(q))
    .sort((a,b) => (b.fav?1:0) - (a.fav?1:0));
  if (selectedContact && !contacts.includes(selectedContact)) selectedContact = null;
  if (!selectedContact && list.length) selectedContact = list[0];

  const cb = document.getElementById('callBtn');
  if (cb) cb.disabled = !selectedContact;

  el.innerHTML = list.length ? list.map(c => {
    const idx = contacts.indexOf(c);
    const sel = c === selectedContact ? ' sel' : '';
    return `<div class="contact-item${sel}" onclick="pickContact(${idx})"
      oncontextmenu="event.preventDefault();testChannelFromContact(${idx})"
      title="Right-click → Test all channels to this peer">
      <div class="contact-avatar">${icon('user',16)}</div>
      <div class="contact-info">
        <div class="contact-cs">${c.cs}</div>
        <div class="contact-name">${escapeHtml(c.name||'')}</div>
      </div>
      <div class="contact-actions">
        ${c.fav ? '<div class="contact-star">★</div>' : ''}
        <button class="contact-edit" title="Edit" onclick="event.stopPropagation();openContactEditor(${idx})">${icon('pencil',12)}</button>
      </div>
    </div>`;
  }).join('') : '<div class="msg-empty">No contacts</div>';
}

function pickContact(i) {
  selectedContact = contacts[i];
  renderContacts();
  if (bridgeConnected) bridgeSend('CONTACT_SELECT', { callsign: selectedContact.cs });
}

function syncContactsFromBridge() {
  bridgeSend('CONTACTS_LIST', {}, (r) => {
    if (!r.ok) return;
    const prevSel = selectedContact?.cs;
    const favs = new Set(contacts.filter(c => c.fav).map(c => c.cs));
    contacts = r.data.map(c => ({ cs: c.callsign, name: c.name, fav: favs.has(c.callsign) }));
    selectedContact = contacts.find(c => c.cs === prevSel) || contacts[0] || null;
    renderContacts();
  });
}

function openContactEditor(idx) {
  const c = (typeof idx === 'number') ? contacts[idx] : null;
  editingContactCs = c ? c.cs : null;
  const cc = c || { cs:'', name:'', fav:false };
  document.getElementById('ceCs').value    = cc.cs;
  document.getElementById('ceName').value  = cc.name || '';
  document.getElementById('ceFav').checked = !!cc.fav;
  document.getElementById('ceDelete').style.display = editingContactCs !== null ? '' : 'none';
  document.getElementById('ceTitle').textContent    = editingContactCs !== null ? 'Edit Contact' : 'Add Contact';
  document.getElementById('contactModal').classList.remove('hidden');
  document.getElementById('ceCs').focus();
}

function closeContactEditor() {
  document.getElementById('contactModal').classList.add('hidden');
  editingContactCs = null;
}

// editingContactCs (not a raw index) is re-resolved here against the *current*
// contacts array — the editor modal can stay open across an async round trip
// (e.g. a Heard-Station auto-promotion elsewhere resyncing `contacts`), so a
// held index could point at the wrong entry by the time Save/Delete fires.
function saveContact() {
  const cs = (document.getElementById('ceCs').value || '').toUpperCase().trim();
  if (!cs) { document.getElementById('ceCs').focus(); return; }
  const c = {
    cs,
    name: document.getElementById('ceName').value.trim(),
    fav:  document.getElementById('ceFav').checked,
  };
  const editingIdx = editingContactCs !== null ? contacts.findIndex(x => x.cs === editingContactCs) : -1;
  const prevCs = editingIdx >= 0 ? contacts[editingIdx].cs : null;
  if (editingIdx >= 0) contacts[editingIdx] = c;
  else { contacts.push(c); selectedContact = c; }
  closeContactEditor();
  renderContacts();
  if (bridgeConnected) {
    if (prevCs && prevCs !== cs) bridgeSend('CONTACT_DEL', { callsign: prevCs });
    bridgeSend('CONTACT_ADD', { callsign: c.cs, name: c.name }, () => syncContactsFromBridge());
  }
}

function deleteContact() {
  let removedCs = null;
  const editingIdx = editingContactCs !== null ? contacts.findIndex(x => x.cs === editingContactCs) : -1;
  if (editingIdx >= 0) {
    removedCs = contacts[editingIdx].cs;
    if (contacts[editingIdx] === selectedContact) selectedContact = null;
    contacts.splice(editingIdx, 1);
  }
  closeContactEditor();
  renderContacts();
  if (bridgeConnected && removedCs) bridgeSend('CONTACT_DEL', { callsign: removedCs }, () => syncContactsFromBridge());
}

// ── AllCall accept ────────────────────────────────────────────────────────────

function syncAllCallAcceptFromBridge() {
  bridgeSend('ALLCALL_GET', {}, (r) => {
    if (!r.ok) return;
    const el = document.getElementById('cfgAcceptAll');
    if (el && typeof r.accept === 'boolean') el.checked = r.accept;
  });
}

function applyAllCallAcceptToBridge() {
  const el = document.getElementById('cfgAcceptAll');
  if (!el || !bridgeConnected) return;
  bridgeSend('ALLCALL_SET', { accept: el.checked });
}

function toggleCallModePanel() {
  const p = document.getElementById('callModePanel');
  if (!p) return;
  const nowHidden = p.classList.toggle('hidden');
  const b = document.getElementById('callBtn');
  if (b) b.innerHTML = nowHidden ? `${icon('phone',14)} Call ▸` : `${icon('phone',14)} Call ▾`;
}

function closeCallModePanel() {
  document.getElementById('callModePanel')?.classList.add('hidden');
  const b = document.getElementById('callBtn');
  if (b) b.innerHTML = `${icon('phone',14)} Call ▸`;
}

function startCall(single) {
  if (!selectedContact || !bridgeConnected) return;
  closeCallModePanel();
  setStatus('Calling…', 'calling');   // cosmetic; real transition comes from CALLING/link_established
  bridgeSend('CALL', { addr: selectedContact.cs, single_channel: !!single });
}

// Manual accept is a POST-link decision: the 3-way handshake completes
// automatically (interoperable with MIL-STD-188-141B Twr/Twrt) and the link is
// already up when the operator is prompted. Answer blesses the established link
// (clears the core's pending-operator gate → active call panel); Decline sends
// REJECT → the core terminates with TWAS → link_terminated → goScanning.
// With Auto-Accept ON the link_established handler already shows the call
// panel, so Answer/Decline are just dismiss/operator-drop.
let pendingAccept    = false;
let preClickedAccept  = false;
let preClickedDecline = false;
let isIncomingCall    = false;  // true = we are the CALLED station (set on call_received)
function autoAcceptOn() { return !!(document.getElementById('cfgAutoAccept')?.checked); }

function answerCall() {
  if (pendingAccept) {
    // Post-link: operator confirmed "Linked — accept?"
    pendingAccept = false;
    if (bridgeConnected) bridgeSend('ACCEPT', {});
    showInc(false);
    setStatus('Linked', 'linked');
    showCallPanel(true);
    callStart = Date.now();
    timerId   = setInterval(tickTimer, 1000);
    tickTimer();
    return;
  }
  // Pre-link: operator clicked Answer before link_established fired — queue it
  preClickedAccept = true;
  document.getElementById('incName').textContent = 'Connecting…';
}
function declineCall() {
  if (pendingAccept) {
    // Post-link: operator rejected "Linked — accept?"
    pendingAccept = false;
    showInc(false);
    if (bridgeConnected) { bridgeSend('REJECT', {}); return; }  // core terminates → link_terminated → goScanning
    goScanning();
  } else {
    // Pre-link: operator clicked Decline before link_established fired — queue it
    preClickedDecline = true;
    showInc(false);
  }
}
function endCall() {
  stopTimer();
  if (bridgeConnected) { bridgeSend('TERMINATE', {}); return; }
  goScanning();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SETTINGS MODAL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function openSettings() {
  document.getElementById('settingsModal').classList.remove('hidden');
  showSec('identity');
  enumDevices();
  enumVoiceDevices();
  populateRigDropdown();
}

// Closing Settings does NOT auto-tune the radio — per the workflow, no
// network is selected initially and therefore no frequency is set. The operator
// selects a network in the header pill, which tunes to that net's first channel.
// We do re-pull nets/channels from core so the main-GUI network pill and channel
// readout reflect any membership/channel edits the operator just made (net
// assignments are pushed live during editing; this sync guarantees the pill
// matches core truth even if local state drifted).
function closeSettings() {
  document.getElementById('settingsModal').classList.add('hidden');
  if (bridgeConnected) {
    syncChannelsFromBridge();
    syncNetsFromBridge();   // re-renders the network pill list from core truth
  }
}

function overlayClose(e) {
  if (e.target === document.getElementById('settingsModal')) closeSettings();
}

function showSec(sec) {
  document.querySelectorAll('.snav-item').forEach(el =>
    el.classList.toggle('active', el.dataset.sec === sec));
  document.querySelectorAll('.ssec').forEach(el =>
    el.classList.toggle('active', el.dataset.sec === sec));
  // Auto-expand Advanced nav group when navigating to an advanced section
  const advSecs = ['voice','location','nets','timing','policy','lqa','fec','logging','files','misc'];
  if (advSecs.includes(sec)) {
    const items = document.getElementById('advNavItems');
    if (items && !items.classList.contains('open')) {
      items.classList.add('open');
      const caret = document.getElementById('advNavCaret');
      if (caret) caret.textContent = '▾';
    }
  }
}

// Rig connection-field visibility, driven by the selected Hamlib model's port
// type (network → Host/Port, serial → device/baud/line-state, other/none →
// nothing). The model dropdown is the single selector; there is no separate
// backend radio group. rigPortTypeById is populated by populateRigDropdown().
let rigPortTypeById = {};
function updateRigFields() {
  const sel = document.getElementById('rigModel');
  const ptype = rigPortTypeById[sel?.value ?? ''] || '';
  const tcp = document.getElementById('rigFieldsTcp');
  const ser = document.getElementById('rigFieldsSerial');
  if (tcp) tcp.style.display = ptype === 'network' ? '' : 'none';
  if (ser) ser.style.display = ptype === 'serial'  ? '' : 'none';
}

// Populate the Hamlib model dropdown from the bridge's RIG_LIST reply.
// Groups entries by manufacturer using <optgroup>. Restores any previously
// selected model number after rebuilding the list. Records each model's port
// type (network/serial/other) so updateRigFields() can adapt the connection
// fields. Defaults to NET rigctl (model 2) on first load — preserves the prior
// TCP-netrigctl default — then re-evaluates field visibility.
function populateRigDropdown() {
  const sel = document.getElementById('rigModel');
  if (!sel || !bridgeConnected) return;
  const prev = sel.value;
  bridgeSend('RIG_LIST', {}, (r) => {
    if (!r.ok || !Array.isArray(r.rigs)) return;
    sel.innerHTML = '';
    rigPortTypeById = { '': '' };  // "None / Offline" → no fields
    let grp = null, lastMfg = null;
    for (const e of r.rigs) {
      if (e.mfg !== lastMfg) {
        grp = document.createElement('optgroup');
        grp.label = e.mfg;
        sel.appendChild(grp);
        lastMfg = e.mfg;
      }
      const opt = document.createElement('option');
      opt.value = String(e.id);
      opt.textContent = e.macro;
      rigPortTypeById[String(e.id)] = e.port || 'other';
      grp.appendChild(opt);
    }
    if (prev && [...sel.options].some(o => o.value === prev)) {
      sel.value = prev;
    } else if ([...sel.options].some(o => o.value === '2')) {
      sel.value = '2';   // NET rigctl — default network backend
    } else {
      sel.value = '';
    }
    updateRigFields();
    qsMirrorRig();
  });
}

// Populate the RX/TX device dropdowns with real WASAPI devices from the bridge's
// AUDIO_DEVICES reply (option value is the bare name AudioDevice::open() matches —
// "IN: "/"OUT: " prefix stripped). Remembers the operator's device choice so
// reopening Settings does not reset it to the first entry.
let audioInSelected  = '';
let audioOutSelected = '';

// Re-select a remembered device if it's still present in the rebuilt list.
function restoreAudioSelection() {
  const sel = (id, want) => {
    const el = document.getElementById(id);
    if (want && [...el.options].some(o => o.value === want)) el.value = want;
  };
  sel('audioIn',  audioInSelected);
  sel('audioOut', audioOutSelected);
}

function onAudioInChange()  { audioInSelected  = document.getElementById('audioIn').value; }
function onAudioOutChange() { audioOutSelected = document.getElementById('audioOut').value; }

function enumDevices() {
  if (bridgeConnected) {
    bridgeSend('AUDIO_DEVICES', {}, (r) => {
      if (!r.ok) return;
      // ALSA entries are "IN:/OUT: NAME — DESC" (see alsa_audio.cpp list_devices);
      // resolve_device() matches on NAME alone, so the option value must drop the
      // " — DESC" suffix too — leaving it in feeds ALSA's plughw: parser a comma-
      // laden description and produces "Parameter DEV must be an integer".
      const strip = s => s.replace(/^(IN:|OUT:)\s*/, '').replace(/ — .*$/, '');
      const mkOpt = s => { const n = strip(s); return `<option value="${escapeHtml(n)}">${escapeHtml(s.replace(/^(IN:|OUT:)\s*/, ''))}</option>`; };
      document.getElementById('audioIn').innerHTML  = (r.inputs  || []).map(mkOpt).join('')  || '<option value="">— none —</option>';
      document.getElementById('audioOut').innerHTML = (r.outputs || []).map(mkOpt).join('') || '<option value="">— none —</option>';
      restoreAudioSelection();
      qsMirrorAudio();
    });
    return;
  }
  // Demo fallback (no bridge): show whatever the browser exposes.
  (async () => {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      stream.getTracks().forEach(t => t.stop());
      const devs = await navigator.mediaDevices.enumerateDevices();
      const mkOpt = d => `<option value="${d.deviceId}">${d.label || d.deviceId.slice(0,20)}</option>`;
      document.getElementById('audioIn').innerHTML  = devs.filter(d => d.kind === 'audioinput').map(mkOpt).join('');
      document.getElementById('audioOut').innerHTML = devs.filter(d => d.kind === 'audiooutput').map(mkOpt).join('');
    } catch {
      document.getElementById('audioIn').innerHTML  = '<option>— start openALE to list devices —</option>';
      document.getElementById('audioOut').innerHTML = '<option>— start openALE to list devices —</option>';
    }
  })();
}

// Dedicated audio Connect/Close (own button, not the settings Save). Sends
// AUDIO_OPEN with the selected device names; the bridge opens the real WASAPI
// device and attaches it to the controller (ale_bridge.cpp AUDIO_OPEN).
let audioOpen = false;
function openAudioDevice() {
  if (!bridgeConnected) { return; }
  const btn = document.getElementById('audioConnectBtn');
  if (audioOpen) {
    bridgeSend('AUDIO_CLOSE', {}, () => {
      audioOpen = false;
      if (btn) { btn.innerHTML = `${icon('power',12)} Connect Audio`; btn.classList.remove('scan-on'); }
    });
    return;
  }
  const inName  = document.getElementById('audioIn').value;
  const outName = document.getElementById('audioOut').value;
  if (btn) btn.textContent = '⟳ Opening…';
  bridgeSend('AUDIO_OPEN', { in: inName, out: outName }, (r) => {
    audioOpen = !!r.ok;
    if (r.ok) {
      audioInSelected = inName; audioOutSelected = outName;
      const vol = document.getElementById('cfgTxVol');
      if (vol) bridgeSend('AUDIO_SET_VOL', { vol: vol.value / 100 });
    }
    if (btn) {
      btn.innerHTML = r.ok ? `${icon('square',12)} Close Audio` : `${icon('power',12)} Connect Audio`;
      btn.classList.toggle('scan-on', !!r.ok);
    }
    if (!r.ok) aleLogInfo('Audio open failed: ' + (r.error || '?'));
  });
}

// Level meter: polls AUDIO_LEVEL from the bridge at 120 ms. Toggle on/off.
let levelTimer = null;
function testAudio() {
  if (levelTimer) { clearInterval(levelTimer); levelTimer = null; return; }
  if (!bridgeConnected) return;
  levelTimer = setInterval(() => {
    bridgeSend('AUDIO_LEVEL', {}, (r) => {
      if (r.ok) document.getElementById('levelBarIn').style.width = Math.round(Math.min(1, r.level) * 100) + '%';
    });
  }, 120);
}

// TX volume — label + live send to bridge when audio is open
function txVolTodBFS(v) {
  if (v <= 0) return '−∞ dBFS';
  const db = Math.round(20 * Math.log10(v / 100));
  return (db >= 0 ? '+' : '') + db + ' dBFS';
}
document.getElementById('cfgTxVol')?.addEventListener('input', function() {
  document.getElementById('txVolLbl').textContent = txVolTodBFS(+this.value);
  if (audioOpen) bridgeSend('AUDIO_SET_VOL', { vol: this.value / 100 });
});

// Live CAT-link state (bridge attached a real pal::IRadio). Drives the Connect
// button label and the radio-control lock (see setRadioCtrlEnabled).
let rigConnected = false;

// Read the structured rig fields the bridge needs for create_radio(). The model
// is the single selector; the bridge derives the connection kind from it, so
// there is no separate "backend" field. Host/Port and serial fields are both
// read regardless — the bridge uses only the ones matching the model's port type.
function rigArgs() {
  return {
    model:   document.getElementById('rigModel').value,
    host:    document.getElementById('rigHost').value,
    port:    document.getElementById('rigPort').value,
    serial:  document.getElementById('rigSerial').value,
    baud:    parseInt(document.getElementById('rigBaud')?.value, 10) || 0,
    dtr:     document.getElementById('rigDtr')?.value  ?? 'on',
    rts:     document.getElementById('rigRts')?.value  ?? 'on',
    stab:    parseInt(document.getElementById('rigStab')?.value, 10) || 200,
    ptt:     document.getElementById('rigPttInput')?.value ?? 'normal',
  };
}

// Test Connection — reachability probe ONLY. Does not establish or change the
// live link (RIG_TEST builds a throwaway radio on the bridge, probes, tears it
// down). Reports reachable/unreachable in the status pill.
function testRig() {
  const el = document.getElementById('rigConnStatus');
  el.classList.remove('hidden', 'ok', 'err');
  if (!bridgeConnected) {
    el.classList.add('err');
    el.textContent = '✗ Not connected to openALE — start it first';
    return;
  }
  el.textContent = '⟳ Testing…';
  bridgeSend('RIG_TEST', rigArgs(), (r) => {
    const ok = r.ok && r.reachable;
    el.classList.add(ok ? 'ok' : 'err');
    el.textContent = (ok ? '✓ reachable' : '✗ ') + (ok ? '' : (r.error || r.status || 'unreachable'));
  });
}

// Connect / Disconnect — actually establishes (or tears down) the live CAT
// link. RIG_CONNECT attaches a real radio to the controller; RIG_DISCONNECT
// detaches it. Updates rigConnected + the radio-control lock from the reply.
function connectRig() {
  const el = document.getElementById('rigConnStatus');
  el.classList.remove('hidden', 'ok', 'err');
  if (!bridgeConnected) {
    el.classList.add('err');
    el.textContent = '✗ Not connected to openALE — start it first';
    return;
  }
  const model = document.getElementById('rigModel').value;
  if (rigConnected || model === '') {
    el.textContent = rigConnected ? '⟳ Disconnecting…' : '⟳ …';
    bridgeSend('RIG_DISCONNECT', {}, (r) => {
      applyRigState(false);
      el.classList.add('ok');
      el.textContent = model === '' ? '○ offline (no radio)' : '○ disconnected';
    });
    return;
  }
  el.textContent = '⟳ Connecting…';
  bridgeSend('RIG_CONNECT', rigArgs(), (r) => {
    const ok = !!(r.ok && r.connected);
    applyRigState(ok);
    el.classList.add(ok ? 'ok' : 'err');
    el.textContent = (ok ? '✓ ' : '✗ ') + (r.status || r.error || (ok ? 'connected' : 'failed'));
  });
}

// Central rig-state apply: reflects on the Connect button and (de)activates all
// radio-control UI. Requires a live bridge connection.
function applyRigState(connected) {
  rigConnected = connected;
  const btn = document.getElementById('rigConnectBtn');
  if (btn) {
    btn.innerHTML = connected ? `${icon('square',12)} Disconnect` : `${icon('power',12)} Connect`;
    btn.classList.toggle('scan-on', connected);
  }
  setRadioCtrlEnabled(!bridgeConnected || connected);
}

// Lock/unlock every control that drives the radio: channel steppers, PTT, the
// VFO panel toggle and its whole keypad. When locking, also close the panel.
function setRadioCtrlEnabled(on) {
  ['pttBtn', 'radioToggle'].forEach(id => {
    const el = document.getElementById(id); if (el) el.disabled = !on;
  });
  document.querySelectorAll('.hdr-icon').forEach(b => b.disabled = !on);          // channel steppers
  document.querySelectorAll('#radioPanel button').forEach(b => b.disabled = !on);  // VFO keypad
  if (!on) {
    const p = document.getElementById('radioPanel');
    if (p && p.classList.contains('open')) {
      p.classList.remove('open');
      const t = document.getElementById('radioToggle'); if (t) t.innerHTML = `${icon('radio',14)} Radio ▸`;
    }
  }
}

// Poll the live rig state so a radio that drops out (cable pulled, rigctld died)
// is noticed and the controls re-lock — and a recovered one re-enables them.
function pollRigStatus() {
  bridgeSend('RIG_STATUS', {}, (r) => {
    if (!r.ok) return;
    const connected = !!r.connected;
    if (connected !== rigConnected) {  // log only on real transitions
      if (rigConnected && !connected)
        aleLogInfo('Radio connection lost — controls locked');
      else if (connected)
        aleLogInfo('Radio connected');
    }
    // Re-assert every poll so the lock is correct even on the first sync (when
    // both are already false) and tracks the bridge-connected state too.
    applyRigState(connected);
  });
}

// Channel table management — data-driven cards.
//   id   : "C-<n>" — auto-assigned, matches ALEController::next_free_channel_id()
//          (Core/src/App/ale_controller.cpp). Not user-editable: net membership
//          and contact valid_channels both reference this exact id format.
//   self : '' = use primary self address; otherwise one of the SelfAddrTable entries
//   inhCall / inhSnd : exclude this channel from outbound calling / sounding
let channels = [
  { id:'C-1', rx:'14109000', tx:'14109000', mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'Primary',    inhCall:false, inhSnd:false, inhRep:false, txOnly:false },
  { id:'C-2', rx:'7102000',  tx:'7102000',  mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'40m Backup', inhCall:false, inhSnd:false, inhRep:false, txOnly:false },
  { id:'C-3', rx:'3596000',  tx:'3596000',  mode:'USB', usage:'VOICE', dir:'RX/TX', self:'', label:'80m Night',  inhCall:false, inhSnd:false, inhRep:false, txOnly:false },
];

// Smallest unused "C-<n>" id (n >= 1) — mirrors next_free_channel_id() in
// Core/src/App/ale_controller.cpp so IDs assigned here match what the Core
// would assign for the same channel list.
function nextFreeChannelId(list) {
  for (let n = 1; ; n++) {
    const candidate = 'C-' + n;
    if (!list.some(c => c.id === candidate)) return candidate;
  }
}

const CH_MODES = ['USB', 'USB-D', 'LSB', 'LSB-D', 'AM', 'FM', 'CWU', 'CWL'];
const CH_USAGE = ['VOICE', 'DATA', 'BOTH'];
const CH_DIRS  = ['RX/TX', 'RX', 'TX'];

function opts(list, sel) {
  return list.map(v => `<option${v === sel ? ' selected' : ''}>${v}</option>`).join('');
}
function selfAddrOpts(sel) {
  const addrs = selfAddrs.map(s => s.addr).filter(Boolean);
  if (sel && !addrs.includes(sel)) addrs.unshift(sel);   // keep a now-removed addr visible
  return `<option value=""${sel ? '' : ' selected'}>— Primary —</option>` +
         addrs.map(a => `<option${a === sel ? ' selected' : ''}>${escapeHtml(a)}</option>`).join('');
}

function renderChannels() {
  const el = document.getElementById('chBody');
  if (!el) return;
  el.innerHTML = channels.map((c, i) => `
    <div class="ch-card">
      <div class="ch-card-top">
        <div class="ch-field ch-id-field">
          <label>ID</label>
          <input class="ch-inp ch-id-inp" data-f="id" value="${escapeHtml(c.id)}"
                 title="Channel id. New channels auto-get C-n; loaded .ale files keep their id (e.g. 03D). Editable — renames the channel and updates net membership."
                 onblur="chCommitId(${i})">
        </div>
        <div class="ch-field">
          <label>RX Hz</label>
          <input class="ch-inp" data-f="rx" value="${escapeHtml(c.rx)}" placeholder="14000000" oninput="chSet(${i},'rx',this.value)" onblur="chCommit(${i})">
        </div>
        <div class="ch-field">
          <label>TX Hz</label>
          <input class="ch-inp" data-f="tx" value="${escapeHtml(c.tx)}" placeholder="same as RX" title="Leer = wie RX (Simplex). Wert eintragen für Full-Duplex (TX ≠ RX)." oninput="chSet(${i},'tx',this.value)" onblur="chCommit(${i})">
        </div>
        <div class="ch-field ch-grow">
          <label>Name</label>
          <input class="ch-inp" value="${escapeHtml(c.label)}" placeholder="e.g. 03DALE" oninput="chSet(${i},'label',this.value)" onblur="chCommit(${i})">
        </div>
        <button class="ch-del" onclick="delCh(${i})" title="Delete channel">✕</button>
      </div>
      <div class="ch-card-row">
        <div class="ch-field">
          <label>Mode</label>
          <select class="ch-sel" onchange="chSet(${i},'mode',this.value);chCommit(${i})">${opts(CH_MODES, c.mode)}</select>
        </div>
        <div class="ch-field">
          <label>Usage</label>
          <select class="ch-sel" onchange="chSet(${i},'usage',this.value);chCommit(${i})">${opts(CH_USAGE, c.usage)}</select>
        </div>
        <div class="ch-field">
          <label>Direction</label>
          <select class="ch-sel" onchange="chSet(${i},'dir',this.value);chCommit(${i})">${opts(CH_DIRS, c.dir)}</select>
        </div>
        <div class="ch-field">
          <label>Self Address</label>
          <select class="ch-sel" onchange="chSet(${i},'self',this.value)">${selfAddrOpts(c.self)}</select>
        </div>
      </div>
      <div class="ch-card-inh">
        <label class="ch-check">
          <input type="checkbox" ${c.inhCall ? 'checked' : ''} onchange="chSet(${i},'inhCall',this.checked);chCommit(${i})"> Inhibit Calling
        </label>
        <label class="ch-check">
          <input type="checkbox" ${c.inhSnd ? 'checked' : ''} onchange="chSet(${i},'inhSnd',this.checked);chCommit(${i})"> Inhibit Sounding
        </label>
        <label class="ch-check">
          <input type="checkbox" ${c.inhRep ? 'checked' : ''} onchange="chSet(${i},'inhRep',this.checked);chCommit(${i})" title="Exclude this channel from the bilateral LQA CMD 'a' exchange (A.5.4.2). Local LQA measurements are still recorded."> Inhibit Reporting
        </label>
        <label class="ch-check">
          <input type="checkbox" ${c.aleOnly ? 'checked' : ''} onchange="chSet(${i},'aleOnly',this.checked);chCommit(${i})" title="Channel carries ALE traffic exclusively (A.5.4.7.1): permits the short 784 ms listen-before-transmit pause. Unchecked = shared channel, LBT waits >= 2 s."> ALE-only (short LBT)
        </label>
      </div>
    </div>`).join('');
}

// Mutate model in place — no re-render, so text inputs keep focus/caret while typing.
function chSet(i, field, val) {
  const c = channels[i];
  if (field === 'rx') {
    // Auto-mirror RX → TX while they're "linked": TX is empty (placeholder
    // "same as RX") or TX still equals the previous RX. This restores the
    // earlier behaviour where entering RX Hz filled TX Hz with the same value.
    // The link breaks the moment the operator sets TX to a divergent value
    // (full-duplex, TX ≠ RX); clearing TX re-establishes it. We update the TX
    // input element live (no re-render) so the mirroring is visible per
    // keystroke without disturbing focus in the RX field.
    const oldRx = c.rx;
    c.rx = val;
    if (c.tx === '' || c.tx === oldRx) {
      c.tx = val;
      const card = document.querySelectorAll('#chBody .ch-card')[i];
      const txInp = card && card.querySelector('[data-f="tx"]');
      if (txInp && document.activeElement !== txInp) txInp.value = val;
    }
    return;
  }
  c[field] = val;
}

// Sync one row to the bridge once it has a usable RX frequency (fires on
// blur/select-change — not per keystroke). ALEController::add_channel()
// matches/replaces by rx_frequency_hz, so editing RX Hz on an existing
// channel creates a new entry rather than renaming the old one — same
// behaviour as editing a .ale file by hand; not something the GUI papers
// over here. Core's Channel has no per-channel self-address (GUI-only), but
// the three inhibit flags + rx_only/tx_only DO round-trip independently now —
// `enabled` is derived as !(inhCall && inhSnd) so a fully-inhibited channel
// drops out of scan (unchanged scan behaviour), while each inhibit flag
// gates its own protocol path (calling / sounding / bilateral LQA CMD 'a').
// Direction RX/TX|RX|TX maps to rx_only|tx_only: RX blocks all TX, TX blocks
// all RX (excluded from scan). The reply acks to the GUI log so the operator
// sees exactly what was applied.
function chCommit(i) {
  if (!bridgeConnected) return;
  const c = channels[i];
  const rxHz = parseInt(c.rx, 10);
  if (!rxHz) return;  // nothing to sync yet — still being typed
  bridgeSend('CHANNEL_ADD', {
    id: c.id,
    rx_hz: rxHz,
    tx_hz: parseInt(c.tx, 10) || rxHz,
    mode: c.mode,
    label: c.label,
    enabled: !(c.inhCall && c.inhSnd),
    rx_only: c.dir === 'RX',
    tx_only: c.dir === 'TX',
    voice_use: c.usage !== 'DATA',
    data_use: c.usage !== 'VOICE',
    inhibit_calling: c.inhCall,
    inhibit_sounding: c.inhSnd,
    inhibit_reporting: c.inhRep,
    ale_only: c.aleOnly,
  }, (r) => {
    if (r && r.ok) {
      aleLogInfo('✓ ' + c.id + ' saved — Dir: ' + c.dir
                 + ', InhCall: ' + (c.inhCall ? 'on' : 'off')
                 + ', InhSnd: ' + (c.inhSnd ? 'on' : 'off')
                 + ', InhRep: ' + (c.inhRep ? 'on' : 'off'));
    } else {
      aleLogInfo('✗ ' + c.id + ' save rejected by core');
    }
  });
}

// Commit an ID edit: rename the channel (old id → new id) in core + locally.
// The ID input is read on blur, so the live c.id — which net membership
// references — only changes on a confirmed rename (typing does NOT mutate it).
// Validates non-empty + unique; a rejection restores the input to the old id.
function chCommitId(i) {
  const c = channels[i];
  if (!c) return;
  const card = document.querySelectorAll('#chBody .ch-card')[i];
  const inp = card && card.querySelector('[data-f="id"]');
  if (!inp) return;
  const newId = (inp.value || '').trim().toUpperCase();
  const oldId = c.id;
  if (!newId || newId === oldId) { inp.value = oldId; return; }
  if (channels.some((o, j) => j !== i && o.id === newId)) {
    aleLogInfo('Channel ID "' + newId + '" already in use — keeping ' + oldId);
    inp.value = oldId;
    return;
  }
  if (bridgeConnected) {
    bridgeSend('CHANNEL_RENAME', { old_id: oldId, new_id: newId }, (r) => {
      if (!r || !r.ok) {
        inp.value = oldId;
        aleLogInfo('Channel rename to "' + newId + '" rejected — keeping ' + oldId);
        return;
      }
      applyChannelRename(i, oldId, newId);
    });
  } else {
    applyChannelRename(i, oldId, newId);
  }
}

// Apply a confirmed rename locally: set the channel id, replace the old id in
// every net's channelIds, and re-render channel cards + net membership so the
// new id shows everywhere. Re-render is safe (fired on blur, focus leaving).
function applyChannelRename(i, oldId, newId) {
  channels[i].id = newId;
  for (const n of nets) {
    const k = n.channelIds.indexOf(oldId);
    if (k !== -1) n.channelIds[k] = newId;
  }
  renderChannels();
  renderNets();
  renderNetPill();
}

function syncChannelsFromBridge() {
  bridgeSend('CHANNELS_LIST', {}, (r) => {
    if (!r.ok) return;
    const prevById = new Map(channels.map(c => [c.id, c]));
    channels = r.data.map(c => ({
      id: c.id, rx: String(c.rx_hz),
      // Collapse simplex (tx == rx, or core's tx=0 convention) to '' so the TX
      // field shows the "same as RX" placeholder and the RX→TX auto-link stays
      // active across a sync round-trip. A genuinely divergent TX is preserved.
      tx: (c.tx_hz && c.tx_hz !== c.rx_hz) ? String(c.tx_hz) : '',
      mode: c.mode, label: c.label,
      usage: c.voice_use && c.data_use ? 'BOTH' : c.data_use ? 'DATA' : 'VOICE',
      dir: c.rx_only ? 'RX' : c.tx_only ? 'TX' : 'RX/TX',
      self: prevById.get(c.id)?.self ?? '',       // preserved: core has no per-channel self-addr
      inhCall: c.inhibit_calling,                 // independent per-channel flags now
      inhSnd:  c.inhibit_sounding,
      inhRep:  c.inhibit_reporting,
      aleOnly: c.ale_only,                        // A.5.4.7.1: short-LBT permission
    }));
    renderChannels();
    renderNets();
    renderSoundPanel();   // net channel labels in the sounding dropdown
    updateScanBtn();   // channel count changed → refresh Scan button gating
    updateSetupBanner();
  });
}

function addCh() {
  channels.push({ id:nextFreeChannelId(channels), rx:'', tx:'', mode:'USB', usage:'BOTH', dir:'RX/TX', self:'', label:'', inhCall:false, inhSnd:false, inhRep:false, aleOnly:false, txOnly:false });
  renderChannels();
  renderNets();   // new channel id becomes selectable in net membership
  updateScanBtn();
  const cards = document.querySelectorAll('#chBody .ch-card');
  const inps  = cards[cards.length - 1]?.querySelectorAll('.ch-inp');
  if (inps && inps[0]) inps[0].focus();   // focus the new card's RX Hz field (ID is no longer an input)
}

function delCh(i) {
  const removedId = channels[i].id;
  const rxHz = parseInt(channels[i].rx, 10);
  channels.splice(i, 1);
  nets.forEach(n => { n.channelIds = n.channelIds.filter(id => id !== removedId); });  // mirrors unassign_channel_everywhere()
  renderChannels();
  renderNets();
  updateScanBtn();
  if (bridgeConnected && rxHz) bridgeSend('CHANNEL_DEL', { rx_hz: rxHz }, () => { syncChannelsFromBridge(); syncNetsFromBridge(); });
}

// Nets — mirrors NetStore (Core/include/Stores/ale_data_store.h): a net is a
// name + a set of assigned channel ids (channels[].id, "C-n"). Used to size
// the scanning-call section for calls to a contact whose Net Members list
// resolves to one of these (see ALEController::initiate_call() docs).
let nets = [
  { name:'NET1', channelIds:['C-1'], dwellMs:200, scanEnabled:true, soundEnabled:false, soundIntervalSec:300, callingC:10 },
];
const netChExpanded = new Set();

function netFmtFreq(hzStr) {
  const hz = Number(hzStr);
  if (!hz) return '—';
  const mhz    = Math.trunc(hz / 1_000_000);
  const kpart  = Math.trunc((hz % 1_000_000) / 1_000);
  const hzpart = hz % 1_000;
  return `${mhz}.${String(kpart).padStart(3, '0')}.${String(hzpart).padStart(3, '0')}`;
}

function toggleNetChannelSection(name) {
  if (netChExpanded.has(name)) netChExpanded.delete(name);
  else netChExpanded.add(name);
  renderNets();
}

function renderNets() {
  const el = document.getElementById('netList');
  if (!el) return;
  if (!nets.length) {
    el.innerHTML = '<div class="msg-empty">No nets configured</div>';
    return;
  }
  el.innerHTML = nets.map((n, i) => {
    const isActive  = n.name === activeNet;
    const memberCnt = n.channelIds.length;
    const expanded  = netChExpanded.has(n.name);
    const safeName  = escapeHtml(n.name);

    const chips = channels.length
      ? channels.map(c => {
          const on   = n.channelIds.includes(c.id);
          const freq = netFmtFreq(c.rx);
          const tip  = c.label ? escapeHtml(c.label) : '';
          return `<label class="net-ch-chip${on ? ' on' : ''}" title="${tip}">
            <input type="checkbox" ${on ? 'checked' : ''}
              onchange="toggleNetChannel(${i},'${c.id}',this.checked);renderNets()">
            <span class="ch-chip-id">${escapeHtml(c.id)}</span>
            <span class="ch-chip-freq">${freq}</span>
            <span class="ch-chip-mode">${escapeHtml(c.mode)}</span>
          </label>`;
        }).join('')
      : '<span class="fhint" style="padding:4px 0;grid-column:1/-1">No channels configured yet</span>';

    return `
<div class="net-card${isActive ? ' active' : ''}">
  <div class="net-card-hdr">
    <input class="net-name-inp" value="${safeName}"
      onblur="netCommitName(${i})" title="Net name — click to rename">
    ${isActive ? '<span class="net-active-badge">ACTIVE</span>' : ''}
    <span class="net-count-badge">${memberCnt} ch</span>
    <button class="net-del-btn" onclick="delNet(${i})" title="Delete net">
      <svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <polyline points="3 6 5 6 21 6"/>
        <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/>
        <path d="M10 11v6"/><path d="M14 11v6"/>
        <path d="M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2"/>
      </svg>
    </button>
  </div>
  <div class="net-card-policy">
    <label class="net-policy-grp">
      <input type="checkbox" ${n.scanEnabled ? 'checked' : ''}
        onchange="netPolicySet(${i},'scanEnabled',this.checked);renderNets()">
      Scan
    </label>
    <label class="net-policy-grp">
      Dwell&nbsp;<input type="number" value="${n.dwellMs}" min="200"
        ${n.scanEnabled ? '' : 'disabled'}
        oninput="netPolicySet(${i},'dwellMs',+this.value)">&nbsp;ms
    </label>
    <div class="net-policy-sep"></div>
    <label class="net-policy-grp">
      <input type="checkbox" ${n.soundEnabled ? 'checked' : ''}
        onchange="netPolicySet(${i},'soundEnabled',this.checked);renderNets()">
      Auto-Sound
    </label>
    <label class="net-policy-grp">
      every&nbsp;<input type="number" value="${n.soundIntervalSec}" min="60"
        ${n.soundEnabled ? '' : 'disabled'}
        oninput="netPolicySet(${i},'soundIntervalSec',+this.value)">&nbsp;s
    </label>
    <div class="net-policy-sep"></div>
    <label class="net-policy-grp"
      title="Assumed scan channels of the called station — determines total call duration (Tsc = C × 784 ms)">
      Call width&nbsp;<input type="number" value="${n.callingC}" min="1" max="10"
        oninput="netPolicySet(${i},'callingC',+this.value)">
    </label>
  </div>
  <div class="net-ch-summary${expanded ? ' open' : ''}"
    onclick="toggleNetChannelSection('${safeName}')">
    <span class="net-ch-caret">▶</span>
    <span>Channels</span>
    <span class="net-count-badge">${memberCnt} / ${channels.length}</span>
    ${memberCnt === 0 ? '<span style="color:var(--s-error);font-size:10px;margin-left:2px">— none assigned</span>' : ''}
  </div>
  <div class="net-ch-grid${expanded ? ' open' : ''}">${chips}</div>
</div>`;
  }).join('');
}

// Commit a net-rename edit on blur: validates non-empty + unique locally,
// pushes NET_RENAME to core, and only mutates local state (or reverts the
// input) once core confirms — mirrors chCommitId()'s channel-id-rename flow.
function netCommitName(i) {
  const n = nets[i];
  if (!n) return;
  const card = document.querySelectorAll('#netList .net-card')[i];
  const inp = card && card.querySelector('.net-name-inp');
  if (!inp) return;
  const newName = (inp.value || '').trim();
  const oldName = n.name;
  if (!newName || newName === oldName) { inp.value = oldName; return; }
  if (nets.some((o, j) => j !== i && o.name === newName)) {
    aleLogInfo('Net name "' + newName + '" already in use — keeping ' + oldName);
    inp.value = oldName;
    return;
  }
  if (bridgeConnected) {
    bridgeSend('NET_RENAME', { old_name: oldName, new_name: newName }, (r) => {
      if (!r || !r.ok) {
        inp.value = oldName;
        aleLogInfo('Net rename to "' + newName + '" rejected — keeping ' + oldName);
        return;
      }
      applyNetRename(i, oldName, newName);
    });
  } else {
    applyNetRename(i, oldName, newName);
  }
}

function applyNetRename(i, oldName, newName) {
  nets[i].name = newName;
  if (activeNet === oldName) activeNet = newName;
  renderNets();
  renderSoundPanel();
  renderNetPill();
}

function netPolicySet(i, field, val) {
  nets[i][field] = val;
  if (!bridgeConnected) return;
  // oninput fires on every keystroke; skip bridge sends for partial/invalid numerics
  // (nets[i] is already updated above so renderNets() won't overwrite the in-progress value)
  if ((field === 'dwellMs' || field === 'soundIntervalSec' || field === 'callingC')
      && (!Number.isFinite(val) || val <= 0)) return;
  const n = nets[i];
  bridgeSend('NET_UPDATE', {
    name:                 n.name,
    dwell_ms:             n.dwellMs,
    scanning_enabled:     n.scanEnabled,
    sounding_enabled:     n.soundEnabled,
    sounding_interval_sec:n.soundIntervalSec,
    calling_length_c:     n.callingC,
  });
  // If the active net's sounding policy just changed, keep the runtime toggle
  // and the sounding timer in sync immediately.
  if (n.name === activeNet) {
    const cb = document.getElementById('cfgAutoSound');
    if (cb) cb.checked = !!n.soundEnabled;
    applySoundAuto();
  }
}

function toggleNetChannel(i, chId, on) {
  const ids = nets[i].channelIds;
  if (on) { if (!ids.includes(chId)) ids.push(chId); }
  else    { nets[i].channelIds = ids.filter(id => id !== chId); }
  if (bridgeConnected) bridgeSend(on ? 'NET_ASSIGN' : 'NET_UNASSIGN', { net: nets[i].name, channel_id: chId });
}

function syncNetsFromBridge() {
  bridgeSend('NETS_LIST', {}, (r) => {
    if (!r.ok) return;
    nets = r.data.map(n => ({
      name:            n.name,
      channelIds:      n.channel_ids,
      dwellMs:         n.dwell_ms             ?? 200,
      scanEnabled:     n.scanning_enabled      ?? true,
      soundEnabled:    n.sounding_enabled      ?? false,
      soundIntervalSec:n.sounding_interval_sec ?? 300,
      callingC:        n.calling_length_c      ?? 10,
    }));
    renderNets();
    renderSoundPanel();   // sounding dropdown's net list mirrors the configured nets
    renderNetPill();      // header Network pill dropdown list mirrors the configured nets
  });
}

function addNet() {
  const name = 'NET' + (nets.length + 1);
  nets.push({ name, channelIds: [] });
  renderNets();
  if (bridgeConnected) bridgeSend('NET_ADD', { name }, () => syncNetsFromBridge());
}

function delNet(i) {
  const name = nets[i].name;
  nets.splice(i, 1);
  renderNets();
  if (bridgeConnected) bridgeSend('NET_DEL', { name }, () => syncNetsFromBridge());
}

function loadAleFile() {
  const path = document.getElementById('cfgStationFile')?.value.trim() || 'station.ale';
  if (!bridgeConnected) { aleLogInfo('Load: bridge not connected'); return; }
  bridgeSend('STATION_LOAD', { path }, (r) => {
    if (r.ok) {
      syncAllFromBridge();
      aleLogInfo('✓ Station file loaded ← ' + path);
    } else {
      aleLogInfo('✗ Failed to load station file: ' + path);
    }
  });
}

function saveAleFile() {
  const path = document.getElementById('cfgStationFile')?.value.trim() || 'station.ale';
  if (!bridgeConnected) { aleLogInfo('Save: bridge not connected'); return; }
  bridgeSend('STATION_SAVE', { path }, (r) => {
    if (r.ok) aleLogInfo('✓ Station file saved → ' + path);
    else      aleLogInfo('✗ Failed to save station file: ' + path);
  });
}
function exportConf() {
  const path = document.getElementById('cfgFile').value.trim() || 'ale.conf';
  if (!bridgeConnected) { aleLogInfo('Export: bridge not connected'); return; }
  if (!window.confirm('Konfiguration nach "' + path + '" exportieren?\nEine vorhandene Datei wird überschrieben.')) return;
  bridgeSend('SETTINGS_EXPORT', { path }, (r) => {
    if (r.ok) aleLogInfo('✓ Konfiguration exportiert → ' + path);
    else      aleLogInfo('Export fehlgeschlagen: ' + (r.error || '?'));
  });
}
function importConf() {
  const path = document.getElementById('cfgFile').value.trim() || 'ale.conf';
  if (!bridgeConnected) { aleLogInfo('Import: bridge not connected'); return; }
  bridgeSend('SETTINGS_IMPORT', { path }, (r) => {
    if (r.ok) {
      syncAllFromBridge();
      closeSettings();
      aleLogInfo('✓ Konfiguration geladen ← ' + path);
    } else {
      aleLogInfo('Import fehlgeschlagen: ' + (r.error || '?'));
    }
  });
}

// Apply visible settings to the UI (real impl sends CMDs to WebSocket)
// Auto-accept OFF ⇒ manual-accept gate ON (the SM pauses incoming calls in
// AWAIT_ACCEPT until Answer/Decline, or drops them after the decision window so
// the caller times out). Auto-accept ON ⇒ manual mode OFF (handshake auto-runs).
function applyManualAcceptToBridge() {
  if (!bridgeConnected) return;
  const auto = document.getElementById('cfgAutoAccept')?.checked ?? false;
  const secs = parseInt(document.getElementById('cfgAcceptTimeout')?.value, 10);
  const timeout_ms = (Number.isFinite(secs) && secs > 0 ? secs : 15) * 1000;
  bridgeSend('MANUAL_ACCEPT_MODE', { on: !auto, timeout_ms });
}

// Grey out the manual-accept window field when auto-accept is on (not used then).
function updateAutoAcceptUi() {
  const auto = document.getElementById('cfgAutoAccept')?.checked ?? false;
  const row  = document.getElementById('acceptDecisionRow');
  if (row) row.style.opacity = auto ? '0.45' : '1';
  const inp = document.getElementById('cfgAcceptTimeout');
  if (inp) inp.disabled = auto;
}

function applyLogLevelToBridge() {
  if (!bridgeConnected) return;
  const level = parseInt(document.getElementById('cfgLogLevel')?.value ?? '2', 10);
  bridgeSend('LOG_LEVEL_SET', { level });
}

// Diagnostics: relay the radio backend's CAT/rig traffic (rigctld/Hamlib
// commands, responses/errors, timing) into the status stream as "[CAT] ..."
// lines. Off by default — opt-in per P1-10.
function applyCatTraceToBridge() {
  if (!bridgeConnected) return;
  bridgeSend('CAT_TRACE', { on: document.getElementById('cfgCatTrace')?.checked ?? false });
}

function saveSettings() {
  applyManualAcceptToBridge();
  applyTimingToBridge();        // Timing + Calling Policy → core
  applyFecToBridge();           // FEC (Golay/votes/adaptive) + Debug RX → core
  applyLqaToBridge();           // Record-LQA toggle → core (A.5.4.1.1)
  applyRelinkToBridge();        // Auto-Relink toggle + threshold → core (A.5.4.5)
  applyLbtToBridge();           // LBT occupancy margin/enable/override → core (A.5.4.7)
  applyScanDetectToBridge();    // scan-stop squelch enable + margin → core (A.5.3.3)
  applyEnhFreqSelectToBridge(); // Enhanced Freq-Select → core (A.5.6.3.2)
  applyLogLevelToBridge();      // HamlibRadio debug logging → core
  applyCatTraceToBridge();      // CAT/rig traffic tracing → core
  applySoundAuto();             // interval may have changed → re-assert periodic mode
  updateSelfHeader();
  closeSettings();
}

// ── Keyboard shortcut ──
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') closeSettings();
  if (e.key === ',' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); openSettings(); }
});

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   REAL-TIME CLOCK  (UTC ⇄ Local — click to switch)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let clockUTC = true;
function toggleClockZone() { clockUTC = !clockUTC; updateClock(); }
function updateClock() {
  const d = new Date();
  const t = clockUTC
    ? d.toISOString().slice(11, 19)
    : [d.getHours(), d.getMinutes(), d.getSeconds()].map(n => String(n).padStart(2,'0')).join(':');
  document.getElementById('clockTime').textContent = t;
  document.getElementById('clockZone').textContent = clockUTC ? 'UTC' : 'LOCAL';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SCAN TOGGLE  (header)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
// Channel-hopping needs a list to hop over: scanning is only meaningful with
// >=2 channels. Below that the Scan button is disabled (see updateScanBtn).
function scanEnabled() { return channels.length >= 2; }

function updateScanBtn() {
  const b = document.getElementById('scanBtn');
  if (!b) return;
  // Keep "Stop" clickable while actually scanning; otherwise gate on >=2 channels.
  const ok = scanEnabled() || wfState === 'scanning';
  b.disabled = !ok;
  b.title = ok ? '' : 'Mindestens 2 Kanäle in den Einstellungen anlegen';
}

function updateSoundBtn() {
  const b = document.getElementById('soundBtn');
  if (!b) return;
  // The button is now a dropdown toggle (always enabled to open); the single-
  // channel action inside is gated by the bridge reply. Reflect any active
  // periodic-sounding net (= activeNet) in the label.
  const autoOn = !!activeNet && !!(document.getElementById('cfgAutoSound')?.checked);
  b.disabled = false;
  b.innerHTML = `${icon('volume2',14)} ` + (autoOn ? activeNet + ' ' : 'Sound ') + (soundPanelOpen() ? '▾' : '▸');
  b.title = autoOn
    ? 'Periodic sounding on ' + activeNet + ' every ' + soundingIntervalSec() + ' s'
    : 'Transmit a sounding (LQA probe). Single channel, or periodic over a net.';
}

function soundingIntervalSec() {
  const v = parseInt(document.getElementById('cfgSounding')?.value, 10);
  return Number.isFinite(v) && v > 0 ? v : 300;
}

function soundPanelOpen() {
  const p = document.getElementById('soundPanel');
  return !!p && p.classList.contains('open');
}

function toggleSoundPanel() {
  const p = document.getElementById('soundPanel');
  const open = p.classList.toggle('open');
  const autoOn = !!activeNet && !!(document.getElementById('cfgAutoSound')?.checked);
  document.getElementById('soundBtn').innerHTML =
    `${icon('volume2',14)} ` + (autoOn ? activeNet + ' ' : 'Sound ') + (open ? '▾' : '▸');
  if (open) renderSoundPanel();
}

// Build the per-net list from the configured nets + channels. Each row enables
// periodic multi-channel sounding on that net's channels (SOUND_AUTO), driven by
// the Sounding Interval setting. The active net (activeNet) is highlighted —
// picking a row routes through selectNet() so the header Network pill and this
// panel stay in sync (one active net drives scan / sound / call).
function renderSoundPanel() {
  const el = document.getElementById('soundNetList');
  if (!el) return;
  if (!nets.length) {
    el.innerHTML = '<div class="fhint" style="margin:0">No nets — configure in Settings ▸ Nets.</div>';
    return;
  }
  el.innerHTML = nets.map(n => {
    const chans = (n.channelIds || []).join(', ') || '—';
    const active = n.name === activeNet;
    return `<div class="sound-net-item${active ? ' active' : ''}" onclick="soundAutoNet('${escapeHtml(n.name)}')">
      <span class="sound-net-name">${escapeHtml(n.name)}</span>
      <span class="sound-net-chans">${escapeHtml(chans)}</span>
    </div>`;
  }).join('');
}

// Single-channel one-shot sounding on the current channel.
function soundSingle() {
  closeSoundPanel();
  if (bridgeConnected) {
    bridgeSend('SOUND', {}, (r) => {
      if (r && r.ok) aleLogInfo('TX SOUNDING from ' + primarySelfAddr());
      else           aleLogInfo('Sounding abgelehnt — nur im Idle/Scan');
    });
    return;
  }
  aleLogInfo('TX SOUNDING from ' + primarySelfAddr());
}

// Select a net AND enable periodic multi-channel sounding on it (SOUND_AUTO on).
// Routes through selectNet() so the Network pill reflects the same active net.
function soundAutoNet(name) {
  const cb = document.getElementById('cfgAutoSound'); if (cb) cb.checked = true;
  selectNet(name);            // sets activeNet + re-asserts SOUND_AUTO (now on for this net)
  closeSoundPanel();
}

// Stop periodic sounding but keep the selected net (the pill still shows it;
// scan/call remain scoped to it).
function soundAutoOff() {
  const cb = document.getElementById('cfgAutoSound'); if (cb) cb.checked = false;
  applySoundAuto();
  renderSoundPanel();
}

// The active network — single source of truth for the header Network pill and
// the sounding panel. Drives scan scope (SCAN_NET_SET → ALEController::
// set_active_scan_net, used by start_scanning / initiate_call / initiate_group_call)
// and the periodic-sounding target net (SOUND_AUTO).
let activeNet = null;

// Channel "number" for first-channel selection: the leading integer of the
// channel id ("C-3" → 3, HFLINK CHNUM "03D" → 3, "10A" → 10). Non-numeric ids
// sort last so they never win the "lowest-numbered" tiebreak spuriously.
function channelNum(id) {
  const m = String(id).match(/\d+/);
  return m ? parseInt(m[0], 10) : Number.MAX_SAFE_INTEGER;
}

// The "first channel" of a net = the lowest-numbered channel among the net's
// assigned (checked) channel ids that exist in channels[]. Returns the channel
// object or null. e.g. a net with channels 3,5,7,8,9 → channel 3.
function firstChannelOfNet(net) {
  const ids = (net && net.channelIds) || [];
  let best = null, bestN = Infinity;
  for (const id of ids) {
    const ch = channels.find(c => c.id === id);
    if (!ch) continue;
    const n = channelNum(id);
    if (n < bestN) { bestN = n; best = ch; }
  }
  return best;
}

// Select the active network from the header pill. Pushes SCAN_NET_SET, refreshes
// the pill + sounding panel, re-asserts SOUND_AUTO so periodic sounding follows
// the new net (turns off when the selection is cleared), and — when a net is
// selected — tunes the radio to that net's first (lowest-numbered) channel via
// VFO_SET_FREQ. The controller's set_frequency() emits channel_changed, which
// onBridgeEvent() mirrors onto the main-GUI frequency readout. Clearing the
// selection ('') leaves the frequency untouched (no auto-tune).
function selectNet(name) {
  activeNet = name || null;
  if (bridgeConnected) bridgeSend('SCAN_NET_SET', { net: name || '' });
  // Mirror the net's sounding_enabled policy into the Auto-Sound toggle so that
  // selecting a net that has sounding configured automatically activates it.
  const selNet = activeNet ? nets.find(n => n.name === activeNet) : null;
  const cb = document.getElementById('cfgAutoSound');
  if (cb) cb.checked = selNet ? !!selNet.soundEnabled : false;
  if (activeNet) {
    // Tune to the net's first (lowest-numbered) channel — frequency AND mode in
    // ONE atomic VFO_SET_CHANNEL. This drives the controller's set_vfo_channel()
    // → set_channel(), the SAME path scanning and the channel-step arrows use.
    // The former VFO_SET_FREQ + VFO_SET_MODE pair sent two separate CAT exchanges
    // and lost the SDR band-restore race (the mode was reverted to the band's
    // saved mode). We mirror locally too so the readout shows the net's first
    // channel immediately, even before a CAT link is up.
    const first = firstChannelOfNet(nets.find(n => n.name === activeNet));
    const hz = first ? parseInt(first.rx, 10) : 0;
    if (hz) {
      if (bridgeConnected) {
        bridgeSend('VFO_SET_CHANNEL', { hz, mode: first.mode || '' });
      }
      radioFreqHz = hz;
      radioMode   = first.mode || radioMode;
      radioChannel = -1;
      radioEntry  = '';
      radioEntryActive = false;
      updateRadioDisplay();
    }
  }
  renderNetPill();
  renderSoundPanel();
  updateSoundBtn();
  applySoundAuto();
}

// Push the periodic-sounding mode to the core. ON when a net is selected and the
// Automatic Sounding toggle is checked; OFF otherwise. The interval is owned by
// the net's own sounding_interval_sec policy (Nets panel "Auto-Sound every Xs"),
// already in core via NET_UPDATE — not sent here.
// The scan-net scope is owned by selectNet() (SCAN_NET_SET); applySoundAuto only
// pushes the SOUND_AUTO on/off + net target.
function applySoundAuto() {
  if (!bridgeConnected) { updateSoundBtn(); return; }
  const on = !!activeNet && !!(document.getElementById('cfgAutoSound')?.checked);
  bridgeSend('SOUND_AUTO', { on, net: on ? activeNet : '' });
  updateSoundBtn();
}

function closeSoundPanel() {
  const p = document.getElementById('soundPanel');
  if (p) p.classList.remove('open');
  updateSoundBtn();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   NETWORK PILL  (header — selects the active net for scan / sound / call)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
// Renders the pill's label (#netBtnLbl) and the dropdown net list (#netPillList).
// Each row shows the net name + assigned channel count; the active net is
// highlighted. Clicking a row calls selectNet(name); the trailing "No active
// net" button (in index.html) calls selectNet('').
function renderNetPill() {
  const lbl = document.getElementById('netBtnLbl');
  if (lbl) lbl.textContent = activeNet || 'No net';
  const caret = document.getElementById('netCaret');
  const panelOpen = !!document.getElementById('netPanel')?.classList.contains('open');
  if (caret) caret.textContent = panelOpen ? '▾' : '▸';
  const el = document.getElementById('netPillList');
  if (!el) return;
  if (!nets.length) {
    el.innerHTML = '<div class="fhint" style="margin:0">No nets — configure in Settings ▸ Nets.</div>';
    return;
  }
  el.innerHTML = nets.map(n => {
    const count = (n.channelIds || []).length;
    const active = n.name === activeNet;
    return `<div class="sound-net-item${active ? ' active' : ''}" onclick="selectNet('${escapeHtml(n.name)}')">
      <span class="sound-net-name">${escapeHtml(n.name)}</span>
      <span class="sound-net-chans">${count} ch</span>
    </div>`;
  }).join('');
}

function netPanelOpen() {
  const p = document.getElementById('netPanel');
  return !!p && p.classList.contains('open');
}

function toggleNetPanel() {
  const p = document.getElementById('netPanel');
  if (!p) return;
  const open = p.classList.toggle('open');
  const caret = document.getElementById('netCaret');
  if (caret) caret.textContent = open ? '▾' : '▸';
  if (open) renderNetPill();
}

function closeNetPanel() {
  const p = document.getElementById('netPanel');
  if (p) p.classList.remove('open');
  const caret = document.getElementById('netCaret');
  if (caret) caret.textContent = '▸';
}

// Pull the active net from core into the GUI (SCAN_NET_GET). Used on connect so
// the pill reflects whatever the controller has scoped. Pull-only — does not push.
function syncActiveNetFromBridge() {
  if (!bridgeConnected) return;
  bridgeSend('SCAN_NET_GET', {}, (r) => {
    if (!r || !r.ok) return;
    activeNet = (typeof r.net === 'string' && r.net) ? r.net : null;
    renderNetPill();
    renderSoundPanel();
    updateSoundBtn();
  });
}

// Push Timing + Calling Policy settings to the core via TIMING_SET.
function applyTimingToBridge() {
  if (!bridgeConnected) return;
  const num = (id) => { const v = parseInt(document.getElementById(id)?.value, 10); return Number.isFinite(v) && v > 0 ? v : null; };
  const args = {};
  const s  = num('cfgSounding');    if (s)  args.sounding_interval_sec = s;
  const li = num('cfgLinkIdle');    if (li) args.link_idle_timeout_sec = li;
  const mt = num('cfgMaxTune');     if (mt) args.max_tune_time_ms = mt;
  // ptt_lead/tail accept 0 (disabled), so bypass the v>0 guard
  const numZ = (id) => { const v = parseInt(document.getElementById(id)?.value, 10); return Number.isFinite(v) && v >= 0 ? v : null; };
  const pl = numZ('cfgPttLead');    if (pl !== null) args.ptt_lead_ms = pl;
  const pt = numZ('cfgPttTail');    if (pt !== null) args.ptt_tail_ms = pt;
  const sl = numZ('cfgSoundingLead'); if (sl !== null) args.sounding_warning_lead_sec = sl;
  const concSel = document.getElementById('cfgSoundingConclusion');
  if (concSel) args.sounding_use_twas = concSel.value === 'twas';
  bridgeSend('TIMING_SET', args);
}

// Push the "Record LQA" + "Request LQA" toggles to the core: lqa_enabled
// gates per-frame BER/SNR measurement into the LQA Memory (A.5.4.1.1);
// lqa_exchange_enabled gates the active bilateral CMD 'a' request exchange
// sent during calling/handshake (A.5.4.2). Fired on change and from
// saveSettings().
function applyLqaToBridge() {
  if (!bridgeConnected) return;
  const on    = document.getElementById('cfgRecLqa')?.checked ?? true;
  const reqOn = document.getElementById('cfgReqLqa')?.checked ?? true;
  bridgeSend('LQA_SET', { lqa_enabled: on, lqa_exchange_enabled: reqOn });
}

// Push Auto-Relink settings (enabled + threshold) to the core (A.5.4.5).
// Fired on change and from saveSettings().
function applyRelinkToBridge() {
  if (!bridgeConnected) return;
  const on  = document.getElementById('cfgAutoRelink')?.checked ?? false;
  const thr = parseFloat(document.getElementById('cfgRelinkThreshold')?.value ?? '5') || 5;
  bridgeSend('RELINK_SET', { relink_enabled: on, relink_threshold: thr });
}

// Sync Auto-Relink state from core into the GUI toggles.
function syncRelinkFromBridge() {
  bridgeSend('RELINK_GET', {}, (r) => {
    if (!r.ok) return;
    const elOn  = document.getElementById('cfgAutoRelink');
    const elThr = document.getElementById('cfgRelinkThreshold');
    if (elOn  && typeof r.relink_enabled   === 'boolean') elOn.checked  = r.relink_enabled;
    if (elThr && typeof r.relink_threshold === 'number')  elThr.value   = r.relink_threshold;
  });
}

// Push LBT occupancy settings to the core (A.5.4.7): busy margin (dB over the
// tracked noise floor — operator-settable for local noise conditions), master
// enable, and the A.5.4.7.3 emergency override. Fired on change and from
// saveSettings().
function applyLbtToBridge() {
  if (!bridgeConnected) return;
  const margin = parseFloat(document.getElementById('cfgLbtMargin')?.value ?? '6');
  bridgeSend('LBT_SET', {
    margin_db:         Number.isFinite(margin) ? margin : 6,
    occupancy_enabled: document.getElementById('cfgLbtOccupancy')?.checked ?? true,
    override:          document.getElementById('cfgLbtOverride')?.checked ?? false,
  });
}

// Sync LBT occupancy state from core into the GUI controls.
function syncLbtFromBridge() {
  bridgeSend('LBT_GET', {}, (r) => {
    if (!r.ok) return;
    const elM = document.getElementById('cfgLbtMargin');
    const elE = document.getElementById('cfgLbtOccupancy');
    const elO = document.getElementById('cfgLbtOverride');
    if (elM && typeof r.margin_db         === 'number')  elM.value   = r.margin_db;
    if (elE && typeof r.occupancy_enabled === 'boolean') elE.checked = r.occupancy_enabled;
    if (elO && typeof r.override          === 'boolean') elO.checked = r.override;
    const chip = document.getElementById('busyChip');
    if (chip && typeof r.busy === 'boolean') {
      chip.classList.toggle('busy', r.busy);
      chip.querySelector('span').textContent = r.busy ? 'FREQ BUSY' : 'FREQ CLEAR';
      chip.title = r.busy
        ? `Channel occupied — level ${Math.round(r.level_db)} dB, floor ${Math.round(r.floor_db)} dB (A.5.4.7.2)`
        : 'Channel clear (A.5.4.7.2)';
    }
  });
}

// Push §A.5.3.3 scan-stop squelch settings to the core: opt-in enable + dB margin
// above the auto-calibrated noise floor. Fired on change and from saveSettings().
function applyScanDetectToBridge() {
  if (!bridgeConnected) return;
  const margin = parseFloat(document.getElementById('cfgScanMargin')?.value ?? '3');
  bridgeSend('SCAN_DETECT_SET', {
    enabled:   document.getElementById('cfgScanSquelch')?.checked ?? false,
    margin_db: Number.isFinite(margin) ? margin : 3,
  });
}

// Sync scan-stop squelch state (incl. live noise floor) from the core into the GUI.
function syncScanDetectFromBridge() {
  bridgeSend('SCAN_DETECT_GET', {}, (r) => {
    if (!r.ok) return;
    const elE = document.getElementById('cfgScanSquelch');
    const elM = document.getElementById('cfgScanMargin');
    if (elE && typeof r.enabled   === 'boolean') elE.checked = r.enabled;
    if (elM && typeof r.margin_db === 'number')  elM.value   = Math.round(r.margin_db);
    const ro = document.getElementById('scanFloorReadout');
    if (ro && typeof r.floor_db === 'number') {
      const base = (typeof r.baseline_db === 'number' && r.baseline_db > 0)
        ? `, cal ${Math.round(r.baseline_db)}` : '';
      ro.textContent = `floor ${Math.round(r.floor_db)} dB${base}`;
    }
  });
}

// Operator "Calibrate": snapshot the current live noise floor as the baseline.
function calibrateScanDetector() {
  if (!bridgeConnected) return;
  bridgeSend('SCAN_DETECT_CALIBRATE', {}, (r) => {
    if (r && r.ok) syncScanDetectFromBridge();
  });
}

// Push FEC settings (Golay mode, min votes, adaptive FEC) and Debug RX to the core.
// Called from saveSettings(). GolayMode enum: Mode0_7=0, Mode1_6=1, Mode2_5=2, Mode3_4=3.
function applyFecToBridge() {
  if (!bridgeConnected) return;
  const golayMap = { '3_4': 3, '2_5': 2, '1_6': 1, '0_7': 0 };
  const golayVal = document.querySelector('input[name="golay"]:checked')?.value ?? '3_4';
  const votes    = parseInt(document.getElementById('cfgVotes')?.value, 10);
  bridgeSend('FEC_SET', {
    golay_mode:          golayMap[golayVal] ?? 3,
    min_unanimous_votes: Number.isFinite(votes) ? votes : 33,
    adaptive_fec:        document.getElementById('cfgAdaptive')?.checked ?? true
  });
  bridgeSend('DEBUG_RX', { on: document.getElementById('cfgDebugRx')?.checked ?? false });
}

// Sync FEC state from core into the GUI (called on connect via syncAllFromBridge).
function syncFecFromBridge() {
  bridgeSend('FEC_GET', {}, (r) => {
    if (!r.ok) return;
    const golayNames = { 3: '3_4', 2: '2_5', 1: '1_6', 0: '0_7' };
    const gName = golayNames[r.golay_mode] ?? '3_4';
    const gEl = document.querySelector(`input[name="golay"][value="${gName}"]`);
    if (gEl) gEl.checked = true;
    const votesEl = document.getElementById('cfgVotes');
    if (votesEl && typeof r.min_unanimous_votes === 'number') votesEl.value = r.min_unanimous_votes;
    const adaptEl = document.getElementById('cfgAdaptive');
    if (adaptEl && typeof r.adaptive_fec === 'boolean') adaptEl.checked = r.adaptive_fec;
  });
}

// Push Enhanced Frequency-Select state to the core (A.5.6.3.2 CMD 'f' bilateral).
function applyEnhFreqSelectToBridge() {
  if (!bridgeConnected) return;
  const on = document.getElementById('cfgEnhFreqSelect')?.checked ?? false;
  bridgeSend('FREQ_SELECT_SET', { enhanced_freq_select: on });
}

// Sync Enhanced Frequency-Select state from core into the GUI toggle.
function syncEnhFreqSelectFromBridge() {
  bridgeSend('FREQ_SELECT_GET', {}, (r) => {
    if (!r.ok) return;
    const el = document.getElementById('cfgEnhFreqSelect');
    if (el && typeof r.enhanced_freq_select === 'boolean')
      el.checked = r.enhanced_freq_select;
  });
}

// ── Station Location & Propagation ───────────────────────────────────────────

function syncLocationFromBridge() {
  bridgeSend('STATION_LOC_GET', {}, (r) => {
    if (!r.ok) return;
    const setNum  = (id, v) => { const el = document.getElementById(id); if (el && typeof v === 'number') el.value = v; };
    const setStr  = (id, v) => { const el = document.getElementById(id); if (el && typeof v === 'string') el.value = v; };
    const setBool = (id, v) => { const el = document.getElementById(id); if (el && typeof v === 'boolean') el.checked = v; };
    if (typeof r.position_source === 'number') {
      const src = r.position_source;
      const mainVal = (src === 3 || src === 4) ? 'gps' : String(src);
      const mainRadio = document.querySelector(`input[name="posSource"][value="${mainVal}"]`);
      if (mainRadio) {
        mainRadio.checked = true;
        if (src === 3 || src === 4) {
          const gpsRadio = document.querySelector(`input[name="gpsMethod"][value="${src}"]`);
          if (gpsRadio) gpsRadio.checked = true;
          onGpsMethodChange();
        }
        onPositionSourceChange();
      }
    }
    setNum('cfgLatDeg',      r.lat_deg);
    setNum('cfgLonDeg',      r.lon_deg);
    setStr('cfgGridLocator', r.grid_locator);
    setStr('cfgGpsdHost',    r.gpsd_host);
    setNum('cfgGpsdPort',    r.gpsd_port);
    setStr('cfgNmeaPort',    r.nmea_port);
    setNum('cfgNmeaBaud',    r.nmea_baud);
    setBool('cfgSfiEnabled', r.sfi_enabled);
    locGpsFix = !!r.has_fix;
    if (r.has_fix) { locGpsLat = r.fix_lat || 0; locGpsLon = r.fix_lon || 0; }
    locSfi = r.sfi || 0;
    updateLocStatus(locGpsFix, locGpsLat, locGpsLon, locSfi);
  });
}

function applyLocationToBridge() {
  if (!bridgeConnected) return;
  const src   = getPositionSource();
  const lat   = parseFloat(document.getElementById('cfgLatDeg')?.value || '0');
  const lon   = parseFloat(document.getElementById('cfgLonDeg')?.value || '0');
  const grid  = document.getElementById('cfgGridLocator')?.value?.trim() || '';
  const host  = document.getElementById('cfgGpsdHost')?.value?.trim() || '127.0.0.1';
  const gport = parseInt(document.getElementById('cfgGpsdPort')?.value || '2947', 10);
  const nport = document.getElementById('cfgNmeaPort')?.value?.trim() || '';
  const nbaud = parseInt(document.getElementById('cfgNmeaBaud')?.value || '4800', 10);
  const sfiOn = document.getElementById('cfgSfiEnabled')?.checked ?? false;
  bridgeSend('STATION_LOC_SET', {
    position_source: src,
    lat_deg:         lat,
    lon_deg:         lon,
    grid_locator:    grid,
    gpsd_host:       host,
    gpsd_port:       gport,
    nmea_port:       nport,
    nmea_baud:       nbaud,
    sfi_enabled:     sfiOn,
  });
}

function onPositionSourceChange() {
  const main = document.querySelector('input[name="posSource"]:checked')?.value || '0';
  const show = (id, vis) => { const el = document.getElementById(id); if (el) el.style.display = vis ? '' : 'none'; };
  show('locManualRow', main === '1');
  show('locGridRow',   main === '2');
  show('locGpsRow',    main === 'gps');
}

function onGpsMethodChange() {
  const method = document.querySelector('input[name="gpsMethod"]:checked')?.value || '3';
  const show = (id, vis) => { const el = document.getElementById(id); if (el) el.style.display = vis ? '' : 'none'; };
  show('locGpsdFields', method === '3');
  show('locNmeaFields', method === '4');
}

function getPositionSource() {
  const main = document.querySelector('input[name="posSource"]:checked')?.value || '0';
  if (main === 'gps') {
    return parseInt(document.querySelector('input[name="gpsMethod"]:checked')?.value || '3', 10);
  }
  return parseInt(main, 10);
}

function gridToLatLon(grid) {
  const u = grid.trim().toUpperCase();
  if (u.length < 4) return null;
  let lon = (u.charCodeAt(0) - 65) * 20 - 180;
  let lat = (u.charCodeAt(1) - 65) * 10 - 90;
  lon += (u.charCodeAt(2) - 48) * 2;
  lat += (u.charCodeAt(3) - 48);
  if (u.length >= 6) {
    lon += (u.charCodeAt(4) - 65) * (2 / 24) + (1 / 24);
    lat += (u.charCodeAt(5) - 65) * (1 / 24) + (1 / 48);
  } else {
    lon += 1.0;
    lat += 0.5;
  }
  return { lat: +lat.toFixed(4), lon: +lon.toFixed(4) };
}

function latLonToGrid(lat, lon) {
  const lo = ((lon + 180 + 360) % 360);
  const la = lat + 90;
  let g = '';
  g += String.fromCharCode(65 + Math.floor(lo / 20));
  g += String.fromCharCode(65 + Math.floor(la / 10));
  g += String(Math.floor((lo % 20) / 2));
  g += String(Math.floor(la % 10));
  g += String.fromCharCode(97 + Math.floor(((lo % 2) / 2) * 24));
  g += String.fromCharCode(97 + Math.floor((la % 1) * 24));
  return g;
}

function onGridInput() {
  const grid = document.getElementById('cfgGridLocator')?.value || '';
  if (grid.length < 4) return;
  const pos = gridToLatLon(grid);
  if (!pos) return;
  const latEl = document.getElementById('cfgLatDeg');
  const lonEl = document.getElementById('cfgLonDeg');
  if (latEl) latEl.value = pos.lat;
  if (lonEl) lonEl.value = pos.lon;
}

function onLatLonInput() {
  const lat = parseFloat(document.getElementById('cfgLatDeg')?.value);
  const lon = parseFloat(document.getElementById('cfgLonDeg')?.value);
  if (isNaN(lat) || isNaN(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
  const gridEl = document.getElementById('cfgGridLocator');
  if (gridEl) gridEl.value = latLonToGrid(lat, lon);
}

function updateLocStatus(hasFix, lat, lon, sfi) {
  const gpsEl = document.getElementById('locGpsStatus');
  const sfiEl = document.getElementById('locSfiStatus');
  if (gpsEl) {
    gpsEl.textContent = hasFix
      ? `Fix: ${lat.toFixed(4)}°, ${lon.toFixed(4)}°`
      : 'No fix';
  }
  if (sfiEl) {
    sfiEl.textContent = sfi > 0 ? `SFI: ${sfi.toFixed(0)} sfu` : 'Not available';
  }
}

// Pull Timing + Calling-Policy state from core into the GUI (TIMING_GET).
// Counterpart to applyTimingToBridge; used after a settings import so the GUI
// reflects the just-loaded config instead of clobbering it with stale DOM.
function syncTimingFromBridge() {
  bridgeSend('TIMING_GET', {}, (r) => {
    if (!r.ok) return;
    const setNum = (id, v) => { const el = document.getElementById(id); if (el && typeof v === 'number') el.value = v; };
    setNum('cfgSounding',       r.sounding_interval_sec);
    setNum('cfgSoundingLead',   r.sounding_warning_lead_sec);
    setNum('cfgLinkIdle',       r.link_idle_timeout_sec);
    setNum('cfgMaxTune',        r.max_tune_time_ms);
    setNum('cfgPttLead',        r.ptt_lead_ms);
    setNum('cfgPttTail',        r.ptt_tail_ms);
    const conc = document.getElementById('cfgSoundingConclusion');
    if (conc && typeof r.sounding_use_twas === 'boolean')
      conc.value = r.sounding_use_twas ? 'twas' : 'tis';
  });
}

// Pull manual-accept state from core into the GUI (MANUAL_ACCEPT_GET).
// GUI "auto-accept" checkbox = NOT manual_accept_mode (applyManualAcceptToBridge
// sends on: !auto). Timeout is stored in ms in the core, shown in seconds in the GUI.
function syncManualAcceptFromBridge() {
  bridgeSend('MANUAL_ACCEPT_GET', {}, (r) => {
    if (!r.ok) return;
    const onEl = document.getElementById('cfgAutoAccept');
    const toEl = document.getElementById('cfgAcceptTimeout');
    if (onEl && typeof r.on === 'boolean') onEl.checked = !r.on;
    if (toEl && typeof r.timeout_ms === 'number') toEl.value = Math.round(r.timeout_ms / 1000);
    updateAutoAcceptUi();
  });
}

// Pull periodic-sounding (auto-sound) state from core into the GUI (SOUND_AUTO_GET).
// Adopts the core's sounding net into activeNet when non-empty so the Network pill
// and the sounding panel reflect it; otherwise leaves activeNet untouched (the pill
// is the authoritative selector, restored separately via syncActiveNetFromBridge).
function syncSoundAutoFromBridge() {
  bridgeSend('SOUND_AUTO_GET', {}, (r) => {
    if (!r.ok) return;
    const el = document.getElementById('cfgAutoSound');
    if (el && typeof r.on === 'boolean') el.checked = r.on;
    if (typeof r.net === 'string' && r.net) activeNet = r.net;
    renderNetPill();
    updateSoundBtn();
  });
}

function toggleScan() {
  // Stopping (currently scanning) is always allowed; only *starting* needs >=2.
  if (wfState !== 'scanning' && !scanEnabled()) {
    aleLogInfo('Scanning braucht ≥2 Kanäle');
    return;
  }
  if (bridgeConnected) { bridgeSend(wfState === 'scanning' ? 'AVAILABLE' : 'SCAN', {}); return; }
  // setStatus() reflects the button label/state; just flip scanning ⇄ idle.
  if (wfState === 'scanning') goIdle(); else goScanning();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   MANUAL RADIO CONTROL  (VFO · channel step · PTT)
   Front-end mock — wire to pal::IRadio (set_channel / set_ptt) later.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const radioChannels = [
  { hz: 3596000,  mode:'USB' },
  { hz: 5371500,  mode:'USB' },
  { hz: 7102000,  mode:'USB' },
  { hz: 10145500, mode:'USB' },
  { hz: 14109000, mode:'USB' },   // boots here → matches the "14.109" stat
  { hz: 18106000, mode:'USB' },
];
let radioChannel = 4;             // index into radioChannels (-1 = manual/off-grid)
let radioFreqHz  = radioChannels[radioChannel].hz;
let radioMode    = radioChannels[radioChannel].mode;
let radioStep    = 1000;          // Hz per UP/DOWN nudge
let radioEntry       = '';     // keypad direct-entry buffer (digits)
let radioEntryActive = false;  // true when entry mode is on (even if buffer is empty after CLR)
let pttOn        = false;
let radioPower          = 100;   // commanded/actual TX power %, kept in sync via syncVfoFromBridge()
let radioPowerSupported = true;  // false once the bridge reports the connected rig lacks RFPOWER

function fmtRadioFreq(hz) {
  if (hz >= 1e9) {
    const ghz = Math.floor(hz / 1e9);
    const mhz = Math.floor((hz % 1e9) / 1e6);
    const khz = Math.floor((hz % 1e6) / 1e3);
    return ghz + '.' + String(mhz).padStart(3,'0') + '.' + String(khz).padStart(3,'0');
  }
  const mhz = Math.floor(hz / 1e6);
  const khz = Math.floor((hz % 1e6) / 1e3);
  return mhz + '.' + String(khz).padStart(3,'0') + '.' + String(hz % 1000).padStart(3,'0');
}
function freqUnit(hz) { return hz >= 1e9 ? 'GHz' : 'MHz'; }
// 6 decimals = 1 Hz resolution. 3 decimals (kHz) would round 14.000500 MHz
// to 14.001 — rounding the actual frequency is not permissible.
function fmtStatFreq(hz) { return (hz / 1e6).toFixed(6); }

function updateRadioDisplay() {
  const fEl = document.getElementById('radioFreq');
  const uEl = document.getElementById('radioUnit');
  const mEl = document.getElementById('radioModeChip');
  if (fEl) {
    if (radioEntry || radioEntryActive) { fEl.textContent = radioEntry;             fEl.classList.add('entry'); }
    else                                { fEl.textContent = fmtRadioFreq(radioFreqHz); fEl.classList.remove('entry'); }
  }
  if (uEl) uEl.textContent = (radioEntry || radioEntryActive) ? 'MHz / kHz ?' : freqUnit(radioFreqHz);
  if (mEl) mEl.textContent = radioMode;
  // mirror onto the header readout + linked-panel label
  const fv = document.getElementById('freqVal');     if (fv) fv.textContent = fmtRadioFreq(radioFreqHz);
  const fu = document.getElementById('freqUnit');    if (fu) fu.textContent = freqUnit(radioFreqHz);
  const fs = document.getElementById('freqSub');
  if (fs) {
    const ch = chFromFreq(radioFreqHz);
    fs.textContent = (ch ? ch.id.replace('C-', 'CH ') : 'VFO') + ' · ' + radioMode;
  }
  const cf = document.getElementById('callFreqLbl');  if (cf) cf.textContent = fmtRadioFreq(radioFreqHz) + ' ' + freqUnit(radioFreqHz) + ' · ' + radioMode;
  document.querySelectorAll('.rk-mode').forEach(b => b.classList.toggle('active', b.dataset.mode === radioMode));
  document.querySelectorAll('.rk-step').forEach(b => b.classList.toggle('active', +b.dataset.step === radioStep));
  document.querySelectorAll('.rk-pwr').forEach(b => b.classList.toggle('active', +b.dataset.pct === radioPower));
  const pv = document.getElementById('radioPowerVal'); if (pv) pv.textContent = radioPower + '%';
  const prow = document.getElementById('radioPowerRow');
  if (prow) {
    // Locked (no live CAT link) hides the reason behind the generic VFO lock;
    // connected-but-unsupported gets its own explicit tooltip so the operator
    // knows this rig specifically can't take power commands (RF safety).
    const unsupported = bridgeConnected && !radioPowerSupported;
    prow.classList.toggle('unsupported', unsupported);
    prow.title = unsupported ? 'Not supported by this rig' : 'RF transmit power';
    prow.querySelectorAll('button').forEach(b => b.disabled = radioCtrlLocked() || unsupported);
  }
}

function toggleRadioPanel() {
  const open = document.getElementById('radioPanel').classList.toggle('open');
  document.getElementById('radioToggle').innerHTML = open ? `${icon('radio',14)} Radio ▾` : `${icon('radio',14)} Radio ▸`;
  if (open) updateRadioDisplay();
}
// dismiss the VFO / Sounding / Network panels on an outside click
document.addEventListener('click', e => {
  const wrap = document.getElementById('radioWrap');
  const panel = document.getElementById('radioPanel');
  if (panel && panel.classList.contains('open') && wrap && !wrap.contains(e.target)) {
    panel.classList.remove('open');
    document.getElementById('radioToggle').innerHTML = `${icon('radio',14)} Radio ▸`;
  }
  const swrap = document.getElementById('soundWrap');
  const spanel = document.getElementById('soundPanel');
  if (spanel && spanel.classList.contains('open') && swrap && !swrap.contains(e.target)) {
    spanel.classList.remove('open');
    updateSoundBtn();
  }
  const nwrap = document.getElementById('netWrap');
  const npanel = document.getElementById('netPanel');
  if (npanel && npanel.classList.contains('open') && nwrap && !nwrap.contains(e.target)) {
    npanel.classList.remove('open');
    const caret = document.getElementById('netCaret');
    if (caret) caret.textContent = '▸';
  }
});

// Pull real freq/mode/tune-step/PTT from the bridge (ALEController::
// get_current_channel/frequency/mode/get_tune_step/get_ptt_state, all real
// IRadio passthrough — see Core/include/App/ale_controller.h) and reflect
// them onto the same display fields as the manual VFO controls.
function syncVfoFromBridge() {
  bridgeSend('VFO_GET', {}, (r) => {
    if (!r.ok) return;
    radioFreqHz = r.freq_hz;
    radioMode   = r.mode;
    radioStep   = r.tune_step_hz;
    pttOn       = r.ptt;
    radioPower          = r.power_pct;
    radioPowerSupported = r.power_supported;
    applyPttUi();
    updateRadioDisplay();
  });
}

// Radio-control commands are locked while bridged without a live CAT link
// (the buttons are also disabled; this guards programmatic/stray calls).
function radioCtrlLocked() { return bridgeConnected && !rigConnected; }

function stepChannel(dir) {
  if (radioCtrlLocked()) return;
  if (bridgeConnected) { bridgeSend('VFO_STEP', { direction: dir }, () => syncVfoFromBridge()); return; }
  if (!radioChannels.length) return;
  radioChannel = (radioChannel + dir + radioChannels.length) % radioChannels.length;
  radioFreqHz = radioChannels[radioChannel].hz;
  radioMode   = radioChannels[radioChannel].mode;
  radioEntry  = '';
  radioEntryActive = false;
  updateRadioDisplay();
}

// Operator manual PTT override — sends SET_PTT to the bridge so the
// controller asserts/releases PTT independently of the ALE state machine.
// Button is press-and-hold: onmousedown/ontouchstart → on=true,
// onmouseup/ontouchend/onmouseleave → on=false.
// Special case: if the radio is already in TX (pttOn=true — SM-driven or manual),
// pressing the button aborts the transmission immediately (SM → IDLE, PTT → RX).
// The button state resets via the ptt_changed push event from the backend.
function setPtt(on) {
  if (radioCtrlLocked()) return;
  // In ALE mode, pressing PTT while already on TX aborts the transmission.
  // In voice passthrough, PTT is press-and-hold talk — no abort path.
  if (!voicePassthrough && on && pttOn) { abortTx(); return; }
  if (bridgeConnected) bridgeSend('SET_PTT', { on });
  pttOn = on;
  if (voicePassthrough) {
    // Half-duplex voice: PTT on → mic up + mute speaker; PTT off → reverse.
    Voice.pttMuted = on;
    if (on) voiceMicStart(); else voiceMicStop();
  }
  applyPttUi();
}

// Stop any ongoing transmission immediately: SM → IDLE, modulator flushed, PTT → RX.
function abortTx() {
  if (bridgeConnected) bridgeSend('EMERGENCY_STOP', {});
}

// keypad direct entry — digits accumulate, MHz/kHz (or ENT) commit
function radioKey(d)  {
  radioEntryActive = true;
  if (radioEntry.length < 9) { radioEntry += d; updateRadioDisplay(); }
}
function radioDel() {
  if (!radioEntry && !radioEntryActive) radioEntry = String(radioFreqHz); // seed from live Hz
  radioEntryActive = true;
  radioEntry = radioEntry.slice(0, -1);
  updateRadioDisplay();
}
function radioClear() {
  if (radioEntryActive && radioEntry === '') {
    radioEntryActive = false;   // second CLR cancels entry mode (live freq resumes)
  } else {
    radioEntry = '';
    radioEntryActive = true;    // blank entry field — operator types fresh frequency
  }
  updateRadioDisplay();
}
function radioCommit(unitHz) {
  if (radioCtrlLocked()) return;
  if (!radioEntry) { radioEntryActive = false; updateRadioDisplay(); return; }
  const v = parseInt(radioEntry, 10);
  radioEntry = '';
  radioEntryActive = false;
  if (isNaN(v)) { updateRadioDisplay(); return; }
  const hz = Math.min(v * unitHz, 150000000000);
  if (bridgeConnected) { bridgeSend('VFO_SET_FREQ', { hz }, () => syncVfoFromBridge()); return; }
  radioFreqHz = hz; radioChannel = -1;
  updateRadioDisplay();
}
function radioEnter()    { radioCommit(1000); }          // bare ENT commits as kHz
function radioSetMode(m) {
  if (radioCtrlLocked()) return;
  if (bridgeConnected) { bridgeSend('VFO_SET_MODE', { mode: m }, () => syncVfoFromBridge()); return; }
  radioMode = m; updateRadioDisplay();
}
function radioSetPower(pct) {
  if (radioCtrlLocked() || (bridgeConnected && !radioPowerSupported)) return;
  pct = Math.max(0, Math.min(100, pct));
  if (bridgeConnected) { bridgeSend('VFO_SET_POWER', { pct }, () => syncVfoFromBridge()); return; }
  radioPower = pct; updateRadioDisplay();
}
function radioNudgePower(delta) {
  radioSetPower(radioPower + delta);
}
function radioSetStep(hz){
  if (radioCtrlLocked()) return;
  if (bridgeConnected) bridgeSend('VFO_SET_TUNE_STEP', { hz });
  radioStep = hz; updateRadioDisplay();
}
function radioNudge(dir) {
  if (radioCtrlLocked()) return;
  radioEntry = '';
  radioEntryActive = false;
  if (bridgeConnected) { bridgeSend('VFO_NUDGE', { direction: dir }, () => syncVfoFromBridge()); return; }
  radioFreqHz = Math.max(0, Math.min(radioFreqHz + dir * radioStep, 150000000000));
  radioChannel = -1;
  updateRadioDisplay();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   MESSAGES  (AMD orderwire — receive, send, delete)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let messages = [];
// Linked peer address (set on link_established, cleared on link_terminated /
// goScanning, recovered from the STATUS poll). Drives AMD target selection.
let linkedPeer = '';
function renderMessages() {
  const el = document.getElementById('msgList');
  if (!messages.length) { el.innerHTML = '<div class="msg-empty">No messages</div>'; return; }
  el.innerHTML = '<div class="msg-list">' + messages.map((m, i) => {
    // Direction = sender → receiver.  own: self → peer; incoming: peer → self.
    const from = m.own ? (m.self || '') : (m.peer || '');
    const to   = m.own ? (m.peer || '') : (m.self || '');
    return `
    <div class="msg-item${m.own?' msg-own':''}">
      <button class="msg-del" title="Delete" onclick="deleteMessage(${i})">✕</button>
      <div class="msg-hdr">
        <span class="msg-from">${escapeHtml(from)} → ${escapeHtml(to)}</span>
      </div>
      <div class="msg-text">${escapeHtml(m.text)}</div>
      <span class="msg-time">${m.time}</span>
    </div>`;
  }).join('') + '</div>';
}
function deleteMessage(i) { messages.splice(i, 1); renderMessages(); }
function clearMessages()  { messages = []; renderMessages(); }
// Live AMD char counter (max 90 Expanded-64 chars, A.5.7.2.3). maxlength="90"
// caps entry; this mirrors the field so the operator sees the budget.
function updateMsgCount() {
  const inp = document.getElementById('msgInput');
  const el  = document.getElementById('msgCount');
  if (!inp || !el) return;
  const n = (inp.value || '').length;
  el.textContent = n + '/90';
  el.classList.toggle('msg-count-full', n >= 90);
}
// AMD orderwire send: if a link is active, the message rides the linked-orderwire
// frame (TO[peer] + CMD AMD + message + TIS) to the connected peer; otherwise it is
// queued and a call is placed to the selected contact, carrying the AMD in the
// handshake's ACK frame (MIL-STD-188-141B A.5.7.2.2). Target = active linked
// peer if any, else the selected contact.
function sendAmd() {
  const inp = document.getElementById('msgInput');
  let txt = (inp.value || '').toUpperCase().trim();
  if (!txt) return;
  if (txt.length > 90) {                // Expanded-64 max 90 chars (A.5.7.2.3)
    txt = txt.slice(0, 90);
    aleLogInfo('AMD: trimmed to 90 chars');
  }
  const to = linkedPeer || (selectedContact ? selectedContact.cs : '');
  if (!to) {
    aleLogInfo('AMD: no target — select a contact or establish a link first');
    return;
  }
  const self = primarySelfAddr();
  messages.unshift({ self, peer: to, time: nowZulu(), text: txt, own: true });
  inp.value = '';
  updateMsgCount();
  renderMessages();
  if (bridgeConnected) { bridgeSend('AMD', { to, text: txt }); return; }
  aleLogInfo('AMD demo: TO:' + to.slice(0,3) + ' DATA:AMD TIS:' + self.slice(0, 3));
}
function nowZulu() { return new Date().toISOString().slice(11,16) + 'Z'; }

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SELF ADDRESS TABLE  (SelfAddr* — A.4.3.4)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let selfAddrs = [ { addr:'SAM', status:'enabled', chans:'ALL' } ];
function primarySelfAddr() {
  const e = selfAddrs.find(a => a.status === 'enabled' && a.addr);
  return e ? e.addr : (selfAddrs.find(a => a.addr)?.addr || '—');
}
function updateSelfHeader() {
  document.getElementById('selfCs').textContent = 'SELF: ' + primarySelfAddr();
}
function renderSelfAddrs() {
  const tb = document.getElementById('selfAddrBody');
  if (!tb) return;
  tb.innerHTML = selfAddrs.map((a, i) => `
    <tr>
      <td><input class="ch-inp" value="${a.addr}" maxlength="15" style="text-transform:uppercase"
            oninput="selfAddrs[${i}].addr=this.value.toUpperCase();updateSelfHeader()" onblur="selfAddrCommit(${i})"></td>
      <td>
        <select class="ch-inp" style="width:96px" onchange="selfAddrs[${i}].status=this.value;updateSelfHeader();selfAddrCommit(${i})">
          <option value="enabled"${a.status==='enabled'?' selected':''}>enabled</option>
          <option value="disabled"${a.status==='disabled'?' selected':''}>disabled</option>
        </select>
      </td>
      <td><input class="ch-inp" value="${a.chans}" oninput="selfAddrs[${i}].chans=this.value" onblur="selfAddrCommit(${i})"></td>
      <td><button class="ch-del" onclick="delSelfAddr(${i})">✕</button></td>
    </tr>`).join('');
}

// Commits one row once it has a usable address (fires on blur/select-change,
// not per keystroke — same reasoning as chCommit()). SelfAddressStore keys
// by address, so editing an existing addr creates a new entry rather than
// renaming; the old one is left for the operator to delete explicitly.
function selfAddrCommit(i) {
  if (!bridgeConnected) return;
  const a = selfAddrs[i];
  if (!a.addr) return;
  bridgeSend('SELF_ADDR_ADD', { addr: a.addr, status: a.status, valid_channels: a.chans },
    () => syncSelfAddrsFromBridge());
}

function syncSelfAddrsFromBridge() {
  bridgeSend('SELF_ADDR_LIST', {}, (r) => {
    if (!r.ok) return;
    selfAddrs = r.data.map(a => ({
      addr: a.addr, status: a.status,
      chans: a.valid_channels === 'ALL' ? 'ALL' : (a.valid_channels || []).join(','),
    }));
    renderSelfAddrs();
    updateSelfHeader();
    renderChannels();
    updateSetupBanner();
    if (!wizardTriggered) { wizardTriggered = true; maybeShowWizard(); }
  });
}

function addSelfAddr() { selfAddrs.push({ addr:'', status:'enabled', chans:'ALL' }); renderSelfAddrs(); renderChannels(); }
function delSelfAddr(i) {
  const addr = selfAddrs[i].addr;
  selfAddrs.splice(i, 1);
  renderSelfAddrs();
  updateSelfHeader();
  renderChannels();
  if (bridgeConnected && addr) bridgeSend('SELF_ADDR_DEL', { addr }, () => syncSelfAddrsFromBridge());
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   FIRST-RUN WIZARD + SETUP BANNER  (mobile/index.html #setupWizard)
   Fires once per session when the backend has no self addresses.
   Four steps: Callsign → Audio → Radio (optional, skippable) → Channels.
   Banner persists on the CALL tab until callsign + channels are both set.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let wizardTriggered = false;
let wizardStep = 0;

function updateSetupBanner() {
  const hasCallsign = selfAddrs.some(a => a.addr && a.status === 'enabled');
  const hasChannels = channels.length > 0;
  const card = document.getElementById('quickSetupCard');
  if (!card) return;
  const sub = document.getElementById('qsSubtitle');
  if (hasCallsign && hasChannels) {
    if (sub) { sub.textContent = primarySelfAddr() + ' · ' + channels.length + ' ch'; sub.className = 'qs-subtitle ok'; }
  } else {
    const missing = [];
    if (!hasCallsign) missing.push('callsign');
    if (!hasChannels) missing.push('channels');
    if (sub) { sub.textContent = 'Missing: ' + missing.join(' + '); sub.className = 'qs-subtitle warn'; }
    // Auto-expand when setup is incomplete and the wizard is not already open
    const wizardHidden = document.getElementById('setupWizard')?.classList.contains('hidden') ?? true;
    if (wizardHidden && card.dataset.expanded !== 'true') {
      card.dataset.expanded = 'true';
      const icon = document.getElementById('qsToggleIcon');
      if (icon) icon.textContent = '▾';
      enumDevices();
      populateRigDropdown();
    }
  }
  // Pre-fill callsign field from current configured address if blank
  const qsCs = document.getElementById('qsCallsign');
  if (qsCs && !qsCs.value) {
    const p = primarySelfAddr();
    if (p && p !== '—') qsCs.value = p;
  }
}

function maybeShowWizard() {
  if (!selfAddrs.length) showWizard();
  else updateSetupBanner();
}

function showWizard() {
  const el = document.getElementById('setupWizard');
  if (!el) return;
  wizardStep = 0;
  el.classList.remove('hidden');
  renderWizardStep();
  enumDevices();         // populates #audioIn / #audioOut → mirrored to wizard selects
  populateRigDropdown(); // populates #rigModel → mirrored to wizard selects
}

function closeWizard() {
  const el = document.getElementById('setupWizard');
  if (el) el.classList.add('hidden');
  updateSetupBanner();
}

function renderWizardStep() {
  for (let i = 0; i < 4; i++) {
    const s = document.getElementById('wzStep' + i);
    if (s) s.classList.toggle('hidden', i !== wizardStep);
  }
  document.querySelectorAll('.wz-pip').forEach((p, i) => {
    p.classList.toggle('active', i === wizardStep);
    p.classList.toggle('done',   i < wizardStep);
  });
  if (wizardStep === 1) wzMirrorAudio();
  if (wizardStep === 2) wzMirrorRig();
}

function wzMirrorAudio() {
  ['In', 'Out'].forEach(dir => {
    const src = document.getElementById('audio' + dir);
    const dst = document.getElementById('wzAudio' + dir);
    if (src && dst) { dst.innerHTML = src.innerHTML; dst.value = src.value; }
  });
}

function wzMirrorRig() {
  const src = document.getElementById('rigModel');
  const dst = document.getElementById('wzRigModel');
  if (src && dst) { dst.innerHTML = src.innerHTML; dst.value = src.value; wzOnRigModelChange(); }
}

function wzOnRigModelChange() {
  const sel = document.getElementById('wzRigModel');
  const ptype = rigPortTypeById[sel?.value ?? ''] || '';
  const row = document.getElementById('wzRigNetFields');
  if (row) row.style.display = ptype === 'network' ? '' : 'none';
}

function wzSetStatus(step, msg, cls) {
  const el = document.getElementById('wzStep' + step + 'Status');
  if (!el) return;
  el.textContent = msg;
  el.className = 'wz-status' + (cls ? ' ' + cls : '');
}

function wzNext() {
  if (wizardStep === 0) {
    const cs = (document.getElementById('wzCallsign')?.value || '').trim().toUpperCase();
    if (!cs) { document.getElementById('wzCallsign')?.focus(); return; }
    if (!bridgeConnected) { wizardStep++; renderWizardStep(); return; }
    wzSetStatus(0, 'Saving…');
    bridgeSend('SELF_ADDR_ADD', { addr: cs, status: 'enabled', valid_channels: 'ALL' }, () => {
      syncSelfAddrsFromBridge();
      wizardStep++;
      renderWizardStep();
    });
    return;
  }

  if (wizardStep === 1) {
    const inName  = document.getElementById('wzAudioIn')?.value  || '';
    const outName = document.getElementById('wzAudioOut')?.value || '';
    const ainEl  = document.getElementById('audioIn');
    const aoutEl = document.getElementById('audioOut');
    if (ainEl  && inName)  ainEl.value  = inName;
    if (aoutEl && outName) aoutEl.value = outName;
    if (!bridgeConnected) { wizardStep++; renderWizardStep(); return; }
    wzSetStatus(1, 'Connecting audio…');
    bridgeSend('AUDIO_OPEN', { in: inName, out: outName }, (r) => {
      if (r.ok) {
        audioOpen = true; audioInSelected = inName; audioOutSelected = outName;
        const btn = document.getElementById('audioConnectBtn');
        if (btn) { btn.innerHTML = icon('square', 12) + ' Close Audio'; btn.classList.add('scan-on'); }
        wzSetStatus(1, '✓ Audio connected', 'ok');
        setTimeout(() => { wizardStep++; renderWizardStep(); }, 600);
      } else {
        wzSetStatus(1, '✗ ' + (r.error || 'Failed — check device selection or Skip'), 'err');
      }
    });
    return;
  }

  if (wizardStep === 2) {
    const model = document.getElementById('wzRigModel')?.value || '';
    const ptype = rigPortTypeById[model] || '';
    const host  = document.getElementById('wzRigHost')?.value || '127.0.0.1';
    const port  = document.getElementById('wzRigPort')?.value || '4532';
    // Sync wizard selection into the settings form so rigArgs() picks it up
    const rigModelEl = document.getElementById('rigModel');
    if (rigModelEl) { rigModelEl.value = model; updateRigFields(); }
    if (ptype === 'network') {
      const h = document.getElementById('rigHost'); if (h) h.value = host;
      const p = document.getElementById('rigPort'); if (p) p.value = port;
    }
    if (!model || !bridgeConnected) { wizardStep++; renderWizardStep(); return; }
    wzSetStatus(2, 'Connecting to radio…');
    bridgeSend('RIG_CONNECT', rigArgs(), (r) => {
      const ok = !!(r.ok && r.connected);
      if (ok) applyRigState(true);
      wzSetStatus(2, ok ? '✓ Radio connected' : '✗ ' + (r.error || r.status || 'Failed — you can Skip'), ok ? 'ok' : 'err');
      if (ok) setTimeout(() => { wizardStep++; renderWizardStep(); }, 600);
    });
    return;
  }

  if (wizardStep === 3) {
    const path = (document.getElementById('wzChannelPath')?.value || '').trim() || 'nets/hflink_usa.ale';
    if (!bridgeConnected) { closeWizard(); return; }
    wzSetStatus(3, 'Loading channels…');
    bridgeSend('STATION_LOAD', { path }, (r) => {
      if (r.ok) {
        syncAllFromBridge();
        wzSetStatus(3, '✓ Channels loaded', 'ok');
        setTimeout(closeWizard, 700);
      } else {
        wzSetStatus(3, '✗ ' + (r.error || 'File not found — check path or Skip'), 'err');
      }
    });
    return;
  }
}

function wzSkip() {
  wizardStep++;
  if (wizardStep >= 4) closeWizard();
  else renderWizardStep();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   QUICK SETUP CARD  (#quickSetupCard in mobile/index.html)
   Inline collapsible card at top of CALL tab for fast reconfiguration.
   Auto-expands when callsign or channels are missing.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function toggleQuickSetup() {
  const card = document.getElementById('quickSetupCard');
  if (!card) return;
  const expanding = card.dataset.expanded !== 'true';
  card.dataset.expanded = expanding ? 'true' : 'false';
  const icon = document.getElementById('qsToggleIcon');
  if (icon) icon.textContent = expanding ? '▾' : '▸';
  if (expanding) {
    enumDevices();
    populateRigDropdown();
  }
}

function qsMirrorAudio() {
  ['In', 'Out'].forEach(dir => {
    const src = document.getElementById('audio' + dir);
    const dst = document.getElementById('qsAudio' + dir);
    if (!src || !dst) return;
    if (src.options.length && src.options[0]?.textContent !== 'Loading…') {
      const prev = dst.value;
      dst.innerHTML = src.innerHTML;
      if (prev && [...dst.options].some(o => o.value === prev)) dst.value = prev;
    }
  });
}

function qsMirrorRig() {
  const src = document.getElementById('rigModel');
  const dst = document.getElementById('qsRigModel');
  if (!src || !dst || src.options.length <= 1) return;
  const prev = dst.value;
  dst.innerHTML = '<option value="">— None / Offline —</option>' + src.innerHTML;
  if (prev && [...dst.options].some(o => o.value === prev)) dst.value = prev;
}

function qsSetStatus(msg, cls) {
  const el = document.getElementById('qsStatus');
  if (!el) return;
  el.textContent = msg;
  el.className = 'qs-status' + (cls ? ' ' + cls : '');
}

function qsApply() {
  const cs = (document.getElementById('qsCallsign')?.value || '').trim().toUpperCase();
  const inName   = document.getElementById('qsAudioIn')?.value  || '';
  const outName  = document.getElementById('qsAudioOut')?.value || '';
  const rigModel = document.getElementById('qsRigModel')?.value || '';

  if (!cs) { document.getElementById('qsCallsign')?.focus(); qsSetStatus('Enter your callsign first', 'err'); return; }
  if (!bridgeConnected) { qsSetStatus('Not connected to openALE bridge', 'err'); return; }

  qsSetStatus('Saving callsign…');
  bridgeSend('SELF_ADDR_ADD', { addr: cs, status: 'enabled', valid_channels: 'ALL' }, () => {
    syncSelfAddrsFromBridge();

    if (!inName || !outName) { qsSetStatus('✓ Callsign saved', 'ok'); return; }
    qsSetStatus('Connecting audio…');
    const ainEl  = document.getElementById('audioIn');  if (ainEl)  ainEl.value  = inName;
    const aoutEl = document.getElementById('audioOut'); if (aoutEl) aoutEl.value = outName;

    bridgeSend('AUDIO_OPEN', { in: inName, out: outName }, (ar) => {
      if (ar.ok) { audioOpen = true; audioInSelected = inName; audioOutSelected = outName; }

      if (!rigModel) {
        qsSetStatus(ar.ok ? '✓ Setup applied' : '✓ Callsign saved (audio failed)', ar.ok ? 'ok' : 'err');
        return;
      }
      qsSetStatus('Connecting radio…');
      const rigModelEl = document.getElementById('rigModel');
      if (rigModelEl) { rigModelEl.value = rigModel; updateRigFields(); }

      bridgeSend('RIG_CONNECT', rigArgs(), (rr) => {
        const ok = !!(rr.ok && rr.connected);
        if (ok) applyRigState(true);
        qsSetStatus(ok ? '✓ Setup applied' : '✓ Applied (radio connection failed)', ok ? 'ok' : 'err');
      });
    });
  });
}

function qsLoadChannels() {
  const path = (document.getElementById('qsChannelPath')?.value || '').trim() || 'nets/hflink_usa.ale';
  if (!bridgeConnected) { qsSetStatus('Not connected to openALE bridge', 'err'); return; }
  qsSetStatus('Loading channels…');
  bridgeSend('STATION_LOAD', { path }, (r) => {
    if (r.ok) { syncAllFromBridge(); qsSetStatus('✓ Channels loaded', 'ok'); }
    else qsSetStatus('✗ ' + (r.error || 'File not found — check path'), 'err');
  });
}

// Settings nav Advanced collapse — mobile only (CSS hides toggle on desktop)
function toggleAdvNav() {
  const items = document.getElementById('advNavItems');
  const caret = document.getElementById('advNavCaret');
  if (!items) return;
  const open = items.classList.toggle('open');
  if (caret) caret.textContent = open ? '▾' : '▸';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   LQA MATRIX  (LqaMatrix / LqaEntry — A.4.3.4)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let lqaEntries = [];

// MIL-STD-188-141B Table A-XIII: average 2/3-vote count (0-30) → approximate BER.
// 31 = "no value available". BER code is lower=better (0 votes → 0.0 BER).
const BER_TABLE_A_XIII = [
  0.0,0.006993,0.01409,0.02129,0.02860,0.03602,0.04356,0.05124,0.05904,0.06699,
  0.07508,0.08333,0.09175,0.1003,0.1091,0.1181,0.1273,0.1368,0.1464,0.1564,
  0.1667,0.1773,0.1882,0.1995,0.2113,0.2236,0.2365,0.2500,0.2643,0.2795,0.3
];
function fmtBerCode(code) {
  if (code == null) return '—';
  const c = Math.round(code);
  if (c === 31 || c < 0 || c > 30) return '—';
  return BER_TABLE_A_XIII[c].toExponential(1);
}

// P1-12: formats the LQA database's raw last_activity_ms (32-bit-wrapped ms
// since epoch — ALEController::get_all_lqa_entries()/LQADatabase::
// get_current_time_ms()) as an absolute local time-of-day string. Reconstructs
// the full timestamp by anchoring to the browser's clock: safe as long as the
// entry is younger than ~49.7 days (2^32 ms) — LQA entries are pruned well
// before that (max_age_ms = 25 h, LQAConfig). Returns '—' when unknown (0).
function fmtLqaTimestamp(u32ms) {
  if (!u32ms) return '—';
  const MOD = 4294967296;  // 2^32
  const nowMs = Date.now();
  let diff = (nowMs % MOD) - u32ms;
  if (diff < 0) diff += MOD;
  return new Date(nowMs - diff).toLocaleTimeString([], { hour12: false });
}

// Bridge's LQA_LIST format (see ALEController::get_all_lqa_entries()):
//   freq_hz|station|snr_db|ber|sinad_db|score|age_ms|bilateral_sinad_db|
//   bilateral_ber|bilateral_mp|display_score|available|last_activity_ms
// Four-level model (MIL-STD-188-141B App. A): FROM (snr_db/ber/sinad_db) is the
// locally MEASURED raw value; bilateral_* is what the peer REPORTED via CMD LQA
// (A.5.4.2). Per spec: SINAD = dB, higher = better (31 = no measurement); BER
// FROM = non-unanimous 2/3-vote count 0–48 (A.5.4.1.1), lower = better;
// bilateral BER = 5-bit CMD code 0–30 (Table A-XIII), 31 = no value, lower =
// better; MP = ms (7 = not measured). display_score is the operator-facing
// ranking (0–30, higher = better, already incorporates bilateral fallback).
// available = sounding conclusion: 1=TIS (available), 0=TWAS (not), -1=unknown.
// The mapping keeps FROM and TO SEPARATE for the bilateral matrix table, and
// also derives collapsed best-available values for the compact heard list.
function syncLqaFromBridge() {
  // Reflect the core's Record-LQA + Request-LQA state into the toggles
  // (A.5.4.1.1 / A.5.4.2).
  bridgeSend('LQA_GET', {}, (r) => {
    if (r.ok && typeof r.lqa_enabled === 'boolean') {
      const el = document.getElementById('cfgRecLqa');
      if (el) el.checked = r.lqa_enabled;
    }
    if (r.ok && typeof r.lqa_exchange_enabled === 'boolean') {
      const el = document.getElementById('cfgReqLqa');
      if (el) el.checked = r.lqa_exchange_enabled;
    }
  });
  syncRelinkFromBridge();
  syncEnhFreqSelectFromBridge();
  bridgeSend('LQA_LIST', {}, (r) => {
    if (!r.ok) return;
    const prevKeys = new Set(lqaEntries.map(e => e.addr + '|' + e.ch));
    lqaEntries = r.data.map(e => {
      // FROM direction (locally measured). SINAD in dB [0,30]; 0 or >30 = none.
      const sinadFromVal = (typeof e.sinad_db === 'number' && e.sinad_db > 0
                            && e.sinad_db <= 30) ? e.sinad_db : null;
      // BER FROM = raw 0–48 vote count (A.5.4.1.1); NOT a BER rate — shown as a
      // plain number. 0 means clean reception (all words unanimous).
      const berFromVal = (typeof e.ber === 'number') ? e.ber : null;

      // TO direction (peer-reported via CMD LQA, A.5.4.2).
      const sinadToVal = (typeof e.bilateral_sinad_db === 'number'
                          && e.bilateral_sinad_db <= 30) ? e.bilateral_sinad_db : null;
      const berToCode = (typeof e.bilateral_ber === 'number') ? e.bilateral_ber : null; // 0–30, 31 = none
      const mpToVal = (typeof e.bilateral_mp === 'number' && e.bilateral_mp >= 0
                        && e.bilateral_mp <= 6) ? e.bilateral_mp : null; // 7 = none

      // Collapsed best-available (FROM preferred, TO fallback). Used by the
      // active-link quality panel (updateLinkQualityFromLqa) and the one-line
      // ALE-log "LQA record:" entry; the heard panel now renders the full
      // FROM/TO table directly from sinad_from/sinad_to above.
      const fromSinadStr = sinadFromVal != null ? String(Math.round(sinadFromVal)) : null;
      const bilatSinadStr = sinadToVal != null ? String(Math.round(sinadToVal)) : null;

      return {
        addr:    e.station || '(sounding)',
        ch:      fmtChFreqExact(e.freq_hz),
        freq_hz: e.freq_hz,
        score:   Math.round(e.display_score != null ? e.display_score : e.score),
        // separate FROM/TO for the matrix table:
        sinad_from: sinadFromVal,                 // dB number or null
        sinad_to:   sinadToVal,                    // dB number or null
        ber_from:   berFromVal,                    // 0–48 number or null
        ber_to_code: berToCode,                    // 0–30 code or null (31 = none)
        ber_to:     berToCode != null ? fmtBerCode(berToCode) : null,  // Table-A-XIII decoded
        mp_to:     mpToVal,                        // 0–6 ms number or null
        // collapsed: FROM preferred, TO fallback. Feeds the active-link quality
        // panel (updateLinkQualityFromLqa) and the ALE-log "LQA record:" line.
        // snr_db (votes-based proxy) is NOT used as a SINAD fallback — it is a
        // different metric (votes/48×31) and would violate A.5.4.1.2 (SINAD in dB).
        sinad:   fromSinadStr || bilatSinadStr || '—',
        sinad_dir: sinadFromVal != null ? 'FROM' : (sinadToVal != null ? 'TO' : null),
        sinad_db: sinadFromVal != null ? sinadFromVal
                : (sinadToVal != null) ? sinadToVal : 0,
        snr_db:  typeof e.snr_db === 'number' ? e.snr_db : 0,
        ber:     berFromVal != null ? berFromVal.toFixed(1)
                : (berToCode != null ? fmtBerCode(berToCode) : '—'),
        mp:      mpToVal != null ? mpToVal.toFixed(0) + ' ms' : '—',
        age_ms:  (typeof e.age_ms === 'number') ? e.age_ms : 0,
        ageMin:  Math.round(e.age_ms / 60000),
        ts_ms:   (typeof e.last_activity_ms === 'number') ? e.last_activity_ms : 0,
        available: (typeof e.available === 'number') ? e.available : -1,
      };
    });
    // Mirror the LQA DB into the heard list (respecting manual deletions) so the
    // list isn't blank on connect/refresh when entries already exist. Previously
    // only rows NEW since the last sync were shown, so any pre-existing DB row
    // (e.g. a station called in a prior session, same addr+freq) never appeared.
    // Genuinely new DB rows still log "LQA record:"; pre-existing ones seed silently.
    const dbKeys = new Set(lqaEntries.map(e => e.addr + '|' + e.freq_hz));
    lqaEntries.forEach(e => {
      const key = e.addr + '|' + e.freq_hz;
      if (heardDeleted.has(key)) return;
      const wasInDb = prevKeys.has(e.addr + '|' + e.ch);
      const inList  = heardStations.some(h => h.addr === e.addr && h.freq_hz === e.freq_hz);
      if (!wasInDb && !inList) onAleLogLqa(e);
      else                     upsertHeard(e);
    });
    // Drop heard rows no longer in the DB (after LQA_CLEAR or the 1h prune).
    if (heardStations.some(h => !dbKeys.has(h.addr + '|' + h.freq_hz))) {
      heardStations = heardStations.filter(h => dbKeys.has(h.addr + '|' + h.freq_hz));
    }
    renderHeard();
    renderLqa();
  });
}
// LQA changes from real radio activity (soundings/contacts), not GUI
// actions — periodic poll, same reasoning as the VFO poll above.
setInterval(() => { if (bridgeConnected) syncLqaFromBridge(); }, 5000);

function clearLqa() {
  if (!confirm('Clear all LQA data? This also overwrites the saved file.')) return;
  bridgeSend('LQA_CLEAR', {}, () => {
    heardDeleted = new Set();   // full DB clear: re-created entries may show again
    syncLqaFromBridge();        // empty DB → mirror step prunes heardStations too
  });
}

// ── Quality colour gradient ──────────────────────────────────────────────
// Continuous red(0) → amber(0.5) → green(1) HSL ramp. goodness is a 0..1
// "betterness" — each metric normalises so that 1 = best, 0 = worst, regardless
// of whether the raw metric is higher- or lower-is-better. Replaces the old
// 3-bucket lqaClass. Higher = better on the display, per MIL-STD operator rule.
function qColor(g) {
  const gg = Math.max(0, Math.min(1, g));
  const h = Math.round(gg * 130);          // 0=red, 65=amber, 130=green
  return `hsl(${h}, 75%, 58%)`;
}
// A <td> with the value gradient-coloured, or a dim "—" when goodness is null
// (no measurement / sentinel: SINAD 31, BER 31, MP 7, available -1).
function qCell(text, goodness) {
  if (goodness == null || text == null || text === '—')
    return `<td class="lqa-cell" style="color:var(--tx-dim)">—</td>`;
  return `<td class="lqa-cell" style="color:${qColor(goodness)};font-weight:600">${text}</td>`;
}
// P1-12: absolute "received at" cell — always shows fmtLqaTimestamp's text
// (no quality gradient; a timestamp isn't a goodness metric like qCell's cells).
function tsCell(ms) {
  return `<td class="lqa-cell" style="color:var(--tx-dim)">${fmtLqaTimestamp(ms)}</td>`;
}
// Sounding-conclusion availability badge (1=TIS available, 0=TWAS not, -1=unknown).
function availBadge(av) {
  const cls = av === 1 ? 'ha-yes' : av === 0 ? 'ha-no' : 'ha-unk';
  const txt = av === 1 ? 'AVAIL' : av === 0 ? 'N/A' : '—';
  const tip = av === 1 ? 'TIS — available for link'
            : av === 0 ? 'TWAS — not available' : 'no sounding heard';
  return `<td class="lqa-cell"><span class="heard-avail ${cls}" title="${tip}">${txt}</span></td>`;
}

function renderLqa() {
  const tb = document.getElementById('lqaBody');
  if (!tb) return;
  const sort = document.getElementById('cfgLqaSort')?.value || 'age';
  // Age limit (min) drives the age gradient — fresher = greener.
  const ageLimit = Math.max(1, Number(document.getElementById('cfgLqaAge')?.value) || 60);
  // Age sort uses fine-grained age_ms (smaller = heard more recently) so the
  // latest-heard station lands on top; ageMin (rounded minutes) is too coarse and
  // leaves same-minute ties unordered. Score-descending tie-break for stability.
  const rows = [...lqaEntries].sort((a, b) =>
    sort === 'age'  ? (a.age_ms ?? 0) - (b.age_ms ?? 0) || b.score - a.score :
    sort === 'addr' ? a.addr.localeCompare(b.addr) || b.score - a.score :
                      b.score - a.score);
  tb.innerHTML = rows.length ? rows.map(e => {
    // goodness per metric (1 = best). Sentinels → null → dim "—".
    const sinadFromG = (e.sinad_from != null) ? e.sinad_from / 30 : null;
    const sinadToG   = (e.sinad_to   != null) ? e.sinad_to   / 30 : null;
    // BER FROM = raw 0–48 vote count (lower better); 0 = clean.
    const berFromG   = (e.ber_from   != null) ? 1 - Math.min(1, e.ber_from / 48) : null;
    // BER TO gradient on the 5-bit code 0–30 (lower better); 31 = no value.
    const berToCode  = (e.ber_to_code != null && e.ber_to_code <= 30) ? e.ber_to_code : null;
    const berToG     = (berToCode != null) ? 1 - berToCode / 30 : null;
    const mpG        = (e.mp_to != null) ? 1 - Math.min(1, e.mp_to / 6) : null;
    const scoreG     = Math.min(1, Math.max(0, e.score / 30));
    const ageG       = 1 - Math.min(1, e.ageMin / ageLimit);

    return `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${escapeHtml(e.addr)}</td>` +
      `<td class="lqa-cell">${e.ch}</td>` +
      availBadge(e.available) +
      qCell(e.score, scoreG) +
      qCell(e.sinad_from != null ? `+${Math.round(e.sinad_from)}` : null, sinadFromG) +
      qCell(e.sinad_to   != null ? `+${Math.round(e.sinad_to)}`   : null, sinadToG) +
      qCell(e.ber_from   != null ? e.ber_from.toFixed(1)          : null, berFromG) +
      qCell(berToCode    != null ? e.ber_to                       : null, berToG) +
      qCell(e.mp_to      != null ? e.mp_to.toFixed(0) + 'ms'      : null, mpG) +
      qCell(e.ageMin >= 60 ? '>60m' : e.ageMin + 'm', ageG) +
      tsCell(e.ts_ms) +
      `</tr>`;
  }).join('') : '<tr><td colspan="11" class="msg-empty">No LQA data yet</td></tr>';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   HELPERS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function escapeHtml(s) {
  return String(s).replace(/[&<>"]/g, c => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;' }[c]));
}

// Esc also closes the contact editor
document.addEventListener('keydown', e => { if (e.key === 'Escape') closeContactEditor(); });

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   BOOT
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
renderContacts();
renderMessages();
renderSelfAddrs();
renderChannels();
renderNets();
renderLqa();
updateClock();
setInterval(updateClock, 1000);
updateRadioDisplay();  // VFO display + mode/step highlight
goIdle();              // boot idle (Scan off); real state arrives via syncAllFromBridge()/STATUS on connect
updateScanBtn();       // reflect channel count on the Scan button (>=2 channels required)
updateSelfHeader();
updateAutoAcceptUi();  // reflect auto-accept checkbox state on the decision-window field
renderSoundPanel();    // sounding dropdown's net list (populated when nets sync from the bridge)
updateSoundBtn();
renderNetPill();       // header Network pill (populated when nets sync from the bridge)
// Show overlay immediately; it is hidden as soon as the WebSocket handshake succeeds.
setBridgeOverlay(true);
// Small delay before first connect: the bridge serves CSS/JS sequentially on the same
// thread as WebSocket upgrades.  On a cold cache load, static resources are still
// in-flight when this code runs; 200 ms lets the browser finish requesting them so
// the WS upgrade is not competing with a pending HTTP response.
setTimeout(connectBridge, 200);
