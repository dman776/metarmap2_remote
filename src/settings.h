// The remote's settings, kept in flash (NVS) so they outlive firmware
// updates: the firmware CI builds for GitHub has no Wi-Fi password in it.
//
// A build with include/secrets.h (a USB flash from your computer) saves its
// Wi-Fi and map settings at startup; any other build uses the saved ones.
#pragma once

#include <cstdint>
#include <string>

struct Settings {
  std::string wifiName;
  std::string wifiPassword;
  std::string mapHost;
  uint16_t mapPort = 80;
  std::string updateChannel = "releases";  // releases, latest, branch (update_check.h)
  std::string updateBranch;                // "" until one is picked
  std::string streamUrl;                   // the MP3 stream the Radio plays; "" for none
  int volume = 63;                         // the speaker's, 0-100 (%): the volume slider while audio plays
  std::string webPassword;                 // the web page's login; "" until one is set (secrets.h or the page)
};

// The saved settings, after saving secrets.h's if this build has them.
Settings settings_load();

// Saves the Radio's stream URL ("" for none).
void settings_save_stream(const std::string& url);

// Saves the speaker's volume (0-100), as set on the volume slider. Writes only a change.
void settings_save_volume(int volume);

// Saves the update channel and branch.
void settings_save_update(const Settings& settings);

// Saves the Wi-Fi network and password, and the map's address and port (the web page; used after a restart).
void settings_save_connection(const Settings& settings);

// Saves the web page's login password.
void settings_save_web_password(const std::string& password);
