// The remote's screens: a small version of the map's /remote page, with its
// Visualizer and Locator tabs.
#pragma once

class MapClient;
class Updater;

void ui_init(MapClient& client, Updater& updater);

// 0: Visualizer, 1: Locator.
void ui_show_tab(int tab);

// The splash screen for 5 s, as a 5 s touch shows it.
void ui_show_splash();

// The update screen, as a 10 s touch shows it.
void ui_show_update();

// Call from loop(): picks up new map state and runs the countdown.
void ui_tick();
