// A prototype internet radio: plays an MP3 stream (a URL from secrets.h, e.g.
// an airport tower's feed) through the speaker, on two background tasks. One
// reads the network into a buffer in PSRAM, the other decodes it (minimp3),
// resamples to the codec's 16 kHz (stream_dsp.h) and writes it to I2S.
//
// The UI starts and stops it with stream_set_playing() and copies the state
// out with stream_get().
#pragma once

#include <cstdint>
#include <string>

struct StreamState {
  enum Phase { Off, Connecting, Buffering, Playing, Retrying, Failed };
  Phase phase = Off;
  int bufferPct = 0;     // how full the network buffer is
  int rate = 0;          // the stream's own sample rate, once it's decoding
  std::string message;   // why it's retrying
  bool hasUrl = false;   // a stream to play is set
  std::string speaking;  // the airport whose METAR is being read aloud; "" for the Radio
};

// Sets up the Radio if the speaker works, with `url` ("" for none yet). Call after audio_begin().
void stream_begin(const std::string& url, const std::string& mapServer, const std::string& speechServer);

// Whether the Radio is set up (the speaker works).
bool stream_available();

// A new stream: stops the one playing, checks it (http:// or https://), saves it. "" forgets it. False if it isn't
// a URL the Radio can use.
bool stream_set_url(const std::string& url);
std::string stream_url();

void stream_set_playing(bool on);

// Reads an airport's METAR aloud: plays <speech server>/api/speak/<code>.mp3 once, stopping the Radio. The server is
// the map (http://MAP_HOST:MAP_PORT) unless set otherwise. Failed, with why, if it can't (e.g. the map has no key).
void stream_speak(const std::string& code);

// The speech server, e.g. "http://192.168.10.76" ("" for the map's). False if it isn't an http(s):// URL.
bool stream_set_speech_server(const std::string& url);
std::string stream_speech_server();

// The state, if it changed since `rev` (as MapClient::getStatus).
bool stream_get(StreamState& out, uint32_t& rev);
