// Timings the screen (ui.cpp), the status light on the back (status_led.cpp) and the map client share, so they
// always agree.
#pragma once

#include <cstdint>

constexpr uint32_t kWifiTimeoutMs = 15000;  // joining Wi-Fi this long: the splash names the network it can't join
// /api/live failing this long in a row before the Chart counts as offline: a busy Chart (a Pi Zero W at full CPU)
// stalls for a few seconds (offline_grace.h). Wi-Fi loss and a map switch are offline at once.
constexpr uint32_t kOfflineGraceMs = 15000;
// Offline this long: back to the splash screen (and the status light red). For the Chart not answering that's
// kOfflineGraceMs + kLostMs after its first missed answer; for Wi-Fi loss, kLostMs.
constexpr uint32_t kLostMs = 5000;
