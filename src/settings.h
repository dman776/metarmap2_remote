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
  std::string updateBranch = "main";
};

// The saved settings, after saving secrets.h's if this build has them.
Settings settings_load();

// Saves the update channel and branch.
void settings_save_update(const Settings& settings);
