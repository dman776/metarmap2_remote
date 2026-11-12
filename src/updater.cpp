#include "updater.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include <algorithm>

#include "firmware_version.h"
#include "github_roots.h"
#include "log.h"
#include "net_client.h"
#include "spiram.h"

namespace {

constexpr uint32_t kFirstCheckMs = 10 * 1000;           // after Wi-Fi joins
constexpr uint32_t kCheckEveryMs = 6 * 60 * 60 * 1000;  // then
constexpr uint32_t kTimeoutMs = 20 * 1000;
constexpr const char* kUserAgent = "metarmap-remote";  // GitHub's API wants one

std::string apiUrl(const char* path) { return std::string("https://api.github.com/repos/") + update::kRepo + path; }

void setUp(HTTPClient& http) {
  http.useHTTP10(true);  // no chunked encoding: the body can be streamed
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  http.setUserAgent(kUserAgent);
}

std::string failure(int code) {
  if (code == 403 || code == 429) return "GitHub is limiting requests; try later";
  if (code < 0) return std::string("can't reach GitHub (") + HTTPClient::errorToString(code).c_str() + ")";
  return "GitHub answered " + std::to_string(code);
}

int getJson(const std::string& url, JsonDocument& doc, JsonDocument& filter) {
  WiFiClientSecure tls;
  tls.setCACert(kGithubRoots);
  HTTPClient http;
  setUp(http);
  if (!http.begin(tls, url.c_str())) return HTTPC_ERROR_CONNECTION_REFUSED;
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    // in turns: deserializeJson() on the stream itself would spin on core 0 if GitHub stalled (the watchdog's 5 s)
    ClientSource src(http.getStream());
    PatientReader body(src, kTimeoutMs);
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
      LOGF("update: %s: bad JSON (%s)\n", url.c_str(), err.c_str());
      code = -100;
    }
  }
  http.end();
  return code;
}

std::string hex(const uint8_t* bytes, size_t n) {
  static const char digits[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += digits[bytes[i] >> 4];
    s += digits[bytes[i] & 15];
  }
  return s;
}

}  // namespace

void Updater::begin(Settings& settings) {
  settings_ = &settings;
  state_.channel = update::channelFromName(settings.updateChannel);
  state_.branch = settings.updateBranch;
  state_.release = settings.updateRelease;
  queue_ = xQueueCreate(4, sizeof(Cmd));
  mutex_ = xSemaphoreCreateMutex();
  // Core 0 beside Wi-Fi; TLS needs a deep stack.
  xTaskCreatePinnedToCore(taskEntry, "updater", 16 * 1024, this, 1, nullptr, 0);
}

template <typename F>
void Updater::change(F f) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  f(state_);
  rev_++;
  xSemaphoreGive(mutex_);
}

bool Updater::get(UpdateState& out, uint32_t& rev) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  bool changed = rev != rev_;
  if (changed) {
    out = state_;
    rev = rev_;
  }
  xSemaphoreGive(mutex_);
  return changed;
}

void Updater::setChannel(update::Channel channel, const std::string& branch, const std::string& release) {
  change([&](UpdateState& s) {
    s.channel = channel;
    s.branch = branch;
    s.release = release;
    s.checked = s.available = s.installable = false;
    s.age = update::Age::Unknown;
    s.candidate.clear();
    s.error.clear();
  });
  settings_->updateChannel = update::channelName(channel);
  settings_->updateBranch = branch;
  settings_->updateRelease = release;
  settings_save_update(*settings_);
  check();
}

void Updater::choice(std::string& branch, std::string& release) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  branch = state_.branch;
  release = state_.release;
  xSemaphoreGive(mutex_);
}

void Updater::check() {
  Cmd cmd = CHECK;
  xQueueSend(queue_, &cmd, 0);
}

void Updater::install() {
  Cmd cmd = INSTALL;
  xQueueSend(queue_, &cmd, 0);
}

void Updater::taskEntry(void* self) { static_cast<Updater*>(self)->run(); }

void Updater::run() {
  uint32_t wifiSince = 0, lastCheck = 0;
  bool checkedOnce = false;
  for (;;) {
    Cmd cmd;
    bool asked = xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE;
    if (WiFi.status() != WL_CONNECTED) {
      wifiSince = 0;
      if (asked) change([](UpdateState& s) { s.error = "no Wi-Fi"; });
      continue;
    }
    if (!wifiSince) wifiSince = millis() | 1;

    if (asked && cmd == INSTALL) {
      doInstall();
    } else if (asked || (!checkedOnce && millis() - wifiSince >= kFirstCheckMs) ||
               (checkedOnce && millis() - lastCheck >= kCheckEveryMs)) {
      doCheck();
      lastCheck = millis();
      checkedOnce = true;
    }
  }
}

