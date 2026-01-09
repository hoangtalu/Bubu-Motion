#include "wifi_service.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include <Preferences.h>
#include "logger.h"
DEFINE_MODULE_LOGGER(WifiLog)

namespace {
  constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
  constexpr uint32_t PROVISION_TIMEOUT_MS = 180000;
  const char* AP_SSID = "BUBU-SETUP";
  constexpr bool WIFI_PROVISION_LOGS = false;
  constexpr bool WIFI_LOGS = false;

#define WIFI_PROV_LOG(MSG) \
  do {                     \
    if (WIFI_PROVISION_LOGS) { WifiLog::println(MSG); } \
  } while (0)
#define WIFI_PROV_LOGF(FMT, ...) \
  do {                           \
    if (WIFI_PROVISION_LOGS) { WifiLog::printf((FMT), __VA_ARGS__); } \
  } while (0)

  WifiState state = WifiState::OFF;
  uint32_t connectStartMs = 0;
  uint32_t provisionStartMs = 0;
  uint32_t lastProvisionLogMs = 0;
  bool autoConnectTried = false;
  bool allowProvisionFallback = true;
  String ipStr;
  bool autoScanInProgress = false;

  bool provisioning = false;
  WebServer webServer(80);
  DNSServer dnsServer;
  std::vector<String> scannedSsids;
  std::vector<int32_t> scannedRssi;
  String targetSsid;
  String targetPass;
  struct KnownNet { String ssid; String pass; };
  std::vector<KnownNet> known;
  Preferences prefs;

