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

function bridgeWsUrl() {
  return 'ws://localhost:' + window.location.port;
}

function setBridgeOverlay(show) {
  const el = document.getElementById('bridgeOverlay');
  el.classList.toggle('hidden', !show);
  if (show) {
    const port = window.location.port || '…';
    document.getElementById('bridgeOverlayCmd').textContent = 'ale_bridge --port ' + port;
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
  catch { return; }
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
    if (ev.data instanceof ArrayBuffer) { onSpectrumFrame(ev.data); return; }
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
  syncContactsFromBridge();
  syncSelfAddrsFromBridge();
  syncLqaFromBridge();
  syncVfoFromBridge();
  pollRigStatus();   // establish initial radio-control lock state
  applyManualAcceptToBridge();  // push the GUI's accept-mode default to the SM
  applyTimingToBridge();        // push Timing settings (Sounding Interval etc.) to the core
  applySoundAuto();             // re-assert periodic-sounding mode if active
}

// No dedicated push event exists for VFO/PTT/audio-level changes (they're
// either operator-driven from this same GUI, or — for PTT during a call —
// fast-changing enough that polling beats adding a new event type for now).
// Light poll only while connected.
setInterval(() => { if (bridgeConnected) { syncVfoFromBridge(); pollRigStatus(); pollSignalQuality(); } }, 2000);

// Header SINAD: best LQA entry on the current frequency (any station).
// sinad_db in the mapped entry already falls back to bilateral_sinad_db
// when no local FROM measurement exists (see syncLqaFromBridge mapping).
function updateHeaderSinadFromLqa() {
  const el = document.getElementById('snrVal');
  if (!el) return;
  const matches = lqaEntries.filter(e =>
    Math.abs((e.freq_hz || 0) - radioFreqHz) < 500);
  if (!matches.length) return;
  const best = matches.reduce((a, b) => a.score > b.score ? a : b);
  const val = best.sinad_db > 0 ? best.sinad_db : best.score;
  if (val > 0) el.textContent = '+' + Math.round(val);
}

// Poll SIGNAL_QUALITY for the active-link quality panel (bars + label).
// The header SINAD readout now comes from the LQA database instead.
function pollSignalQuality() {
  bridgeSend('SIGNAL_QUALITY', {}, (r) => {
    if (!r.ok) return;
    const sinad = Math.max(0, Math.min(30, Math.round(r.sinad_db)));
    const qtext = sinad >= 24 ? 'Excellent' : sinad >= 17 ? 'Good' : sinad >= 10 ? 'Fair' : 'Poor';
    const lbl = document.getElementById('qualityLbl');
    if (lbl) lbl.textContent = qtext + ' · +' + sinad + ' dB';
    const activeBars = sinad >= 30 ? 5 : Math.floor(sinad / 6);
    const bars = document.querySelectorAll('#qbars .qbar');
    bars.forEach((b, i) => b.classList.toggle('inactive', i >= activeBars));
  });
}

function updateLinkQualityFromLqa(peerAddr) {
  const best = [...lqaEntries]
    .filter(e => e.addr === peerAddr)
    .sort((a, b) => b.score - a.score)[0];
  if (!best) return;
  const qt = best.score >= 24 ? 'Excellent' : best.score >= 17 ? 'Good'
           : best.score >= 10 ? 'Fair' : 'Poor';
  const sinadPart = best.sinad !== '—' ? ' · +' + best.sinad + ' dB' : '';
  const lbl = document.getElementById('qualityLbl');
  if (lbl) lbl.textContent = qt + sinadPart;
  const bars = Math.min(5, Math.floor(best.score / 6));
  document.querySelectorAll('#qbars .qbar')
    .forEach((b, i) => b.classList.toggle('inactive', i >= bars));
}

function applyStatusReply(r) {
  if (!r.ok) return;
  applyBridgeState(r.state);
}

// Map the bridge's per-instance display state -> pill. The bridge reports
// "HANDSHAKE" for both the called station's HANDSHAKE state and the caller's
// response-exchange sub-phases (LISTENING/SENDING_ACK), so each side shows
// calling → handshake → linked from its own perspective. LINKED is driven by
// the link_established event instead (it carries the peer address).
function applyBridgeState(state) {
  if (state === 'IDLE') goIdle();
  else if (state === 'SCANNING') goScanning();
  else if (state === 'CALLING') setStatus('Calling…', 'calling');
  else if (state === 'HANDSHAKE') setStatus('Handshake…', 'handshake');
  else if (state === 'LINKED') setStatus('Linked', 'linked');
}

function onBridgeEvent(e) {
  switch (e.event) {
    case 'state': applyBridgeState(e.value); break;
    case 'status': aleLogInfo(e.msg); break;
    case 'call_received':
      isIncomingCall = true;
      stopTimer();
      setStatus('Incoming', 'incoming');
      document.getElementById('incCs').textContent   = e.caller;
      document.getElementById('incName').textContent = '';
      showInc(true);
      showCallPanel(false);
      break;
    case 'link_established':
      document.getElementById('callCs').textContent = e.peer;
      updateLinkQualityFromLqa(e.peer);
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
      stopTimer();
      goScanning();
      break;
    case 'amd_received':
      messages.unshift({ from: e.from, time: nowZulu(), text: e.text, own: false });
      renderMessages();
      break;
    case 'word_decoded':  onAleLogWord(e);   break;
    case 'frame_decoded': onAleLogFrame(e);  break;
  }
}

function onSpectrumFrame(buf) {
  latestSpectrum = new Float32Array(buf);
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
const BW_LO  = 0, BW_HI = 4000;       // ❶ displayed Hz range; must equal 0…Nyquist
const ALE_LO = 750, ALE_HI = 2500;    // ALE sub-band for the band-frame overlay only
const ALE_GUARD = 125;                // frame padding beyond the edge tones (Hz)
const AXIS_MAJOR = [0, 1000, 2000, 3000, 4000];  // labelled gridlines (Hz)
const AXIS_MINOR = [500, 1500, 2500, 3500];      // unlabelled gridlines (Hz)

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

  // ALE 8-FSK band frame inside the 0–4 kHz window, padded by ALE_GUARD on each
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

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   HEARD STATIONS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let heardStations = [];  // { addr, freq_hz, ts, score, sinad_db }

function upsertHeard(addr, freqHz, score, sinadDb) {
  const ts  = new Date().toTimeString().slice(0, 8);
  const idx = heardStations.findIndex(h => h.addr === addr && h.freq_hz === freqHz);
  const entry = { addr, freq_hz: freqHz, ts, score, sinad_db: sinadDb };
  if (idx >= 0) heardStations[idx] = entry;
  else          heardStations.unshift(entry);
  renderHeard();
}

function deleteHeard(addr, freqHz) {
  heardStations = heardStations.filter(h => !(h.addr === addr && h.freq_hz === freqHz));
  renderHeard();
}

function clearHeard() { heardStations = []; renderHeard(); }

function renderHeard() {
  const el = document.getElementById('heardList');
  if (!el) return;
  if (!heardStations.length) {
    el.innerHTML = '<div class="heard-empty">No stations heard yet</div>';
    return;
  }
  el.innerHTML = heardStations.map(h => {
    const freqMhz  = h.freq_hz ? (h.freq_hz / 1e6).toFixed(3) : '?';
    const lbl      = chLabelForFreq(h.freq_hz);
    const scoreCls = h.score >= 24 ? 'hs-good' : h.score >= 14 ? 'hs-ok' : 'hs-bad';
    const sinadTxt = h.sinad_db > 0 ? `+${Math.round(h.sinad_db)} dB` : '—';
    return `<div class="heard-row">` +
      `<span class="heard-cs">${escapeHtml(h.addr)}</span>` +
      `<span class="heard-freq">${escapeHtml(freqMhz)}</span>` +
      `<span class="heard-lbl">${lbl ? '['+escapeHtml(lbl)+']' : ''}</span>` +
      `<span class="heard-score ${scoreCls}">${h.score != null ? h.score : '—'}</span>` +
      `<span class="heard-sinad">${sinadTxt}</span>` +
      `<span class="heard-ts">${h.ts}</span>` +
      `<button class="heard-del" onclick="deleteHeard(${JSON.stringify(h.addr)},${h.freq_hz})" title="Remove">×</button>` +
      `</div>`;
  }).join('');
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

function onAleLogWord(e) {
  const fid = e.frame_id;
  if (!aleLogFrameCh.has(fid) && e.freq_hz)
    aleLogFrameCh.set(fid, e.freq_hz);
  const freqHz = aleLogFrameCh.get(fid) || e.freq_hz || 0;
  const chDisp = aleChLabel(freqHz);
  const ts  = new Date().toTimeString().slice(0, 8);
  const p   = (e.preamble || '').toLowerCase();
  const pill = p === 'to' ? 'pill-to' : p === 'tis' ? 'pill-tis' :
               p === 'twas' ? 'pill-twas' : p === 'thru' ? 'pill-thru' :
               p === 'rep' ? 'pill-rep' : 'pill-data';
  const fec = e.fec || 0;
  const berCls = fec === 0 ? 'ale-ber-ok' : fec <= 1 ? 'ale-ber-warn' : 'ale-ber-bad';
  aleLogAppend(
    `<div class="ale-entry">` +
    `<span class="ale-entry-ts">${ts}</span>` +
    `<span class="ale-entry-ch">[${escapeHtml(chDisp)}]</span>` +
    `<span class="ale-entry-mode">[ALE]</span>` +
    `<span class="ale-entry-word pill ${pill}">${escapeHtml(e.preamble)}</span>` +
    `<span class="ale-entry-addr">[${escapeHtml(e.addr)}]</span>` +
    `<span class="ale-entry-ber ${berCls}">BER: ${fec}</span>` +
    `</div>`);
  if (!wfMarkers.some(m => m.frameId === fid)) {
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

function onAleLogLqa(addr, freqHz, score, sinadDb) {
  upsertHeard(addr, freqHz, score, sinadDb);
  const ts      = new Date().toTimeString().slice(0, 8);
  const freqStr = freqHz ? ` ${(freqHz / 1e6).toFixed(3)} MHz` : '';
  const lbl     = freqHz ? chLabelForFreq(freqHz) : '';
  const lblStr  = lbl ? ` [${lbl}]` : '';
  const scoreStr = score != null ? ` score=${score}` : '';
  const sinadStr = sinadDb > 0   ? ` SINAD=+${Math.round(sinadDb)}dB` : '';
  aleLogAppend(
    `<div class="ale-entry ale-info">` +
    `<span class="ale-entry-ts">${ts}</span>` +
    `<span class="ale-entry-ch"></span>` +
    `<span class="ale-entry-mode">[INFO]</span>` +
    `<span class="ale-entry-addr">LQA record: ${escapeHtml(addr)}` +
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
let editingContactIdx = -1;

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
    const off = c.status === 'disabled';
    return `<div class="contact-item${sel}" style="${off?'opacity:.5':''}" onclick="pickContact(${idx})">
      <div class="contact-avatar">📻</div>
      <div class="contact-info">
        <div class="contact-cs">${c.cs}${off?' <span style="font-size:9px;color:var(--tx-dim)">OFF</span>':''}</div>
        <div class="contact-name">${escapeHtml(c.name||'')}</div>
      </div>
      <div class="contact-actions">
        ${c.fav ? '<div class="contact-star">★</div>' : ''}
        <button class="contact-edit" title="Edit" onclick="event.stopPropagation();openContactEditor(${idx})">✎</button>
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
    const favs = new Set(contacts.filter(c => c.fav).map(c => c.cs));  // "favorite" is GUI-only, not in Core
    contacts = r.data.map(c => ({
      cs: c.callsign, name: c.name, fav: favs.has(c.callsign), status: c.status,
      net: (c.net_members || [])[0] || '',
      chans: c.valid_channels === 'ALL' ? 'ALL' : (c.valid_channels || []).join(','),
    }));
    selectedContact = contacts.find(c => c.cs === prevSel) || contacts[0] || null;
    renderContacts();
  });
}

function openContactEditor(idx) {
  editingContactIdx = (typeof idx === 'number') ? idx : -1;
  const c = editingContactIdx >= 0 ? contacts[editingContactIdx]
                                   : { cs:'', name:'', fav:false, status:'enabled', net:'', chans:'ALL' };
  document.getElementById('ceCs').value     = c.cs;
  document.getElementById('ceName').value   = c.name || '';
  document.getElementById('ceFav').checked  = !!c.fav;
  document.getElementById('ceStatus').value = c.status || 'enabled';
  document.getElementById('ceNet').value    = c.net || '';
  document.getElementById('ceChans').value  = c.chans || 'ALL';
  document.getElementById('ceDelete').style.display = editingContactIdx >= 0 ? '' : 'none';
  document.getElementById('ceTitle').textContent    = editingContactIdx >= 0 ? 'Edit Contact' : 'Add Contact';
  document.getElementById('contactModal').classList.remove('hidden');
  document.getElementById('ceCs').focus();
}

function closeContactEditor() { document.getElementById('contactModal').classList.add('hidden'); }

function saveContact() {
  const cs = (document.getElementById('ceCs').value || '').toUpperCase().trim();
  if (!cs) { document.getElementById('ceCs').focus(); return; }
  const c = {
    cs,
    name:   document.getElementById('ceName').value.trim(),
    fav:    document.getElementById('ceFav').checked,
    status: document.getElementById('ceStatus').value,
    net:    document.getElementById('ceNet').value.trim(),
    chans:  document.getElementById('ceChans').value.trim() || 'ALL',
  };
  const prevCs = editingContactIdx >= 0 ? contacts[editingContactIdx].cs : null;
  if (editingContactIdx >= 0) contacts[editingContactIdx] = c;
  else { contacts.push(c); selectedContact = c; }
  closeContactEditor();
  renderContacts();
  if (bridgeConnected) {
    // callsign is ContactStore's key — a rename needs the old entry removed too.
    if (prevCs && prevCs !== cs) bridgeSend('CONTACT_DEL', { callsign: prevCs });
    bridgeSend('CONTACT_ADD', {
      callsign: c.cs, name: c.name, status: c.status,
      net_members: c.net, valid_channels: c.chans,
    }, () => syncContactsFromBridge());
  }
}

function deleteContact() {
  let removedCs = null;
  if (editingContactIdx >= 0) {
    removedCs = contacts[editingContactIdx].cs;
    if (contacts[editingContactIdx] === selectedContact) selectedContact = null;
    contacts.splice(editingContactIdx, 1);
  }
  closeContactEditor();
  renderContacts();
  if (bridgeConnected && removedCs) bridgeSend('CONTACT_DEL', { callsign: removedCs }, () => syncContactsFromBridge());
}

function toggleCallModePanel() {
  const p = document.getElementById('callModePanel');
  if (!p) return;
  const nowHidden = p.classList.toggle('hidden');
  const b = document.getElementById('callBtn');
  if (b) b.textContent = nowHidden ? '📞 Call ▸' : '📞 Call ▾';
}

function closeCallModePanel() {
  document.getElementById('callModePanel')?.classList.add('hidden');
  const b = document.getElementById('callBtn');
  if (b) b.textContent = '📞 Call ▸';
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
}

function closeSettings() {
  document.getElementById('settingsModal').classList.add('hidden');
}

function overlayClose(e) {
  if (e.target === document.getElementById('settingsModal')) closeSettings();
}

function showSec(sec) {
  document.querySelectorAll('.snav-item').forEach(el =>
    el.classList.toggle('active', el.dataset.sec === sec));
  document.querySelectorAll('.ssec').forEach(el =>
    el.classList.toggle('active', el.dataset.sec === sec));
}

// Rig backend field visibility
function updateRigFields() {
  const val = document.querySelector('input[name="rigbe"]:checked')?.value ?? 'netrigctl';
  document.getElementById('rigFieldsTcp').style.display    = val === 'netrigctl' ? '' : 'none';
  document.getElementById('rigFieldsSerial').style.display = val === 'serial'    ? '' : 'none';
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
      const strip = s => s.replace(/^(IN:|OUT:)\s*/, '');
      const mkOpt = s => { const n = strip(s); return `<option value="${escapeHtml(n)}">${escapeHtml(n)}</option>`; };
      document.getElementById('audioIn').innerHTML  = (r.inputs  || []).map(mkOpt).join('')  || '<option value="">— none —</option>';
      document.getElementById('audioOut').innerHTML = (r.outputs || []).map(mkOpt).join('') || '<option value="">— none —</option>';
      restoreAudioSelection();
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
      document.getElementById('audioIn').innerHTML  = '<option>— start ale_bridge to list devices —</option>';
      document.getElementById('audioOut').innerHTML = '<option>— start ale_bridge to list devices —</option>';
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
      if (btn) { btn.textContent = '🔌 Connect Audio'; btn.classList.remove('scan-on'); }
    });
    return;
  }
  const inName  = document.getElementById('audioIn').value;
  const outName = document.getElementById('audioOut').value;
  if (btn) btn.textContent = '⟳ Opening…';
  bridgeSend('AUDIO_OPEN', { in: inName, out: outName }, (r) => {
    audioOpen = !!r.ok;
    if (r.ok) { audioInSelected = inName; audioOutSelected = outName; }  // remember the working choice
    if (btn) {
      btn.textContent = r.ok ? '■ Close Audio' : '🔌 Connect Audio';
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

// TX volume label sync
document.getElementById('cfgTxVol')?.addEventListener('input', function() {
  document.getElementById('txVolLbl').textContent = this.value + ' %';
});

// Live CAT-link state (bridge attached a real pal::IRadio). Drives the Connect
// button label and the radio-control lock (see setRadioCtrlEnabled).
let rigConnected = false;

// Read the structured rig fields the bridge needs for create_radio().
function rigArgs() {
  return {
    backend: document.querySelector('input[name="rigbe"]:checked')?.value ?? 'netrigctl',
    host:    document.getElementById('rigHost').value,
    port:    document.getElementById('rigPort').value,
    serial:  document.getElementById('rigSerial').value,
    model:   document.getElementById('rigModel').value,
    baud:    parseInt(document.getElementById('rigBaud')?.value, 10) || 0,
    dtr:     document.getElementById('rigDtr')?.value  ?? 'on',
    rts:     document.getElementById('rigRts')?.value  ?? 'on',
    stab:    parseInt(document.getElementById('rigStab')?.value, 10) || 200,
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
    el.textContent = '✗ Not connected to ale_bridge — start it first';
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
    el.textContent = '✗ Not connected to ale_bridge — start it first';
    return;
  }
  const backend = document.querySelector('input[name="rigbe"]:checked')?.value ?? 'netrigctl';
  if (rigConnected || backend === 'none') {
    el.textContent = rigConnected ? '⟳ Disconnecting…' : '⟳ …';
    bridgeSend('RIG_DISCONNECT', {}, (r) => {
      applyRigState(false);
      el.classList.add('ok');
      el.textContent = backend === 'none' ? '○ offline (no radio)' : '○ disconnected';
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
    btn.textContent = connected ? '■ Disconnect' : '🔌 Connect';
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
      const t = document.getElementById('radioToggle'); if (t) t.textContent = '📻 Radio ▸';
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
  { id:'C-1', rx:'14109000', tx:'14109000', mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'Primary',    inhCall:false, inhSnd:false },
  { id:'C-2', rx:'7102000',  tx:'7102000',  mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'40m Backup', inhCall:false, inhSnd:false },
  { id:'C-3', rx:'3596000',  tx:'3596000',  mode:'USB', usage:'VOICE', dir:'RX/TX', self:'', label:'80m Night',  inhCall:false, inhSnd:false },
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

const CH_MODES = ['USB', 'USB-D', 'LSB', 'LSB-D'];
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
          <div class="ch-id-ro" title="Auto-assigned, matches Core's channel id">${escapeHtml(c.id)}</div>
        </div>
        <div class="ch-field">
          <label>RX Hz</label>
          <input class="ch-inp" value="${escapeHtml(c.rx)}" placeholder="14000000" oninput="chSet(${i},'rx',this.value)" onblur="chCommit(${i})">
        </div>
        <div class="ch-field">
          <label>TX Hz</label>
          <input class="ch-inp" value="${escapeHtml(c.tx)}" placeholder="same as RX" oninput="chSet(${i},'tx',this.value)" onblur="chCommit(${i})">
        </div>
        <div class="ch-field ch-grow">
          <label>Label</label>
          <input class="ch-inp" value="${escapeHtml(c.label)}" placeholder="Description" oninput="chSet(${i},'label',this.value)" onblur="chCommit(${i})">
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
      </div>
    </div>`).join('');
}

// Mutate model in place — no re-render, so text inputs keep focus/caret while typing.
function chSet(i, field, val) { channels[i][field] = val; }

// Sync one row to the bridge once it has a usable RX frequency (fires on
// blur/select-change — not per keystroke). ALEController::add_channel()
// matches/replaces by rx_frequency_hz, so editing RX Hz on an existing
// channel creates a new entry rather than renaming the old one — same
// behaviour as editing a .ale file by hand; not something the GUI papers
// over here. Core's Channel has no per-channel self-address or separate
// inhibit-calling/-sounding flags (only one combined `enabled`), so
// self/inhCall/inhSnd round-trip approximately — see syncChannelsFromBridge().
function chCommit(i) {
  if (!bridgeConnected) return;
  const c = channels[i];
  const rxHz = parseInt(c.rx, 10);
  if (!rxHz) return;  // nothing to sync yet — still being typed
  bridgeSend('CHANNEL_ADD', {
    rx_hz: rxHz,
    tx_hz: parseInt(c.tx, 10) || rxHz,
    mode: c.mode,
    label: c.label,
    enabled: !(c.inhCall && c.inhSnd),
    voice_use: c.usage !== 'DATA',
    data_use: c.usage !== 'VOICE',
  }, () => syncChannelsFromBridge());
}

function syncChannelsFromBridge() {
  bridgeSend('CHANNELS_LIST', {}, (r) => {
    if (!r.ok) return;
    channels = r.data.map(c => ({
      id: c.id, rx: String(c.rx_hz), tx: String(c.tx_hz), mode: c.mode, label: c.label,
      usage: c.voice_use && c.data_use ? 'BOTH' : c.data_use ? 'DATA' : 'VOICE',
      dir: c.rx_only ? 'RX' : 'RX/TX',
      self: '',                                   // Core has no per-channel self-address yet
      inhCall: !c.enabled, inhSnd: !c.enabled,     // Core has one combined enabled flag, not two
    }));
    renderChannels();
    renderNets();
    renderSoundPanel();   // net channel labels in the sounding dropdown
    updateScanBtn();   // channel count changed → refresh Scan button gating
  });
}

function addCh() {
  channels.push({ id:nextFreeChannelId(channels), rx:'', tx:'', mode:'USB', usage:'BOTH', dir:'RX/TX', self:'', label:'', inhCall:false, inhSnd:false });
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
  { name:'NET1', channelIds:['C-1'] },
];

function renderNets() {
  const el = document.getElementById('netList');
  if (!el) return;
  el.innerHTML = nets.length ? nets.map((n, i) => `
    <div style="display:flex;gap:8px;align-items:flex-start">
      <input class="finput" value="${escapeHtml(n.name)}" style="width:140px" oninput="netSet(${i},'name',this.value)">
      <div class="fhint" style="margin:4px 0 0;flex:1;display:flex;gap:12px;flex-wrap:wrap">
        ${channels.map(c => `
          <label style="display:flex;align-items:center;gap:4px;color:var(--tx)">
            <input type="checkbox" ${n.channelIds.includes(c.id) ? 'checked' : ''}
              onchange="toggleNetChannel(${i},'${c.id}',this.checked)"> ${escapeHtml(c.id)}
          </label>`).join('') || '<span class="fhint" style="margin:0">No channels configured</span>'}
      </div>
      <button class="ch-del" onclick="delNet(${i})" title="Delete net">✕</button>
    </div>`).join('') : '<div class="msg-empty">No nets configured</div>';
}

// Net rename has no direct Core primitive (NetStore is add_net(name)/
// del_net(name) only) — kept local-only here rather than faking a rename
// via delete+recreate+reassign, which would briefly drop the net and isn't
// worth the edge-case risk for what's a rare admin action.
function netSet(i, field, val) { nets[i][field] = val; }

function toggleNetChannel(i, chId, on) {
  const ids = nets[i].channelIds;
  if (on) { if (!ids.includes(chId)) ids.push(chId); }
  else    { nets[i].channelIds = ids.filter(id => id !== chId); }
  if (bridgeConnected) bridgeSend(on ? 'NET_ASSIGN' : 'NET_UNASSIGN', { net: nets[i].name, channel_id: chId });
}

function syncNetsFromBridge() {
  bridgeSend('NETS_LIST', {}, (r) => {
    if (!r.ok) return;
    nets = r.data.map(n => ({ name: n.name, channelIds: n.channel_ids }));
    renderNets();
    renderSoundPanel();   // sounding dropdown's net list mirrors the configured nets
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

function loadAleFile()  { /* TODO: integrate with backend CMD:LOAD_CHANNELS */ }
function saveAleFile()  { /* TODO: integrate with backend CMD:SAVE_CHANNELS */ }
function exportConf()   { /* TODO: serialize all cfgXxx inputs to key=value */ }
function importConf()   { /* TODO: parse uploaded file into cfgXxx inputs */ }

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

function saveSettings() {
  applyManualAcceptToBridge();
  applyTimingToBridge();      // Timing + Calling Policy → core
  applyLqaToBridge();         // Record-LQA toggle → core (A.5.4.1.1)
  applyRelinkToBridge();      // Auto-Relink toggle + threshold → core (A.5.4.5)
  applyEnhFreqSelectToBridge(); // Enhanced Freq-Select → core (A.5.6.3.2)
  applySoundAuto();           // interval may have changed → re-assert periodic mode
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
  // periodic-sounding net in the label.
  b.disabled = false;
  b.textContent = (activeSoundNet ? '📢 ' + activeSoundNet + ' ' : '📢 Sound ') + (soundPanelOpen() ? '▾' : '▸');
  b.title = activeSoundNet
    ? 'Periodic sounding on ' + activeSoundNet + ' every ' + soundingIntervalSec() + ' s'
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
  document.getElementById('soundBtn').textContent =
    (activeSoundNet ? '📢 ' + activeSoundNet + ' ' : '📢 Sound ') + (open ? '▾' : '▸');
  if (open) renderSoundPanel();
}

// Build the per-net list from the configured nets + channels. Each row enables
// periodic multi-channel sounding on that net's channels (SOUND_AUTO), driven by
// the Sounding Interval setting. The active net is highlighted.
function renderSoundPanel() {
  const el = document.getElementById('soundNetList');
  if (!el) return;
  if (!nets.length) {
    el.innerHTML = '<div class="fhint" style="margin:0">No nets — configure in Settings ▸ Nets.</div>';
    return;
  }
  el.innerHTML = nets.map(n => {
    const chans = (n.channelIds || []).join(', ') || '—';
    const active = n.name === activeSoundNet;
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

// Select a net for periodic multi-channel sounding (SOUND_AUTO on).
function soundAutoNet(name) {
  activeSoundNet = name;
  const cb = document.getElementById('cfgAutoSound'); if (cb) cb.checked = true;
  applySoundAuto();
  renderSoundPanel();
  closeSoundPanel();
}

function soundAutoOff() {
  activeSoundNet = null;
  const cb = document.getElementById('cfgAutoSound'); if (cb) cb.checked = false;
  applySoundAuto();
  renderSoundPanel();
}

// Push the periodic-sounding mode to the core. ON when a net is selected and the
// Automatic Sounding toggle is checked; OFF otherwise. The interval comes from
// the Sounding Interval setting (Timing), wired via applyTimingToBridge().
let activeSoundNet = null;
function applySoundAuto() {
  if (!bridgeConnected) { updateSoundBtn(); return; }
  const on = !!activeSoundNet && !!(document.getElementById('cfgAutoSound')?.checked);
  bridgeSend('SOUND_AUTO', { on, interval_sec: soundingIntervalSec(), net: on ? activeSoundNet : '' });
  updateSoundBtn();
}

function closeSoundPanel() {
  const p = document.getElementById('soundPanel');
  if (p) p.classList.remove('open');
  updateSoundBtn();
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
  const sc = numZ('cfgTargetScan'); if (sc !== null) args.assumed_scan_channels = sc;
  bridgeSend('TIMING_SET', args);
}

// Push the "Record LQA" toggle to the core (A.5.4.1.1 per-frame BER/SNR
// measurement into the LQA Memory). Fired on change and from saveSettings().
function applyLqaToBridge() {
  if (!bridgeConnected) return;
  const on = document.getElementById('cfgRecLqa')?.checked ?? true;
  bridgeSend('LQA_SET', { lqa_enabled: on });
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
let radioEntry   = '';            // keypad direct-entry buffer (digits)
let pttOn        = false;

function fmtRadioFreq(hz) {
  const mhz = Math.floor(hz / 1e6);
  const khz = Math.floor((hz % 1e6) / 1e3);
  return mhz + '.' + String(khz).padStart(3,'0') + '.' + String(hz % 1000).padStart(3,'0');
}
function fmtStatFreq(hz) { return (hz / 1e6).toFixed(3); }

function updateRadioDisplay() {
  const fEl = document.getElementById('radioFreq');
  const uEl = document.getElementById('radioUnit');
  const mEl = document.getElementById('radioModeChip');
  if (fEl) {
    if (radioEntry) { fEl.textContent = radioEntry;             fEl.classList.add('entry'); }
    else            { fEl.textContent = fmtRadioFreq(radioFreqHz); fEl.classList.remove('entry'); }
  }
  if (uEl) uEl.textContent = radioEntry ? 'MHz / kHz ?' : 'MHz';
  if (mEl) mEl.textContent = radioMode;
  // mirror onto the header readout + linked-panel label
  const fv = document.getElementById('freqVal');     if (fv) fv.textContent = fmtStatFreq(radioFreqHz);
  const fs = document.getElementById('freqSub');
  if (fs) {
    const ch = chFromFreq(radioFreqHz);
    fs.textContent = (ch ? ch.id.replace('C-', 'CH ') : 'VFO') + ' · ' + radioMode;
  }
  const cf = document.getElementById('callFreqLbl');  if (cf) cf.textContent = fmtStatFreq(radioFreqHz) + ' MHz · ' + radioMode;
  updateHeaderSinadFromLqa();
  document.querySelectorAll('.rk-mode').forEach(b => b.classList.toggle('active', b.dataset.mode === radioMode));
  document.querySelectorAll('.rk-step').forEach(b => b.classList.toggle('active', +b.dataset.step === radioStep));
}

function toggleRadioPanel() {
  const open = document.getElementById('radioPanel').classList.toggle('open');
  document.getElementById('radioToggle').textContent = open ? '📻 Radio ▾' : '📻 Radio ▸';
  if (open) updateRadioDisplay();
}
// dismiss the VFO / Sounding panels on an outside click
document.addEventListener('click', e => {
  const wrap = document.getElementById('radioWrap');
  const panel = document.getElementById('radioPanel');
  if (panel && panel.classList.contains('open') && wrap && !wrap.contains(e.target)) {
    panel.classList.remove('open');
    document.getElementById('radioToggle').textContent = '📻 Radio ▸';
  }
  const swrap = document.getElementById('soundWrap');
  const spanel = document.getElementById('soundPanel');
  if (spanel && spanel.classList.contains('open') && swrap && !swrap.contains(e.target)) {
    spanel.classList.remove('open');
    updateSoundBtn();
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
    const b = document.getElementById('pttBtn');
    if (b) { b.classList.toggle('ptt-on', pttOn); b.textContent = pttOn ? '● TX' : '🎙 PTT'; }
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
  updateRadioDisplay();
}

// Operator manual PTT override — sends SET_PTT to the bridge so the
// controller asserts/releases PTT independently of the ALE state machine.
// Button is press-and-hold: onmousedown/ontouchstart → on=true,
// onmouseup/ontouchend/onmouseleave → on=false.
function setPtt(on) {
  if (radioCtrlLocked()) return;
  if (bridgeConnected) bridgeSend('SET_PTT', { on });
  pttOn = on;
  const b = document.getElementById('pttBtn');
  if (b) { b.classList.toggle('ptt-on', on); b.textContent = on ? '⚡ TX' : '🎙 PTT'; }
}

// keypad direct entry — digits accumulate, MHz/kHz (or ENT) commit
function radioKey(d)  { if (radioEntry.length < 9) { radioEntry += d; updateRadioDisplay(); } }
function radioDel()   { radioEntry = radioEntry.slice(0, -1); updateRadioDisplay(); }
function radioClear() { radioEntry = ''; updateRadioDisplay(); }
function radioCommit(unitHz) {
  if (radioCtrlLocked()) return;
  if (!radioEntry) return;
  const v = parseInt(radioEntry, 10);
  radioEntry = '';
  if (isNaN(v)) { updateRadioDisplay(); return; }
  const hz = Math.min(v * unitHz, 999999999);
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
function radioSetStep(hz){
  if (radioCtrlLocked()) return;
  if (bridgeConnected) bridgeSend('VFO_SET_TUNE_STEP', { hz });
  radioStep = hz; updateRadioDisplay();
}
function radioNudge(dir) {
  if (radioCtrlLocked()) return;
  radioEntry = '';
  if (bridgeConnected) { bridgeSend('VFO_NUDGE', { direction: dir }, () => syncVfoFromBridge()); return; }
  radioFreqHz = Math.max(0, Math.min(radioFreqHz + dir * radioStep, 999999999));
  radioChannel = -1;
  updateRadioDisplay();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   MESSAGES  (AMD orderwire — receive, send, delete)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let messages = [];
function renderMessages() {
  const el = document.getElementById('msgList');
  if (!messages.length) { el.innerHTML = '<div class="msg-empty">No messages</div>'; return; }
  el.innerHTML = '<div class="msg-list">' + messages.map((m, i) => `
    <div class="msg-item${m.own?' msg-own':''}">
      <button class="msg-del" title="Delete" onclick="deleteMessage(${i})">✕</button>
      <div class="msg-hdr">
        <span class="msg-from">${m.from}${m.own?' →':''}</span>
      </div>
      <div class="msg-text">${escapeHtml(m.text)}</div>
      <span class="msg-time">${m.time}</span>
    </div>`).join('') + '</div>';
}
function deleteMessage(i) { messages.splice(i, 1); renderMessages(); }
function clearMessages()  { messages = []; renderMessages(); }
// AMD orderwire is queued for the operator's NEXT CMD:CALL — it is not a
// live message channel over an already-established link (MIL-STD-188-141B
// carries it in the calling sequence's MESSAGE section, not after LINKED).
// process_command("CMD:AMD ...") is the only entry point for this on
// ALEController (no dedicated typed method) — see apps/ale_bridge.cpp's
// dispatch_command().
function sendAmd() {
  const inp = document.getElementById('msgInput');
  const txt = (inp.value || '').toUpperCase().trim();
  if (!txt) return;
  const self = primarySelfAddr();
  messages.unshift({ from:self, time:nowZulu(), text:txt, own:true });
  inp.value = '';
  renderMessages();
  if (bridgeConnected) { bridgeSend('AMD', { text: txt }); return; }
  const to = selectedContact ? selectedContact.cs.slice(0,3) : '@@@';
  aleLogInfo('AMD demo: TO:' + to + ' DATA:AMD TIS:' + self.slice(0, 3));
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

// Bridge's LQA_LIST format (see ALEController::get_all_lqa_entries()):
//   freq_hz|station|snr_db|ber|sinad_db|score|age_ms|bilateral_sinad_db|
//   bilateral_ber|bilateral_mp|display_score
// FROM-direction (snr_db/ber/sinad_db) is locally measured; bilateral_* is what
// the peer reported via CMD LQA (A.5.4.2). Per spec: SINAD code = dB, higher =
// better (31 = no measurement); BER code = 2/3-vote count, lower = better
// (31 = no value); MP = ms (7 = not measured). Prefer a real measurement from
// either direction; show "—" for the no-data sentinels. display_score already
// incorporates the bilateral fallback (compute_score), so it is non-zero for
// bilateral-only stub entries.
function syncLqaFromBridge() {
  // Reflect the core's Record-LQA state into the toggle (A.5.4.1.1).
  bridgeSend('LQA_GET', {}, (r) => {
    if (r.ok && typeof r.lqa_enabled === 'boolean') {
      const el = document.getElementById('cfgRecLqa');
      if (el) el.checked = r.lqa_enabled;
    }
  });
  syncRelinkFromBridge();
  syncEnhFreqSelectFromBridge();
  bridgeSend('LQA_LIST', {}, (r) => {
    if (!r.ok) return;
    const prevKeys = new Set(lqaEntries.map(e => e.addr + '|' + e.ch));
    lqaEntries = r.data.map(e => {
      const fromSinad = (typeof e.sinad_db === 'number' && e.sinad_db > 0)
        ? String(Math.round(e.sinad_db))
        : (typeof e.snr_db === 'number' && e.snr_db > 0)
        ? String(Math.round(e.snr_db)) : null;
      const bilatSinad = (typeof e.bilateral_sinad_db === 'number'
                          && e.bilateral_sinad_db <= 30)
        ? String(Math.round(e.bilateral_sinad_db)) : null;
      const fromBer = (typeof e.ber === 'number' && e.ber > 0)
        ? e.ber.toExponential(1) : null;
      const bilatBer = (typeof e.bilateral_ber === 'number')
        ? fmtBerCode(e.bilateral_ber) : null;
      const mp = (typeof e.bilateral_mp === 'number' && e.bilateral_mp >= 0
                  && e.bilateral_mp <= 6)
        ? e.bilateral_mp.toFixed(0) + ' ms' : '—';
      return {
        addr:    e.station || '(sounding)',
        ch:      (e.freq_hz / 1e6).toFixed(3),
        freq_hz: e.freq_hz,
        score:   Math.round(e.display_score != null ? e.display_score : e.score),
        sinad:   fromSinad || bilatSinad || '—',
        sinad_db: (typeof e.sinad_db === 'number' && e.sinad_db > 0) ? e.sinad_db
                : (typeof e.snr_db === 'number' && e.snr_db > 0)   ? e.snr_db
                : typeof e.bilateral_sinad_db === 'number' ? e.bilateral_sinad_db : 0,
        snr_db:  typeof e.snr_db === 'number' ? e.snr_db : 0,
        ber:     fromBer   || bilatBer   || '—',
        mp,
        ageMin:  Math.round(e.age_ms / 60000),
      };
    });
    lqaEntries.forEach(e => {
      if (!prevKeys.has(e.addr + '|' + e.ch))
        onAleLogLqa(e.addr, e.freq_hz, e.score, e.sinad_db);
    });
    renderLqa();
    updateHeaderSinadFromLqa();
  });
}
// LQA changes from real radio activity (soundings/contacts), not GUI
// actions — periodic poll, same reasoning as the VFO poll above.
setInterval(() => { if (bridgeConnected) syncLqaFromBridge(); }, 5000);

function clearLqa() {
  if (!confirm('Clear all LQA data? This also overwrites the saved file.')) return;
  bridgeSend('LQA_CLEAR', {}, () => syncLqaFromBridge());
}

function lqaClass(s) { return s >= 24 ? 'lqa-hi' : s >= 14 ? 'lqa-mid' : 'lqa-lo'; }
function renderLqa() {
  const tb = document.getElementById('lqaBody');
  if (!tb) return;
  const sort = document.getElementById('cfgLqaSort')?.value || 'score';
  const rows = [...lqaEntries].sort((a, b) =>
    sort === 'age'  ? a.ageMin - b.ageMin :
    sort === 'addr' ? a.addr.localeCompare(b.addr) || b.score - a.score :
                      b.score - a.score);
  tb.innerHTML = rows.length ? rows.map(e => `
    <tr>
      <td class="lqa-cell" style="text-align:left">${escapeHtml(e.addr)}</td>
      <td class="lqa-cell">${e.ch}</td>
      <td class="lqa-cell ${lqaClass(e.score)}">${e.score}</td>
      <td class="lqa-cell">${e.sinad === '—' ? '—' : e.sinad + ' dB'}</td>
      <td class="lqa-cell">${e.ber}</td>
      <td class="lqa-cell">${e.mp}</td>
      <td class="lqa-cell">${e.ageMin}m</td>
    </tr>`).join('') : '<tr><td colspan="7" class="msg-empty">No LQA data yet</td></tr>';
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
// Show overlay immediately; it is hidden as soon as the WebSocket handshake succeeds.
setBridgeOverlay(true);
// Small delay before first connect: the bridge serves CSS/JS sequentially on the same
// thread as WebSocket upgrades.  On a cold cache load, static resources are still
// in-flight when this code runs; 200 ms lets the browser finish requesting them so
// the WS upgrade is not competing with a pending HTTP response.
setTimeout(connectBridge, 200);
