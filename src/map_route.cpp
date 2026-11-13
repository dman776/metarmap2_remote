#include "map_route.h"

namespace map_route {

void Route::set(const std::string& host, const std::string& ip) {
  host_ = host;
  ip_ = ip == host ? "" : ip;
  byIp_ = false;
  fails_ = 0;
  rev_++;
}

bool Route::useIp(uint32_t now) {
  if (byIp_ && now - since_ >= kRetryNameMs) {
    byIp_ = false;
    fails_ = kNameFails - 1;  // one try: should it still not resolve, back to the IP
    rev_++;
  }
  return byIp_;
}

void Route::nameLookedUp(bool ok, const std::string& addr, uint32_t now) {
  if (ok) {
    fails_ = 0;
    if (!addr.empty() && addr != "0.0.0.0" && addr != host_ && addr != ip_) {
      ip_ = addr;  // where the name is now: the IP to fall back to
      rev_++;
    }
    return;
  }
  if (!canFallBack() || ++fails_ < kNameFails) return;
  byIp_ = true;
  since_ = now;
  fails_ = 0;
  rev_++;
}

void Route::ipFailed() {
  if (!byIp_) return;
  byIp_ = false;
  fails_ = 0;
  rev_++;
}

}  // namespace map_route
