#include "audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

#include "board_es3c28p.h"
#include "debounce.h"
#include "log.h"

namespace {

constexpr uint8_t kCodecAddr = 0x18;  // ES8311 with CE low
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kRate = kAudioRate;  // MCLK = 256 x 16 kHz = 4.096 MHz

// The tick: a 2.4 kHz tone dying away over a few ms, stereo 16-bit.
constexpr int kClickFrames = kRate * 25 / 1000;
constexpr float kClickHz = 2400;
constexpr float kClickDecayMs = 4;
constexpr float kClickLevel = 0.25f;  // of full scale
int16_t clickPcm[kClickFrames * 2];

bool ready = false;

// audio_play() and audio_click(): one task writes them to the speaker, so the two never interleave. A request is
// handed over whole, through a one-place queue a newer one overwrites (from any task or core, never waiting).
constexpr size_t kPlayChunk = 128;        // frames per write: 8 ms
constexpr uint32_t kStreamQuietMs = 300;  // a stream wrote this recently: it has the speaker
struct Request {
  const Sound* sound;
  int volume;
};
QueueHandle_t requests = nullptr;
TaskHandle_t playTask = nullptr;
std::atomic<bool> clickAsked{false};
std::atomic<bool> soundsOn{true};
std::atomic<uint32_t> lastStreamWrite{0};
std::atomic<uint32_t> lastClick{0};  // when a tick was last let through (0: never)

bool codecWrite(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(kCodecAddr);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

// ES8311 as an I2S slave, 16-bit, DAC only, MCLK from the ESP32 at 256 fs.
// The sequence and clock values follow Espressif's es8311 driver (esp-adf,
// esp-bsp) for 4.096 MHz MCLK at 16 kHz.
bool codecInit() {
  struct Write {
    uint8_t reg, value;
  };
  // One write per line, so each comment stays with its register.
  // clang-format off
  static const Write seq[] = {
      {0x45, 0x00},
      {0x01, 0x30},  // clocks from the MCLK pin
      {0x02, 0x00},  // pre-divide 1, multiply 1
      {0x03, 0x10},  // ADC single speed, OSR 16
      {0x16, 0x24},
      {0x04, 0x10},  // DAC OSR 16
      {0x05, 0x00},  // ADC and DAC clock divide 1
      {0x0B, 0x00},
      {0x0C, 0x00},
      {0x10, 0x1F},
      {0x11, 0x7F},
      {0x00, 0x80},  // slave, power on
      {0x01, 0x3F},  // all clocks on
      {0x06, 0x03},  // BCLK divide 4 (ignored as slave)
      {0x07, 0x00},  // LRCK divide 256
      {0x08, 0xFF},
      {0x09, 0x0C},  // serial input: I2S, 16-bit
      {0x0A, 0x0C},  // serial output: I2S, 16-bit
      {0x13, 0x10},
      {0x1B, 0x0A},
      {0x1C, 0x6A},
      {0x0D, 0x01},  // power up the analog circuits
      {0x0E, 0x02},
      {0x12, 0x00},  // power up the DAC
      {0x14, 0x1A},
      {0x15, 0x40},
      {0x37, 0x08},  // bypass the DAC equalizer
      {0x31, 0x00},  // unmute
      {0x32, 0xBF},  // DAC volume 0 dB
  };
  // clang-format on

  // Reset, then the setup.
  if (!codecWrite(0x00, 0x1F)) return false;
  delay(20);
  if (!codecWrite(0x00, 0x00)) return false;
  return std::all_of(std::begin(seq), std::end(seq), [](const Write& w) { return codecWrite(w.reg, w.value); });
}

bool startI2s() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 8;  // 128 ms: a stream rides out the Wi-Fi task being busy that long
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;  // silence, not the last sound, when idle
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCLK;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRCK;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  return i2s_set_pin(kPort, &pins) == ESP_OK;
}

void makeClick() {
  for (int i = 0; i < kClickFrames; i++) {
    float t = (float)i / kRate;
    float env = expf(-t * 1000 / kClickDecayMs);
    int16_t s = (int16_t)(32767 * kClickLevel * env * sinf(2 * (float)M_PI * kClickHz * t));
    clickPcm[2 * i] = s;
    clickPcm[2 * i + 1] = s;
  }
}

bool streamPlaying() {
  uint32_t at = lastStreamWrite.load();
  return at && millis() - at < kStreamQuietMs;
}

// Plays the latest sound, a chunk at a time, as mono turned stereo with its volume, and the tick mixed in when a
// touch asks for one; a newer sound replaces the one playing. Gives way to a stream (it would mix into noise).
void playLoop(void*) {
  int16_t out[kPlayChunk * 2];
  const Sound* sound = nullptr;
  size_t at = 0;     // frames of the sound played
  int gain = 0;      // of 256
  int clickAt = -1;  // frames of the tick played; -1: none
  for (;;) {
    Request r;
    if (xQueueReceive(requests, &r, 0) == pdTRUE) {
      sound = r.sound;
      at = 0;
      gain = r.volume < 0 ? 0 : (r.volume > 100 ? 256 : r.volume * 256 / 100);
    }
    if (clickAsked.exchange(false)) clickAt = 0;
    if (!soundsOn.load()) sound = nullptr;  // turned off: stops at once
    if (streamPlaying()) {                  // the stream has the speaker: drop what's playing
      sound = nullptr;
      clickAt = -1;
    }
    if (sound && at >= sound->frames) sound = nullptr;
    if (clickAt >= kClickFrames) clickAt = -1;
    if (!sound && clickAt < 0) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // nothing to play: audio_play() and audio_click() wake it
      continue;
    }

    size_t n = kPlayChunk;
    if (sound) n = std::min(n, sound->frames - at);
    if (!sound) n = std::min(n, (size_t)(kClickFrames - clickAt));
    for (size_t i = 0; i < n; i++) {
      int v = sound ? (sound->pcm[at + i] * gain) >> 8 : 0;
      if (clickAt >= 0 && clickAt + (int)i < kClickFrames) v += clickPcm[2 * (clickAt + i)];
      int16_t c = (int16_t)std::max(-32768, std::min(32767, v));
      out[2 * i] = c;
      out[2 * i + 1] = c;
    }
    if (sound) at += n;
    if (clickAt >= 0) clickAt += (int)n;
    size_t written;
    i2s_write(kPort, out, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
  }
}

}  // namespace