  const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>BUBU Setup</title>
<style>
  :root{--eye-width:64px;--eye-height:64px;--eye-gap:22px;--text-color:#fff;--bg:#000;}
  *{box-sizing:border-box;}
  body{margin:0;width:100vw;height:100vh;background:var(--bg);color:var(--text-color);font-family:"Inter","Helvetica Neue",Arial,sans-serif;overflow:hidden;display:flex;align-items:center;justify-content:center;position:relative;}
  .layer{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;pointer-events:none;}
  #message{position:absolute;top:32%;width:90%;max-width:320px;text-align:center;font-size:18px;letter-spacing:0.5px;opacity:0;transition:opacity var(--text-fade) linear;z-index:3;pointer-events:none;}
  #eyes{gap:var(--eye-gap);z-index:2;opacity:0;transition:opacity var(--eyes-fade) linear;}
  .eye{width:var(--eye-width);height:var(--eye-height);background:#fff;border-radius:16px;transform-origin:center;}
  .blink{animation:blink var(--blink-duration) ease-in-out forwards;}
  @keyframes blink{0%{transform:scaleY(1);}45%{transform:scaleY(0.08);}100%{transform:scaleY(1);}}
  #bubbles{position:absolute;inset:0;z-index:1;pointer-events:none;}
  .bubble{position:absolute;color:#fff;border-radius:50%;display:flex;align-items:center;justify-content:center;text-align:center;padding:6px;font-size:12px;opacity:0;transform:translateY(0);pointer-events:auto;overflow:hidden;}
  .bubble::after{content:"";position:absolute;width:80%;height:80%;background:#000;border-radius:50%;top:10%;left:10%;z-index:1;}
  .bubble span{position:relative;z-index:2;}
  .bubble.show{animation:bubbleFadeIn var(--bubble-fade) ease forwards,bubbleFloat var(--bubble-float) ease-in-out infinite alternate;}
  .bubble.nofloat{animation:bubbleFadeIn var(--bubble-fade) ease forwards!important;}
  .bubble.dim{opacity:0.3!important;}
  .bubble.selected{transition:transform 300ms ease,opacity 200ms linear;transform:translate(-50%,-50%) scale(1);left:50%!important;top:50%!important;z-index:4;}
  .bubble.pulsing{animation:bubblePulse var(--connect-pulse) ease-in-out infinite;}
  @keyframes bubbleFadeIn{from{opacity:0;}to{opacity:1;}}
  @keyframes bubbleFloat{from{transform:translateY(0);}to{transform:translateY(-10px);}}
  @keyframes bubblePulse{0%{transform:translate(-50%,-50%) scale(1);}50%{transform:translate(-50%,-50%) scale(1.15);}100%{transform:translate(-50%,-50%) scale(1);}}
  #form{position:absolute;top:65%;width:80%;max-width:320px;display:flex;flex-direction:column;gap:10px;opacity:0;pointer-events:none;transition:opacity 300ms ease;z-index:4;}
  #form.show{opacity:1;pointer-events:auto;}
  input,button{width:100%;padding:10px 12px;border:1px solid rgba(255,255,255,0.3);background:rgba(255,255,255,0.06);color:#fff;border-radius:6px;font-size:14px;}
  button{border:1px solid rgba(255,255,255,0.5);background:rgba(255,255,255,0.12);}
  #successText{position:absolute;top:18%;width:90%;max-width:320px;text-align:center;font-size:18px;letter-spacing:0.5px;opacity:0;z-index:3;pointer-events:none;}
  @keyframes eyesPop{0%{transform:scale(1);}50%{transform:scale(1.1);}100%{transform:scale(1);}}
</style>
</head>
<body>
  <div id="message">HELLO! ARE YOU GOING TO SHOW ME THE WORLD?</div>
  <div id="eyes" class="layer"><div class="eye"></div><div class="eye"></div></div>
  <div id="bubbles"></div>
  <div id="form"><input id="password" type="password" placeholder="Password (leave blank if open)"><button id="connectBtn" type="button">Connect</button></div>
  <div id="successText">WOW! THANKS!</div>
<script>
const TIMING={EYES_FADE_IN:800,BLINK_DURATION:160,BLINK_GAP:220,TEXT_FADE_IN:700,BUBBLE_FADE_IN:600,BUBBLE_FLOAT_PERIOD:6000,CONNECT_PULSE_PERIOD:900};
const COLORS=["#FF6B6B","#FFD93D","#6BCB77","#4D96FF","#9D4EDD","#F28482","#F7B801","#3AB0FF"];
document.documentElement.style.setProperty('--eyes-fade',TIMING.EYES_FADE_IN+'ms');document.documentElement.style.setProperty('--blink-duration',TIMING.BLINK_DURATION+'ms');document.documentElement.style.setProperty('--text-fade',TIMING.TEXT_FADE_IN+'ms');document.documentElement.style.setProperty('--bubble-fade',TIMING.BUBBLE_FADE_IN+'ms');document.documentElement.style.setProperty('--bubble-float',TIMING.BUBBLE_FLOAT_PERIOD+'ms');document.documentElement.style.setProperty('--connect-pulse',TIMING.CONNECT_PULSE_PERIOD+'ms');
const STATES={INTRO:'INTRO',EYES_VISIBLE:'EYES_VISIBLE',TEXT_VISIBLE:'TEXT_VISIBLE',WIFI_SELECTION:'WIFI_SELECTION',PASSWORD_INPUT:'PASSWORD_INPUT',CONNECTING:'CONNECTING',SUCCESS:'SUCCESS'};
let state=STATES.INTRO,bubbles=[],selectedBubble=null;
const eyesEl=document.getElementById('eyes'),messageEl=document.getElementById('message'),bubblesEl=document.getElementById('bubbles'),formEl=document.getElementById('form'),passwordEl=document.getElementById('password'),connectBtn=document.getElementById('connectBtn'),successTextEl=document.getElementById('successText');
const setState=s=>{console.log('State ->',s);state=s;};
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function runIntro(){setState(STATES.INTRO);eyesEl.style.opacity='1';await wait(TIMING.EYES_FADE_IN);setState(STATES.EYES_VISIBLE);await doBlinkSequence(2);messageEl.style.opacity='1';await wait(TIMING.TEXT_FADE_IN);setState(STATES.TEXT_VISIBLE);await startWifiSelection();}
async function doBlinkSequence(c){const eyes=[...document.querySelectorAll('.eye')];for(let i=0;i<c;i++){eyes.forEach(e=>{e.classList.remove('blink');void e.offsetWidth;e.classList.add('blink');});await wait(TIMING.BLINK_DURATION+TIMING.BLINK_GAP);}}
const mapRssiToSize=r=>{const min=-90,max=-30,cl=Math.max(min,Math.min(max,r||-80)),t=(cl-min)/(max-min);return 100+t*(200-100);};
async function fetchNetworks(){try{const r=await fetch('/scan');if(!r.ok)throw new Error('scan failed');const d=await r.json();return Array.isArray(d)?d:[]}catch(e){console.warn('Scan failed',e);return[]}}
function placeBubbles(nets){bubbles=[];bubblesEl.innerHTML='';const w=innerWidth,h=innerHeight,taken=[];const avoid={x1:w/2-140,x2:w/2+140,y1:h*0.18,y2:h*0.7};nets.slice(0,8).forEach((net,idx)=>{const size=mapRssiToSize(net.rssi),radius=size/2,margin=20;let x=0,y=0;if(idx===0){x=w/2;y=h*0.55;}else{let tries=0;do{x=margin+radius+Math.random()*(w-2*(margin+radius));y=margin+radius+Math.random()*(h-2*(margin+radius));tries++;}while(tries<20&&((x>avoid.x1&&x<avoid.x2&&y>avoid.y1&&y<avoid.y2)||taken.some(p=>{const dx=p.x-x,dy=p.y-y,dist=Math.sqrt(dx*dx+dy*dy);return dist<(p.r+radius+12);})));}taken.push({x,y,r:radius});const div=document.createElement('div');div.className='bubble';if(idx===0)div.classList.add('nofloat');div.style.background=COLORS[idx%COLORS.length];div.style.width=`${size}px`;div.style.height=`${size}px`;div.style.left=`${x-radius}px`;div.style.top=`${y-radius}px`;const label=document.createElement('span');label.textContent=net.ssid||'(hidden)';div.appendChild(label);div.dataset.ssid=net.ssid||'';div.dataset.rssi=net.rssi||'';bubblesEl.appendChild(div);bubbles.push(div);});}
function showBubbles(){bubbles.forEach(b=>{b.classList.add('show');b.addEventListener('click',()=>handleBubbleSelect(b));});}
async function startWifiSelection(){setState(STATES.WIFI_SELECTION);const nets=await fetchNetworks();nets.sort((a,b)=>(b.rssi||-999)-(a.rssi||-999));placeBubbles(nets.slice(0,8));if(bubbles.length>0){const b=bubbles[0];const rect=b.getBoundingClientRect();b.style.left=`calc(50% - ${rect.width/2}px)`;b.style.top=`calc(55% - ${rect.height/2}px)`;b.classList.add('show');b.removeEventListener('click',()=>{});}showBubbles();}
function handleBubbleSelect(el){if(state!==STATES.WIFI_SELECTION&&state!==STATES.PASSWORD_INPUT)return;selectedBubble=el;setState(STATES.PASSWORD_INPUT);bubbles.forEach(b=>{if(b!==el)b.classList.add('dim');});el.classList.add('selected');formEl.classList.add('show');passwordEl.focus();}
async function handleConnect(){if(!selectedBubble)return;setState(STATES.CONNECTING);selectedBubble.classList.add('pulsing');connectBtn.disabled=true;const payload={ssid:selectedBubble.dataset.ssid,pass:passwordEl.value||''};try{const res=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`ssid=${encodeURIComponent(payload.ssid)}&pass=${encodeURIComponent(payload.pass)}`});if(!res.ok)throw new Error('connect failed');await showSuccess();}catch(e){console.warn('Connect failed',e);connectBtn.disabled=false;selectedBubble.classList.remove('pulsing');setState(STATES.PASSWORD_INPUT);}}
async function showSuccess(){setState(STATES.SUCCESS);bubbles.forEach(b=>b.style.opacity='0');formEl.classList.remove('show');eyesEl.style.animation=`eyesPop 400ms ease`;await wait(400);eyesEl.style.animation='';messageEl.style.opacity='0';await wait(200);successTextEl.style.opacity='1';}
connectBtn.addEventListener('click',handleConnect);window.addEventListener('load',runIntro);
</script></body></html>)HTML";

  const char* stateToStr(WifiState s) {
    switch (s) {
      case WifiState::OFF: return "OFF";
      case WifiState::PROVISIONING: return "PROVISIONING";
      case WifiState::CONNECTING: return "CONNECTING";
      case WifiState::CONNECTED: return "CONNECTED";
      case WifiState::FAILED: return "FAILED";
      default: return "?";
    }
  }

  void setState(WifiState newState) {
    if (state == newState) return;
    state = newState;
    switch (state) {
      case WifiState::PROVISIONING:
        WIFI_PROV_LOG("WiFi: PROVISIONING (AP)");
        break;
      case WifiState::CONNECTING:
        if (WIFI_LOGS) WifiLog::println("WiFi: CONNECTING");
        break;
      case WifiState::CONNECTED:
        if (WIFI_LOGS) WifiLog::printf("WiFi: CONNECTED, IP=%s\n", ipStr.c_str());
        break;
      case WifiState::FAILED:
        if (WIFI_LOGS) WifiLog::println("WiFi: FAILED");
        break;
      case WifiState::OFF:
      default:
        break;
    }
  }

  void stopServers() {
    dnsServer.stop();
    webServer.stop();
    lastProvisionLogMs = 0;
  }

  void handleRoot() {
    webServer.send_P(200, "text/html", PORTAL_HTML);
  }

  void handleCaptive() {
    // Always send the portal page for captive portal probes
    webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    webServer.send(302, "text/plain", "Redirecting to setup...");
  }

  void handleSave() {
    String chosenSsid;
    if (webServer.hasArg("ssid_manual") && webServer.arg("ssid_manual").length() > 0) {
      chosenSsid = webServer.arg("ssid_manual");
    } else if (webServer.hasArg("ssid")) {
      chosenSsid = webServer.arg("ssid");
    }
    if (chosenSsid.length() == 0) {
      webServer.send(400, "text/plain", "SSID required");
      return;
    }
    targetSsid = chosenSsid;
    targetPass = webServer.hasArg("pass") ? webServer.arg("pass") : "";
    webServer.send(200, "text/plain", "Connecting...");
    stopServers();
    WiFi.softAPdisconnect(true);
    provisioning = false;
    lastProvisionLogMs = 0;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(targetSsid.c_str(), targetPass.c_str());
    // Persist as primary known network (slot 0)
    if (prefs.begin("wifi", false)) {
      prefs.putString("ssid0", targetSsid);
      prefs.putString("pass0", targetPass);
      prefs.end();
    }
    connectStartMs = millis();
    setState(WifiState::CONNECTING);
  }

  void handleScan() {
    String json = "[";
    size_t count = scannedSsids.size();
    for (size_t i = 0; i < count; ++i) {
      json += "{\"ssid\":\"";
      json += scannedSsids[i];
      json += "\",\"rssi\":";
      if (i < scannedRssi.size()) {
        json += scannedRssi[i];
      } else {
        json += 0;
      }
      json += "}";
      if (i + 1 < count) json += ",";
    }
    json += "]";
    webServer.send(200, "application/json", json);
  }

  void placeStrongestCenter() {
    if (scannedSsids.empty() || scannedRssi.empty()) return;
    // sort by RSSI descending (simple bubble placement override)
    int bestIdx = 0;
    for (size_t i = 1; i < scannedRssi.size(); ++i) {
      if (scannedRssi[i] > scannedRssi[bestIdx]) bestIdx = i;
    }
    // Move the strongest to front of list
    if (bestIdx != 0) {
      std::swap(scannedSsids[0], scannedSsids[bestIdx]);
      std::swap(scannedRssi[0], scannedRssi[bestIdx]);
    }
  }

  int findBestKnownInScan() {
    int bestIdx = -1;
    int bestRssi = -1000;
    for (size_t i = 0; i < scannedSsids.size(); ++i) {
      for (const auto& k : known) {
        if (scannedSsids[i] == k.ssid) {
          int rssi = (i < scannedRssi.size()) ? scannedRssi[i] : -1000;
          if (rssi > bestRssi) {
            bestRssi = rssi;
            bestIdx = static_cast<int>(i);
          }
        }
      }
    }
    return bestIdx;
  }

  void startProvisioning() {
    WIFI_PROV_LOG("WiFi: starting provisioning (AP-only)");
    stopServers();
    provisioning = false;
    lastProvisionLogMs = 0;

    // Stabilize driver: single init into AP+STA so we can scan before AP and keep STA running
    WiFi.persistent(true);  // allow credentials to be stored in NVS
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, false);  // disconnect but keep stored credentials
    bool modeOk = WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    delay(100);
    WIFI_PROV_LOGF("WiFi: mode set to AP_STA (ok=%d, mode=%d)\n", modeOk, WiFi.getMode());

    // Scan once for nearby SSIDs
    scannedSsids.clear();
    scannedRssi.clear();
    int n = WiFi.scanNetworks(false, true);
      WIFI_PROV_LOGF("WiFi: scan complete, found %d networks\n", n);
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length()) {
        scannedSsids.push_back(ssid);
        scannedRssi.push_back(WiFi.RSSI(i));
      }
    }
    WiFi.scanDelete();
    placeStrongestCenter();

