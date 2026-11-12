#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "audio.h"
#include "log.h"

static LGFX* g_lcd = nullptr;

static constexpr int kBufLines = 40;

static uint32_t tick() { return millis(); }

static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  g_lcd->pushImage(area->x1, area->y1, w, h, (const lgfx::rgb565_t*)px);
  lv_display_flush_ready(disp);
}

// The FT6336 sometimes misses a finger for a reading in the middle of a
// touch. Count it as lifted only after it has been gone this long, so one
// touch is one press (one tick, one click) for LVGL.
static constexpr uint32_t kReleaseMs = 60;

static bool touchDown = false;

static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  static uint32_t lastSeen = 0;
  static lv_point_t last = {0, 0};
  int32_t x, y;
  if (g_lcd->getTouch(&x, &y)) {
    if (!touchDown) {
      audio_click();  // a tick as the finger lands: the only one a press makes (debounced there)
    }
    touchDown = true;
    lastSeen = millis();
    last.x = x;
    last.y = y;
  } else if (touchDown && millis() - lastSeen >= kReleaseMs) {
    touchDown = false;
  }
  data->point = last;
  data->state = touchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
