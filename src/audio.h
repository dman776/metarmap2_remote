// The ES3C28P's speaker: an ES8311 codec on I2S, configured over the touch
// controller's I2C bus, driving an FM8002E amplifier.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sounds.h"

// Sets up I2S and the codec and turns the amplifier on. Call before
// lcd.init(): it borrows the touch I2C pins for a moment. Returns false
// (and stays silent) when the codec doesn't answer.
bool audio_begin();

// A short tick, for touch feedback. Returns at once: the task that plays audio_play()'s sounds writes it too (mixed
// into one playing), so the two never interleave. (Dropped while a stream plays: nothing to hear a tick over; and
// within 120 ms of the last one, which is the same touch.) The touch driver (lvgl_port.cpp) asks for it as a finger
// lands: buttons don't ask again.
void audio_click();

// Plays a short sound from sounds.h (mono, 16 kHz) at volume 0-100 (%), the speaker's own volume setting. Returns at
// once: a task feeds it to the speaker. A sound asked for while another plays replaces it, and nothing plays while a
// stream is playing (the two would mix into noise). Silent while the sounds are off (audio_set_sounds()).
void audio_play(const Sound& sound, int volume);

// The web page's Sounds setting: whether audio_play() plays (the chime, the picker's and the splash volume's pops).
// Off stops one playing. The touch tick and the spoken weather don't follow it. Any task.
void audio_set_sounds(bool on);

// Whether audio_begin() found the codec.
bool audio_ready();

// Plays `frames` stereo frames (left, right, 16 kHz, 16-bit) from the stream,
// waiting up to waitMs for room in the I2S queue. Returns how many were taken.
size_t audio_write(const int16_t* stereo, size_t frames, uint32_t waitMs);

// The rate audio_write() takes: the codec is set up for this one only.
constexpr int kAudioRate = 16000;
