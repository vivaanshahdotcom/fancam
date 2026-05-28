// =============================================================================
// dashboard_ui.h — PROGMEM dashboard HTML/CSS/JS (no external CDN)
// =============================================================================
#pragma once
#include <pgmspace.h>

static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FanCam Dashboard</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg0:#050d1a;--bg1:#0a1628;--glass:rgba(255,255,255,0.04);
  --border:rgba(255,255,255,0.08);--cyan:#22d3ee;--amber:#f59e0b;
  --rose:#f43f5e;--text:#e2e8f0;--muted:#64748b;--radius:1rem;
  --shadow:0 8px 32px rgba(0,0,0,0.5);
}
html{font-family:system-ui,-apple-system,'Inter',sans-serif;font-size:15px;color:var(--text)}
body{
  min-height:100vh;background:linear-gradient(160deg,var(--bg1) 0%,var(--bg0) 100%);
  padding:0.75rem;display:flex;flex-direction:column;gap:0.75rem;
}
/* header */
#hdr{
  display:flex;flex-wrap:wrap;gap:0.5rem 1.5rem;align-items:center;
  background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);
  padding:0.6rem 1rem;backdrop-filter:blur(14px);
  box-shadow:var(--shadow);
}
#hdr .title{font-size:1.1rem;font-weight:700;color:var(--cyan);letter-spacing:.04em}
#hdr .meta{font-size:.78rem;color:var(--muted);display:flex;gap:.75rem;flex-wrap:wrap}
#hdr .meta span{display:flex;align-items:center;gap:.3rem}
.rssi-bar{display:inline-flex;gap:2px;align-items:flex-end;height:14px}
.rssi-bar div{width:3px;background:var(--muted);border-radius:1px;transition:background .3s}
.rssi-bar div.on{background:var(--cyan)}
.pill{
  padding:.15rem .55rem;border-radius:999px;font-size:.7rem;font-weight:600;
  background:rgba(34,211,238,.12);color:var(--cyan);border:1px solid rgba(34,211,238,.25);
  display:none;
}
.pill.warn{background:rgba(245,158,11,.15);color:var(--amber);border-color:rgba(245,158,11,.3)}
.pill.err{background:rgba(244,63,94,.15);color:var(--rose);border-color:rgba(244,63,94,.3)}
#reconnPill{display:none}

/* grid layout */
.grid{display:grid;gap:.75rem;grid-template-columns:1fr}
@media(min-width:640px){.grid{grid-template-columns:1fr 1fr}}
@media(min-width:1024px){.grid{grid-template-columns:2fr 1fr}}

/* card */
.card{
  background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);
  padding:1rem;backdrop-filter:blur(14px);box-shadow:var(--shadow);
  transition:transform .2s ease,box-shadow .2s ease;
}
.card:hover{transform:translateY(-2px);box-shadow:0 12px 40px rgba(0,0,0,.6)}
.card h2{font-size:.8rem;font-weight:600;color:var(--muted);text-transform:uppercase;
  letter-spacing:.08em;margin-bottom:.75rem}

/* camera card */
#camCard{grid-column:1/-1}
@media(min-width:1024px){#camCard{grid-column:1/2;grid-row:1/3}}
#camWrap{position:relative;background:#000;border-radius:.5rem;overflow:hidden;aspect-ratio:4/3}
#camWrap img{width:100%;height:100%;object-fit:cover;display:block;transform:rotate(180deg)}
#camOverlay{
  position:absolute;bottom:0;left:0;right:0;padding:.5rem .75rem;
  background:linear-gradient(transparent,rgba(0,0,0,.7));
  display:flex;gap:.5rem;justify-content:flex-end;
}
/* stats card */
#statsCard{display:flex;flex-direction:column;gap:.75rem}
.big-stat{display:flex;flex-direction:column}
.big-stat .val{font-size:2.2rem;font-weight:700;color:var(--cyan);line-height:1;
  transition:color .3s}
.big-stat .lbl{font-size:.72rem;color:var(--muted);margin-top:.15rem}
.stat-row{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem}
.mini-stat .val{font-size:1.2rem;font-weight:600;transition:color .3s}
.mini-stat .lbl{font-size:.68rem;color:var(--muted)}
canvas.spark{width:100%;height:40px;display:block;margin-top:.25rem}

