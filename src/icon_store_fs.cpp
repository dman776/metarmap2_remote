// The board's part of icon_store.h: the saved icons on LittleFS, and the pool of images LVGL draws them from.
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <map>
#include <string>
#include <vector>

#include "icon_store.h"
#include "icons.h"
#include "log.h"

namespace icon_store {

namespace {

constexpr const char* kDir = "/ic";
constexpr size_t kSlots = 32;  // the icons the Copilot can show from the Chart this run (18 visualizers' today)

// One fetched or saved icon as an LVGL image. LVGL keeps the lv_image_dsc_t's pointer (it doesn't copy it), so a
// slot, once used, is never freed or written again: a changed icon takes another one.
struct Slot {
  char name[kNameMax + 1];
  char rev[kRevChars + 1];
  lv_image_dsc_t dsc;
  uint8_t mask[kMaskBytes];
};

bool mounted_ = false;  // set once in begin(), before MapClient's task starts
Slot* pool = nullptr;   // kSlots, in PSRAM (the screen's task only, after begin())
size_t used = 0;
// The newest copy of each icon the Copilot has (saved, or kept in RAM): its rev ("" if unknown)
std::map<std::string, std::string> latest;

std::string path(const std::string& name, const char* ext) { return std::string(kDir) + "/" + name + ext; }

const Slot* newest(const char* name) {
  const Slot* found = nullptr;
  for (size_t i = 0; i < used; i++) {
    if (strcmp(pool[i].name, name) == 0) found = &pool[i];  // later is newer
  }
  return found;
}

bool put(const std::string& name, const std::string& rev, const uint8_t* mask) {
  const Slot* same = newest(name.c_str());
  if (same && rev == same->rev && memcmp(same->mask, mask, kMaskBytes) == 0) return true;  // there already
  if (!pool || used >= kSlots) {
    LOGF("icons: no room for %s, the built-in icon stays\n", name.c_str());
    return false;
  }
  Slot& s = pool[used];
  strlcpy(s.name, name.c_str(), sizeof(s.name));
  strlcpy(s.rev, rev.c_str(), sizeof(s.rev));
  memcpy(s.mask, mask, kMaskBytes);
  fillDsc(s.dsc, s.mask, LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_A8);
  used++;  // only now does vis_icon() see it
  return true;
}

// The saved icons into the pool; a corrupt one (or a .tmp a power cut left) is deleted
void load() {
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  static uint8_t buf[kFileBytes + 1];  // setup() only
  uint8_t mask[kMaskBytes];
  std::vector<std::string> bad;
  int loaded = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    std::string file = f.name();
    std::string full = f.path();
    size_t dot = file.rfind(".bin");
    std::string name = dot == std::string::npos ? std::string() : file.substr(0, dot);
    std::string rev;
    size_t n = f.read(buf, sizeof(buf));
    f.close();
    if (dot == std::string::npos || dot + 4 != file.size() || !validName(name) || !parseIconFile(buf, n, rev, mask)) {
      bad.push_back(full);
      continue;
    }
    latest[name] = rev;
    if (put(name, rev, mask)) loaded++;
  }
  dir.close();
  for (const std::string& p : bad) {
    LOGF("icons: %s isn't an icon, deleted\n", p.c_str());
    LittleFS.remove(p.c_str());
  }
  LOGF("icons: %d saved icons loaded\n", loaded);
}

}  // namespace

void begin() {
  pool = static_cast<Slot*>(heap_caps_calloc(kSlots, sizeof(Slot), MALLOC_CAP_SPIRAM));
  uint32_t start = millis();
  // the `spiffs` partition (LittleFS's default label); formatted the first time (or if it isn't LittleFS)
  mounted_ = LittleFS.begin(true);
  if (!mounted_) {
    LOGF("icons: no flash for icons (LittleFS didn't mount): fetched ones are kept till a restart\n");
    return;
  }
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  LOGF("icons: LittleFS mounted in %u ms, %u of %u KB used\n", (unsigned)(millis() - start),
       (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  load();
}

bool mounted() { return mounted_; }

bool saveFile(const std::string& name, const std::string& rev, const uint8_t* mask) {
  if (!mounted_ || !validName(name)) return false;
  static uint8_t buf[kFileBytes];  // MapClient's task only
  buildIconFile(rev, mask, buf);
  std::string tmp = path(name, ".tmp"), dest = path(name, ".bin");
  File f = LittleFS.open(tmp.c_str(), FILE_WRITE);
  if (!f) {
    LOGF("icons: can't write %s\n", tmp.c_str());
    return false;
  }
  size_t n = f.write(buf, sizeof(buf));
  f.close();
  if (n != sizeof(buf)) {
    LOGF("icons: %s: wrote %u of %u bytes (the flash is full?)\n", tmp.c_str(), (unsigned)n, (unsigned)sizeof(buf));
    LittleFS.remove(tmp.c_str());
    return false;
  }
  // littlefs's rename replaces the old file in one step; should it refuse, the old one goes first
  if (!LittleFS.rename(tmp.c_str(), dest.c_str())) {
    LittleFS.remove(dest.c_str());
    if (!LittleFS.rename(tmp.c_str(), dest.c_str())) {
      LOGF("icons: can't rename %s\n", tmp.c_str());
      LittleFS.remove(tmp.c_str());
      return false;
    }
  }
  return true;
}

bool adopt(const std::string& name, const std::string& rev, const uint8_t* mask, bool saved) {
  if (!validName(name)) return false;
  bool kept = put(name, rev, mask);
  if (kept || saved) latest[name] = rev;
  return kept;
}

Have have(const std::string& name) {
  Have h;
  auto it = latest.find(name);
  if (it != latest.end()) {
    h.saved = true;
    h.savedRev = it->second;
  }
  const char* rev = vis_icon_rev(name.c_str());
  if (rev) {
    h.compiled = true;
    h.compiledRev = rev;
  }
  return h;
}

}  // namespace icon_store

// icons.h: the newest fetched icon at `rev` (any, while the Chart doesn't say), else the built-in one, else an older
// fetched one, else none
const lv_image_dsc_t* vis_icon(const char* name, const char* rev) {
  if (!name || !*name) return nullptr;
  using icon_store::pool;
  using icon_store::used;
  const char* want = rev ? rev : "";
  const lv_image_dsc_t* exact = nullptr;
  const lv_image_dsc_t* older = nullptr;
  for (size_t i = 0; i < used; i++) {
    if (strcmp(pool[i].name, name) != 0) continue;
    older = &pool[i].dsc;
    if (!*want || strcmp(pool[i].rev, want) == 0) exact = &pool[i].dsc;
  }
  if (exact) return exact;
  const lv_image_dsc_t* built = vis_icon(name);
  return built ? built : older;
}
