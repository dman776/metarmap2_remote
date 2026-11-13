#include "map_scan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mdns.h>

#include <atomic>

#include "log.h"

namespace {

constexpr uint32_t kListenMs = 3000;   // how long the scan listens for answers
constexpr size_t kMaxAnswers = 20;     // answers kept (a Chart can answer twice); map_list caps the rows at 8
constexpr uint32_t kAddressMs = 1000;  // asking a Chart's address, when its answer came without one
constexpr int kMaxAddressLookups = 3;  // at most these, so a scan is over in about 6 s at worst
constexpr uint32_t kStack = 5 * 1024;  // ESP-IDF's mdns task does the work: this one waits, copies and logs

SemaphoreHandle_t mutex = nullptr;
MapScan state;
uint32_t stateRev = 0;
std::atomic<bool> running{false};

void publish(const MapScan& s) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  state = s;
  stateRev++;
  xSemaphoreGive(mutex);
}

std::string txtValue(const mdns_result_t* r, const char* key) {
  for (size_t i = 0; i < r->txt_count; i++) {
    if (!r->txt[i].key || strcmp(r->txt[i].key, key) != 0 || !r->txt[i].value) continue;
    size_t len = r->txt_value_len ? r->txt_value_len[i] : strlen(r->txt[i].value);
    return std::string(r->txt[i].value, len);
  }
  return "";
}

std::string ipv4(const mdns_result_t* r) {
  for (const mdns_ip_addr_t* a = r->addr; a; a = a->next) {
    if (a->addr.type == ESP_IPADDR_TYPE_V4) return IPAddress(a->addr.u_addr.ip4.addr).toString().c_str();
  }
  return "";
}

void scanTask(void*) {
  MapScan s;
  s.done = true;
  mdns_result_t* results = nullptr;
  esp_err_t err = ESP_OK;
  if (WiFi.status() != WL_CONNECTED) {
    s.error = "Not on Wi-Fi";
  } else {
    // the IDF's own call: Arduino's MDNS.queryService() keeps its answers in the shared MDNS object, and its
    // instance names out of reach
    err = mdns_query_ptr("_metarmap2", "_tcp", kListenMs, kMaxAnswers, &results);
    // not started yet (remote_web.cpp starts it a moment after Wi-Fi is up)
    if (err != ESP_OK) s.error = err == ESP_ERR_INVALID_STATE ? "Not on the network yet" : "The scan failed";
  }
  // A Chart can answer twice (once over IPv4, once over IPv6, each with what it knew): map_list merges them
  for (const mdns_result_t* r = results; r; r = r->next) {
    map_list::Found f;
    f.name = r->instance_name ? r->instance_name : "";
    f.hostname = r->hostname ? r->hostname : "";
    f.port = r->port;
    f.version = txtValue(r, "version");
    f.ip = ipv4(r);
    s.found.push_back(f);
  }
  if (results) mdns_query_results_free(results);
  s.found = map_list::merge(s.found);
  // An answer without the address (it was in no answer): asked for, by its host name
  int lookups = 0;
  for (map_list::Found& f : s.found) {
    if (!f.ip.empty() || f.hostname.empty() || lookups >= kMaxAddressLookups) continue;
    lookups++;
    esp_ip4_addr_t addr{};
    std::string host = f.hostname.substr(0, f.hostname.find('.'));  // the IDF asks <host>.local itself
    if (mdns_query_a(host.c_str(), kAddressMs, &addr) == ESP_OK) f.ip = IPAddress(addr.addr).toString().c_str();
  }
  if (err == ESP_OK) {
    LOGF("maps: found %u\n", (unsigned)s.found.size());
  } else {
    LOGF("maps: scan failed (%s, %d)\n", s.error.c_str(), (int)err);
  }
  // how much of its kStack it never used (bytes, on the ESP32), to check the 5 KB is enough on the board
  LOGF("maps: scan stack %u B unused of %u\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)kStack);
  publish(s);
  running = false;
  vTaskDelete(nullptr);
}

}  // namespace

bool map_scan_start() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (running.exchange(true)) return false;
  MapScan s;
  s.scanning = true;
  publish(s);
  // Core 0, beside Wi-Fi, as the map client; LVGL runs in the Arduino loop on core 1. It ends when the scan does,
  // giving its stack back.
  if (xTaskCreatePinnedToCore(scanTask, "map_scan", kStack, nullptr, 1, nullptr, 0) != pdPASS) {
    s.scanning = false;
    s.done = true;
    s.error = "No memory to scan";
    publish(s);
    running = false;
    return false;
  }
  return true;
}

bool map_scan_get(MapScan& out, uint32_t& rev) {
  if (!mutex) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool changed = rev != stateRev;
  if (changed) {
    out = state;
    rev = stateRev;
  }
  xSemaphoreGive(mutex);
  return changed;
}
