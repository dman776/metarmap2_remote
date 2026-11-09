#include "wifi_setup_text.h"

#include <algorithm>
#include <cstdio>

std::string setupApName(const uint8_t mac[6]) {
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "-%02X%02X", mac[4], mac[5]);
  return std::string(kSetupSsid) + suffix;
}

bool isSetupSsid(const std::string& name) { return name.empty() || name == kSetupSsid || name == kOldSetupSsid; }

std::string wifiQr(const std::string& ssid) {
  std::string escaped;
  for (char c : ssid) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') escaped += '\\';
    escaped += c;
  }
  return "WIFI:S:" + escaped + ";T:nopass;;";
}

std::vector<ScannedNetwork> tidyScan(std::vector<ScannedNetwork> found, size_t limit) {
  std::stable_sort(found.begin(), found.end(),
                   [](const ScannedNetwork& a, const ScannedNetwork& b) { return a.rssi > b.rssi; });
  std::vector<ScannedNetwork> out;
  for (const ScannedNetwork& n : found) {
    if (n.ssid.empty()) continue;
    bool seen = std::any_of(out.begin(), out.end(), [&](const ScannedNetwork& o) { return o.ssid == n.ssid; });
    if (!seen) out.push_back(n);  // strongest first, so the first of a name is its strongest
    if (out.size() == limit) break;
  }
  return out;
}