/* fan card */
.mode-toggle{display:flex;gap:.4rem;margin-bottom:.75rem}
.mode-btn{
  flex:1;padding:.4rem;border-radius:.5rem;border:1px solid var(--border);
  background:transparent;color:var(--muted);font-size:.8rem;font-weight:600;
  cursor:pointer;transition:all .2s;
}
.mode-btn.active{background:rgba(34,211,238,.15);color:var(--cyan);border-color:var(--cyan)}
.slider-wrap{margin:.5rem 0}
.slider-wrap label{font-size:.75rem;color:var(--muted);display:flex;justify-content:space-between}
input[type=range]{
  width:100%;accent-color:var(--cyan);height:4px;margin:.35rem 0;cursor:pointer;
}
input[type=range]:disabled{opacity:.3;cursor:default}

/* RPM gauge */
#rpmGauge{display:flex;flex-direction:column;align-items:center;margin:.5rem 0}
#rpmGauge svg{width:140px;height:80px}
#rpmVal{font-size:1rem;font-weight:700;color:var(--cyan);margin-top:.25rem;
  transition:color .3s}

/* Fan health chart */
#fhChart{margin-top:.5rem}
#fhBars{display:flex;gap:4px;align-items:flex-end;height:48px;margin-top:.35rem}
.fh-bar{flex:1;border-radius:2px 2px 0 0;background:var(--cyan);opacity:.7;
  transition:height .4s ease;min-height:2px}
.fh-labels{display:flex;gap:4px;margin-top:2px}
.fh-labels span{flex:1;font-size:.6rem;color:var(--muted);text-align:center}

/* LED card */
.led-top{display:flex;align-items:center;gap:.75rem;margin-bottom:.6rem}
.toggle{
  position:relative;width:42px;height:24px;flex-shrink:0;
}
.toggle input{opacity:0;width:0;height:0}
.toggle .knob{
  position:absolute;inset:0;background:var(--muted);border-radius:999px;
  cursor:pointer;transition:background .2s;
}
.toggle .knob::after{
  content:'';position:absolute;left:3px;top:3px;width:18px;height:18px;
  background:#fff;border-radius:50%;transition:transform .2s;
}
.toggle input:checked+.knob{background:var(--cyan)}
.toggle input:checked+.knob::after{transform:translateX(18px)}

/* event log */
#evList{max-height:180px;overflow-y:auto;display:flex;flex-direction:column;gap:.35rem;
  scrollbar-width:thin;scrollbar-color:var(--border) transparent}
.ev-item{
  font-size:.72rem;padding:.35rem .6rem;border-radius:.4rem;
  border-left:3px solid var(--amber);background:rgba(245,158,11,.06);
  display:flex;gap:.5rem;
}
.ev-item.crit{border-color:var(--rose);background:rgba(244,63,94,.07)}
.ev-item .ts{color:var(--muted);flex-shrink:0}

/* fault banner */
#faultBanner{
  display:none;align-items:center;justify-content:space-between;gap:.75rem;
  background:rgba(244,63,94,.15);border:1px solid rgba(244,63,94,.35);
  border-radius:.6rem;padding:.6rem 1rem;
}
#faultBanner span{color:var(--rose);font-weight:600;font-size:.85rem}

/* settings collapsible */
#settingsToggle{
  width:100%;background:transparent;border:none;color:var(--cyan);
  font-size:.8rem;font-weight:600;text-align:left;cursor:pointer;
  display:flex;justify-content:space-between;padding:.1rem 0;
}
#settingsBody{display:none;margin-top:.75rem}
#settingsBody.open{display:block}
.settings-grid{display:grid;grid-template-columns:1fr 1fr;gap:.5rem}
.settings-grid label{font-size:.72rem;color:var(--muted);display:flex;
  flex-direction:column;gap:.2rem}
.settings-grid input[type=number]{
  background:rgba(255,255,255,.05);border:1px solid var(--border);
  border-radius:.4rem;color:var(--text);padding:.3rem .5rem;font-size:.78rem;
  width:100%;
}

