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
};

// Starts the tasks if `url` is set and the speaker works. Call after audio_begin().
void stream_begin(const std::string& url);

// Whether there's a stream to play (stream_begin() started it).
bool stream_available();

void stream_set_playing(bool on);

// The state, if it changed since `rev` (as MapClient::getStatus).
bool stream_get(StreamState& out, uint32_t& rev);
