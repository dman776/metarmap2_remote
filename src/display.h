// LovyanGFX driver for the ES3C28P's ILI9341V panel and FT6336G touch.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_es3c28p.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_FT5x06 touch_;  // the FT6336 speaks the FT5x06 protocol

 public:
  LGFX() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = LCD_SCLK;
      cfg.pin_mosi = LCD_MOSI;
      cfg.pin_miso = LCD_MISO;
      cfg.pin_dc = LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = LCD_CS;
      cfg.pin_rst = LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = LCD_WIDTH;
      cfg.panel_height = LCD_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = LCD_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.i2c_port = 0;
      cfg.i2c_addr = TOUCH_I2C_ADDR;
      cfg.pin_sda = TOUCH_SDA;
      cfg.pin_scl = TOUCH_SCL;
      cfg.pin_int = TOUCH_INT;
      cfg.pin_rst = TOUCH_RST;
      cfg.freq = 400000;
      // The touch panel is rotated 180° from the LCD; min > max flips an axis.
      cfg.x_min = LCD_WIDTH - 1;
      cfg.x_max = 0;
      cfg.y_min = LCD_HEIGHT - 1;
      cfg.y_max = 0;
      cfg.offset_rotation = 0;
      cfg.bus_shared = false;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
