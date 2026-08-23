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
  // resolve_device() matches the BARE device name, so strip both the "IN:"/"OUT:"
  // prefix and (ALSA only) the " — DESC" suffix list_devices() appends — leaving
  // the description in feeds ALSA's plughw: parser a comma-laden string and
  // produces "Parameter DEV must be an integer" (mirrors apps/gui/app.js enumDevices).
  bridgeSend('AUDIO_DEVICES', {}, (r) => {
    if (!r.ok) return;
    const strip = s => s.replace(/^(IN:|OUT:)\s*/, '').replace(/ — .*$/, '');
    const sel = document.getElementById('setAudioIn');
    sel.innerHTML = '<option value="">(none — RX idle)</option>' +
      r.inputs.map(d => { const n = strip(d); return `<option value="${escapeHtml(n)}">${escapeHtml(d.replace(/^(IN:|OUT:)\s*/, ''))}</option>`; }).join('');
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
  bridgeSend('WATCHLIST_GET', {}, (r) => {
    if (!r.ok) return;
    watchlist = r.data || [];
    renderWatchlist();
  });
  syncLocationSharingFromBridge();
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

// ── Location Relay (docs/LOCATION_SHARING_CONCEPT.md) — forward overheard ────
// ALE-GPR positions to a configured web API. This monitor has no self
// address, so it only ever forwards ALLCALL-broadcast reports — the GUI
// exposes a single enable toggle, not per-call-type checkboxes (unlike
// apps/gui, which has one address and can link).
function renderLocStatus(enabled, running, connState) {
  const el = document.getElementById('locStatus');
  if (!el) return;
  el.classList.remove('ok', 'err', 'warn');
  if (!enabled) { el.textContent = 'Disabled'; return; }
  if (!running) { el.textContent = 'Enabled, not running'; el.classList.add('warn'); return; }
  switch (connState) {
    case 'connected':    el.textContent = 'Running · Connected';    el.classList.add('ok');   break;
    case 'disconnected': el.textContent = 'Running · No connection'; el.classList.add('err');  break;
    case 'server_error': el.textContent = 'Running · Server error';  el.classList.add('warn'); break;
    default:              el.textContent = 'Running…'; break;  // initial probe in flight
  }
}
function syncLocationSharingFromBridge() {
  bridgeSend('LOCATION_SHARING_GET', {}, (r) => {
    if (!r.ok) return;
    document.getElementById('setLocEnabled').checked = !!r.enabled;
    document.getElementById('setLocIncludeComment').checked = !!r.include_comment;
    setField('setLocUrl',          r.url || '');
    setField('setLocCaCert',       r.ca_cert_path || '');
    setField('setLocMinInterval',  r.min_interval_sec);
    setField('setLocRoundDigits',  r.round_digits);
    // Token is never echoed back (core-side privacy) — the hint just
    // reflects whether one is already stored.
    document.getElementById('locTokenHint').textContent =
      r.token_set ? 'A token is currently stored.' : 'No token stored.';
    renderLocStatus(r.enabled, r.running, r.conn_state);
  });
}
function doLocationSharingSet() {
  bridgeSend('LOCATION_SHARING_SET', {
    enabled:          !!document.getElementById('setLocEnabled').checked,
    url:              document.getElementById('setLocUrl').value.trim(),
    token:            document.getElementById('setLocToken').value,
    ca_cert_path:     document.getElementById('setLocCaCert').value.trim(),
    min_interval_sec: Number(document.getElementById('setLocMinInterval').value || 30),
    round_digits:     Number(document.getElementById('setLocRoundDigits').value || 6),
    include_comment:  !!document.getElementById('setLocIncludeComment').checked,
  }, (r) => {
    document.getElementById('setLocToken').value = '';   // never keep the token in the DOM
    if (!r || !r.ok) {
      document.getElementById('locStatus').textContent = 'Failed: ' + ((r && r.error) || 'apply error');
      return;
    }
    syncLocationSharingFromBridge();
  });
}

// ── Watchlist / Alerting ─────────────────────────────────────────────────────

let watchlist = [];  // [{addr, audible, visible}]

function doWatchlistAdd() {
  const addrEl = document.getElementById('wlAddAddr');
  const addr = addrEl.value.trim().toUpperCase();
  if (!addr) return;
  const audible = document.getElementById('wlAddAudible').checked;
  const visible = document.getElementById('wlAddVisible').checked;
  watchlist = watchlist.filter(w => w.addr !== addr);
  watchlist.push({ addr, audible, visible });
  addrEl.value = '';
  renderWatchlist();
  doWatchlistSave();
}

function doWatchlistRemove(addr) {
  watchlist = watchlist.filter(w => w.addr !== addr);
  renderWatchlist();
  doWatchlistSave();
}

function doWatchlistSave() {
  bridgeSend('WATCHLIST_SET', { list: watchlist }, (r) => {
    if (r.ok) aleLogInfo('Watchlist updated (' + watchlist.length + ' entries) — click "Save as startup" to persist across restarts');
  });
}

function renderWatchlist() {
  const el = document.getElementById('watchlistBody');
  if (!el) return;
  if (!watchlist.length) {
    el.innerHTML = '<div class="watchlist-empty">No watchlisted callsigns</div>';
    return;
  }
  el.innerHTML = watchlist.map(w =>
    `<div class="watchlist-row">` +
      `<span>${escapeHtml(w.addr)}</span>` +
      `<span class="heard-avail ${w.audible ? 'ha-yes' : 'ha-unk'}">${w.audible ? 'AUDIBLE' : 'MUTE'}</span>` +
      `<span class="heard-avail ${w.visible ? 'ha-yes' : 'ha-unk'}">${w.visible ? 'VISIBLE' : 'HIDDEN'}</span>` +
      `<button class="heard-del" onclick='doWatchlistRemove(${JSON.stringify(w.addr)})' title="Remove">×</button>` +
    `</div>`
  ).join('');
}

// Per-station alert cooldown — avoids re-alerting on every decoded word from
// the same station within one sounding/handshake burst.
const WL_COOLDOWN_MS = 5 * 60 * 1000;
const wlLastAlert = new Map();

function checkWatchlistAlert(addr) {
  const entry = watchlist.find(w => w.addr === addr);
  if (!entry) return;
  const now = Date.now();
  if (now - (wlLastAlert.get(addr) || 0) < WL_COOLDOWN_MS) return;
  wlLastAlert.set(addr, now);
  if (entry.audible) playAlertTone();
  if (entry.visible) flashWatchlistBanner(addr);
}

function playAlertTone() {
  try {
    const ctx = new (window.AudioContext || window.webkitAudioContext)();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.frequency.value = 880;
    osc.connect(gain);
    gain.connect(ctx.destination);
    gain.gain.setValueAtTime(0.2, ctx.currentTime);
    osc.start();
    osc.stop(ctx.currentTime + 0.3);
    osc.onended = () => ctx.close();
  } catch {}
}

function flashWatchlistBanner(addr) {
  const el = document.getElementById('watchlistFlash');
  if (!el) return;
  el.textContent = '⚠ Watchlist: ' + addr + ' heard!';
  el.classList.add('flashing');
  clearTimeout(el._hideTimer);
  el._hideTimer = setTimeout(() => el.classList.remove('flashing'), 4000);
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
    case 'location_relay':
      renderLocStatus(document.getElementById('setLocEnabled').checked, e.running, e.conn_state);
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
  if (!confirm('Clear all heard-station data? This also overwrites the saved LQA file.')) return;
  bridgeSend('LQA_CLEAR', {}, () => {
    heardDeleted = new Set();
    heardStations = [];
    renderHeard();
    syncLqaFromBridge();
  });
}

function renderHeard() {
  const el = document.getElementById('heardList');
  if (!el) return;
  if (!heardStations.length) {
    el.innerHTML = '<div class="heard-empty">No stations heard yet</div>';
    return;
  }
  const sort = document.getElementById('heardSort')?.value || 'score';
  const rows = [...heardStations].sort((a, b) =>
    sort === 'age'  ? (a.ageMin || 0) - (b.ageMin || 0) :
    sort === 'addr' ? a.addr.localeCompare(b.addr) || (b.score || 0) - (a.score || 0) :
                      (b.score || 0) - (a.score || 0));
  const body = rows.map(h => {
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
  if (!isTx && e.addr) checkWatchlistAlert(e.addr);
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

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  PROPAGATION ANALYSIS  (append-only LQA history — separate from lqaEntries
//  above, which is the live overwritten snapshot. All aggregation is done
//  here client-side from the raw history array; the backend is a dumb
//  data exporter (LQA_HISTORY_LIST).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

let historyRaw = [];  // [{ts_ms, freq_hz, station, sinad_db, ber, score}]

function toggleAnalysis() {
  const page = document.getElementById('analysisPage');
  const main = document.getElementById('main');
  page.classList.toggle('hidden');
  main.classList.toggle('hidden');
  if (!page.classList.contains('hidden')) syncHistoryFromBridge();
}

function showAnalysisView(name) {
  document.querySelectorAll('.analysis-view').forEach(v => v.classList.add('hidden'));
  const view = document.getElementById('analysis' + name[0].toUpperCase() + name.slice(1));
  if (view) view.classList.remove('hidden');
  document.querySelectorAll('.subnav-btn[data-view]').forEach(b =>
    b.classList.toggle('active', b.dataset.view === name));
  if      (name === 'station') renderStationHistory();
  else if (name === 'channel') renderChannelHistory();
  else if (name === 'heatmap') renderHeatmaps();
  else if (name === 'tod')     renderTimeOfDay();
}

function syncHistoryFromBridge() {
  bridgeSend('LQA_HISTORY_LIST', { limit: 20000 }, (r) => {
    if (!r.ok) return;
    historyRaw = r.data || [];
    populateStationPicker();
    populateChannelPicker();
    const active = document.querySelector('.subnav-btn[data-view].active');
    showAnalysisView(active ? active.dataset.view : 'station');
  });
}

function doHistoryClear() {
  if (!confirm('Permanently delete all propagation history? This cannot be undone.')) return;
  bridgeSend('LQA_HISTORY_CLEAR', {}, (r) => {
    if (r.ok) { historyRaw = []; syncHistoryFromBridge(); aleLogInfo('Propagation history cleared'); }
    else aleLogInfo('History clear failed: ' + (r.error || '?'));
  });
}

function doExportHistoryCsv() {
  const header = 'timestamp_utc,freq_mhz,station,sinad_db,ber,score\n';
  const rows = historyRaw.map(s =>
    [new Date(s.ts_ms).toISOString(), fmtChFreqExact(s.freq_hz), s.station, s.sinad_db, s.ber, s.score].join(','));
  const blob = new Blob([header + rows.join('\n')], { type: 'text/csv' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'ale_monitor_lqa_history.csv';
  a.click();
  URL.revokeObjectURL(url);
}

function groupBy(rows, keyFn) {
  const m = new Map();
  for (const r of rows) {
    const k = keyFn(r);
    if (!m.has(k)) m.set(k, []);
    m.get(k).push(r);
  }
  return m;
}

function avg(rows, field) {
  if (!rows.length) return null;
  return rows.reduce((s, r) => s + r[field], 0) / rows.length;
}

function populateStationPicker() {
  const sel = document.getElementById('stationPicker');
  const prev = sel.value;
  const stations = [...new Set(historyRaw.map(s => s.station))].sort();
  sel.innerHTML = stations.map(s => `<option value="${escapeHtml(s)}">${escapeHtml(s)}</option>`).join('')
    || '<option value="">(no history yet)</option>';
  if (stations.includes(prev)) sel.value = prev;
}

function populateChannelPicker() {
  const sel = document.getElementById('channelPicker');
  const prev = sel.value;
  const freqs = [...new Set(historyRaw.map(s => s.freq_hz))].sort((a, b) => a - b);
  sel.innerHTML = freqs.map(f => `<option value="${f}">${fmtChFreqExact(f)} MHz</option>`).join('')
    || '<option value="">(no history yet)</option>';
  if (freqs.some(f => String(f) === prev)) sel.value = prev;
}

// Minimal hand-rolled line chart: static redraw (no animation — historical, not live).
function drawLineChart(canvasId, points, color, fmtY) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const w = canvas.clientWidth || 260, h = canvas.clientHeight || 120;
  canvas.width = w; canvas.height = h;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, w, h);
  if (points.length < 2) {
    ctx.fillStyle = 'rgba(200,216,232,0.4)';
    ctx.font = '11px monospace';
    ctx.fillText('not enough data', 8, h / 2);
    return;
  }
  const pad = 20;
  const xs = points.map(p => p.ts_ms), ys = points.map(p => p.y);
  const xMin = Math.min(...xs), xMax = Math.max(...xs);
  const yMin = Math.min(...ys), yMax = Math.max(...ys);
  const xr = xMax - xMin || 1, yr = yMax - yMin || 1;
  const px = t => pad + ((t - xMin) / xr) * (w - 2 * pad);
  const py = v => h - pad - ((v - yMin) / yr) * (h - 2 * pad);
  ctx.strokeStyle = 'rgba(255,255,255,0.08)';
  ctx.strokeRect(pad, pad, w - 2 * pad, h - 2 * pad);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  points.forEach((p, i) => { const x = px(p.ts_ms), y = py(p.y); i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y); });
  ctx.stroke();
  ctx.fillStyle = 'rgba(200,216,232,0.55)';
  ctx.font = '9px monospace';
  ctx.fillText(fmtY(yMax), 2, pad);
  ctx.fillText(fmtY(yMin), 2, h - pad + 9);
}

function renderStationHistory() {
  const station = document.getElementById('stationPicker').value;
  const rows = historyRaw.filter(s => s.station === station).sort((a, b) => a.ts_ms - b.ts_ms);
  drawLineChart('chartSinad', rows.map(r => ({ ts_ms: r.ts_ms, y: r.sinad_db })), '#00dc8c', v => v.toFixed(0));
  drawLineChart('chartBer',   rows.map(r => ({ ts_ms: r.ts_ms, y: r.ber })),      '#ff8a65', v => v.toFixed(0));
  drawLineChart('chartScore', rows.map(r => ({ ts_ms: r.ts_ms, y: r.score })),    '#4dc8ff', v => v.toFixed(0));

  const el = document.getElementById('soundingHistoryTable');
  if (!rows.length) { el.innerHTML = '<div class="msg-empty">No history for this station yet</div>'; return; }
  const body = [...rows].reverse().map(r =>
    `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${new Date(r.ts_ms).toLocaleString()}</td>` +
      `<td class="lqa-cell">${fmtChFreqExact(r.freq_hz)}</td>` +
      qCell(r.sinad_db.toFixed(1), r.sinad_db / 30) +
      qCell(r.ber.toFixed(1), 1 - Math.min(1, r.ber / 48)) +
      qCell(r.score.toFixed(1), r.score / 30) +
    `</tr>`
  ).join('');
  el.innerHTML =
    `<table class="ch-table"><thead><tr>` +
    `<th>Time</th><th>Freq (MHz)</th><th>SINAD</th><th>BER</th><th>Score</th>` +
    `</tr></thead><tbody>${body}</tbody></table>`;
}

function renderChannelHistory() {
  const freq = Number(document.getElementById('channelPicker').value);
  const rows = historyRaw.filter(s => s.freq_hz === freq);
  const el = document.getElementById('channelStatsTable');
  if (!rows.length) { el.innerHTML = '<div class="msg-empty">No history for this channel yet</div>'; return; }
  const byHour = groupBy(rows, r => new Date(r.ts_ms).getHours());
  let bestHour = null, bestScore = -1;
  for (const [hour, hrows] of byHour) {
    const a = avg(hrows, 'score');
    if (a > bestScore) { bestScore = a; bestHour = hour; }
  }
  const stations = new Set(rows.map(r => r.station));
  el.innerHTML =
    `<table class="ch-table"><tbody>` +
    `<tr><td class="lqa-cell" style="text-align:left">Reception count</td><td class="lqa-cell">${rows.length}</td></tr>` +
    `<tr><td class="lqa-cell" style="text-align:left">Distinct stations</td><td class="lqa-cell">${stations.size}</td></tr>` +
    `<tr><td class="lqa-cell" style="text-align:left">Avg SINAD</td><td class="lqa-cell">${avg(rows, 'sinad_db').toFixed(1)}</td></tr>` +
    `<tr><td class="lqa-cell" style="text-align:left">Avg BER</td><td class="lqa-cell">${avg(rows, 'ber').toFixed(1)}</td></tr>` +
    `<tr><td class="lqa-cell" style="text-align:left">Avg Score</td><td class="lqa-cell">${avg(rows, 'score').toFixed(1)}</td></tr>` +
    `<tr><td class="lqa-cell" style="text-align:left">Best operating hour (UTC-local)</td><td class="lqa-cell">${bestHour}:00</td></tr>` +
    `</tbody></table>`;
}

function heatCell(scoreAvg, count) {
  if (count === 0) return `<td class="heatmap-cell" style="color:var(--tx-dim)">·</td>`;
  return `<td class="heatmap-cell" style="background:${qColor(scoreAvg / 30)}22;color:${qColor(scoreAvg / 30)}" title="${count} samples">${scoreAvg.toFixed(0)}</td>`;
}

function renderHeatmaps() {
  const freqTimeEl = document.getElementById('heatmapFreqTime');
  const stationFreqEl = document.getElementById('heatmapStationFreq');
  if (!historyRaw.length) {
    freqTimeEl.innerHTML = stationFreqEl.innerHTML = '<div class="msg-empty">No history yet</div>';
    return;
  }

  // Frequency × hour-of-day
  const freqs = [...new Set(historyRaw.map(s => s.freq_hz))].sort((a, b) => a - b);
  let head = '<tr><th>Freq</th>' + Array.from({ length: 24 }, (_, h) => `<th>${h}</th>`).join('') + '</tr>';
  let body = freqs.map(f => {
    const cells = Array.from({ length: 24 }, (_, h) => {
      const rows = historyRaw.filter(s => s.freq_hz === f && new Date(s.ts_ms).getHours() === h);
      return heatCell(avg(rows, 'score') || 0, rows.length);
    }).join('');
    return `<tr><td class="lqa-cell" style="text-align:left">${fmtChFreqExact(f)}</td>${cells}</tr>`;
  }).join('');
  freqTimeEl.innerHTML = `<table class="ch-table heatmap-table"><thead>${head}</thead><tbody>${body}</tbody></table>`;

  // Station × frequency
  const stations = [...new Set(historyRaw.map(s => s.station))].sort();
  head = '<tr><th>Station</th>' + freqs.map(f => `<th>${fmtChFreqExact(f)}</th>`).join('') + '</tr>';
  body = stations.map(st => {
    const cells = freqs.map(f => {
      const rows = historyRaw.filter(s => s.station === st && s.freq_hz === f);
      return heatCell(avg(rows, 'score') || 0, rows.length);
    }).join('');
    return `<tr><td class="lqa-cell" style="text-align:left">${escapeHtml(st)}</td>${cells}</tr>`;
  }).join('');
  stationFreqEl.innerHTML = `<table class="ch-table heatmap-table"><thead>${head}</thead><tbody>${body}</tbody></table>`;
}

function renderTimeOfDay() {
  const el = document.getElementById('todTable');
  if (!historyRaw.length) { el.innerHTML = '<div class="msg-empty">No history yet</div>'; return; }
  const byHour = groupBy(historyRaw, r => new Date(r.ts_ms).getHours());
  const rows = Array.from({ length: 24 }, (_, h) => {
    const hrows = byHour.get(h) || [];
    const a = avg(hrows, 'score');
    return `<tr>` +
      `<td class="lqa-cell" style="text-align:left">${h}:00</td>` +
      `<td class="lqa-cell">${hrows.length}</td>` +
      (a == null ? `<td class="lqa-cell" style="color:var(--tx-dim)">—</td>` : qCell(a.toFixed(1), a / 30)) +
    `</tr>`;
  }).join('');
  el.innerHTML =
    `<table class="ch-table"><thead><tr><th>Hour</th><th>Samples</th><th>Avg Score</th></tr></thead>` +
    `<tbody>${rows}</tbody></table>`;
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
