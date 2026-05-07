#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

// ─── Pin Configuration ────────────────────────────────────────────────────────
#define IR_RECV_PIN   D3
#define IR_SEND_PIN   D8
#define LED_PIN       LED_BUILTIN   // NodeMCU built-in LED (active LOW)

// ─── IR ───────────────────────────────────────────────────────────────────────
#define MAX_RAW_LEN   200

IRrecv irrecv(IR_RECV_PIN, 1024, 50, true);
IRsend irsend(IR_SEND_PIN);

// ─── Wi-Fi / Web server ───────────────────────────────────────────────────────
const char* AP_SSID = "IR-Remote";
const char* AP_PASS = "12345678";

ESP8266WebServer server(80);

// ─── Helpers ──────────────────────────────────────────────────────────────────
void blinkLED(int times = 2) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(80);
    digitalWrite(LED_PIN, HIGH);
    delay(80);
  }
}

// Add CORS + JSON headers for every response
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJSON(int code, String body) {
  addCORSHeaders();
  server.send(code, "application/json", body);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Embedded Web Portal (PROGMEM)
// ─────────────────────────────────────────────────────────────────────────────
static const char PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>IR Remote</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Syne+Mono&family=Syne:wght@700;800&display=swap" rel="stylesheet">
<style>
  :root {
    --bg: #05080d;
    --surface: #0c1219;
    --surface2: #111c28;
    --border: #1a2d42;
    --accent: #00e5ff;
    --accent-dim: rgba(0,229,255,0.12);
    --orange: #ff7043;
    --green: #00e676;
    --red: #ff1744;
    --text: #d0e4f0;
    --muted: #4a6880;
    --glow: 0 0 20px rgba(0,229,255,0.25);
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    font-family: 'Syne Mono', monospace;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
  }

  /* Scanline overlay */
  body::after {
    content: '';
    position: fixed;
    inset: 0;
    background: repeating-linear-gradient(
      0deg,
      transparent,
      transparent 2px,
      rgba(0,0,0,0.03) 2px,
      rgba(0,0,0,0.03) 4px
    );
    pointer-events: none;
    z-index: 9999;
  }

  /* ── Header ── */
  header {
    padding: 28px 24px 20px;
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    gap: 16px;
    position: relative;
  }

  .logo-icon {
    width: 44px; height: 44px;
    border: 2px solid var(--accent);
    border-radius: 50%;
    display: flex; align-items: center; justify-content: center;
    box-shadow: var(--glow);
    flex-shrink: 0;
    animation: pulse 3s ease-in-out infinite;
  }

  @keyframes pulse {
    0%, 100% { box-shadow: 0 0 12px rgba(0,229,255,0.25); }
    50%       { box-shadow: 0 0 28px rgba(0,229,255,0.55); }
  }

  .logo-icon svg { width: 22px; height: 22px; fill: var(--accent); }

  .header-text h1 {
    font-family: 'Syne', sans-serif;
    font-weight: 800;
    font-size: 1.25rem;
    letter-spacing: 0.12em;
    color: var(--accent);
    text-transform: uppercase;
  }

  .header-text p {
    font-size: 0.65rem;
    color: var(--muted);
    letter-spacing: 0.1em;
    margin-top: 2px;
  }

  .ip-badge {
    margin-left: auto;
    font-size: 0.65rem;
    color: var(--muted);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 10px;
    letter-spacing: 0.05em;
  }

  /* ── Layout ── */
  .container {
    max-width: 900px;
    margin: 0 auto;
    padding: 24px 16px 60px;
  }

  /* ── Capture Panel ── */
  .capture-panel {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 24px;
    margin-bottom: 28px;
    position: relative;
    overflow: hidden;
  }

  .capture-panel::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
  }

  .section-title {
    font-family: 'Syne', sans-serif;
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.2em;
    color: var(--muted);
    text-transform: uppercase;
    margin-bottom: 18px;
  }

  .capture-controls {
    display: flex;
    gap: 12px;
    align-items: flex-end;
    flex-wrap: wrap;
  }

  .field {
    flex: 1;
    min-width: 140px;
  }

  .field label {
    display: block;
    font-size: 0.62rem;
    color: var(--muted);
    letter-spacing: 0.1em;
    margin-bottom: 6px;
    text-transform: uppercase;
  }

  input[type=text], select {
    width: 100%;
    background: var(--surface2);
    border: 1px solid var(--border);
    border-radius: 8px;
    color: var(--text);
    font-family: 'Syne Mono', monospace;
    font-size: 0.78rem;
    padding: 10px 14px;
    outline: none;
    transition: border-color 0.2s, box-shadow 0.2s;
  }

  input[type=text]:focus, select:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px rgba(0,229,255,0.08);
  }

  select option { background: var(--surface2); }

  /* ── Buttons ── */
  .btn {
    font-family: 'Syne Mono', monospace;
    font-size: 0.72rem;
    letter-spacing: 0.08em;
    border: none;
    border-radius: 8px;
    padding: 11px 20px;
    cursor: pointer;
    transition: all 0.15s;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    white-space: nowrap;
  }

  .btn:active { transform: scale(0.96); }

  .btn-capture {
    background: var(--accent);
    color: #000;
    font-weight: 700;
    box-shadow: 0 0 16px rgba(0,229,255,0.3);
  }
  .btn-capture:hover { box-shadow: 0 0 24px rgba(0,229,255,0.5); }
  .btn-capture:disabled { opacity: 0.4; cursor: not-allowed; transform: none; }

  .btn-send {
    background: var(--orange);
    color: #000;
    box-shadow: 0 0 12px rgba(255,112,67,0.3);
  }
  .btn-send:hover { box-shadow: 0 0 20px rgba(255,112,67,0.5); }
  .btn-send:disabled { opacity: 0.4; cursor: not-allowed; transform: none; }

  .btn-delete {
    background: transparent;
    color: var(--red);
    border: 1px solid var(--red);
    padding: 7px 12px;
    font-size: 0.65rem;
  }
  .btn-delete:hover { background: rgba(255,23,68,0.1); }

  .btn-save-raw {
    background: transparent;
    color: var(--green);
    border: 1px solid var(--green);
    font-size: 0.65rem;
  }
  .btn-save-raw:hover { background: rgba(0,230,118,0.1); }

  /* ── Status bar ── */
  .status-bar {
    margin-top: 16px;
    padding: 10px 14px;
    border-radius: 8px;
    font-size: 0.72rem;
    display: flex;
    align-items: center;
    gap: 10px;
    min-height: 42px;
    transition: all 0.3s;
  }

  .status-bar.idle { background: var(--surface2); border: 1px solid var(--border); color: var(--muted); }
  .status-bar.listening { background: rgba(0,229,255,0.07); border: 1px solid var(--accent); color: var(--accent); }
  .status-bar.ok { background: rgba(0,230,118,0.07); border: 1px solid var(--green); color: var(--green); }
  .status-bar.err { background: rgba(255,23,68,0.07); border: 1px solid var(--red); color: var(--red); }

  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .idle .status-dot     { background: var(--muted); }
  .listening .status-dot { background: var(--accent); animation: blink 0.6s ease infinite alternate; }
  .ok .status-dot       { background: var(--green); }
  .err .status-dot      { background: var(--red); }

  @keyframes blink { from { opacity: 1; } to { opacity: 0.2; } }

  /* ── Raw preview ── */
  .raw-preview {
    margin-top: 14px;
    display: none;
  }

  .raw-preview label {
    font-size: 0.62rem;
    color: var(--muted);
    letter-spacing: 0.1em;
    text-transform: uppercase;
    display: block;
    margin-bottom: 6px;
  }

  textarea.raw-data {
    width: 100%;
    background: #020508;
    border: 1px solid var(--border);
    border-radius: 8px;
    color: #ff9800;
    font-family: 'Syne Mono', monospace;
    font-size: 0.65rem;
    padding: 10px 14px;
    min-height: 72px;
    resize: vertical;
    outline: none;
    line-height: 1.6;
    transition: border-color 0.2s;
  }
  textarea.raw-data:focus { border-color: var(--orange); }

  .raw-actions {
    display: flex;
    gap: 8px;
    margin-top: 8px;
  }

  /* ── Slots grid ── */
  .slots-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
  }

  .slot-count {
    font-size: 0.65rem;
    color: var(--muted);
    letter-spacing: 0.08em;
  }

  .slots-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 12px;
  }

  .slot-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px;
    transition: border-color 0.2s, transform 0.15s;
    cursor: default;
    position: relative;
  }

  .slot-card:hover { border-color: #2a4060; transform: translateY(-1px); }

  .slot-card-header {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 12px;
  }

  .slot-num {
    font-family: 'Syne', sans-serif;
    font-size: 0.62rem;
    font-weight: 800;
    color: var(--bg);
    background: var(--orange);
    width: 22px; height: 22px;
    border-radius: 50%;
    display: flex; align-items: center; justify-content: center;
    flex-shrink: 0;
  }

  .slot-name {
    font-size: 0.82rem;
    color: var(--text);
    flex: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .slot-len {
    font-size: 0.6rem;
    color: var(--muted);
    letter-spacing: 0.06em;
  }

  .slot-raw {
    font-size: 0.58rem;
    color: var(--muted);
    background: #020508;
    border-radius: 6px;
    padding: 6px 10px;
    margin-bottom: 12px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    border: 1px solid #0e1a26;
    letter-spacing: 0.03em;
  }

  .slot-actions {
    display: flex;
    gap: 8px;
  }

  .slot-actions .btn-send { flex: 1; justify-content: center; font-size: 0.68rem; padding: 8px 12px; }
  .slot-actions .btn-delete { flex-shrink: 0; }

  /* ── Empty state ── */
  .empty-state {
    text-align: center;
    padding: 60px 20px;
    color: var(--muted);
    font-size: 0.75rem;
    letter-spacing: 0.08em;
    grid-column: 1 / -1;
  }

  .empty-state .icon { font-size: 2.5rem; margin-bottom: 14px; opacity: 0.3; }

  /* ── Toast ── */
  #toast {
    position: fixed;
    bottom: 28px;
    right: 24px;
    padding: 12px 20px;
    border-radius: 8px;
    font-size: 0.72rem;
    letter-spacing: 0.06em;
    z-index: 1000;
    transform: translateY(80px);
    opacity: 0;
    transition: all 0.3s cubic-bezier(0.34,1.56,0.64,1);
    pointer-events: none;
    max-width: 320px;
  }
  #toast.show { transform: translateY(0); opacity: 1; }
  #toast.ok  { background: #0a2018; border: 1px solid var(--green); color: var(--green); }
  #toast.err { background: #200a0e; border: 1px solid var(--red); color: var(--red); }

  @media (max-width: 540px) {
    .capture-controls { flex-direction: column; }
    .field { min-width: 100%; }
    .btn-capture { width: 100%; justify-content: center; }
  }
</style>
</head>
<body>

<header>
  <div class="logo-icon">
    <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
      <path d="M12 1a1 1 0 011 1v2a1 1 0 01-2 0V2a1 1 0 011-1zm0 17a1 1 0 011 1v2a1 1 0 01-2 0v-2a1 1 0 011-1zM4.22 4.22a1 1 0 011.42 0l1.41 1.42a1 1 0 01-1.41 1.41L4.22 5.64a1 1 0 010-1.42zM17 17l1.41 1.42a1 1 0 01-1.41 1.41L15.59 18.4A1 1 0 0117 17zM1 12a1 1 0 011-1h2a1 1 0 010 2H2a1 1 0 01-1-1zm19 0a1 1 0 011-1h2a1 1 0 010 2h-2a1 1 0 01-1-1zM5.64 17l-1.42 1.41a1 1 0 01-1.41-1.41L4.22 15.6A1 1 0 015.64 17zm12.72-12.78a1 1 0 010 1.42l-1.42 1.41a1 1 0 01-1.41-1.41l1.41-1.42a1 1 0 011.42 0zM12 7a5 5 0 100 10A5 5 0 0012 7zm0 2a3 3 0 110 6 3 3 0 010-6z"/>
    </svg>
  </div>
  <div class="header-text">
    <h1>IR Remote</h1>
    <p>NodeMCU &middot; Raw IR &middot; SoftAP</p>
  </div>
  <div class="ip-badge">192.168.4.1</div>
</header>

<div class="container">

  <!-- Capture Panel -->
  <div class="capture-panel">
    <div class="section-title">&#9711;&nbsp; Capture &amp; Send</div>

    <div class="capture-controls">
      <div class="field">
        <label>Signal Name</label>
        <input type="text" id="capName" placeholder="e.g. TV Power" maxlength="32">
      </div>
      <div class="field" style="max-width:110px">
        <label>Save to Slot</label>
        <select id="capSlot">
          <option value="-1">— none —</option>
          <option value="0">Slot 1</option>
          <option value="1">Slot 2</option>
          <option value="2">Slot 3</option>
          <option value="3">Slot 4</option>
          <option value="4">Slot 5</option>
          <option value="5">Slot 6</option>
          <option value="6">Slot 7</option>
          <option value="7">Slot 8</option>
          <option value="8">Slot 9</option>
          <option value="9">Slot 10</option>
        </select>
      </div>
      <button class="btn btn-capture" id="btnCapture" onclick="startCapture()">
        <span>&#11044;</span> CAPTURE
      </button>
    </div>

    <div class="status-bar idle" id="statusBar">
      <div class="status-dot"></div>
      <span id="statusText">Ready — point remote at sensor and press CAPTURE</span>
    </div>

    <div class="raw-preview" id="rawPreview">
      <label>Captured Raw Data (µs)</label>
      <textarea class="raw-data" id="rawTextarea" rows="3" placeholder="Raw IR values will appear here…"></textarea>
      <div class="raw-actions">
        <button class="btn btn-save-raw" onclick="saveFromPreview()">&#10003; Save to Slot</button>
        <button class="btn btn-send" onclick="sendFromPreview()"><span>&#9654;</span> Send Now</button>
        <button class="btn btn-delete" onclick="clearPreview()">&#10005; Clear</button>
      </div>
    </div>
  </div>

  <!-- Saved Slots -->
  <div>
    <div class="slots-header">
      <div class="section-title" style="margin:0">&#9632;&nbsp; Saved Slots</div>
      <div class="slot-count" id="slotCount">0 / 10</div>
    </div>
    <div class="slots-grid" id="slotsGrid"></div>
  </div>

</div>

<div id="toast"></div>

<script>
const STORAGE_KEY = 'ir_remote_slots';
const MAX_SLOTS = 10;

// ── Storage ──────────────────────────────────────────────────────────────────
function loadSlots() {
  try { return JSON.parse(localStorage.getItem(STORAGE_KEY)) || []; }
  catch(e) { return []; }
}

function saveSlots(slots) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(slots.slice(0, MAX_SLOTS)));
}

