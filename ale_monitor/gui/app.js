'use strict';
/* ALE Monitor — browser-side JS.
 * Adapted from apps/gui/app.js.  Passive RX-only: no TX, no call, no link
 * events, no audio/radio config panels.  Display sections copied verbatim.  */

// ── Bridge connection ────────────────────────────────────────────────────────

let bridgeWs             = null;
let bridgeConnected      = false;
let bridgeReconnectTimer = null;
let bridgeReqId          = 0;
const bridgePending      = new Map();
let latestSpectrum       = null;  // Float32Array from last binary frame

const wfMarkers = [];  // ALE frame markers, aged in drawWaterfall()

function bridgeWsUrl() {
  return 'ws://' + window.location.host;
}

function setBridgeOverlay(show) {
  const el = document.getElementById('bridgeOverlay');
  el.classList.toggle('hidden', !show);
  if (show) {
    const port = window.location.port || '…';
    document.getElementById('bridgeOverlayCmd').textContent =
      'ale_monitor --port ' + port + ' --net-file nets/USA.ale';
  }
}

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
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    bridgeConnected = true;
    setBridgeOverlay(false);
    aleLogInfo('Monitor connected — syncing state');
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

  ws.onerror = () => {};

  ws.onclose = () => {
    if (bridgeConnected) aleLogInfo('Monitor disconnected');
    bridgeConnected = false;
    bridgeWs = null;
    bridgePending.clear();
    setBridgeOverlay(true);
    if (!bridgeReconnectTimer) {
      bridgeReconnectTimer = setTimeout(() => {
        bridgeReconnectTimer = null;
        connectBridge();
      }, 1000);
    }
  };
}

// ── Sync on connect ──────────────────────────────────────────────────────────

function syncAllFromBridge() {
  bridgeSend('STATUS', {}, applyStatusReply);
  syncChannelsFromBridge();
  syncLqaFromBridge();
  syncVfoFromBridge();
  syncSettingsFromBridge();
}

// ── Settings panel sync ──────────────────────────────────────────────────────

function syncSettingsFromBridge() {
  // Audio device dropdown (enumerate; select the currently-open device if any).
  // The bridge's WASAPI resolve_device() matches the BARE device name, so strip
  // the "IN:"/"OUT:" prefix the enumerator adds (mirrors apps/gui/app.js enumDevices).
  bridgeSend('AUDIO_DEVICES', {}, (r) => {
    if (!r.ok) return;
    const strip = s => s.replace(/^(IN:|OUT:)\s*/, '');
    const sel = document.getElementById('setAudioIn');
    sel.innerHTML = '<option value="">(none — RX idle)</option>' +
      r.inputs.map(d => { const n = strip(d); return `<option value="${escapeHtml(n)}">${escapeHtml(n)}</option>`; }).join('');
  });
  // Radio model dropdown (full Hamlib rig list).
  bridgeSend('RIG_LIST', {}, (r) => {
    if (!r.ok) return;
    const sel = document.getElementById('setRigModel');
    sel.innerHTML = '<option value="">(no radio)</option>' +
      r.rigs.map(g => `<option value="${g.id}">${escapeHtml(g.mfg + ' ' + g.macro + ' [' + g.port + ']')}</option>`).join('');
  });
  // Current monitor config → fill the panel fields.
  bridgeSend('MON_CONFIG_GET', {}, (r) => {
    if (!r.ok) return;
    setField('setDwell',         r.dwell_ms);
    setField('setFilter',         r.channel_filter || 'all');
    setField('setModeOverride',   r.mode_override || '');
    setField('setRigHost',        r.rig_host || '127.0.0.1');
    setField('setRigPort',        r.rig_port || '4532');
    setField('setRigSerial',      r.rig_serial || '');
    setField('setRigBaud',        r.rig_baud || 0);
    if (r.rig_model) setField('setRigModel', String(r.rig_model));
    // Reflect the active audio device after the device list has populated.
    setTimeout(() => {
      if (r.audio_in) setField('setAudioIn', r.audio_in);
      document.getElementById('audioStatus').textContent = r.audio_in ? 'open: ' + r.audio_in : 'idle';
    }, 50);
  });
  bridgeSend('RIG_STATUS', {}, (r) => {
    if (!r.ok) return;
    document.getElementById('rigStatus').textContent = r.status;
  });
}

