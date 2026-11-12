// The map picker's scan: asks the network once for the Charts that announce themselves (zeroconf, _metarmap2._tcp),
// on a background FreeRTOS task, so the 3 s it listens never stalls the touch screen. Only when asked (the picker
// opens): nothing scans in the background.
//
// The UI starts it with map_scan_start() and copies the result out with map_scan_get(); map_list.h makes the rows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map_list.h"

struct MapScan {
  bool scanning = false;
  bool done = false;  // a scan has finished (found may be empty)
  std::string error;  // why it couldn't ask ("not on Wi-Fi"); "" when it asked
  std::vector<map_list::Found> found;
};

// Starts a scan: false if one is already running (its answer is coming) or the task couldn't start (error says why).
bool map_scan_start();

// The state, if it changed since `rev` (as MapClient::getStatus).
bool map_scan_get(MapScan& out, uint32_t& rev);
