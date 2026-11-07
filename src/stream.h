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
  enum Phase { Off, Connecting, Buffering, Playing, Retrying };
  Phase phase = Off;
  int bufferPct = 0;    // how full the network buffer is
  int rate = 0;         // the stream's own sample rate, once it's decoding
  std::string message;  // why it's retrying
  bool hasUrl = false;  // a stream to play is set
};

// Sets up the Radio if the speaker works, with `url` ("" for none yet). Call after audio_begin().
void stream_begin(const std::string& url);

// Whether the Radio is set up (the speaker works).
bool stream_available();

// A new stream: stops the one playing, checks it (http:// or https://), saves it. "" forgets it. False if it isn't
// a URL the Radio can use.
bool stream_set_url(const std::string& url);
std::string stream_url();

void stream_set_playing(bool on);

// The state, if it changed since `rev` (as MapClient::getStatus).
bool stream_get(StreamState& out, uint32_t& rev);
