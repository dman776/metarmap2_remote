#include "settings.h"

#include <Preferences.h>

#include "log.h"
#include "wifi_setup_text.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

constexpr const char* kNamespace = "remote";

std::string getString(Preferences& p, const char* key, const std::string& fallback) {
  return p.isKey(key) ? std::string(p.getString(key).c_str()) : fallback;
}

// Writes `value` if it changed (flash wears): whether it wrote.
bool putString(Preferences& p, const char* key, const std::string& value) {
  if (getString(p, key, "\x01") == value) return false;
  p.putString(key, value.c_str());
  return true;
}

// The settings, open for saving; a save that wrote to flash is logged when it closes, with how long it took (a
// flash write stalls both cores while it's written: the log's time stamp says when, should a crash follow).
class Saving {
 public:
  explicit Saving(const char* what) : what_(what), start_(millis()) { p.begin(kNamespace, false); }
  ~Saving() {
    p.end();
    if (wrote_) LOGF("settings: saved %s (%u ms)\n", what_, (unsigned)(millis() - start_));
  }
  Saving(const Saving&) = delete;
  Saving& operator=(const Saving&) = delete;

  // `did` write to flash (or not): logged when it closes
  void wrote(bool did) { wrote_ = wrote_ || did; }

  Preferences p;

 private:
  bool wrote_ = false;
  const char* what_;
  uint32_t start_;
};

}  // namespace

Settings settings_load() {
  Saving saving("at startup (secrets.h)");
  Preferences& p = saving.p;
#ifdef WIFI_SSID
  // A build from your computer: its secrets.h is the newest word, the first time it starts with them. After that
  // what's saved on the web page stays, until secrets.h changes.
  std::string seed = std::string(WIFI_SSID) + "\n" + WIFI_PASSWORD + "\n" + MAP_HOST + "\n" + std::to_string(MAP_PORT);
  if (getString(p, "conn_seed", "\x01") != seed) {
    saving.wrote(true);
    putString(p, "conn_seed", seed);
    putString(p, "wifi_name", WIFI_SSID);
    putString(p, "wifi_pass", WIFI_PASSWORD);
    putString(p, "map_host", MAP_HOST);
    if (p.getUShort("map_port", 0) != MAP_PORT) p.putUShort("map_port", MAP_PORT);
  }
#endif
#ifdef WEB_PASSWORD
  if (getString(p, "web_seed", "\x01") != WEB_PASSWORD) {  // the same: once, then the web page's
    saving.wrote(true);
    putString(p, "web_seed", WEB_PASSWORD);
    putString(p, "web_pass", WEB_PASSWORD);
  }
#endif
#ifdef STREAM_URL
  // Only when this build's URL is new: one sent over the network (stream_url) otherwise stays through restarts.
  if (getString(p, "stream_seed", "\x01") != STREAM_URL) {
    saving.wrote(true);
    putString(p, "stream_seed", STREAM_URL);
    putString(p, "stream_url", STREAM_URL);
  }
#endif
  Settings s;
  s.wifiName = getString(p, "wifi_name", kSetupSsid);  // nothing set up yet: Wi-Fi config mode
  s.wifiPassword = getString(p, "wifi_pass", "");
  s.mapHost = getString(p, "map_host", "metarmap.local");  // the installer's usual hostname
  s.mapPort = p.getUShort("map_port", 80);
  s.updateChannel = getString(p, "upd_channel", s.updateChannel);
  s.updateBranch = getString(p, "upd_branch", s.updateBranch);
  s.updateRelease = getString(p, "upd_release", s.updateRelease);
  s.streamUrl = getString(p, "stream_url", "");
  s.volume = p.getUChar("volume", s.volume);
  s.webPassword = getString(p, "web_pass", kDefaultWebPassword);
  s.ledBrightness = p.getUChar("led_pct", s.ledBrightness);
  s.sounds = p.getBool("sounds", s.sounds);
  if (p.isKey("speech_server")) {  // a prototype's own speech server: spoken METARs come from the map now
    p.remove("speech_server");
    saving.wrote(true);
  }
  if (s.wifiName.empty()) s.wifiName = kSetupSsid;
  return s;
}

void settings_save_stream(const std::string& url) {
  Saving s("the stream URL");
  s.wrote(putString(s.p, "stream_url", url));
}

void settings_save_volume(int volume) {
  Saving s("the volume");
  bool changed = s.p.getUChar("volume", 255) != volume;  // flash wears: changes only
  if (changed) s.p.putUChar("volume", (uint8_t)volume);
  s.wrote(changed);
}

void settings_save_update(const Settings& s) {
  Saving saving("the update channel");
  bool channel = putString(saving.p, "upd_channel", s.updateChannel);
  bool branch = putString(saving.p, "upd_branch", s.updateBranch);
  // Firmware from before Pick release reads "release" as Releases, and leaves upd_release alone.
  bool release = putString(saving.p, "upd_release", s.updateRelease);
  saving.wrote(channel || branch || release);
}

void settings_save_connection(const Settings& s) {
  Saving saving("the connection");
  Preferences& p = saving.p;
  bool name = putString(p, "wifi_name", s.wifiName);
  bool pass = putString(p, "wifi_pass", s.wifiPassword);
  bool host = putString(p, "map_host", s.mapHost);
  bool port = p.getUShort("map_port", 0) != s.mapPort;
  if (port) p.putUShort("map_port", s.mapPort);
  saving.wrote(name || pass || host || port);
}

void settings_save_led(int brightness) {
  Saving s("the status light");
  bool changed = s.p.getUChar("led_pct", 255) != brightness;  // changes only
  if (changed) s.p.putUChar("led_pct", (uint8_t)brightness);
  s.wrote(changed);
}

void settings_save_sounds(bool on) {
  Saving s("sounds");
  bool changed = !s.p.isKey("sounds") || s.p.getBool("sounds") != on;  // changes only
  if (changed) s.p.putBool("sounds", on);
  s.wrote(changed);
}

void settings_save_web_password(const std::string& password) {
  Saving s("the web password");
  s.wrote(putString(s.p, "web_pass", password));
}
