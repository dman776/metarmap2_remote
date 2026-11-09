// The status light: the board's RGB LED on the back (IO42), lit by what the Copilot is doing. Pure logic (no
// Arduino), so it's tested on the computer; main.cpp writes its color to the LED.
//
//   joining Wi-Fi          blue, blinking (for kWifiTimeoutMs, as the splash), then steady blue: no Wi-Fi
//   Wi-Fi setup mode       purple, breathing slowly
//   installing an update   yellow, blinking
//   the Chart answering    green for 10 s, then fading out: dark while all is well
//   the Chart not answering (Wi-Fi up) for kLostMs, when the screen goes back to the splash: red
//
// The brightness (0-100 %, 0 = off) scales every color: the LED faces the wall and is bright at full.
#pragma once

#include <cstdint>

namespace status_led {

struct Rgb {
  uint8_t r, g, b;
  constexpr Rgb() : r(0), g(0), b(0) {}
  constexpr Rgb(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb& o) const { return !(*this == o); }
};

struct Inputs {
  bool wifi = false;        // joined the Wi-Fi network
  bool online = false;      // the Chart answers (MapClient's Connection.online)
  bool setup = false;       // Wi-Fi config mode (wifi_setup_active())
  bool installing = false;  // an update is installing or restarting into the new firmware
};

constexpr uint32_t kGreenMs = 10000;       // green this long after the Chart answers,
constexpr uint32_t kFadeMs = 1500;         // then fading out over this long
constexpr uint32_t kBlinkMs = 500;         // joining Wi-Fi: on this long, off this long
constexpr uint32_t kUpdateBlinkMs = 400;   // installing an update: the same, quicker
constexpr uint32_t kBreatheMs = 4000;      // Wi-Fi setup mode: one breath, dim to full and back
constexpr int kDefaultBrightness = 5;      // %: a soft glow on the wall

constexpr Rgb kBlue{0, 0, 255};
constexpr Rgb kGreen{0, 255, 0};
constexpr Rgb kRed{255, 0, 0};
constexpr Rgb kPurple{160, 0, 255};
constexpr Rgb kYellow{255, 170, 0};

class StatusLight {
 public:
  // The color now, at `brightness` %. Call it often (each loop()); `now` is millis().
  Rgb tick(const Inputs& in, uint32_t now, int brightness);

 private:
  bool started_ = false;
  bool wifi_ = false, online_ = false, everOnline_ = false;
  uint32_t wifiDownSince_ = 0;  // when the Wi-Fi went (or the Copilot started without it)
  uint32_t offlineSince_ = 0;   // when the Chart stopped answering, with the Wi-Fi up
  uint32_t onlineSince_ = 0;    // when the Chart started answering
};

// `c` at `level` (0-1000, of full) and `brightness` %.
Rgb scale(Rgb c, int level, int brightness);

}  // namespace status_led
