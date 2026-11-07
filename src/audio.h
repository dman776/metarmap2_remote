// The ES3C28P's speaker: an ES8311 codec on I2S, configured over the touch
// controller's I2C bus, driving an FM8002E amplifier.
#pragma once

#include <cstddef>
#include <cstdint>

// Sets up I2S and the codec and turns the amplifier on. Call before
// lcd.init(): it borrows the touch I2C pins for a moment. Returns false
// (and stays silent) when the codec doesn't answer.
bool audio_begin();

// A short tick, for touch feedback. Returns at once. (Dropped while a stream
// has the output queue full: nothing to hear a tick over, and no pop.)
void audio_click();

// Whether audio_begin() found the codec.
bool audio_ready();

// Plays `frames` stereo frames (left, right, 16 kHz, 16-bit) from the stream,
// waiting up to waitMs for room in the I2S queue. Returns how many were taken.
size_t audio_write(const int16_t* stereo, size_t frames, uint32_t waitMs);

// The rate audio_write() takes: the codec is set up for this one only.
constexpr int kAudioRate = 16000;