/* buttons */
.btn{
  padding:.4rem .85rem;border-radius:.5rem;border:1px solid var(--border);
  background:rgba(255,255,255,.05);color:var(--text);font-size:.78rem;
  font-weight:600;cursor:pointer;transition:all .2s;
}
.btn:hover{background:rgba(34,211,238,.12);border-color:var(--cyan);color:var(--cyan)}
.btn.danger{border-color:rgba(244,63,94,.35)}
.btn.danger:hover{background:rgba(244,63,94,.15);border-color:var(--rose);color:var(--rose)}
.btn.primary{background:rgba(34,211,238,.15);border-color:var(--cyan);color:var(--cyan)}
.btn.primary:hover{background:rgba(34,211,238,.28)}

/* wifi setup overlay */
#wifiSetup{
  display:none;position:fixed;inset:0;background:rgba(5,13,26,.92);
  z-index:100;align-items:center;justify-content:center;
}
#wifiSetup .card{max-width:380px;width:90%}
#wifiSetup input[type=text],#wifiSetup input[type=password]{
  width:100%;background:rgba(255,255,255,.05);border:1px solid var(--border);
  border-radius:.5rem;color:var(--text);padding:.5rem .75rem;font-size:.9rem;margin:.3rem 0;
}
</style>
</head>
<body>

<!-- Header -->
<div id="hdr">
  <span class="title">&#9650; FanCam</span>
  <div class="meta">
    <span>&#x23F1; <span id="uptime">--</span></span>
    <span>
      <span class="rssi-bar" id="rssiBar">
        <div id="r1"></div><div id="r2"></div><div id="r3"></div><div id="r4"></div>
      </span>
      <span id="rssiVal">--</span> dBm
    </span>
    <span>&#x1F9E0; <span id="heap">--</span> KB</span>
    <span style="color:var(--muted)">v1.0.0</span>
  </div>
  <span class="pill warn" id="reconnPill">&#9679; Reconnecting&hellip;</span>
  <span class="pill err" id="faultPill" style="display:none">&#9888; FAULT</span>
</div>

<!-- Fault banner -->
<div id="faultBanner">
  <span>&#9888; Fan stall fault latched &mdash; fan locked at 100%</span>
  <button class="btn danger" onclick="ackFault()">Acknowledge</button>
</div>

