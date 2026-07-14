#pragma once
// ============================================================
//  WebDashboard.h – Device-centric dashboard  v2.2.0
//  Served from PROGMEM, ESP32 built-in WebServer (synchronous)
//
//  API endpoint changes vs v2.1:
//    PUT  /api/device       → POST /api/device/update?idx=N
//    DELETE /api/device     → POST /api/device/delete?idx=N
//    DELETE /api/mappings   → POST /api/mappings/delete?idx=N
//    PUT  /api/system       → POST /api/system/update
// ============================================================
#include <Arduino.h>
#include <WebServer.h>
#include "Config.h"

extern Config config;

// ─────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modbus Gateway Login</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#0d1117;color:#e6edf3;font-family:'Segoe UI',sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;
      padding:40px;width:360px;box-shadow:0 8px 32px #0008}
.logo{text-align:center;margin-bottom:28px}
.logo svg{width:40px;height:40px;margin-bottom:8px}
.logo h2{color:#58a6ff;font-size:1.2rem;letter-spacing:2px;text-transform:uppercase}
.logo p{color:#8b949e;font-size:.82rem;margin-top:4px}
label{display:block;margin-bottom:5px;color:#8b949e;font-size:.82rem}
input{width:100%;background:#0d1117;border:1px solid #30363d;border-radius:6px;
      color:#e6edf3;padding:10px 14px;font-size:.92rem;margin-bottom:16px}
input:focus{outline:none;border-color:#58a6ff}
button{width:100%;background:#1f6feb;border:none;border-radius:6px;
       color:#fff;padding:12px;font-size:.95rem;cursor:pointer;transition:.2s}
button:hover{background:#388bfd}
.err{color:#f85149;font-size:.82rem;margin-bottom:12px;display:none;
     background:#3a1a1a;padding:8px 10px;border-radius:6px}
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <svg viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <rect x="2" y="8" width="36" height="24" rx="3" stroke="#58a6ff" stroke-width="2"/>
      <line x1="2" y1="16" x2="38" y2="16" stroke="#58a6ff" stroke-width="1.5"/>
      <circle cx="8" cy="12" r="2" fill="#3fb950"/>
      <circle cx="15" cy="12" r="2" fill="#d29922"/>
      <rect x="8" y="20" width="24" height="3" rx="1" fill="#30363d"/>
      <rect x="8" y="26" width="16" height="3" rx="1" fill="#30363d"/>
    </svg>
    <h2>Modbus Gateway</h2>
    <p>SysCon Solutions v2.2</p>
  </div>
  <div class="err" id="err">Invalid username or password</div>
  <label>Username</label>
  <input type="text" id="u" value="admin" autocomplete="off">
  <label>Password</label>
  <input type="password" id="p" placeholder="••••••••">
  <button onclick="doLogin()">Sign In →</button>
</div>
<script>
function doLogin(){
  fetch('/api/login',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({user:document.getElementById('u').value,
                         pass:document.getElementById('p').value})})
  .then(r=>r.json()).then(d=>{
    if(d.ok){sessionStorage.setItem('tok',d.token);location.href='/dashboard';}
    else{document.getElementById('err').style.display='block';}
  }).catch(()=>{document.getElementById('err').style.display='block';});
}
document.addEventListener('keydown',e=>{if(e.key==='Enter')doLogin();});
</script>
</body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────
static const char DASH_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modbus Gateway</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{
  --bg:#0d1117;--sf:#161b22;--bd:#30363d;--tx:#e6edf3;--mu:#8b949e;
  --bl:#58a6ff;--gn:#3fb950;--rd:#f85149;--yw:#d29922;--or:#db6d28;
  --sf2:#1c2128
}
body{background:var(--bg);color:var(--tx);font-family:'Segoe UI',Consolas,sans-serif;
     display:flex;min-height:100vh;font-size:14px}
/* ── Sidebar ── */
.sidebar{width:210px;background:var(--sf);border-right:1px solid var(--bd);
         display:flex;flex-direction:column;flex-shrink:0;position:fixed;
         top:0;left:0;height:100vh;z-index:50}
.brand{padding:18px 16px 14px;border-bottom:1px solid var(--bd)}
.brand h3{color:var(--bl);font-size:.95rem;letter-spacing:1px;font-weight:700}
.brand p{color:var(--mu);font-size:.7rem;margin-top:3px}
nav a{display:flex;align-items:center;gap:10px;padding:11px 16px;
      color:var(--mu);text-decoration:none;font-size:.88rem;cursor:pointer;
      border-left:3px solid transparent;transition:.12s;user-select:none}
nav a:hover,nav a.active{color:var(--tx);background:var(--sf2);border-left-color:var(--bl)}
.sb-bot{margin-top:auto;padding:14px;border-top:1px solid var(--bd)}
.sb-bot button{width:100%;background:transparent;border:1px solid var(--bd);
               color:var(--mu);padding:7px;border-radius:6px;cursor:pointer;font-size:.82rem}
.sb-bot button:hover{border-color:var(--rd);color:var(--rd)}
/* ── Main ── */
.main{flex:1;overflow-y:auto;padding:24px;margin-left:210px}
.page{display:none}.page.active{display:block}
h1{font-size:1.2rem;font-weight:600;margin-bottom:18px;
   border-bottom:1px solid var(--bd);padding-bottom:10px;
   display:flex;align-items:center;gap:10px}
/* ── Device cards grid ── */
.dev-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));
          gap:16px;margin-bottom:24px}
.dev-card{background:var(--sf);border:1px solid var(--bd);border-radius:10px;
          padding:16px;position:relative;overflow:hidden}
.dev-card::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;
                  background:var(--bl)}
.dev-card.ok::before{background:var(--gn)}
.dev-card.err::before{background:var(--rd)}
.dev-card.off::before{background:var(--mu)}
.dev-hdr{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:10px}
.dev-name{font-weight:600;font-size:.95rem;color:var(--tx)}
.dev-meta{color:var(--mu);font-size:.75rem;margin-top:2px}
.dev-regs{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}
.dev-reg{background:var(--sf2);border-radius:6px;padding:8px 10px}
.dev-reg .reg-lbl{color:var(--mu);font-size:.72rem;text-transform:uppercase;
                  letter-spacing:.4px;margin-bottom:3px}
.dev-reg .reg-val{font-size:1.15rem;font-weight:700;color:var(--bl)}
.dev-reg .reg-unit{color:var(--mu);font-size:.75rem;margin-left:3px;font-weight:400}
.dev-reg.ok .reg-val{color:var(--gn)}
.dev-reg.err .reg-val{color:var(--rd)}
/* ── Status bar ── */
.status-bar{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:20px}
.stat-chip{background:var(--sf);border:1px solid var(--bd);border-radius:8px;
           padding:8px 14px;display:flex;align-items:center;gap:8px;font-size:.82rem}
.stat-chip .sv{font-weight:700;color:var(--bl);font-size:.95rem}
.stat-chip .sl{color:var(--mu)}
/* ── Shared table wrapper ── */
.tw{background:var(--sf);border:1px solid var(--bd);border-radius:10px;overflow:auto}
table{width:100%;border-collapse:collapse}
th{background:var(--sf2);padding:9px 14px;text-align:left;font-size:.75rem;
   color:var(--mu);text-transform:uppercase;letter-spacing:.4px;
   border-bottom:1px solid var(--bd);white-space:nowrap}
td{padding:9px 14px;border-bottom:1px solid var(--sf2);font-size:.85rem;vertical-align:middle}
tr:last-child td{border-bottom:none}
tr:hover td{background:var(--sf2)}
/* ── Form layout ── */
.fr{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
.fr.f1{grid-template-columns:1fr}
.fl label{display:block;color:var(--mu);font-size:.78rem;margin-bottom:4px}
.fl input,.fl select{width:100%;background:#0d1117;border:1px solid var(--bd);
  border-radius:6px;color:var(--tx);padding:8px 11px;font-size:.88rem}
.fl input:focus,.fl select:focus{outline:none;border-color:var(--bl)}
/* ── Buttons ── */
.btn{background:#1f6feb;border:none;border-radius:6px;color:#fff;
     padding:8px 16px;cursor:pointer;font-size:.88rem;transition:.18s;white-space:nowrap}
.btn:hover{background:#388bfd}
.btn.d{background:#3a1a1a;color:var(--rd);border:1px solid #6e2020}
.btn.d:hover{background:var(--rd);color:#fff}
.btn.s{padding:4px 10px;font-size:.78rem}
.btn.g{background:#1a3a2a;color:var(--gn);border:1px solid #1a4a2a}
.btn.g:hover{background:var(--gn);color:#000}
/* ── Toolbar ── */
.tb{display:flex;gap:10px;margin-bottom:14px;align-items:center;flex-wrap:wrap}
.tb input[type=text]{background:#0d1117;border:1px solid var(--bd);
  border-radius:6px;color:var(--tx);padding:7px 12px;font-size:.85rem;min-width:200px}
/* ── Badge ── */
.bx{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.75rem;font-weight:600}
.bx.ok{background:#1a3a2a;color:var(--gn)}.bx.er{background:#3a1a1a;color:var(--rd)}
.bx.wa{background:#3a2a00;color:var(--yw)}
/* ── Modal ── */
.modal{display:none;position:fixed;inset:0;background:#000a;
       align-items:center;justify-content:center;z-index:100}
.mbox{background:#161b22;border:1px solid #30363d;border-radius:10px;
      padding:26px;width:500px;max-width:95vw;max-height:90vh;overflow-y:auto}
.mbox h2{font-size:.95rem;margin-bottom:16px;padding-bottom:10px;
         border-bottom:1px solid var(--bd)}
/* ── Misc ── */
.cc{background:var(--sf);border:1px solid var(--bd);border-radius:10px;padding:18px;margin-bottom:18px}
.empty{text-align:center;padding:40px 20px;color:var(--mu);font-size:.88rem}
.toast{position:fixed;bottom:24px;right:24px;background:#1f6feb;color:#fff;
       padding:10px 18px;border-radius:8px;font-size:.85rem;z-index:200;
       opacity:0;transition:opacity .3s;pointer-events:none}
.toast.ok-t{background:#1a3a2a;color:var(--gn);border:1px solid #1a4a2a}
.toast.err-t{background:#3a1a1a;color:var(--rd);border:1px solid #6e2020}
</style>
</head>
<body>

<div class="sidebar">
  <div class="brand"><h3>▶ MODBUS GW</h3><p>CET Power Solutions • v2.2</p></div>
  <nav>
    <a class="active" onclick="nav('home',this)">⬛ Devices</a>
    <a onclick="nav('live',this)">◯ Live Data</a>
    <a onclick="nav('mappings',this)">⊙ Register Map</a>
    <a onclick="nav('history',this)">▶ History</a>
    <a onclick="nav('tcpset',this)">▾ TCP Settings</a>
    <a onclick="nav('sysset',this)">⚙ System</a>
  </nav>
  <div class="sb-bot"><button onclick="logout()">✕ Logout</button></div>
</div>

<div class="main">

<!-- HOME – Device Cards + live register values -->
<div class="page active" id="pg-home">
  <h1>
    <span>Devices</span>
    <button class="btn" style="margin-left:auto;font-size:.82rem" onclick="openDev()">+ Add Device</button>
  </h1>
  <!-- Gateway status chips -->
  <div class="status-bar" id="statusBar">
    <div class="stat-chip"><span class="sl">TCP</span><span class="sv ok" id="sTCPPort">502</span></div>
    <div class="stat-chip"><span class="sl">Clients</span><span class="sv" id="sTCPCli">0</span></div>
    <div class="stat-chip"><span class="sl">WiFi</span><span class="sv" id="sRSSI">—</span></div>
    <div class="stat-chip"><span class="sl">Heap</span><span class="sv" id="sHeap">—</span></div>
    <div class="stat-chip"><span class="sl">Uptime</span><span class="sv" id="sUptime">—</span></div>
    <div class="stat-chip">
      <span id="wsStatDot" style="font-size:.65rem;color:var(--rd)">●</span>
      <span class="sl" id="wsStatLbl">WS Disconnected</span>
    </div>
  </div>
  <!-- Device cards populated by JS -->
  <div class="dev-grid" id="devGrid">
    <div class="empty">Loading devices…</div>
  </div>
</div>

<!-- LIVE DATA -->
<div class="page" id="pg-live">
  <h1>Live Register Data</h1>
  <div class="tb">
    <input type="text" id="lsrch" placeholder="Search register…" oninput="filtLive()">
    <span id="wsState2" style="color:var(--rd);font-size:.82rem">● Disconnected</span>
  </div>
  <div class="tw"><table>
    <thead><tr><th>Parameter</th><th>Slave</th><th>Addr</th>
      <th>Value</th><th>Unit</th><th>Timestamp</th><th>Status</th></tr></thead>
    <tbody id="liveTb"></tbody>
  </table></div>
</div>

<!-- REGISTER MAP -->
<div class="page" id="pg-mappings">
  <h1>
    <span>Register Mappings</span>
    <button class="btn" style="margin-left:auto;font-size:.82rem" onclick="openMap()">+ Add Mapping</button>
  </h1>
  <div class="tw"><table>
    <thead><tr><th>Name</th><th>Slave</th><th>FC</th><th>Address</th>
      <th>Type</th><th>Unit</th><th></th></tr></thead>
    <tbody id="mapTb"></tbody>
  </table></div>
  <div class="modal" id="mapModal">
    <div class="mbox" style="width:420px">
      <h2>Add Register Mapping</h2>
      <input type="hidden" id="mIdx" value="-1">
      <div class="fr">
        <div class="fl"><label>Name</label><input id="mNm" placeholder="Output Voltage"></div>
        <div class="fl"><label>Unit</label><input id="mUn" placeholder="V / A / kWh"></div>
      </div>
      <div class="fr">
        <div class="fl"><label>Slave ID</label>
          <input id="mSl" type="number" min="1" max="247" value="1"></div>
        <div class="fl"><label>Function Code</label>
          <select id="mFC">
            <option value="1">FC01 – Coils</option>
            <option value="2">FC02 – Discrete</option>
            <option value="3" selected>FC03 – Holding</option>
            <option value="4">FC04 – Input</option>
          </select></div>
      </div>
      <div class="fr">
        <div class="fl"><label>Register Address</label>
          <input id="mAd" type="number" min="0" value="0"></div>
        <div class="fl"><label>Data Type</label>
          <select id="mDT">
            <option>UInt16</option><option>Int16</option>
            <option>UInt32</option><option>Int32</option>
            <option>Float</option><option>FloatSwap</option><option>Double</option>
          </select></div>
      </div>
      <div style="display:flex;gap:10px;margin-top:8px">
        <button class="btn" onclick="saveMap()">Save</button>
        <button class="btn d" onclick="closeMap()">Cancel</button>
      </div>
    </div>
  </div>
</div>

<!-- HISTORY -->
<div class="page" id="pg-history">
  <h1>Historical Data</h1>
  <div class="tb">
    <select id="hPar" style="background:#0d1117;border:1px solid var(--bd);
      color:var(--tx);padding:7px 12px;border-radius:6px;font-size:.85rem">
      <option>— select register —</option></select>
    <select id="hRng" style="background:#0d1117;border:1px solid var(--bd);
      color:var(--tx);padding:7px 12px;border-radius:6px;font-size:.85rem">
      <option value="1h">1 Hour</option>
      <option value="24h" selected>24 Hours</option>
      <option value="7d">7 Days</option>
    </select>
    <button class="btn" onclick="loadHist()">Load</button>
    <button class="btn g" onclick="expCSV()">↓ CSV</button>
  </div>
  <div class="cc"><canvas id="histChart" height="120"></canvas></div>
</div>

<!-- TCP SETTINGS -->
<div class="page" id="pg-tcpset">
  <h1>Modbus TCP Settings</h1>
  <div style="max-width:460px">
    <div class="fr f1"><div class="fl"><label>TCP Port</label>
      <input id="tPort" type="number" value="502"></div></div>
    <div class="fr">
      <div class="fl"><label>Max Clients</label>
        <input id="tMaxC" type="number" value="10"></div>
      <div class="fl"><label>Idle Timeout (ms)</label>
        <input id="tTout" type="number" value="5000"></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Poll Rate (ms)</label>
        <input id="tPoll" type="number" value="1000"></div>
      <div class="fl"><label>RS485 Baud Rate</label>
        <select id="tBaud">
          <option>9600</option><option>19200</option>
          <option>38400</option><option>57600</option><option>115200</option>
        </select></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Parity</label>
        <select id="tPar">
          <option value="N">None</option>
          <option value="E">Even</option>
          <option value="O">Odd</option>
        </select></div>
      <div class="fl"><label>Stop Bits</label>
        <select id="tStop">
          <option value="1">1</option>
          <option value="2">2</option>
        </select></div>
    </div>
    <button class="btn" onclick="saveTCP()">Save &amp; Reboot</button>
  </div>
</div>

<!-- SYSTEM -->
<div class="page" id="pg-sysset">
  <h1>System Settings</h1>
  <div style="max-width:460px">
    <div class="fr">
      <div class="fl"><label>Web Username</label><input id="sUsr"></div>
      <div class="fl"><label>New Password</label>
        <input id="sPwd" type="password" placeholder="(leave blank to keep)"></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Wi-Fi SSID (STA mode)</label>
        <input id="sSSID" placeholder="Home or office network"></div>
      <div class="fl"><label>Wi-Fi Password</label>
        <input id="sWPwd" type="password"></div>
    </div>
    <div class="fr">
      <div class="fl"><label>AP SSID</label>
        <input id="sAPSSID" placeholder="ModbusGW-ESP32"></div>
      <div class="fl"><label>AP Password</label>
        <input id="sAPPwd" type="password" placeholder="(min 8 chars)"></div>
    </div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:14px">
      <button class="btn" onclick="saveSys()">Save Settings</button>
      <button class="btn" onclick="doReboot()">Reboot</button>
      <button class="btn d" onclick="doFactory()">Factory Reset</button>
      <button class="btn g" onclick="doBackup()">↓ Backup</button>
    </div>
    <hr style="border-color:var(--bd);margin:20px 0">
    <h2 style="font-size:.9rem;margin-bottom:10px;font-weight:600">OTA Firmware Update</h2>
    <input type="file" id="otaF" accept=".bin"
           style="color:var(--mu);font-size:.82rem;margin-bottom:10px;display:block">
    <button class="btn" onclick="doOTA()">Upload Firmware</button>
    <div id="otaProg" style="display:none;margin-top:12px">
      <div style="background:#0d1117;border-radius:4px;height:8px;overflow:hidden">
        <div id="otaBar" style="background:var(--bl);height:100%;width:0%;transition:.3s"></div>
      </div>
      <div id="otaMsg" style="color:var(--mu);font-size:.78rem;margin-top:6px">Uploading…</div>
    </div>
  </div>
</div>

</div><!-- /main -->

<!-- Device Add/Edit Modal (shared) -->
<div class="modal" id="devModal">
  <div class="mbox">
    <h2 id="devModalTitle">Add Device</h2>
    <input type="hidden" id="dIdx" value="-1">
    <div class="fr">
      <div class="fl"><label>Device Name</label>
        <input id="dNm" placeholder="UPS Main"></div>
      <div class="fl"><label>Slave ID</label>
        <input id="dSl" type="number" min="1" max="247" value="1"></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Baud Rate</label>
        <select id="dBd">
          <option>9600</option><option>19200</option>
          <option>38400</option><option>57600</option><option>115200</option>
        </select></div>
      <div class="fl"><label>Parity</label>
        <select id="dPr">
          <option value="N">None</option>
          <option value="E">Even</option>
          <option value="O">Odd</option>
        </select></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Function Code</label>
        <select id="dFC">
          <option value="1">FC01 – Coils</option>
          <option value="2">FC02 – Discrete</option>
          <option value="3" selected>FC03 – Holding</option>
          <option value="4">FC04 – Input</option>
        </select></div>
      <div class="fl"><label>Data Type</label>
        <select id="dDT">
          <option>UInt16</option><option>Int16</option>
          <option>UInt32</option><option>Int32</option>
          <option>Float</option><option>FloatSwap</option><option>Double</option>
        </select></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Start Register Address</label>
        <input id="dAd" type="number" min="0" value="0"></div>
      <div class="fl"><label>Register Count</label>
        <input id="dCo" type="number" min="1" max="125" value="1"></div>
    </div>
    <div class="fr">
      <div class="fl"><label>Poll Interval (ms)</label>
        <input id="dPl" type="number" min="100" value="1000"></div>
      <div class="fl"><label>Enabled</label>
        <select id="dEn">
          <option value="1">Yes</option>
          <option value="0">No</option>
        </select></div>
    </div>
    <div style="display:flex;gap:10px;margin-top:8px">
      <button class="btn" onclick="saveDev()">Save Device</button>
      <button class="btn d" onclick="closeDev()">Cancel</button>
    </div>
  </div>
</div>

<!-- Toast -->
<div class="toast" id="toast"></div>

<script>
const tok = () => sessionStorage.getItem('tok') || '';
const ah  = () => ({'Content-Type':'application/json','X-Token':tok()});

// ── Toast ─────────────────────────────────────────────────────
function toast(msg, type='ok'){
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className   = 'toast ' + (type==='ok'?'ok-t':'err-t');
  t.style.opacity = 1;
  setTimeout(() => t.style.opacity = 0, 2500);
}

// ── Navigation ────────────────────────────────────────────────
function nav(id, el){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('nav a').forEach(a => a.classList.remove('active'));
  document.getElementById('pg-'+id).classList.add('active');
  el.classList.add('active');
  if(id==='home')     { loadDevs(); }
  if(id==='mappings') { loadMaps(); }
  if(id==='tcpset')   { loadTCP(); }
  if(id==='sysset')   { loadSys(); }
}

// ── WebSocket ─────────────────────────────────────────────────
let ws, liveData=[], liveUnits={};
function wsConn(){
  ws = new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen  = () => setWsState(true);
  ws.onclose = () => { setWsState(false); setTimeout(wsConn,3000); };
  ws.onmessage = e => {
    const d = JSON.parse(e.data);
    liveData = d.registers || [];
    // Update status bar
    document.getElementById('sTCPPort').textContent = (d.tcpPort||502);
    document.getElementById('sTCPCli').textContent  = d.tcpClients || 0;
    document.getElementById('sRSSI').textContent    = (d.rssi||'—')+' dBm';
    document.getElementById('sHeap').textContent    = fmtBytes(d.freeHeap||0);
    document.getElementById('sUptime').textContent  = fmtUptime(d.uptime||0);
    // Refresh device card live values if on home
    if(document.getElementById('pg-home').classList.contains('active'))
      updateDevCards();
    // Refresh live table
    if(document.getElementById('pg-live').classList.contains('active'))
      updLive();
  };
}
function setWsState(ok){
  document.getElementById('wsStatDot').style.color = ok ? 'var(--gn)' : 'var(--rd)';
  document.getElementById('wsStatLbl').textContent = ok ? 'WS Live' : 'WS Disconnected';
  const el2 = document.getElementById('wsState2');
  if(el2){ el2.textContent = ok ? '● Live' : '● Disconnected';
           el2.style.color = ok ? 'var(--gn)' : 'var(--rd)'; }
}

// ── Helpers ───────────────────────────────────────────────────
function fmtBytes(b){
  if(b>1048576) return (b/1048576).toFixed(1)+' MB';
  if(b>1024)    return (b/1024).toFixed(1)+' KB';
  return b+' B';
}
function fmtUptime(s){
  if(s<60)    return s+'s';
  if(s<3600)  return Math.floor(s/60)+'m '+( s%60)+'s';
  return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m';
}

// ── Devices (home page) ───────────────────────────────────────
let _dArr = [], _mArr = [];

function loadDevs(){
  // Load devices and mappings in parallel for the home cards
  Promise.all([
    fetch('/api/device',   {headers:ah()}).then(r=>r.json()),
    fetch('/api/mappings', {headers:ah()}).then(r=>r.json())
  ]).then(([dd, mm]) => {
    _dArr  = dd.devices  || [];
    _mArr  = mm.mappings || [];
    // Build unit lookup from mappings: key = slaveId+':'+address
    liveUnits = {};
    _mArr.forEach(m => { liveUnits[m.slaveId+':'+m.address] = m.unit || ''; });
    renderDevCards();
  }).catch(err => {
    document.getElementById('devGrid').innerHTML =
      '<div class="empty">Failed to load devices</div>';
  });
}

function renderDevCards(){
  const grid = document.getElementById('devGrid');
  if(!_dArr.length){
    grid.innerHTML = '<div class="empty">No devices configured. Click "+ Add Device" to start.</div>';
    return;
  }
  grid.innerHTML = '';
  _dArr.forEach((d, i) => {
    // Find all mappings for this slave
    const devMaps = _mArr.filter(m => m.slaveId === d.slaveId);
    // Build register value tiles (up to 6)
    const regTiles = devMaps.slice(0,6).map(m => {
      const live = liveData.find(r => r.slaveId===m.slaveId && r.address===m.address);
      const val  = live ? parseFloat(live.value).toFixed(2) : '—';
      const ok   = live ? live.ok : null;
      const cls  = ok===true?'ok':ok===false?'err':'';
      const unit = m.unit ? `<span class="reg-unit">${m.unit}</span>` : '';
      return `<div class="dev-reg ${cls}">
        <div class="reg-lbl">${m.name}</div>
        <div class="reg-val">${val}${unit}</div>
      </div>`;
    }).join('');

    // Device card status
    const anyFail = devMaps.some(m => {
      const live = liveData.find(r => r.slaveId===m.slaveId && r.address===m.address);
      return live && !live.ok;
    });
    const anyOk   = devMaps.some(m => {
      const live = liveData.find(r => r.slaveId===m.slaveId && r.address===m.address);
      return live && live.ok;
    });
    const cardCls = !d.enabled ? 'off' : anyFail ? 'err' : anyOk ? 'ok' : '';
    const stLbl   = !d.enabled ? 'Disabled' : anyFail ? 'Comm Error' : anyOk ? 'Online' : 'Waiting';
    const stCls   = !d.enabled ? 'wa' : anyFail ? 'er' : anyOk ? 'ok' : 'wa';

    const card = document.createElement('div');
    card.className = `dev-card ${cardCls}`;
    card.innerHTML = `
      <div class="dev-hdr">
        <div>
          <div class="dev-name">${d.name}</div>
          <div class="dev-meta">Slave ${d.slaveId} · FC0${d.fc} · ${d.baudRate} ${d.parity}${d.stopBits}</div>
        </div>
        <div style="display:flex;flex-direction:column;align-items:flex-end;gap:6px">
          <span class="bx ${stCls}">${stLbl}</span>
          <div style="display:flex;gap:4px">
            <button class="btn s" onclick="openDev(${i})">Edit</button>
            <button class="btn s d" onclick="delDev(${i})">Del</button>
          </div>
        </div>
      </div>
      ${devMaps.length ? `<div class="dev-regs">${regTiles}</div>` :
        `<div style="color:var(--mu);font-size:.78rem;margin-top:8px">
          No register mappings for Slave ID ${d.slaveId}
          <a href="#" onclick="nav('mappings',document.querySelector('[onclick*=mappings]'));return false"
             style="color:var(--bl);margin-left:4px">Add mappings →</a>
        </div>`}
    `;
    grid.appendChild(card);
  });
}

function updateDevCards(){
  // Lightweight live-value refresh without full DOM rebuild
  if(!_dArr.length) return;
  renderDevCards();
}

// ── Device Modal ──────────────────────────────────────────────
function openDev(i=-1){
  document.getElementById('dIdx').value = i;
  document.getElementById('devModalTitle').textContent = i>=0 ? 'Edit Device' : 'Add Device';
  if(i>=0 && i<_dArr.length){
    const v = _dArr[i];
    document.getElementById('dNm').value = v.name;
    document.getElementById('dSl').value = v.slaveId;
    document.getElementById('dBd').value = v.baudRate;
    document.getElementById('dPr').value = v.parity;
    document.getElementById('dFC').value = v.fc;
    document.getElementById('dDT').value = v.dataType;
    document.getElementById('dAd').value = v.regAddress;
    document.getElementById('dCo').value = v.regCount;
    document.getElementById('dPl').value = v.pollInterval;
    document.getElementById('dEn').value = v.enabled ? '1' : '0';
  } else {
    document.getElementById('dNm').value = '';
    document.getElementById('dSl').value = 1;
    document.getElementById('dBd').value = 9600;
    document.getElementById('dPr').value = 'N';
    document.getElementById('dFC').value = 3;
    document.getElementById('dDT').value = 'UInt16';
    document.getElementById('dAd').value = 0;
    document.getElementById('dCo').value = 1;
    document.getElementById('dPl').value = 1000;
    document.getElementById('dEn').value = '1';
  }
  document.getElementById('devModal').style.display = 'flex';
}
function closeDev(){ document.getElementById('devModal').style.display = 'none'; }

function saveDev(){
  const idx = parseInt(document.getElementById('dIdx').value);
  const body = {
    name:         document.getElementById('dNm').value.trim(),
    slaveId:      parseInt(document.getElementById('dSl').value),
    baudRate:     parseInt(document.getElementById('dBd').value),
    parity:       document.getElementById('dPr').value,
    stopBits:     1,
    fc:           parseInt(document.getElementById('dFC').value),
    dataType:     document.getElementById('dDT').value,
    regAddress:   parseInt(document.getElementById('dAd').value),
    regCount:     parseInt(document.getElementById('dCo').value),
    pollInterval: parseInt(document.getElementById('dPl').value),
    enabled:      document.getElementById('dEn').value === '1'
  };
  if(!body.name){ toast('Device name is required','err'); return; }

  // FIX: Use /api/device/update?idx=N for edit (replaces broken PUT)
  //      Use /api/device (POST) for new
  const url = idx >= 0
    ? '/api/device/update?idx=' + idx
    : '/api/device';

  fetch(url, {method:'POST', headers:ah(), body:JSON.stringify(body)})
    .then(r => r.json())
    .then(d => {
      if(d.ok){ closeDev(); loadDevs(); toast('Device saved'); }
      else     { toast(d.error||'Save failed','err'); }
    })
    .catch(() => toast('Network error','err'));
}

function delDev(i){
  if(!confirm('Delete device "' + _dArr[i].name + '"?')) return;
  // FIX: Use /api/device/delete?idx=N (replaces broken DELETE)
  fetch('/api/device/delete?idx='+i, {method:'POST', headers:ah()})
    .then(() => { loadDevs(); toast('Device deleted'); })
    .catch(() => toast('Delete failed','err'));
}

// ── Mappings ──────────────────────────────────────────────────
function loadMaps(){
  fetch('/api/mappings', {headers:ah()}).then(r=>r.json()).then(d => {
    _mArr = d.mappings || [];
    const tb = document.getElementById('mapTb');
    tb.innerHTML = '';
    if(!_mArr.length){
      tb.innerHTML = '<tr><td colspan="7" class="empty">No mappings. Click "+ Add Mapping".</td></tr>';
      return;
    }
    _mArr.forEach((m, i) => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${m.name}</td><td>${m.slaveId}</td><td>FC0${m.fc}</td>
        <td>${m.address}</td><td>${m.dataType}</td><td>${m.unit||'—'}</td>
        <td><button class="btn s d" onclick="delMap(${i})">Delete</button></td>`;
      tb.appendChild(tr);
    });
    // Populate history selector
    const sel = document.getElementById('hPar');
    sel.innerHTML = '<option>— select register —</option>';
    _mArr.forEach(m => {
      const o = document.createElement('option');
      o.textContent = m.name;
      sel.appendChild(o);
    });
  });
}
function openMap(){
  document.getElementById('mNm').value = '';
  document.getElementById('mUn').value = '';
  document.getElementById('mSl').value = 1;
  document.getElementById('mFC').value = 3;
  document.getElementById('mAd').value = 0;
  document.getElementById('mDT').value = 'UInt16';
  document.getElementById('mapModal').style.display = 'flex';
}
function closeMap(){ document.getElementById('mapModal').style.display = 'none'; }
function saveMap(){
  const body = {
    name:     document.getElementById('mNm').value.trim(),
    slaveId:  parseInt(document.getElementById('mSl').value),
    fc:       parseInt(document.getElementById('mFC').value),
    address:  parseInt(document.getElementById('mAd').value),
    dataType: document.getElementById('mDT').value,
    unit:     document.getElementById('mUn').value.trim()
  };
  if(!body.name){ toast('Register name is required','err'); return; }
  fetch('/api/mappings', {method:'POST', headers:ah(), body:JSON.stringify(body)})
    .then(r => r.json())
    .then(d => {
      if(d.ok){ closeMap(); loadMaps(); toast('Mapping added'); }
      else     { toast(d.error||'Save failed','err'); }
    })
    .catch(() => toast('Network error','err'));
}
function delMap(i){
  if(!confirm('Delete mapping "' + (_mArr[i]?.name||i) + '"?')) return;
  // FIX: Use /api/mappings/delete?idx=N (replaces broken DELETE)
  fetch('/api/mappings/delete?idx='+i, {method:'POST', headers:ah()})
    .then(() => { loadMaps(); toast('Mapping deleted'); })
    .catch(() => toast('Delete failed','err'));
}

// ── Live table ────────────────────────────────────────────────
function updLive(){
  const q  = (document.getElementById('lsrch').value||'').toLowerCase();
  const tb = document.getElementById('liveTb');
  tb.innerHTML = '';
  if(!liveData.length){
    tb.innerHTML = '<tr><td colspan="7" class="empty">No live data yet — waiting for WebSocket…</td></tr>';
    return;
  }
  liveData.filter(r => r.name.toLowerCase().includes(q)).forEach(r => {
    const unit = liveUnits[r.slaveId+':'+r.address] || '';
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${r.name}</td><td>${r.slaveId}</td><td>${r.address}</td>
      <td><b>${parseFloat(r.value).toFixed(3)}</b></td>
      <td>${unit}</td>
      <td>${r.ts}s</td>
      <td><span class="bx ${r.ok?'ok':'er'}">${r.ok?'OK':'FAIL'}</span></td>`;
    tb.appendChild(tr);
  });
}
function filtLive(){ updLive(); }

// ── History ───────────────────────────────────────────────────
let hChart = null;
function loadHist(){
  const par = document.getElementById('hPar').value;
  const rng = document.getElementById('hRng').value;
  fetch('/api/history?param='+encodeURIComponent(par)+'&range='+rng, {headers:ah()})
    .then(r=>r.json()).then(d=>{
      const ctx = document.getElementById('histChart');
      if(hChart) hChart.destroy();
      hChart = new Chart(ctx, {type:'line',
        data:{labels:d.labels||[],datasets:[{
          label:d.param||par,data:d.values||[],
          borderColor:'#58a6ff',backgroundColor:'#58a6ff18',fill:true,tension:.3,pointRadius:3}]},
        options:{responsive:true,
          plugins:{legend:{labels:{color:'#8b949e'}}},
          scales:{x:{ticks:{color:'#8b949e'},grid:{color:'#21262d'}},
                  y:{ticks:{color:'#8b949e'},grid:{color:'#21262d'}}}}});
    });
}
function expCSV(){
  location.href = '/api/history/csv?param='+
    encodeURIComponent(document.getElementById('hPar').value);
}

// ── TCP Settings ──────────────────────────────────────────────
function loadTCP(){
  fetch('/api/system', {headers:ah()}).then(r=>r.json()).then(d=>{
    document.getElementById('tPort').value  = d.tcpPort  || 502;
    document.getElementById('tMaxC').value  = d.maxClients || 10;
    document.getElementById('tTout').value  = d.tcpTimeout || 5000;
    document.getElementById('tPoll').value  = d.pollRate || 1000;
    document.getElementById('tBaud').value  = d.baudRate || 9600;
    document.getElementById('tPar').value   = d.parity   || 'N';
    document.getElementById('tStop').value  = d.stopBits || 1;
  });
}
function saveTCP(){
  // FIX: Use /api/system/update (replaces broken PUT /api/system)
  fetch('/api/system/update', {method:'POST', headers:ah(), body:JSON.stringify({
    tcpPort:    parseInt(document.getElementById('tPort').value),
    maxClients: parseInt(document.getElementById('tMaxC').value),
    tcpTimeout: parseInt(document.getElementById('tTout').value),
    pollRate:   parseInt(document.getElementById('tPoll').value),
    baudRate:   parseInt(document.getElementById('tBaud').value),
    parity:     document.getElementById('tPar').value,
    stopBits:   parseInt(document.getElementById('tStop').value)
  })}).then(r=>r.json()).then(d=>{
    if(d.ok){ toast('Saved – rebooting…'); setTimeout(()=>location.reload(),4000); }
    else     { toast(d.error||'Save failed','err'); }
  }).catch(()=>toast('Network error','err'));
}

// ── System Settings ───────────────────────────────────────────
function loadSys(){
  fetch('/api/system', {headers:ah()}).then(r=>r.json()).then(d=>{
    document.getElementById('sUsr').value    = d.webUser || 'admin';
    document.getElementById('sSSID').value   = d.ssid    || '';
    document.getElementById('sAPSSID').value = d.apSSID  || 'ModbusGW-ESP32';
  });
}
function saveSys(){
  fetch('/api/system/update', {method:'POST', headers:ah(), body:JSON.stringify({
    webUser:  document.getElementById('sUsr').value,
    webPass:  document.getElementById('sPwd').value,
    ssid:     document.getElementById('sSSID').value,
    wifiPass: document.getElementById('sWPwd').value,
    apSSID:   document.getElementById('sAPSSID').value,
    apPass:   document.getElementById('sAPPwd').value
  })}).then(r=>r.json()).then(d=>{
    if(d.ok) toast('Settings saved');
    else     toast(d.error||'Save failed','err');
  }).catch(()=>toast('Network error','err'));
}

// ── Misc ──────────────────────────────────────────────────────
function doReboot(){
  if(!confirm('Reboot device?')) return;
  fetch('/api/reboot',{method:'POST',headers:ah()})
    .then(()=>toast('Rebooting…'));
}
function doFactory(){
  if(!confirm('Factory reset? ALL configuration will be erased!')) return;
  fetch('/api/factory',{method:'POST',headers:ah()});
}
function doBackup(){ location.href='/api/backup'; }
function logout(){ sessionStorage.removeItem('tok'); location.href='/'; }

// ── OTA ───────────────────────────────────────────────────────
function doOTA(){
  const f = document.getElementById('otaF').files[0];
  if(!f){ toast('Select a .bin file first','err'); return; }
  const fd = new FormData(); fd.append('firmware',f);
  const xhr = new XMLHttpRequest();
  xhr.open('POST','/api/ota');
  xhr.setRequestHeader('X-Token', tok());
  xhr.upload.onprogress = e => {
    document.getElementById('otaProg').style.display = 'block';
    document.getElementById('otaBar').style.width = (e.loaded/e.total*100)+'%';
  };
  xhr.onload = () => { document.getElementById('otaMsg').textContent = 'Done – rebooting…'; };
  xhr.send(fd);
}

// ── Init ──────────────────────────────────────────────────────
wsConn();
loadDevs();   // load on page open so home tab is populated immediately
</script>
</body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────
namespace WebDashboard {

void setup(WebServer &server) {
    server.on("/", HTTP_GET, [&server]() {
        server.send_P(200, "text/html", LOGIN_HTML);
    });
    server.on("/dashboard", HTTP_GET, [&server]() {
        server.send_P(200, "text/html", DASH_HTML);
    });
    Serial.println(F("[WEB] Dashboard routes OK"));
}

} // namespace WebDashboard
