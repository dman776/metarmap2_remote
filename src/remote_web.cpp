#include "remote_web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp32s3/rom/miniz.h>
#include <esp_heap_caps.h>

#include <string>
#include <vector>

#include "firmware_version.h"
#include "log.h"
#include "lvgl_port.h"
#include "png.h"
#include "remote_web_page.h"
#include "settings.h"
#include "update_check.h"
#include "updater.h"

namespace {

constexpr const char* kUser = "metar";  // as the map's own web UI
constexpr const char* kRealm = "MetarMap2 Remote";
constexpr uint32_t kRestartAfterMs = 1500;  // after saving the connection: time for the answer to get out

WebServer server(80);
Updater* updater = nullptr;
Settings* settings = nullptr;
bool started = false;
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

// The login: user metar, the password (map until it's changed, as the map's own web UI)
bool allowed() {
  if (server.authenticate(kUser, settings->webPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, kRealm);
  return false;
}

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
  doc["defaultPassword"] = settings->webPassword == kDefaultWebPassword;
  JsonObject u = doc["update"].to<JsonObject>();
  u["phase"] = phaseName(st.phase);
  u["channel"] = update::channelName(st.channel);
  u["branch"] = st.branch;
  u["checked"] = st.checked;
  u["available"] = st.available;
  u["candidate"] = st.candidate;
  u["error"] = st.error;
  u["progress"] = st.progress;
  JsonArray branches = u["branches"].to<JsonArray>();
  for (const std::string& b : st.branches) branches.add(b);
  sendJson(doc);
}

void onConnection() {
  if (!allowed()) return;
  std::string name = arg("wifi_name"), host = arg("map_host"), port = arg("map_port");
  int portNum = atoi(port.c_str());
  if (name.empty() || name.size() > 32) {
    server.send(400, "text/plain", "The Wi-Fi network's name is needed (at most 32 characters).");
    return;
  }
  if (host.empty() || host.size() > 63 || host.find_first_of(" /:") != std::string::npos) {
    server.send(400, "text/plain", "The map's address is a name or an IP, like metarmap.local or 192.168.1.50.");
    return;
  }
  if (portNum < 1 || portNum > 65535) {
    server.send(400, "text/plain", "The map's port is a number from 1 to 65535.");
    return;
  }
  settings->wifiName = name;
  if (server.hasArg("wifi_pass") && !server.arg("wifi_pass").isEmpty()) {  // blank: keep the one saved
    settings->wifiPassword = server.arg("wifi_pass").c_str();
  }
  settings->mapHost = host;
  settings->mapPort = (uint16_t)portNum;
  settings_save_connection(*settings);
  LOGF("web: connection saved (%s, map %s:%d); restarting\n", name.c_str(), host.c_str(), portNum);
  server.send(200, "text/plain", "Saved. The remote restarts to use them.");
  restartAt = millis() + kRestartAfterMs;
}

void onChannel() {
  if (!allowed()) return;
  std::string channel = arg("channel"), branch = arg("branch");
  if (channel != "releases" && channel != "latest" && channel != "branch") {
    server.send(400, "text/plain", "channel: releases, latest or branch");
    return;
  }
  updater->setChannel(update::channelFromName(channel), branch);
  server.send(200, "text/plain", "OK");
}

void onCheck() {
  if (!allowed()) return;
  updater->check();
  server.send(200, "text/plain", "OK");
}

void onInstall() {
  if (!allowed()) return;
  LOGF("web: install asked for\n");
  updater->install();
  server.send(200, "text/plain", "OK");
}

void onPassword() {
  if (!allowed()) return;
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

void onScreen() {
  if (!allowed()) return;
  static std::vector<uint16_t> pixels;
  int w = 0, h = 0;
  lvgl_port_capture(pixels, w, h);
  std::vector<uint8_t> png = png_from_rgb565(w, h, pixels.data(), romDeflate);
  server.sendHeader("Cache-Control", "no-store");
  if (server.hasArg("download")) {
    server.sendHeader("Content-Disposition",
                      ("attachment; filename=\"remote-screen-" + std::to_string(millis() / 1000) + ".png\"").c_str());
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
  server.sendHeader("Content-Disposition", "attachment; filename=\"remote-log.txt\"");
  server.send(200, "text/plain; charset=utf-8", text.c_str());
}

void start() {
  server.on("/", HTTP_GET, onPage);
  server.on("/api/state", HTTP_GET, onState);
  server.on("/api/connection", HTTP_POST, onConnection);
  server.on("/api/channel", HTTP_POST, onChannel);
  server.on("/api/check", HTTP_POST, onCheck);
  server.on("/api/install", HTTP_POST, onInstall);
  server.on("/api/password", HTTP_POST, onPassword);
  server.on("/screen.png", HTTP_GET, onScreen);
  server.on("/log", HTTP_GET, onLog);
  server.on("/log.txt", HTTP_GET, onLogFile);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  if (MDNS.begin("metarmap-remote")) MDNS.addService("http", "tcp", 80);
  address = "http://" + std::string(WiFi.localIP().toString().c_str()) + "/";
  started = true;
  LOGF("web: %s (http://metarmap-remote.local/)\n", address.c_str());
}

}  // namespace

void remote_web_begin(Updater& u, Settings& s) {
  updater = &u;
  settings = &s;
}

void remote_web_tick() {
  if (!settings) return;
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
  if (!started) {
    if (WiFi.status() == WL_CONNECTED) start();
    return;
  }
  server.handleClient();
}

const char* remote_web_address() { return address.c_str(); }
