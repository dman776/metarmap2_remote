// The remote's web page (remote_web.cpp serves it at /): settings, firmware, the screen and the log. One page, in
// flash; it reads and writes through the /api endpoints. metarmap2's colors.
#pragma once

#include <pgmspace.h>

static const char kRemoteWebPage[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MetarMap2 Copilot</title>
<style>
:root{--bg:#0b1117;--s1:#121a23;--s2:#18222d;--line:#223041;--ctl:#2c3b4e;--tx:#e6edf3;--tx2:#c4d0dd;--mut:#93a4b8;
--acc:#f5b544;--ok:#3ddc84;--bad:#ff5a5a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
header{display:flex;align-items:center;gap:10px;padding:14px 16px;border-bottom:1px solid var(--line)}
header h1{font-size:18px;margin:0;font-weight:600}header .v{color:var(--mut);font:12px ui-monospace,monospace;margin-left:auto}
main{max-width:980px;margin:0 auto;padding:16px;display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr))}
section{min-width:0;background:var(--s1);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
h2{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin:0 0 12px}
label{display:block;font-size:13px;color:var(--tx2);margin:10px 0 4px}
input,select{width:100%;padding:9px 10px;font:inherit;color:var(--tx);background:var(--s2);border:1px solid var(--ctl);border-radius:8px}
button,.btn{display:inline-flex;align-items:center;justify-content:center;min-height:40px;padding:0 14px;margin:12px 8px 0 0;
font-family:inherit;font-size:14px;font-weight:600;color:var(--tx);background:var(--s2);border:1px solid var(--ctl);border-radius:8px;cursor:pointer;text-decoration:none}
button.primary{background:var(--acc);border-color:var(--acc);color:#1a1204}
button:disabled{opacity:.4;cursor:default}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row>*{flex:1;min-width:0}
input,select{min-width:0}
.row.tight{gap:0}.row.tight>*{flex:0 0 auto}
.note{color:var(--mut);font-size:13px;margin:8px 0 0}.err{color:var(--bad)}.okc{color:var(--ok)}
.screen{display:block;width:320px;max-width:100%;aspect-ratio:4/3;border-radius:8px;border:1px solid var(--line);background:#000}
pre{margin:0;height:340px;overflow:auto;background:#05090d;border:1px solid var(--line);border-radius:8px;padding:10px;
font:12px/1.4 ui-monospace,Menlo,monospace;color:var(--tx2);white-space:pre-wrap;word-break:break-word}
.chk{display:inline-flex;gap:6px;align-items:center;margin:12px 12px 0 0;color:var(--tx2);font-size:14px}.chk input{width:auto}
progress{width:100%;height:10px;margin-top:10px;accent-color:var(--acc)}
.wide{grid-column:1/-1}[hidden]{display:none!important}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0}dt{color:var(--mut)}dd{margin:0;font:13px ui-monospace,monospace}
</style></head><body>
<header><h1>MetarMap2 Copilot</h1><span class="v" id="version"></span></header>
<main>

<section class="wide app" hidden>
<h2>Screen</h2>
<img class="screen" id="screen" alt="The Copilot's screen">
<div class="row tight"><label class="chk"><input type="checkbox" id="live"> Live</label>
<button id="snap">Refresh</button><a class="btn" href="/screen.png?download=1">Download PNG</a></div>
</section>

<section class="app" hidden>
<h2>Firmware</h2>
<dl><dt>Installed</dt><dd id="installed"></dd><dt>Available</dt><dd id="available"></dd><dt>Up for</dt><dd id="uptime"></dd></dl>
<label for="channel">Update channel</label>
<select id="channel"><option value="releases">Releases</option><option value="latest">Latest commit</option><option value="branch">Branch</option><option value="release">Pick release</option></select>
<div id="branchRow" hidden><label for="branch">Branch</label><select id="branch"></select></div>
<div id="releaseRow" hidden><label for="release">Release</label><select id="release"></select>
<p class="note">Any of the newest releases, older ones too. Your settings are kept.</p></div>
<button id="check">Check now</button><button class="primary" id="install" disabled>Install</button>
<progress id="progress" max="100" hidden></progress>
<p class="note" id="updMsg"></p>
</section>

<section class="app" hidden>
<h2>Settings</h2>
<form id="connForm">
<label for="wifiName">Wi-Fi network</label><input id="wifiName" required maxlength="32" autocomplete="off">
<label for="wifiPass">Wi-Fi password</label><input id="wifiPass" type="password" placeholder="unchanged" autocomplete="new-password">
<div class="row"><div><label for="mapHost">Chart address</label><input id="mapHost" required placeholder="metarmap.local"></div>
<div style="flex:0 0 96px"><label for="mapPort">Port</label><input id="mapPort" type="number" min="1" max="65535" required></div></div>
<button class="primary">Save and restart</button>
<p class="note" id="connMsg">The Copilot restarts to use them. If it can't join the new network, flash it over USB with include/secrets.h.</p>
</form>
<label for="led">Status light on the back <span id="ledVal"></span></label>
<input id="led" type="range" min="0" max="100" step="1" style="padding:0;accent-color:var(--acc)">
<p class="note" id="ledMsg">Blue: no Wi-Fi (blinking while joining). Green after it reaches the Chart, then dark.
Red: the Chart isn't answering. Purple: Wi-Fi setup. Yellow: installing an update. 0 turns it off.</p>
<label class="chk"><input type="checkbox" id="sounds"> Sounds</label>
<p class="note" id="soundsMsg">The chime when it reaches the Chart, and the pops when you hold a visualizer in the
picker or let go of the splash's volume slider. Not the touch tick or the spoken weather.</p>
<form id="pwForm"><label for="pw1">New web password (user metar)</label><input id="pw1" type="password" autocomplete="new-password" minlength="3">
<label for="pw2">Again</label><input id="pw2" type="password" autocomplete="new-password" minlength="3">
<p class="note err" id="pwDefault" hidden>The password is still the default (map), as on MetarMap2 Web.</p>
<button>Change password</button><p class="note" id="pwMsg"></p></form>
</section>

<section class="wide app" hidden>
<h2>Log</h2>
<pre id="log"></pre>
<div class="row tight"><label class="chk"><input type="checkbox" id="follow" checked> Follow</label>
<button id="clear">Clear view</button><a class="btn" href="/log.txt">Download</a></div>
</section>

</main>
<script>
const $ = s => document.querySelector(s);
async function post(url, data) {
  const r = await fetch(url, {method: 'POST', body: new URLSearchParams(data)});
  const t = await r.text();
  if (!r.ok) throw new Error(t || r.statusText);
  return t;
}
function say(el, text, bad) { el.textContent = text; el.className = 'note' + (bad ? ' err' : ''); }
function dur(s) { const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return (d ? d + 'd ' : '') + (d || h ? h + 'h ' : '') + m + 'm'; }

// ---- the state: firmware, settings (filled once, so typing isn't overwritten) ----
let filled = false, branchesKey = '', releasesKey = '', offline = false;
async function poll() {
  try {
    const r = await fetch('/api/state', {cache: 'no-store'});
    if (!r.ok) throw new Error(r.status);
    const s = await r.json();
    offline = false;
    document.querySelectorAll('.app').forEach(e => e.hidden = false);
    $('#pwDefault').hidden = !s.defaultPassword;
    $('#version').textContent = s.version + ' · ' + s.ip;
    $('#installed').textContent = s.version;
    $('#uptime').textContent = dur(s.uptime);
    const u = s.update;
    if (document.activeElement !== $('#channel')) $('#channel').value = u.channel;
    $('#branchRow').hidden = u.channel !== 'branch';
    const key = u.branches.join(',') + '|' + u.branch;
    if (key !== branchesKey && document.activeElement !== $('#branch')) {
      branchesKey = key;
      const names = u.branches.slice();
      if (u.branch && !names.includes(u.branch)) names.push(u.branch);
      $('#branch').innerHTML = '';
      names.forEach(n => { const o = document.createElement('option'); o.value = o.textContent = n; $('#branch').append(o); });
      $('#branch').value = u.branch;
    }
    const pick = u.channel === 'release';
    $('#releaseRow').hidden = !pick;
    const rkey = u.releases.join(',') + '|' + u.release;
    if (rkey !== releasesKey && document.activeElement !== $('#release')) {
      releasesKey = rkey;
      const tags = u.releases.slice();
      if (u.release && !tags.includes(u.release)) tags.push(u.release);
      $('#release').innerHTML = '';
      if (!u.release) { const o = new Option('Pick a release', '', true, true); o.disabled = true; $('#release').append(o); }
      tags.forEach(t => $('#release').append(new Option(t + (u.releases.includes(t) ? '' : ' (not on GitHub)'), t)));
      $('#release').value = u.release;
    }
    let avail = u.phase === 'checking' ? 'Checking...' : !u.checked ? '' : !u.candidate ? (pick && !u.release ? 'Pick a release' : 'None published')
      : pick ? u.candidate + (u.age ? '  (' + {older: 'older than installed', same: 'same as installed', newer: 'newer than installed'}[u.age] + ')' : '')
      : u.candidate + (u.available ? '' : '  (up to date)');
    $('#available').textContent = avail;
    $('#available').className = u.installable ? 'okc' : '';
    const busy = u.phase === 'installing' || u.phase === 'restarting';
    $('#install').disabled = !u.installable || u.phase !== 'idle';
    $('#install').textContent = pick && u.candidate ? 'Install ' + u.candidate : 'Install';
    $('#check').disabled = busy;
    $('#channel').disabled = $('#branch').disabled = $('#release').disabled = busy;
    $('#progress').hidden = !busy;
    $('#progress').value = u.progress;
    say($('#updMsg'), u.error ? u.error : u.phase === 'installing' ? 'Installing: ' + u.progress + '%'
      : u.phase === 'restarting' ? 'Installed. Restarting...' : '', !!u.error);
    if (!filled) {
      filled = true;
      $('#wifiName').value = s.wifiName; $('#mapHost').value = s.mapHost; $('#mapPort').value = s.mapPort;
      $('#led').value = s.led; $('#ledVal').textContent = s.led + '%';
      $('#sounds').checked = s.sounds !== false;
    }
  } catch (e) {
    if (!offline) say($('#updMsg'), "The Copilot isn't answering (restarting?). Trying again...", true);
    offline = true;
  }
}
setInterval(poll, 2000); poll();

$('#channel').onchange = async () => {
  const ch = $('#channel').value;
  $('#branchRow').hidden = ch !== 'branch';
  $('#releaseRow').hidden = ch !== 'release';
  const data = {channel: ch};
  if (ch === 'branch') data.branch = $('#branch').value || '';
  try { await post('/api/channel', data); poll(); }
  catch (e) { say($('#updMsg'), e.message, true); }
};
$('#release').onchange = async () => {
  try { await post('/api/channel', {channel: 'release', release: $('#release').value}); poll(); }
  catch (e) { say($('#updMsg'), e.message, true); }
};
$('#branch').onchange = async () => {
  try { await post('/api/channel', {channel: 'branch', branch: $('#branch').value}); poll(); }
  catch (e) { say($('#updMsg'), e.message, true); }
};
$('#check').onclick = async () => { try { await post('/api/check', {}); poll(); } catch (e) { say($('#updMsg'), e.message, true); } };
$('#install').onclick = async () => {
  if (!confirm('Install ' + $('#available').textContent.trim() + '? Your settings are kept. The Copilot restarts when it has it.')) return;
  try { await post('/api/install', {}); poll(); } catch (e) { say($('#updMsg'), e.message, true); }
};

$('#connForm').onsubmit = async ev => {
  ev.preventDefault();
  try {
    say($('#connMsg'), await post('/api/connection', {wifi_name: $('#wifiName').value, wifi_pass: $('#wifiPass').value,
      map_host: $('#mapHost').value, map_port: $('#mapPort').value}));
    $('#wifiPass').value = '';
  } catch (e) { say($('#connMsg'), e.message, true); }
};
let ledTimer = null;
$('#led').oninput = () => {
  $('#ledVal').textContent = $('#led').value + '%';
  clearTimeout(ledTimer);  // saved once the slider rests, not on every step
  ledTimer = setTimeout(async () => {
    try { say($('#ledMsg'), await post('/api/led', {brightness: $('#led').value})); }
    catch (e) { say($('#ledMsg'), e.message, true); }
  }, 300);
};
$('#sounds').onchange = async () => {
  try { say($('#soundsMsg'), await post('/api/sounds', {on: $('#sounds').checked ? '1' : '0'})); }
  catch (e) { say($('#soundsMsg'), e.message, true); $('#sounds').checked = !$('#sounds').checked; }
};
async function setPassword(a, b, msg) {
  if (a.value !== b.value) { say(msg, "The two don't match.", true); return false; }
  try { say(msg, await post('/api/password', {password: a.value})); a.value = b.value = ''; return true; }
  catch (e) { say(msg, e.message, true); return false; }
}
$('#pwForm').onsubmit = async ev => { ev.preventDefault(); await setPassword($('#pw1'), $('#pw2'), $('#pwMsg')); };

// ---- the screen: one now, or live (the next as soon as one has arrived) ----
let liveTimer = null;
function snap() { $('#screen').src = '/screen.png?t=' + Date.now(); }
$('#screen').onload = $('#screen').onerror = () => { if ($('#live').checked) liveTimer = setTimeout(snap, 700); };
$('#live').onchange = () => { clearTimeout(liveTimer); if ($('#live').checked) snap(); };
$('#snap').onclick = snap;
snap();

// ---- the log: what's kept, then each new line as it comes ----
let logFrom = 0;
async function tail() {
  try {
    const r = await fetch('/log?from=' + logFrom, {cache: 'no-store'});
    if (r.ok) {
      const end = r.headers.get('X-Log-End');
      if (end !== null && +end < logFrom) logFrom = 0;  // it restarted: from the start of the new log
      else {
        const t = await r.text();
        if (end !== null) logFrom = +end;
        if (t) {
          const pre = $('#log');
          pre.textContent += t;
          if (pre.textContent.length > 200000) pre.textContent = pre.textContent.slice(-150000);
          if ($('#follow').checked) pre.scrollTop = pre.scrollHeight;
        }
      }
    }
  } catch (e) { /* the state poll says so */ }
  setTimeout(tail, 1500);
}
tail();
$('#clear').onclick = () => { $('#log').textContent = ''; };
</script>
</body></html>
)html";
