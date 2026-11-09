// Wi-Fi config mode's text, as plain C++ (unit tested on the host, test/test_wifi_setup): the access point's name,
// the QR code a phone scans to join it, and the networks a scan found, tidied for the setup page.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The Wi-Fi a fresh remote has saved (a build without secrets.h): finding it at startup starts config mode.
constexpr const char* kSetupSsid = "MetarMap2-Remote";

// The access point's name: kSetupSsid and the last two bytes of the board's MAC, e.g. "MetarMap2-Remote-A1B2", so
// remotes set up side by side don't share one
std::string setupApName(const uint8_t mac[6]);

// What a phone camera reads to join an open network: WIFI:S:<name>;T:nopass;; (\ ; , : " in the name escaped)
std::string wifiQr(const std::string& ssid);

struct ScannedNetwork {
  std::string ssid;
  int rssi = 0;  // dBm: higher is stronger
  bool secure = false;
};

// A scan for the setup page: no hidden (unnamed) networks, each name once (its strongest), strongest first, at most
// `limit`
std::vector<ScannedNetwork> tidyScan(std::vector<ScannedNetwork> found, size_t limit = 20);
