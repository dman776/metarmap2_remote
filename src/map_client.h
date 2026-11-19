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
#include <vector>

#include "icon_store.h"
#include "map_route.h"
#include "map_state.h"
#include "offline_grace.h"
#include "timing.h"

class ClientSource;
class WiFiClient;

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

  // A visualizer icon (/api/icon/<name>?v=<rev>: a 20x20 A8 mask), fetched in the background and saved
  // (icon_store.h); getIcons() has the answer then, a failure too. `rev` is the iconRev asked for ("" from a Chart
  // that doesn't send one). false if it couldn't be queued. The screen asks for one at a time (icon_store::Fetcher):
  // the queue is shared with the buttons.
  bool requestIcon(const std::string& name, const std::string& rev);
  // The answers that came since `rev`, moved into `out` (each is given once)
  bool getIcons(std::vector<icon_store::IconDone>& out, uint32_t& rev);

  bool getStatus(MapStatus& out, uint32_t& rev);
  bool getLive(MapLive& out, uint32_t& rev);
  bool getConnection(Connection& out, uint32_t& rev);

  // Whether it has just reached the Chart and the chime is due (not again within a minute); once. The answer that
  // reached it is already in getLive(): take this first, then the live data, then chime if quiet hours allow.
  bool takeChime();

  // The Chart's address as given to begin() or setMap() (the newest, should a switch still be waiting)
  std::string host();

  // How the Chart is reached, if that changed since `rev`: a switch, the fallback to its IP and back, or a new IP
  // its name resolved to (the UI saves that, and points spoken METARs at it)
  bool getRoute(MapRoute& out, uint32_t& rev);

 private:
  struct Cmd {
    char path[64];
    char atis[8];                             // an airport whose ATIS lines to fetch, instead of a command
    char skipId[32];                          // a visualizer to skip or unskip (skipOn), instead of a command
    char icon[icon_store::kNameMax + 1];      // a visualizer icon to fetch, instead of a command
    char iconRev[icon_store::kRevChars + 1];  // its iconRev ("" for none)
    bool skipOn;
    uint32_t skipSeq;   // its number, for its answer
    uint32_t queuedAt;  // millis() when it was queued
  };

  static void taskEntry(void* self);
  void run();
  // GET path; with doc, its JSON (through filter, if any) when it's a 200, or a 400 too with errorBody
  int get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter, bool errorBody = false);
  // GET path, a binary answer of exactly `expected` bytes into `out`, all of it within `timeoutMs`: true for a 200
  // that says (Content-Length) and gives that many. `code`: the status, or a negative HTTPClient code; `etag` its
  // ETag header ("" without one)
  bool getBinary(const char* path, uint32_t timeoutMs, uint8_t* out, size_t expected, int& code, std::string& etag);
  // What get() and getBinary() share: connects (by name or IP), sends the GET and reads the status line and headers,
  // leaving `src` at the body. The status code, or a negative HTTPClient code with `tcp` closed. `length`: the
  // Content-Length (-1 without one), `etag` the ETag, if asked for.
  int request(const char* path, uint32_t timeoutMs, WiFiClient& tcp, ClientSource& src, long* length = nullptr,
              std::string* etag = nullptr);
  bool pollStatus();  // true once it has the Chart's /api/status
  void pollLive();
  void fetchAtis(const char* code);
  void fetchIcon(const char* name, const char* rev);
  void sendSkip(const Cmd& cmd);
  void setConnection(bool wifi, bool online, int httpCode);  // offline clears conn_.missing
  void setMissing(bool missing);  // conn_.missing (online only): in the grace after a missed /api/live answer
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
  std::vector<icon_store::IconDone> icons_;  // answers the screen hasn't taken yet
  uint32_t iconsRev_ = 0;
  SkipAnswer skip_;
  uint32_t skipRev_ = 0;
  uint32_t skipSeq_ = 0;  // the last skip command's number (requestSkip(), on the screen's task only)
  Connection conn_;
  bool chimeDue_ = false;                        // setConnection() reached the Chart: takeChime()
  offline_grace::Grace grace_{kOfflineGraceMs};  // failed /api/live polls while online (the task's only)
  uint32_t statusRev_ = 0, liveRev_ = 0, connRev_ = 0;
};
