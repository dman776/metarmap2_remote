// A small web page on the remote, for its Radio: http://metarmap-remote.local/
// (or the remote's IP) shows the stream URL, lets you change it, and plays or
// stops it. JSON for scripts:
//   GET /radio                          {"url", "phase", "message", "rate", "buffer"}
//   GET /radio/url?url=http://...       sets (and saves) the stream; "" forgets it
//   GET /radio/play, GET /radio/stop
// No login: anyone on your network can change what the remote plays.
#pragma once

// Starts the server and mDNS (metarmap-remote.local). Call once Wi-Fi is set up.
void remote_web_begin();

// Call from loop(): answers requests.
void remote_web_tick();