// ── Toast ────────────────────────────────────────────────────────────────────
let toastTimer;
function toast(msg, type = 'ok') {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className = 'show ' + type;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.className = '', 2800);
}

// ── Status bar ───────────────────────────────────────────────────────────────
function setStatus(state, msg) {
  const bar  = document.getElementById('statusBar');
  const text = document.getElementById('statusText');
  bar.className  = 'status-bar ' + state;
  text.textContent = msg;
}

// ── Render slots ─────────────────────────────────────────────────────────────
function renderSlots() {
  const slots = loadSlots();
  const grid  = document.getElementById('slotsGrid');
  document.getElementById('slotCount').textContent = slots.length + ' / ' + MAX_SLOTS;

  if (slots.length === 0) {
    grid.innerHTML = `<div class="empty-state"><div class="icon">&#128246;</div>No slots saved yet.<br>Capture a signal above and save it.</div>`;
    return;
  }

  grid.innerHTML = slots.map((s, i) => `
    <div class="slot-card">
      <div class="slot-card-header">
        <div class="slot-num">${i + 1}</div>
        <div class="slot-name">${escHtml(s.name)}</div>
        <div class="slot-len">${s.raw.split(',').length} vals</div>
      </div>
      <div class="slot-raw">${s.raw.substring(0, 80)}${s.raw.length > 80 ? '…' : ''}</div>
      <div class="slot-actions">
        <button class="btn btn-send" onclick="sendSlot(${i})"><span>&#9654;</span> SEND</button>
        <button class="btn btn-delete" onclick="deleteSlot(${i})">&#10005;</button>
      </div>
    </div>
  `).join('');
}

