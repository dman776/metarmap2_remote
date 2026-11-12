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
  std::string mapHost;
  uint16_t mapPort = 80;
  std::string updateChannel = "releases";  // releases, latest, branch, release (update_check.h)
  std::string updateBranch;                // "" until one is picked
  std::string updateRelease;               // the Pick release channel's tag (v26.11.07); "" until one is picked
  std::string streamUrl;                   // the MP3 stream the Radio plays; "" for none
  int volume = 63;                         // the speaker's, 0-100 (%): the volume slider while audio plays
  std::string webPassword;                 // the web page's login password (user metar); map until it's changed
  int ledBrightness = 5;                   // the status light on the back, 0-100 (%); 0 is off
  bool sounds = true;                      // the chime and the picker's pops (audio_play()); not the tick or speech
};

// The saved settings, after saving secrets.h's if this build has them.
Settings settings_load();

// Saves the Radio's stream URL ("" for none).
void settings_save_stream(const std::string& url);

// Saves the speaker's volume (0-100), as set on the volume slider. Writes only a change.
void settings_save_volume(int volume);

// Saves the update channel, branch and picked release.
void settings_save_update(const Settings& settings);

// Saves the Wi-Fi network and password, and the map's address and port (the web page; used after a restart).
void settings_save_connection(const Settings& settings);

// Saves the status light's brightness (0-100). Writes only a change.
void settings_save_led(int brightness);

// Saves whether the sounds play (the web page's Sounds). Writes only a change.
void settings_save_sounds(bool on);

// Saves the web page's login password.
void settings_save_web_password(const std::string& password);