    // If a known network is present, start STA connect in parallel
    int knownIdx = findBestKnownInScan();
    if (knownIdx >= 0) {
      String ssid = scannedSsids[knownIdx];
      String pass;
      for (const auto& k : known) {
        if (k.ssid == ssid) { pass = k.pass; break; }
      }
      WiFi.begin(ssid.c_str(), pass.c_str());
      connectStartMs = millis();
      setState(WifiState::CONNECTING);
    } else {
      setState(WifiState::PROVISIONING);
    }

    // Start AP for captive portal (stay in AP only, no STA/scan churn)
    delay(20);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress apGW(192, 168, 4, 1);
    IPAddress apMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGW, apMask);
    bool apOk = WiFi.softAP(AP_SSID, nullptr, 1, false, 4);
    if (!apOk) {
      WIFI_PROV_LOG("WiFi: softAP primary start failed, retrying with defaults");
      apOk = WiFi.softAP(AP_SSID);
    }
    if (!apOk) {
      WIFI_PROV_LOGF("WiFi: failed to start AP (mode=%d)\n", WiFi.getMode());
      setState(WifiState::FAILED);
      provisioning = false;
      return;
    }
    WIFI_PROV_LOGF("WiFi: AP started, IP=%s\n", WiFi.softAPIP().toString().c_str());

    dnsServer.start(53, "*", WiFi.softAPIP());
    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/scan", HTTP_GET, handleScan);
    // Common captive portal endpoints across platforms
    webServer.on("/generate_204", HTTP_GET, handleCaptive);
    webServer.on("/gen_204", HTTP_GET, handleCaptive);
    webServer.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
    webServer.on("/library/test/success.html", HTTP_GET, handleCaptive);
    webServer.on("/connecttest.txt", HTTP_GET, handleCaptive);
    webServer.on("/ncsi.txt", HTTP_GET, handleCaptive);
    webServer.onNotFound([]() { handleCaptive(); });
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.begin();

    provisioning = true;
    provisionStartMs = millis();
    lastProvisionLogMs = provisionStartMs;
    WIFI_PROV_LOG("WiFi: captive portal running");
  }
}  // namespace

