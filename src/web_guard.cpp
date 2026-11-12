#include "web_guard.h"

#include <algorithm>
#include <cctype>

#include "settings.h"

namespace web_guard {

const char* const kNoHeader =
    "This request needs the header X-Copilot: 1 (the Copilot's web page and tools/shoot.py send it; with curl, "
    "-H 'X-Copilot: 1').";
const char* const kBadHost =
    "This address isn't the Copilot's: use its IP address or http://metarmap-copilot.local/ (a request that names "
    "another host is refused, against DNS rebinding).";
const char* const kDefaultPasswordRemote =
    "Change the web password first: it's still the default (map), and remote control needs your own, so a web page "
    "or another device on the network can't drive the Copilot with it. Set a new one under New web password, then "
    "turn remote control on.";

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

bool digits(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

}  // namespace

bool headerOk(const std::string& value) { return value == "1"; }

bool isIpv4(const std::string& text) {
  int parts = 0;
  size_t at = 0;
  while (true) {
    size_t dot = text.find('.', at);
    std::string part = text.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
    if (!digits(part) || part.size() > 3 || std::stoi(part) > 255) return false;
    parts++;
    if (dot == std::string::npos) break;
    at = dot + 1;
  }
  return parts == 4;
}

bool hostOk(const std::string& host) {
  std::string h = lower(host);
  if (h.empty()) return false;
  if (h.front() == '[') {  // [fe80::1] or [fe80::1]:80
    size_t close = h.find(']');
    if (close == std::string::npos || close == 1) return false;
    std::string rest = h.substr(close + 1);
    if (!rest.empty() && (rest.front() != ':' || !digits(rest.substr(1)))) return false;
    std::string inside = h.substr(1, close - 1);
    return std::all_of(inside.begin(), inside.end(),
                       [](unsigned char c) { return std::isxdigit(c) || c == ':' || c == '.'; });
  }
  size_t colon = h.find(':');
  if (colon != std::string::npos) {
    if (!digits(h.substr(colon + 1))) return false;
    h.erase(colon);
  }
  if (!h.empty() && h.back() == '.') h.pop_back();
  return isIpv4(h) || h == kName || h == std::string(kName) + ".local";
}

bool isDefaultPassword(const std::string& password) { return password == kDefaultWebPassword; }

}  // namespace web_guard
