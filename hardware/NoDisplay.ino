#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>

// ─── Pin Configuration (unchanged) ───────────────────────────────────────────
#define IR_RECV_PIN   D3
#define IR_SEND_PIN   D8
#define LED_PIN       LED_BUILTIN   // NodeMCU built-in LED (active LOW)

// ─── Buttons ──────────────────────────────────────────────────────────────────
uint8_t sw[4]            = { D1, D7, D5, D6 };
bool    PreviousState[4] = { 1, 1, 1, 1 };

// ─── IR ───────────────────────────────────────────────────────────────────────
#define MAX_RAW_LEN   150
#define REMOTES_FILE  "/remotes.dat"

struct IRSignal {
  uint16_t rawLen;
  uint16_t rawData[MAX_RAW_LEN];
  char     name[24];
  bool     valid;
};

IRSignal remotes[4];   // exactly 4 slots (one per button)

IRrecv irrecv(IR_RECV_PIN, 1024, 50, true);
IRsend irsend(IR_SEND_PIN);

// ─── Wi-Fi / Web server ───────────────────────────────────────────────────────
const char* AP_SSID = "IR-Remote-Manager";
const char* AP_PASS = "12345678";          // min 8 chars for WPA2

ESP8266WebServer server(80);

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadRemotesFromFS();
void saveRemotesToFS();
void sendIRSignal(int index);
void blinkLED(int times = 2);
void setupRoutes();

// ─────────────────────────────────────────────────────────────────────────────
//  File-system helpers
// ─────────────────────────────────────────────────────────────────────────────
void loadRemotesFromFS() {
  // Initialise all slots to empty
  for (int i = 0; i < 4; i++) {
    remotes[i].valid  = false;
    remotes[i].rawLen = 0;
    snprintf(remotes[i].name, sizeof(remotes[i].name), "Button %d", i + 1);
  }

  if (!LittleFS.exists(REMOTES_FILE)) {
    Serial.println("No saved remotes file.");
    return;
  }

  File f = LittleFS.open(REMOTES_FILE, "r");
  if (!f) { Serial.println("Cannot open remotes file."); return; }

  for (int i = 0; i < 4; i++) {
    if (f.read((uint8_t*)&remotes[i], sizeof(IRSignal)) != sizeof(IRSignal)) {
      Serial.printf("Read error at slot %d\n", i);
      remotes[i].valid = false;
    }
    // Safety clamp
    if (remotes[i].rawLen > MAX_RAW_LEN) remotes[i].rawLen = MAX_RAW_LEN;
  }
  f.close();
  Serial.println("Remotes loaded.");
}

void saveRemotesToFS() {
  File f = LittleFS.open(REMOTES_FILE, "w");
  if (!f) { Serial.println("Cannot open remotes file for writing."); return; }

  for (int i = 0; i < 4; i++) {
    f.write((uint8_t*)&remotes[i], sizeof(IRSignal));
  }
  f.close();
  Serial.println("Remotes saved.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  IR send + LED blink
// ─────────────────────────────────────────────────────────────────────────────
void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);   // active LOW
    delay(80);
    digitalWrite(LED_PIN, HIGH);
    delay(80);
  }
}

void sendIRSignal(int index) {
  if (index < 0 || index >= 4) return;
  if (!remotes[index].valid || remotes[index].rawLen == 0) {
    Serial.printf("Slot %d has no IR code.\n", index + 1);
    blinkLED(5);   // rapid blink = no code stored
    return;
  }

  irrecv.disableIRIn();
  irsend.begin();

  irsend.sendRaw(remotes[index].rawData, remotes[index].rawLen, 38);

  Serial.printf("Sent IR: %s  rawLen=%d\n", remotes[index].name, remotes[index].rawLen);

  irrecv.enableIRIn();

  blinkLED(2);   // 2 blinks = sent OK
}

// ─────────────────────────────────────────────────────────────────────────────
//  Web server – HTML UI
// ─────────────────────────────────────────────────────────────────────────────

