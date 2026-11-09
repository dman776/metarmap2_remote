#include "settings.h"

#include <Preferences.h>

#include "log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

constexpr const char* kNamespace = "remote";

std::string getString(Preferences& p, const char* key, const std::string& fallback) {
  return p.isKey(key) ? std::string(p.getString(key).c_str()) : fallback;
}

void putString(Preferences& p, const char* key, const std::string& value) {
  if (getString(p, key, "\x01") != value) p.putString(key, value.c_str());  // flash wears: write changes only
}

}  // namespace

Settings settings_load() {
  Preferences p;
  p.begin(kNamespace, false);
#ifdef WIFI_SSID
  // A build from your computer: its secrets.h is the newest word, the first time it starts with them. After that
  // what's saved on the web page stays, until secrets.h changes.
  std::string seed = std::string(WIFI_SSID) + "\n" + WIFI_PASSWORD + "\n" + MAP_HOST + "\n" + std::to_string(MAP_PORT);
  if (getString(p, "conn_seed", "\x01") != seed) {
    putString(p, "conn_seed", seed);
    putString(p, "wifi_name", WIFI_SSID);
    putString(p, "wifi_pass", WIFI_PASSWORD);
    putString(p, "map_host", MAP_HOST);
    if (p.getUShort("map_port", 0) != MAP_PORT) p.putUShort("map_port", MAP_PORT);
  }
#endif
#ifdef WEB_PASSWORD
  if (getString(p, "web_seed", "\x01") != WEB_PASSWORD) {  // the same: once, then the web page's
    putString(p, "web_seed", WEB_PASSWORD);
    putString(p, "web_pass", WEB_PASSWORD);
  }
#endif
#ifdef STREAM_URL
  // Only when this build's URL is new: one sent over the network (stream_url) otherwise stays through restarts.
  if (getString(p, "stream_seed", "\x01") != STREAM_URL) {
    putString(p, "stream_seed", STREAM_URL);
    putString(p, "stream_url", STREAM_URL);
  }
#endif
  Settings s;
  s.wifiName = getString(p, "wifi_name", "");
  s.wifiPassword = getString(p, "wifi_pass", "");
  s.mapHost = getString(p, "map_host", "");
  s.mapPort = p.getUShort("map_port", 80);
  s.updateChannel = getString(p, "upd_channel", s.updateChannel);
  s.updateBranch = getString(p, "upd_branch", s.updateBranch);
  s.streamUrl = getString(p, "stream_url", "");
  s.volume = p.getUChar("volume", s.volume);
  s.webPassword = getString(p, "web_pass", kDefaultWebPassword);
  p.remove("speech_server");  // a prototype's own speech server: spoken METARs come from the map now
  p.end();
  if (s.wifiName.empty()) LOGF("settings: no Wi-Fi network saved; flash a build with include/secrets.h\n");
  return s;
}

void settings_save_stream(const std::string& url) {
  Preferences p;
  p.begin(kNamespace, false);
  putString(p, "stream_url", url);
  p.end();
}

void settings_save_volume(int volume) {
  Preferences p;
  p.begin(kNamespace, false);
  if (p.getUChar("volume", 255) != volume) p.putUChar("volume", (uint8_t)volume);  // flash wears: changes only
  p.end();
}

void settings_save_update(const Settings& s) {
  Preferences p;
  p.begin(kNamespace, false);
  putString(p, "upd_channel", s.updateChannel);
  putString(p, "upd_branch", s.updateBranch);
  p.end();
}

void settings_save_connection(const Settings& s) {
  Preferences p;
  p.begin(kNamespace, false);
  putString(p, "wifi_name", s.wifiName);
  putString(p, "wifi_pass", s.wifiPassword);
  putString(p, "map_host", s.mapHost);
  if (p.getUShort("map_port", 0) != s.mapPort) p.putUShort("map_port", s.mapPort);
  p.end();
}

void settings_save_web_password(const std::string& password) {
  Preferences p;
  p.begin(kNamespace, false);
  putString(p, "web_pass", password);
  p.end();
}
