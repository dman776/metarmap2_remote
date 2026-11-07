// Talks to the MetarMap2 HTTP API from a background FreeRTOS task, so a slow
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

  // An airport's ATIS line by line (/api/atis/<code>/lines), fetched in the background; getAtis() has it then
  // (valid false if the map couldn't give it).
  void requestAtis(const std::string& code);
  bool getAtis(AtisTranscript& out, uint32_t& rev);

  bool getStatus(MapStatus& out, uint32_t& rev);
  bool getLive(MapLive& out, uint32_t& rev);
  bool getConnection(Connection& out, uint32_t& rev);

  const char* host() const { return host_; }

 private:
  struct Cmd {
    char path[64];
    char atis[8];  // an airport whose ATIS lines to fetch, instead of a command
  };

  static void taskEntry(void* self);
  void run();
  int get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter);
  void pollStatus();
  void pollLive();
  void fetchAtis(const char* code);
  void setConnection(bool wifi, bool online, int httpCode);

  const char* host_ = "";
  uint16_t port_ = 80;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;

  MapStatus status_;
  MapLive live_;
  AtisTranscript atis_;
  uint32_t atisRev_ = 0;
  Connection conn_;
  uint32_t statusRev_ = 0, liveRev_ = 0, connRev_ = 0;
};
