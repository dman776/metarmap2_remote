// How MapClient reaches the Chart: by its name ("metarmap2-a1b2c3.local", "metarmap.local"), falling back to the IP
// it was last reached at while the name doesn't resolve.
//
// A Chart picked on the map picker is saved as its announced mDNS name, which only resolves while its network
// discovery (zeroconf) runs; its IP is saved too. The name is preferred (it follows a DHCP change); after kNameFails
// lookups of it fail in a row, the saved IP is used; when the IP stops answering, or every kRetryNameMs, the name is
// tried again. A name that resolves to a new IP updates the saved one (rev() changes: MapClient publishes it, the UI
// saves it), so a typed address (metarmap.local) gets the fallback too once it has answered.
//
// Plain C++, so it's unit tested on the host (test/test_map_route); MapClient asks and tells it around each request.
#pragma once

#include <cstdint>
#include <string>

namespace map_route {

constexpr int kNameFails = 3;                     // lookups of the name that fail in a row before the IP is used
constexpr uint32_t kRetryNameMs = 5 * 60 * 1000;  // on the IP, the name is tried again this often

class Route {
 public:
  // A map: its saved address (a name or an IP) and the IP it was last reached at ("" for none). By the name first.
  void set(const std::string& host, const std::string& ip);

  // Before a request: whether to connect to the saved IP (true) or look the name up (false). On the IP for
  // kRetryNameMs, the name is tried once more (one failed lookup goes back to the IP).
  bool useIp(uint32_t now);

  // After looking the name up: whether it resolved, and the IPv4 address it gave
  void nameLookedUp(bool ok, const std::string& addr, uint32_t now);

  // After a connection to the saved IP failed: the name again
  void ipFailed();

  const std::string& host() const { return host_; }
  const std::string& ip() const { return ip_; }
  bool byIp() const { return byIp_; }
  // What a request goes to now: the IP or the name
  const std::string& target() const { return byIp_ ? ip_ : host_; }
  // Changes when target() or ip() does
  uint32_t rev() const { return rev_; }

 private:
  // Whether there's an IP to fall back to: one that isn't the address itself (a typed IP has nothing to fall to)
  bool canFallBack() const { return !ip_.empty() && ip_ != host_; }

  std::string host_;
  std::string ip_;
  bool byIp_ = false;
  int fails_ = 0;
  uint32_t since_ = 0;  // when it went to the IP
  uint32_t rev_ = 0;
};

}  // namespace map_route