<!-- Main grid -->
<div class="grid">

  <!-- Camera -->
  <div class="card" id="camCard">
    <h2>Live Camera</h2>
    <div id="camWrap">
      <img id="camImg" alt="stream">
      <div id="camOverlay">
        <button class="btn" onclick="snapshot()">&#128247; Snapshot</button>
        <button class="btn" onclick="fullscreen()">&#x26F6; Fullscreen</button>
      </div>
    </div>
  </div>

  <!-- Stats -->
  <div class="card" id="statsCard">
    <h2>System Stats</h2>
    <div class="big-stat">
      <span class="val" id="tempVal">--</span>
      <span class="lbl">CPU Temperature (°C)</span>
    </div>
    <canvas class="spark" id="tempSpark" width="300" height="40"></canvas>
    <div class="stat-row">
      <div class="mini-stat">
        <div class="val" id="fanPctVal">--</div>
        <div class="lbl">Fan PWM %</div>
      </div>
      <div class="mini-stat">
        <div class="val" id="rpmStatVal">--</div>
        <div class="lbl">Fan RPM</div>
      </div>
      <div class="mini-stat">
        <div class="val" id="ledPctVal">--</div>
        <div class="lbl">LED %</div>
      </div>
    </div>
  </div>

  <!-- Fan control -->
  <div class="card">
    <h2>Fan Control</h2>
    <div class="mode-toggle">
      <button class="mode-btn" id="btnAuto" onclick="setMode('auto')">&#9711; Auto</button>
      <button class="mode-btn" id="btnManual" onclick="setMode('manual')">&#9654; Manual</button>
    </div>

    <div class="slider-wrap">
      <label>Manual PWM <span id="manualPctLbl">0%</span></label>
      <input type="range" id="fanSlider" min="0" max="100" value="0"
        oninput="onFanSlider(this.value)" disabled>
    </div>

    <div style="font-size:.72rem;color:var(--muted);margin:.4rem 0">
      Target: <span id="targetPct" style="color:var(--text)">--</span>%
      &nbsp;|&nbsp; Actual: <span id="actualPct" style="color:var(--text)">--</span>%
    </div>

    <div id="rpmGauge">
      <svg viewBox="0 0 140 80">
        <path d="M10,70 A60,60 0 0,1 130,70" fill="none" stroke="rgba(255,255,255,.07)" stroke-width="10" stroke-linecap="round"/>
        <path id="gaugeFill" d="M10,70 A60,60 0 0,1 130,70" fill="none" stroke="#22d3ee"
          stroke-width="10" stroke-linecap="round"
          stroke-dasharray="188.5" stroke-dashoffset="188.5"
          style="transition:stroke-dashoffset .4s ease,stroke .3s"/>
        <text x="70" y="68" text-anchor="middle" fill="#64748b" font-size="9">RPM</text>
      </svg>
      <div id="rpmVal">-- RPM</div>
    </div>

    <div id="fhChart">
      <div style="font-size:.72rem;color:var(--muted)">Fan Health (boot test)</div>
      <div id="fhBars">
        <div class="fh-bar" id="fh25" style="height:4px"></div>
        <div class="fh-bar" id="fh50" style="height:4px"></div>
        <div class="fh-bar" id="fh75" style="height:4px"></div>
        <div class="fh-bar" id="fh100" style="height:4px"></div>
      </div>
      <div class="fh-labels">
        <span>25%</span><span>50%</span><span>75%</span><span>100%</span>
      </div>
    </div>
  </div>

  <!-- LED control -->
  <div class="card">
    <h2>LED Control</h2>
    <div class="led-top">
      <label class="toggle">
        <input type="checkbox" id="ledToggle" onchange="onLedToggle()">
        <span class="knob"></span>
      </label>
      <span id="ledStatLbl" style="font-size:.82rem">Off</span>
    </div>
    <div class="slider-wrap">
      <label>Brightness <span id="ledPctLbl">0%</span></label>
      <input type="range" id="ledSlider" min="0" max="100" value="0"
        oninput="onLedSlider(this.value)">
    </div>
    <div class="led-top" style="margin-top:.5rem">
      <label class="toggle">
        <input type="checkbox" id="pulseToggle" onchange="onPulseToggle()">
        <span class="knob"></span>
      </label>
      <span style="font-size:.82rem">Pulse / Breathing mode</span>
    </div>
  </div>

  <!-- Event log -->
  <div class="card">
    <h2>Event Log</h2>
    <div id="evList"><span style="font-size:.75rem;color:var(--muted)">No events yet.</span></div>
  </div>

  <!-- Settings -->
  <div class="card">
    <button id="settingsToggle" onclick="toggleSettings()">
      &#9881; Settings
      <span id="settingsArrow">&#9660;</span>
    </button>
    <div id="settingsBody">
      <div style="font-size:.72rem;color:var(--muted);margin-bottom:.5rem">Temperature curve (°C → %)</div>
      <div class="settings-grid">
        <label>Temp 1 (°C)<input type="number" id="s_t1" value="40"></label>
        <label>PWM 1 (%)<input type="number" id="s_p1" value="0"></label>
        <label>Temp 2 (°C)<input type="number" id="s_t2" value="45"></label>
        <label>PWM 2 (%)<input type="number" id="s_p2" value="30"></label>
        <label>Temp 3 (°C)<input type="number" id="s_t3" value="55"></label>
        <label>PWM 3 (%)<input type="number" id="s_p3" value="60"></label>
        <label>Temp 4 (°C)<input type="number" id="s_t4" value="65"></label>
        <label>PWM 4 (%)<input type="number" id="s_p4" value="85"></label>
        <label>Temp 5 (°C)<input type="number" id="s_t5" value="75"></label>
        <label>PWM 5 (%)<input type="number" id="s_p5" value="100"></label>
      </div>
      <div style="font-size:.72rem;color:var(--muted);margin:.6rem 0 .3rem">Thresholds</div>
      <div class="settings-grid">
        <label>Slew rate (%/100ms)<input type="number" id="s_slew" value="2" min="1" max="20"></label>
        <label>Stall RPM ratio (%)<input type="number" id="s_stallRatio" value="30" min="5" max="80"></label>
        <label>Stall detect delay (s)<input type="number" id="s_stallDelay" value="3" min="1" max="10"></label>
      </div>
      <div style="display:flex;gap:.5rem;margin-top:.75rem;flex-wrap:wrap">
        <button class="btn primary" onclick="saveSettings()">&#10003; Save &amp; Apply</button>
        <button class="btn danger" onclick="reboot()">&#9211; Reboot</button>
      </div>
    </div>
  </div>

