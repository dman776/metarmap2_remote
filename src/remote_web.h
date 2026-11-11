// The remote's own web page, http://metarmap-copilot.local/ (or its IP): its settings (Wi-Fi, the map's address,
// the update channel), firmware updates, the log, and screenshots. Behind a login: user metar, password map until
// it's changed (on the page, or WEB_PASSWORD in secrets.h), the same as the map's own web UI.
//
//   GET  /                     the page
//   GET  /api/state            {version, ip, wifiName, mapHost, mapPort, led, sounds, update: {...}}
//   POST /api/connection       wifi_name, wifi_pass ("" keeps it), map_host, map_port: saved, then a restart
//   POST /api/channel          channel (releases, latest, branch), branch
//   POST /api/check            check GitHub now
//   POST /api/install          install the available firmware
//   POST /api/password         password (3 to 64 characters)
//   POST /api/led              brightness (0-100): the status light's
//   POST /api/sounds           on (1 or 0): the chime and the picker's pops
//   GET  /screen.png           the screen now (?download=1: as a file)
//   GET  /log?from=N           the kept log after position N; X-Log-End: the position to ask from next
//   GET  /log.txt              all of the kept log, as a file
//
// In Wi-Fi config mode (wifi_setup.h), without a login: / is the setup page, GET /setup/networks the networks found
// and what is saved, POST /setup/rescan, POST /setup/save (wifi_name, wifi_pass, map_host, map_port: saved, then a
// restart); anything else redirects to the page (the captive portal).
#pragma once

class Updater;
struct Settings;

// Starts the server and mDNS (metarmap-copilot.local) once Wi-Fi is up. Call once from setup().
void remote_web_begin(Updater& updater, Settings& settings);

// Call from loop() (the LVGL task: screenshots read the display): answers requests.
void remote_web_tick();

// The page's address ("http://192.168.1.60/"), or "" before Wi-Fi is up
const char* remote_web_address();
