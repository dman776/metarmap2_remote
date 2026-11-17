// A missed /api/live answer or two isn't the Chart going away: a busy Chart (a Pi Zero W at full CPU) can stall its
// web server for a few seconds. While the Copilot shows the Chart online, failed polls only count as offline once
// they have failed for kOfflineGraceMs in a row (timed from the first failure of the run; any answer ends the run).
// Until then the last live data stays and the screen doesn't dim. Wi-Fi loss and a map switch don't come here: they
// go offline at once. Plain C++, unit tested on the host (test/test_offline_grace).
#pragma once

#include <cstdint>

namespace offline_grace {

enum class Verdict {
  kFirstMiss,  // the run's first failure: stay online (log it once)
  kRetrying,   // a later failure, still within the grace: stay online
  kOffline,    // offline now (or already offline): set the connection offline
};

class Grace {
 public:
  explicit Grace(uint32_t graceMs) : graceMs_(graceMs) {}

  // A failed poll at `now` (ms, wrapping). `online`: whether the connection shows online now; when it doesn't
  // (starting up, after a switch or Wi-Fi loss, or the grace already over) there's no grace: kOffline at once.
  Verdict failed(uint32_t now, bool online) {
    if (!online) {
      missing_ = false;
      return Verdict::kOffline;
    }
    if (!missing_) {
      missing_ = true;
      since_ = now;
      return Verdict::kFirstMiss;
    }
    if (now - since_ >= graceMs_) {
      missing_ = false;
      return Verdict::kOffline;
    }
    return Verdict::kRetrying;
  }

  // An answer at `now`: the run (if any) is over. How long it lasted, in ms (at least 1), or 0 if there was none.
  uint32_t answered(uint32_t now) {
    if (!missing_) return 0;
    uint32_t ran = now - since_;
    missing_ = false;
    return ran ? ran : 1;
  }

  // Wi-Fi lost or another Chart: forget the run (the connection goes offline at once, not through here).
  void reset() { missing_ = false; }

  bool missing() const { return missing_; }  // inside a run of failures

 private:
  uint32_t graceMs_;
  bool missing_ = false;  // inside a run of failures
  uint32_t since_ = 0;    // its first failure
};

}  // namespace offline_grace