</div><!-- /grid -->

<!-- Wi-Fi setup overlay (shown in AP mode) -->
<div id="wifiSetup">
  <div class="card">
    <h2 style="font-size:1rem;color:var(--cyan);margin-bottom:.75rem">&#128246; Wi-Fi Setup</h2>
    <p style="font-size:.82rem;color:var(--muted);margin-bottom:.75rem">
      No network configured. Enter your Wi-Fi credentials to connect.
    </p>
    <input type="text" id="wSSID" placeholder="SSID (network name)">
    <input type="password" id="wPass" placeholder="Password">
    <button class="btn primary" style="width:100%;margin-top:.75rem" onclick="saveWifi()">
      Connect
    </button>
    <div id="wifiMsg" style="font-size:.75rem;color:var(--muted);margin-top:.5rem"></div>
  </div>
</div>

<script>
// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
let tempHistory = [];
const SPARK_MAX = 60;
let fanMode = 'auto';
let fanDebTimer = null;
let ledDebTimer = null;
let maxRPM = 3000; // updated from health data
let healthRPMs = [0,0,0,0];
let reconnecting = false;
let lastEvents = [];

// ─────────────────────────────────────────────────────────────────────────────
// Stats polling
// ─────────────────────────────────────────────────────────────────────────────
async function pollStats() {
  try {
    const r = await fetch('/stats', {signal: AbortSignal.timeout(2000)});
    if (!r.ok) throw new Error('bad status');
    const d = await r.json();
    applyStats(d);
    if (reconnecting) { reconnecting=false; document.getElementById('reconnPill').style.display='none'; }
  } catch(e) {
    if (!reconnecting) { reconnecting=true; document.getElementById('reconnPill').style.display='inline-flex'; }
  }
}

function applyStats(d) {
  // Header
  document.getElementById('uptime').textContent = fmtUptime(d.uptimeMs);
  document.getElementById('rssiVal').textContent = d.rssi;
  document.getElementById('heap').textContent = Math.round(d.freeHeap/1024);
  updateRSSI(d.rssi);

  // Temp
  const t = d.tempC.toFixed(1);
  setText('tempVal', t, colorTemp(d.tempC));
  tempHistory.push(d.tempC);
  if (tempHistory.length > SPARK_MAX) tempHistory.shift();
  drawSparkline('tempSpark', tempHistory, [30,80], '#22d3ee');

  // Fan
  setText('fanPctVal', d.fanPWM+'%', '#22d3ee');
  setText('rpmStatVal', d.fanRPM, '#22d3ee');
  setText('ledPctVal', d.ledPWM+'%', '#22d3ee');
  document.getElementById('targetPct').textContent = d.fanTargetPWM;
  document.getElementById('actualPct').textContent = d.fanPWM;

  // RPM gauge
  updateGauge(d.fanRPM);
  document.getElementById('rpmVal').textContent = d.fanRPM + ' RPM';

  // Mode sync
  if (d.mode !== fanMode) { fanMode = d.mode; updateModeUI(); }

  // Fault
  const fb = document.getElementById('faultBanner');
  const fp = document.getElementById('faultPill');
  fb.style.display = d.faultLatched ? 'flex' : 'none';
  fp.style.display = d.faultLatched ? 'inline-flex' : 'none';

  // health bars from first poll
  if (d.healthRPM && d.healthRPM.length === 4) {
    healthRPMs = d.healthRPM;
    const mx = Math.max(...healthRPMs, 1);
    maxRPM = mx;
    ['25','50','75','100'].forEach((p,i) => {
      const el = document.getElementById('fh'+p);
      el.style.height = Math.max(4, Math.round((healthRPMs[i]/mx)*48))+'px';
      el.title = healthRPMs[i]+' RPM';
    });
  }
}

