#include "remote_web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp32s3/rom/miniz.h>
#include <esp_heap_caps.h>

#include <string>
#include <vector>

#include "audio.h"
#include "firmware_version.h"
#include "log.h"
#include "lvgl_port.h"
#include "png.h"
#include "remote_setup_page.h"
#include "remote_web_page.h"
#include "settings.h"
#include "ui.h"
#include "ui_control.h"
#include "update_check.h"
#include "updater.h"
#include "web_guard.h"
#include "wifi_setup.h"

namespace {

constexpr const char* kUser = "metar";  // as the map's own web UI
constexpr const char* kRealm = "MetarMap2 Copilot";
constexpr uint32_t kRestartAfterMs = 1500;  // after saving the connection: time for the answer to get out

WebServer server(80);
Updater* updater = nullptr;
Settings* settings = nullptr;
bool started = false;
bool announced = false;  // mDNS and the address: once the remote is on the Wi-Fi
uint32_t restartAt = 0;
std::string address;

const char* phaseName(UpdateState::Phase phase) {
  switch (phase) {
    case UpdateState::Checking:
      return "checking";
    case UpdateState::Installing:
      return "installing";
    case UpdateState::Restarting:
      return "restarting";
    default:
      return "idle";
  }
}

// PNG data compressed by the ESP32-S3 ROM's deflate (miniz, built without malloc: its 150 KB of state is ours, in
// PSRAM). A screenshot shrinks from 230 KB to a few tens: quick to send, so the live view doesn't stall the screen.
std::vector<uint8_t> romDeflate(const std::vector<uint8_t>& raw) {
  static tdefl_compressor* comp =
      static_cast<tdefl_compressor*>(heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM));
  if (!comp) return {};
  // few probes, greedy: fast, and a UI's flat colors compress well anyway
  if (tdefl_init(comp, nullptr, nullptr, TDEFL_WRITE_ZLIB_HEADER | TDEFL_GREEDY_PARSING_FLAG | 16) != TDEFL_STATUS_OKAY)
    return {};
  std::vector<uint8_t> out(raw.size() + 1024);
  size_t in = raw.size(), outSize = out.size();
  if (tdefl_compress(comp, raw.data(), &in, out.data(), &outSize, TDEFL_FINISH) != TDEFL_STATUS_DONE) return {};
  out.resize(outSize);
  return out;
}

// A POST carries X-Copilot: 1, which another site's page can't add (web_guard.h). Alone for the setup page's POSTs.
bool headerSent() {
  if (web_guard::headerOk(server.header(web_guard::kHeader).c_str())) return true;
  LOGF("web: %s refused: no X-Copilot header\n", server.uri().c_str());
  server.send(403, "text/plain", web_guard::kNoHeader);
  return false;
}

// The Host names the Copilot (an IPv4 address, or metarmap-copilot.local), not a rebinding page's name
bool hostAllowed() {
  // collected per request (start()): WebServer's hostHeader() keeps the last request's when one has none
  String host = server.header("Host");
  if (web_guard::hostOk(host.c_str())) return true;
  LOGF("web: %s refused: Host \"%s\"\n", server.uri().c_str(), host.c_str());
  server.send(403, "text/plain", web_guard::kBadHost);
  return false;
}

