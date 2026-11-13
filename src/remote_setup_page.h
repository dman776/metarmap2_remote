// The setup page of Wi-Fi config mode (wifi_setup.h), served at / to a phone joined to the remote's own network:
// pick the Wi-Fi (from a scan, or a hidden one by name) and the map's address. No login. metarmap2's colors.
#pragma once

#include <pgmspace.h>

static const char kRemoteSetupPage[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MetarMap2 Copilot setup</title>
<style>
:root{--bg:#0b1117;--s1:#121a23;--s2:#18222d;--line:#223041;--ctl:#2c3b4e;--tx:#e6edf3;--tx2:#c4d0dd;--mut:#93a4b8;
--acc:#f5b544;--ok:#3ddc84;--bad:#ff5a5a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 2px}.sub{color:var(--mut);font-size:14px;margin:0 0 16px}
section{background:var(--s1);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}
h2{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin:0 0 10px;display:flex;align-items:center}
h2 button{margin:0 0 0 auto;min-height:32px;font-size:13px}
.nets{display:flex;flex-direction:column;gap:6px}
.net{display:flex;align-items:center;gap:10px;min-height:48px;padding:0 12px;border:1px solid var(--ctl);border-radius:10px;
background:var(--s2);color:var(--tx);font:inherit;text-align:left;cursor:pointer;width:100%}
.net[aria-pressed=true]{border-color:var(--acc);background:#2a2210;color:var(--acc)}
.net .n{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net .m{color:var(--mut);font-size:13px;white-space:nowrap}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:14px}.bars i{width:3px;background:var(--ctl);border-radius:1px}
.bars i.on{background:currentColor}
label{display:block;font-size:14px;color:var(--tx2);margin:12px 0 4px}
input{width:100%;min-width:0;padding:11px 12px;font:inherit;color:var(--tx);background:var(--s2);border:1px solid var(--ctl);border-radius:10px}
.row{display:flex;gap:10px}.row>div{flex:1;min-width:0}.row>div.port{flex:0 0 96px}
button{min-height:44px;padding:0 16px;font:inherit;font-weight:600;color:var(--tx);background:var(--s2);border:1px solid var(--ctl);border-radius:10px;cursor:pointer}
.primary{width:100%;margin-top:4px;background:var(--acc);border-color:var(--acc);color:#1a1204}
.note{color:var(--mut);font-size:14px;margin:10px 0 0}.err{color:var(--bad)}.okc{color:var(--ok)}
[hidden]{display:none!important}
</style></head><body><main>
<h1>MetarMap2 Copilot</h1>
<p class="sub" id="sub">Set up its Wi-Fi and the Chart it controls.</p>

<form id="form">
<section>
<h2>Wi-Fi network <button type="button" id="rescan">Scan again</button></h2>
<div class="nets" id="nets"><p class="note">Looking for networks...</p></div>
<div id="otherBox" hidden><label for="other">Network name</label><input id="other" maxlength="32" autocomplete="off" autocapitalize="none"></div>
<div id="passBox" hidden><label for="pass">Password</label><input id="pass" type="password" maxlength="63" autocomplete="off"></div>
<p class="note">The Copilot uses 2.4 GHz Wi-Fi only.</p>
</section>

<section>
<h2>The Chart</h2>
<div class="row"><div><label for="host">Address</label><input id="host" required maxlength="63" autocapitalize="none" placeholder="metarmap.local"></div>
<div class="port"><label for="port">Port</label><input id="port" type="number" min="1" max="65535" required></div></div>
<p class="note">Its name or IP, as you open its web page: metarmap.local unless you gave the Pi another hostname.
Not sure? Leave it: on its first start the Copilot looks for the Chart itself, uses it if there's one and lists
them if there are several. Later, pick another on the Copilot's logo screen (the globe at its top left).</p>
</section>

<button class="primary" id="save">Save and connect</button>
<p class="note" id="msg"></p>
</form>
</main>
<script>
const $ = s => document.querySelector(s);
let chosen = null, secure = true, filled = false, saved = false;

function bars(rssi) {
  const n = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  return '<span class="bars">' + [6, 9, 12, 14].map((h, i) => '<i style="height:' + h + 'px"' + (i < n ? ' class="on"' : '') + '></i>').join('') + '</span>';
}
function esc(t) { const d = document.createElement('div'); d.textContent = t; return d.innerHTML; }

let other = false;  // "Other network..." is chosen
// Marks the chosen network (after a tap, or the list being redrawn) and shows the fields it needs
function mark() {
  const key = other ? '\u0000' : chosen;
  document.querySelectorAll('.net').forEach(b => b.setAttribute('aria-pressed', b.dataset.ssid === key ? 'true' : 'false'));
  $('#otherBox').hidden = !other;
  $('#passBox').hidden = !other && (chosen === null || !secure);
}
// A tap: and the cursor goes where it's needed next (only on a tap: the list is redrawn while you type)
function choose(ssid, isSecure) {
  other = ssid === null; chosen = ssid; secure = isSecure;
  mark();
  if (other) $('#other').focus(); else if (isSecure) $('#pass').focus();
}

async function load() {
  if (saved) return;
  try {
    const r = await fetch('/setup/networks', {cache: 'no-store'});
    const s = await r.json();
    $('#sub').textContent = 'Set up its Wi-Fi and the Chart it controls. (' + s.apName + ', ' + s.version + ')';
    if (!filled) {
      filled = true;
      $('#host').value = s.mapHost; $('#port').value = s.mapPort;
      if (s.wifiName) chosen = s.wifiName;
    }
    const box = $('#nets');
    box.innerHTML = '';
    if (!s.networks.length) box.innerHTML = '<p class="note">' + (s.scanning ? 'Looking for networks...' : 'No networks found. Scan again, or enter one below.') + '</p>';
    s.networks.forEach(n => {
      const b = document.createElement('button');
      b.type = 'button'; b.className = 'net'; b.dataset.ssid = n.ssid;
      b.innerHTML = '<span class="n">' + esc(n.ssid) + '</span><span class="m">' + (n.secure ? '&#128274;' : 'open') + '</span>' + bars(n.rssi);
      b.onclick = () => choose(n.ssid, n.secure);
      box.append(b);
    });
    const o = document.createElement('button');
    o.type = 'button'; o.className = 'net'; o.dataset.ssid = '\u0000';
    o.innerHTML = '<span class="n">Other network...</span>';
    o.onclick = () => choose(null, true);
    box.append(o);
    if (!other && chosen !== null) {
      const known = s.networks.find(n => n.ssid === chosen);
      if (known) secure = known.secure;
    }
    mark();
    $('#rescan').disabled = s.scanning;
    setTimeout(load, s.scanning ? 1500 : 6000);
  } catch (e) { setTimeout(load, 3000); }
}
load();

$('#rescan').onclick = async () => { await fetch('/setup/rescan', {method: 'POST', headers: {'X-Copilot': '1'}}); $('#rescan').disabled = true; setTimeout(load, 1500); };

$('#form').onsubmit = async ev => {
  ev.preventDefault();
  const name = other ? $('#other').value.trim() : chosen;
  const msg = $('#msg');
  if (!name) { msg.className = 'note err'; msg.textContent = 'Choose a Wi-Fi network.'; return; }
  const pass = (other || secure) ? $('#pass').value : '';
  try {
    const r = await fetch('/setup/save', {method: 'POST', headers: {'X-Copilot': '1'}, body: new URLSearchParams({wifi_name: name, wifi_pass: pass,
      map_host: $('#host').value.trim(), map_port: $('#port').value})});
    const t = await r.text();
    if (!r.ok) throw new Error(t);
    saved = true;
    msg.className = 'note okc';
    msg.textContent = 'Saved. The Copilot restarts and joins ' + name + '. Put this phone back on your usual Wi-Fi; '
      + "if the Copilot can't join it, tap Wi-Fi setup on its screen to try again.";
    $('#save').disabled = true;
  } catch (e) { msg.className = 'note err'; msg.textContent = e.message || "The Copilot didn't answer."; }
};
</script>
</body></html>
)html";
