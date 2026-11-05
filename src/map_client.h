// Talks to the METARMap2 HTTP API from a background FreeRTOS task, so a slow
// request never stalls the touch screen.
//
// The UI sends control paths with command() and copies the latest state out
// with the get*() calls, which return true only when that part changed since
// the revision the caller passes in (and then update it).
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <string>

#include "map_state.h"

class MapClient {
 public:
  void begin(const char* host, uint16_t port);

  // A control endpoint, e.g. "/visualizer/next"; /api/live is polled again
  // right after it.
  void command(const std::string& path);

  bool getStatus(MapStatus& out, uint32_t& rev);
  bool getLive(MapLive& out, uint32_t& rev);
  bool getConnection(Connection& out, uint32_t& rev);

  const char* host() const { return host_; }

 private:
  struct Cmd {
    char path[64];
  };

  static void taskEntry(void* self);
  void run();
  int get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter);
  void pollStatus();
  void pollLive();
  void setConnection(bool wifi, bool online, int httpCode);

  const char* host_ = "";
  uint16_t port_ = 80;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;

  MapStatus status_;
  MapLive live_;
  Connection conn_;
  uint32_t statusRev_ = 0, liveRev_ = 0, connRev_ = 0;
};
