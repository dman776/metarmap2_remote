#include "icon_store.h"

#include <algorithm>
#include <cstring>

namespace icon_store {

bool validName(const std::string& name) {
  if (name.empty() || name.size() > kNameMax) return false;
  if (name[0] < 'a' || name[0] > 'z') return false;
  return std::all_of(name.begin(), name.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool validRev(const std::string& rev) {
  if (rev.size() != kRevChars) return false;
  return std::all_of(rev.begin(), rev.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool parseIconFile(const uint8_t* data, size_t len, std::string& rev, uint8_t* mask) {
  if (!data || len != kFileBytes || data[0] != kMagic || data[1] != kVersion) return false;
  const uint8_t* r = data + 2;
  bool none = std::all_of(r, r + kRevChars, [](uint8_t b) { return b == 0; });
  std::string got = none ? std::string() : std::string(reinterpret_cast<const char*>(r), kRevChars);
  if (!none && !validRev(got)) return false;
  rev = got;
  memcpy(mask, data + kHeaderBytes, kMaskBytes);
  return true;
}

void buildIconFile(const std::string& rev, const uint8_t* mask, uint8_t* out) {
  out[0] = kMagic;
  out[1] = kVersion;
  memset(out + 2, 0, kRevChars);
  if (validRev(rev)) memcpy(out + 2, rev.data(), kRevChars);
  memcpy(out + kHeaderBytes, mask, kMaskBytes);
}

void Misses::failed(const std::string& name, const std::string& rev, Failure why, uint32_t now) {
  if (why == Failure::Connection) return;
  if (why == Failure::NotFound && rev.empty()) unsupported_ = true;
  Miss& m = misses_[name];
  m.rev = rev;
  m.at = now;
  m.backoffMs = why == Failure::BadAnswer ? kBadAnswerMs : kNotFoundMs;
}

bool Misses::blocked(const std::string& name, const std::string& rev, uint32_t now) const {
  auto it = misses_.find(name);
  if (it == misses_.end()) return false;
  const Miss& m = it->second;
  if (m.rev != rev) return false;  // the Chart has another mask for it now
  return now - m.at < m.backoffMs;
}

void Misses::clear() {
  misses_.clear();
  unsupported_ = false;
}

std::vector<Want> missingOrStale(const MapStatus& status, const Lookup& have, const Misses& misses, uint32_t now) {
  std::vector<Want> out;
  if (!status.valid) return out;
  if (!status.hasIconRev && misses.unsupported()) return out;
  std::vector<std::string> seen;
  for (const Visualizer& v : status.visualizers) {
    const std::string& name = v.icon;
    if (!validName(name)) continue;
    if (std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
    seen.push_back(name);
    Have h = have(name);
    std::string rev;
    if (status.hasIconRev) {
      if (!validRev(v.iconRev)) continue;  // the Chart has no mask for it
      if (h.saved && h.savedRev == v.iconRev) continue;
      if (h.compiled && h.compiledRev == v.iconRev) continue;
      rev = v.iconRev;
    } else if (h.saved || h.compiled) {
      continue;  // an older Chart: whatever the Copilot has will do
    }
    if (misses.blocked(name, rev, now)) continue;
    out.push_back({name, rev});
  }
  return out;
}

std::string etagRev(const std::string& etag) {
  std::string v = etag;
  if (v.compare(0, 2, "W/") == 0) v = v.substr(2);
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  return validRev(v) ? v : std::string();
}

bool Fetcher::next(const MapStatus& status, const Lookup& have, bool online, uint32_t now, Want& out) {
  if (waiting_ && now - askedAt_ < kAnswerMs) return false;
  waiting_ = false;  // given up, if it was waiting
  if (!online) return false;
  std::vector<Want> wants = missingOrStale(status, have, misses_, now);
  if (wants.empty()) return false;
  out = wants.front();
  return true;
}

void Fetcher::asked(uint32_t now) {
  waiting_ = true;
  askedAt_ = now;
}

void Fetcher::answered(const IconDone& done, bool kept, uint32_t now) {
  waiting_ = false;
  if (!done.ok) {
    misses_.failed(done.name, done.rev, done.why, now);
  } else if (!kept) {
    misses_.failed(done.name, done.rev, Failure::NoRoom, now);
  } else if (!done.askedRev.empty() && done.rev != done.askedRev) {
    // the Chart changed the mask since its /api/status: not asked again for the old rev till the next one says
    misses_.failed(done.name, done.askedRev, Failure::BadAnswer, now);
  }
}

void Fetcher::switched() {
  misses_.clear();
  waiting_ = false;
}

}  // namespace icon_store