function setField(id, val) {
  const el = document.getElementById(id);
  if (!el) return;
  if (el.tagName === 'SELECT') {
    for (const o of el.options) if (String(o.value) === String(val)) { o.selected = true; return; }
  } else {
    el.value = val;
  }
}

function toggleSettings() {
  document.getElementById('settingsPanel').classList.toggle('hidden');
}

// ── Settings actions ─────────────────────────────────────────────────────────

function doAudioOpen() {
  const inDev = document.getElementById('setAudioIn').value;
  bridgeSend('AUDIO_OPEN', { in: inDev }, (r) => {
    document.getElementById('audioStatus').textContent =
      r.ok ? 'open: ' + inDev : ('failed: ' + (r.error || '?'));
  });
}
function doAudioClose() {
  bridgeSend('AUDIO_CLOSE', {}, () => {
    document.getElementById('audioStatus').textContent = 'idle';
  });
}
function doRigConnect() {
  const args = {
    model:  document.getElementById('setRigModel').value,
    host:   document.getElementById('setRigHost').value,
    port:   document.getElementById('setRigPort').value,
    serial: document.getElementById('setRigSerial').value,
    baud:   Number(document.getElementById('setRigBaud').value || 0),
  };
  bridgeSend('RIG_CONNECT', args, (r) => {
    document.getElementById('rigStatus').textContent =
      r.ok ? r.status : ('failed: ' + (r.error || '?'));
  });
}
function doRigDisconnect() {
  bridgeSend('RIG_DISCONNECT', {}, (r) => {
    document.getElementById('rigStatus').textContent = r.status || 'disconnected';
  });
}
function doDwellSet() {
  const ms = Number(document.getElementById('setDwell').value || 0);
  bridgeSend('TIMING_SET', { scan_dwell_ms: ms }, (r) => {
    if (r.ok) aleLogInfo('Dwell set to ' + ms + ' ms');
  });
}
function doFilterSet() {
  const f = document.getElementById('setFilter').value;
  bridgeSend('MON_FILTER', { filter: f }, (r) => {
    if (r.ok) aleLogInfo('Channel filter: ' + f);
  });
}
function doModeOverrideSet() {
  const m = document.getElementById('setModeOverride').value.trim();
  bridgeSend('MON_MODE_OVERRIDE', { mode: m }, (r) => {
    if (r.ok) aleLogInfo(m ? ('Mode override: ' + m) : 'Mode restored from file');
  });
}
function doConfigSave() {
  bridgeSend('MON_CONFIG_SAVE', {}, (r) => {
    aleLogInfo(r.ok ? 'Settings saved as startup config' : ('Save failed: ' + (r.error || '?')));
  });
}

// Pull current freq/mode from the radio on connect and on channel_changed events.
function syncVfoFromBridge() {
  bridgeSend('VFO_GET', {}, (r) => {
    if (!r.ok) return;
    radioFreqHz = r.freq_hz;
    radioMode   = r.mode;
    updateFreqDisplay();
  });
}

// Poll LQA + rig heartbeat every 5 s.
setInterval(() => {
  if (bridgeConnected) syncLqaFromBridge();
}, 5000);

// ── State display ────────────────────────────────────────────────────────────

function applyStatusReply(r) {
  if (!r.ok) return;
  applyState(r.state);
}

function applyState(state) {
  const dot  = document.getElementById('statusDot');
  const text = document.getElementById('statusText');
  const cls  = state ? state.toLowerCase() : 'idle';
  dot.className  = 'dot ' + cls;
  text.textContent = state || 'IDLE';
  text.style.color = state === 'SCANNING'  ? 'var(--s-scanning)'
                   : state === 'HANDSHAKE' ? 'var(--s-handshake)'
                   : state === 'LINKED'    ? 'var(--s-linked)'
                   : 'var(--accent)';
}

