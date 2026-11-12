// The remote's screens: a small version of the map's /remote page. A rail at the left: LEDs (the visualizer, its
// legend, auto-rotate and brightness), Airports (a page of airports; a tap opens its card), and the logo (the
// splash screen, and from it the firmware update screen).
#pragma once

#include "ui_control.h"

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

// ---- remote control, for screenshots (POST /api/ui/..., remote_web.cpp; ui_control.h has the names) ----
// Each does what a tap on that name does, then lets the screen settle (screen fades and the stream's state come in,
// up to a second) and draws it, so a screenshot taken after the answer shows the result. Call only from the task
// that runs LVGL (loop(): the web server answers there); from any other they do nothing and answer 500.

ui_control::Outcome ui_control_show(const ui_control::ShowRequest& req);
ui_control::Outcome ui_control_card(const ui_control::CardRequest& req);
// Switches the Chart to the visualizer (an id, or a name or short name): a tap on its picker tile
ui_control::Outcome ui_control_visualizer(const std::string& idOrName);
// What's on screen now (remoteControl is the caller's to fill)
ui_control::State ui_control_state();
