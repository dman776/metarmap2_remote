#include "status_led.h"

#include "timing.h"

namespace status_led {

namespace {

constexpr int kFull = 1000;

uint8_t part(uint8_t v, int level, int brightness) {
  if (v == 0 || level <= 0 || brightness <= 0) return 0;
  long out = (long)v * level * brightness / (kFull * 100L);
  return (uint8_t)(out < 1 ? 1 : out > 255 ? 255 : out);  // a lit color never rounds down to dark
}

bool blinkOn(uint32_t since, uint32_t now, uint32_t halfMs) { return ((now - since) / halfMs) % 2 == 0; }

// a triangle wave, 15 % to full and back, so it never goes quite dark
int breath(uint32_t now) {
  uint32_t t = now % kBreatheMs, half = kBreatheMs / 2;
  int up = (int)((t < half ? t : kBreatheMs - t) * kFull / half);
  return 150 + up * (kFull - 150) / kFull;
}

}  // namespace

Rgb scale(Rgb c, int level, int brightness) {
  if (brightness > 100) brightness = 100;
  return Rgb{part(c.r, level, brightness), part(c.g, level, brightness), part(c.b, level, brightness)};
}

Rgb StatusLight::tick(const Inputs& in, uint32_t now, int brightness) {
  if (!started_) {
    started_ = true;
    wifi_ = in.wifi;
    online_ = false;
    wifiDownSince_ = offlineSince_ = now;
  }
  if (in.wifi != wifi_) {
    wifi_ = in.wifi;
    if (!wifi_)
      wifiDownSince_ = now;
    else
      offlineSince_ = now;  // waiting for the Chart from here
  }
  bool online = in.wifi && in.online;
  if (online != online_) {
    online_ = online;
    if (online) {
      onlineSince_ = now;
      everOnline_ = true;
    } else {
      offlineSince_ = now;
    }
  }

  if (in.installing) return scale(kYellow, blinkOn(0, now, kUpdateBlinkMs) ? kFull : 0, brightness);
  if (in.setup) return scale(kPurple, breath(now), brightness);
  if (!in.wifi) {
    bool joining = now - wifiDownSince_ < kWifiTimeoutMs;
    return scale(kBlue, !joining || blinkOn(wifiDownSince_, now, kBlinkMs) ? kFull : 0, brightness);
  }
  if (online) {
    uint32_t t = now - onlineSince_;
    if (t < kGreenMs) return scale(kGreen, kFull, brightness);
    if (t < kGreenMs + kFadeMs) return scale(kGreen, (int)((kGreenMs + kFadeMs - t) * kFull / kFadeMs), brightness);
    return Rgb{};
  }
  // Wi-Fi up, the Chart not answering
  if (now - offlineSince_ >= kLostMs) return scale(kRed, kFull, brightness);
  if (!everOnline_)
    return scale(kBlue, blinkOn(offlineSince_, now, kBlinkMs) ? kFull : 0, brightness);  // still connecting
  return Rgb{};  // a blip: dark until it's the splash's 5 s
}

}  // namespace status_led