function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// ── Capture ──────────────────────────────────────────────────────────────────
async function startCapture() {
  const btn = document.getElementById('btnCapture');
  btn.disabled = true;
  setStatus('listening', 'Listening… point remote at sensor and press a button (10 s timeout)');
  hidePreview();

  try {
    const res = await fetch('/capture', { signal: AbortSignal.timeout(15000) });
    const data = await res.json();

    if (!res.ok) {
      setStatus('err', data.error || 'Capture failed');
      toast(data.error || 'Capture failed', 'err');
      return;
    }

    setStatus('ok', `Captured ${data.rawLen} values — review below`);
    showPreview(data.raw);

    // Auto-fill slot select
    const slots = loadSlots();
    const slotSel = document.getElementById('capSlot');
    if (slots.length < MAX_SLOTS && slotSel.value === '-1') {
      slotSel.value = String(slots.length);
    }

  } catch (err) {
    setStatus('err', err.name === 'TimeoutError' ? 'Request timed out' : 'Connection error');
    toast('Connection error', 'err');
  } finally {
    btn.disabled = false;
  }
}

function showPreview(rawStr) {
  const box = document.getElementById('rawPreview');
  document.getElementById('rawTextarea').value = rawStr;
  box.style.display = 'block';
}

function hidePreview() {
  document.getElementById('rawPreview').style.display = 'none';
  document.getElementById('rawTextarea').value = '';
}

