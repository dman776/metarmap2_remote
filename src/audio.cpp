#include "audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "board_es3c28p.h"
#include "log.h"

namespace {

constexpr uint8_t kCodecAddr = 0x18;  // ES8311 with CE low
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kRate = 16000;  // MCLK = 256 x 16 kHz = 4.096 MHz

// The tick: a 2.4 kHz tone dying away over a few ms, stereo 16-bit.
constexpr int kClickFrames = kRate * 25 / 1000;
constexpr float kClickHz = 2400;
constexpr float kClickDecayMs = 4;
constexpr float kClickLevel = 0.25f;  // of full scale
int16_t clickPcm[kClickFrames * 2];

bool ready = false;

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
  cfg.dma_buf_count = 4;
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
  delay(10);
  digitalWrite(AUDIO_EN, LOW);  // amplifier on
  ready = true;
  LOGF("audio: ready\n");
  return true;
}

void audio_click() {
  if (!ready) return;
  size_t written;
  i2s_write(kPort, clickPcm, sizeof(clickPcm), &written, 0);
}
