// The remote's settings, kept in flash (NVS) so they outlive firmware
// updates: the firmware CI builds for GitHub has no Wi-Fi password in it.
//
// A build with include/secrets.h (a USB flash from your computer) saves its
// Wi-Fi and map settings at startup; any other build uses the saved ones.
#pragma once

#include <cstdint>
#include <string>

// The web page's password until it's changed: the map's own default (metarmap2 settings.py web_server)
constexpr const char* kDefaultWebPassword = "map";

struct Settings {
  std::string wifiName;
  std::string wifiPassword;
  std::string mapHost;  // the Chart's address: the web page's Chart address, or a map picked
  uint16_t mapPort = 80;
  std::string mapIp;    // the IP the Chart was last reached at: MapClient's fallback when its name doesn't resolve
  std::string mapName;  // the picked map's announced name (the splash's line under "Connecting to the Chart..."); ""
  // A map was picked or typed (the map picker, the web page, the Wi-Fi setup page with a new address, secrets.h).
  // False on a first start: the Copilot looks for a map itself (map_list::autoPick()).
  bool mapChosen = false;
  std::string updateChannel = "releases";  // releases, latest, branch, release (update_check.h)
  std::string updateBranch;                // "" until one is picked
  std::string updateRelease;               // the Pick release channel's tag (v26.11.07); "" until one is picked
  std::string streamUrl;                   // the MP3 stream the Radio plays; "" for none
  int volume = 63;                         // the speaker's, 0-100 (%): the volume slider while audio plays
  std::string webPassword;                 // the web page's login password (user metar); map until it's changed
  int ledBrightness = 5;                   // the status light on the back, 0-100 (%); 0 is off
  bool sounds = true;                      // the chime and the picker's pops (audio_play()); not the tick or speech
  bool remoteControl = false;              // the screens can be driven over HTTP, for screenshots (/api/ui/...)
};

// The saved settings, after saving secrets.h's if this build has them.
Settings settings_load();

// Saves the Radio's stream URL ("" for none).
void settings_save_stream(const std::string& url);

// Saves the speaker's volume (0-100), as set on the volume slider. Writes only a change.
void settings_save_volume(int volume);

// Saves the update channel, branch and picked release.
void settings_save_update(const Settings& settings);

// Saves the Wi-Fi network and password, the map's address and port, and whether a map was chosen (mapChosen; the web
// page; used after a restart). A new address forgets the old one's IP and name.
void settings_save_connection(const Settings& settings);

// Saves the map's address, port, IP ("" for none) and name (the map picker's choice, or the one map a first start
// found; used at once, and after a restart), and that a map was chosen. Writes only a change.
void settings_save_map(const std::string& host, uint16_t port, const std::string& ip, const std::string& name);

// Saves the IP the map's name now resolves to (MapClient learned it). Writes only a change.
void settings_save_map_ip(const std::string& ip);

// Saves the status light's brightness (0-100). Writes only a change.
void settings_save_led(int brightness);

// Saves whether the sounds play (the web page's Sounds). Writes only a change.
void settings_save_sounds(bool on);

// Saves whether remote control is on (the web page's "Remote control (for screenshots)"). Writes only a change.
void settings_save_remote_control(bool on);

// Saves the web page's login password.
void settings_save_web_password(const std::string& password);