bool audio_begin() {
  pinMode(AUDIO_EN, OUTPUT);
  digitalWrite(AUDIO_EN, HIGH);  // amplifier off while setting up: no pop

  if (!startI2s()) {
    LOGF("audio: I2S setup failed\n");
    return false;
  }

  // The codec shares the touch controller's I2C pins. LovyanGFX takes them
  // over in lcd.init(), so use them here first and let go.
  Wire1.begin(TOUCH_SDA, TOUCH_SCL, 100000);
  Wire1.beginTransmission(kCodecAddr);
  bool found = Wire1.endTransmission() == 0;
  if (!found) {
    LOGF("audio: no ES8311 at 0x%02x; I2C devices:", kCodecAddr);
    for (uint8_t a = 1; a < 127; a++) {
      Wire1.beginTransmission(a);
      if (Wire1.endTransmission() == 0) LOGF(" 0x%02x", a);
    }
    LOGF("\n");
  }
  bool ok = found && codecInit();
  Wire1.end();
  if (!ok) {
    if (found) LOGF("audio: ES8311 setup failed\n");
    return false;
  }

  makeClick();
  requests = xQueueCreate(1, sizeof(Request));
  if (requests) xTaskCreate(playLoop, "sounds", 3072, nullptr, 1, &playTask);
  delay(10);
  digitalWrite(AUDIO_EN, LOW);  // amplifier on
  ready = true;
  LOGF("audio: ready\n");
  return true;
}

void audio_click() {
  if (!ready || !playTask || streamPlaying()) return;  // over a stream: nothing to hear it over, and no pop
  // One tick per press: a request within kClickMs of the last one is the same touch (a finger the touch controller
  // lost for a moment), not a new one. From any task: only the one that moves lastClick asks.
  uint32_t last = lastClick.load(), now = millis();
  if (!debounce::due(last, now, debounce::kClickMs) || !lastClick.compare_exchange_strong(last, now | 1)) return;
  clickAsked.store(true);
  xTaskNotifyGive(playTask);
}

void audio_play(const Sound& sound, int volume) {
  if (!ready || !playTask || !soundsOn.load()) return;
  Request r{&sound, volume};
  xQueueOverwrite(requests, &r);
  xTaskNotifyGive(playTask);
}

void audio_set_sounds(bool on) { soundsOn.store(on); }

bool audio_ready() { return ready; }

size_t audio_write(const int16_t* stereo, size_t frames, uint32_t waitMs) {
  if (!ready) return 0;
  lastStreamWrite.store(millis() | 1);
  size_t written = 0;
  i2s_write(kPort, stereo, frames * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(waitMs));
  return written / (2 * sizeof(int16_t));
}