function setText(id, val, color) {
  const el = document.getElementById(id);
  el.textContent = val;
  if (color) el.style.color = color;
}

function colorTemp(t) {
  if (t < 50) return '#22d3ee';
  if (t < 65) return '#f59e0b';
  return '#f43f5e';
}

// ─────────────────────────────────────────────────────────────────────────────
// Events polling
// ─────────────────────────────────────────────────────────────────────────────
async function pollEvents() {
  try {
    const r = await fetch('/events', {signal: AbortSignal.timeout(2000)});
    if (!r.ok) return;
    const evs = await r.json();
    if (JSON.stringify(evs) === JSON.stringify(lastEvents)) return;
    lastEvents = evs;
    renderEvents(evs);
  } catch(e){}
}

function renderEvents(evs) {
  const el = document.getElementById('evList');
  if (!evs || evs.length === 0) {
    el.innerHTML = '<span style="font-size:.75rem;color:var(--muted)">No events yet.</span>';
    return;
  }
  el.innerHTML = evs.slice().reverse().map(e => {
    const ts = fmtUptime(e.tMs);
    const crit = e.type === 'fault' || e.type === 'recovery_fail';
    return `<div class="ev-item${crit?' crit':''}">
      <span class="ts">${ts}</span>
      <span>${e.type} &mdash; PWM:${e.pwm}% RPM:${e.rpm}</span>
    </div>`;
  }).join('');
}

// ─────────────────────────────────────────────────────────────────────────────
// Gauge
// ─────────────────────────────────────────────────────────────────────────────
function updateGauge(rpm) {
  const arc = 188.5;
  const ratio = Math.min(rpm / Math.max(maxRPM,1), 1);
  const offset = arc - ratio * arc;
  const fill = document.getElementById('gaugeFill');
  fill.style.strokeDashoffset = offset;
  fill.style.stroke = ratio > 0.85 ? '#f43f5e' : ratio > 0.6 ? '#f59e0b' : '#22d3ee';
}

// ─────────────────────────────────────────────────────────────────────────────
// Sparkline
// ─────────────────────────────────────────────────────────────────────────────
function drawSparkline(id, data, range, color) {
  const canvas = document.getElementById(id);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  ctx.clearRect(0,0,W,H);
  if (data.length < 2) return;
  const [lo, hi] = range;
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  data.forEach((v,i) => {
    const x = (i/(data.length-1))*W;
    const y = H - ((Math.min(Math.max(v,lo),hi)-lo)/(hi-lo))*H;
    i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
  });
  ctx.stroke();
  // fill
  ctx.lineTo(W,H); ctx.lineTo(0,H); ctx.closePath();
  ctx.fillStyle = color+'22';
  ctx.fill();
}

