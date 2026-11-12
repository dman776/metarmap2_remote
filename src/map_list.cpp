#include "map_list.h"

#include <algorithm>
#include <cctype>

namespace map_list {

namespace {

constexpr size_t kMaxHost = 63;  // the web page's limit for the Chart's address

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

// A host name in one form: lowercase, without the trailing dot of a fully qualified one
std::string normalHost(const std::string& host) {
  std::string h = lower(host);
  while (!h.empty() && h.back() == '.') h.pop_back();
  return h;
}

bool endsWith(const std::string& s, const std::string& end) {
  return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// Letters, digits, - and . only, and not too long: what the web page takes, and what a lookup can find
bool usableHost(const std::string& h) {
  if (h.empty() || h.size() > kMaxHost || h.front() == '.' || h.front() == '-') return false;
  return std::all_of(h.begin(), h.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '.'; });
}

// The order of the list: by name in any case, then by address
bool before(const Row& a, const Row& b) {
  std::string na = lower(a.name), nb = lower(b.name);
  if (na != nb) return na < nb;
  return a.address < b.address;
}

}  // namespace

std::string address(const std::string& host, uint16_t port) {
  return port == 80 ? host : host + ":" + std::to_string(port);
}

std::string versionText(const std::string& version) {
  if (version.empty()) return "";
  return std::isdigit((unsigned char)version[0]) ? "v" + version : version;
}

std::string hostToSave(const Found& f) {
  std::string h = normalHost(f.hostname);
  if (!h.empty() && !endsWith(h, ".local")) h += ".local";
  return usableHost(h) ? h : f.ip;
}

bool isCurrent(const Found& f, const Current& current) {
  if (f.port != current.port) return false;
  std::string saved = normalHost(current.host);
  if (saved.empty()) return false;
  std::string host = hostToSave(f);
  if (!host.empty() && saved == normalHost(host)) return true;
  if (!f.ip.empty() && (saved == f.ip || current.ip == f.ip)) return true;
  return false;
}

std::vector<Found> merge(const std::vector<Found>& found) {
  std::vector<Found> out;
  for (const Found& f : found) {
    std::string host = hostToSave(f);
    if (host.empty()) continue;  // nothing to reach it by
    auto same = std::find_if(out.begin(), out.end(), [&](const Found& m) {
      return m.port == f.port && ((!f.ip.empty() && m.ip == f.ip) || hostToSave(m) == host);
    });
    if (same == out.end()) {
      out.push_back(f);
      continue;
    }
    if (same->ip.empty()) same->ip = f.ip;  // what the other answer knew
    if (same->version.empty()) same->version = f.version;
    if (same->name.empty()) same->name = f.name;
  }
  return out;
}

std::vector<Row> rows(const std::vector<Found>& found, const Current& current, size_t cap) {
  std::vector<Row> out;
  bool haveCurrent = false;
  for (const Found& f : merge(found)) {
    std::string host = hostToSave(f);
    Row r;
    r.name = f.name.empty() ? host : f.name;
    r.address = address(f.ip.empty() ? host : f.ip, f.port);
    r.version = versionText(f.version);
    r.host = host;
    r.port = f.port;
    r.current = !haveCurrent && isCurrent(f, current);  // one row only, should two answers both match
    haveCurrent = haveCurrent || r.current;
    out.push_back(r);
  }
  if (!haveCurrent && !current.host.empty()) {
    Row r;
    r.name = current.name.empty() ? current.host : current.name;
    r.address = address(current.host, current.port);
    r.version = current.version;
    r.host = current.host;
    r.port = current.port;
    r.current = true;
    r.announced = false;
    out.push_back(r);
  }
  std::sort(out.begin(), out.end(), before);
  if (cap == 0) cap = 1;
  if (out.size() > cap) {
    auto cur = std::find_if(out.begin(), out.end(), [](const Row& r) { return r.current; });
    if (cur != out.end() && cur - out.begin() >= (std::ptrdiff_t)cap) {
      Row kept = *cur;
      out.resize(cap - 1);
      out.push_back(kept);  // after the others: it sorts after them all
    } else {
      out.resize(cap);
    }
  }
  return out;
}

}  // namespace map_list