function clearPreview() {
  hidePreview();
  setStatus('idle', 'Ready — point remote at sensor and press CAPTURE');
}

// ── Save from preview ─────────────────────────────────────────────────────────
function saveFromPreview() {
  const raw  = document.getElementById('rawTextarea').value.trim();
  const name = document.getElementById('capName').value.trim() || 'Signal';
  const slotIdx = parseInt(document.getElementById('capSlot').value);

  if (!raw) { toast('No raw data to save', 'err'); return; }

  const slots = loadSlots();

  if (slotIdx >= 0 && slotIdx < MAX_SLOTS) {
    // Save/overwrite specific slot
    slots[slotIdx] = { name, raw, ts: Date.now() };
    // Pad if needed
    for (let i = slots.length; i <= slotIdx && i < MAX_SLOTS; i++) {
      if (!slots[i]) slots[i] = null;
    }
    saveSlots(slots.filter(Boolean));
  } else {
    // Append
    if (slots.length >= MAX_SLOTS) { toast('All 10 slots are full — delete one first', 'err'); return; }
    slots.push({ name, raw, ts: Date.now() });
    saveSlots(slots);
  }

  renderSlots();
  toast('Saved: ' + name, 'ok');
}

// ── Send from preview ─────────────────────────────────────────────────────────
async function sendFromPreview() {
  const raw = document.getElementById('rawTextarea').value.trim();
  if (!raw) { toast('No data to send', 'err'); return; }
  await sendRaw(raw);
}

