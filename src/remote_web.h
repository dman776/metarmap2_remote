// The remote's own web page, http://metarmap-remote.local/ (or its IP): its settings (Wi-Fi, the map's address,
// the update channel), firmware updates, the log, and screenshots. Behind a login (user "admin", the password set
// in secrets.h WEB_PASSWORD or on the page's first visit).
//
//   GET  /                     the page
//   GET  /api/state            {version, ip, wifiName, mapHost, mapPort, update: {...}}
//   POST /api/connection       wifi_name, wifi_pass ("" keeps it), map_host, map_port: saved, then a restart
//   POST /api/channel          channel (releases, latest, branch), branch
//   POST /api/check            check GitHub now
//   POST /api/install          install the available firmware
//   POST /api/password         password (at least 6 characters); open without a login while none is set
//   GET  /screen.png           the screen now (?download=1: as a file)
//   GET  /log?from=N           the kept log after position N; X-Log-End: the position to ask from next
//   GET  /log.txt              all of the kept log, as a file
#pragma once

class Updater;
struct Settings;

// Starts the server and mDNS (metarmap-remote.local) once Wi-Fi is up. Call once from setup().
void remote_web_begin(Updater& updater, Settings& settings);

// Call from loop() (the LVGL task: screenshots read the display): answers requests.
void remote_web_tick();

// The page's address ("http://192.168.1.60/"), or "" before Wi-Fi is up
const char* remote_web_address();
