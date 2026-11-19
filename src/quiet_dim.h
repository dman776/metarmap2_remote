// In the Chart's quiet hours (/api/live's `quiet`) the screen's backlight dims to about 5%, so the Copilot doesn't
// light up a dark room. A touch wakes it to full for kAwakeMs from the last touch; that first touch only wakes it (it
// doesn't press what's beneath), so the next can turn the pixels up. Once quiet hours end, full at once. A touch
// outside quiet hours counts too: quiet hours starting while someone is using it wait till they've let it be for
// kAwakeMs. Offline, the last `quiet` the Chart said stays (a map switch forgets it: MapLive starts over). Plain
// C++, unit tested on the host (test/test_quiet_dim).
#pragma once

#include <cstdint>

namespace quiet_dim {

constexpr uint8_t kNormal = 200;  // the backlight (LovyanGFX's 0-255) as it always was
constexpr uint8_t kDim = 10;      // about 5% of that
constexpr uint32_t kAwakeMs = 30000;

class Backlight {
 public:
  // The Chart's quiet hours, as its latest /api/live said (false from a Chart that doesn't say)
  void setQuiet(bool quiet) { quiet_ = quiet; }
  bool quiet() const { return quiet_; }

  // A finger on the screen at `now` (ms, wrapping): each reading while it's down. True if the screen was dim: this
  // touch only wakes it, and shouldn't reach what's beneath (from landing to lifting).
  bool touched(uint32_t now) {
    bool woke = dimmed(now);
    awake_ = true;
    since_ = now;
    return woke;
  }

  bool dimmed(uint32_t now) {
    if (awake_ && now - since_ >= kAwakeMs) awake_ = false;  // ended here, so a wrap of millis() can't revive it
    return quiet_ && !awake_;
  }

  uint8_t level(uint32_t now) { return dimmed(now) ? kDim : kNormal; }

 private:
  bool quiet_ = false;
  bool awake_ = false;  // touched within kAwakeMs
  uint32_t since_ = 0;  // the last touch
};

}  // namespace quiet_dim
