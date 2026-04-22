#include "NetworkManager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "DisplayManager.h" // To update the screen during setup
#include "stations.h"

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;
bool portalActive = false;
String targetStation = "";

const char* AP_NAME = "VBZ-Display-Setup";

// ---------------------------------------------------------------------------
// Captive portal HTML page (served from flash)
// ---------------------------------------------------------------------------
static const char HTML_PAGE[] PROGMEM = R"====(<!DOCTYPE html>
<html lang="de">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Station Board Setup</title>
<style>
*{box-sizing:border-box}
body{font-family:Arial,sans-serif;background:#1a1a2e;margin:0;padding:20px;display:flex;justify-content:center}
.card{background:#16213e;color:#eee;border-radius:10px;padding:24px;width:100%;max-width:430px}
h2{margin:0 0 4px;color:#e94560;font-size:1.3em}
.sub{color:#888;font-size:.85em;margin:0 0 18px}
label{display:block;margin-top:18px;font-size:.75em;color:#999;text-transform:uppercase;letter-spacing:.06em;font-weight:bold}
input,select{width:100%;padding:9px 11px;margin-top:5px;font-size:15px;background:#0f3460;color:#eee;border:1px solid #333;border-radius:6px;outline:none;-webkit-appearance:none}
input:focus,select:focus{border-color:#e94560;box-shadow:0 0 0 2px rgba(233,69,96,.2)}
.ac-wrap{position:relative}
.ac-list{position:absolute;top:100%;left:0;right:0;background:#0f3460;border:1px solid #e94560;border-top:none;border-radius:0 0 6px 6px;max-height:200px;overflow-y:auto;z-index:99;display:none}
.ac-item{padding:9px 11px;cursor:pointer;font-size:14px;color:#eee}
.ac-item:hover,.ac-item.sel{background:rgba(233,69,96,.25)}
.hint{font-size:11px;color:#666;margin:4px 0 0;line-height:1.5}
button{margin-top:22px;width:100%;padding:12px;font-size:16px;background:#e94560;color:#fff;border:none;border-radius:6px;cursor:pointer;font-weight:bold}
button:hover{background:#c73652}
.err{color:#ff6b6b;font-size:13px;margin-top:8px;display:none}
</style>
</head><body>
<div class="card">
<h2>Station Board Setup</h2><p class="sub">VBZ departure display configuration</p>
<form id="F" method="POST" action="/save">

<label>WiFi Network</label>
<select id="ssidSel"><option value="">Scanning&hellip;</option></select>
<input type="text" name="ssid" id="ssidIn" placeholder="SSID (select above or type here)" autocomplete="off" style="margin-top:6px">
<p class="hint" id="wifiHint" style="display:none"></p>

<label>WiFi Password</label>
<input type="password" name="password" autocomplete="new-password" placeholder="Leave blank to keep current / for open networks">

<label>Station</label>
<div class="ac-wrap">
  <input type="text" name="station" id="stIn" autocomplete="off" placeholder="e.g. Zurich HB">
  <div class="ac-list" id="acList"></div>
</div>
<p class="hint">Umlauts optional: u for &uuml;, o for &ouml;, a for &auml;. Commas optional.</p>

<label>Display Mode</label>
<select name="opMode" id="modeEl">
  <option value="tram">Tram &mdash; actual arrival time &amp; minutes remaining</option>
  <option value="train">Train &mdash; timetable order &amp; delay column</option>
</select>

<button type="submit">Save &amp; Reboot</button>
<div class="err" id="errMsg"></div>
</form></div>
<script>
(function(){
var stns=[],acEl=document.getElementById('acList'),stIn=document.getElementById('stIn');
var ssidIn=document.getElementById('ssidIn'),ssidSel=document.getElementById('ssidSel');
var acIdx=-1;

function norm(s){
  return (s||'').toLowerCase()
    .replace(/\u00e4/g,'a').replace(/\u00f6/g,'o').replace(/\u00fc/g,'u')
    .replace(/\u00c4/g,'a').replace(/\u00d6/g,'o').replace(/\u00dc/g,'u')
    .replace(/[,;:]/g,' ').replace(/\s+/g,' ').trim();
}

function renderAc(v){
  if(v.length<2){acEl.style.display='none';return;}
  var nv=norm(v),res=[];
  for(var i=0;i<stns.length&&res.length<12;i++)if(norm(stns[i]).indexOf(nv)!==-1)res.push(stns[i]);
  if(!res.length){acEl.style.display='none';return;}
  acEl.innerHTML='';acIdx=-1;
  res.forEach(function(s){
    var d=document.createElement('div');d.className='ac-item';d.textContent=s;
    d.addEventListener('mousedown',function(e){e.preventDefault();stIn.value=s;acEl.style.display='none';});
    acEl.appendChild(d);
  });
  acEl.style.display='block';
}

function highlightAc(){
  var its=acEl.querySelectorAll('.ac-item');
  its.forEach(function(el,i){el.classList.toggle('sel',i===acIdx);});
  if(acIdx>=0)its[acIdx].scrollIntoView({block:'nearest'});
}

stIn.addEventListener('input',function(){renderAc(this.value);});
stIn.addEventListener('blur',function(){setTimeout(function(){acEl.style.display='none';},200);});
stIn.addEventListener('keydown',function(e){
  if(acEl.style.display==='none')return;
  var its=acEl.querySelectorAll('.ac-item');
  if(e.key==='ArrowDown'){e.preventDefault();acIdx=Math.min(acIdx+1,its.length-1);highlightAc();}
  else if(e.key==='ArrowUp'){e.preventDefault();acIdx=Math.max(acIdx-1,-1);highlightAc();}
  else if(e.key==='Enter'&&acIdx>=0){e.preventDefault();stIn.value=its[acIdx].textContent;acEl.style.display='none';}
});

stIn.addEventListener('focus',function(){
  if(!stns.length)fetch('/api/stations').then(function(r){return r.json();}).then(function(d){stns=d;renderAc(stIn.value);}).catch(function(){});
},true);

ssidSel.addEventListener('change',function(){if(this.value)ssidIn.value=this.value;});

fetch('/api/config').then(function(r){return r.json();}).then(function(c){
  stIn.value=c.station||'';
  document.getElementById('modeEl').value=c.opMode||'tram';
  window._sv=c.ssid||'';
  var hint=document.getElementById('wifiHint');
  if(c.ssid){hint.textContent='Currently using: '+c.ssid+' \u2014 leave blank to keep.';hint.style.display='block';ssidIn.placeholder='Leave blank to keep current SSID';}
}).catch(function(){});

fetch('/api/wifi').then(function(r){return r.json();}).then(function(nets){
  ssidSel.innerHTML='<option value="">-- pick network --</option>';
  nets.sort(function(a,b){return b.rssi-a.rssi;});
  nets.forEach(function(n){
    var o=document.createElement('option');
    o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';
    if(n.ssid===window._sv)o.selected=true;
    ssidSel.appendChild(o);
  });
}).catch(function(){ssidSel.innerHTML='<option value="">Scan failed &mdash; type SSID below</option>';});

document.getElementById('F').addEventListener('submit',function(e){
  var err=document.getElementById('errMsg');
  err.style.display='none';
  if(!ssidIn.value.trim()&&!window._sv){err.textContent='Please enter or select a WiFi network name.';err.style.display='block';e.preventDefault();return;}
  if(!stIn.value.trim()){err.textContent='Please enter a station name.';err.style.display='block';e.preventDefault();}
});
})();
</script></body></html>
)====";

String getOperationMode() {
    preferences.begin("settings", true);
    String opMode = preferences.getString("opMode", "tram");
    preferences.end();
    return opMode;
}

void handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", HTML_PAGE);
}

void handleConfig() {
    preferences.begin("settings", true);
    String station = preferences.getString("station", "Zürich HB");
    String opMode  = preferences.getString("opMode",  "tram");
    String ssid    = preferences.getString("ssid",    "");
    preferences.end();

    // Escape JSON string values
    auto jsonEsc = [](String s) -> String {
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        return s;
    };

    String json = "{";
    json += "\"station\":\"" + jsonEsc(station) + "\",";
    json += "\"opMode\":\""  + jsonEsc(opMode)  + "\",";
    json += "\"ssid\":\""    + jsonEsc(ssid)    + "\"";
    json += "}";
    server.send(200, "application/json; charset=utf-8", json);
}

void handleSave() {
    if (!server.hasArg("station") || !server.hasArg("opMode")) {
        server.send(400, "text/plain", "Missing required arguments");
        return;
    }

    preferences.begin("settings", false);
    preferences.putString("station", server.arg("station"));
    preferences.putString("opMode",  server.arg("opMode"));

    // Only overwrite WiFi credentials when provided
    if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
        preferences.putString("ssid", server.arg("ssid"));
    }
    if (server.hasArg("password") && server.arg("password").length() > 0) {
        preferences.putString("password", server.arg("password"));
    }
    preferences.end();

    server.send(200, "text/html; charset=utf-8",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'></head>"
        "<body style='font-family:Arial;text-align:center;margin-top:60px;background:#1a1a2e;color:#eee'>"
        "<h2 style='color:#e94560'>Saved! Rebooting&hellip;</h2></body></html>");
    delay(1000);
    ESP.restart();
}

void handleWiFiScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
}

void handleStations() {
    // Serve the auto-generated JSON string from PROGMEM
    server.send(200, "application/json; charset=utf-8", STATIONS_JSON);
}

void startPortal() {
    portalActive = true;
    WiFi.mode(WIFI_AP_STA); // Set to AP+STA mode so it can scan while broadcasting AP
    WiFi.softAP(AP_NAME);
    
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    
    // Register endpoints
    server.on("/",            handleRoot);
    server.on("/api/config",  handleConfig);
    server.on("/api/wifi",    handleWiFiScan);
    server.on("/api/stations",handleStations);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRoot);
    server.begin();
    
    showStatus("Connect to WiFi:\n" + String(AP_NAME) + "\nto configure.");
}

void setupNetwork() {
    // All settings stored under a single unified namespace
    preferences.begin("settings", true);
    String ssid     = preferences.getString("ssid",     "");
    String password = preferences.getString("password", "");
    targetStation   = preferences.getString("station",  "Zürich, Hubertus");
    preferences.end();

    if (ssid == "") {
        startPortal();
        return;
    }

    showStatus("Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) { // 10-second timeout
        delay(500);
        retries++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        startPortal(); // Fallback to portal if connection fails
    }
}

void handleNetworkLoop() {
    if (portalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}

void triggerCaptivePortal() {
    startPortal();
}

String getTargetStation() {
    return targetStation;
}