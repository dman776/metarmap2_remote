#include "map_client.h"

#include <HTTPClient.h>  // its HTTPC_ERROR_* codes, kept for the connection state
#include <WiFi.h>

#include "audio.h"
#include "log.h"
#include "map_parse.h"
#include "net_client.h"
#include "skips.h"
#include "spiram.h"
#include "stream.h"

namespace {

constexpr uint32_t kLiveMs = 1500;         // the /remote page polls every 1 s
constexpr uint32_t kStatusMs = 60 * 1000;  // and /api/status every minute
// How long the Chart has to start answering, and then between parts of the answer. Waited in turns, so a slow
// Chart (a Pi Zero busy sending an ATIS) can't hold core 0 and set off the task watchdog.
constexpr uint32_t kTimeoutMs = 5000;
constexpr uint32_t kConnectMs = 3000;
// What's left of an answer is read and dropped before closing (closing with it unread sends a reset): up to this
// much, and only what has already come, so it never waits.
constexpr size_t kDrainBytes = 4096;
constexpr uint32_t kDrainMs = 5;

// Closes `tcp` after reading and dropping what has already come of the answer (bounded; never waits).
void drainAndStop(WiFiClient& tcp) {
  uint8_t junk[128];
  size_t dropped = 0;
  uint32_t start = millis();
  while (dropped < kDrainBytes && millis() - start < kDrainMs) {
    int n = tcp.read(junk, sizeof(junk));
    if (n <= 0) break;
    dropped += static_cast<size_t>(n);
  }
  tcp.stop();
}

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

uint32_t MapClient::requestSkip(const std::string& id, bool skip) {
  Cmd cmd{};
  strlcpy(cmd.skipId, id.c_str(), sizeof(cmd.skipId));
  cmd.skipOn = skip;
  if (++skipSeq_ == 0) skipSeq_ = 1;  // never 0, wrapping round: 0 is "not queued"
  cmd.skipSeq = skipSeq_;
  cmd.queuedAt = millis();
  if (xQueueSend(queue_, &cmd, 0) == pdTRUE) return cmd.skipSeq;
  LOGF("map: command queue full, dropped %s's skip\n", cmd.skipId);
  return 0;
}

bool MapClient::getSkip(SkipAnswer& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != skipRev_;
  if (changed) {
    out = skip_;
    rev = skipRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
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
      } else if (cmd.skipId[0]) {
        sendSkip(cmd);
        lastLive = 0;  // the marks from the Chart now
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

// A GET to the Chart, its JSON into `doc`. Not HTTPClient: its header lines and deserializeJson() on its stream read
// with Stream's timed reads, which spin on core 0 while the Chart is slow (the watchdog restarts the board at 5 s).
// The status code, or HTTPClient's negative codes for what went wrong.
int MapClient::get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter, bool errorBody) {
  WiFiClient tcp;
  if (!tcp.connect(host_, port_, kConnectMs)) return HTTPC_ERROR_CONNECTION_REFUSED;
  tcp.setTimeout(kConnectMs / 1000);  // sending the request
  // HTTP/1.0: no chunked encoding, so the body can be streamed; the Chart closes it after the answer
  std::string request = std::string("GET ") + path + " HTTP/1.0\r\nHost: " + host_ + ":" + std::to_string(port_) +
                        "\r\nUser-Agent: metarmap-remote\r\nConnection: close\r\n\r\n";
  if (tcp.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()) != request.size()) {
    tcp.stop();
    return HTTPC_ERROR_SEND_HEADER_FAILED;
  }
  ClientSource src(tcp);
  std::string line;
  NetResult got = readLine(src, millis() + timeoutMs, line);
  if (got != NetResult::Ok) {
    drainAndStop(tcp);
    return got == NetResult::TimedOut ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_LOST;
  }
  int code = httpStatusCode(line);
  if (!code) {
    drainAndStop(tcp);
    return HTTPC_ERROR_NO_HTTP_SERVER;
  }
  for (int i = 0; i < 40; i++) {  // the headers: nothing in them is needed
    got = readLine(src, millis() + timeoutMs, line);
    if (got != NetResult::Ok) {
      drainAndStop(tcp);
      return got == NetResult::TimedOut ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_LOST;
    }
    if (line.empty()) break;
  }
  PatientReader body(src, timeoutMs);
  if (doc && code == HTTP_CODE_BAD_REQUEST && errorBody) {
    // a 400's reason, if it has one: no JSON is still a 400
    if (deserializeJson(*doc, body)) doc->clear();
  } else if (code == HTTP_CODE_OK && doc) {
    DeserializationError err =
        filter ? deserializeJson(*doc, body, DeserializationOption::Filter(*filter)) : deserializeJson(*doc, body);
    if (err) {
      LOGF("map: %s: bad JSON (%s)\n", path, err.c_str());
      code = -100;
    }
  }
  drainAndStop(tcp);  // src's read-ahead, if any, is dropped with it
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

void MapClient::sendSkip(const Cmd& cmd) {
  std::string path = std::string("/visualizer/") + cmd.skipId + (cmd.skipOn ? "/skip" : "/unskip");
  SkipAnswer a;
  if (millis() - cmd.queuedAt >= skips::kQueuedMs) {
    // it waited out a Wi-Fi outage or slow requests: done now it would surprise; answered as failed, not sent
    LOGF("map: %s waited %u s, dropped\n", path.c_str(), (unsigned)((millis() - cmd.queuedAt) / 1000));
  } else {
    JsonDocument doc(spiram());  // a few ids: no filter needed
    int code = get(path.c_str(), kTimeoutMs, &doc, nullptr, true);
    a = parseSkip(doc.as<JsonVariantConst>(), code);
    LOGF("map: %s -> %d%s%s\n", path.c_str(), code, a.reason.empty() ? "" : " ", a.reason.c_str());
  }
  a.seq = cmd.skipSeq;
  if (a.id.empty()) a.id = cmd.skipId;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  skip_ = std::move(a);
  skipRev_++;
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

// the least time between two chimes
constexpr uint32_t kChimeEveryMs = 60000;

void MapClient::setConnection(bool wifi, bool online, int httpCode) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (conn_.wifi != wifi || conn_.online != online || conn_.httpCode != httpCode) {
    if (conn_.online != online) LOGF("map: %s (%d)\n", online ? "online" : "offline", httpCode);
    // the chime when it reaches the Chart; not again within a minute, so a flaky Wi-Fi doesn't ring on and on
    static uint32_t chimed = 0;
    if (online && !conn_.online && (chimed == 0 || millis() - chimed >= kChimeEveryMs)) {
      chimed = millis() | 1;
      audio_play(sound_chime, stream_volume());
    }
    conn_.wifi = wifi;
    conn_.online = online;
    conn_.httpCode = httpCode;
    connRev_++;
  }
  xSemaphoreGive(mutex_);
}
