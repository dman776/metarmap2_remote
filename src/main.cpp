// MetarMap2 Remote: a small version of the map's /remote page on the ES3C28P.
#include <Arduino.h>
#include <WiFi.h>

#include "audio.h"
#include "board_es3c28p.h"
#include "display.h"
#include "firmware_version.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "settings.h"
#include "ui.h"
#include "updater.h"

volatile bool g_serialBusy = false;

static LGFX lcd;
static MapClient mapClient;
static Updater updater;
static Settings settings;  // read once at startup; the clients keep pointers into it

void setup() {
  Serial.begin(115200);
  LOGF("MetarMap2 Remote %s\n", FIRMWARE_VERSION);

  settings = settings_load();  // Wi-Fi and map: from secrets.h, else as saved
  neopixelWrite(RGB_LED, 0, 0, 0);
  audio_begin();  // before lcd.init(): it borrows the touch I2C pins

  lcd.init();
  lcd.setRotation(3);  // landscape, USB-C on the right
  lcd.setBrightness(200);

  lvgl_port_init(lcd);
  ui_init(mapClient, updater);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("metarmap-remote");
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.wifiName.c_str(), settings.wifiPassword.c_str());

  mapClient.begin(settings.mapHost.c_str(), settings.mapPort);
  updater.begin(settings);
}

void loop() {
  lv_timer_handler();
  ui_tick();

  // Serial console: "S" sends a screenshot (tools/screenshot.py); "1" and
  // "2" open the Visualizer and Locator tabs; "3" shows the splash screen
  // and "4" the update screen.
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'S') {
      g_serialBusy = true;
      lvgl_port_screenshot();
      g_serialBusy = false;
    } else if (c == '1' || c == '2') {
      ui_show_tab(c - '1');
    } else if (c == '3') {
      ui_show_splash();
    } else if (c == '4') {
      ui_show_update();
    }
  }
  delay(5);
}