void wifiInit() {
  state = WifiState::OFF;
  connectStartMs = 0;
  provisionStartMs = 0;
  lastProvisionLogMs = 0;
  ipStr = "";
  provisioning = false;
  autoConnectTried = false;
  allowProvisionFallback = true;
  known.clear();
  if (prefs.begin("wifi", true)) {
    String s0 = prefs.getString("ssid0", "");
    String p0 = prefs.getString("pass0", "");
    String s1 = prefs.getString("ssid1", "");
    String p1 = prefs.getString("pass1", "");
    if (s0.length()) known.push_back({s0, p0});
    if (s1.length()) known.push_back({s1, p1});
    prefs.end();
  }
  // Enable NVS persistence so credentials survive reboot
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
}

void wifiStart(bool allowProvisionFallbackOnFail) {
  allowProvisionFallback = allowProvisionFallbackOnFail;
  if (provisioning || state == WifiState::PROVISIONING || state == WifiState::CONNECTING || state == WifiState::CONNECTED) {
    return;
  }
  // Menu toggle -> provisioning only
  startProvisioning();
}

void wifiStop() {
  stopServers();
  WiFi.softAPdisconnect(true);
  // Disconnect but keep saved credentials (NVS)
  WiFi.disconnect(true, false);
  WiFi.scanDelete();
  provisioning = false;
  lastProvisionLogMs = 0;
  ipStr = "";
  autoConnectTried = false;
  allowProvisionFallback = true;
  autoScanInProgress = false;
  setState(WifiState::OFF);
}

