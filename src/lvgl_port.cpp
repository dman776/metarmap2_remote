#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "audio.h"
#include "log.h"
#include "quiet_dim.h"

static LGFX* g_lcd = nullptr;

static constexpr int kBufLines = 40;

static uint32_t tick() { return millis(); }

static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  g_lcd->pushImage(area->x1, area->y1, w, h, (const lgfx::rgb565_t*)px);
  lv_display_flush_ready(disp);
}

// The FT6336 sometimes misses a finger for a reading in the middle of a touch: it counts as lifted only after it
// has been gone this long, so one touch is one press (one tick, one click) for LVGL (quiet_dim::Touch).
static constexpr uint32_t kReleaseMs = 60;

static quiet_dim::Backlight backlight;
static quiet_dim::Touch touch(backlight, kReleaseMs);  // a touch on a dim screen only wakes it
static uint8_t shownLevel = quiet_dim::kNormal;        // main.cpp starts the backlight at that

// The backlight as quiet hours and the last touch have it (quiet_dim.h). Called each touch reading (LVGL reads
// every 33 ms), so it re-dims on time without a timer of its own.
static void applyBacklight(uint32_t now) {
  uint8_t level = backlight.level(now);
  if (level == shownLevel) return;
  shownLevel = level;
  g_lcd->setBrightness(level);
  LOGF("screen: %s\n", level == quiet_dim::kDim ? "dimmed (quiet hours)" : "full brightness");
}

static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  static lv_point_t last = {0, 0};
  int32_t x, y;
  uint32_t now = millis();
  bool finger = g_lcd->getTouch(&x, &y);
  quiet_dim::Touch::Reading r = touch.read(finger, now);
  if (r.click) audio_click();  // a tick as the finger lands: the only one a press makes (debounced there)
  if (finger) {
    last.x = x;
    last.y = y;
  }
  applyBacklight(now);
  data->point = last;
  data->state = r.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvgl_port_set_quiet(bool quiet) {
  if (quiet != backlight.quiet()) LOGF("screen: the Chart's quiet hours %s\n", quiet ? "began" : "ended");
  backlight.setQuiet(quiet);
  if (g_lcd) applyBacklight(millis());  // ended: full at once, not at the next touch reading
}

static void logPrint(lv_log_level_t, const char* buf) { LOGF("%s", buf); }

void lvgl_port_init(LGFX& lcd) {
  g_lcd = &lcd;
  lv_init();
  lv_tick_set_cb(tick);
  lv_log_register_print_cb(logPrint);

  int32_t w = lcd.width(), h = lcd.height();
  size_t bytes = w * kBufLines * sizeof(uint16_t);
  void* buf1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

  lv_display_t* disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readTouch);
  // A finger drifts: a touch has to move 16 px (not 10) before it scrolls instead of tapping
  lv_indev_set_scroll_limit(indev, 16);
  // A hold (the picker's skip) is 600 ms, as on the Cockpit, not LVGL's 400: a slow tap stays a tap
  lv_indev_set_long_press_time(indev, 600);
}

void lvgl_port_screenshot() {
  int32_t w = g_lcd->width(), h = g_lcd->height();
  static lgfx::rgb565_t line[480];
  Serial.printf("\nSCREENSHOT %d %d\n", (int)w, (int)h);
  for (int32_t y = 0; y < h; y++) {
    g_lcd->readRect(0, y, w, 1, line);
    Serial.write((const uint8_t*)line, w * sizeof(uint16_t));
  }
  Serial.print("\nEND\n");
  Serial.flush();
}

void lvgl_port_capture(std::vector<uint16_t>& pixels, int& w, int& h) {
  w = g_lcd->width();
  h = g_lcd->height();
  pixels.resize((size_t)w * h);  // 150 KB: in PSRAM
  for (int y = 0; y < h; y++) g_lcd->readRect(0, y, w, 1, (lgfx::rgb565_t*)&pixels[(size_t)y * w]);
}
