// The visualizer icons fetched from the Chart (GET /api/icon/<name>: a 20x20 A8 alpha mask, 400 bytes) and saved on
// the Copilot, so a visualizer newer than the firmware (FAA Delays' hourglass) shows its icon, and a mask the Chart
// changes follows it (its iconRev in /api/status). The ones built in (icons.h) are the fallback.
//
// This part is plain C++, unit tested on the host (test/test_icon_store): the names, the saved file's format, the
// LVGL image it becomes, the backoff after a failed fetch, and which icons to fetch. icon_store_fs.cpp is the board's
// part: the files on LittleFS (the `spiffs` partition), and the pool of images LVGL draws from.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "map_state.h"

namespace icon_store {

constexpr int kSize = 20;                       // pixels a side
constexpr size_t kMaskBytes = kSize * kSize;    // 400: A8, row by row from the top left, 0 clear to 255 solid
constexpr size_t kNameMax = 32;                 // the longest name (^[a-z][a-z0-9_]{0,31}$)
constexpr size_t kRevChars = 8;                 // an iconRev: 8 lowercase hex digits
constexpr uint8_t kMagic = 'I';                 // a saved file: kMagic, kVersion, the rev (8 ASCII hex digits, or
constexpr uint8_t kVersion = 1;                 // 8 zero bytes for one the Chart gave without a rev), the mask
constexpr size_t kHeaderBytes = 2 + kRevChars;  // 10
constexpr size_t kFileBytes = kHeaderBytes + kMaskBytes;  // 410

// Backoff after a failed fetch, per icon: the Chart has no such icon (404), or its answer was cut short or wrong
constexpr uint32_t kNotFoundMs = 10 * 60 * 1000;
constexpr uint32_t kBadAnswerMs = 60 * 1000;

// An icon name from the Chart, checked before it goes into a path or a URL: ^[a-z][a-z0-9_]{0,31}$
bool validName(const std::string& name);
// An iconRev: 8 lowercase hex digits
bool validRev(const std::string& rev);

// A saved file (kFileBytes) into its rev ("" if saved without one) and mask (kMaskBytes); false for a file of the
// wrong size, magic or version, or a rev that isn't one (a corrupt file: deleted, and the icon fetched again).
bool parseIconFile(const uint8_t* data, size_t len, std::string& rev, uint8_t* mask);
// The file for `mask` with `rev` ("" for none, or one that isn't valid) into `out` (kFileBytes)
void buildIconFile(const std::string& rev, const uint8_t* mask, uint8_t* out);

// Fills an LVGL image descriptor (lv_image_dsc_t) for a mask: A8, kSize a side, stride kSize, kMaskBytes from `mask`
// (which LVGL reads every time it draws it: the buffer must stay as it is while a tile shows it). A template, so the
// host tests check it without LVGL; icon_store_fs.cpp passes LV_IMAGE_HEADER_MAGIC and LV_COLOR_FORMAT_A8.
template <class Dsc>
void fillDsc(Dsc& d, const uint8_t* mask, uint32_t magic, uint32_t cfA8) {
  d = Dsc();
  d.header.magic = magic;
  d.header.cf = cfA8;
  d.header.w = kSize;
  d.header.h = kSize;
  d.header.stride = kSize;
  d.data_size = kMaskBytes;
  d.data = mask;
}

// How a fetch failed, for the backoff
enum class Failure {
  NotFound,    // a 404: the Chart has no such icon (kNotFoundMs), or no /api/icon at all (an older Chart)
  BadAnswer,   // a timeout, an answer cut short or too long, another status (kBadAnswerMs)
  Connection,  // the Chart couldn't be reached (Wi-Fi down, a switch): not the icon's fault, not charged to it
  NoRoom,      // it came, but it could be neither saved nor kept (the pool full and the flash too): kNotFoundMs
};

// The icons not to ask for again yet: per name, when it failed and for which rev. A new rev in /api/status (the Chart
// changed the mask) is asked for at once, whatever failed before. clear() on a switch to another Chart.
class Misses {
 public:
  // `rev`: what was asked for ("" from a Chart that doesn't say). A 404 asked without one (the Chart doesn't send
  // iconRev, so it's older than /api/icon) marks the Chart unsupported: nothing more is asked of it.
  void failed(const std::string& name, const std::string& rev, Failure why, uint32_t now);
  // Asking for `name` at `rev` waits for the backoff
  bool blocked(const std::string& name, const std::string& rev, uint32_t now) const;
  bool unsupported() const { return unsupported_; }
  void clear();

