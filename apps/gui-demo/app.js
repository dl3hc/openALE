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

// Generate one row of ALE FSK energy — FSK tones + noise floor
function genRow() {
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

// Placeholder: replace genRow() with WebSocket binary frames in production
// e.g.: ws.onmessage = e => { if (e.data instanceof ArrayBuffer) { const bins = new Float32Array(e.data); /* inject */ } }

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
let contacts = [
  { cs:'W1ABC', name:'Net Control',        fav:true,  status:'enabled',  net:'NET1', chans:'ALL' },
  { cs:'K2XYZ', name:'Mike — Region 4',    fav:false, status:'enabled',  net:'',     chans:'1,2' },
  { cs:'N3DEF', name:'Base Station West',  fav:false, status:'enabled',  net:'',     chans:'ALL' },
  { cs:'W4GHI', name:'Dave — Mobile',      fav:false, status:'enabled',  net:'',     chans:'ALL' },
  { cs:'K5JKL', name:'Steve — Tech Lead',  fav:true,  status:'disabled', net:'NET1', chans:'1'   },
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

function pickContact(i) { selectedContact = contacts[i]; renderContacts(); }

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
  if (editingContactIdx >= 0) contacts[editingContactIdx] = c;
  else { contacts.push(c); selectedContact = c; }
  closeContactEditor();
  renderContacts();
}

function deleteContact() {
  if (editingContactIdx >= 0) {
    if (contacts[editingContactIdx] === selectedContact) selectedContact = null;
    contacts.splice(editingContactIdx, 1);
  }
  closeContactEditor();
  renderContacts();
}

function startCall() {
  if (!selectedContact) return;
  setStatus('Calling…', 'calling');
  setTimeout(goLinked, 2200);
}

function answerCall()  { showInc(false); goLinked(); }
function declineCall() { showInc(false); goScanning(); }
function endCall()     { stopTimer(); goScanning(); }

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

// Enumerate WebAudio devices for dropdowns
async function enumDevices() {
  try {
    // Request mic permission so labels are populated
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    stream.getTracks().forEach(t => t.stop());
    const devs = await navigator.mediaDevices.enumerateDevices();
    const ins   = devs.filter(d => d.kind === 'audioinput');
    const outs  = devs.filter(d => d.kind === 'audiooutput');
    const mkOpt = d => `<option value="${d.deviceId}">${d.label || d.deviceId.slice(0,20)}</option>`;
    document.getElementById('audioIn').innerHTML  = ins.map(mkOpt).join('');
    document.getElementById('audioOut').innerHTML = outs.map(mkOpt).join('');
  } catch {
    document.getElementById('audioIn').innerHTML  = '<option>— permission denied —</option>';
    document.getElementById('audioOut').innerHTML = '<option>— permission denied —</option>';
  }
}

// Animate level meter with a demo sine wave (real backend sends real RMS)
let levelTimer = null;
function testAudio() {
  if (levelTimer) { clearInterval(levelTimer); levelTimer = null; return; }
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

// Test rig connection (stub — real impl sends CMD:STATUS to WebSocket)
function testRig() {
  const el = document.getElementById('rigConnStatus');
  el.classList.remove('hidden', 'ok', 'err');
  el.textContent = '⟳ Connecting…';
  setTimeout(() => {
    const ok = Math.random() > 0.35;
    el.classList.add(ok ? 'ok' : 'err');
    el.textContent = ok ? '✓ rigctld reachable — IC-7300 (Hamlib 5.x)' : '✗ Connection refused — is rigctld running?';
  }, 900);
}

// Channel table management — data-driven cards.
//   self : '' = use primary self address; otherwise one of the SelfAddrTable entries
//   inhCall / inhSnd : exclude this channel from outbound calling / sounding
let channels = [
  { id:1, rx:'14109000', tx:'14109000', mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'Primary',    inhCall:false, inhSnd:false },
  { id:2, rx:'7102000',  tx:'7102000',  mode:'USB', usage:'BOTH',  dir:'RX/TX', self:'', label:'40m Backup', inhCall:false, inhSnd:false },
  { id:3, rx:'3596000',  tx:'3596000',  mode:'USB', usage:'VOICE', dir:'RX/TX', self:'', label:'80m Night',  inhCall:false, inhSnd:false },
];

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
          <input class="ch-inp ch-id-inp" value="${escapeHtml(String(c.id))}" oninput="chSet(${i},'id',this.value)">
        </div>
        <div class="ch-field">
          <label>RX Hz</label>
          <input class="ch-inp" value="${escapeHtml(c.rx)}" placeholder="14000000" oninput="chSet(${i},'rx',this.value)">
        </div>
        <div class="ch-field">
          <label>TX Hz</label>
          <input class="ch-inp" value="${escapeHtml(c.tx)}" placeholder="same as RX" oninput="chSet(${i},'tx',this.value)">
        </div>
        <div class="ch-field ch-grow">
          <label>Label</label>
          <input class="ch-inp" value="${escapeHtml(c.label)}" placeholder="Description" oninput="chSet(${i},'label',this.value)">
        </div>
        <button class="ch-del" onclick="delCh(${i})" title="Delete channel">✕</button>
      </div>
      <div class="ch-card-row">
        <div class="ch-field">
          <label>Mode</label>
          <select class="ch-sel" onchange="chSet(${i},'mode',this.value)">${opts(CH_MODES, c.mode)}</select>
        </div>
        <div class="ch-field">
          <label>Usage</label>
          <select class="ch-sel" onchange="chSet(${i},'usage',this.value)">${opts(CH_USAGE, c.usage)}</select>
        </div>
        <div class="ch-field">
          <label>Direction</label>
          <select class="ch-sel" onchange="chSet(${i},'dir',this.value)">${opts(CH_DIRS, c.dir)}</select>
        </div>
        <div class="ch-field">
          <label>Self Address</label>
          <select class="ch-sel" onchange="chSet(${i},'self',this.value)">${selfAddrOpts(c.self)}</select>
        </div>
      </div>
      <div class="ch-card-inh">
        <label class="ch-check">
          <input type="checkbox" ${c.inhCall ? 'checked' : ''} onchange="chSet(${i},'inhCall',this.checked)"> Inhibit Calling
        </label>
        <label class="ch-check">
          <input type="checkbox" ${c.inhSnd ? 'checked' : ''} onchange="chSet(${i},'inhSnd',this.checked)"> Inhibit Sounding
        </label>
      </div>
    </div>`).join('');
}

// Mutate model in place — no re-render, so text inputs keep focus/caret while typing.
function chSet(i, field, val) { channels[i][field] = val; }

function addCh() {
  const nextId = channels.reduce((m, c) => Math.max(m, parseInt(c.id, 10) || 0), 0) + 1;
  channels.push({ id:nextId, rx:'', tx:'', mode:'USB', usage:'BOTH', dir:'RX/TX', self:'', label:'', inhCall:false, inhSnd:false });
  renderChannels();
  const cards = document.querySelectorAll('#chBody .ch-card');
  const inps  = cards[cards.length - 1]?.querySelectorAll('.ch-inp');
  if (inps && inps[1]) inps[1].focus();   // focus the new card's RX Hz field
}

function delCh(i) { channels.splice(i, 1); renderChannels(); }

function addNet() {
  const list = document.getElementById('netList');
  const div = document.createElement('div');
  div.style.cssText = 'display:flex;gap:8px;align-items:center';
  div.innerHTML = `<input class="finput" placeholder="NET address" style="width:140px">
    <input class="finput" placeholder="Description" style="flex:1">
    <button class="ch-del" onclick="this.closest('div').remove()">✕</button>`;
  list.appendChild(div);
  div.querySelector('.finput').focus();
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
function toggleScan() {
  // setStatus() reflects the button label/state; just flip scanning ⇄ idle.
  if (wfState === 'scanning') goIdle(); else goScanning();
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

function stepChannel(dir) {
  if (!radioChannels.length) return;
  radioChannel = (radioChannel + dir + radioChannels.length) % radioChannels.length;
  radioFreqHz = radioChannels[radioChannel].hz;
  radioMode   = radioChannels[radioChannel].mode;
  radioEntry  = '';
  updateRadioDisplay();
}

function togglePtt() {
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
  if (!radioEntry) return;
  const v = parseInt(radioEntry, 10);
  if (!isNaN(v)) { radioFreqHz = Math.min(v * unitHz, 999999999); radioChannel = -1; }
  radioEntry = '';
  updateRadioDisplay();
}
function radioEnter()    { radioCommit(1000); }          // bare ENT commits as kHz
function radioSetMode(m) { radioMode = m; updateRadioDisplay(); }
function radioSetStep(hz){ radioStep = hz; updateRadioDisplay(); }
function radioNudge(dir) {
  radioEntry  = '';
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
function sendAmd() {
  const inp = document.getElementById('msgInput');
  const txt = (inp.value || '').toUpperCase().trim();
  if (!txt) return;
  const self = primarySelfAddr();
  messages.unshift({ from:self, time:nowZulu(), text:txt, own:true });
  inp.value = '';
  renderMessages();
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
            oninput="selfAddrs[${i}].addr=this.value.toUpperCase();updateSelfHeader()"></td>
      <td>
        <select class="ch-inp" style="width:96px" onchange="selfAddrs[${i}].status=this.value;updateSelfHeader()">
          <option value="enabled"${a.status==='enabled'?' selected':''}>enabled</option>
          <option value="disabled"${a.status==='disabled'?' selected':''}>disabled</option>
        </select>
      </td>
      <td><input class="ch-inp" value="${a.chans}" oninput="selfAddrs[${i}].chans=this.value"></td>
      <td><button class="ch-del" onclick="delSelfAddr(${i})">✕</button></td>
    </tr>`).join('');
}
function addSelfAddr() { selfAddrs.push({ addr:'', status:'enabled', chans:'ALL' }); renderSelfAddrs(); renderChannels(); }
function delSelfAddr(i) { selfAddrs.splice(i, 1); renderSelfAddrs(); updateSelfHeader(); renderChannels(); }

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   LQA MATRIX  (LqaMatrix / LqaEntry — A.4.3.4)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let lqaEntries = [
  { addr:'W1ABC', ch:'14.109', score:28, sinad:24, ber:'1e-4', mp:'0.8 ms', ageMin:2  },
  { addr:'W1ABC', ch:'7.102',  score:19, sinad:16, ber:'4e-3', mp:'1.4 ms', ageMin:5  },
  { addr:'K2XYZ', ch:'14.109', score:22, sinad:19, ber:'2e-3', mp:'1.1 ms', ageMin:8  },
  { addr:'N3DEF', ch:'3.596',  score:11, sinad:9,  ber:'2e-2', mp:'2.9 ms', ageMin:14 },
];
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
renderLqa();
updateClock();
setInterval(updateClock, 1000);
updateRadioDisplay();  // VFO display + mode/step highlight
goScanning();          // boot in scanning state
updateSelfHeader();