// ─────────────────────────────────────────────────────────────────────────────
// RSSI bar
// ─────────────────────────────────────────────────────────────────────────────
function updateRSSI(rssi) {
  // -50 excellent, -60 good, -70 fair, <-80 poor
  const bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  for (let i=1; i<=4; i++) {
    document.getElementById('r'+i).className = i <= bars ? 'on' : '';
    document.getElementById('r'+i).style.height = (i*3+4)+'px';
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Fan UI
// ─────────────────────────────────────────────────────────────────────────────
function updateModeUI() {
  document.getElementById('btnAuto').classList.toggle('active', fanMode==='auto');
  document.getElementById('btnManual').classList.toggle('active', fanMode==='manual');
  document.getElementById('fanSlider').disabled = fanMode==='auto';
}

function setMode(m) {
  fanMode = m;
  updateModeUI();
  postFan();
}

function onFanSlider(v) {
  document.getElementById('manualPctLbl').textContent = v+'%';
  clearTimeout(fanDebTimer);
  fanDebTimer = setTimeout(postFan, 150);
}

function postFan() {
  const pwm = parseInt(document.getElementById('fanSlider').value)||0;
  fetch('/api/fan', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({mode: fanMode, pwm: pwm})}).catch(()=>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// LED UI
// ─────────────────────────────────────────────────────────────────────────────
function onLedToggle() {
  const on = document.getElementById('ledToggle').checked;
  document.getElementById('ledStatLbl').textContent = on ? 'On' : 'Off';
  postLed();
}

function onLedSlider(v) {
  document.getElementById('ledPctLbl').textContent = v+'%';
  clearTimeout(ledDebTimer);
  ledDebTimer = setTimeout(postLed, 150);
}

function onPulseToggle() { postLed(); }

function postLed() {
  const on = document.getElementById('ledToggle').checked;
  const pwm = parseInt(document.getElementById('ledSlider').value)||0;
  const pulse = document.getElementById('pulseToggle').checked;
  fetch('/api/led', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({on, pwm, pulse})}).catch(()=>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────
function toggleSettings() {
  const b = document.getElementById('settingsBody');
  const a = document.getElementById('settingsArrow');
  b.classList.toggle('open');
  a.textContent = b.classList.contains('open') ? '▲' : '▼';
}

function saveSettings() {
  const n = id => parseFloat(document.getElementById(id).value)||0;
  const body = {
    curve: [
      {t: n('s_t1'), p: n('s_p1')},
      {t: n('s_t2'), p: n('s_p2')},
      {t: n('s_t3'), p: n('s_p3')},
      {t: n('s_t4'), p: n('s_p4')},
      {t: n('s_t5'), p: n('s_p5')},
    ],
    slewRate: n('s_slew'),
    stallRatio: n('s_stallRatio'),
    stallDelay: n('s_stallDelay'),
  };
  fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify(body)}).then(r=>r.ok && alert('Settings saved!')).catch(()=>{});
}

function ackFault() {
  fetch('/api/ack-fault', {method:'POST'}).catch(()=>{});
}

function reboot() {
  if (confirm('Reboot the device?'))
    fetch('/api/reboot', {method:'POST'}).catch(()=>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera helpers
// ─────────────────────────────────────────────────────────────────────────────
// Stream and capture both live on port 81 (separate server so slow MJPEG
// clients can never stall the main dashboard/API on port 80).
const STREAM_HOST = 'http://' + location.hostname + ':81';

function startStream() {
  document.getElementById('camImg').src = STREAM_HOST + '/stream';
}

function snapshot() {
  const a = document.createElement('a');
  a.href = STREAM_HOST + '/capture';
  a.download = 'fancam_' + Date.now() + '.jpg';
  a.click();
}

function fullscreen() {
  const el = document.getElementById('camWrap');
  if (el.requestFullscreen) el.requestFullscreen();
}

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi setup
// ─────────────────────────────────────────────────────────────────────────────
async function saveWifi() {
  const ssid = document.getElementById('wSSID').value.trim();
  const pass = document.getElementById('wPass').value;
  if (!ssid) { document.getElementById('wifiMsg').textContent = 'SSID required'; return; }
  document.getElementById('wifiMsg').textContent = 'Saving…';
  try {
    await fetch('/api/wifi', {method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({ssid, pass})});
    document.getElementById('wifiMsg').textContent = 'Saved! Rebooting…';
  } catch(e) {
    document.getElementById('wifiMsg').textContent = 'Error: '+e.message;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Uptime formatter
// ─────────────────────────────────────────────────────────────────────────────
function fmtUptime(ms) {
  const s = Math.floor(ms/1000);
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), sec = s%60;
  return [h,m,sec].map(v=>String(v).padStart(2,'0')).join(':');
}

// ─────────────────────────────────────────────────────────────────────────────
// AP mode detection: if AP IP, show Wi-Fi setup overlay
// ─────────────────────────────────────────────────────────────────────────────
if (location.hostname === '192.168.4.1') {
  document.getElementById('wifiSetup').style.display = 'flex';
}

// ─────────────────────────────────────────────────────────────────────────────
// Start
// ─────────────────────────────────────────────────────────────────────────────
startStream();
updateModeUI();
pollStats();
pollEvents();
setInterval(pollStats, 1000);
setInterval(pollEvents, 3000);
</script>
</body>
</html>
)rawhtml";
