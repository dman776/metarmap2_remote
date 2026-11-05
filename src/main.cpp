// ES3C28P bring-up: exercises the display, touch, RGB LED and Wi-Fi, and
// checks that the map's /api/status answers. Placeholder until the real
// remote UI is written.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "board_es3c28p.h"
#include "display.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h missing: copy include/secrets.example.h; Wi-Fi disabled"
#endif

static LGFX lcd;

static constexpr int kStatusY = 120;
static constexpr int kTouchY = 290;

static void statusLine(int row, uint16_t color, const char* fmt, ...) {
  char buf[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  int y = kStatusY + row * 20;
  lcd.fillRect(0, y, lcd.width(), 20, TFT_BLACK);
  lcd.setTextColor(color, TFT_BLACK);
  lcd.drawString(buf, 8, y);
  Serial.println(buf);
}

static void drawHeader() {
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0, 0, lcd.width(), 36, TFT_NAVY);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  lcd.drawString("METARMap Remote", 8, 9);
  lcd.setFont(&fonts::Font2);

  // Colour bars: if red/blue are swapped or the colours look inverted,
  // flip rgb_order / invert in display.h.
  const uint16_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  int w = lcd.width() / 4;
  for (int i = 0; i < 4; i++) lcd.fillRect(i * w, 44, w, 16, bars[i]);

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.setCursor(8, 70);
  lcd.printf("Flash %u MB  PSRAM %u KB", ESP.getFlashChipSize() >> 20,
             ESP.getPsramSize() >> 10);
  lcd.setCursor(8, 90);
  lcd.printf("Heap %u KB free", ESP.getFreeHeap() >> 10);
}

static void checkWifiAndMap() {
#ifdef WIFI_SSID
  statusLine(0, TFT_YELLOW, "Wi-Fi: connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    statusLine(0, TFT_RED, "Wi-Fi: failed (%d)", WiFi.status());
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      // The S3 is 2.4 GHz only; list what it can see to spot a 5 GHz SSID.
      int n = WiFi.scanNetworks();
      Serial.printf("%d networks visible:\n", n);
      for (int i = 0; i < n; i++)
        Serial.printf("  %-32s ch%-2d %d dBm\n", WiFi.SSID(i).c_str(),
                      WiFi.channel(i), WiFi.RSSI(i));
      statusLine(1, TFT_RED, "SSID not seen (2.4 GHz only)");
    }
    return;
  }
  statusLine(0, TFT_GREEN, "Wi-Fi: %s", WiFi.localIP().toString().c_str());

  HTTPClient http;
  String url = String("http://") + MAP_HOST + ":" + MAP_PORT + "/api/status";
  http.setTimeout(5000);
  http.begin(url);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    statusLine(1, TFT_GREEN, "Map: OK (%d bytes)", http.getSize());
  } else {
    statusLine(1, TFT_RED, "Map: %s (%d)", MAP_HOST, code);
  }
  http.end();
#else
  statusLine(0, TFT_DARKGREY, "Wi-Fi: no secrets.h");
#endif
}

void setup() {
  Serial.begin(115200);

  pinMode(AUDIO_EN, OUTPUT);
  digitalWrite(AUDIO_EN, HIGH);  // keep the amp off

  lcd.init();
  lcd.setRotation(0);  // portrait, USB-C at the bottom
  lcd.setBrightness(200);
  drawHeader();

  neopixelWrite(RGB_LED, 0, 0, 16);
  checkWifiAndMap();
  neopixelWrite(RGB_LED, 0, 16, 0);

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("Touch the screen", 8, kTouchY);
}

void loop() {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    lcd.fillCircle(x, y, 3, TFT_CYAN);
    lcd.fillRect(0, kTouchY, lcd.width(), 20, TFT_BLACK);
    lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    lcd.setCursor(8, kTouchY);
    lcd.printf("Touch %3d, %3d", (int)x, (int)y);
  }
  delay(10);
}
