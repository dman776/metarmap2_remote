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

#include "map_route.h"
#include "map_state.h"

// How the Chart is reached now (map_route.h): its saved address, the IP it was last reached at, and whether requests
// go to that IP (the name doesn't resolve)
struct MapRoute {
  std::string host;
  std::string ip;  // "" until known
  uint16_t port = 80;
  bool byIp = false;
  // What spoken METARs and ATIS should use: "http://<host or IP>:<port>"
  std::string baseUrl() const { return "http://" + (byIp ? ip : host) + ":" + std::to_string(port); }
};

class MapClient {
 public:
  // `ip`: the IP the Chart was last reached at ("" for none), for when its name doesn't resolve (map_route.h)
  void begin(const char* host, uint16_t port, const char* ip);

  // Talks to another Chart from now on (the map picker), without a restart: the request under way finishes and its
  // answer is dropped, the connection shows offline at once (the splash's Reconnect), commands still queued for the
  // old Chart are dropped, and the next polls are the new Chart's, its /api/status first thing. What the old Chart
  // said (its status and live state) is cleared at once, so its visualizers and legend don't stay on screen. The
  // host and IP are copied (at most 63 and 15 characters).
  void setMap(const char* host, uint16_t port, const char* ip);

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

  // The Chart's address as given to begin() or setMap() (the newest, should a switch still be waiting)
  std::string host();

  // How the Chart is reached, if that changed since `rev`: a switch, the fallback to its IP and back, or a new IP
  // its name resolved to (the UI saves that, and points spoken METARs at it)
  bool getRoute(MapRoute& out, uint32_t& rev);

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
  bool pollStatus();  // true once it has the Chart's /api/status
  void pollLive();
  void fetchAtis(const char* code);
  void sendSkip(const Cmd& cmd);
  void setConnection(bool wifi, bool online, int httpCode);
  // A switch setMap() asked for: applied (true) between requests, on the task, which then starts over
  // While one waits (pending_), an answer that comes is the old Chart's: dropped, and not "online"
  bool applySwitch();

  // Publishes route_ (under the mutex) when it changed
  void publishRoute();

  // route_ and port_ are the task's (begin() sets them before it starts); a switch waits in pending* under the mutex
  map_route::Route route_;
  uint16_t port_ = 80;
  uint32_t routeSeen_ = 0;  // route_.rev() when it was last published
  char pendingHost_[64] = "";
  char pendingIp_[16] = "";
  uint16_t pendingPort_ = 80;
  bool pending_ = false;
  MapRoute routeOut_;  // for getRoute(), under the mutex
  uint32_t routeRev_ = 0;
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
