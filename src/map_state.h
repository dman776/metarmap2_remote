// What the remote knows about the map, from its JSON API. The same split
// as the map's /remote page: /api/live polled often, /api/status for the
// visualizer list and the active visualizer's legend.
#pragma once

#include <Arduino.h>

#include <cmath>
#include <vector>

struct Visualizer {
  String id;
  String name;
  bool exclusive = false;
};

struct LegendItem {
  String label;
  String detail;
  String effect;                 // solid, blink, pulse, flash, chase, off...
  std::vector<uint32_t> colors;  // 0xRRGGBB; empty for "off"
};

// GET /api/status
struct MapStatus {
  bool valid = false;
  String version;
  String ip;
  int activeIndex = -1;  // the visualizer the legend is for
  std::vector<LegendItem> legend;
  std::vector<Visualizer> visualizers;
};

struct Station {
  String code;
  int pixel = -1;
  bool isVirtual = false;
  String category;  // VFR, MVFR, IFR, LIFR, or "" when unknown
  // What the LED is showing now: its effect (solid, off, blink, pulse,
  // cycle) and colors (0xRRGGBB, each once).
  String effect;
  std::vector<uint32_t> colors;
  bool lightning = false;
};

// GET /api/live
struct MapLive {
  bool valid = false;
  double now = NAN;       // the map's clock (Unix time) when it answered
  uint32_t rxMillis = 0;  // our millis() when the answer arrived
  float brightness = 0;
  int activeIndex = -1;
  String activeName;
  bool exclusive = false;
  bool autoRotate = false;
  int autoSeconds = 0;
  double autoNext = NAN;  // map clock; NAN while auto-rotate is off
  bool strip = false;     // one effect (rainbow, chase) drives the whole strip
  std::vector<Station> stations;  // in LED order
  int locatePixel = -1;
  double locateAt = NAN;
};

struct Connection {
  bool wifi = false;
  bool online = false;  // the last /api/live answered
  int httpCode = 0;     // the last failure; negative for HTTPClient errors
};
