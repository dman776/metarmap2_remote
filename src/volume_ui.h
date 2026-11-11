// The speaker's volume on screen: which speaker symbol shows it, and whether letting go of a volume slider plays a
// pop to hear the new level. Plain C++, unit tested on the host (test/test_volume_ui).
#pragma once

namespace volume_ui {

enum class Icon {
  Mute,  // 0%: muted
  Mid,   // 1-50%
  Max,   // 51-100%
};

inline Icon icon(int pct) {
  if (pct <= 0) return Icon::Mute;
  return pct <= 50 ? Icon::Mid : Icon::Max;
}

// A pop on letting go, at the new level: not at 0% (nothing to hear). The Sounds setting and a playing stream are
// audio_play()'s to check.
inline bool popOnRelease(int pct) { return pct > 0; }

}  // namespace volume_ui
