// LVGL 9 configuration. Anything not set here takes LVGL's default
// (see lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the C library's allocator: the ESP32 heap, with large blocks in PSRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE  // LVGL runs only in the Arduino loop task
#define LV_DEF_REFR_PERIOD 20
#define LV_DPI_DEF 140

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif  // LV_CONF_H
