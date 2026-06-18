/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   BRIDGE CONNECTION  (apps/ale_bridge.cpp — WebSocket ↔ ALEController)

   When connected, every list/mutation below talks to a real station via
   bridgeSend(); when not (bridge not running), each call site falls back
   to the original local-only demo behaviour, so apps/gui/ still works
   standalone for UI work. apps/gui-demo/ is the frozen, always-mock copy.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let bridgeWs        = null;
let bridgeConnected = false;
let bridgeReqId      = 0;
const bridgePending  = new Map();   // id -> callback(reply)
let latestSpectrum   = null;        // Float32Array(257) from the last binary frame, or null

function bridgeWsUrl() {
  const port = document.getElementById('cfgWsPort')?.value || '8765';
  return 'ws://localhost:' + port;
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
    pushLog([['data', 'Bridge connected — syncing live station state']], '');
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

  ws.onclose = ws.onerror = () => {
    if (bridgeConnected) pushLog([['data', 'Bridge disconnected — back to local demo mode']], '');
    bridgeConnected = false;
    bridgeWs = null;
    bridgePending.clear();
    applyRigState(false);  // no bridge → demo mode: radio controls live again (mock)
    setTimeout(connectBridge, 3000);  // keep retrying quietly — bridge may start later
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
}

// No dedicated push event exists for VFO/PTT/audio-level changes (they're
// either operator-driven from this same GUI, or — for PTT during a call —
// fast-changing enough that polling beats adding a new event type for now).
// Light poll only while connected.
setInterval(() => { if (bridgeConnected) { syncVfoFromBridge(); pollRigStatus(); } }, 2000);

function applyStatusReply(r) {
  if (!r.ok) return;
  applyBridgeState(r.state);
}

// Map ALEState (Core) -> existing demo UI transitions. HANDSHAKE/SOUNDING
// are transient technical states with no dedicated UI of their own here;
// LINKED is driven by the link_established event instead (it carries the
// peer address, which bare state doesn't).
function applyBridgeState(state) {
  if (state === 'IDLE') goIdle();
  else if (state === 'SCANNING') goScanning();
  else if (state === 'CALLING') setStatus('Calling…', 'calling');
  else if (state === 'HANDSHAKE') setStatus('Handshake…', 'calling');
}

function onBridgeEvent(e) {
  switch (e.event) {
    case 'state': applyBridgeState(e.value); break;
    case 'status': pushLog([['data', e.msg]], ''); break;
    case 'call_received':
      stopTimer();
      setStatus('Incoming', 'incoming');
      document.getElementById('incCs').textContent   = e.caller;
      document.getElementById('incName').textContent = '';
      showInc(true);
      showCallPanel(false);
      break;
    case 'link_established':
      stopTimer();
      setStatus('Linked', 'linked');
      document.getElementById('callCs').textContent = e.peer;
      showInc(false);
      showCallPanel(true);
      callStart = Date.now();
      timerId   = setInterval(tickTimer, 1000);
      tickTimer();
      setSyncChip(true);
      break;
    case 'link_terminated':
      pushLog([['data', 'Link terminated: ' + e.reason]], '');
      stopTimer();
      goScanning();
      break;
    case 'amd_received':
      messages.unshift({ from: e.from, time: nowZulu(), text: e.text, own: false });
      renderMessages();
      break;
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
const TONES  = [750, 1000, 1250, 1500, 1750, 2000, 2250, 2500]; // ALE 8-FSK tones
const BW_LO  = 0, BW_HI = 4000;       // FFT window = 0…Nyquist (8 kHz sample rate)
const ALE_LO = 750, ALE_HI = 2500;    // ALE 8-FSK sub-band within the window
const ALE_GUARD = 125;                // frame padding beyond the edge tones (Hz)
const AXIS_MAJOR = [0, 1000, 2000, 3000, 4000];  // labelled gridlines (kHz)
const AXIS_MINOR = [500, 1500, 2500, 3500];      // unlabelled gridlines

let rows = [];
let wfState  = 'scanning';  // drives signal simulation
let toneTick = 0;
let toneIdx  = 0;
let frameCnt = 0;

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

// Real spectrum (bridge connected): 257 bins, 0–4000 Hz, 15.625 Hz/bin
// (ALEController::set_spectrum_callback() doc) resampled onto the canvas
// width. Falls back to the synthetic demo row otherwise.
function genRowFromSpectrum(spec) {
  const W = canvas.width || 1;
  const row = new Float32Array(W);
  // Magnitude bins aren't already 0-1 energy — normalise against a running
  // sense of "loud" so the display doesn't depend on absolute FFT scale.
  let maxv = 1e-6;
  for (let i = 0; i < spec.length; i++) maxv = Math.max(maxv, spec[i]);
  for (let x = 0; x < W; x++) {
    const hz  = BW_LO + (x / (W - 1)) * (BW_HI - BW_LO);
    const bin = Math.min(spec.length - 1, Math.round((hz / 4000) * (spec.length - 1)));
    row[x] = Math.min(1, spec[bin] / maxv);
  }
  return row;
}

// Generate one row of ALE FSK energy — FSK tones + noise floor (demo mode)
function genRow() {
  if (bridgeConnected && latestSpectrum) return genRowFromSpectrum(latestSpectrum);

  const W   = canvas.width || 1;
  const row = new Float32Array(W);
  for (let i = 0; i < W; i++)
    row[i] = Math.random() * 0.055 + 0.015;

  const active = wfState === 'scanning' || wfState === 'calling' || wfState === 'linked';
  if (active) {
    frameCnt++;
    if (frameCnt >= 7) {           // symbol period ≈ 7 animation frames
      frameCnt = 0;
      toneIdx = Math.floor(Math.random() * TONES.length);
    }
    const hz  = TONES[toneIdx];
    const pos = (hz - BW_LO) / (BW_HI - BW_LO);
    const cx  = Math.round(pos * (W - 1));
    const amp = wfState === 'linked' ? 0.88 : 0.65;
    const sp  = W * 0.020;
    for (let x = 0; x < W; x++) {
      const d = x - cx;
      row[x] = Math.max(row[x], amp * Math.exp(-d * d / (2 * sp * sp)) + Math.random() * 0.04);
    }
  }
  return row;
}

// Map linear energy 0-1 → RGB: dark-blue → teal → green → bright-green
function energy2rgb(v) {
  v = Math.max(0, Math.min(1, v));
  if (v < 0.28) {
    const t = v / 0.28;
    return [0, Math.round(18 + t * 70), Math.round(35 + t * 75)];
  } else if (v < 0.58) {
    const t = (v - 0.28) / 0.30;
    return [0, Math.round(88 + t * 130), Math.round(110 - t * 25)];
  } else {
    const t = (v - 0.58) / 0.42;
    return [Math.round(t * 170), Math.round(218 + t * 37), Math.round(85 + t * 105)];
  }
}

function drawWaterfall() {
  const W = canvas.width, H = canvas.height;
  if (W < 4 || H < 4) { requestAnimationFrame(drawWaterfall); return; }

  rows.unshift(genRow());
  if (rows.length > H) rows.length = H;

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
  requestAnimationFrame(drawWaterfall);
}
drawWaterfall();

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   PROTOCOL LOG
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const logEntries = [
  { t:'14:21:05', w:[['to','TO:W1A'],['data','DATA:BC@'],['tis','TIS:SAM']], r:'linked'  },
  { t:'14:20:51', w:[['to','TO:W1A'],['data','DATA:BC@']],                   r:'miss'    },
  { t:'14:19:33', w:[['tis','TIS:K2X'],['data','DATA:YZ@']],                 r:'miss'    },
  { t:'14:18:10', w:[['to','TO:BOB'],['tis','TIS:W4G'],['data','DATA:HI@']], r:'linked'  },
];

function renderLog() {
  const el = document.getElementById('protoLog');
  if (logEntries.length > 250) logEntries.length = 250;   // cap history
  el.innerHTML = logEntries.map(e => {
    const pills = e.w.map(([cls, lbl]) =>
      `<span class="pill pill-${cls}">${lbl}</span>`).join('');
    const rc = e.r === 'linked' ? 'linked' : e.r === 'calling' ? 'calling' : 'miss';
    const rt = e.r === 'linked' ? 'LINKED' : e.r === 'calling' ? 'CALLING' : '—';
    return `<div class="proto-row">
      <span class="ptime">${e.t}</span>
      <span class="pwords">${pills}</span>
      <span class="presult ${rc}">${rt}</span>
    </div>`;
  }).join('');
}
renderLog();

function pushLog(words, result) {
  const now = new Date().toTimeString().slice(0, 8);
  logEntries.unshift({ t: now, w: words, r: result });
  renderLog();
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

function setSyncChip(locked) {
  const dot = document.getElementById('syncDot');
  const lbl = document.getElementById('syncLbl');
  dot.style.background = locked ? 'var(--s-linked)' : 'var(--s-idle)';
  lbl.style.color      = locked ? 'var(--s-linked)' : 'var(--s-idle)';
  lbl.textContent      = locked ? 'LOCK' : 'SCAN';
}

function stopTimer() {
  if (timerId) clearInterval(timerId);
  timerId = null;
  callStart = null;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   DEMO STATE MACHINE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function goIdle() {
  stopTimer();
  setStatus('Idle', 'idle');
  showInc(false);
  showCallPanel(false);
  setSyncChip(false);
}

function goScanning() {
  stopTimer();
  setStatus('Scanning', 'scanning');
  showInc(false);
  showCallPanel(false);
  setSyncChip(true);
}

function goIncoming() {
  stopTimer();
  setStatus('Incoming', 'incoming');
  document.getElementById('incCs').textContent   = 'W1ABC';
  document.getElementById('incName').textContent = 'Net Control Station';
  showInc(true);
  showCallPanel(false);
  pushLog([['to','TO:SAM'],['data','DATA:@@'],['tis','TIS:W1A']], 'calling');
}

function goLinked() {
  stopTimer();
  const cs = selectedContact?.cs || 'W1ABC';
  setStatus('Linked', 'linked');
  document.getElementById('callCs').textContent = cs;
  showInc(false);
  showCallPanel(true);
  callStart = Date.now();
  timerId   = setInterval(tickTimer, 1000);
  tickTimer();
  setSyncChip(true);
  pushLog([['to','TO:'+cs.slice(0,3)],['data','DATA:'+cs.slice(3,5)+'@'],['tis','TIS:'+primarySelfAddr().slice(0,3)]], 'linked');
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CONTACTS / ADDRESS BOOK  (OtherAddr* — A.4.3.4)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
// chans: comma-separated Core channel ids ("C-1,C-2") or "ALL" — matches
// ALEController::add_contact()'s valid_channels format.
let contacts = [
  { cs:'W1ABC', name:'Net Control',        fav:true,  status:'enabled',  net:'NET1', chans:'ALL' },
  { cs:'K2XYZ', name:'Mike — Region 4',    fav:false, status:'enabled',  net:'',     chans:'C-1,C-2' },
  { cs:'N3DEF', name:'Base Station West',  fav:false, status:'enabled',  net:'',     chans:'ALL' },
  { cs:'W4GHI', name:'Dave — Mobile',      fav:false, status:'enabled',  net:'',     chans:'ALL' },
  { cs:'K5JKL', name:'Steve — Tech Lead',  fav:true,  status:'disabled', net:'NET1', chans:'C-1'   },
];
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

  document.querySelector('.btn-call').disabled = !selectedContact;

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

function startCall() {
  if (!selectedContact) return;
  if (bridgeConnected) {
    setStatus('Calling…', 'calling');   // cosmetic; real transition comes from CALLING/link_established
    bridgeSend('CALL', { addr: selectedContact.cs });
    return;
  }
  setStatus('Calling…', 'calling');
  setTimeout(goLinked, 2200);
}

// answerCall()/accept_call() is only meaningful with manual-accept mode on
// (ALEController::set_manual_accept_mode()) — by default the SM already
// auto-accepts, so the panel here is mostly "dismiss"; the ACCEPT command
// is still sent so it's correct once manual-accept mode is enabled.
function answerCall() {
  showInc(false);
  if (bridgeConnected) { bridgeSend('ACCEPT', {}); return; }
  goLinked();
}
function declineCall() {
  showInc(false);
  if (bridgeConnected) { bridgeSend('REJECT', {}); return; }
  goScanning();
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

// Populate the RX/TX device dropdowns. Connected: real WASAPI devices from the
// bridge's AUDIO_DEVICES (the option value is the bare name the bridge's
// AudioDevice::open() substring-matches — "IN: "/"OUT: " prefix stripped).
// Not connected: WebAudio enumeration as a demo placeholder.
// Remember the operator's device choice so reopening Settings (which rebuilds
// the <option> lists from scratch) doesn't reset it to the first entry.
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
    if (!r.ok) pushLog([['data', 'Audio open failed: ' + (r.error || '?')]], 'miss');
  });
}

// Level meter: real RMS from the bridge (AUDIO_LEVEL) when connected, else a
// demo sine. Toggle on/off.
let levelTimer = null;
function testAudio() {
  if (levelTimer) { clearInterval(levelTimer); levelTimer = null; return; }
  if (bridgeConnected) {
    levelTimer = setInterval(() => {
      bridgeSend('AUDIO_LEVEL', {}, (r) => {
        if (r.ok) document.getElementById('levelBarIn').style.width = Math.round(Math.min(1, r.level) * 100) + '%';
      });
    }, 120);
    return;
  }
  let phase = 0;
  levelTimer = setInterval(() => {
    phase += 0.18;
    const pct = Math.max(5, Math.round(45 + 38 * Math.sin(phase) + Math.random() * 10));
    document.getElementById('levelBarIn').style.width = pct + '%';
  }, 60);
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
// radio-control UI. Controls stay live in pure demo mode (no bridge) so the
// standalone mock keeps working; once bridged, they require a real link.
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
        pushLog([['data', 'Radio connection lost — controls locked']], 'miss');
      else if (connected)
        pushLog([['data', 'Radio connected']], '');
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
function saveSettings() {
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
  // Mirror the SM: send_sounding() is only honoured in IDLE/SCANNING. Grey out
  // while calling/handshaking (wfState 'calling'), incoming, or linked.
  const ok = wfState === 'idle' || wfState === 'scanning';
  b.disabled = !ok;
  b.title = ok ? 'Transmit a manual sounding (LQA probe) on the current channel'
               : 'Sounding nur im Idle/Scan möglich';
}

function toggleScan() {
  // Stopping (currently scanning) is always allowed; only *starting* needs >=2.
  if (wfState !== 'scanning' && !scanEnabled()) {
    pushLog([['data', 'Scanning braucht ≥2 Kanäle']], 'miss');
    return;
  }
  if (bridgeConnected) { bridgeSend(wfState === 'scanning' ? 'AVAILABLE' : 'SCAN', {}); return; }
  // setStatus() reflects the button label/state; just flip scanning ⇄ idle.
  if (wfState === 'scanning') goIdle(); else goScanning();
}

// Manual sounding — fire a one-shot LQA probe on the current channel. The SM
// only honours SOUNDING_REQUEST while IDLE or SCANNING; the bridge reply's
// `ok:false` surfaces a rejection (e.g. while linked/handshaking).
function manualSound() {
  if (bridgeConnected) {
    bridgeSend('SOUND', {}, (r) => {
      if (r && r.ok) pushLog([['tis', 'TIS:'+primarySelfAddr().slice(0,3)], ['data', 'SOUNDING']], '');
      else           pushLog([['data', 'Sounding abgelehnt — nur im Idle/Scan']], 'miss');
    });
    return;
  }
  pushLog([['tis', 'TIS:'+primarySelfAddr().slice(0,3)], ['data', 'SOUNDING']], '');
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   DEMO PANEL COLLAPSE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function toggleDemo() {
  const p = document.getElementById('demoPanel');
  const collapsed = p.classList.toggle('collapsed');
  document.getElementById('demoToggle').textContent = collapsed ? 'DEV ▸' : 'DEV ▾';
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
  const fs = document.getElementById('freqSub');     if (fs) fs.textContent = (radioChannel >= 0 ? 'CH ' + (radioChannel + 1) : 'VFO') + ' · ' + radioMode;
  const cf = document.getElementById('callFreqLbl');  if (cf) cf.textContent = fmtStatFreq(radioFreqHz) + ' MHz · ' + radioMode;
  document.querySelectorAll('.rk-mode').forEach(b => b.classList.toggle('active', b.dataset.mode === radioMode));
  document.querySelectorAll('.rk-step').forEach(b => b.classList.toggle('active', +b.dataset.step === radioStep));
}

function toggleRadioPanel() {
  const open = document.getElementById('radioPanel').classList.toggle('open');
  document.getElementById('radioToggle').textContent = open ? '📻 Radio ▾' : '📻 Radio ▸';
  if (open) updateRadioDisplay();
}
// dismiss the VFO panel on an outside click
document.addEventListener('click', e => {
  const wrap = document.getElementById('radioWrap');
  const panel = document.getElementById('radioPanel');
  if (panel && panel.classList.contains('open') && wrap && !wrap.contains(e.target)) {
    panel.classList.remove('open');
    document.getElementById('radioToggle').textContent = '📻 Radio ▸';
  }
});

// Pull real freq/mode/tune-step/PTT from the bridge (ALEController::
// get_current_channel/frequency/mode/get_tune_step/get_ptt_state, all real
// IRadio passthrough — see Core/include/App/ale_controller.h) and reflect
// them onto the same display fields the demo VFO uses.
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

// Manual voice PTT has no Core counterpart (ALE's own PTT is automatic,
// driven by the protocol's RX-enable signal — see ALEController::
// get_ptt_state()'s doc). Left cosmetic-only even when connected; the
// indicator still reflects the REAL state via syncVfoFromBridge().
function togglePtt() {
  if (radioCtrlLocked()) return;
  pttOn = !pttOn;
  const b = document.getElementById('pttBtn');
  b.classList.toggle('ptt-on', pttOn);
  b.textContent = pttOn ? '● TX' : '🎙 PTT';
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
let messages = [
  { from:'W1ABC', time:'14:21Z', text:'500 GALLONS WATER 1000 MRE TO SHELTER 5', own:false },
  { from:'K2XYZ', time:'14:19Z', text:'CHECK IN COMPLETE ALL STATIONS NOMINAL',  own:false },
  { from:'N3DEF', time:'13:45Z', text:'NET CONTROL CHANGE AT 1500Z',             own:false },
  { from:'W4GHI', time:'13:32Z', text:'ROGER STANDING BY ON PRIMARY',            own:false },
];
function renderMessages() {
  const el = document.getElementById('msgList');
  if (!messages.length) { el.innerHTML = '<div class="msg-empty">No messages</div>'; return; }
  el.innerHTML = '<div class="msg-list">' + messages.map((m, i) => `
    <div class="msg-item${m.own?' msg-own':''}">
      <button class="msg-del" title="Delete" onclick="deleteMessage(${i})">✕</button>
      <div class="msg-hdr">
        <span class="msg-from">${m.from}${m.own?' →':''}</span>
        <span class="msg-time">${m.time}</span>
      </div>
      <div class="msg-text">${escapeHtml(m.text)}</div>
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
  pushLog([['to','TO:'+to],['data','DATA:AMD'],['tis','TIS:'+self.slice(0,3)]], 'linked');
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
let lqaEntries = [
  { addr:'W1ABC', ch:'14.109', score:28, sinad:24, ber:'1e-4', mp:'0.8 ms', ageMin:2  },
  { addr:'W1ABC', ch:'7.102',  score:19, sinad:16, ber:'4e-3', mp:'1.4 ms', ageMin:5  },
  { addr:'K2XYZ', ch:'14.109', score:22, sinad:19, ber:'2e-3', mp:'1.1 ms', ageMin:8  },
  { addr:'N3DEF', ch:'3.596',  score:11, sinad:9,  ber:'2e-2', mp:'2.9 ms', ageMin:14 },
];
// Bridge's LQA_LIST format (freq_hz/station/snr_db/ber/sinad_db/score/age_ms
// — see ALEController::get_all_lqa_entries()) has no multipath field; "mp"
// shows "—" (genuinely not in the data) rather than a guessed value.
function syncLqaFromBridge() {
  bridgeSend('LQA_LIST', {}, (r) => {
    if (!r.ok) return;
    lqaEntries = r.data.map(e => ({
      addr: e.station || '(sounding)', ch: (e.freq_hz / 1e6).toFixed(3),
      score: Math.round(e.score), sinad: Math.round(e.sinad_db),
      ber: e.ber.toExponential(0), mp: '—', ageMin: Math.round(e.age_ms / 60000),
    }));
    renderLqa();
  });
}
// LQA changes from real radio activity (soundings/contacts), not GUI
// actions — periodic poll, same reasoning as the VFO poll above.
setInterval(() => { if (bridgeConnected) syncLqaFromBridge(); }, 5000);

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
      <td class="lqa-cell" style="text-align:left">${e.addr}</td>
      <td class="lqa-cell">${e.ch}</td>
      <td class="lqa-cell ${lqaClass(e.score)}">${e.score}</td>
      <td class="lqa-cell">${e.sinad} dB</td>
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
connectBridge();        // apps/ale_bridge.cpp — falls back to demo mode if not running
