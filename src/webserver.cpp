#include "webserver.h"
#include "storage.h"
#include "timeutil.h"
#include "../include/config.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer _server(80);
static AsyncWebSocket _ws("/ws");
static DNSServer _dnsServer;
static PerformanceMode *_perfPtr = nullptr;
static TripMode *_tripPtr = nullptr;
static Storage *_storagePtr = nullptr;

// Fully self-contained: the phone has no internet while joined to this AP,
// so no CDN links, no external fonts/scripts -- everything inline.
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>0sixty</title>
<style>
  body { background:#0b0b0f; color:#e8e8f0; font-family:-apple-system,sans-serif; margin:0; padding:16px; }
  h1 { font-size:1rem; color:#8a8aa0; margin:0 0 12px; letter-spacing:0.05em; }
  h2 { font-size:0.85rem; color:#8a8aa0; margin:20px 0 8px; letter-spacing:0.05em; }
  .status { display:flex; gap:8px; margin-bottom:12px; flex-wrap:wrap; align-items:center; }
  .pill { background:#1a1a24; border-radius:8px; padding:5px 10px; font-size:0.8rem; }
  .pill.fix { color:#4ade80; }
  .pill.nofix { color:#f87171; }
  .pill.state-RUN, .pill.state-REC { color:#facc15; background:#332b0a; }
  .pill.state-DONE { color:#4ade80; background:#0a3320; }
  input, button { background:#1a1a24; color:#e8e8f0; border:1px solid #2a2a38; border-radius:8px; padding:6px 12px; font-size:0.8rem; }
  button:active { background:#2a2a38; }
  input { width:100%; box-sizing:border-box; margin-bottom:6px; }
  .speed { font-size:3rem; font-weight:700; margin:4px 0; }
  .speed-unit { font-size:1.1rem; color:#8a8aa0; }
  canvas { width:100%; height:150px; background:#12121a; border-radius:8px; display:block; margin-top:8px; }
  #routeCanvas { height:220px; background:#0d1a12; }
  .tiles { display:grid; grid-template-columns:repeat(3,1fr); gap:8px; margin:12px 0; }
  .tile { background:#12121a; border-radius:8px; padding:10px; text-align:center; }
  .tile .v { font-size:1.3rem; font-weight:700; }
  .tile .l { font-size:0.7rem; color:#8a8aa0; margin-top:2px; }
  table { width:100%; border-collapse:collapse; margin-top:12px; font-size:0.85rem; }
  td { padding:6px 4px; border-bottom:1px solid #1e1e28; }
  td.metric { color:#8a8aa0; }
  td.time { text-align:right; font-variant-numeric:tabular-nums; }
  td.time.reached { color:#4ade80; }
  .row { display:flex; gap:16px; font-size:0.8rem; color:#8a8aa0; margin-top:6px; }
  .downloads { display:flex; gap:8px; flex-wrap:wrap; margin-top:14px; }
  .downloads a { color:#8a8aa0; font-size:0.8rem; background:#12121a; border-radius:8px; padding:6px 10px; text-decoration:none; }
  .nameRow { display:flex; gap:8px; margin-top:8px; }
  .nameRow input { margin-bottom:0; }
  .card { background:#12121a; border-radius:8px; padding:10px; margin-bottom:8px; }
  .card .t1 { font-weight:600; font-size:0.9rem; }
  .card .t2 { font-size:0.75rem; color:#8a8aa0; margin-top:2px; }
  .card .links { display:flex; gap:8px; margin-top:6px; }
  .card a { color:#8a8aa0; font-size:0.75rem; }
  .card a.del { color:#f87171; margin-left:auto; }
  .card .fn { font-family:ui-monospace,monospace; font-size:0.68rem; color:#5a5a70; word-break:break-all; }
  .hint { font-size:0.75rem; color:#8a8aa0; margin-top:4px; }
  [hidden] { display:none !important; }
</style>
</head>
<body>
<h1>0SIXTY</h1>
<div class="status">
  <span id="fixPill" class="pill nofix">no fix</span>
  <span class="pill" id="satsPill">0 sats</span>
  <span class="pill" id="battPill">--%</span>
  <span class="pill" id="statePill">WAIT</span>
  <span class="pill" id="connPill">connecting...</span>
  <button id="modeBtn">Switch to Trip</button>
</div>
<div class="speed"><span id="speed">0.0</span><span class="speed-unit"> km/h</span></div>
<canvas id="graph" width="600" height="150"></canvas>

<div id="perfPanel">
  <div class="tiles">
    <div class="tile"><div class="v" id="distance">0m</div><div class="l">DISTANCE</div></div>
    <div class="tile"><div class="v" id="t060">--</div><div class="l">0-60 KM/H</div></div>
    <div class="tile"><div class="v" id="slope">0%</div><div class="l">SLOPE</div></div>
  </div>
  <div class="tiles">
    <div class="tile"><div class="v" id="quarterMile">--</div><div class="l">1/4 MILE</div></div>
    <div class="tile"><div class="v" id="quarterTrap">--</div><div class="l">TRAP KM/H</div></div>
    <div class="tile"><div class="v" id="rolling">--</div><div class="l">60-130 KM/H</div></div>
  </div>
  <table id="splits"></table>
</div>

<div id="tripPanel" hidden>
  <div class="nameRow">
    <input id="tripName" placeholder="Trip / vehicle name">
    <button id="tripNameBtn">Set</button>
  </div>
  <div class="tiles">
    <div class="tile"><div class="v" id="tripDist">0 km</div><div class="l">DISTANCE</div></div>
    <div class="tile"><div class="v" id="tripDur">0:00</div><div class="l">DURATION</div></div>
    <div class="tile"><div class="v" id="tripMax">0</div><div class="l">MAX KM/H</div></div>
  </div>
  <div class="row"><span id="tripPoints">0 / 0 points</span><span id="tripEvents">0 events</span></div>
  <canvas id="routeCanvas" width="600" height="440"></canvas>
</div>

<div class="row">
  <span id="pos">--</span>
  <span id="alt">alt: -- m</span>
</div>
<div class="downloads">
  <a id="dlPerf" href="/download/run.csv">Download last run (CSV)</a>
  <a id="dlPerfReport" href="/report/run.html" target="_blank">View / print report</a>
  <a id="dlTripKml" href="/download/trip.kml" hidden>Download trip (KML)</a>
  <a id="dlTripCsv" href="/download/trip.csv" hidden>Download trip (CSV)</a>
</div>
<div class="row" id="myMapsHint" hidden>
  <span>To view this in Google My Maps: download the KML above, then <b>disconnect from 0sixty WiFi</b> and open <b>mymaps.google.com</b> yourself in your phone's normal browser. This page may close the instant you disconnect -- many phones treat it as a temporary WiFi sign-in screen, not a real tab -- so there's no link here that survives that. Once there, use Import and pick the file from your Downloads.</span>
</div>

<div id="perfHistorySection">
  <h2>PERFORMANCE HISTORY</h2>
  <div id="perfHistoryList"></div>
  <button id="perfHistoryRefreshBtn">Refresh</button>
</div>

<div id="tripHistorySection" hidden>
  <h2>TRIP HISTORY</h2>
  <div id="historyList"></div>
  <button id="historyRefreshBtn">Refresh</button>
</div>

<h2>HOME WIFI AUTO-UPLOAD (optional)</h2>
<input id="syncSsid" placeholder="Home WiFi SSID">
<input id="syncPassword" type="password" placeholder="Home WiFi password">
<input id="syncUrl" placeholder="Upload URL (e.g. http://192.168.1.10:8080/trips)">
<button id="syncSaveBtn">Save</button>
<div class="hint">Leave SSID blank to keep this off. Untested feature -- see README.</div>

<div id="versionFooter" class="hint" style="text-align:center;margin-top:20px;"></div>

<script>
const speedEl = document.getElementById('speed');
const fixPill = document.getElementById('fixPill');
const satsPill = document.getElementById('satsPill');
const battPill = document.getElementById('battPill');
const statePill = document.getElementById('statePill');
const connPill = document.getElementById('connPill');
const posEl = document.getElementById('pos');
const altEl = document.getElementById('alt');
const distEl = document.getElementById('distance');
const t060El = document.getElementById('t060');
const slopeEl = document.getElementById('slope');
const quarterMileEl = document.getElementById('quarterMile');
const quarterTrapEl = document.getElementById('quarterTrap');
const rollingEl = document.getElementById('rolling');
const splitsEl = document.getElementById('splits');
const canvas = document.getElementById('graph');
const ctx = canvas.getContext('2d');
const routeCanvas = document.getElementById('routeCanvas');
const routeCtx = routeCanvas.getContext('2d');
const perfPanel = document.getElementById('perfPanel');
const tripPanel = document.getElementById('tripPanel');
const modeBtn = document.getElementById('modeBtn');
const dlPerf = document.getElementById('dlPerf');
const dlPerfReport = document.getElementById('dlPerfReport');
const dlTripKml = document.getElementById('dlTripKml');
const dlTripCsv = document.getElementById('dlTripCsv');
const myMapsHint = document.getElementById('myMapsHint');
const tripDist = document.getElementById('tripDist');
const tripDur = document.getElementById('tripDur');
const tripMax = document.getElementById('tripMax');
const tripPoints = document.getElementById('tripPoints');
const tripEvents = document.getElementById('tripEvents');
const tripName = document.getElementById('tripName');
const tripNameBtn = document.getElementById('tripNameBtn');
const historyList = document.getElementById('historyList');
const historyRefreshBtn = document.getElementById('historyRefreshBtn');
const perfHistoryList = document.getElementById('perfHistoryList');
const perfHistoryRefreshBtn = document.getElementById('perfHistoryRefreshBtn');
const perfHistorySection = document.getElementById('perfHistorySection');
const tripHistorySection = document.getElementById('tripHistorySection');
const syncSsid = document.getElementById('syncSsid');
const syncPassword = document.getElementById('syncPassword');
const syncUrl = document.getElementById('syncUrl');
const syncSaveBtn = document.getElementById('syncSaveBtn');

let currentMode = 'PERF';
let tripNameEditing = false;
// Live route is drawn from points collected client-side while this page is
// open (the device only streams the current fix, not its whole trip buffer
// -- the full-trip line lives in the KML/CSV export instead). Throttled to
// one point per 2s so a long drive doesn't grow this unbounded, and cleared
// whenever the device's own point count goes backwards (an X-key reset).
let routePoints = [];
let lastRoutePushMs = 0;
let lastTripPointCount = 0;

function fmtDuration(ms) {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return h > 0 ? `${h}h${String(m).padStart(2,'0')}m` : `${m}:${String(sec).padStart(2,'0')}`;
}

function drawGraph(history) {
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (!history || history.length < 2) return;
  const maxSpeed = Math.max(10, ...history.map(p => p.s));
  ctx.strokeStyle = '#4ade80';
  ctx.lineWidth = 2;
  ctx.beginPath();
  history.forEach((p, i) => {
    const x = (i / (history.length - 1)) * w;
    const y = h - (p.s / maxSpeed) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function drawRoute(points) {
  const w = routeCanvas.width, h = routeCanvas.height;
  routeCtx.clearRect(0, 0, w, h);
  if (!points || points.length < 2) return;
  let minLat = Infinity, maxLat = -Infinity, minLng = Infinity, maxLng = -Infinity;
  for (const p of points) {
    if (p.lat < minLat) minLat = p.lat;
    if (p.lat > maxLat) maxLat = p.lat;
    if (p.lng < minLng) minLng = p.lng;
    if (p.lng > maxLng) maxLng = p.lng;
  }
  const latSpan = Math.max(maxLat - minLat, 0.0002);
  const lngSpan = Math.max(maxLng - minLng, 0.0002);
  const pad = 16;
  const scale = Math.min((w - pad * 2) / lngSpan, (h - pad * 2) / latSpan);
  const offX = (w - lngSpan * scale) / 2;
  const offY = (h - latSpan * scale) / 2;
  // Canvas y grows downward but latitude grows north (up), so flip it.
  const toXY = (p) => [offX + (p.lng - minLng) * scale, h - (offY + (p.lat - minLat) * scale)];
  routeCtx.strokeStyle = '#4ade80';
  routeCtx.lineWidth = 3;
  routeCtx.lineJoin = 'round';
  routeCtx.beginPath();
  points.forEach((p, i) => {
    const [x, y] = toXY(p);
    if (i === 0) routeCtx.moveTo(x, y); else routeCtx.lineTo(x, y);
  });
  routeCtx.stroke();
  const [sx, sy] = toXY(points[0]);
  routeCtx.fillStyle = '#8a8aa0';
  routeCtx.beginPath();
  routeCtx.arc(sx, sy, 4, 0, Math.PI * 2);
  routeCtx.fill();
  const [lx, ly] = toXY(points[points.length - 1]);
  routeCtx.fillStyle = '#facc15';
  routeCtx.beginPath();
  routeCtx.arc(lx, ly, 5, 0, Math.PI * 2);
  routeCtx.fill();
}

function renderSplits(splits) {
  splitsEl.innerHTML = splits.map(s =>
    `<tr><td class="metric">0-${s.t} km/h</td><td class="time${s.reached ? ' reached' : ''}">${s.reached ? (s.ms/1000).toFixed(2)+'s' : '--'}</td></tr>`
  ).join('');
}

modeBtn.onclick = () => fetch('/api/mode', { method: 'POST' });

fetch('/api/version').then(r => r.json()).then(v => {
  document.getElementById('versionFooter').textContent =
    '0sixty v' + v.version + (v.author ? ' · by ' + v.author : '');
}).catch(() => {});

tripNameBtn.onclick = () => {
  fetch('/api/trip/name?name=' + encodeURIComponent(tripName.value), { method: 'POST' });
  tripName.blur();
};
tripName.onfocus = () => { tripNameEditing = true; };
tripName.onblur = () => { tripNameEditing = false; };

// startedEpoch is 0 for anything recorded before the GPS clock was set that
// session -- show nothing rather than 1970.
function fmtStamp(epoch) {
  if (!epoch) return 'no date';
  const d = new Date(epoch * 1000);
  const p = n => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function deleteRecording(kind, file, reload) {
  if (!confirm('Delete ' + file + '?\n\nThis removes it from the SD card permanently.')) return;
  fetch('/api/' + kind + '/delete?file=' + encodeURIComponent(file), { method: 'POST' })
    .then(r => { if (!r.ok) throw new Error(); reload(); })
    .catch(() => alert('Could not delete ' + file));
}

function loadHistory() {
  fetch('/api/trips').then(r => r.json()).then(trips => {
    if (!trips.length) { historyList.innerHTML = '<div class="hint">No saved trips yet.</div>'; return; }
    historyList.innerHTML = trips.map(t => {
      const dist = (t.distanceM / 1000).toFixed(2) + ' km';
      const dur = fmtDuration(t.durationMs);
      const status = t.ended ? '' : ' (in progress)';
      return `<div class="card">
        <div class="t1">${t.name || '(unnamed)'}${status}</div>
        <div class="t2">${fmtStamp(t.startedEpoch)} &middot; ${dist} &middot; ${dur} &middot; max ${t.maxSpeedKmh.toFixed(1)} km/h</div>
        <div class="t2 fn">${t.file}</div>
        <div class="links">
          <a href="/download/trip-history.kml?file=${encodeURIComponent(t.file)}">KML</a>
          <a href="/download/trip-history.csv?file=${encodeURIComponent(t.file)}">CSV</a>
          <a href="#" class="del" data-kind="trips" data-file="${t.file}">Delete</a>
        </div>
      </div>`;
    }).join('');
    historyList.querySelectorAll('a.del').forEach(a => {
      a.onclick = e => { e.preventDefault(); deleteRecording('trips', a.dataset.file, loadHistory); };
    });
  }).catch(() => { historyList.innerHTML = '<div class="hint">Could not load history.</div>'; });
}
historyRefreshBtn.onclick = loadHistory;
loadHistory();

function fmtRunTime(ms) { return ms > 0 ? (ms / 1000).toFixed(2) + 's' : '--'; }

function loadPerfHistory() {
  fetch('/api/runs').then(r => r.json()).then(runs => {
    if (!runs.length) { perfHistoryList.innerHTML = '<div class="hint">No saved runs yet.</div>'; return; }
    perfHistoryList.innerHTML = runs.map(r => {
      return `<div class="card">
        <div class="t1">${r.name || '(unnamed)'} &middot; 0-60: ${fmtRunTime(r.zeroSixtyMs)}</div>
        <div class="t2">${fmtStamp(r.startedEpoch)} &middot; 1/4mi ${fmtRunTime(r.quarterMileMs)} &middot; max ${r.maxSpeedKmh.toFixed(1)} km/h</div>
        <div class="t2 fn">${r.file}</div>
        <div class="links">
          <a href="/report/run.html?file=${encodeURIComponent(r.file)}" target="_blank">Report</a>
          <a href="/download/run-history.csv?file=${encodeURIComponent(r.file)}">CSV</a>
          <a href="#" class="del" data-kind="runs" data-file="${r.file}">Delete</a>
        </div>
      </div>`;
    }).join('');
    perfHistoryList.querySelectorAll('a.del').forEach(a => {
      a.onclick = e => { e.preventDefault(); deleteRecording('runs', a.dataset.file, loadPerfHistory); };
    });
  }).catch(() => { perfHistoryList.innerHTML = '<div class="hint">Could not load history.</div>'; });
}
perfHistoryRefreshBtn.onclick = loadPerfHistory;
loadPerfHistory();

function loadSyncConfig() {
  fetch('/api/sync/config').then(r => r.json()).then(cfg => {
    syncSsid.value = cfg.ssid || '';
    syncUrl.value = cfg.uploadUrl || '';
  }).catch(() => {});
}
syncSaveBtn.onclick = () => {
  const params = new URLSearchParams({ ssid: syncSsid.value, password: syncPassword.value, uploadUrl: syncUrl.value });
  fetch('/api/sync/config?' + params.toString(), { method: 'POST' }).then(() => { syncPassword.value = ''; });
};
loadSyncConfig();

function connect() {
  const ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => connPill.textContent = 'live';
  ws.onclose = () => { connPill.textContent = 'disconnected'; setTimeout(connect, 1000); };
  ws.onerror = () => ws.close();
  ws.onmessage = (evt) => {
    const d = JSON.parse(evt.data);
    speedEl.textContent = d.speedKmh.toFixed(1);
    fixPill.textContent = d.hasFix ? 'FIX' : 'no fix';
    fixPill.className = 'pill ' + (d.hasFix ? 'fix' : 'nofix');
    satsPill.textContent = d.satellites + ' sats';
    battPill.textContent = d.batteryPct >= 0 ? d.batteryPct + '%' : '?';
    posEl.textContent = d.hasFix ? d.lat.toFixed(6) + ', ' + d.lng.toFixed(6) : '--';
    altEl.textContent = 'alt: ' + d.altitudeM.toFixed(0) + ' m';

    if (d.mode !== currentMode) {
      currentMode = d.mode;
      const isTrip = currentMode === 'TRIP';
      perfPanel.hidden = isTrip;
      tripPanel.hidden = !isTrip;
      dlPerf.hidden = isTrip;
      dlPerfReport.hidden = isTrip;
      // v1.3.0: each mode only shows its own history, rather than both
      // lists stacked on every screen.
      perfHistorySection.hidden = isTrip;
      tripHistorySection.hidden = !isTrip;
      dlTripKml.hidden = !isTrip;
      dlTripCsv.hidden = !isTrip;
      myMapsHint.hidden = !isTrip;
      canvas.hidden = isTrip;
      modeBtn.textContent = isTrip ? 'Switch to Performance' : 'Switch to Trip';
    }

    if (currentMode === 'PERF') {
      const p = d.perf;
      statePill.textContent = (p.state === 'DONE' && !p.persisted) ? 'DONE (not saved!)' : p.state;
      statePill.className = 'pill state-' + p.state;
      distEl.textContent = p.distanceM.toFixed(1) + 'm';
      slopeEl.textContent = p.slopePercent.toFixed(1) + '%';
      const s60 = p.splits.find(s => s.t === 60);
      t060El.textContent = (s60 && s60.reached) ? (s60.ms/1000).toFixed(2) + 's' : '--';
      quarterMileEl.textContent = p.quarterMileReached ? (p.quarterMileMs/1000).toFixed(2) + 's' : '--';
      quarterTrapEl.textContent = p.quarterMileReached ? p.quarterMileTrapKmh.toFixed(1) : '--';
      rollingEl.textContent = p.rollingSplitReached ? (p.rollingSplitMs/1000).toFixed(2) + 's' : '--';
      renderSplits(p.splits);
      drawGraph(p.history);
    } else {
      const t = d.trip;
      const stateLabel = t.state === 'RECORDING' ? 'REC' : (t.state === 'SUMMARY' ? 'DONE' : 'WAIT');
      statePill.textContent = stateLabel;
      statePill.className = 'pill state-' + (t.state === 'RECORDING' ? 'REC' : (t.state === 'SUMMARY' ? 'DONE' : ''));
      tripDist.textContent = (t.distanceM / 1000).toFixed(2) + ' km';
      tripDur.textContent = fmtDuration(t.durationMs);
      tripMax.textContent = t.maxSpeedKmh.toFixed(1);
      tripPoints.textContent = t.pointCount + ' / ' + t.maxPoints + ' points';
      tripEvents.textContent = t.eventCount + ' events';
      if (!tripNameEditing) tripName.value = t.name || '';

      if (t.pointCount < lastTripPointCount) routePoints = [];
      lastTripPointCount = t.pointCount;
      if (d.hasFix) {
        const now = Date.now();
        if (now - lastRoutePushMs > 2000) {
          routePoints.push({ lat: d.lat, lng: d.lng });
          if (routePoints.length > 3000) routePoints.shift();
          lastRoutePushMs = now;
        }
      }
      drawRoute(routePoints);
    }
  };
}
connect();
</script>
</body>
</html>
)HTML";

// v1.2.0: a standalone printable report for one Performance run -- live
// (no ?file=) or historical (?file=run00007.csv). "Export as PDF" without
// any PDF library on the device: this just parses the same CSV the
// download links already produce and renders a clean page, then leans on
// the phone's own browser Print dialog ("Save as PDF" is a standard option
// there) via window.print() -- no server-side PDF generation needed.
static const char RUN_REPORT_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>0sixty Report</title>
<style>
  body { background:#0b0b0f; color:#e8e8f0; font-family:-apple-system,sans-serif; margin:0; padding:16px; }
  h1 { font-size:1.2rem; margin:0 0 2px; }
  .sub { color:#8a8aa0; font-size:0.85rem; margin-bottom:16px; }
  .tiles { display:grid; grid-template-columns:repeat(2,1fr); gap:8px; margin:12px 0; }
  .tile { background:#12121a; border-radius:8px; padding:12px; text-align:center; }
  .tile .v { font-size:1.5rem; font-weight:700; }
  .tile .l { font-size:0.75rem; color:#8a8aa0; margin-top:2px; }
  table { width:100%; border-collapse:collapse; margin-top:8px; font-size:0.9rem; }
  td, th { padding:6px 4px; border-bottom:1px solid #1e1e28; text-align:left; }
  td.time { text-align:right; font-variant-numeric:tabular-nums; }
  td.time.reached { color:#4ade80; }
  canvas { width:100%; height:150px; background:#12121a; border-radius:8px; display:block; margin-top:12px; }
  button { background:#1a1a24; color:#e8e8f0; border:1px solid #2a2a38; border-radius:8px; padding:10px 16px; font-size:0.9rem; margin-top:16px; width:100%; }
  .err { color:#f87171; }
  @media print {
    body { background:#fff; color:#000; padding:0; }
    .tile, table, canvas { background:#fff; }
    td, th { border-bottom:1px solid #ccc; }
    .no-print { display:none; }
  }
</style>
</head>
<body>
<h1 id="title">0sixty -- Performance Report</h1>
<div class="sub" id="subtitle">Loading...</div>
<div class="sub" id="byline"></div>
<div class="tiles" id="tiles"></div>
<table id="splitsTable"></table>
<canvas id="graph" width="600" height="150"></canvas>
<button id="printBtn" class="no-print">Print / Save as PDF</button>
<script>
const params = new URLSearchParams(location.search);
const file = params.get('file');
const url = file ? ('/download/run-history.csv?file=' + encodeURIComponent(file)) : '/download/run.csv';

function fmtMs(ms) { return (ms / 1000).toFixed(2) + 's'; }

function parseCsv(text) {
  const lines = text.split('\n');
  const meta = {};
  if (lines[0] && lines[0].startsWith('# id=')) {
    lines[0].substring(2).split(',').forEach(kv => {
      const [k, v] = kv.split('=');
      meta[k] = v;
    });
  }
  let i = 1;
  const history = [];
  if (lines[i] === 'elapsedMs,speedKmh,altitudeM,accelG') {
    i++;
    while (i < lines.length && lines[i].trim() !== '') {
      const [t, s, a, g] = lines[i].split(',');
      history.push({ t: +t, s: +s, a: +a, g: +g });
      i++;
    }
  }
  const splits = [];
  while (i < lines.length && !lines[i].startsWith('# splits')) i++;
  i++;
  while (i < lines.length && lines[i].trim() !== '') {
    const [t, ms, reached] = lines[i].split(',');
    splits.push({ t: +t, ms: +ms, reached: reached === '1' });
    i++;
  }
  return { meta, history, splits };
}

function drawGraph(history) {
  const canvas = document.getElementById('graph');
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (!history.length) return;
  const maxSpeed = Math.max(10, ...history.map(p => p.s));
  ctx.strokeStyle = '#4ade80';
  ctx.lineWidth = 2;
  ctx.beginPath();
  history.forEach((p, idx) => {
    const x = (idx / Math.max(1, history.length - 1)) * w;
    const y = h - (p.s / maxSpeed) * h;
    if (idx === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

fetch(url).then(r => {
  if (!r.ok) throw new Error('not found');
  return r.text();
}).then(text => {
  const { meta, history, splits } = parseCsv(text);
  const stamp = meta.startedEpoch > 0
    ? new Date(meta.startedEpoch * 1000).toLocaleString()
    : 'no date recorded';
  if (meta.name) document.getElementById('title').textContent = '0sixty -- ' + meta.name;
  // Author is stamped into each recording's metadata at capture time, so an
  // old report keeps the signature it was recorded under. Falls back to the
  // running firmware's own author if an older file predates the field.
  if (meta.author) {
    document.getElementById('byline').textContent = 'Recorded with 0sixty by ' + meta.author;
  } else {
    fetch('/api/version').then(r => r.json()).then(v => {
      document.getElementById('byline').textContent = 'Recorded with 0sixty by ' + v.author;
    }).catch(() => {});
  }
  document.getElementById('subtitle').textContent = 'Run #' + (meta.id || '?') + ' -- ' + stamp +
    (meta.distanceM ? ' -- ' + (+meta.distanceM).toFixed(0) + ' m' : '');

  const tiles = [
    ['0-60 km/h', meta.zeroSixtyMs > 0 ? fmtMs(meta.zeroSixtyMs) : '--'],
    ['1/4 mile', meta.quarterMileMs > 0 ? fmtMs(meta.quarterMileMs) : '--'],
    ['Trap km/h', meta.quarterMileMs > 0 ? (+meta.quarterMileTrapKmh).toFixed(1) : '--'],
    ['60-130 km/h', meta.rollingSplitMs > 0 ? fmtMs(meta.rollingSplitMs) : '--'],
    ['Max km/h', (+meta.maxSpeedKmh || 0).toFixed(1)],
    ['Distance', ((+meta.distanceM || 0)).toFixed(0) + ' m'],
  ];
  document.getElementById('tiles').innerHTML = tiles.map(([l, v]) =>
    `<div class="tile"><div class="v">${v}</div><div class="l">${l}</div></div>`).join('');

  document.getElementById('splitsTable').innerHTML =
    '<tr><th>Split</th><th>Time</th></tr>' +
    splits.map(s => `<tr><td>0-${s.t} km/h</td><td class="time${s.reached ? ' reached' : ''}">${s.reached ? fmtMs(s.ms) : '--'}</td></tr>`).join('');

  drawGraph(history);
}).catch(() => {
  document.getElementById('subtitle').innerHTML = '<span class="err">Could not load this run (it may no longer exist).</span>';
});

document.getElementById('printBtn').onclick = () => window.print();
</script>
</body>
</html>
)HTML";

void WebServer::setDataSources(PerformanceMode *perf, TripMode *trip, Storage *storage) {
	_perfPtr = perf;
	_tripPtr = trip;
	_storagePtr = storage;
}

// v1.2.0: the live "download last run" export now shares its exact format
// (metadata header + history + splits + quarter mile + rolling split) with
// the SD-persisted run files, via PerformanceMode::toCsv() -- see its
// comment. Was a separate buildRunCsv() here that duplicated the format;
// removed in favor of one shared implementation.

// Chunked generator state for the Trip export routes. One download at a
// time is the expected use (personal single-user device), so this simple
// per-request struct (no global/static state) is enough.
struct KmlLegBuilder {
	String coords;
	int band = -1;
	bool open = false;
	uint32_t startEpoch = 0, endEpoch = 0;
	double distM = 0.0;
	float sumSpeed = 0.0f, maxSpeed = 0.0f;
	int count = 0;

	bool haveLast = false;
	double lastLat = 0.0, lastLng = 0.0;

	int pendingBand = -1;   // candidate band change, not yet committed
	int pendingPoints = 0;
	double pendingDistM = 0.0;

	bool inStop = false;    // run of near-stationary points
	int stopPoints = 0;
	uint32_t stopStartEpoch = 0;
	double stopLat = 0.0, stopLng = 0.0;
};

struct TripExportState {
	size_t pointIdx = 0;
	bool wroteHeader = false;
	bool done = false;
	String carry;
	KmlLegBuilder leg;
	// Timestamp anchor, snapshotted once at the start of each chunk callback
	// rather than re-read per point. The main loop can reset() and
	// startRecording() a new trip on the other core mid-export; re-reading
	// per point would then stamp the tail of trip A's points (still being
	// iterated, since the count was cached) with trip B's anchor.
	uint32_t anchorEpoch = 0;
	uint32_t anchorFirstMs = 0;
};

static void appendKmlHeader(String &out) {
	out += F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n<name>0sixty Trip</name>\n");
	// Shows up in Google My Maps' layer description, so a shared map carries
	// the signature too.
	out += String("<description>Recorded with 0sixty v") + FIRMWARE_VERSION + " by " + AUTHOR_NAME + "</description>\n";
	for (int i = 0; i < TRIP_SPEED_BAND_COUNT; i++) {
		out += "<Style id=\"band" + String(i) + "\"><LineStyle><color>" + TRIP_SPEED_BANDS[i].kmlColor + "</color><width>4</width></LineStyle></Style>\n";
	}
	// v1.4.0: pin style for the stop markers between driving legs.
	out += F("<Style id=\"stop\"><IconStyle><color>ff00a5ff</color><scale>1.1</scale>"
	         "<Icon><href>http://maps.google.com/mapfiles/kml/paddle/P.png</href></Icon>"
	         "</IconStyle></Style>\n");
}

// (v1.4.0 replaced appendKmlPlacemarkOpen/Close -- placemarks are now built
// by kmlEmitLeg(), which can't write the opening tag until the leg's stats
// are known. See the KmlLegBuilder comment above.)

static void appendKmlFooter(String &out) {
	out += F("</Document>\n</kml>\n");
}

// --- v1.4.0: KML leg building ---------------------------------------------
//
// Before this, the exporter opened a new Placemark on every speed-band
// change, named only with the band ("0-30 km/h"), with no timestamps at
// all. Speed hovering near a band edge in traffic therefore shattered one
// drive into hundreds of slivers in Google My Maps' sidebar, none of which
// told you the actual speed or when you were there.
//
// Now a band change has to persist (KML_MIN_SEGMENT_MS / KML_MIN_SEGMENT_M)
// before it splits a leg, stops break legs apart the way Google Timeline
// separates driving from visits, and each finished leg is labelled with its
// real time range, distance and average/max speed.
//
// KML requires <name> before <coordinates>, and a leg's stats aren't final
// until it ends, so the coordinate text is buffered until close. That buffer
// is capped (KML_MAX_SEGMENT_BUFFER) -- a leg that outgrows it is split into
// a same-coloured continuation rather than being allowed to grow unbounded
// on a long motorway run.

static void kmlStartLeg(KmlLegBuilder &L, int band, uint32_t epoch) {
	L.coords = "";
	L.band = band;
	L.open = true;
	L.startEpoch = epoch;
	L.endEpoch = epoch;
	L.distM = 0.0;
	L.sumSpeed = 0.0f;
	L.maxSpeed = 0.0f;
	L.count = 0;
	L.pendingBand = -1;
	L.pendingPoints = 0;
	L.pendingDistM = 0.0;
}

static void kmlEmitLeg(KmlLegBuilder &L, String &out) {
	if (!L.open) {
		L.coords = "";
		return;
	}
	if (L.count < 2) {
		// A 1-point leg has no line to draw, but silently dropping it gaps
		// the route -- and the likeliest place for that is the *end* of a
		// trip: arriving at a destination opens a fresh leg (speed near
		// zero) and recording stops before a second point lands, quietly
		// losing the final position. Emit it as a plain point instead.
		if (L.count == 1 && L.haveLast) {
			String label = (L.startEpoch != 0) ? TimeUtil::hhmm(L.startEpoch)
			                                   : String(TRIP_SPEED_BANDS[L.band].label) + " km/h";
			out += "<Placemark><name>" + label + "</name>";
			out += "<styleUrl>#band" + String(L.band) + "</styleUrl>";
			out += "<Point><coordinates>" + String(L.lastLng, 6) + "," + String(L.lastLat, 6) +
			       ",0</coordinates></Point></Placemark>\n";
		}
		L.open = false;
		L.coords = "";
		return;
	}
	float avg = L.sumSpeed / L.count;
	String label;
	if (L.startEpoch != 0) {
		label += TimeUtil::hhmm(L.startEpoch) + "-" + TimeUtil::hhmm(L.endEpoch) + "  ";
	}
	label += String(L.distM / 1000.0, 1) + " km  " + String((int)(avg + 0.5f)) + " km/h avg";

	out += "<Placemark><name>" + label + "</name>";
	out += "<description>Avg " + String(avg, 1) + " km/h, max " + String(L.maxSpeed, 1) +
	       " km/h over " + String(L.distM / 1000.0, 2) + " km";
	if (L.startEpoch != 0) {
		out += " (" + TimeUtil::hhmm(L.startEpoch) + " to " + TimeUtil::hhmm(L.endEpoch) + ")";
	}
	out += ". Band " + String(TRIP_SPEED_BANDS[L.band].label) + " km/h.</description>";
	out += "<styleUrl>#band" + String(L.band) + "</styleUrl>";
	if (L.startEpoch != 0 && L.endEpoch != 0) {
		out += "<TimeSpan><begin>" + TimeUtil::iso8601Utc(L.startEpoch) +
		       "</begin><end>" + TimeUtil::iso8601Utc(L.endEpoch) + "</end></TimeSpan>";
	}
	out += "<LineString><tessellate>1</tessellate><coordinates>\n" + L.coords +
	       "</coordinates></LineString></Placemark>\n";

	L.open = false;
	L.coords = "";
}

static void kmlEmitStop(String &out, uint32_t startEpoch, uint32_t endEpoch, int points, double lat, double lng) {
	// Duration from the clock when we have one; otherwise inferred from the
	// sample count, so stops are still labelled on a recording made before
	// the GPS clock was ever set.
	uint32_t mins = (startEpoch != 0 && endEpoch > startEpoch)
		? (endEpoch - startEpoch) / 60
		: (uint32_t)((points * (unsigned long)TRIP_SAMPLE_MS) / 60000UL);
	String label = "Stopped";
	if (startEpoch != 0) label += " " + TimeUtil::hhmm(startEpoch) + "-" + TimeUtil::hhmm(endEpoch);
	label += " (" + String(mins) + " min)";

	out += "<Placemark><name>" + label + "</name><styleUrl>#stop</styleUrl>";
	if (startEpoch != 0 && endEpoch != 0) {
		out += "<TimeSpan><begin>" + TimeUtil::iso8601Utc(startEpoch) +
		       "</begin><end>" + TimeUtil::iso8601Utc(endEpoch) + "</end></TimeSpan>";
	}
	out += "<Point><coordinates>" + String(lng, 6) + "," + String(lat, 6) + ",0</coordinates></Point></Placemark>\n";
}

// epoch may be 0 throughout (recording predates the GPS clock) -- everything
// degrades to unlabelled times rather than fabricating any.
static void kmlAddPoint(KmlLegBuilder &L, double lat, double lng, float speedKmh, float altitudeM,
                        uint32_t epoch, String &out) {
	// Distance from the previous point, which may belong to the leg that
	// just closed: the joining step is always credited to the new leg.
	// Accepted approximation -- bounded by one sample interval (~130m at
	// 120km/h), and keeping haveLast/lastLat/lastLng across leg
	// boundaries is what makes the odometer continuous.
	double stepM = L.haveLast ? TripMode::haversineM(L.lastLat, L.lastLng, lat, lng) : 0.0;

	// --- stop detection: ends the current leg and drops a pin ---
	if (speedKmh <= KML_STOP_SPEED_KMH) {
		if (!L.inStop) {
			L.inStop = true;
			L.stopPoints = 0;
			L.stopStartEpoch = epoch;
			L.stopLat = lat;
			L.stopLng = lng;
		}
		L.stopPoints++;
	} else if (L.inStop) {
		bool longEnough = (uint32_t)L.stopPoints * (uint32_t)TRIP_SAMPLE_MS >= KML_STOP_MIN_MS;
		if (longEnough) {
			kmlEmitLeg(L, out);
			kmlEmitStop(out, L.stopStartEpoch, epoch, L.stopPoints, L.stopLat, L.stopLng);
		}
		L.inStop = false;
	}

	int band = TripMode::bandFor(speedKmh);
	if (!L.open) {
		kmlStartLeg(L, band, epoch);
	} else if (band != L.band && !L.inStop) {
		// Band splitting is suppressed while a possible stop is in progress.
		// KML_STOP_MIN_MS (2 min) is six times KML_MIN_SEGMENT_MS (20s), so
		// otherwise every real stop would first trip the ordinary debounce --
		// spawning a near-zero "0-30" leg -- and only later be recognised as
		// a stop, leaving a pin AND a co-located meaningless sliver at every
		// stop. That is precisely the clutter this design exists to avoid.
		// The stopped points instead stay with the leg that was already
		// open, which is closed when the stop is confirmed below.
		if (band != L.pendingBand) {
			L.pendingBand = band;
			L.pendingPoints = 0;
			L.pendingDistM = 0.0;
		}
		L.pendingPoints++;
		L.pendingDistM += stepM;
		bool sustained = ((uint32_t)L.pendingPoints * (uint32_t)TRIP_SAMPLE_MS >= KML_MIN_SEGMENT_MS) ||
		                 (L.pendingDistM >= KML_MIN_SEGMENT_M);
		if (sustained) {
			kmlEmitLeg(L, out);
			kmlStartLeg(L, band, epoch);
		}
	} else if (band == L.band) {
		L.pendingBand = -1; // back within the current band; forget the candidate
	}

	// Keep peak heap bounded on a leg that never changes band. This is a
	// continuation of the same leg, so an in-flight band-change candidate is
	// preserved across it -- kmlStartLeg() would otherwise reset the
	// debounce and delay recognising a real transition that was already
	// partway confirmed.
	if (L.coords.length() > KML_MAX_SEGMENT_BUFFER) {
		int sameBand = L.band;
		int savedPendingBand = L.pendingBand;
		int savedPendingPoints = L.pendingPoints;
		double savedPendingDist = L.pendingDistM;
		kmlEmitLeg(L, out);
		kmlStartLeg(L, sameBand, epoch);
		L.pendingBand = savedPendingBand;
		L.pendingPoints = savedPendingPoints;
		L.pendingDistM = savedPendingDist;
	}

	L.coords += String(lng, 6) + "," + String(lat, 6) + "," + String(altitudeM, 1) + "\n";
	L.distM += stepM;
	L.sumSpeed += speedKmh;
	if (speedKmh > L.maxSpeed) L.maxSpeed = speedKmh;
	L.count++;
	if (epoch != 0) {
		if (L.startEpoch == 0) L.startEpoch = epoch;
		L.endEpoch = epoch;
	}
	L.haveLast = true;
	L.lastLat = lat;
	L.lastLng = lng;
}

static size_t fillFromCarry(String &carry, uint8_t *buffer, size_t maxLen) {
	size_t n = min(carry.length(), maxLen);
	memcpy(buffer, carry.c_str(), n);
	carry.remove(0, n);
	return n;
}

static void WebServer_handleTripKml(AsyncWebServerRequest *request) {
	if (!_tripPtr) {
		request->send(503, "text/plain", "Trip data not available");
		return;
	}
	auto state = std::make_shared<TripExportState>();
	AsyncWebServerResponse *response = request->beginChunkedResponse(
		"application/vnd.google-earth.kml+xml",
		[state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
			int n = _tripPtr->pointCount();
			// Snapshot alongside n, for the reason in TripExportState.
			state->anchorEpoch = _tripPtr->startedEpoch();
			state->anchorFirstMs = _tripPtr->firstSampleMs();
			while (state->carry.length() < maxLen && !state->done) {
				if (!state->wroteHeader) {
					appendKmlHeader(state->carry);
					state->wroteHeader = true;
					continue;
				}
				if ((int)state->pointIdx >= n) {
					kmlEmitLeg(state->leg, state->carry); // flush whatever leg is still open
					appendKmlFooter(state->carry);
					state->done = true;
					continue;
				}
				const TripPoint &p = _tripPtr->points()[state->pointIdx];
				// Each point's millis() stamp becomes a real timestamp by
				// anchoring it to the trip's start epoch. Stays 0 (and so
				// unlabelled) if the trip began before the GPS clock was set.
				// Guarded subtraction: p.tMs < firstMs would underflow an
				// unsigned and bake an absurd date into the leg's name and
				// <TimeSpan>. Reachable via a millis() rollover on a
				// long-running device. Better to omit the time than invent one.
				uint32_t epoch = 0;
				if (state->anchorEpoch != 0 && p.tMs >= state->anchorFirstMs) {
					epoch = state->anchorEpoch + (p.tMs - state->anchorFirstMs) / 1000;
				}
				kmlAddPoint(state->leg, p.lat, p.lng, p.speedKmh, p.altitudeM, epoch, state->carry);
				state->pointIdx++;
			}
			return fillFromCarry(state->carry, buffer, maxLen);
		});
	response->addHeader("Content-Disposition", "attachment; filename=\"trip.kml\"");
	request->send(response);
}

// Same shape as TripExportState -- `carry` MUST persist across filler
// invocations (each call's leftover, un-sent bytes belong to the next
// call). An earlier version declared `carry` as a local String here, which
// silently dropped/corrupted rows at every chunk boundary since almost no
// call ends exactly on a row boundary.
struct TripCsvState {
	size_t idx = 0;
	bool wroteHeader = false;
	String carry;
};

static void WebServer_handleTripCsv(AsyncWebServerRequest *request) {
	if (!_tripPtr) {
		request->send(503, "text/plain", "Trip data not available");
		return;
	}
	auto state = std::make_shared<TripCsvState>();
	AsyncWebServerResponse *response = request->beginChunkedResponse(
		"text/csv",
		[state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
			int n = _tripPtr->pointCount();
			if (!state->wroteHeader) {
				state->carry += String("# recorded with 0sixty v") + FIRMWARE_VERSION + " by " + AUTHOR_NAME + "\n";
				state->carry += "millis,lat,lng,speedKmh,altitudeM\n";
				state->wroteHeader = true;
			}
			while (state->carry.length() < maxLen && (int)state->idx < n) {
				const TripPoint &p = _tripPtr->points()[state->idx];
				state->carry += String(p.tMs) + "," + String(p.lat, 6) + "," + String(p.lng, 6) + "," +
				                 String(p.speedKmh, 2) + "," + String(p.altitudeM, 1) + "\n";
				state->idx++;
			}
			return fillFromCarry(state->carry, buffer, maxLen);
		});
	response->addHeader("Content-Disposition", "attachment; filename=\"trip.csv\"");
	request->send(response);
}

// --- v1.1: historical (SD-backed) trip export ---
// Unlike the live routes above (which read the in-RAM point array), these
// stream from a closed SD file via Storage's mutex-protected read API (see
// storage.h) -- this project's first SD access from a task other than the
// main loop.

struct HistoricalCsvState {
	Storage *storage;
	File file;
	size_t fileSize;
	size_t bytesRead = 0;
};

static void WebServer_handleTripHistoryCsv(AsyncWebServerRequest *request) {
	if (!_storagePtr || !request->hasParam("file")) {
		request->send(400, "text/plain", "Missing file param");
		return;
	}
	String filename = request->getParam("file")->value();
	File f = _storagePtr->openTripForRead(filename);
	if (!f) {
		request->send(404, "text/plain", "Trip file not found");
		return;
	}
	auto state = std::make_shared<HistoricalCsvState>();
	state->storage = _storagePtr;
	state->file = f;
	state->fileSize = f.size();

	AsyncWebServerResponse *response = request->beginChunkedResponse(
		"text/csv",
		[state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
			if (state->bytesRead >= state->fileSize) {
				state->storage->closeTripRead(state->file);
				return 0;
			}
			size_t want = min(maxLen, state->fileSize - state->bytesRead);
			size_t got = state->storage->readTripChunk(state->file, buffer, want);
			state->bytesRead += got;
			if (got == 0) state->storage->closeTripRead(state->file);
			return got;
		});
	response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
	request->send(response);
}

// v1.2.0: identical shape to the trip-history CSV handler above (reuses the
// same HistoricalCsvState struct, which was already generic over any File),
// just pointed at RUNS_DIR via openRunForRead() instead of openTripForRead().
static void WebServer_handleRunHistoryCsv(AsyncWebServerRequest *request) {
	if (!_storagePtr || !request->hasParam("file")) {
		request->send(400, "text/plain", "Missing file param");
		return;
	}
	String filename = request->getParam("file")->value();
	File f = _storagePtr->openRunForRead(filename);
	if (!f) {
		request->send(404, "text/plain", "Run file not found");
		return;
	}
	auto state = std::make_shared<HistoricalCsvState>();
	state->storage = _storagePtr;
	state->file = f;
	state->fileSize = f.size();

	AsyncWebServerResponse *response = request->beginChunkedResponse(
		"text/csv",
		[state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
			if (state->bytesRead >= state->fileSize) {
				state->storage->closeTripRead(state->file);
				return 0;
			}
			size_t want = min(maxLen, state->fileSize - state->bytesRead);
			size_t got = state->storage->readTripChunk(state->file, buffer, want);
			state->bytesRead += got;
			if (got == 0) state->storage->closeTripRead(state->file);
			return got;
		});
	response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
	request->send(response);
}

struct HistoricalKmlState {
	Storage *storage;
	File file;
	size_t fileSize;
	size_t bytesRead = 0;
	String lineAccum;
	String carry;
	bool wroteHeader = false;
	bool done = false;
	KmlLegBuilder leg;
	// v1.4.0 timestamp anchor, both read out of the file as it streams: the
	// startedEpoch from its "# id=..." metadata header, and the millis() of
	// its first data row. Either missing leaves epochs at 0 and the export
	// simply carries no times.
	uint32_t startedEpoch = 0;
	uint32_t firstMs = 0;
	bool haveFirstMs = false;
};

static void processHistoricalLine(HistoricalKmlState *state, const String &line) {
	if (line.length() == 0) return;

	// Metadata/event comment lines carry the timestamp anchor for the whole
	// file. Pre-v1.3 files have no startedEpoch, so this simply stays 0.
	if (line.startsWith("#")) {
		int at = line.indexOf("startedEpoch=");
		if (at >= 0) {
			int valueStart = at + 13; // strlen("startedEpoch=")
			int comma = line.indexOf(',', valueStart);
			String v = (comma < 0) ? line.substring(valueStart) : line.substring(valueStart, comma);
			state->startedEpoch = (uint32_t)v.toInt();
		}
		return;
	}
	if (line.startsWith("millis,")) return; // the CSV column header

	int c1 = line.indexOf(',');
	int c2 = (c1 < 0) ? -1 : line.indexOf(',', c1 + 1);
	int c3 = (c2 < 0) ? -1 : line.indexOf(',', c2 + 1);
	int c4 = (c3 < 0) ? -1 : line.indexOf(',', c3 + 1);
	if (c4 < 0) return; // malformed/partial line, skip rather than crash on a bad substring

	uint32_t tMs = (uint32_t)line.substring(0, c1).toInt();
	double lat = line.substring(c1 + 1, c2).toDouble();
	double lng = line.substring(c2 + 1, c3).toDouble();
	float speedKmh = line.substring(c3 + 1, c4).toFloat();
	float altitudeM = line.substring(c4 + 1).toFloat();

	if (!state->haveFirstMs) {
		state->firstMs = tMs;
		state->haveFirstMs = true;
	}
	// Same guarded subtraction as the live path. This one matters more: tMs
	// comes straight from column 0 of a CSV row with no sanity check, so a
	// torn row (power lost mid-write) or an out-of-order first row could
	// otherwise underflow and bake a nonsense date into the export.
	uint32_t epoch = (state->startedEpoch != 0 && tMs >= state->firstMs)
		? state->startedEpoch + (tMs - state->firstMs) / 1000
		: 0;

	kmlAddPoint(state->leg, lat, lng, speedKmh, altitudeM, epoch, state->carry);
}

static void WebServer_handleTripHistoryKml(AsyncWebServerRequest *request) {
	if (!_storagePtr || !request->hasParam("file")) {
		request->send(400, "text/plain", "Missing file param");
		return;
	}
	String filename = request->getParam("file")->value();
	File f = _storagePtr->openTripForRead(filename);
	if (!f) {
		request->send(404, "text/plain", "Trip file not found");
		return;
	}
	auto state = std::make_shared<HistoricalKmlState>();
	state->storage = _storagePtr;
	state->file = f;
	state->fileSize = f.size();

	AsyncWebServerResponse *response = request->beginChunkedResponse(
		"application/vnd.google-earth.kml+xml",
		[state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
			while (state->carry.length() < maxLen && !state->done) {
				if (!state->wroteHeader) {
					appendKmlHeader(state->carry);
					state->wroteHeader = true;
					continue;
				}
				if (state->bytesRead < state->fileSize) {
					uint8_t raw[128];
					size_t want = min((size_t)sizeof(raw), state->fileSize - state->bytesRead);
					size_t got = state->storage->readTripChunk(state->file, raw, want);
					if (got == 0) {
						state->bytesRead = state->fileSize; // treat a short read as EOF, don't spin
					} else {
						state->bytesRead += got;
						for (size_t i = 0; i < got; i++) {
							char c = (char)raw[i];
							if (c == '\n') {
								processHistoricalLine(state.get(), state->lineAccum);
								state->lineAccum = "";
							} else if (c != '\r') {
								state->lineAccum += c;
							}
						}
					}
					continue;
				}
				if (state->lineAccum.length() > 0) {
					processHistoricalLine(state.get(), state->lineAccum);
					state->lineAccum = "";
				}
				kmlEmitLeg(state->leg, state->carry); // flush whatever leg is still open
				appendKmlFooter(state->carry);
				state->storage->closeTripRead(state->file);
				state->done = true;
			}
			return fillFromCarry(state->carry, buffer, maxLen);
		});
	response->addHeader("Content-Disposition", "attachment; filename=\"" + filename.substring(0, filename.length() - 4) + ".kml\"");
	request->send(response);
}

static void WebServer_handleTripsList(AsyncWebServerRequest *request) {
	if (!_storagePtr) {
		request->send(503, "application/json", "[]");
		return;
	}
	TripSummary trips[TRIP_LIST_MAX];
	int n = _storagePtr->listTrips(trips, TRIP_LIST_MAX);

	JsonDocument doc;
	JsonArray arr = doc.to<JsonArray>();
	for (int i = 0; i < n; i++) {
		JsonObject o = arr.add<JsonObject>();
		o["file"] = trips[i].filename;
		o["name"] = trips[i].name;
		o["startedMs"] = trips[i].startedMs;
		o["startedEpoch"] = trips[i].startedEpoch; // 0 = recorded before the GPS clock was set
		o["ended"] = trips[i].ended;
		o["distanceM"] = trips[i].distanceM;
		o["durationMs"] = trips[i].durationMs;
		o["maxSpeedKmh"] = trips[i].maxSpeedKmh;
	}
	String json;
	serializeJson(doc, json);
	request->send(200, "application/json", json);
}

// v1.2.0: same shape as WebServer_handleTripsList above, for Performance
// runs -- one entry per SD-persisted run, most recent first.
static void WebServer_handleRunsList(AsyncWebServerRequest *request) {
	if (!_storagePtr) {
		request->send(503, "application/json", "[]");
		return;
	}
	RunSummary runs[RUN_LIST_MAX];
	int n = _storagePtr->listRuns(runs, RUN_LIST_MAX);

	JsonDocument doc;
	JsonArray arr = doc.to<JsonArray>();
	for (int i = 0; i < n; i++) {
		JsonObject o = arr.add<JsonObject>();
		o["file"] = runs[i].filename;
		o["name"] = runs[i].name;
		o["startedEpoch"] = runs[i].startedEpoch; // 0 = recorded before the GPS clock was set
		o["startedMs"] = runs[i].startedMs;
		o["elapsedMs"] = runs[i].elapsedMs;
		o["zeroSixtyMs"] = runs[i].zeroSixtyMs;
		o["quarterMileMs"] = runs[i].quarterMileMs;
		o["quarterMileTrapKmh"] = runs[i].quarterMileTrapKmh;
		o["rollingSplitMs"] = runs[i].rollingSplitMs;
		o["maxSpeedKmh"] = runs[i].maxSpeedKmh;
		o["distanceM"] = runs[i].distanceM;
	}
	String json;
	serializeJson(doc, json);
	request->send(200, "application/json", json);
}

void WebServer::begin() {
	// v1.1: open network (no password) -- the on-device QR screen (Q key)
	// replaces typing one in. See config.h's AP_SSID comment.
	WiFi.mode(WIFI_AP);
	WiFi.softAP(AP_SSID);

	// Captive portal: any hostname resolves to this device, and any request
	// that doesn't match a real route below gets redirected to "/" -- the
	// same minimal pattern used by WiFiManager-style captive portals, and
	// how iOS/Android/Windows all recognize "this network wants me to open
	// a browser" and auto-launch one pointed here after the QR-code join.
	_dnsServer.start(53, "*", WiFi.softAPIP());
	_server.onNotFound([](AsyncWebServerRequest *request) {
		request->redirect("http://192.168.4.1/");
	});

	_server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = String("{\"version\":\"") + FIRMWARE_VERSION +
		              "\",\"author\":\"" + AUTHOR_NAME + "\"}";
		request->send(200, "application/json", json);
	});

	_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", DASHBOARD_HTML);
	});

	_server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest *request) {
		AppMode current = Settings::mode();
		// Same reasoning as the on-device M key (main.cpp) -- clear Trip's
		// auto-start/auto-stop hold timers on the way out so a stale one
		// can't immediately fire on the first update() after switching back.
		if (current == AppMode::TRIP && _tripPtr) _tripPtr->onModeExit();
		AppMode next = (current == AppMode::PERFORMANCE) ? AppMode::TRIP : AppMode::PERFORMANCE;
		Settings::setMode(next);
		request->send(200, "text/plain", next == AppMode::TRIP ? "TRIP" : "PERF");
	});

	_server.on("/api/trip/name", HTTP_POST, [](AsyncWebServerRequest *request) {
		if (_tripPtr && request->hasParam("name")) {
			_tripPtr->setName(request->getParam("name")->value());
		}
		request->send(200, "text/plain", "OK");
	});

	_server.on("/api/trips", HTTP_GET, WebServer_handleTripsList);
	_server.on("/api/runs", HTTP_GET, WebServer_handleRunsList);

	// v1.3.0: delete one saved recording. Storage validates the filename
	// (basename only, correct prefix/extension, never the trip currently
	// being written) -- see isSafeRecordingName() in storage.cpp.
	_server.on("/api/trips/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
		if (!_storagePtr || !request->hasParam("file")) {
			request->send(400, "text/plain", "Missing file param");
			return;
		}
		bool ok = _storagePtr->deleteTrip(request->getParam("file")->value());
		request->send(ok ? 200 : 409, "text/plain", ok ? "OK" : "Could not delete that file");
	});
	_server.on("/api/runs/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
		if (!_storagePtr || !request->hasParam("file")) {
			request->send(400, "text/plain", "Missing file param");
			return;
		}
		bool ok = _storagePtr->deleteRun(request->getParam("file")->value());
		request->send(ok ? 200 : 409, "text/plain", ok ? "OK" : "Could not delete that file");
	});

	_server.on("/api/sync/config", HTTP_GET, [](AsyncWebServerRequest *request) {
		JsonDocument doc;
		doc["ssid"] = Settings::homeSsid();
		doc["uploadUrl"] = Settings::uploadUrl();
		String json;
		serializeJson(doc, json);
		request->send(200, "application/json", json);
	});
	_server.on("/api/sync/config", HTTP_POST, [](AsyncWebServerRequest *request) {
		String ssid = request->hasParam("ssid") ? request->getParam("ssid")->value() : "";
		String password = request->hasParam("password") ? request->getParam("password")->value() : "";
		String uploadUrl = request->hasParam("uploadUrl") ? request->getParam("uploadUrl")->value() : "";
		Settings::setSyncConfig(ssid, password, uploadUrl);
		request->send(200, "text/plain", "OK");
	});

	_server.on("/download/run.csv", HTTP_GET, [](AsyncWebServerRequest *request) {
		String csv = _perfPtr ? _perfPtr->toCsv() : String("elapsedMs,speedKmh,altitudeM,accelG\n");
		AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
		response->addHeader("Content-Disposition", "attachment; filename=\"run.csv\"");
		request->send(response);
	});
	_server.on("/download/trip.kml", HTTP_GET, WebServer_handleTripKml);
	_server.on("/download/trip.csv", HTTP_GET, WebServer_handleTripCsv);
	_server.on("/download/trip-history.csv", HTTP_GET, WebServer_handleTripHistoryCsv);
	_server.on("/download/trip-history.kml", HTTP_GET, WebServer_handleTripHistoryKml);
	_server.on("/download/run-history.csv", HTTP_GET, WebServer_handleRunHistoryCsv);
	_server.on("/report/run.html", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", RUN_REPORT_HTML);
	});

	_ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
	               AwsEventType type, void *arg, uint8_t *data, size_t len) {
		(void)server; (void)client; (void)arg; (void)data; (void)len;
		// Broadcast-only; the dashboard's mode switch goes through /api/mode instead.
		if (type == WS_EVT_CONNECT || type == WS_EVT_DISCONNECT) {
			// nothing to do; AsyncWebSocket tracks clients itself
		}
	});
	_server.addHandler(&_ws);

	_server.begin();
}

void WebServer::loop() {
	_dnsServer.processNextRequest();
}

void WebServer::broadcast(const LiveSample &sample, AppMode mode, const PerformanceMode &perf, const TripMode &trip) {
	if (_ws.count() == 0) return; // skip JSON work with nobody connected

	JsonDocument doc;
	doc["hasFix"] = sample.gpsHasFix;
	doc["satellites"] = sample.satellites;
	doc["speedKmh"] = sample.speedKmh;
	doc["lat"] = sample.lat;
	doc["lng"] = sample.lng;
	doc["altitudeM"] = sample.altitudeM;
	doc["batteryPct"] = sample.batteryPct;
	doc["mode"] = (mode == AppMode::TRIP) ? "TRIP" : "PERF";

	JsonObject perfObj = doc["perf"].to<JsonObject>();
	const char *stateStr = "WAIT";
	switch (perf.state()) {
		case RunState::IDLE_WAIT: stateStr = "WAIT"; break;
		case RunState::ARMED: stateStr = "ARMED"; break;
		case RunState::RUNNING: stateStr = "RUN"; break;
		case RunState::FINISHED: stateStr = "DONE"; break;
	}
	perfObj["state"] = stateStr;
	perfObj["elapsedMs"] = perf.elapsedMs();
	perfObj["distanceM"] = perf.distanceM();
	perfObj["maxSpeedKmh"] = perf.maxSpeedKmh();
	perfObj["slopePercent"] = perf.slopePercent();
	perfObj["quarterMileReached"] = perf.quarterMileReached();
	perfObj["quarterMileMs"] = perf.quarterMileTimeMs();
	perfObj["quarterMileTrapKmh"] = perf.quarterMileTrapKmh();
	perfObj["rollingSplitReached"] = perf.rollingSplitReached();
	perfObj["rollingSplitMs"] = perf.rollingSplitMs();
	perfObj["persisted"] = perf.lastRunPersisted();

	JsonArray splitsArr = perfObj["splits"].to<JsonArray>();
	for (int i = 0; i < perf.splitCount(); i++) {
		JsonObject s = splitsArr.add<JsonObject>();
		s["t"] = perf.splits()[i].thresholdKmh;
		s["ms"] = perf.splits()[i].timeMs;
		s["reached"] = perf.splits()[i].reached;
	}

	JsonArray historyArr = perfObj["history"].to<JsonArray>();
	for (int i = 0; i < perf.historyCount(); i++) {
		JsonObject h = historyArr.add<JsonObject>();
		h["s"] = perf.history()[i].speedKmh;
	}

	JsonObject tripObj = doc["trip"].to<JsonObject>();
	const char *tripStateStr = "IDLE";
	switch (trip.tripState()) {
		case TripState::IDLE: tripStateStr = "IDLE"; break;
		case TripState::RECORDING: tripStateStr = "RECORDING"; break;
		case TripState::SUMMARY: tripStateStr = "SUMMARY"; break;
	}
	tripObj["state"] = tripStateStr;
	tripObj["name"] = trip.currentName();
	tripObj["pointCount"] = trip.pointCount();
	tripObj["maxPoints"] = TRIP_MAX_POINTS;
	tripObj["distanceM"] = trip.distanceM();
	tripObj["durationMs"] = trip.durationMs();
	tripObj["maxSpeedKmh"] = trip.maxSpeedKmh();
	tripObj["full"] = trip.isFull();
	tripObj["eventCount"] = trip.eventCount();

	String json;
	serializeJson(doc, json);
	_ws.textAll(json);
}
