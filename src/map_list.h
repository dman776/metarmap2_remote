// The map picker's list (the globe on the logo's splash): the Charts that announce themselves on the network
// (zeroconf, _metarmap2._tcp: their name, address, port and version), and the one the Copilot uses now, which is
// always there, announced or not (a Chart without network discovery doesn't).
// Plain C++, so it's unit tested on the host (test/test_map_list); map_scan.cpp asks the network, ui.cpp shows it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace map_list {

constexpr size_t kMaxMaps = 8;  // rows in the list: a scan with more keeps the first by name (and the current one)

// One Chart's answer to the scan
struct Found {
  std::string name;      // the instance name: the map's name ("Houston map"), or "Houston map-2" for a taken one
  std::string hostname;  // its mDNS host, "metarmap2-a1b2c3" or "metarmap2-a1b2c3.local(.)"; "" without one
  std::string ip;        // its IPv4 address, "192.168.1.50"; "" without one
  uint16_t port = 80;
  std::string version;  // the TXT record's version, "26.11.10"; "" without one
};

// The map the Copilot uses now: as saved (the web page's Chart address, or a map picked here), and what its
// /api/status said (ip, version; "" until it answers)
struct Current {
  std::string host;  // "metarmap.local", "metarmap2-a1b2c3.local" or "192.168.1.50"
  uint16_t port = 80;
  std::string ip;
  std::string name;     // its /api/status name (a Chart with network discovery); "" for none
  std::string version;  // the splash's "v26.11.10"
};

// A row of the list
struct Row {
  std::string name;
  std::string address;  // as shown: "192.168.1.50", or "192.168.1.50:8080" (not port 80)
  std::string version;  // as shown: "v26.11.10"; "" for none
  std::string host;     // what's saved when it's picked: the mDNS host name (it follows a DHCP change), else the IP
  uint16_t port = 80;
  bool current = false;   // the map in use: marked, and a tap on it does nothing
  bool announced = true;  // false: the current map, which didn't answer the scan
};

// "192.168.1.50", or "192.168.1.50:8080" for a port other than 80
std::string address(const std::string& host, uint16_t port);

// The TXT version as the splash shows a Chart's: "26.11.10" -> "v26.11.10"; one with its v as it is; "" for none
std::string versionText(const std::string& version);

// The host to save for a found Chart: its mDNS host name as "<host>.local" (lowercase, no trailing dot) when it has a
// usable one, else its IP; "" with neither
std::string hostToSave(const Found& f);

// Whether `f` is the map in use: the same port, and the saved host is its host name or its IP, or the IP the
// current map's /api/status gave
bool isCurrent(const Found& f, const Current& current);

// The answers, one per Chart: a Chart heard twice (over IPv4 and IPv6, or two answers) is one, with what each answer
// knew (its IP from one, its version from the other); one with neither an IP nor a host name is left out
std::vector<Found> merge(const std::vector<Found>& found);

// The rows: one per Chart (merge()), sorted by name (any case), at most
// `cap`; the current map always among them (a row of its own if it didn't answer the scan: named by its
// /api/status name, else its saved host, at its saved address)
std::vector<Row> rows(const std::vector<Found>& found, const Current& current, size_t cap = kMaxMaps);

}  // namespace map_list
