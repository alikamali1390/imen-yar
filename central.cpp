// Central ESP32 device: local web server with live updates, password-protected
// settings panel, gas alert SMS/call notifications.
//
// Communication with the unit devices now runs over Bluetooth-mesh (BLE
// advertising flood-relay, see mesh_common.h / mesh_node.h) instead of
// WiFi/HTTP. WiFi is still used here only for the local web panel and for
// calling out to the Kavenegar SMS/voice API, which needs internet access.
// ESP32 can run WiFi (station) and BLE concurrently.

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "mesh_common.h"
#include "mesh_node.h"

// WiFi settings
const char* ssid = "Nova";
const char* password = "n@va889n@va";

// Kavenegar settings
const char* kavenegarApiKey = "4E64346F484E324930706C3271466653796F723134665170626243503349584F695774766A7471684B50383D";
const char* senderLine = "2000660110"; // test webservice line

// Settings panel password
const char* SETTINGS_PASSWORD = "1234"; // change this password as desired

// Central buzzer pin
const int BUZZER_PIN = 26;

// Central's own id on the mesh (0 = central, by convention)
#define CENTRAL_MESH_ID 0

// Alert status
bool unit1Alert = false;
bool unit2Alert = false;
unsigned long lastAlertTime = 0;
const unsigned long ALERT_COOLDOWN = 30000;

// Stored settings
Preferences prefs;
String unit1Phone, unit1Mode;
String unit2Phone, unit2Mode;
String guardPhone, guardMode;
const char* DEFAULT_GUARD_PHONE = "09901260181"; // default guard phone number (you)

WebServer server(80);
MeshNode mesh;

// Load/save settings
void loadSettings() {
  prefs.begin("gascfg", true);
  unit1Phone = prefs.getString("u1phone", "");
  unit1Mode = prefs.getString("u1mode", "sms");
  unit2Phone = prefs.getString("u2phone", "");
  unit2Mode = prefs.getString("u2mode", "sms");
  guardPhone = prefs.getString("gphone", DEFAULT_GUARD_PHONE);
  guardMode = prefs.getString("gmode", "sms");
  prefs.end();
}

void saveSettings() {
  prefs.begin("gascfg", false);
  prefs.putString("u1phone", unit1Phone);
  prefs.putString("u1mode", unit1Mode);
  prefs.putString("u2phone", unit2Phone);
  prefs.putString("u2mode", unit2Mode);
  prefs.putString("gphone", guardPhone);
  prefs.putString("gmode", guardMode);
  prefs.end();
}

