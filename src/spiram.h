// An ArduinoJson allocator in PSRAM, for the big JSON answers (the map's
// /api/live and /api/status, GitHub's releases).
#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

inline SpiRamAllocator* spiram() {
  static SpiRamAllocator allocator;
  return &allocator;
}
