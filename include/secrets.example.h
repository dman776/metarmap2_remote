// Copy to include/secrets.h (git-ignored) and fill in.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// The MetarMap2 web server, e.g. "metarmap.local" or "192.168.1.50".
#define MAP_HOST "metarmap.local"
#define MAP_PORT 80

// Optional, a prototype: an MP3 stream the Radio plays (started from the
// remote's web page, http://metarmap-remote.local/) through the speaker, e.g. an airport tower's feed. It must be
// http:// or https:// (https isn't checked against a certificate) and MP3, not AAC or Ogg. Saved like the Wi-Fi
// settings; "" forgets it. #define STREAM_URL "http://example.com:8000/tower.mp3"
