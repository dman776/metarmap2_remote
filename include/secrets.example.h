// Copy to include/secrets.h (git-ignored) and fill in.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// The MetarMap2 web server, e.g. "metarmap.local" or "192.168.1.50".
#define MAP_HOST "metarmap.local"
#define MAP_PORT 80

// Optional: the password for the remote's web page (http://metarmap-remote.local/, user "admin"). Without it, the
// page asks you to choose one the first time it's opened. Either way, it can be changed on the page.
// #define WEB_PASSWORD "choose-one"

// Optional, a prototype: an MP3 stream for the Radio (an airport tower's feed),
// http:// or https:// (https isn't checked against a certificate), MP3, not
// AAC or Ogg. Saved like the Wi-Fi settings. Nothing starts the Radio for now.
// #define STREAM_URL "http://example.com:8000/tower.mp3"