// ── Send slot ─────────────────────────────────────────────────────────────────
async function sendSlot(i) {
  const slots = loadSlots();
  if (!slots[i]) return;
  await sendRaw(slots[i].raw, slots[i].name);
}

// ── Core send ─────────────────────────────────────────────────────────────────
async function sendRaw(rawStr, label = '') {
  try {
    const res = await fetch('/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ raw: rawStr }),
      signal: AbortSignal.timeout(8000)
    });
    const data = await res.json();

    if (res.ok) {
      toast((label ? label + ': ' : '') + 'Sent ✓ (' + data.rawLen + ' values)', 'ok');
    } else {
      toast(data.error || 'Send failed', 'err');
    }
  } catch(e) {
    toast('Connection error', 'err');
  }
}

// ── Delete slot ───────────────────────────────────────────────────────────────
function deleteSlot(i) {
  const slots = loadSlots();
  if (!confirm('Delete "' + slots[i].name + '"?')) return;
  slots.splice(i, 1);
  saveSlots(slots);
  renderSlots();
  toast('Slot deleted', 'ok');
}

// ── Boot ─────────────────────────────────────────────────────────────────────
renderSlots();
</script>
</body>
</html>
)rawhtml";

// ─────────────────────────────────────────────────────────────────────────────
//  Route Handlers
// ─────────────────────────────────────────────────────────────────────────────

// OPTIONS preflight (for Flutter app CORS)
void handleOptions() {
  addCORSHeaders();
  server.send(204, "text/plain", "");
}

// GET /
void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

