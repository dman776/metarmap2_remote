// What the remote knows about the map, from its JSON API. The same split
// as the map's /remote page: /api/live polled often, /api/status for the
// visualizer list and the active visualizer's legend.
//
// Plain C++ (no Arduino types), so the parsing and legend code that uses it
// can be unit tested on the host.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct Visualizer {
  std::string id;
  std::string name;
  std::string shortName;  // the name where there's little room (the picker); the name from a map without it
  std::string icon;       // what its icon shows (icons.h vis_icon()); "" from a map without it
  bool exclusive = false;
  bool skipped = false;  // auto-rotate leaves it out (visualizer.auto_rotate.skip); false for an exclusive one
};

// An airport's details from /api/status, for its card
struct AirportInfo {
  std::string code;
  std::string name;  // "" if the map has none
  std::string raw;   // the METAR as reported; "" without one
};

struct LegendItem {
  std::string label;
  std::string detail;
  std::string effect;            // solid, blink, pulse, flash, alternate, chase, off
  std::vector<uint32_t> colors;  // 0xRRGGBB; empty for "off"
  std::string key;  // what /api/live's legendInUse names it by; "" from a map without it (last: keeps {label, ...})
};

// GET /api/status
struct MapStatus {
  bool valid = false;
  std::string version;
  // What the splash shows: the release ("v26.11.04"), or a build after one
  // ("v26.11.04+16 f86daa4"), from /api/status's build; else the version.
  std::string versionText;
  std::string ip;
  int activeIndex = -1;  // the visualizer the legend is for
  std::vector<LegendItem> legend;
  std::vector<Visualizer> visualizers;
  bool hasSkip = false;  // the visualizers say whether auto-rotate skips them (a Chart after 26.11.10, B83)
  std::vector<AirportInfo> airports;  // in LED order
};

struct Station {
  std::string code;
  int pixel = -1;
  bool isVirtual = false;
  std::string category;  // VFR, MVFR, IFR, LIFR, or "" when unknown
  // What the LED is showing now: its effect (solid, off, blink, pulse,
  // cycle) and colors (0xRRGGBB, each once).
  std::string effect;
  std::vector<uint32_t> colors;
  bool lightning = false;
  bool atis = false;  // its real ATIS can be read aloud (/api/atis/<code>.audio): the Info tab's ATIS mode shows it
};

// GET /api/atis/<code>/lines: an airport's ATIS line by line, to read along as it's spoken
struct AtisLine {
  std::string raw;   // the FAA's text, e.g. "LNDG AND DEPG RWY 13R."
  std::string said;  // how it's said: "Landing and departing runway one tree right."
};
struct AtisTranscript {
  std::string code;  // the airport asked for
  bool valid = false;
  std::vector<AtisLine> lines;
};

// GET /api/live
struct MapLive {
  bool valid = false;
  double now = NAN;       // the map's clock (Unix time) when it answered
  uint32_t rxMillis = 0;  // our millis() when the answer arrived
  float brightness = 0;
  int activeIndex = -1;
  std::string activeName;
  bool exclusive = false;
  bool autoRotate = false;
  int autoSeconds = 0;
  double autoNext = NAN;  // map clock; NAN while auto-rotate is off
  // the visualizer ids auto-rotate leaves out (autoRotate.skip); hasSkip is false for a map that doesn't say
  bool hasSkip = false;
  std::vector<std::string> skip;
  bool strip = false;  // one effect (rainbow, chase) drives the whole strip
  // the legend entries (keys) the LEDs show now, as the map works them out; hasLegendInUse is false for a map
  // that doesn't say (older ones), or while a whole-strip effect runs
  bool hasLegendInUse = false;
  std::vector<std::string> legendInUse;
  // A visualizer whose pixels step through the same steps together (Flight Category Forecast): the steps' names
  // ("Now", "+3h", ...) and the one on now; empty and -1 for any other visualizer (or an older map)
  std::vector<std::string> stepLabels;
  int stepIndex = -1;
  std::vector<Station> stations;  // in LED order
  int locatePixel = -1;
  double locateAt = NAN;
  bool speech = false;  // the map reads METARs aloud (/api/speak) now: on, with a server; false from an older map
};

// The answer to GET /visualizer/<id>/skip or /unskip: {"id", "skipped", "skip"} (200), or {"error", "reason"} (400)
struct SkipAnswer {
  uint32_t seq = 0;  // the command's number (MapClient::requestSkip()'s)
  std::string id;    // the visualizer asked about
  int httpCode = 0;  // 200, 400, anything else (negative for HTTPClient errors)
  bool ok = false;   // a 200 with the answer's JSON
  bool skipped = false;
  std::vector<std::string> skip;  // the whole list after the change (ok only)
  std::string reason;             // a 400's: "unknown", "exclusive", "last", "invalid"; "" without one
};

struct Connection {
  bool wifi = false;
  bool online = false;  // the last /api/live answered
  int httpCode = 0;     // the last failure; negative for HTTPClient errors
};
