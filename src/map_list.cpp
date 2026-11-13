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
    if (host.empty() || f.port == 0) continue;  // nothing to reach it by
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

Row toRow(const Found& f) {
  std::string host = hostToSave(f);
  Row r;
  r.name = f.name.empty() ? host : f.name;
  r.address = address(f.ip.empty() ? host : f.ip, f.port);
  r.version = versionText(f.version);
  r.host = host;
  r.ip = f.ip;
  r.port = f.port;
  return r;
}

std::vector<Row> rows(const std::vector<Found>& found, const Current& current, size_t cap) {
  std::vector<Row> out;
  bool haveCurrent = false;
  for (const Found& f : merge(found)) {
    Row r = toRow(f);
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
    r.ip = current.ip;
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

namespace {

// U+00C0 to U+00FF without their accents (x for the multiplication sign, / for division)
constexpr const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

// One code point as ASCII, onto `out`
void plainCodePoint(uint32_t cp, std::string& out) {
  if (cp < 0x20 || cp == 0x7f) {
    out += ' ';
  } else if (cp < 0x80) {
    out += (char)cp;
  } else if (cp == 0xa0) {
    out += ' ';
  } else if (cp >= 0xc0 && cp <= 0xff) {
    out += kLatin1[cp - 0xc0];
  } else if (cp == 0x2018 || cp == 0x2019) {
    out += '\'';
  } else if (cp == 0x201c || cp == 0x201d) {
    out += '"';
  } else if (cp == 0x2013 || cp == 0x2014) {
    out += '-';
  } else if (cp == 0x2026) {
    out += "...";
  } else {
    out += '?';
  }
}

}  // namespace

std::string plainText(const std::string& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    auto b = (unsigned char)s[i];
    size_t len = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : (b & 0xf8) == 0xf0 ? 4 : 0;
    uint32_t cp = len == 1 ? b : len == 2 ? (b & 0x1f) : len == 3 ? (b & 0x0f) : (b & 0x07);
    bool ok = len != 0 && i + len <= s.size();
    for (size_t k = 1; ok && k < len; k++) {
      auto c = (unsigned char)s[i + k];
      ok = (c & 0xc0) == 0x80;
      cp = (cp << 6) | (c & 0x3f);
    }
    if (!ok) {  // a broken sequence: its first byte only, as '?'
      out += '?';
      i++;
      continue;
    }
    plainCodePoint(cp, out);
    i += len;
  }
  return out;
}

std::string splashName(const std::string& switchingTo, const std::string& statusName, const std::string& savedName,
                       const std::string& host, uint16_t port) {
  const std::string& name = !switchingTo.empty() ? switchingTo : !statusName.empty() ? statusName : savedName;
  return plainText(name.empty() ? address(host, port) : name);
}

bool mapChosen(bool hasFlag, bool flag, bool hasHost) { return hasFlag ? flag : hasHost; }

std::string hostToUse(const std::string& saved) { return saved.empty() ? kDefaultHost : saved; }

AddressForm parseAddress(const std::string& host, const std::string& port) {
  AddressForm f;
  size_t from = host.find_first_not_of(" \t");
  size_t to = host.find_last_not_of(" \t");
  f.host = from == std::string::npos ? "" : host.substr(from, to - from + 1);
  if (f.host.empty()) {  // none: found by the Copilot (map_list::autoPick())
    f.ok = true;
    return f;
  }
  if (f.host.size() > kMaxHost || f.host.find_first_of(" /:") != std::string::npos) {
    f.error = "The Chart's address is a name or an IP, like metarmap.local or 192.168.1.50 (or empty, to find it).";
    return f;
  }
  if (port.empty()) {
    f.ok = true;
    return f;
  }
  bool digits =
      port.size() <= 5 && std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); });
  long n = digits ? std::stol(port) : 0;
  if (n < 1 || n > 65535) {
    f.error = "The Chart's port is a number from 1 to 65535 (or empty, for 80).";
    return f;
  }
  f.port = (uint16_t)n;
  f.ok = true;
  return f;
}

bool chosenAfterSave(bool wasChosen, bool settingsPage, bool changed, bool emptyHost) {
  if (emptyHost) return false;
  return wasChosen || settingsPage || changed;
}

AutoDecision autoPick(const std::vector<Found>& found, const AutoInput& in) {
  AutoDecision d;
  if (in.chosen || in.answering || in.pickerOpen) return d;  // never over a map the user set, or one that works
  std::vector<Found> maps = in.scanFailed ? std::vector<Found>() : merge(found);
  if (maps.size() == 1) {
    d.step = AutoStep::Pick;
    d.pick = toRow(maps[0]);
  } else if (maps.size() > 1) {
    d.step = AutoStep::OpenPicker;
  } else if (in.scans < kAutoScans) {
    d.step = AutoStep::Rescan;
  }
  return d;
}

}  // namespace map_list