// GET /status  →  {"ip":"192.168.4.1","heap":12345}
void handleStatus() {
  String json = "{\"ip\":\"" + WiFi.softAPIP().toString() + "\","
                + "\"heap\":" + String(ESP.getFreeHeap()) + "}";
  sendJSON(200, json);
}

// GET /capture  →  {"raw":"9000,4500,...","rawLen":67}   or error JSON
void handleCapture() {
  irrecv.enableIRIn();
  Serial.println("[capture] Waiting for IR signal (10 s)…");

  decode_results cap;
  bool got = false;
  unsigned long deadline = millis() + 10000UL;

  while (millis() < deadline) {
    if (irrecv.decode(&cap)) { got = true; break; }
    delay(50);
    yield();
  }

  if (!got) {
    sendJSON(408, "{\"error\":\"Timeout: no IR signal received\"}");
    return;
  }

  uint16_t len = min((uint16_t)(cap.rawlen - 1), (uint16_t)MAX_RAW_LEN);
  String rawStr = "";
  rawStr.reserve(len * 6);
  for (uint16_t i = 0; i < len; i++) {
    if (i) rawStr += ',';
    rawStr += String((uint16_t)(cap.rawbuf[i + 1] * kRawTick));
  }

  irrecv.resume();
  blinkLED(2);

  String json = "{\"raw\":\"" + rawStr + "\",\"rawLen\":" + String(len) + "}";
  sendJSON(200, json);

  Serial.printf("[capture] OK — %d values\n", len);
}

// POST /send   body JSON: {"raw":"9000,4500,..."}
//              →  {"ok":true,"rawLen":67}   or error JSON
void handleSend() {
  if (!server.hasArg("plain")) {
    sendJSON(400, "{\"error\":\"Missing JSON body\"}");
    return;
  }

  String body = server.arg("plain");

  // Extract "raw" value from JSON (lightweight manual parse)
  int rStart = body.indexOf("\"raw\"");
  if (rStart < 0) {
    sendJSON(400, "{\"error\":\"Missing 'raw' field\"}");
    return;
  }
  int q1 = body.indexOf('"', rStart + 5);
  int q2 = body.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) {
    sendJSON(400, "{\"error\":\"Malformed raw value\"}");
    return;
  }
  String rawStr = body.substring(q1 + 1, q2);
  rawStr.trim();

  // Parse comma-separated values
  uint16_t rawData[MAX_RAW_LEN];
  uint16_t count = 0;
  int start = 0;

  for (int i = 0; i <= (int)rawStr.length() && count < MAX_RAW_LEN; i++) {
    if (i == (int)rawStr.length() || rawStr.charAt(i) == ',') {
      String token = rawStr.substring(start, i);
      token.trim();
      if (token.length() > 0) {
        rawData[count++] = (uint16_t)token.toInt();
      }
      start = i + 1;
    }
  }

  if (count == 0) {
    sendJSON(400, "{\"error\":\"No valid raw values parsed\"}");
    return;
  }

  irrecv.disableIRIn();
  irsend.begin();
  irsend.sendRaw(rawData, count, 38);
  irrecv.enableIRIn();

  blinkLED(2);

  Serial.printf("[send] OK — %d values\n", count);
  sendJSON(200, "{\"ok\":true,\"rawLen\":" + String(count) + "}");
}

