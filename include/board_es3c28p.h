// Pin map for the LCDwiki ES3C28P (2.8" ESP32-S3 touch display).
// Source: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
#pragma once

// ILI9341V LCD, 4-wire SPI. LCD reset is tied to the S3's EN line.
#define LCD_SCLK 12
#define LCD_MOSI 11
#define LCD_MISO 13
#define LCD_CS 10
#define LCD_DC 46
#define LCD_RST -1
#define LCD_BL 45  // high = on
#define LCD_WIDTH 240
#define LCD_HEIGHT 320

// FT6336G capacitive touch, I2C (shared with the external IIC header).
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18  // active low
#define TOUCH_INT 17  // low while touched
#define TOUCH_I2C_ADDR 0x38

// Single WS2812-style RGB LED.
#define RGB_LED 42

// microSD, 4-bit SDMMC (shared with the external SPI header).
#define SD_CLK 38
#define SD_CMD 40
#define SD_D0 39
#define SD_D1 41
#define SD_D2 48
#define SD_D3 47

// I2S audio amp and microphone.
#define AUDIO_EN 1  // active low
#define I2S_MCLK 4
#define I2S_BCLK 5
#define I2S_LRCK 7
#define I2S_DOUT 8
#define I2S_DIN 6

// Battery voltage divider on an ADC pin.
#define BAT_ADC 9

#define BOOT_BUTTON 0

// Free on the expansion header: IO2, IO3, IO14, IO21.
