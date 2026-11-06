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
  // A build from your computer: its secrets.h is the newest word.
  putString(p, "wifi_name", WIFI_SSID);
  putString(p, "wifi_pass", WIFI_PASSWORD);
  putString(p, "map_host", MAP_HOST);
  if (p.getUShort("map_port", 0) != MAP_PORT) p.putUShort("map_port", MAP_PORT);
#endif
#ifdef STREAM_URL
  putString(p, "stream_url", STREAM_URL);
#endif
  Settings s;
  s.wifiName = getString(p, "wifi_name", "");
  s.wifiPassword = getString(p, "wifi_pass", "");
  s.mapHost = getString(p, "map_host", "");
  s.mapPort = p.getUShort("map_port", 80);
  s.updateChannel = getString(p, "upd_channel", s.updateChannel);
  s.updateBranch = getString(p, "upd_branch", s.updateBranch);
  s.streamUrl = getString(p, "stream_url", "");
  p.end();
  if (s.wifiName.empty()) LOGF("settings: no Wi-Fi network saved; flash a build with include/secrets.h\n");
  return s;
}

void settings_save_update(const Settings& s) {
  Preferences p;
  p.begin(kNamespace, false);
  putString(p, "upd_channel", s.updateChannel);
  putString(p, "upd_branch", s.updateBranch);
  p.end();
}