// GET /slots (convenience stub — actual storage is in browser localStorage)
// Returns just the API contract info
void handleSlotsInfo() {
  sendJSON(200, "{\"note\":\"Slots are stored in browser localStorage. Use POST /send with raw values.\",\"maxSlots\":10,\"maxRawLen\":" + String(MAX_RAW_LEN) + "}");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Startup IR Codes
//  Sent once on power-on: Epson projector power, then Vivitek projector power.
//  Each signal has two frames (main + repeat) separated by a long gap.
//  We split at the gap and send each frame independently so the IR library
//  doesn't choke on the huge inter-frame µs value.
//
//  Gap between the two projector signals: STARTUP_GAP_MS (adjustable).
// ─────────────────────────────────────────────────────────────────────────────

#define STARTUP_GAP_MS  1000   // ms to wait between Epson send and Vivitek send

// ── Epson Power ──────────────────────────────────────────────────────────────
// Frame 1 (main)
static const uint16_t EPSONN[]  = {
  9092,4404,632,1576,660,472,630,474,630,472,594,494,700,418,632,472,
  632,1604,630,1578,658,1588,652,468,632,472,632,472,630,498,632,498,
  632,472,632,474,632,472,632,472,630,474,632,1578,658,1578,658,1578,
  658,1576,658,1578,658,1576,658,1576,660,1576,658,500,632,498,632,500,
  630,472,632,43828,9104,4390,632,1604,632,1602,632,500,630,472,632,472,
  632,472,630,474,618,1590,658,1604,632,472,632,1602,632,500,630,1604,632,
  472,632,1604,630,500,632,498,630,472,632,474,630,472,632,1604,632,498,632,
  474,630,1578,658,1576,660,1604,632,1604,632,1604,632,474,630,1576,658,1604,
  632,474,630
};
static const uint16_t EPSON_LEN = sizeof(EPSONN) / sizeof(EPSONN[0]);


// ── Vivitek Power ─────────────────────────────────────────────────────────────
// Frame 1 (main)
static const uint16_t VIVITEK[]  = {
  9058,4468,630,1642,628,506,628,508,626,510,624,
  1644,630,1642,630,506,604,534,602,532,626,1634,
  624,1654,604,1668,630,506,628,508,630,1642,604,1666,
  630,1642,628,508,604,530,630,508,602,532,626,508,602,
  532,602,1668,630,508,600,1668,628,1644,630,1640,604,
  1668,602,1668,628,1644,628,508,626,40140,9034,2222,630
};
static const uint16_t VIVITEK_LEN = sizeof(VIVITEK) / sizeof(VIVITEK[0]);



// Helper: copy PROGMEM array to RAM buffer and send
void sendProgmemRaw(const uint16_t* pgmData, uint16_t len, uint32_t freq = 38000) {
  uint16_t buf[MAX_RAW_LEN];
  uint16_t safeLen = min(len, (uint16_t)MAX_RAW_LEN);
  for (uint16_t i = 0; i < safeLen; i++) {
    buf[i] = pgm_read_word(&pgmData[i]);
  }
  irsend.sendRaw(buf, safeLen, freq);
}

void sendStartupCodes() {
  Serial.println("[startup] Sending Epson Power...");

  // Epson frame 1
  irsend.sendRaw(EPSONN, EPSON_LEN,38000);
  blinkLED(1);
  Serial.println("[startup] Epson sent. Waiting before Vivitek...");
  delay(STARTUP_GAP_MS);

  Serial.println("[startup] Sending Vivitek Power...");

  // Vivitek frame 1
  irsend.sendRaw(VIVITEK, VIVITEK_LEN,38000);
  blinkLED(1);


  Serial.println("[startup] Both projector codes sent.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Setup / Loop
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== IR Remote Manager (API Mode) ===");

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // IR init
  irsend.begin();

  // ── Startup IR codes ──────────────────────────────────────────────────────
  sendStartupCodes();
  // ─────────────────────────────────────────────────────────────────────────

  irrecv.enableIRIn();

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Routes
  server.on("/",        HTTP_GET,     handleRoot);
  server.on("/status",  HTTP_GET,     handleStatus);
  server.on("/capture", HTTP_GET,     handleCapture);
  server.on("/send",    HTTP_POST,    handleSend);
  server.on("/slots",   HTTP_GET,     handleSlotsInfo);

  // CORS preflight for all routes
  server.on("/capture", HTTP_OPTIONS, handleOptions);
  server.on("/send",    HTTP_OPTIONS, handleOptions);
  server.on("/status",  HTTP_OPTIONS, handleOptions);
  server.on("/slots",   HTTP_OPTIONS, handleOptions);

  server.begin();
  Serial.println("HTTP server started.");

  blinkLED(3);
  Serial.println("Connect to Wi-Fi SSID: " + String(AP_SSID));
  Serial.println("Open http://192.168.4.1");
  Serial.println();
  Serial.println("API Endpoints:");
  Serial.println("  GET  /status   → device info JSON");
  Serial.println("  GET  /capture  → capture IR, returns raw JSON (10s timeout)");
  Serial.println("  POST /send     → body: {\"raw\":\"9000,4500,...\"} → sends IR");
  Serial.println("  GET  /slots    → API contract info");
}

void loop() {
  server.handleClient();
  delay(5);
}
