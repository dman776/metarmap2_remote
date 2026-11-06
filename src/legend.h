// Which legend entries the map is showing now.
//
// A map that says (/api/live's visualizer.legendInUse, with a key on each
// legend entry in /api/status) is taken at its word: it knows exactly which
// entries each LED shows. An older map doesn't, so there each LED's look from
// /api/live is matched against the legend: effect and color, a color along a
// gradient entry, the lightning flag for the flash entry.
// Plain C++, unit tested on the host (test/test_legend).
#pragma once

#include <cstdint>
#include <vector>

#include "map_state.h"

namespace legend {

// An RGB distance that counts as on a gradient entry's colors.
constexpr float kNearGradient = 40;

// One flag per status.legend entry: is the map showing it? None until the
// legend, the LEDs and the visualizer on screen (shownIndex) are all the
// same visualizer, so a legend never shows unfiltered. All of them while one
// effect drives the whole strip (Rainbow, Chase), or when nothing matched.
std::vector<bool> inUse(const MapStatus& status, const MapLive& live, int shownIndex);

// Whether the map says which entries are in use (and every entry has a key
// to match), so its list is used instead of matching the LEDs' looks.
bool fromMap(const MapStatus& status, const MapLive& live);

// Drawn as a gradient: two or more colors that aren't a flash or alternation.
bool isGradient(const LegendItem& item);

// The squared RGB distance from color to the nearest point of the gradient
// through stops.
float gradientDist2(uint32_t color, const std::vector<uint32_t>& stops);

}  // namespace legend