// Inline HTML page (stored in PROGMEM to save RAM)
static const char PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>IR Remote Manager</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700&display=swap');

  :root {
    --bg:      #0a0e14;
    --panel:   #111822;
    --border:  #1e3a5f;
    --accent:  #00d4ff;
    --accent2: #ff6b35;
    --green:   #39ff14;
    --text:    #c8d8e8;
    --dim:     #5a7a9a;
    --danger:  #ff3355;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    font-family: 'Share Tech Mono', monospace;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    padding: 20px 12px 40px;
  }

  h1 {
    font-family: 'Orbitron', sans-serif;
    text-align: center;
    font-size: 1.3rem;
    letter-spacing: .15em;
    color: var(--accent);
    text-shadow: 0 0 12px var(--accent);
    margin-bottom: 6px;
  }

  .sub {
    text-align: center;
    font-size: .72rem;
    color: var(--dim);
    margin-bottom: 28px;
    letter-spacing: .1em;
  }

  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
    max-width: 680px;
    margin: 0 auto;
  }

  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 18px 16px;
    position: relative;
    overflow: hidden;
    transition: border-color .2s;
  }

  .card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
    opacity: 0;
    transition: opacity .3s;
  }

  .card:hover { border-color: var(--accent); }
  .card:hover::before { opacity: 1; }

  .card-header {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 14px;
  }

  .btn-num {
    font-family: 'Orbitron', sans-serif;
    font-size: .75rem;
    font-weight: 700;
    background: var(--accent);
    color: var(--bg);
    border-radius: 50%;
    width: 28px; height: 28px;
    display: flex; align-items: center; justify-content: center;
    flex-shrink: 0;
    box-shadow: 0 0 8px var(--accent);
  }

  .slot-label {
    font-size: .8rem;
    color: var(--accent);
    letter-spacing: .08em;
    flex: 1;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .status-dot.on  { background: var(--green); box-shadow: 0 0 6px var(--green); }
  .status-dot.off { background: var(--dim); }

  label {
    display: block;
    font-size: .68rem;
    color: var(--dim);
    letter-spacing: .06em;
    margin-bottom: 4px;
  }

  input[type=text] {
    width: 100%;
    background: #0d1520;
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--text);
    font-family: inherit;
    font-size: .78rem;
    padding: 7px 10px;
    margin-bottom: 10px;
    outline: none;
    transition: border-color .2s;
  }
  input[type=text]:focus { border-color: var(--accent); }

  textarea {
    width: 100%;
    background: #0d1520;
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--accent2);
    font-family: inherit;
    font-size: .68rem;
    padding: 7px 10px;
    min-height: 68px;
    resize: vertical;
    outline: none;
    line-height: 1.5;
    transition: border-color .2s;
    margin-bottom: 10px;
  }
  textarea:focus { border-color: var(--accent2); }

  .btn-row {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
  }

  button {
    font-family: 'Share Tech Mono', monospace;
    font-size: .72rem;
    letter-spacing: .06em;
    border: none;
    border-radius: 6px;
    padding: 8px 14px;
    cursor: pointer;
    transition: opacity .15s, transform .1s;
    flex: 1;
    min-width: 60px;
  }
  button:active { transform: scale(.96); }
  button:hover  { opacity: .88; }

  .btn-save   { background: var(--accent);  color: var(--bg); }
  .btn-delete { background: var(--danger);  color: #fff; }
  .btn-capture { background: #1a2e45; color: var(--accent); border: 1px solid var(--border); }

  .msg {
    max-width: 680px;
    margin: 18px auto 0;
    padding: 10px 16px;
    border-radius: 8px;
    font-size: .78rem;
    text-align: center;
    display: none;
  }
  .msg.ok  { background: #0f2a18; border: 1px solid var(--green); color: var(--green); }
  .msg.err { background: #2a0f14; border: 1px solid var(--danger); color: var(--danger); }

  .capture-hint {
    font-size: .65rem;
    color: var(--dim);
    margin-top: -6px;
    margin-bottom: 10px;
  }

  @media (max-width: 480px) {
    .grid { grid-template-columns: 1fr; }
  }
</style>
</head>
<body>

<h1>&#x25B6; IR REMOTE MANAGER</h1>
<p class="sub">NodeMCU &middot; 4-Button Controller &middot; Raw IR</p>

<div class="grid" id="grid"></div>
<div class="msg" id="msg"></div>

<script>
// Data injected from ESP
const SLOTS = JSON.parse('SLOTS_JSON_PLACEHOLDER');

function showMsg(text, isErr) {
  const el = document.getElementById('msg');
  el.textContent = text;
  el.className = 'msg ' + (isErr ? 'err' : 'ok');
  el.style.display = 'block';
  clearTimeout(el._t);
  el._t = setTimeout(() => el.style.display = 'none', 3000);
}

function renderGrid() {
  const grid = document.getElementById('grid');
  grid.innerHTML = '';
  SLOTS.forEach((slot, i) => {
    const hasCode = slot.valid && slot.rawLen > 0;
    const rawPreview = hasCode ? slot.rawData : '';
    grid.innerHTML += `
    <div class="card">
      <div class="card-header">
        <div class="btn-num">${i+1}</div>
        <div class="slot-label">${slot.name || 'Slot ' + (i+1)}</div>
        <div class="status-dot ${hasCode ? 'on' : 'off'}"></div>
      </div>

      <label>NAME</label>
      <input type="text" id="name_${i}" value="${slot.name || ''}" placeholder="e.g. TV Power">

      <label>RAW IR DATA <span style="color:#ff6b35">(comma-separated µs values)</span></label>
      <textarea id="raw_${i}" placeholder="9000,4500,600,550,600,550,...">${rawPreview}</textarea>
      <p class="capture-hint">&#128161; Use the CAPTURE endpoint or paste values from a serial dump.</p>

      <div class="btn-row">
        <button class="btn-save"   onclick="saveSlot(${i})">&#10003; SAVE</button>
        <button class="btn-delete" onclick="deleteSlot(${i})">&#10005; DELETE</button>
        <button class="btn-capture" onclick="captureSlot(${i})">&#9711; CAPTURE</button>
      </div>
    </div>`;
  });
}

async function saveSlot(i) {
  const name = document.getElementById('name_' + i).value.trim() || ('Slot ' + (i+1));
  const raw  = document.getElementById('raw_'  + i).value.trim();
  if (!raw) { showMsg('Raw data cannot be empty.', true); return; }

  const res = await fetch('/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `slot=${i}&name=${encodeURIComponent(name)}&raw=${encodeURIComponent(raw)}`
  });
  const txt = await res.text();
  showMsg(txt, !res.ok);
  if (res.ok) {
    SLOTS[i].name   = name;
    SLOTS[i].rawData = raw;
    SLOTS[i].valid  = true;
    SLOTS[i].rawLen = raw.split(',').length;
    renderGrid();
  }
}

async function deleteSlot(i) {
  if (!confirm('Delete IR code for slot ' + (i+1) + '?')) return;
  const res = await fetch('/delete?slot=' + i);
  const txt = await res.text();
  showMsg(txt, !res.ok);
  if (res.ok) {
    SLOTS[i].valid  = false;
    SLOTS[i].rawLen = 0;
    SLOTS[i].rawData = '';
    SLOTS[i].name   = 'Button ' + (i+1);
    renderGrid();
  }
}

async function captureSlot(i) {
  showMsg('Point your remote at the sensor and press a button… (10 s)', false);
  const res = await fetch('/capture?slot=' + i);
  const txt = await res.text();
  if (res.ok) {
    // Response is "name|rawdata"
    const parts = txt.split('|');
    if (parts.length === 2) {
      SLOTS[i].rawData = parts[1];
      SLOTS[i].rawLen  = parts[1].split(',').length;
      SLOTS[i].valid   = true;
      renderGrid();
      // Fill the textarea right after render
      document.getElementById('raw_' + i).value = parts[1];
      showMsg('Captured! Review and click SAVE.', false);
    } else {
      showMsg(txt, true);
    }
  } else {
    showMsg(txt, true);
  }
}

renderGrid();
</script>
</body>
</html>
)rawhtml";

// Build the JSON slots string to inject into the HTML
String buildSlotsJSON() {
  String json = "[";
  for (int i = 0; i < 4; i++) {
    json += "{";
    json += "\"name\":\"";
    // Escape name
    for (char c : String(remotes[i].name)) {
      if (c == '"' || c == '\\') json += '\\';
      json += c;
    }
    json += "\",";
    json += "\"valid\":" + String(remotes[i].valid ? "true" : "false") + ",";
    json += "\"rawLen\":" + String(remotes[i].rawLen) + ",";
    json += "\"rawData\":\"";
    for (int j = 0; j < remotes[i].rawLen; j++) {
      if (j) json += ',';
      json += String(remotes[i].rawData[j]);
    }
    json += "\"";
    json += "}";
    if (i < 3) json += ",";
  }
  json += "]";
  return json;
}

// GET /
void handleRoot() {
  String page = FPSTR(PAGE_HTML);
  page.replace("SLOTS_JSON_PLACEHOLDER", buildSlotsJSON());
  server.send(200, "text/html", page);
}

// POST /save   body: slot=0&name=TV+Power&raw=9000,4500,...
void handleSave() {
  if (!server.hasArg("slot") || !server.hasArg("raw")) {
    server.send(400, "text/plain", "Missing parameters.");
    return;
  }

  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot > 3) {
    server.send(400, "text/plain", "Invalid slot (0-3).");
    return;
  }

  String nameStr = server.arg("name");
  if (nameStr.length() == 0) nameStr = "Button " + String(slot + 1);
  nameStr.trim();

  String rawStr = server.arg("raw");
  rawStr.trim();

  // Parse comma-separated raw data
  uint16_t count = 0;
  uint16_t tmpRaw[MAX_RAW_LEN];
  int start = 0;
  for (int i = 0; i <= (int)rawStr.length() && count < MAX_RAW_LEN; i++) {
    if (i == (int)rawStr.length() || rawStr.charAt(i) == ',') {
      String token = rawStr.substring(start, i);
      token.trim();
      if (token.length() > 0) {
        tmpRaw[count++] = (uint16_t)token.toInt();
      }
      start = i + 1;
    }
  }

  if (count == 0) {
    server.send(400, "text/plain", "No valid raw values found.");
    return;
  }

  // Commit
  strncpy(remotes[slot].name, nameStr.c_str(), 23);
  remotes[slot].name[23] = '\0';
  remotes[slot].rawLen = count;
  for (int i = 0; i < count; i++) remotes[slot].rawData[i] = tmpRaw[i];
  remotes[slot].valid = true;

  saveRemotesToFS();

  server.send(200, "text/plain",
    "Slot " + String(slot + 1) + " saved (" + String(count) + " values).");
}

// GET /delete?slot=N
void handleDelete() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing slot.");
    return;
  }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot > 3) {
    server.send(400, "text/plain", "Invalid slot.");
    return;
  }

  remotes[slot].valid  = false;
  remotes[slot].rawLen = 0;
  snprintf(remotes[slot].name, sizeof(remotes[slot].name), "Button %d", slot + 1);

  saveRemotesToFS();
  server.send(200, "text/plain", "Slot " + String(slot + 1) + " deleted.");
}

