#include "map_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "log.h"

namespace {

constexpr uint32_t kLiveMs = 1500;         // the /remote page polls every 1 s
constexpr uint32_t kStatusMs = 60 * 1000;  // and /api/status every minute
constexpr uint32_t kTimeoutMs = 5000;

// The JSON documents live in PSRAM: /api/live is ~18 KB, /api/status ~25 KB.
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
  }
};
SpiRamAllocator spiram;

uint32_t parseColor(const char* hex) {
  if (!hex || hex[0] != '#') return 0;
  return strtoul(hex + 1, nullptr, 16);
}

double numberOrNan(JsonVariantConst v) { return v.isNull() ? NAN : v.as<double>(); }

}  // namespace

void MapClient::begin(const char* host, uint16_t port) {
  host_ = host;
  port_ = port;
  queue_ = xQueueCreate(8, sizeof(Cmd));
  mutex_ = xSemaphoreCreateMutex();
  // Core 0, beside Wi-Fi; LVGL runs in the Arduino loop on core 1.
  xTaskCreatePinnedToCore(taskEntry, "map_client", 12 * 1024, this, 1, nullptr, 0);
}

void MapClient::command(const String& path) {
  Cmd cmd{};
  strlcpy(cmd.path, path.c_str(), sizeof(cmd.path));
  if (xQueueSend(queue_, &cmd, 0) != pdTRUE) LOGF("map: command queue full, dropped %s\n", cmd.path);
}

bool MapClient::getStatus(MapStatus& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != statusRev_;
  if (changed) {
    out = status_;
    rev = statusRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
}

bool MapClient::getLive(MapLive& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != liveRev_;
  if (changed) {
    out = live_;
    rev = liveRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
}

bool MapClient::getConnection(Connection& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != connRev_;
  if (changed) {
    out = conn_;
    rev = connRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
}

void MapClient::taskEntry(void* self) { static_cast<MapClient*>(self)->run(); }

void MapClient::run() {
  uint32_t lastLive = 0, lastStatus = 0;
  bool haveStatus = false;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      setConnection(false, false, 0);
      haveStatus = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    Cmd cmd;
    if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
      int code = get(cmd.path, kTimeoutMs, nullptr, nullptr);
      LOGF("map: %s -> %d\n", cmd.path, code);
      lastLive = 0;  // show the result now
    }

    uint32_t now = millis();
    if (lastLive == 0 || now - lastLive >= kLiveMs) {
      lastLive = now ? now : 1;
      pollLive();
    }

    // The legend belongs to one visualizer: fetch it again when it changes,
    // like the /remote page does.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool stale = live_.valid && live_.activeIndex != status_.activeIndex;
    bool online = conn_.online;
    xSemaphoreGive(mutex_);
    uint32_t since = now - lastStatus;
    if (online && (!haveStatus || (stale && since >= 2000) || since >= kStatusMs)) {
      lastStatus = now;
      pollStatus();
      haveStatus = true;
    }
  }
}

int MapClient::get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter) {
  WiFiClient tcp;
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the body can be streamed
  http.setConnectTimeout(3000);
  http.setTimeout(timeoutMs);
  if (!http.begin(tcp, host_, port_, path)) return HTTPC_ERROR_CONNECTION_REFUSED;
  int code = http.GET();
  if (code == HTTP_CODE_OK && doc) {
    DeserializationError err =
        deserializeJson(*doc, http.getStream(), DeserializationOption::Filter(*filter));
    if (err) {
      LOGF("map: %s: bad JSON (%s)\n", path, err.c_str());
      code = -100;
    }
  }
  http.end();
  return code;
}

