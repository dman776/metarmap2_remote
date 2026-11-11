// Turning the map's JSON (/api/live, /api/status) into MapLive and MapStatus.
// Plain C++ and ArduinoJson, unit tested on the host (test/test_parse).
#pragma once

#include <ArduinoJson.h>

#include "map_state.h"

// ArduinoJson filters keeping only what the remote uses: /api/live is
// ~18 KB and /api/status ~25 KB, most of it METAR text and config.
void liveFilter(JsonDocument& filter);
void statusFilter(JsonDocument& filter);
void atisFilter(JsonDocument& filter);

// The JSON's fields; rxMillis is left for the caller.
MapLive parseLive(JsonVariantConst doc);
MapStatus parseStatus(JsonVariantConst doc);
AtisTranscript parseAtis(JsonVariantConst doc);

// /visualizer/<id>/skip or /unskip's answer, its JSON (null if it had none) and HTTP status; ok only for a 200 with
// the skip list. id is left for the caller when the answer has none (a 400, or an older Chart).
SkipAnswer parseSkip(JsonVariantConst doc, int httpCode);

// The line being said `playedMs` into `totalMs` of an ATIS read aloud, worked out from how long each line's words
// are (the voice doesn't say when it reaches one): -1 before it's known (totalMs 0) or with no lines.
int atisLineAt(const std::vector<AtisLine>& lines, uint32_t playedMs, uint32_t totalMs);

// The map's version as the splash shows it, from /api/status (see MapStatus).
std::string versionText(JsonVariantConst doc);

// "#rrggbb" as 0xRRGGBB; 0 for anything else.
uint32_t parseColor(const char* css);