void wifiUpdate() {
  uint32_t now = millis();
  if (autoScanInProgress) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      return;
    }
    autoScanInProgress = false;
    if (n <= 0) {
      WiFi.scanDelete();
      wifiStop();
      return;
    }
    String chosenSsid;
    String chosenPass;
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      for (const auto& k : known) {
        if (ssid == k.ssid) {
          chosenSsid = k.ssid;
          chosenPass = k.pass;
          break;
        }
      }
      if (chosenSsid.length()) break;
    }
    WiFi.scanDelete();
    if (chosenSsid.length() == 0) {
      wifiStop();
      return;
    }
    allowProvisionFallback = false;
    WiFi.begin(chosenSsid.c_str(), chosenPass.c_str());
    connectStartMs = millis();
    setState(WifiState::CONNECTING);
    return;
  }
  if (provisioning) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    // Monitor STA connect while portal runs
    if (state == WifiState::CONNECTING) {
      wl_status_t s = WiFi.status();
      if (s == WL_CONNECTED) {
        ipStr = WiFi.localIP().toString();
        setState(WifiState::CONNECTED);
      } else if (millis() - connectStartMs > CONNECT_TIMEOUT_MS) {
        if (allowProvisionFallback) {
          if (WIFI_LOGS) WifiLog::println("WiFi: connect timed out during provisioning");
          setState(WifiState::FAILED);
        } else {
          if (WIFI_LOGS) WifiLog::println("WiFi: connect timed out -> stopping");
          wifiStop();
        }
      }
    }
    if (now - lastProvisionLogMs > 5000) {
      WIFI_PROV_LOGF("WiFi: provisioning active (%lus)\n", (now - provisionStartMs) / 1000);
      lastProvisionLogMs = now;
    }
    if (now - provisionStartMs > PROVISION_TIMEOUT_MS) {
      wifiStop();
      setState(WifiState::FAILED);
    }
    return;
  }

  if (state == WifiState::CONNECTING) {
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
      ipStr = WiFi.localIP().toString();
      setState(WifiState::CONNECTED);
    } else if (millis() - connectStartMs > CONNECT_TIMEOUT_MS) {
      if (allowProvisionFallback) {
        if (WIFI_LOGS) WifiLog::println("WiFi: connect timed out -> provisioning");
        wifiStop();
        startProvisioning();
      } else {
        if (WIFI_LOGS) WifiLog::println("WiFi: connect timed out -> stopping");
        wifiStop();
      }
    }
  } else if (state == WifiState::FAILED && !provisioning && !known.empty()) {
    // Try to reconnect to a known network if one appears after a drop
    wifiAutoConnectKnown();
  }
}

WifiState wifiGetState() {
  return state;
}

bool wifiIsProvisioning() {
  return provisioning;
}

const char* wifiGetIp() {
  return (state == WifiState::CONNECTED) ? ipStr.c_str() : "";
}

void wifiAutoConnectKnown() {
  if (provisioning || state == WifiState::CONNECTING || state == WifiState::CONNECTED) return;
  if (known.empty()) return;
  if (autoScanInProgress) return;

  // One quick scan to see if any known SSID is visible
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  int n = WiFi.scanNetworks(true, true);
  if (n == WIFI_SCAN_FAILED) {
    wifiStop();
    return;
  }
  autoScanInProgress = true;
}