 private:
  struct Miss {
    std::string rev;
    uint32_t at = 0;
    uint32_t backoffMs = 0;
  };
  std::map<std::string, Miss> misses_;
  bool unsupported_ = false;
};

// What the Copilot has for an icon name: one saved (fetched before) and its rev ("" if unknown), and one built in
// and its rev
struct Have {
  bool saved = false;
  std::string savedRev;
  bool compiled = false;
  std::string compiledRev;
};
using Lookup = std::function<Have(const std::string& name)>;

struct Want {
  std::string name;
  std::string rev;  // "" from a Chart that doesn't send iconRev
};

// The icons to fetch for the visualizers /api/status lists, in their order (the picker's), each name once:
// - from a Chart that sends iconRev (hasIconRev): each icon with a rev that neither a saved one nor the built-in one
//   is (an icon the Chart has no mask for has no rev: not asked for, the Chart would say 404);
// - from an older Chart: each icon with nothing saved and nothing built in, until a 404 says it has no /api/icon;
// leaving out invalid names and those waiting out a backoff. Exclusive visualizers (Rainbow, Chase) are in: their
// picker tiles show icons too.
std::vector<Want> missingOrStale(const MapStatus& status, const Lookup& have, const Misses& misses, uint32_t now);

// "W/\"7cb23636\"" (an ETag header's value) as the rev it names; "" if it isn't one
std::string etagRev(const std::string& etag);

// A fetch's answer, from MapClient's task to the screen's (MapClient::getIcons())
struct IconDone {
  std::string name;
  std::string rev;       // what came (the answer's ETag), else what was asked for
  std::string askedRev;  // what was asked for ("" from a Chart that doesn't send iconRev)
  bool ok = false;       // a 200 with exactly kMaskBytes: in mask
  Failure why = Failure::BadAnswer;
  bool saved = false;  // written to the flash (a full or unmounted one: not, and it's kept in RAM only)
  std::vector<uint8_t> mask;
};

// Asking for icons, on the screen's task: one at a time (MapClient's queue is 8 deep, and shared with the buttons),
// the next only after the last one's answer, and nothing while the Chart is offline.
class Fetcher {
 public:
  // An answer that doesn't come within this is given up (it can't take longer: 3 s to connect, 5 s to answer)
  static constexpr uint32_t kAnswerMs = 30000;

  // The next icon to ask for, if one may be asked for now: true, with it in `out` (asked() once it's queued)
  bool next(const MapStatus& status, const Lookup& have, bool online, uint32_t now, Want& out);
  void asked(uint32_t now);
  // Its answer: a failure waits out its backoff. `kept`: an icon that came is saved or in the pool (false: NoRoom)
  void answered(const IconDone& done, bool kept, uint32_t now);
  // Another Chart: nothing is waited for, and every icon may be asked for again (the saved files stay)
  void switched();

  bool waiting() const { return waiting_; }
  const Misses& misses() const { return misses_; }

 private:
  Misses misses_;
  bool waiting_ = false;
  uint32_t askedAt_ = 0;
};

// ---- on the board: icon_store_fs.cpp ----------------------------------------------------------------------------
// Saved icons are files on LittleFS, on the `spiffs` partition (nothing else uses it): /ic/<name>.bin (kFileBytes),
// written as /ic/<name>.tmp and renamed, so a power cut leaves the old file or none. Erasing the flash
// (pio run -t erase) clears them; a firmware update (OTA, or a factory image) doesn't touch them.

// Mounts LittleFS (formatting it the first time) and loads the saved icons into the pool, a corrupt file deleted (the
// icon is fetched again). Once, in setup(), before MapClient's task starts. Never fails: without the flash, fetched
// icons are kept in RAM only, till a restart.
void begin();
bool mounted();

// Saves a fetched icon (MapClient's task); false if it couldn't be (no flash, or full): it's kept in RAM only then
bool saveFile(const std::string& name, const std::string& rev, const uint8_t* mask);

// The rest on the screen's (LVGL's) task only.
// A fetched icon into the pool, for vis_icon(name, rev): a new slot, never one a tile may be drawing (LVGL keeps the
// image's pointer). `saved`: it's on the flash too. false if the pool is full: the built-in icon stays.
bool adopt(const std::string& name, const std::string& rev, const uint8_t* mask, bool saved);
// What the Copilot has for `name` (missingOrStale()'s Lookup)
Have have(const std::string& name);

}  // namespace icon_store
