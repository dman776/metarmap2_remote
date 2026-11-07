#include "remote_web.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <string>

#include "log.h"
#include "stream.h"

namespace {

WebServer server(80);
bool started = false;

const char* phaseName(StreamState::Phase phase) {
  switch (phase) {
    case StreamState::Connecting:
      return "connecting";
    case StreamState::Buffering:
      return "buffering";
    case StreamState::Playing:
      return "playing";
    case StreamState::Retrying:
      return "retrying";
    default:
      return "off";
  }
}

std::string jsonString(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string html(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else if (c == '&') {
      out += "&amp;";
    } else if (c == '"') {
      out += "&quot;";
    } else {
      out += c;
    }
  }
  return out;
}

StreamState current() {
  static StreamState state;
  static uint32_t rev = 0;
  stream_get(state, rev);
  return state;
}

void sendStatus() {
  StreamState s = current();
  std::string body = "{\"url\":" + jsonString(stream_url()) + ",\"phase\":\"" + phaseName(s.phase) +
                     "\",\"message\":" + jsonString(s.message) + ",\"rate\":" + std::to_string(s.rate) +
                     ",\"buffer\":" + std::to_string(s.bufferPct) + "}";
  server.send(200, "application/json", body.c_str());
}

void onPage() {
  StreamState s = current();
  std::string url = stream_url();
  std::string status = phaseName(s.phase);
  if (s.phase == StreamState::Playing)
    status += " - " + std::to_string(s.rate) + " Hz, buffer " + std::to_string(s.bufferPct) + "%";
  if (!s.message.empty()) status += " - " + s.message;
  std::string page =
      "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>Remote Radio</title><style>"
      "body{font:16px system-ui,sans-serif;background:#0b1117;color:#e6edf3;margin:0;padding:16px;max-width:640px}"
      "h1{font-size:20px}input{width:100%;box-sizing:border-box;padding:10px;font:15px monospace;background:#121a23;"
      "color:#e6edf3;border:1px solid #2c3b4e;border-radius:8px}button{margin:10px 8px 0 0;padding:10px 16px;font:600 "
      "15px system-ui;border-radius:8px;border:1px solid #2c3b4e;background:#18222d;color:#e6edf3}"
      ".primary{background:#f5b544;color:#1a1204;border-color:#f5b544}p{color:#93a4b8}"
      "</style></head><body><h1>MetarMap2 Remote: Radio</h1>"
      "<form action=/radio/url method=get><label for=u>MP3 stream URL (http:// or https://)</label>"
      "<input id=u name=url value=\"" +
      html(url) +
      "\" placeholder='http://example.com:8000/stream'><input type=hidden name=page value=1>"
      "<button class=primary>Save</button></form>"
      "<form action=/radio/play method=get style=display:inline><input type=hidden name=page value=1>"
      "<button>Play</button></form>"
      "<form action=/radio/stop method=get style=display:inline><input type=hidden name=page value=1>"
      "<button>Stop</button></form>"
      "<p>Now: " +
      html(status) + "</p><p>The URL is saved on the remote. Saving a new one stops the one playing.</p></body></html>";
  server.send(200, "text/html", page.c_str());
}

// After a form: back to the page; for a script: the status.
void reply() {
  if (server.hasArg("page")) {
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    sendStatus();
  }
}

void onUrl() {
  if (!server.hasArg("url")) {
    server.send(400, "text/plain", "url is missing");
    return;
  }
  std::string url = server.arg("url").c_str();
  while (!url.empty() && isspace((unsigned char)url.back())) url.pop_back();
  while (!url.empty() && isspace((unsigned char)url.front())) url.erase(0, 1);
  if (!stream_set_url(url)) {
    server.send(400, "text/plain", "not an http:// or https:// URL");
    return;
  }
  reply();
}

void onPlay() {
  stream_set_playing(true);
  reply();
}

void onStop() {
  stream_set_playing(false);
  reply();
}

}  // namespace

void remote_web_begin() {
  if (started || !stream_available()) return;  // nothing to control without a speaker
  server.on("/", HTTP_GET, onPage);
  server.on("/radio", HTTP_GET, sendStatus);
  server.on("/radio/url", HTTP_GET, onUrl);
  server.on("/radio/play", HTTP_GET, onPlay);
  server.on("/radio/stop", HTTP_GET, onStop);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  if (MDNS.begin("metarmap-remote")) MDNS.addService("http", "tcp", 80);
  started = true;
  LOGF("web: http://metarmap-remote.local/ (%s)\n", WiFi.localIP().toString().c_str());
}

void remote_web_tick() {
  if (!started) {
    if (WiFi.status() == WL_CONNECTED) remote_web_begin();
    return;
  }
  server.handleClient();
}
