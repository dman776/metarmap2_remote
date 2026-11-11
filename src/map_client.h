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

  // Leave a visualizer out of auto-rotate (skip) or put it back (/visualizer/<id>/skip, /unskip; no login), in
  // the background; getSkip() has the answer then (the last one: a newer one replaces it), with the number this
  // returned, and /api/live is polled again right after it. 0 if it couldn't be queued. Every queued one is
  // answered: one still queued after skips::kQueuedMs (a Wi-Fi outage) isn't sent, and its answer is a failure.
  uint32_t requestSkip(const std::string& id, bool skip);
  bool getSkip(SkipAnswer& out, uint32_t& rev);

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
    char atis[8];     // an airport whose ATIS lines to fetch, instead of a command
    char skipId[32];  // a visualizer to skip or unskip (skipOn), instead of a command
    bool skipOn;
    uint32_t skipSeq;   // its number, for its answer
    uint32_t queuedAt;  // millis() when it was queued
  };

  static void taskEntry(void* self);
  void run();
  // GET path; with doc, its JSON (through filter, if any) when it's a 200, or a 400 too with errorBody
  int get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter, bool errorBody = false);
  void pollStatus();
  void pollLive();
  void fetchAtis(const char* code);
  void sendSkip(const Cmd& cmd);
  void setConnection(bool wifi, bool online, int httpCode);

  const char* host_ = "";
  uint16_t port_ = 80;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;

  MapStatus status_;
  MapLive live_;
  AtisTranscript atis_;
  uint32_t atisRev_ = 0;
  SkipAnswer skip_;
  uint32_t skipRev_ = 0;
  uint32_t skipSeq_ = 0;  // the last skip command's number (requestSkip(), on the screen's task only)
  Connection conn_;
  uint32_t statusRev_ = 0, liveRev_ = 0, connRev_ = 0;
};