// ── Event handler ────────────────────────────────────────────────────────────

function onBridgeEvent(e) {
  switch (e.event) {
    case 'state':
      applyState(e.value);
      break;
    case 'status':
      aleLogInfo(e.msg);
      break;
    case 'word_decoded':
      onAleLogWord(e, 'rx');
      break;
    case 'frame_decoded':
      onAleLogFrame(e);
      break;
    case 'channel_changed':
      // Real-time hop update — no need to poll VFO_GET on every dwell.
      radioFreqHz = e.rx_hz;
      radioMode   = e.mode;
      updateFreqDisplay();
      break;
    case 'channel_busy':
      applyLbtState(e.busy, e.level_db, e.floor_db);
      break;
    default:
      break;
  }
}

// ── LBT busy indicator ───────────────────────────────────────────────────────

function applyLbtState(busy, level_db, floor_db) {
  const chip  = document.getElementById('lbtChip');
  const label = document.getElementById('lbtLabel');
  chip.classList.toggle('busy', busy);
  if (busy) {
    const margin = (level_db != null && floor_db != null)
      ? ' +' + Math.round(level_db - floor_db) + 'dB' : '';
    label.textContent = 'BUSY' + margin;
  } else {
    label.textContent = 'IDLE';
  }
}

// ── Scan / Available controls ────────────────────────────────────────────────

function doScan() {
  bridgeSend('SCAN', {}, (r) => {
    if (r.ok) aleLogInfo('Scanning started');
    else aleLogInfo('Scan failed: ' + (r.error || '?'));
  });
}
function doAvailable() {
  bridgeSend('AVAILABLE', {}, (r) => {
    if (r.ok) aleLogInfo('Stopped scanning — monitoring single channel');
  });
}
function clearLog() {
  const body = document.getElementById('aleLogBody');
  body.innerHTML = '<div class="ale-log-empty">Log cleared</div>';
  aleLogCount = 0;
  document.getElementById('aleLogCount').textContent = '0 entries';
  wfMarkers.length = 0;
}

// ── Freq display ─────────────────────────────────────────────────────────────

let radioFreqHz = 0;
let radioMode   = 'USB';

function fmtFreq(hz) {
  if (!hz) return '—';
  return (hz / 1e6).toFixed(6);
}
function freqUnit(hz) {
  return hz >= 1e9 ? 'GHz' : 'MHz';
}

function updateFreqDisplay() {
  const fv = document.getElementById('freqVal');
  const fu = document.getElementById('freqUnit');
  const fs = document.getElementById('freqSub');
  if (fv) fv.textContent = fmtFreq(radioFreqHz);
  if (fu) fu.textContent = freqUnit(radioFreqHz);
  if (fs) {
    const ch = chFromFreq(radioFreqHz);
    const chLabel = ch ? ch.id.replace('C-', 'CH ') : 'VFO';
    fs.textContent = chLabel + ' · ' + radioMode;
  }
}

// ── Channel store ────────────────────────────────────────────────────────────

let channels = [];

function syncChannelsFromBridge() {
  bridgeSend('CHANNELS_LIST', {}, (r) => {
    if (!r.ok) return;
    channels = r.data.map(c => ({
      id:   c.id,
      rx:   String(c.rx_hz),
      mode: c.mode || 'USB',
    }));
    positionBandOverlay();
    updateFreqDisplay();
  });
}

