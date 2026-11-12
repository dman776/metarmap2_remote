// One action per press: the touch controller can lose a finger for a moment in the middle of a touch, and LVGL then
// sees two or three presses (two or three ticks, and a card's Stop followed by a fresh start). A Gate lets an action
// through, then ignores it again for a short while. Plain C++, unit tested on the host (test/test_debounce).
#pragma once

#include <cstdint>

namespace debounce {

constexpr uint32_t kClickMs = 120;   // the touch tick: one per press
constexpr uint32_t kButtonMs = 700;  // a card's AWOS, ATIS (and their Stop) and Locate

// Whether something at `now` (ms, wrapping) is far enough from the last one let through at `last` (0: none yet).
inline bool due(uint32_t last, uint32_t now, uint32_t gapMs) { return last == 0 || now - last >= gapMs; }

class Gate {
 public:
  explicit Gate(uint32_t gapMs) : gapMs_(gapMs) {}

  // True (and the window starts again) if it's due; false for a repeat within the window.
  bool accept(uint32_t now) {
    if (!due(last_, now, gapMs_)) return false;
    last_ = now | 1;  // never 0, which means "none yet"
    return true;
  }

 private:
  uint32_t gapMs_;
  uint32_t last_ = 0;
};

}  // namespace debounce