// The login: user metar, the password (map until it's changed, as the map's own web UI)
bool loggedIn() {
  if (server.authenticate(kUser, settings->webPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, kRealm);
  return false;
}

// Every GET behind the login: the Host, then the login
bool allowed() { return hostAllowed() && loggedIn(); }

// Every POST behind the login: the Host, the X-Copilot header (before the login, so a forged one gets no login
// prompt), then the login
bool postAllowed() { return hostAllowed() && headerSent() && loggedIn(); }

std::string arg(const char* name) {
  std::string v = server.hasArg(name) ? server.arg(name).c_str() : "";
  while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
  while (!v.empty() && isspace((unsigned char)v.front())) v.erase(0, 1);
  return v;
}

void sendJson(JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void onPage() {
  if (wifi_setup_active()) {  // config mode: the setup page, no login (it's a phone on the remote's own network)
    server.send_P(200, "text/html", kRemoteSetupPage);
    return;
  }
  if (!allowed()) return;
  server.send_P(200, "text/html", kRemoteWebPage);
}

void onState() {
  if (!allowed()) return;
  static UpdateState st;
  static uint32_t rev = 0;
  updater->get(st, rev);
  JsonDocument doc;
  doc["version"] = FIRMWARE_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["wifiName"] = settings->wifiName;
  doc["mapHost"] = settings->mapHost;
  doc["mapPort"] = settings->mapPort;
  doc["uptime"] = millis() / 1000;
  doc["defaultPassword"] = web_guard::isDefaultPassword(settings->webPassword);
  doc["led"] = settings->ledBrightness;
  doc["sounds"] = settings->sounds;
  doc["remoteControl"] = settings->remoteControl;
  JsonObject u = doc["update"].to<JsonObject>();
  u["phase"] = phaseName(st.phase);
  u["channel"] = update::channelName(st.channel);
  u["branch"] = st.branch;
  u["release"] = st.release;
  u["checked"] = st.checked;
  u["available"] = st.available;
  u["installable"] = st.installable;
  u["age"] = st.age == update::Age::Older   ? "older"
             : st.age == update::Age::Same  ? "same"
             : st.age == update::Age::Newer ? "newer"
                                            : "";
  u["candidate"] = st.candidate;
  u["error"] = st.error;
  u["progress"] = st.progress;
  JsonArray branches = u["branches"].to<JsonArray>();
  for (const std::string& b : st.branches) branches.add(b);
  JsonArray releases = u["releases"].to<JsonArray>();
  for (const std::string& r : st.releases) releases.add(r);
  sendJson(doc);
}

// Saves the Wi-Fi network and the map's address from the request, then restarts to use them. `keepPassword`: a blank
// password keeps the saved one (the settings page); otherwise blank is an open network (the setup page).
void saveConnection(bool keepPassword) {
  std::string name = arg("wifi_name"), host = arg("map_host"), port = arg("map_port");
  int portNum = atoi(port.c_str());
  if (name.empty() || name.size() > 32) {
    server.send(400, "text/plain", "The Wi-Fi network's name is needed (at most 32 characters).");
    return;
  }
  if (host.empty() || host.size() > 63 || host.find_first_of(" /:") != std::string::npos) {
    server.send(400, "text/plain", "The Chart's address is a name or an IP, like metarmap.local or 192.168.1.50.");
    return;
  }
  if (portNum < 1 || portNum > 65535) {
    server.send(400, "text/plain", "The Chart's port is a number from 1 to 65535.");
    return;
  }
  if (isSetupSsid(name)) {
    server.send(400, "text/plain", "That's the name a Copilot has before it's set up: choose your Wi-Fi network.");
    return;
  }
  std::string password = server.hasArg("wifi_pass") ? server.arg("wifi_pass").c_str() : "";
  if (!password.empty() && (password.size() < 8 || password.size() > 63)) {
    server.send(400, "text/plain", "A Wi-Fi password has 8 to 63 characters (or none, for an open network).");
    return;
  }
  settings->wifiName = name;
  if (!password.empty() || !keepPassword) settings->wifiPassword = password;
  // A map is chosen: on the settings page, always (its Save is the Chart address too); on the setup page, when its
  // address was changed from the one shown (a first start's metarmap.local left as it is: the Copilot looks for one)
  bool changed = host != settings->mapHost || (uint16_t)portNum != settings->mapPort;
  settings->mapChosen = settings->mapChosen || keepPassword || changed;
  if (changed) {
    settings->mapIp.clear();
    settings->mapName.clear();
  }
  settings->mapHost = host;
  settings->mapPort = (uint16_t)portNum;
  settings_save_connection(*settings);
  LOGF("web: connection saved (%s, map %s:%d); restarting\n", name.c_str(), host.c_str(), portNum);
  server.send(200, "text/plain", "Saved. The Copilot restarts to use them.");
  restartAt = millis() + kRestartAfterMs;
}

void onConnection() {
  if (!postAllowed()) return;
  saveConnection(true);
}

// ---- config mode: the setup page's network list and saving, and the captive portal ----

void onSetupNetworks() {
  if (!wifi_setup_active()) {
    server.send(404, "text/plain", "not found");
    return;
  }
  bool scanning = false;
  std::vector<ScannedNetwork> found = wifi_setup_networks(scanning);
  JsonDocument doc;
  doc["apName"] = wifi_setup_ap_name();
  doc["scanning"] = scanning;
  doc["wifiName"] = isSetupSsid(settings->wifiName) ? "" : settings->wifiName;
  doc["mapHost"] = settings->mapHost;
  doc["mapPort"] = settings->mapPort;
  doc["version"] = FIRMWARE_VERSION;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (const ScannedNetwork& n : found) {
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = n.ssid;
    o["rssi"] = n.rssi;
    o["secure"] = n.secure;
  }
  sendJson(doc);
}

void onSetupRescan() {
  if (!wifi_setup_active()) {
    server.send(404, "text/plain", "not found");
    return;
  }
  if (!headerSent()) return;
  wifi_setup_rescan();
  server.send(200, "text/plain", "OK");
}

void onSetupSave() {
  if (!wifi_setup_active()) {
    server.send(404, "text/plain", "not found");
    return;
  }
  if (!headerSent()) return;
  saveConnection(false);
}

// Anything else, in config mode: to the setup page. A phone joining the network asks a known address (Apple's
// hotspot-detect.html, Android's generate_204, Windows' connecttest.txt) and, sent elsewhere, shows its "sign in to
// network" sheet with the setup page.
void onNotFound() {
  if (wifi_setup_active()) {
    server.sendHeader("Location", wifi_setup_address());
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

// channel=releases|latest|branch|release, with branch=<name> for branch and release=<tag> (v26.11.07) for release.
// The one not sent stays as saved.
void onChannel() {
  if (!postAllowed()) return;
  std::string channel = arg("channel"), branch, release;
  updater->choice(branch, release);
  if (server.hasArg("branch")) branch = arg("branch");
  if (server.hasArg("release")) release = arg("release");
  if (channel != "releases" && channel != "latest" && channel != "branch" && channel != "release") {
    server.send(400, "text/plain", "channel: releases, latest, branch or release");
    return;
  }
  if (!release.empty() && update::versionParts(release).empty()) {
    server.send(400, "text/plain", "release: a release's tag, like v26.11.07");
    return;
  }
  updater->setChannel(update::channelFromName(channel), branch, release);
  server.send(200, "text/plain", "OK");
}

void onCheck() {
  if (!postAllowed()) return;
  updater->check();
  server.send(200, "text/plain", "OK");
}

void onInstall() {
  if (!postAllowed()) return;
  LOGF("web: install asked for\n");
  updater->install();
  server.send(200, "text/plain", "OK");
}

void onPassword() {
  if (!postAllowed()) return;
  std::string password = server.hasArg("password") ? server.arg("password").c_str() : "";
  if (password.size() < 3 || password.size() > 64) {
    server.send(400, "text/plain", "The password needs 3 to 64 characters.");
    return;
  }
  settings->webPassword = password;
  settings_save_web_password(password);
  LOGF("web: password set\n");
  server.send(200, "text/plain", "Saved. Log in as metar with the new password.");
}

// The status light's brightness, 0-100 (%): on at once (main.cpp reads it each loop), and saved
void onLed() {
  if (!postAllowed()) return;
  std::string v = arg("brightness");
  char* end = nullptr;
  long pct = strtol(v.c_str(), &end, 10);
  if (v.empty() || *end || pct < 0 || pct > 100) {
    server.send(400, "text/plain", "The status light's brightness is 0 to 100 (%).");
    return;
  }
  settings->ledBrightness = (int)pct;
  settings_save_led((int)pct);
  server.send(200, "text/plain", pct ? "Saved." : "Saved: the status light is off.");
}

// The sounds (the chime, the picker's pops): on=1 or 0; at once (audio_set_sounds()), and saved
void onSounds() {
  if (!postAllowed()) return;
  std::string v = arg("on");
  if (v != "0" && v != "1") {
    server.send(400, "text/plain", "on: 1 or 0.");
    return;
  }
  settings->sounds = v == "1";
  audio_set_sounds(settings->sounds);
  settings_save_sounds(settings->sounds);
  LOGF("web: sounds %s\n", settings->sounds ? "on" : "off");
  server.send(200, "text/plain", settings->sounds ? "Saved: sounds on." : "Saved: sounds off.");
}

// Remote control (for screenshots): on=1 or 0; at once, and saved. Off at first.
void onRemoteControl() {
  if (!postAllowed()) return;
  std::string v = arg("on");
  if (v != "0" && v != "1") {
    server.send(400, "text/plain", "on: 1 or 0.");
    return;
  }
  if (v == "1" && web_guard::isDefaultPassword(settings->webPassword)) {
    LOGF("web: remote control not turned on: the password is the default\n");
    server.send(403, "text/plain", web_guard::kDefaultPasswordRemote);
    return;
  }
  settings->remoteControl = v == "1";
  settings_save_remote_control(settings->remoteControl);
  LOGF("web: remote control %s\n", settings->remoteControl ? "on" : "off");
  server.send(200, "text/plain", settings->remoteControl ? "Saved: remote control on." : "Saved: remote control off.");
}

// ---- remote control of the screens, for screenshots (ui_control.h): behind the login and the setting ----
//
// Each runs here, on loop()'s task, which LVGL runs on: ui_control_*() do what the tap does, let the screen settle
// (up to a second) and draw it before the answer goes, so a GET /screen.png after it shows the result.

ui_control::Arg param(const char* name) {
  ui_control::Arg a;
  a.given = server.hasArg(name);
  if (a.given) a.value = arg(name);
  return a;
}

// A POST's guards and the login (403, 401), then the setting and a password that isn't the default (403): one turned
// on before the default was refused (or the password set back to map) does nothing until the password is changed
bool controlAllowed() {
  if (!postAllowed()) return false;
  if (!settings->remoteControl) {
    server.send(403, "text/plain",
                "Remote control is off. Turn on \"Remote control (for screenshots)\" in the Copilot's web page's "
                "Settings.");
    return false;
  }
  if (web_guard::isDefaultPassword(settings->webPassword)) {
    server.send(403, "text/plain", web_guard::kDefaultPasswordRemote);
    return false;
  }
  return true;
}

void sendState() {
  ui_control::State st = ui_control_state();
  st.remoteControl = settings->remoteControl;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", ui_control::stateJson(st).c_str());
}

// What a control did: the state after it (200), or why not, as text
void answer(const ui_control::Outcome& r) {
  if (r.ok()) {
    sendState();
    return;
  }
  LOGF("web: ui: %d %s\n", r.status, r.text.c_str());
  server.send(r.status, "text/plain", r.text.c_str());
}

void onUiState() {
  if (!allowed()) return;  // answers with remote control off too, so a script can say it's off
  sendState();
}

void onUiShow() {
  if (!controlAllowed()) return;
  ui_control::ShowRequest req;
  ui_control::Outcome r = ui_control::parseShow(param("screen"), param("page"), req);
  if (r.ok()) {
    std::string what = ui_control::screenName(req.screen);
    if (req.page) what += " page " + std::to_string(req.page);
    LOGF("web: ui show %s\n", what.c_str());
    r = ui_control_show(req);
  }
  answer(r);
}

void onUiCard() {
  if (!controlAllowed()) return;
  ui_control::CardRequest req;
  ui_control::Outcome r =
      ui_control::parseCard(param("airport"), param("position"), param("page"), param("button"), req);
  if (r.ok()) {
    std::string what = req.airport;
    if (req.position) what += "position " + std::to_string(req.position);
    if (req.page) what += " page " + std::to_string(req.page);
    if (req.button != ui_control::Button::None)
      what += std::string(what.empty() ? "" : " ") + ui_control::buttonName(req.button);
    LOGF("web: ui card %s\n", what.c_str());
    r = ui_control_card(req);
  }
  answer(r);
}

void onUiVisualizer() {
  if (!controlAllowed()) return;
  std::string id;
  ui_control::Outcome r = ui_control::parseVisualizer(param("id"), id);
  if (r.ok()) {
    LOGF("web: ui visualizer %s\n", id.c_str());
    r = ui_control_visualizer(id);
  }
  answer(r);
}

void onScreen() {
  if (!allowed()) return;
  lv_refr_now(nullptr);  // anything changed since the last frame drawn first
  static std::vector<uint16_t> pixels;
  int w = 0, h = 0;
  lvgl_port_capture(pixels, w, h);
  std::vector<uint8_t> png = png_from_rgb565(w, h, pixels.data(), romDeflate);
  server.sendHeader("Cache-Control", "no-store");
  if (server.hasArg("download")) {
    server.sendHeader("Content-Disposition",
                      ("attachment; filename=\"copilot-screen-" + std::to_string(millis() / 1000) + ".png\"").c_str());
  }
  server.setContentLength(png.size());
  server.send(200, "image/png", "");
  server.sendContent((const char*)png.data(), png.size());
}

void onLog() {
  if (!allowed()) return;
  uint64_t from = server.hasArg("from") ? strtoull(server.arg("from").c_str(), nullptr, 10) : 0;
  std::string text = log_since(from);
  server.sendHeader("X-Log-End", String((unsigned long long)from));
  server.sendHeader("Access-Control-Expose-Headers", "X-Log-End");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", text.c_str());
}

void onLogFile() {
  if (!allowed()) return;
  uint64_t from = 0;
  std::string text = log_since(from);
  server.sendHeader("Content-Disposition", "attachment; filename=\"copilot-log.txt\"");
  server.send(200, "text/plain; charset=utf-8", text.c_str());
}

void start() {
  // kept for each request (reset by the next): the POSTs' header, and the Host (web_guard.h)
  static const char* collect[] = {web_guard::kHeader, "Host"};
  server.collectHeaders(collect, 2);
  server.on("/", HTTP_GET, onPage);
  server.on("/api/state", HTTP_GET, onState);
  server.on("/api/connection", HTTP_POST, onConnection);
  server.on("/api/channel", HTTP_POST, onChannel);
  server.on("/api/check", HTTP_POST, onCheck);
  server.on("/api/install", HTTP_POST, onInstall);
  server.on("/api/password", HTTP_POST, onPassword);
  server.on("/api/led", HTTP_POST, onLed);
  server.on("/api/sounds", HTTP_POST, onSounds);
  server.on("/api/remote-control", HTTP_POST, onRemoteControl);
  server.on("/api/ui/state", HTTP_GET, onUiState);
  server.on("/api/ui/show", HTTP_POST, onUiShow);
  server.on("/api/ui/card", HTTP_POST, onUiCard);
  server.on("/api/ui/visualizer", HTTP_POST, onUiVisualizer);
  server.on("/screen.png", HTTP_GET, onScreen);
  server.on("/log", HTTP_GET, onLog);
  server.on("/log.txt", HTTP_GET, onLogFile);
  server.on("/setup/networks", HTTP_GET, onSetupNetworks);
  server.on("/setup/rescan", HTTP_POST, onSetupRescan);
  server.on("/setup/save", HTTP_POST, onSetupSave);
  server.onNotFound(onNotFound);
  server.begin();
  started = true;
}

// On the Wi-Fi: its name on the network, and the address the logo's splash shows
void announce() {
  if (MDNS.begin("metarmap-copilot")) MDNS.addService("http", "tcp", 80);
  address = "http://" + std::string(WiFi.localIP().toString().c_str()) + "/";
  announced = true;
  LOGF("web: %s (http://metarmap-copilot.local/)\n", address.c_str());
}

}  // namespace

void remote_web_begin(Updater& u, Settings& s) {
  updater = &u;
  settings = &s;
}

void remote_web_tick() {
  if (!settings) return;
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
  bool online = WiFi.status() == WL_CONNECTED;
  if (!started) {
    if (!online && !wifi_setup_active()) return;
    start();
  }
  if (online && !announced) announce();
  server.handleClient();
}

const char* remote_web_address() { return address.c_str(); }