// ── Channel lookup helpers (from apps/gui/app.js — verbatim) ─────────────────

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
function fmtChFreqExact(freqHz) {
  const ch = chFromFreq(freqHz);
  const hz = ch ? parseInt(ch.rx, 10) : freqHz;
  return hz ? (hz / 1e6).toFixed(6) : '?';
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  WATERFALL  (copied verbatim from apps/gui/app.js lines 386–608)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

const canvas = document.getElementById('waterfallCanvas');
const ctx    = canvas.getContext('2d');

const TONES    = [750, 1000, 1250, 1500, 1750, 2000, 2250, 2500];
const BW_LO    = 0, BW_HI = 4000;
const ALE_LO   = 750, ALE_HI = 2500;
const ALE_GUARD = 125;
const AXIS_MAJOR = [0, 1000, 2000, 3000, 4000];
const AXIS_MINOR = [500, 1500, 2500, 3500];

let rows     = [];
let nextRowAt = 0;

function resizeCanvas() {
  const el = canvas.parentElement;
  canvas.width  = el.clientWidth;
  canvas.height = el.clientHeight;
  rows = [];
  buildTicks();
  positionBandOverlay();
}

window.addEventListener('resize', resizeCanvas);

function buildTicks() {
  const el = document.getElementById('wfTicks');
  el.innerHTML = '';
  const span = BW_HI - BW_LO;

  AXIS_MINOR.forEach(hz => {
    const t = document.createElement('div');
    t.className = 'wf-tick minor';
    t.style.left = ((hz - BW_LO) / span * 100) + '%';
    t.style.transform = 'translateX(-50%)';
    el.appendChild(t);
  });

  const last = AXIS_MAJOR.length - 1;
  AXIS_MAJOR.forEach((hz, i) => {
    const pct   = (hz - BW_LO) / span * 100;
    const xform = i === 0    ? 'translateX(0)'
                : i === last ? 'translateX(-100%)'
                :              'translateX(-50%)';
    const tick = document.createElement('div');
    tick.className = 'wf-tick';
    tick.style.left = pct + '%';
    tick.style.transform = xform;
    el.appendChild(tick);
    const lbl = document.createElement('div');
    lbl.textContent = hz >= 1000 ? (hz / 1000) + 'k' : hz;
    tick.appendChild(lbl);
  });
}

function positionBandOverlay() {
  const span = BW_HI - BW_LO;
  const lo   = (ALE_LO - ALE_GUARD - BW_LO) / span * 100;
  const width = (ALE_HI + ALE_GUARD - (ALE_LO - ALE_GUARD)) / span * 100;
  const region = document.getElementById('wfBandRegion');
  const label  = document.getElementById('wfBandLabel');
  if (region) { region.style.left = lo + '%'; region.style.width = width + '%'; }
  if (label)  { label.style.left  = (lo + width / 2) + '%'; }
}

let emaFloor = -90;
let emaPeak  = -20;

function genRowFromSpectrum(spectrum) {
  const W     = canvas.width;
  const n     = spectrum.length;
  const span  = BW_HI - BW_LO;
  const row   = new Float32Array(W);
  const alpha_floor = 0.03;
  const attack = 0.20, decay = 0.002;

  let sum = 0, peak = -120;
  for (let k = 0; k < n; k++) { sum += spectrum[k]; if (spectrum[k] > peak) peak = spectrum[k]; }
  const avg = sum / n;
  emaFloor = emaFloor + alpha_floor * (avg - emaFloor);
  emaPeak  = peak > emaPeak ? emaPeak + attack * (peak - emaPeak)
                             : emaPeak + decay  * (peak - emaPeak);
  const range = Math.max(20, emaPeak - emaFloor);

  for (let x = 0; x < W; x++) {
    const hz = BW_LO + (x / W) * span;
    const k  = Math.min(n - 1, Math.round(hz / (BW_HI / n)));
    row[x] = Math.max(0, Math.min(1, (spectrum[k] - emaFloor) / range));
  }
  return row;
}

function genRow() {
  if (latestSpectrum) return genRowFromSpectrum(latestSpectrum);
  const W = canvas.width;
  const row = new Float32Array(W);
  for (let x = 0; x < W; x++) row[x] = Math.random() * 0.03;
  return row;
}

function energy2rgb(v) {
  if (v < 0.20) {
    const t = v / 0.20;
    return [0, 0, Math.round(t * 180)];
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

  for (const m of wfMarkers) {
    if (m.age >= H) continue;
    ctx.fillStyle = m.color;
    ctx.fillRect(0, m.age, 4, 3);
  }

  requestAnimationFrame(drawWaterfall);
}

function onSpectrumFrame(buf) {
  latestSpectrum = new Float32Array(buf);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  HEARD STATIONS  (adapted from apps/gui/app.js — no address-book button)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

let heardStations = [];
let heardDeleted  = new Set();

function upsertHeard(e) {
  const idx = heardStations.findIndex(
    h => h.addr === e.addr && h.freq_hz === e.freq_hz);
  const metrics = {
    score:      e.score,
    available:  (typeof e.available === 'number') ? e.available : -1,
    sinad_from: e.sinad_from,
    ber_from:   e.ber_from,
    ageMin:     e.ageMin,
  };
  if (idx >= 0) {
    heardStations[idx] = { ...heardStations[idx], ...metrics };
  } else {
    const ts = new Date().toTimeString().slice(0, 8);
    heardStations.unshift({ addr: e.addr, freq_hz: e.freq_hz, ts, ...metrics });
  }
  renderHeard();
}

function deleteHeard(addr, freqHz) {
  heardDeleted.add(addr + '|' + freqHz);
  heardStations = heardStations.filter(
    h => !(h.addr === addr && h.freq_hz === freqHz));
  renderHeard();
}

function clearHeard() {
  heardStations.forEach(h => heardDeleted.add(h.addr + '|' + h.freq_hz));
  heardStations = [];
  renderHeard();
}

function renderHeard() {
  const el = document.getElementById('heardList');
  if (!el) return;
  if (!heardStations.length) {
    el.innerHTML = '<div class="heard-empty">No stations heard yet</div>';
    return;
  }
  const body = heardStations.map(h => {
    const sinadFromG = (h.sinad_from != null) ? h.sinad_from / 30 : null;
    const berFromG   = (h.ber_from   != null) ? 1 - Math.min(1, h.ber_from / 48) : null;
    const scoreG     = Math.min(1, Math.max(0, (h.score || 0) / 30));
    const ageG       = 1 - Math.min(1, (h.ageMin || 0) / 60);
    return `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${escapeHtml(h.addr)}</td>` +
      `<td class="lqa-cell">${fmtChFreqExact(h.freq_hz)}</td>` +
      availBadge(h.available) +
      qCell(h.score, scoreG) +
      qCell(h.sinad_from != null ? `+${Math.round(h.sinad_from)}` : null, sinadFromG) +
      qCell(h.ber_from   != null ? h.ber_from.toFixed(1)          : null, berFromG) +
      qCell(h.ageMin >= 60 ? '>60m' : (h.ageMin || 0) + 'm', ageG) +
      `<td class="lqa-cell heard-actions">` +
        `<button class="heard-del" onclick='deleteHeard(${JSON.stringify(h.addr)},${h.freq_hz})' title="Remove">×</button>` +
      `</td>` +
      `</tr>`;
  }).join('');
  el.innerHTML =
    `<table class="ch-table heard-table">` +
      `<thead><tr>` +
        `<th>Callsign</th><th>Freq (MHz)</th><th>Avail</th>` +
        `<th>Score</th><th>SINAD</th><th>BER</th><th>Age</th><th></th>` +
      `</tr></thead>` +
      `<tbody>${body}</tbody>` +
    `</table>`;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ALE LOG  (copied verbatim from apps/gui/app.js lines 760–884)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

const ALE_LOG_CAP   = 1000;
let   aleLogCount   = 0;
const aleLogFrameCh = new Map();

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
  const isTx  = dir === 'tx';
  const fid   = e.frame_id;
  let freqHz;
  if (isTx) {
    freqHz = e.freq_hz || 0;
  } else {
    if (!aleLogFrameCh.has(fid) && e.freq_hz)
      aleLogFrameCh.set(fid, e.freq_hz);
    freqHz = aleLogFrameCh.get(fid) || e.freq_hz || 0;
  }
  const chDisp = aleChLabel(freqHz);
  const ts     = new Date().toTimeString().slice(0, 8);
  const p      = (e.preamble || '').toLowerCase();
  const pill   = p === 'to'   ? 'pill-to'   : p === 'tis'  ? 'pill-tis'  :
                 p === 'twas' ? 'pill-twas' : p === 'thru' ? 'pill-thru' :
                 p === 'rep'  ? 'pill-rep'  : 'pill-data';
  const fec    = e.fec || 0;
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
  if (!isTx && !wfMarkers.some(m => m.frameId === fid)) {
    wfMarkers.unshift({ frameId: fid, age: 0, color: '#7a9ab8' });
    if (wfMarkers.length > 500) wfMarkers.length = 500;
  }
}

const ALE_LOG_COLORS = {
  SOUNDING: '#00dc8c', INDIVIDUAL: '#4dc8ff',
  NET: '#ffca28', AMD: '#ff8a65', UNKNOWN: '#7a9ab8',
};

function onAleLogFrame(e) {
  const m = wfMarkers.find(x => x.frameId === e.frame_id);
  if (m) m.color = ALE_LOG_COLORS[e.call_type] || ALE_LOG_COLORS.UNKNOWN;
  if (aleLogFrameCh.size > 200)
    aleLogFrameCh.delete(aleLogFrameCh.keys().next().value);
  aleLogAppend(`<div class="ale-frame-sep"></div>`);
}

function onAleLogLqa(e) {
  upsertHeard(e);
  const ts       = new Date().toTimeString().slice(0, 8);
  const freqStr  = e.freq_hz ? ` ${fmtChFreqExact(e.freq_hz)} MHz` : '';
  const lbl      = e.freq_hz ? chLabelForFreq(e.freq_hz) : '';
  const lblStr   = lbl ? ` [${lbl}]` : '';
  const scoreStr = e.score != null ? ` score=${e.score}` : '';
  const sinadDb  = e.sinad_from != null ? e.sinad_from : 0;
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

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LQA  (adapted from apps/gui/app.js — FROM-direction only, no bilateral)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// Table-A-XIII BER codes (MIL-STD-188-141B A.5.4.1.2)
const BER_TABLE_A_XIII = [
  0.0,0.006993,0.01409,0.02129,0.02860,0.03602,0.04356,0.05124,0.05904,0.06699,
  0.07508,0.08333,0.09175,0.1003,0.1091,0.1181,0.1273,0.1368,0.1464,0.1564,
  0.1667,0.1773,0.1882,0.1995,0.2113,0.2236,0.2365,0.2500,0.2643,0.2795,0.3,
];
function fmtBerCode(code) {
  if (code == null) return '—';
  const c = Math.round(code);
  if (c === 31 || c < 0 || c > 30) return '—';
  return BER_TABLE_A_XIII[c].toExponential(1);
}

let lqaEntries = [];

function syncLqaFromBridge() {
  bridgeSend('LQA_LIST', {}, (r) => {
    if (!r.ok) return;
    const prevKeys = new Set(lqaEntries.map(e => e.addr + '|' + e.ch));
    lqaEntries = r.data.map(e => {
      const sinadFromVal = (typeof e.sinad_db === 'number' && e.sinad_db > 0
                           && e.sinad_db <= 30) ? e.sinad_db : null;
      const berFromVal   = (typeof e.ber === 'number') ? e.ber : null;

      return {
        addr:       e.station || '(sounding)',
        ch:         fmtChFreqExact(e.freq_hz),
        freq_hz:    e.freq_hz,
        score:      Math.round(e.display_score != null ? e.display_score : e.score),
        sinad_from: sinadFromVal,
        ber_from:   berFromVal,
        snr_db:     typeof e.snr_db === 'number' ? e.snr_db : 0,
        age_ms:     (typeof e.age_ms === 'number') ? e.age_ms : 0,
        ageMin:     Math.round(e.age_ms / 60000),
        available:  (typeof e.available === 'number') ? e.available : -1,
      };
    });

    // Mirror into heard list.
    const dbKeys = new Set(lqaEntries.map(e => e.addr + '|' + e.freq_hz));
    lqaEntries.forEach(e => {
      const key     = e.addr + '|' + e.freq_hz;
      if (heardDeleted.has(key)) return;
      const wasInDb = prevKeys.has(e.addr + '|' + e.ch);
      const inList  = heardStations.some(h => h.addr === e.addr && h.freq_hz === e.freq_hz);
      if (!wasInDb && !inList) onAleLogLqa(e);
      else                     upsertHeard(e);
    });
    if (heardStations.some(h => !dbKeys.has(h.addr + '|' + h.freq_hz))) {
      heardStations = heardStations.filter(h => dbKeys.has(h.addr + '|' + h.freq_hz));
    }
    renderHeard();
    renderLqa();
  });
}

function clearLqa() {
  if (!confirm('Clear all LQA data? This also overwrites the saved file.')) return;
  bridgeSend('LQA_CLEAR', {}, () => {
    heardDeleted = new Set();
    syncLqaFromBridge();
  });
}

// Quality colour — red(0) → amber(0.5) → green(1)
function qColor(g) {
  const gg = Math.max(0, Math.min(1, g));
  const h  = Math.round(gg * 130);
  return `hsl(${h}, 75%, 58%)`;
}
function qCell(text, goodness) {
  if (goodness == null || text == null || text === '—')
    return `<td class="lqa-cell" style="color:var(--tx-dim)">—</td>`;
  return `<td class="lqa-cell" style="color:${qColor(goodness)};font-weight:600">${text}</td>`;
}
function availBadge(av) {
  const cls = av === 1 ? 'ha-yes' : av === 0 ? 'ha-no' : 'ha-unk';
  const txt = av === 1 ? 'AVAIL' : av === 0 ? 'N/A' : '—';
  const tip = av === 1 ? 'TIS — available for link'
            : av === 0 ? 'TWAS — not available' : 'no sounding heard';
  return `<td class="lqa-cell"><span class="heard-avail ${cls}" title="${tip}">${txt}</span></td>`;
}

function renderLqa() {
  const tb   = document.getElementById('lqaBody');
  if (!tb) return;
  const sort = document.getElementById('cfgLqaSort')?.value || 'score';
  const rows = [...lqaEntries].sort((a, b) =>
    sort === 'age'  ? a.ageMin - b.ageMin :
    sort === 'addr' ? a.addr.localeCompare(b.addr) || b.score - a.score :
                      b.score - a.score);
  tb.innerHTML = rows.length ? rows.map(e => {
    const sinadFromG = e.sinad_from != null ? e.sinad_from / 30 : null;
    const berFromG   = e.ber_from   != null ? 1 - Math.min(1, e.ber_from / 48) : null;
    const scoreG     = Math.min(1, Math.max(0, e.score / 30));
    const ageG       = 1 - Math.min(1, e.ageMin / 60);
    return `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${escapeHtml(e.addr)}</td>` +
      `<td class="lqa-cell">${e.ch}</td>` +
      availBadge(e.available) +
      qCell(e.score, scoreG) +
      qCell(e.sinad_from != null ? `+${Math.round(e.sinad_from)}` : null, sinadFromG) +
      qCell(e.ber_from   != null ? e.ber_from.toFixed(1)          : null, berFromG) +
      qCell(e.ageMin >= 60 ? '>60m' : e.ageMin + 'm', ageG) +
      `</tr>`;
  }).join('') : '<tr><td colspan="7" class="msg-empty">No LQA data yet</td></tr>';
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  HELPERS
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

function escapeHtml(s) {
  return String(s).replace(/[&<>"]/g, c =>
    ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;' }[c]));
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  BOOT
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

resizeCanvas();
drawWaterfall();
setBridgeOverlay(true);
connectBridge();