void Updater::doCheck() {
  update::Channel channel;
  std::string branch, release;
  change([&](UpdateState& s) {
    s.phase = UpdateState::Checking;
    s.error.clear();
    channel = s.channel;
    branch = s.branch;
    release = s.release;
  });

  // One request answers every channel: the releases (and the ones Pick
  // release offers), and the builds (with the branches that have one).
  JsonDocument filter(spiram()), doc(spiram());
  update::releaseListFilter(filter);
  int code = getJson(apiUrl("/releases?per_page=100"), doc, filter);
  if (code != HTTP_CODE_OK) {
    LOGF("update: check -> %d\n", code);
    change([&](UpdateState& s) {
      s.phase = UpdateState::Idle;
      s.error = failure(code);
    });
    return;
  }
  std::vector<update::Release> releases = update::parseReleases(doc.as<JsonVariantConst>());

  update::Release candidate;
  if (channel == update::Channel::Releases) {
    for (const update::Release& r : releases) {  // the newest release with the firmware file
      std::vector<int> v = update::versionParts(r.tag);
      if (r.prerelease || v.empty() || r.url.empty()) continue;
      if (candidate.tag.empty() || v > update::versionParts(candidate.tag)) candidate = r;
    }
  } else if (channel == update::Channel::Release) {
    candidate = update::findRelease(releases, release);  // exactly that one, older or newer
  } else {
    std::string tag = update::buildTag(channel == update::Channel::Latest ? update::kMainBranch : branch);
    for (const update::Release& r : releases) {
      if (r.tag == tag && !r.url.empty()) candidate = r;
    }
  }
  bool available = update::isUpdate(channel, candidate, FIRMWARE_TAG, FIRMWARE_COMMIT);
  bool installable = update::canInstall(channel, candidate, FIRMWARE_TAG, FIRMWARE_VERSION, FIRMWARE_COMMIT);
  update::Age age = channel == update::Channel::Release && !candidate.tag.empty()
                        ? update::ageOf(candidate.tag, FIRMWARE_TAG, FIRMWARE_VERSION)
                        : update::Age::Unknown;
  const std::string& pick = channel == update::Channel::Branch ? branch : release;
  bool picked = channel == update::Channel::Branch || channel == update::Channel::Release;
  LOGF("update: %s%s%s: %s%s%s%s\n", update::channelName(channel), picked ? " " : "", picked ? pick.c_str() : "",
       candidate.version.empty() ? "none published" : candidate.version.c_str(), available ? " (update)" : "",
       age != update::Age::Unknown ? ", " : "", update::ageText(age));

  xSemaphoreTake(mutex_, portMAX_DELAY);
  // not changed meanwhile
  bool stillWanted = state_.channel == channel && state_.branch == branch && state_.release == release;
  if (stillWanted) {
    candidate_ = candidate;
    state_.checked = true;
    state_.available = available;
    state_.installable = installable;
    state_.age = age;
    state_.candidate = candidate.version;
  }
  state_.branches = update::buildBranches(releases);
  state_.releases = update::releaseTags(releases);
  state_.phase = UpdateState::Idle;
  rev_++;
  xSemaphoreGive(mutex_);
}

void Updater::doInstall() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  update::Release r = candidate_;
  bool ok = state_.installable;  // an update; or on Release, the picked release, older ones too
  xSemaphoreGive(mutex_);
  auto fail = [&](const std::string& why) {
    LOGF("update: install failed: %s\n", why.c_str());
    change([&](UpdateState& s) {
      s.phase = UpdateState::Idle;
      s.error = why;
    });
  };
  if (!ok || r.url.empty()) return fail("nothing to install");

  change([](UpdateState& s) {
    s.phase = UpdateState::Installing;
    s.progress = 0;
    s.error.clear();
  });
  LOGF("update: installing %s from %s\n", r.version.c_str(), r.url.c_str());

  WiFiClientSecure tls;
  tls.setCACert(kGithubRoots);
  HTTPClient http;
  setUp(http);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // to GitHub's file host
  if (!http.begin(tls, r.url.c_str())) return fail("bad download address");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return fail(failure(code));
  }
  size_t size = r.size ? r.size : (size_t)std::max(0, http.getSize());
  if (!size) {
    http.end();
    return fail("unknown file size");
  }
  // Into the spare firmware slot; the running firmware stays until it's done.
  if (!Update.begin(size, U_FLASH)) {
    http.end();
    return fail(std::string("can't start: ") + Update.errorString());
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  static uint8_t buf[4096];
  WiFiClient* stream = http.getStreamPtr();
  size_t done = 0;
  uint32_t lastData = millis();
  int lastPct = -1;
  while (done < size) {
    size_t avail = stream->available();
    if (!avail) {
      if (!http.connected() || millis() - lastData > kTimeoutMs) break;
      delay(5);
      continue;
    }
    int n = stream->readBytes(buf, std::min({avail, sizeof(buf), size - done}));
    if (n <= 0) {
      delay(5);  // never spin on core 0
      continue;
    }
    lastData = millis();
    if (Update.write(buf, n) != (size_t)n) break;
    mbedtls_sha256_update_ret(&sha, buf, n);
    done += n;
    int pct = (int)(done * 100 / size);
    if (pct != lastPct) {
      lastPct = pct;
      change([pct](UpdateState& s) { s.progress = pct; });
    }
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (done != size) {
    Update.abort();
    return fail(Update.hasError() ? std::string("write failed: ") + Update.errorString()
                                  : "download stopped at " + std::to_string(done * 100 / size) + "%");
  }
  if (!r.sha256.empty() && hex(digest, sizeof(digest)) != r.sha256) {
    Update.abort();
    return fail("the download doesn't match GitHub's checksum");
  }
  if (!Update.end(true)) return fail(std::string("can't finish: ") + Update.errorString());

  LOGF("update: installed %s; restarting\n", r.version.c_str());
  change([](UpdateState& s) {
    s.phase = UpdateState::Restarting;
    s.progress = 100;
  });
  delay(1500);  // the screen shows it
  ESP.restart();
}
