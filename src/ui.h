// The remote's screens: a small version of the map's /remote page, with its
// Visualizer and Locator tabs.
#pragma once

class MapClient;

void ui_init(MapClient& client);

// 0: Visualizer, 1: Locator.
void ui_show_tab(int tab);

// Call from loop(): picks up new map state and runs the countdown.
void ui_tick();
