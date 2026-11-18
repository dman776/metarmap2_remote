#include "map_client.h"

#include <HTTPClient.h>  // its HTTPC_ERROR_* codes, kept for the connection state
#include <WiFi.h>

#include "audio.h"
#include "icon_store.h"
#include "log.h"
#include "map_parse.h"
#include "net_client.h"
#include "skips.h"
#include "spiram.h"
#include "stream.h"

namespace {

constexpr uint32_t kLiveMs = 1500;         // the /remote page polls every 1 s
constexpr uint32_t kStatusMs = 60 * 1000;  // and /api/status every minute
constexpr uint32_t kStatusRetryMs = 5000;  // until the first one answers (after a switch, or Wi-Fi back): this often
constexpr int kWrongLength = -101;         // getBinary(): a 200 of another length than asked for, or cut short
constexpr int kNotSent = -102;             // not sent: the Chart is in the grace after a missed /api/live answer
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

void MapClient::begin(const char* host, uint16_t port, const char* ip) {
  strlcpy(pendingHost_, host, sizeof(pendingHost_));
  strlcpy(pendingIp_, ip, sizeof(pendingIp_));
  route_.set(pendingHost_, pendingIp_);
  port_ = pendingPort_ = port;
  queue_ = xQueueCreate(8, sizeof(Cmd));
  mutex_ = xSemaphoreCreateMutex();
  publishRoute();
  // Core 0, beside Wi-Fi; LVGL runs in the Arduino loop on core 1.
  xTaskCreatePinnedToCore(taskEntry, "map_client", 12 * 1024, this, 1, nullptr, 0);
}

void MapClient::setMap(const char* host, uint16_t port, const char* ip) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(pendingHost_, host, sizeof(pendingHost_));
  strlcpy(pendingIp_, ip, sizeof(pendingIp_));
  pendingPort_ = port;
  pending_ = true;
  bool wifi = conn_.wifi;
  // What the old Chart said goes now: its visualizers and legend aren't the new one's
  status_ = MapStatus();
  statusRev_++;
  live_ = MapLive();
  liveRev_++;
  icons_.clear();  // an icon the screen hasn't taken: asked of the old Chart
  xSemaphoreGive(mutex_);
  setConnection(wifi, false, 0);  // offline until the new Chart answers
  LOGF("map: switching to %s:%u (%s)\n", host, (unsigned)port, *ip ? ip : "no IP");
}

bool MapClient::getRoute(MapRoute& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != routeRev_;
  if (changed) {
    out = routeOut_;
    rev = routeRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
}

void MapClient::publishRoute() {
  if (route_.rev() == routeSeen_) return;
  routeSeen_ = route_.rev();
  if (route_.byIp()) {
    LOGF("map: %s doesn't resolve: by its IP, %s\n", route_.host().c_str(), route_.ip().c_str());
  } else {
    LOGF("map: by name, %s%s%s\n", route_.host().c_str(), route_.ip().empty() ? "" : "; its IP ", route_.ip().c_str());
  }
  MapRoute r;
  r.host = route_.host();
  r.ip = route_.ip();
  r.port = port_;
  r.byIp = route_.byIp();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  routeOut_ = r;
  routeRev_++;
  xSemaphoreGive(mutex_);
}

std::string MapClient::host() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::string h = pendingHost_;
  xSemaphoreGive(mutex_);
  return h;
}

bool MapClient::applySwitch() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool p = pending_;
  if (p) {
    route_.set(pendingHost_, pendingIp_);
    port_ = pendingPort_;
    // Commands still queued were for the old Chart (the splash covers the panels till the new one answers): dropped.
    // A skip's mark is undone when its answer doesn't come (skips::kQueuedMs).
    xQueueReset(queue_);
    // and anything the old Chart answered since setMap() cleared it
    status_ = MapStatus();
    statusRev_++;
    live_ = MapLive();
    liveRev_++;
    pending_ = false;
  }
  xSemaphoreGive(mutex_);
  if (p) publishRoute();
  return p;
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