// ================== URL Encode ==================
String urlEncode(const String &str) {
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum((unsigned char)c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Send SMS/call via Kavenegar
void kavenegarRequest(const String &endpoint, const String &phone, const String &message) {
  if (phone.length() == 0 || WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.kavenegar.com/v1/" + String(kavenegarApiKey) + "/" + endpoint +
               "?receptor=" + urlEncode(phone);
  if (endpoint.indexOf("sms/") >= 0) {
    url += "&sender=" + urlEncode(String(senderLine));
  }
  url += "&message=" + urlEncode(message);
  http.begin(client, url);
  int code = http.GET();
  Serial.printf(">> [%s -> %s] Response code: %d\n", endpoint.c_str(), phone.c_str(), code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}

void notify(const String &phone, const String &mode, const String &message) {
  if (phone.length() == 0) return;
  if (mode == "call") {
    kavenegarRequest("call/maketts.json", phone, message);
  } else {
    kavenegarRequest("sms/send.json", phone, message);
  }
}

// ---- Core alert/clear logic, now triggered from the BLE mesh callback
// (still exposed as small helper functions so both the mesh path and the
// optional manual HTTP path below reuse the same code) ----
void raiseAlert(const String &unit) {
  String message, ownerPhone, ownerMode;

  if (unit == "1") {
    unit1Alert = true;
    ownerPhone = unit1Phone; ownerMode = unit1Mode;
    message = "هشدار !!\nگاز در واحد ۱ تشخیص داده شد .\nجهت جلوگیری از آتش سوزی ؛ گاز واحد موقتا قطع می‌شود .\n( MQ 135 )";
  } else if (unit == "2") {
    unit2Alert = true;
    ownerPhone = unit2Phone; ownerMode = unit2Mode;
    message = "هشدار !!\nگاز در واحد ۲ تشخیص داده شد .\nجهت جلوگیری از آتش سوزی ؛ گاز واحد موقتا قطع می‌شود .\n( MQ 135 )";
  } else {
    return;
  }

  digitalWrite(BUZZER_PIN, HIGH);

  unsigned long now = millis();
  if (now - lastAlertTime > ALERT_COOLDOWN) {
    notify(ownerPhone, ownerMode, message);
    notify(guardPhone, guardMode, message);
    lastAlertTime = now;
  }
}

void clearAlert(const String &unit) {
  if (unit == "1") unit1Alert = false;
  else if (unit == "2") unit2Alert = false;
  else return;

  if (!unit1Alert && !unit2Alert) {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ---- BLE mesh receive callback: this replaces the old /gas-alert and
// /gas-clear HTTP endpoints as the primary trigger path ----
void onMeshPacket(const MeshPacket &pkt, bool isRelay) {
  String unit = String(pkt.srcUnit);
  if (pkt.msgType == MESH_MSG_GAS_ALERT) {
    Serial.printf("[mesh] gas-alert received from unit %u (ttl=%u)\n", pkt.srcUnit, pkt.ttl);
    raiseAlert(unit);
  } else if (pkt.msgType == MESH_MSG_GAS_CLEAR) {
    Serial.printf("[mesh] gas-clear received from unit %u (ttl=%u)\n", pkt.srcUnit, pkt.ttl);
    clearAlert(unit);
  }
}

void handleReset() {
  unit1Alert = false;
  unit2Alert = false;
  digitalWrite(BUZZER_PIN, LOW);
  server.send(200, "text/plain", "Reset done");
}

// Real-time status API
void handleStatus() {
  String json = "{\"unit1\":" + String(unit1Alert ? "true" : "false") +
                ",\"unit2\":" + String(unit2Alert ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// Settings API
bool checkPass() {
  return server.hasArg("pass") && server.arg("pass") == String(SETTINGS_PASSWORD);
}

void handleGetSettings() {
  if (!checkPass()) { server.send(401, "text/plain", "wrong password"); return; }
  String json = "{\"u1phone\":\"" + unit1Phone + "\",\"u1mode\":\"" + unit1Mode +
                "\",\"u2phone\":\"" + unit2Phone + "\",\"u2mode\":\"" + unit2Mode +
                "\",\"gphone\":\"" + guardPhone + "\",\"gmode\":\"" + guardMode + "\"}";
  server.send(200, "application/json", json);
}

void handleSaveSettings() {
  if (!checkPass()) { server.send(401, "text/plain", "wrong password"); return; }
  if (server.hasArg("u1phone")) unit1Phone = server.arg("u1phone");
  if (server.hasArg("u1mode")) unit1Mode = server.arg("u1mode");
  if (server.hasArg("u2phone")) unit2Phone = server.arg("u2phone");
  if (server.hasArg("u2mode")) unit2Mode = server.arg("u2mode");
  if (server.hasArg("gphone")) guardPhone = server.arg("gphone");
  if (server.hasArg("gmode")) guardMode = server.arg("gmode");
  saveSettings();
  server.send(200, "text/plain", "saved");
}

// Main page
void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html><html lang="fa" dir="rtl"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>سامانه هشدار گاز</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:Tahoma,sans-serif}
body{background:linear-gradient(135deg,#1e293b,#0f172a);min-height:100vh;color:#f1f5f9;padding:20px}
.topbar{display:flex;justify-content:space-between;align-items:center;max-width:700px;margin:0 auto 30px}
.topbar h1{font-size:22px}
.gear{cursor:pointer;font-size:26px;background:#1e293b;border:1px solid #334155;border-radius:10px;width:44px;height:44px;display:flex;align-items:center;justify-content:center;transition:.2s}
.gear:hover{background:#334155}
.cards{max-width:700px;margin:0 auto;display:grid;gap:16px;grid-template-columns:1fr 1fr}
.card{background:#1e293b;border-radius:16px;padding:24px;text-align:center;border:1px solid #334155;transition:.3s}
.card h2{font-size:16px;color:#94a3b8;margin-bottom:12px}
.status{font-size:18px;font-weight:bold;padding:10px;border-radius:10px}
.ok{background:#064e3b;color:#34d399}
.alert{background:#7f1d1d;color:#fca5a5;animation:pulse 1s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);align-items:center;justify-content:center;z-index:10}
.modal.show{display:flex}
.box{background:#1e293b;border-radius:16px;padding:24px;width:90%;max-width:400px;border:1px solid #334155}
.box h3{margin-bottom:16px}
input,select{width:100%;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #334155;background:#0f172a;color:#f1f5f9}
label{font-size:13px;color:#94a3b8}
.row{display:flex;gap:10px}
button{width:100%;padding:12px;border:none;border-radius:8px;background:#3b82f6;color:#fff;font-size:15px;cursor:pointer;margin-top:6px}
button.secondary{background:#334155}
.unit-block{border-top:1px solid #334155;padding-top:12px;margin-top:12px}
</style></head>
<body>
<div class="topbar">
<h1>سامانه هشدار گاز</h1>
<div class="gear" onclick="openLogin()">⚙️</div>
</div>
<div class="cards">
<div class="card"><h2>واحد ۱</h2><div id="s1" class="status ok">در حال بررسی...</div></div>
<div class="card"><h2>واحد ۲</h2><div id="s2" class="status ok">در حال بررسی...</div></div>
</div>
<div class="modal" id="loginModal">
<div class="box">
<h3>ورود به تنظیمات</h3>
<label>رمز عبور</label>
<input type="password" id="passInput">
<button onclick="tryLogin()">ورود</button>
<button class="secondary" onclick="closeModal('loginModal')">انصراف</button>
</div>
</div>
<div class="modal" id="settingsModal">
<div class="box">
<h3>تنظیمات شماره‌ها</h3>
<div class="unit-block">
<label>شماره صاحب واحد ۱</label>
<input id="u1phone">
<label>نحوه اطلاع‌رسانی</label>
<select id="u1mode"><option value="sms">پیامک</option><option value="call">تماس</option></select>
</div>
<div class="unit-block">
<label>شماره صاحب واحد ۲</label>
<input id="u2phone">
<label>نحوه اطلاع‌رسانی</label>
<select id="u2mode"><option value="sms">پیامک</option><option value="call">تماس</option></select>
</div>
<div class="unit-block">
<label>شماره نگهبان</label>
<input id="gphone">
<label>نحوه اطلاع‌رسانی</label>
<select id="gmode"><option value="sms">پیامک</option><option value="call">تماس</option></select>
</div>
<button onclick="saveSettings()">ذخیره</button>
<button class="secondary" onclick="closeModal('settingsModal')">بستن</button>
</div>
</div>
<script>
let currentPass = "";
function refreshStatus(){
fetch('/status').then(r=>r.json()).then(d=>{
setBox('s1', d.unit1);
setBox('s2', d.unit2);
}).catch(()=>{});
}
function setBox(id, alertOn){
const el = document.getElementById(id);
el.className = 'status ' + (alertOn ? 'alert' : 'ok');
el.textContent = alertOn ? 'هشدار گاز!' : 'عادی';
}
setInterval(refreshStatus, 2000);
refreshStatus();
function openLogin(){ document.getElementById('loginModal').classList.add('show'); }
function closeModal(id){ document.getElementById(id).classList.remove('show'); }
function tryLogin(){
const p = document.getElementById('passInput').value;
fetch('/get-settings?pass=' + encodeURIComponent(p)).then(r=>{
if(!r.ok) throw new Error('bad pass');
return r.json();
}).then(d=>{
currentPass = p;
document.getElementById('u1phone').value = d.u1phone;
document.getElementById('u1mode').value = d.u1mode;
document.getElementById('u2phone').value = d.u2phone;
document.getElementById('u2mode').value = d.u2mode;
document.getElementById('gphone').value = d.gphone;
document.getElementById('gmode').value = d.gmode;
closeModal('loginModal');
document.getElementById('settingsModal').classList.add('show');
}).catch(()=>alert('رمز اشتباه است'));
}
function saveSettings(){
const params = new URLSearchParams({
pass: currentPass,
u1phone: document.getElementById('u1phone').value,
u1mode: document.getElementById('u1mode').value,
u2phone: document.getElementById('u2phone').value,
u2mode: document.getElementById('u2mode').value,
gphone: document.getElementById('gphone').value,
gmode: document.getElementById('gmode').value
});
fetch('/save-settings?' + params.toString()).then(r=>{
if(r.ok){ alert('ذخیره شد'); closeModal('settingsModal'); }
else alert('خطا در ذخیره');
});
}
</script>
</body></html>
)HTML";
  server.send(200, "text/html; charset=utf-8", html);
}

// Setup
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  loadSettings();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // Start the BLE mesh node — this is now how unit alerts/clears arrive.
  mesh.begin(CENTRAL_MESH_ID, onMeshPacket);
  Serial.println("Central BLE-mesh node started.");

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/reset", handleReset);
  server.on("/get-settings", handleGetSettings);
  server.on("/save-settings", handleSaveSettings);
  server.begin();
  Serial.println("Central web server started.");
}

void loop() {
  server.handleClient();
}
