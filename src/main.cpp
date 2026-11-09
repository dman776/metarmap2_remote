// MetarMap2 Remote: a small version of the map's /remote page on the ES3C28P.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "audio.h"
#include "board_es3c28p.h"
#include "display.h"
#include "firmware_version.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "remote_web.h"
#include "settings.h"
#include "stream.h"
#include "ui.h"
#include "updater.h"
#include "wifi_setup.h"

volatile bool g_serialBusy = false;

static LGFX lcd;
static MapClient mapClient;
static Updater updater;
static Settings settings;  // read once at startup; the clients keep pointers into it

void setup() {
  Serial.begin(115200);
  log_begin();  // the log the web page shows
  LOGF("MetarMap2 Remote %s\n", FIRMWARE_VERSION);

  settings = settings_load();  // Wi-Fi and map: from secrets.h, else as saved
  neopixelWrite(RGB_LED, 0, 0, 0);
  audio_begin();  // before lcd.init(): it borrows the touch I2C pins
  // the Radio's stream, and spoken METARs from the map
  stream_begin(settings.streamUrl, "http://" + settings.mapHost + ":" + std::to_string(settings.mapPort),
               settings.volume);

  lcd.init();
  lcd.setRotation(3);  // landscape, USB-C on the right
  lcd.setBrightness(200);

  lvgl_port_init(lcd);
  ui_init(mapClient, updater);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("metarmap-remote");
  wifi_setup_begin(settings);
  if (settings.wifiName == kSetupSsid) {
    wifi_setup_start(SetupReason::Fresh);  // not set up yet: its own network, for a phone to set it up
  } else {
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiName.c_str(), settings.wifiPassword.c_str());
  }

  mapClient.begin(settings.mapHost.c_str(), settings.mapPort);
  updater.begin(settings);
  remote_web_begin(updater, settings);  // starts once Wi-Fi is up, or config mode is on
}

void loop() {
  lv_timer_handler();
  ui_tick();
  wifi_setup_tick();
  remote_web_tick();

  // Serial console: "S" sends a screenshot (tools/screenshot.py); "1" and
  // "2" open the LEDs and Airports panels; "3" shows the splash screen,
  // "4" the update screen, "5" the visualizer picker, "6" the brightness
  // sheet and "7" an airport's card; "H" logs the free memory; "W" starts
  // Wi-Fi config mode (as the splash's Wi-Fi setup button does).
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
    } else if (c >= '5' && c <= '7') {
      ui_open("pbc"[c - '5']);
    } else if (c == 'W') {
      wifi_setup_start(SetupReason::Lost);
    } else if (c == 'H') {  // the memory left: internal RAM is what TLS (update checks) and Wi-Fi need
      LOGF("heap: internal free %u, largest block %u, min ever %u; PSRAM free %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
  }
  delay(5);
}
