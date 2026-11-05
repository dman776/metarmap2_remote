// The ES3C28P's speaker: an ES8311 codec on I2S, configured over the touch
// controller's I2C bus, driving an FM8002E amplifier.
#pragma once

// Sets up I2S and the codec and turns the amplifier on. Call before
// lcd.init(): it borrows the touch I2C pins for a moment. Returns false
// (and stays silent) when the codec doesn't answer.
bool audio_begin();

// A short tick, for touch feedback. Returns at once.
void audio_click();
