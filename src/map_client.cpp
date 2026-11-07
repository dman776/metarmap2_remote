#include "map_client.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "log.h"
#include "map_parse.h"
#include "spiram.h"

namespace {

constexpr uint32_t kLiveMs = 1500;         // the /remote page polls every 1 s
constexpr uint32_t kStatusMs = 60 * 1000;  // and /api/status every minute
constexpr uint32_t kTimeoutMs = 5000;

}  // namespace

void MapClient::begin(const char* host, uint16_t port) {
  host_ = host;
  port_ = port;
  queue_ = xQueueCreate(8, sizeof(Cmd));
  mutex_ = xSemaphoreCreateMutex();
  // Core 0, beside Wi-Fi; LVGL runs in the Arduino loop on core 1.
  xTaskCreatePinnedToCore(taskEntry, "map_client", 12 * 1024, this, 1, nullptr, 0);
}

void MapClient::command(const std::string& path) {
  Cmd cmd{};
  strlcpy(cmd.path, path.c_str(), sizeof(cmd.path));
  if (xQueueSend(queue_, &cmd, 0) != pdTRUE) LOGF("map: command queue full, dropped %s\n", cmd.path);
}

void MapClient::requestAtis(const std::string& code) {
  Cmd cmd{};
  strlcpy(cmd.atis, code.c_str(), sizeof(cmd.atis));
  if (xQueueSend(queue_, &cmd, 0) != pdTRUE) LOGF("map: command queue full, dropped %s's ATIS\n", cmd.atis);
}

bool MapClient::getAtis(AtisTranscript& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != atisRev_;
  if (changed) {
    out = atis_;
    rev = atisRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
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

    Cmd cmd{};
    if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (cmd.atis[0]) {
        fetchAtis(cmd.atis);
      } else {
        int code = get(cmd.path, kTimeoutMs, nullptr, nullptr);
        LOGF("map: %s -> %d\n", cmd.path, code);
        lastLive = 0;  // show the result now
      }
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
    DeserializationError err = deserializeJson(*doc, http.getStream(), DeserializationOption::Filter(*filter));
    if (err) {
      LOGF("map: %s: bad JSON (%s)\n", path, err.c_str());
      code = -100;
    }
  }
  http.end();
  return code;
}

void MapClient::pollLive() {
  static JsonDocument filter(spiram());
  if (filter.isNull()) liveFilter(filter);

  JsonDocument doc(spiram());
  int code = get("/api/live", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    setConnection(true, false, code);
    return;
  }

  MapLive live = parseLive(doc.as<JsonVariantConst>());
  live.rxMillis = millis();

  xSemaphoreTake(mutex_, portMAX_DELAY);
  live_ = std::move(live);
  liveRev_++;
  xSemaphoreGive(mutex_);
  setConnection(true, true, 0);
}

void MapClient::fetchAtis(const char* code) {
  static JsonDocument filter(spiram());
  if (filter.isNull()) atisFilter(filter);

  JsonDocument doc(spiram());
  std::string path = std::string("/api/atis/") + code + "/lines";
  // the map may ask atis.info first (a few seconds at most): longer than a poll
  int status = get(path.c_str(), 15000, &doc, &filter);
  AtisTranscript t;
  if (status == HTTP_CODE_OK)
    t = parseAtis(doc.as<JsonVariantConst>());
  else
    LOGF("map: %s -> %d\n", path.c_str(), status);
  t.code = code;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  atis_ = std::move(t);
  atisRev_++;
  xSemaphoreGive(mutex_);
}

void MapClient::pollStatus() {
  static JsonDocument filter(spiram());
  if (filter.isNull()) statusFilter(filter);

  JsonDocument doc(spiram());
  int code = get("/api/status", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    LOGF("map: /api/status -> %d\n", code);
    return;
  }

  MapStatus status = parseStatus(doc.as<JsonVariantConst>());

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
