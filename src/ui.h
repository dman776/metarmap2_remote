// The remote's screens: a small version of the map's /remote page. A rail at the left: LEDs (the visualizer, its
// legend, auto-rotate and brightness), Airports (a page of airports; a tap opens its card), and the logo (the
// splash screen, and from it the firmware update screen).
#pragma once

class MapClient;
class Updater;

void ui_init(MapClient& client, Updater& updater);

// 0: LEDs, 1: Airports.
void ui_show_tab(int tab);

// The splash screen, as the rail's logo shows it.
void ui_show_splash();

// The firmware update screen, as the splash's Firmware button shows it.
void ui_show_update();

// For screenshots (the serial console): 'p' the visualizer picker, 'b' the brightness sheet, 'c' the card of the
// Airports page's first airport.
void ui_open(char what);

// Call from loop(): picks up new map state and runs the countdown.
void ui_tick();
