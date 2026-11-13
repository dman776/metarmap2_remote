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
  std::string ip;       // its IPv4 address, saved with it: MapClient falls back to it should the name stop resolving
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
// knew (its IP from one, its version from the other); one with neither an IP nor a host name, or with port 0 (an
// answer without its service record), is left out
std::vector<Found> merge(const std::vector<Found>& found);

// A found Chart's row (not the current one: rows() marks that)
Row toRow(const Found& f);

// The rows: one per Chart (merge()), sorted by name (any case), at most
// `cap`; the current map always among them (a row of its own if it didn't answer the scan: named by its
// /api/status name, else its saved host, at its saved address)
std::vector<Row> rows(const std::vector<Found>& found, const Current& current, size_t cap = kMaxMaps);

// ---- names on the screen ----

// A name as the build's fonts can draw it (Montserrat, ASCII only): UTF-8 accented Latin letters lose their accent
// (Zurich for Zürich), curly quotes, dashes and the ellipsis become their ASCII forms, control characters a space,
// and anything else (or a broken UTF-8 byte) a '?'
std::string plainText(const std::string& s);

// The map's name for the splash's own line under "Connecting to the Chart...": the Chart just picked; else the one
// in use by its /api/status name, the name saved when it was picked, or its saved address (host, with :port if not
// 80). As plainText(); the screen shortens it with dots only if it's wider than the screen.
std::string splashName(const std::string& switchingTo, const std::string& statusName, const std::string& savedName,
                       const std::string& host, uint16_t port);

// ---- the Chart address on the web and setup pages ----

constexpr const char* kDefaultHost = "metarmap.local";  // the installer's usual hostname

// The address MapClient talks to: the saved one, or kDefaultHost while none is saved (left empty: the Copilot looks
// for a map itself, and meanwhile tries the usual one)
std::string hostToUse(const std::string& saved);

// The Chart address and port as typed on a page: both may be empty. An empty address is "none: the Copilot finds the
// map" (its port is then ignored); an empty port is 80.
struct AddressForm {
  bool ok = false;
  std::string host;  // trimmed; "" for none
  uint16_t port = 80;
  std::string error;  // why it isn't ok, for the page
};
AddressForm parseAddress(const std::string& host, const std::string& port);

// Whether a map is chosen after a page saved the address: an empty one is never (the Copilot looks for one at its next
// start); otherwise it stays chosen, and becomes chosen on the settings page (its Save is the Chart address too) or
// when the address was changed from the one shown (the setup page: a first start's address left as it is isn't a
// choice)
bool chosenAfterSave(bool wasChosen, bool settingsPage, bool changed, bool emptyHost);

// ---- the first start: a Copilot that has never had a map picked or typed ----

// Whether the map has been chosen, from what's saved: the flag (map_chosen) when it's there; else whether a map
// address is (firmware from before the flag: an existing Copilot, its map left as it is)
bool mapChosen(bool hasFlag, bool flag, bool hasHost);

constexpr int kAutoScans = 4;  // scans at most on a first start while the map doesn't answer (one each kAutoScanMs)
constexpr uint32_t kAutoScanMs = 20000;
constexpr uint32_t kAutoFirstMs = 2000;  // the first, this long after Wi-Fi is up (the network's mDNS starts first)

enum class AutoStep {
  Nothing,     // leave it: a map was chosen, the one in use answers, the picker is open, or the scans are used up
  Pick,        // exactly one Chart answered: use it (pick)
  OpenPicker,  // two or more: the map picker opens, for a tap on one
  Rescan,      // none (or the scan failed): scan again in kAutoScanMs
};

struct AutoInput {
  bool chosen = false;      // mapChosen()
  bool answering = false;   // the map in use answers (it's left as it is)
  bool pickerOpen = false;  // the map picker is on screen: the user picks
  bool scanFailed = false;  // the scan couldn't ask (not on the network yet)
  int scans = 1;            // scans done so far, this one included
};

struct AutoDecision {
  AutoStep step = AutoStep::Nothing;
  Row pick;  // with Pick
};

// What a first start does with a scan's answers (`found`, as the scan gave them: merged here)
AutoDecision autoPick(const std::vector<Found>& found, const AutoInput& in);

}  // namespace map_list
