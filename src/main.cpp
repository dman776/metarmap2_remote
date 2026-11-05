// METARMap Remote: a small version of the map's /remote page on the ES3C28P.
#include <Arduino.h>
#include <WiFi.h>

#include "board_es3c28p.h"
#include "display.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

volatile bool g_serialBusy = false;

static LGFX lcd;
static MapClient mapClient;

void setup() {
  Serial.begin(115200);

  pinMode(AUDIO_EN, OUTPUT);
  digitalWrite(AUDIO_EN, HIGH);  // keep the amp off
  neopixelWrite(RGB_LED, 0, 0, 0);

  lcd.init();
  lcd.setRotation(3);  // landscape, USB-C on the right
  lcd.setBrightness(200);

  lvgl_port_init(lcd);
  ui_init(mapClient);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("metarmap-remote");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mapClient.begin(MAP_HOST, MAP_PORT);
}

void loop() {
  lv_timer_handler();
  ui_tick();

  // Serial console: "S" sends a screenshot (tools/screenshot.py); "1" and
  // "2" open the Visualizer and Locator tabs.
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'S') {
      g_serialBusy = true;
      lvgl_port_screenshot();
      g_serialBusy = false;
    } else if (c == '1' || c == '2') {
      ui_show_tab(c - '1');
    }
  }
  delay(5);
}