bool MapClient::requestIcon(const std::string& name, const std::string& rev) {
  if (!icon_store::validName(name)) return false;  // it goes into a URL and a file name
  Cmd cmd{};
  strlcpy(cmd.icon, name.c_str(), sizeof(cmd.icon));
  if (icon_store::validRev(rev)) strlcpy(cmd.iconRev, rev.c_str(), sizeof(cmd.iconRev));
  if (xQueueSend(queue_, &cmd, 0) == pdTRUE) return true;
  LOGF("map: command queue full, dropped icon %s\n", cmd.icon);
  return false;
}

bool MapClient::getIcons(std::vector<icon_store::IconDone>& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != iconsRev_;
  if (changed) {
    out = std::move(icons_);
    icons_.clear();
    rev = iconsRev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
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
  bool haveStatus = false, statusAsked = false;
  for (;;) {
    if (applySwitch()) {  // another Chart: everything from it, the status first
      LOGF("map: now %s:%u\n", route_.host().c_str(), (unsigned)port_);
      lastLive = 0;
      haveStatus = statusAsked = false;
      grace_.reset();
    }
    if (WiFi.status() != WL_CONNECTED) {
      setConnection(false, false, 0);  // at once: no grace without Wi-Fi
      grace_.reset();
      haveStatus = statusAsked = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    Cmd cmd{};
    if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
      // In the grace after a missed /api/live answer a command, skip or icon is answered as failed, not sent: a
      // stalled Chart would hold the task for its whole timeout (and the live polls with it). An ATIS transcript is
      // sent (fetchAtis())
      if (cmd.atis[0]) {
        fetchAtis(cmd.atis);
      } else if (cmd.icon[0]) {
        fetchIcon(cmd.icon, cmd.iconRev);
      } else if (cmd.skipId[0]) {
        sendSkip(cmd);
        lastLive = 0;  // the marks from the Chart now
      } else if (grace_.missing()) {
        LOGF("map: %s not sent: the Chart isn't answering\n", cmd.path);
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
    // Till the first answer, every kStatusRetryMs (a failed one doesn't wait out kStatusMs: the panels need it)
    bool due = haveStatus ? (stale && since >= 2000) || since >= kStatusMs : !statusAsked || since >= kStatusRetryMs;
    if (online && !grace_.missing() && due) {  // not in the grace: a stalled Chart would hold it 5 s
      lastStatus = now;
      statusAsked = true;
      // a failed poll keeps the one it had, if any: only a switch or Wi-Fi loss starts over
      haveStatus = pollStatus() || haveStatus;
    }
  }
}

int MapClient::request(const char* path, uint32_t timeoutMs, WiFiClient& tcp, ClientSource& src, long* length,
                       std::string* etag) {
  if (length) *length = -1;
  if (etag) etag->clear();
  // By the name, or by the IP it was last at while the name doesn't resolve (map_route.h)
  bool byIp = route_.useIp(millis());
  IPAddress addr;
  bool found = byIp ? addr.fromString(route_.ip().c_str()) : WiFi.hostByName(route_.host().c_str(), addr) == 1;
  if (!byIp) route_.nameLookedUp(found, found ? addr.toString().c_str() : "", millis());
  if (!found) {
    publishRoute();
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  if (!tcp.connect(addr, port_, kConnectMs)) {
    if (byIp) route_.ipFailed();  // the IP doesn't answer either: the name again (the Chart may have moved)
    publishRoute();
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  publishRoute();
  tcp.setTimeout(kConnectMs / 1000);  // sending the request
  // HTTP/1.0: no chunked encoding, so the body can be streamed; the Chart closes it after the answer
  std::string request = std::string("GET ") + path + " HTTP/1.0\r\nHost: " + route_.host() + ":" +
                        std::to_string(port_) + "\r\nUser-Agent: metarmap-remote\r\nConnection: close\r\n\r\n";
  if (tcp.write(reinterpret_cast<const uint8_t*>(request.data()), request.size()) != request.size()) {
    tcp.stop();
    return HTTPC_ERROR_SEND_HEADER_FAILED;
  }
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
  std::string value;
  for (int i = 0; i < 40; i++) {  // the headers: only the length and ETag, and only when asked for
    got = readLine(src, millis() + timeoutMs, line);
    if (got != NetResult::Ok) {
      drainAndStop(tcp);
      return got == NetResult::TimedOut ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_LOST;
    }
    if (line.empty()) break;
    if (length && httpHeader(line, "content-length", value)) *length = value.empty() ? -1 : atol(value.c_str());
    if (etag && httpHeader(line, "etag", value)) *etag = value;
  }
  return code;
}

// A GET to the Chart, its JSON into `doc`. Not HTTPClient: its header lines and deserializeJson() on its stream read
// with Stream's timed reads, which spin on core 0 while the Chart is slow (the watchdog restarts the board at 5 s).
// The status code, or HTTPClient's negative codes for what went wrong.
int MapClient::get(const char* path, uint32_t timeoutMs, JsonDocument* doc, JsonDocument* filter, bool errorBody) {
  WiFiClient tcp;
  ClientSource src(tcp);
  int code = request(path, timeoutMs, tcp, src);
  if (code < 0) return code;
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

bool MapClient::getBinary(const char* path, uint32_t timeoutMs, uint8_t* out, size_t expected, int& code,
                          std::string& etag) {
  uint32_t deadline = millis() + timeoutMs;  // all of it, not per part: an icon is 400 bytes
  WiFiClient tcp;
  ClientSource src(tcp);
  long length = -1;
  code = request(path, timeoutMs, tcp, src, &length, &etag);
  if (code < 0) return false;
  if (code != HTTP_CODE_OK || length != (long)expected) {
    if (code == HTTP_CODE_OK) code = kWrongLength;
    drainAndStop(tcp);
    return false;
  }
  size_t n = 0;
  while (n < expected) {
    int b = readByte(src, deadline);
    if (b < 0) break;
    out[n++] = (uint8_t)b;
  }
  drainAndStop(tcp);
  if (n != expected) {
    code = kWrongLength;  // cut short (a timeout, or the Wi-Fi went)
    return false;
  }
  return true;
}

void MapClient::pollLive() {
  static JsonDocument filter(spiram());
  if (filter.isNull()) liveFilter(filter);

  JsonDocument doc(spiram());
  int code = get("/api/live", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    // Online, a miss isn't offline yet: the last live data stays (offline_grace.h). Offline already (starting
    // up, a switch, Wi-Fi back), it is at once, with the code.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool online = conn_.online;
    xSemaphoreGive(mutex_);
    switch (grace_.failed(millis(), online)) {
      case offline_grace::Verdict::kFirstMiss:
        LOGF("map: poll failed (%d), retrying\n", code);
        setMissing(true);
        return;
      case offline_grace::Verdict::kRetrying:
        return;
      case offline_grace::Verdict::kOffline:
        break;
    }
    setConnection(true, false, code);
    return;
  }

  MapLive live = parseLive(doc.as<JsonVariantConst>());
  live.rxMillis = millis();

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (pending_) {  // the old Chart's: the switch comes first
    xSemaphoreGive(mutex_);
    return;
  }
  uint32_t rx = live.rxMillis;
  live_ = std::move(live);
  liveRev_++;
  xSemaphoreGive(mutex_);
  // A miss within the grace was never offline, so this isn't "online" again: just how long it didn't answer
  if (uint32_t ran = grace_.answered(rx)) {
    LOGF("map: answering again after %u ms\n", (unsigned)ran);
    setMissing(false);
  }
  setConnection(true, true, 0);
}

void MapClient::fetchAtis(const char* code) {
  static JsonDocument filter(spiram());
  if (filter.isNull()) atisFilter(filter);

  JsonDocument doc(spiram());
  std::string path = std::string("/api/atis/") + code + "/lines";
  // the map may ask atis.info first (a few seconds at most): longer than a poll
  // Sent in the grace after a missed /api/live answer too: the audio (stream_speak) is already asking the Chart, and
  // a transcript not asked for now isn't asked again
  int status = get(path.c_str(), 15000, &doc, &filter);
  AtisTranscript t;
  if (status == HTTP_CODE_OK)
    t = parseAtis(doc.as<JsonVariantConst>());
  else
    LOGF("map: %s -> %d\n", path.c_str(), status);
  t.code = code;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (pending_) {  // asked of the old Chart: the switch comes first
    xSemaphoreGive(mutex_);
    return;
  }
  atis_ = std::move(t);
  atisRev_++;
  xSemaphoreGive(mutex_);
}

void MapClient::fetchIcon(const char* name, const char* rev) {
  icon_store::IconDone d;
  d.name = name;
  d.rev = d.askedRev = rev;
  std::string path = std::string("/api/icon/") + name + (*rev ? std::string("?v=") + rev : std::string());
  static uint8_t mask[icon_store::kMaskBytes];  // this task's only
  int code = 0;
  std::string etag;
  if (grace_.missing()) {
    code = kNotSent;
  } else {
    d.ok = getBinary(path.c_str(), kTimeoutMs, mask, sizeof(mask), code, etag);
  }
  LOGF("map: %s -> %d\n", path.c_str(), code);
  if (d.ok) {
    std::string served = icon_store::etagRev(etag);  // the mask the Chart has now: it may have changed since asked
    if (!served.empty()) d.rev = served;
    d.mask.assign(mask, mask + sizeof(mask));
  } else if (code == HTTP_CODE_NOT_FOUND) {
    d.why = icon_store::Failure::NotFound;
  } else if (code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_SEND_HEADER_FAILED || code == kNotSent) {
    d.why = icon_store::Failure::Connection;  // not the icon's fault
  } else {
    d.why = icon_store::Failure::BadAnswer;  // a timeout, an answer cut short, the wrong length, another status
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool old = pending_;
  xSemaphoreGive(mutex_);
  if (old) return;  // asked of the old Chart: the switch comes first
  if (d.ok) d.saved = icon_store::saveFile(d.name, d.rev, mask);

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (pending_) {
    xSemaphoreGive(mutex_);
    return;
  }
  icons_.push_back(std::move(d));
  iconsRev_++;
  xSemaphoreGive(mutex_);
}

void MapClient::sendSkip(const Cmd& cmd) {
  std::string path = std::string("/visualizer/") + cmd.skipId + (cmd.skipOn ? "/skip" : "/unskip");
  SkipAnswer a;
  if (millis() - cmd.queuedAt >= skips::kQueuedMs) {
    // it waited out a Wi-Fi outage or slow requests: done now it would surprise; answered as failed, not sent
    LOGF("map: %s waited %u s, dropped\n", path.c_str(), (unsigned)((millis() - cmd.queuedAt) / 1000));
  } else if (grace_.missing()) {  // answered as failed (the marks are undone), as one that waited too long
    LOGF("map: %s not sent: the Chart isn't answering\n", path.c_str());
  } else {
    JsonDocument doc(spiram());  // a few ids: no filter needed
    int code = get(path.c_str(), kTimeoutMs, &doc, nullptr, true);
    a = parseSkip(doc.as<JsonVariantConst>(), code);
    LOGF("map: %s -> %d%s%s\n", path.c_str(), code, a.reason.empty() ? "" : " ", a.reason.c_str());
  }
  a.seq = cmd.skipSeq;
  if (a.id.empty()) a.id = cmd.skipId;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (pending_) {  // the old Chart's (its mark is undone when no answer comes)
    xSemaphoreGive(mutex_);
    return;
  }
  skip_ = std::move(a);
  skipRev_++;
  xSemaphoreGive(mutex_);
}

bool MapClient::pollStatus() {
  static JsonDocument filter(spiram());
  if (filter.isNull()) statusFilter(filter);

  JsonDocument doc(spiram());
  int code = get("/api/status", kTimeoutMs, &doc, &filter);
  if (code != HTTP_CODE_OK) {
    LOGF("map: /api/status -> %d\n", code);
    return false;
  }

  MapStatus status = parseStatus(doc.as<JsonVariantConst>());

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (pending_) {  // the old Chart's
    xSemaphoreGive(mutex_);
    return false;
  }
  status_ = std::move(status);
  statusRev_++;
  xSemaphoreGive(mutex_);
  return true;
}

// the least time between two chimes
constexpr uint32_t kChimeEveryMs = 60000;

void MapClient::setConnection(bool wifi, bool online, int httpCode) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (online && pending_) {  // the old Chart answered after setMap(): offline till the new one does
    xSemaphoreGive(mutex_);
    return;
  }
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
  if (!online && conn_.missing) {  // offline: no grace to be in
    conn_.missing = false;
    connRev_++;
  }
  xSemaphoreGive(mutex_);
}

void MapClient::setMissing(bool missing) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (missing && (!conn_.online || pending_)) missing = false;  // only online has a grace
  if (conn_.missing != missing) {
    conn_.missing = missing;
    connRev_++;
  }
  xSemaphoreGive(mutex_);
}
