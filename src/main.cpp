// MetarMap2 Copilot: a small version of the map's /remote page on the ES3C28P.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "audio.h"
#include "board_es3c28p.h"
#include "display.h"
#include "firmware_version.h"
#include "last_run.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "remote_web.h"
#include "settings.h"
#include "status_led.h"
#include "stream.h"
#include "ui.h"
#include "updater.h"
#include "wifi_setup.h"

volatile bool g_serialBusy = false;

static LGFX lcd;
static MapClient mapClient;
static Updater updater;
static Settings settings;  // read once at startup; the updater, the web page and the screens keep pointers to it
static status_led::StatusLight statusLight;

// The status light on the back: what the Copilot is doing (status_led.h), written only when its color changes
static void tickStatusLight() {
  static Connection conn;
  static uint32_t connRev = 0;
  static UpdateState upd;
  static uint32_t updRev = 0;
  static status_led::Rgb shown{1, 1, 1};  // not a real color: the first tick writes
  mapClient.getConnection(conn, connRev);
  updater.get(upd, updRev);
  status_led::Inputs in;
  in.wifi = conn.wifi;
  in.online = conn.online;
  in.setup = wifi_setup_active();
  in.installing = upd.phase == UpdateState::Installing || upd.phase == UpdateState::Restarting;
  status_led::Rgb c = statusLight.tick(in, millis(), settings.ledBrightness);
  if (c != shown) {
    shown = c;
    neopixelWrite(RGB_LED, c.r, c.g, c.b);  // red, green, blue: it sends them in the WS2812's own order (G, R, B)
  }
}

// Wi-Fi's ups and downs in the log, with their times (the Wi-Fi task logs them): a disconnect once per reason in a
// row, not each retry's.
static int lastDisconnect = -1;  // its reason; -1 when connected (only the Wi-Fi event task uses it)

static void logWifiEvents() {
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        lastDisconnect = -1;
        LOGF("wifi: got %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      },
      ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        int reason = info.wifi_sta_disconnected.reason;
        if (reason != lastDisconnect) LOGF("wifi: disconnected (reason %d)\n", reason);
        lastDisconnect = reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { LOGF("wifi: lost the IP address\n"); },
               ARDUINO_EVENT_WIFI_STA_LOST_IP);
}

void setup() {
  Serial.begin(115200);
  log_begin();  // the log the web page shows
  LOGF("MetarMap2 Copilot %s\n", FIRMWARE_VERSION);
  log_last_run();  // why the last run ended (a crash's summary too), for the web page's log

  settings = settings_load();  // Wi-Fi and map: from secrets.h, else as saved
  neopixelWrite(RGB_LED, 0, 0, 0);
  audio_set_sounds(settings.sounds);
  audio_begin();  // before lcd.init(): it borrows the touch I2C pins
  // the Radio's stream, and spoken METARs from the map
  stream_begin(settings.streamUrl, "http://" + settings.mapHost + ":" + std::to_string(settings.mapPort),
               settings.volume);

  lcd.init();
  lcd.setRotation(3);  // landscape, USB-C on the right
  lcd.setBrightness(200);

  lvgl_port_init(lcd);
  ui_init(mapClient, updater, settings);

  // The Copilot keeps the network and password in its own settings and gives them to WiFi.begin() each time, so the
  // Wi-Fi driver needn't keep a copy in flash too: not persistent, each begin() (while not connected, e.g. Wi-Fi
  // setup's retry every 30 s) would write the station config to NVS. Before the first WiFi.mode(), which applies it.
  WiFi.persistent(false);
  logWifiEvents();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("metarmap-copilot");
  wifi_setup_begin(settings);
  if (isSetupSsid(settings.wifiName)) {
    wifi_setup_start(SetupReason::Fresh);  // not set up yet: its own network, for a phone to set it up
  } else {
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiName.c_str(), settings.wifiPassword.c_str());
  }

  mapClient.begin(settings.mapHost.c_str(), settings.mapPort);  // a copy: the map picker switches it with setMap()
  updater.begin(settings);
  remote_web_begin(updater, settings);  // starts once Wi-Fi is up, or config mode is on
}

void loop() {
  lv_timer_handler();
  ui_tick();
  wifi_setup_tick();
  remote_web_tick();
  tickStatusLight();

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