// GET /capture?slot=N  – listen for up to 10 s then return "name|raw"
void handleCapture() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing slot.");
    return;
  }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot > 3) {
    server.send(400, "text/plain", "Invalid slot.");
    return;
  }

  // Temporarily disable send, enable receive
  irrecv.enableIRIn();

  Serial.printf("Capture mode for slot %d — waiting up to 10 s...\n", slot + 1);

  decode_results cap;
  bool got = false;
  unsigned long deadline = millis() + 10000UL;

  while (millis() < deadline) {
    if (irrecv.decode(&cap)) {
      got = true;
      break;
    }
    delay(50);
    yield();
  }

  if (!got) {
    server.send(408, "text/plain", "Timeout: no IR signal received.");
    return;
  }

  // Build raw string
  uint16_t len = min((uint16_t)(cap.rawlen - 1), (uint16_t)MAX_RAW_LEN);
  String rawStr = "";
  for (uint16_t i = 0; i < len; i++) {
    if (i) rawStr += ',';
    rawStr += String((uint16_t)(cap.rawbuf[i + 1] * kRawTick));
  }

  irrecv.resume();

  // Return as "name|rawdata" — the JS will show it for review before saving
  String defaultName = "Button " + String(slot + 1);
  server.send(200, "text/plain", defaultName + "|" + rawStr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Setup / Loop
// ─────────────────────────────────────────────────────────────────────────────
void setupRoutes() {
  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/save",    HTTP_POST, handleSave);
  server.on("/delete",  HTTP_GET,  handleDelete);
  server.on("/capture", HTTP_GET,  handleCapture);
  server.begin();
  Serial.println("HTTP server started.");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== IR Remote Manager (AP Mode) ===");

  // Buttons
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(sw[i], INPUT_PULLUP);
  }

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // off (active LOW)

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
    // Blink rapidly forever to signal error
    while (true) { blinkLED(10); delay(500); }
  }
  Serial.println("LittleFS mounted.");
  loadRemotesFromFS();

  // IR
  irrecv.enableIRIn();
  irsend.begin();

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Web server
  setupRoutes();

  // Startup blink
  blinkLED(3);
  Serial.println("Ready. Connect to Wi-Fi: " + String(AP_SSID));
  Serial.println("Then open http://192.168.4.1");
}

void loop() {
  server.handleClient();

  // Button scan – only in steady state (not during web capture)
  for (uint8_t i = 0; i < 4; i++) {
    bool cur = digitalRead(sw[i]);
    if (cur != PreviousState[i] && cur == LOW) {
      // Button i pressed
      sendIRSignal(i);
    }
    PreviousState[i] = cur;
  }

  delay(20);
}