void MapClient::pollLive() {
  static JsonDocument filter(&spiram);
  if (filter.isNull()) {
    filter["now"] = true;
    filter["brightness"] = true;
    JsonObject v = filter["visualizer"].to<JsonObject>();
    v["active"] = true;
    v["name"] = true;
    v["exclusive"] = true;
    v["autoRotate"] = true;
    JsonObject led = filter["leds"].add<JsonObject>();
    led["code"] = true;
    led["pixel"] = true;
    led["virtual"] = true;
    led["flightCategory"] = true;
    led["lightning"] = true;
    led["look"]["effect"] = true;
    led["look"]["colors"] = true;
    filter["locate"] = true;
    filter["strip"] = true;
  }

  JsonDocument doc(&spiram);
  int code = get("/api/live", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    setConnection(true, false, code);
    return;
  }

  MapLive live;
  live.valid = true;
  live.now = doc["now"] | 0.0;
  live.rxMillis = millis();
  live.brightness = doc["brightness"] | 0.0f;
  JsonObjectConst v = doc["visualizer"];
  live.activeIndex = v["active"] | -1;
  live.activeName = v["name"] | "";
  live.exclusive = v["exclusive"] | false;
  live.autoRotate = v["autoRotate"]["enabled"] | false;
  live.autoSeconds = v["autoRotate"]["seconds"] | 0;
  live.autoNext = numberOrNan(v["autoRotate"]["next"]);
  live.strip = !doc["strip"].isNull();
  for (JsonObjectConst led : doc["leds"].as<JsonArrayConst>()) {
    Station s;
    s.code = led["code"] | "";
    s.pixel = led["pixel"] | -1;
    s.isVirtual = led["virtual"] | false;
    s.category = led["flightCategory"] | "";
    s.lightning = led["lightning"] | false;
    s.effect = led["look"]["effect"] | "off";
    for (JsonVariantConst c : led["look"]["colors"].as<JsonArrayConst>()) {
      uint32_t color = parseColor(c);  // a cycle repeats its colors
      if (std::find(s.colors.begin(), s.colors.end(), color) == s.colors.end()) s.colors.push_back(color);
    }
    live.stations.push_back(s);
  }
  if (!doc["locate"].isNull()) {
    live.locatePixel = doc["locate"]["pixel"] | -1;
    live.locateAt = numberOrNan(doc["locate"]["at"]);
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  live_ = std::move(live);
  liveRev_++;
  xSemaphoreGive(mutex_);
  setConnection(true, true, 0);
}

void MapClient::pollStatus() {
  static JsonDocument filter(&spiram);
  if (filter.isNull()) {
    filter["version"] = true;
    filter["ip"] = true;
    JsonObject v = filter["visualizer"].to<JsonObject>();
    v["active"] = true;
    JsonObject item = v["legend"].add<JsonObject>();
    item["label"] = true;
    item["detail"] = true;
    item["effect"] = true;
    item["colors"] = true;
    JsonObject vis = filter["visualizers"].add<JsonObject>();
    vis["id"] = true;
    vis["name"] = true;
    vis["exclusive"] = true;
  }

  JsonDocument doc(&spiram);
  int code = get("/api/status", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    LOGF("map: /api/status -> %d\n", code);
    return;
  }

  MapStatus status;
  status.valid = true;
  status.version = doc["version"] | "";
  status.ip = doc["ip"] | "";
  status.activeIndex = doc["visualizer"]["active"] | -1;
  for (JsonObjectConst item : doc["visualizer"]["legend"].as<JsonArrayConst>()) {
    LegendItem li;
    li.label = item["label"] | "";
    li.detail = item["detail"] | "";
    li.effect = item["effect"] | "solid";
    for (JsonVariantConst c : item["colors"].as<JsonArrayConst>()) li.colors.push_back(parseColor(c));
    status.legend.push_back(li);
  }
  for (JsonObjectConst vis : doc["visualizers"].as<JsonArrayConst>()) {
    Visualizer v;
    v.id = vis["id"] | "";
    v.name = vis["name"] | "";
    v.exclusive = vis["exclusive"] | false;
    status.visualizers.push_back(v);
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  status_ = std::move(status);
  statusRev_++;
  xSemaphoreGive(mutex_);
}

void MapClient::setConnection(bool wifi, bool online, int httpCode) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (conn_.wifi != wifi || conn_.online != online || conn_.httpCode != httpCode) {
    if (conn_.online != online) LOGF("map: %s (%d)\n", online ? "online" : "offline", httpCode);
    conn_.wifi = wifi;
    conn_.online = online;
    conn_.httpCode = httpCode;
    connRev_++;
  }
  xSemaphoreGive(mutex_);
}
