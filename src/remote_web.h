// A small web page on the remote, for its Radio: http://metarmap-remote.local/
// (or the remote's IP) shows the stream URL, lets you change it, and plays or
// stops it. JSON for scripts:
//   GET /radio                          {"url", "phase", "message", "rate", "buffer"}
//   GET /radio/url?url=http://...       sets (and saves) the stream; "" forgets it
//   GET /radio/play, GET /radio/stop
//   GET /speak?code=KDWH                reads that airport's METAR aloud (as holding its Locator tile does)
//   GET /speak/server?url=http://...    where spoken METARs come from ("" for the map)
// No login: anyone on your network can change what the remote plays.
#pragma once

// Starts the server and mDNS (metarmap-remote.local). Call once Wi-Fi is set up.
void remote_web_begin();

// Call from loop(): answers requests.
void remote_web_tick();
