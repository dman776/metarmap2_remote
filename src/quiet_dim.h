// In the Chart's quiet hours (/api/live's `quiet`) the screen's backlight dims to about 10%, so the Copilot doesn't
// light up a dark room. A touch wakes it to full for kAwakeMs from the last touch; that first touch only wakes it (it
// doesn't press what's beneath), so the next can turn the pixels up. Once quiet hours end, full at once. A touch
// outside quiet hours counts too: quiet hours starting while someone is using it wait till they've let it be for
// kAwakeMs. Offline, the last `quiet` the Chart said stays (a map switch forgets it: MapLive starts over). Plain
// C++, unit tested on the host (test/test_quiet_dim). Only a touch wakes it: the web page's remote control doesn't.
#pragma once

#include <cstdint>

namespace quiet_dim {

constexpr uint8_t kNormal = 200;  // the backlight (LovyanGFX's 0-255) as it always was
constexpr uint8_t kDim = 24;      // about 10% of full (255): a little darker than the Cockpit's 15%
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

// One touch as LVGL should see it, from the controller's readings. The FT6336 sometimes misses a finger for a reading
// in the middle of a touch, so the finger counts as lifted only after it has been gone releaseMs: one touch is one
// press (one tick, one click). A touch that lands on a dim screen only wakes it: no press and no tick, from finger
// down to lift, drag or not. The next touch (once the last one has been lifted for releaseMs) presses as always.
// A finger back within releaseMs is the same touch: the controller can't tell it from a missed reading, and a
// missed reading mid-touch mustn't press what's beneath a waking touch.
class Touch {
 public:
  struct Reading {
    bool pressed = false;  // what LVGL sees
    bool click = false;    // the touch tick: once, as a touch that presses lands
  };

  Touch(Backlight& backlight, uint32_t releaseMs) : backlight_(backlight), releaseMs_(releaseMs) {}

  // A reading at `now` (ms, wrapping): `finger`, whether the controller sees one
  Reading read(bool finger, uint32_t now) {
    Reading r;
    if (finger) {
      bool woke = backlight_.touched(now);  // each reading: a long hold or drag keeps it awake
      if (!down_) {
        waking_ = woke;
        r.click = !waking_;
      }
      down_ = true;
      lastSeen_ = now;
    } else if (down_ && now - lastSeen_ >= releaseMs_) {
      down_ = false;
      waking_ = false;  // lifted: the next touch is its own
    }
    r.pressed = down_ && !waking_;
    return r;
  }

  bool down() const { return down_; }
  bool waking() const { return waking_; }  // this touch landed on a dim screen: it only wakes it

 private:
  Backlight& backlight_;
  uint32_t releaseMs_;
  bool down_ = false;
  bool waking_ = false;
  uint32_t lastSeen_ = 0;
};

}  // namespace quiet_dim
