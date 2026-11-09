#include "log.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdarg>

#include "log_ring.h"

namespace {

constexpr size_t kKeep = 32 * 1024;
LogRing* ring = nullptr;
SemaphoreHandle_t lock = nullptr;

}  // namespace

void log_begin() {
  if (ring) return;
  char* buf = static_cast<char*>(heap_caps_malloc(kKeep, MALLOC_CAP_SPIRAM));
  if (!buf) return;  // no PSRAM: serial only
  lock = xSemaphoreCreateMutex();
  ring = new LogRing(buf, kKeep);
}

void remote_log(const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (n < 0) return;
  size_t len = std::min((size_t)n, sizeof(line) - 1);  // a longer line is cut
  if (!g_serialBusy) Serial.write(reinterpret_cast<const uint8_t*>(line), len);
  if (ring && xSemaphoreTake(lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    char stamp[16];
    // each line marked with the seconds since startup, as the serial monitor doesn't do it
    static bool lineStart = true;
    for (size_t i = 0; i < len;) {
      if (lineStart) {
        int s = snprintf(stamp, sizeof(stamp), "[%7.1f] ", millis() / 1000.0);
        ring->write(stamp, s);
        lineStart = false;
      }
      const char* nl = static_cast<const char*>(memchr(line + i, '\n', len - i));
      size_t end = nl ? (size_t)(nl - line) + 1 : len;  // up to and with the newline
      ring->write(line + i, end - i);
      lineStart = nl != nullptr;
      i = end;
    }
    xSemaphoreGive(lock);
  }
}

std::string log_since(uint64_t& from) {
  if (!ring || xSemaphoreTake(lock, pdMS_TO_TICKS(200)) != pdTRUE) return "";
  std::string text = ring->since(from);
  xSemaphoreGive(lock);
  return text;
}
