#include "last_run.h"

#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cstdio>

#include "log.h"

namespace {

const char* resetWord(esp_reset_reason_t why) {
  switch (why) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external reset";
    case ESP_RST_SW:
      return "software restart";
    case ESP_RST_PANIC:
      return "panic (a crash)";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "wake from deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout (low power)";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "unknown";
  }
}

// The core dump a crash left in flash (the coredump partition), summed up, then erased.
void logCoreDump() {
  constexpr size_t kBlank = 0xFFFFFFFF;  // the size word of an erased (empty) partition
  size_t addr = 0, size = kBlank;
  esp_err_t got = esp_core_dump_image_get(&addr, &size);
  if (got != ESP_OK) {
    // none (the usual case: a blank partition), or none to be had (no partition, flash unreadable)
    if (got == ESP_ERR_NOT_FOUND || size == kBlank) return;
    // one that's there but can't be read (a bad size or checksum): IDF would complain of it at every boot
    LOGF("last crash: unreadable crash dump (%s, %u bytes), erased\n", esp_err_to_name(got), (unsigned)size);
    esp_core_dump_image_erase();
    return;
  }
  if (esp_core_dump_image_check() != ESP_OK) {
    LOGF("last crash: a core dump that can't be read (%u bytes); erased\n", (unsigned)size);
    esp_core_dump_image_erase();
    return;
  }
  // ~250 bytes: off the small setup() stack, in PSRAM if there is any
  void* mem = heap_caps_malloc(sizeof(esp_core_dump_summary_t), MALLOC_CAP_SPIRAM);
  if (!mem) mem = heap_caps_malloc(sizeof(esp_core_dump_summary_t), MALLOC_CAP_DEFAULT);
  auto* sum = static_cast<esp_core_dump_summary_t*>(mem);
  if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
    sum->exc_task[sizeof(sum->exc_task) - 1] = '\0';
    sum->app_elf_sha256[sizeof(sum->app_elf_sha256) - 1] = '\0';
    LOGF("last crash: task %s, PC 0x%08x, cause %u, address 0x%08x, ELF sha256 %s\n", sum->exc_task,
         (unsigned)sum->exc_pc, (unsigned)sum->ex_info.exc_cause, (unsigned)sum->ex_info.exc_vaddr,
         reinterpret_cast<const char*>(sum->app_elf_sha256));
    char bt[16 * 11 + 1] = "";  // up to 16 " 0x%08x"
    size_t used = 0;
    uint32_t depth = sum->exc_bt_info.depth;
    if (depth > 16) depth = 16;
    for (uint32_t i = 0; i < depth && used < sizeof(bt); i++) {
      int n = snprintf(bt + used, sizeof(bt) - used, " 0x%08x", (unsigned)sum->exc_bt_info.bt[i]);
      if (n < 0) break;
      used += (size_t)n;
    }
    LOGF("last crash: backtrace%s%s\n", bt, sum->exc_bt_info.corrupted ? " (corrupted)" : "");
  } else {
    LOGF("last crash: a core dump (%u bytes) without a summary\n", (unsigned)size);
  }
  heap_caps_free(mem);
  esp_core_dump_image_erase();  // reported once
}

}  // namespace

void log_last_run() {
  esp_reset_reason_t why = esp_reset_reason();
  LOGF("last run ended: %s (reset reason %d)\n", resetWord(why), (int)why);
  logCoreDump();
}
