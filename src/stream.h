// A prototype internet radio: plays an MP3 stream (a URL from secrets.h, e.g.
// an airport tower's feed) through the speaker, on two background tasks. One
// reads the network into a buffer in PSRAM, the other decodes it (minimp3),
// resamples to the codec's 16 kHz (stream_dsp.h) and writes it to I2S. It also
// plays an airport's METAR read aloud by the map: a WAV (16-bit PCM), once.
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
  std::string speaking;  // the airport whose METAR (or ATIS) is being read aloud; "" for the Radio
  bool atis = false;     // it's the airport's ATIS (stream_speak's), not its METAR
  // how far into what's read aloud, once it plays (from the map's WAV and its length); 0 while it isn't known
  uint32_t playedMs = 0, totalMs = 0;
};

// Sets up the Radio if the speaker works, with `url` ("" for none yet). Call after audio_begin().
void stream_begin(const std::string& url, const std::string& mapServer, int volume);

// Whether the Radio is set up (the speaker works).
bool stream_available();

// A new stream: stops the one playing, checks it (http:// or https://), saves it. "" forgets it. False if it isn't
// a URL the Radio can use.
bool stream_set_url(const std::string& url);
std::string stream_url();

void stream_set_playing(bool on);

// The speaker's volume, 0-100 (%). Takes effect at once, while playing too; `save` keeps it through restarts (on
// the slider's release, not each step).
void stream_set_volume(int volume, bool save);
int stream_volume();

// Reads an airport's METAR aloud: plays the map's /api/speak/<code>.audio (a WAV, made by the map's speech server:
// Piper in Home Assistant, or Kokoro) once, stopping the Radio; with atis, its real ATIS (/api/atis/<code>.audio).
// Failed, with the map's why, if it can't (e.g. speech is off, or the airport has no ATIS).
void stream_speak(const std::string& code, bool atis = false);

// The state, if it changed since `rev` (as MapClient::getStatus).
bool stream_get(StreamState& out, uint32_t& rev);
