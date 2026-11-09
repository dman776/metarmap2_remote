// Timings the screen (ui.cpp) and the status light on the back (status_led.cpp) share, so they always agree.
#pragma once

#include <cstdint>

constexpr uint32_t kWifiTimeoutMs = 15000;  // joining Wi-Fi this long: the splash names the network it can't join
constexpr uint32_t kLostMs = 5000;          // the map unreachable this long: back to the splash screen
