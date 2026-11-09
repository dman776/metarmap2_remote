// LVGL's memory (LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM in lv_conf.h): its objects, styles and draw buffers in PSRAM.
// The screens make hundreds of small objects; with the C library's malloc they all landed in internal RAM (it keeps
// blocks under 4 KB there), which left too little for TLS: the firmware update check failed ("SSL - Memory
// allocation failed") and an update could crash. Internal RAM only if PSRAM runs out.
#include <esp_heap_caps.h>

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;  // not supported
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) {
  void* p = heap_caps_malloc(size, PSRAM_CAPS);
  return p ? p : malloc(size);
}

void* lv_realloc_core(void* p, size_t new_size) {
  void* q = heap_caps_realloc(p, new_size, PSRAM_CAPS);
  return q ? q : realloc(p, new_size);
}

void lv_free_core(void* p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { LV_UNUSED(mon_p); }

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
