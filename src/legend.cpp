#include "legend.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace legend {

namespace {

float channel(uint32_t c, int shift) { return (float)(c >> shift & 0xff); }

float dist2(float r, float g, float b, uint32_t c) {
  float dr = r - channel(c, 16), dg = g - channel(c, 8), db = b - channel(c, 0);
  return dr * dr + dg * dg + db * db;
}

// The legend's name for what an LED is doing: a blink between two colors is
// the legend's "alternate".
std::string legendEffect(const Station& s) {
  if (s.colors.empty() || s.effect == "off") return "off";
  if (s.effect == "blink" && s.colors.size() > 1) return "alternate";
  return s.effect;
}

void markStation(const std::vector<LegendItem>& legend, const Station& s, std::vector<bool>& used) {
  if (s.lightning) {  // its own color is hidden under the flashes
    for (size_t i = 0; i < legend.size(); i++) {
      if (legend[i].effect == "flash") used[i] = true;
    }
    return;
  }
  std::string effect = legendEffect(s);
  uint32_t color = s.colors.empty() ? 0 : s.colors[0];
  int nearest = -1;
  float nearestDist = INFINITY;
  bool hit = false;
  for (size_t i = 0; i < legend.size(); i++) {
    const LegendItem& item = legend[i];
    if (item.effect != effect) continue;
    float d = INFINITY;
    if (effect == "off") {
      used[i] = hit = true;
    } else if (isGradient(item)) {
      d = gradientDist2(color, item.colors);
      if (d <= kNearGradient * kNearGradient) used[i] = hit = true;
    } else if (!item.colors.empty()) {
      if (item.colors[0] == color) used[i] = hit = true;
      d = dist2(channel(item.colors[0], 16), channel(item.colors[0], 8), channel(item.colors[0], 0), color);
    }
    if (d < nearestDist) {
      nearestDist = d;
      nearest = (int)i;
    }
  }
  // No exact match (e.g. an estimated category breathes in its category's
  // color, but the legend shows it in green): the nearest entry with that
  // effect.
  if (!hit && nearest >= 0) used[nearest] = true;
}

}  // namespace

bool fromMap(const MapStatus& status, const MapLive& live) {
  if (!live.hasLegendInUse || status.legend.empty()) return false;
  for (const LegendItem& item : status.legend) {
    if (item.key.empty()) return false;
  }
  return true;
}

bool isGradient(const LegendItem& item) {
  return item.colors.size() > 1 && item.effect != "flash" && item.effect != "alternate";
}

float gradientDist2(uint32_t color, const std::vector<uint32_t>& stops) {
  float best = INFINITY;
  float cr = channel(color, 16), cg = channel(color, 8), cb = channel(color, 0);
  for (size_t i = 0; i + 1 < stops.size(); i++) {
    float ar = channel(stops[i], 16), ag = channel(stops[i], 8), ab = channel(stops[i], 0);
    float vr = channel(stops[i + 1], 16) - ar, vg = channel(stops[i + 1], 8) - ag, vb = channel(stops[i + 1], 0) - ab;
    float len2 = vr * vr + vg * vg + vb * vb;
    float t = len2 > 0 ? ((cr - ar) * vr + (cg - ag) * vg + (cb - ab) * vb) / len2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    best = std::min(best, dist2(ar + t * vr, ag + t * vg, ab + t * vb, color));
  }
  return best;
}

std::vector<bool> inUse(const MapStatus& status, const MapLive& live, int shownIndex) {
  size_t n = status.legend.size();
  bool ready = live.valid && live.activeIndex == status.activeIndex && status.activeIndex == shownIndex;
  if (!ready) return std::vector<bool>(n, false);
  if (live.strip) return std::vector<bool>(n, true);
  std::vector<bool> used(n, false);
  if (fromMap(status, live)) {
    for (size_t i = 0; i < n; i++) {
      const std::vector<std::string>& keys = live.legendInUse;
      used[i] = std::find(keys.begin(), keys.end(), status.legend[i].key) != keys.end();
    }
  } else {
    for (const Station& s : live.stations) markStation(status.legend, s, used);
  }
  if (std::find(used.begin(), used.end(), true) == used.end()) return std::vector<bool>(n, true);
  return used;
}

}  // namespace legend
