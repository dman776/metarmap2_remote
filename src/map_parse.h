// Turning the map's JSON (/api/live, /api/status) into MapLive and MapStatus.
// Plain C++ and ArduinoJson, unit tested on the host (test/test_parse).
#pragma once

#include <ArduinoJson.h>

#include "map_state.h"

// ArduinoJson filters keeping only what the remote uses: /api/live is
// ~18 KB and /api/status ~25 KB, most of it METAR text and config.
void liveFilter(JsonDocument& filter);
void statusFilter(JsonDocument& filter);

// The JSON's fields; rxMillis is left for the caller.
MapLive parseLive(JsonVariantConst doc);
MapStatus parseStatus(JsonVariantConst doc);

// "#rrggbb" as 0xRRGGBB; 0 for anything else.
uint32_t parseColor(const char* css);
