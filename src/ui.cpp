#include "ui.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <lvgl.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "audio.h"
#include "firmware_version.h"
#include "legend.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "map_parse.h"
#include "stream.h"
#include "updater.h"

extern "C" const lv_image_dsc_t logo;       // logo.c, from assets/logo.svg
extern "C" const lv_image_dsc_t star_icon;  // star_icon.c, from assets/star.svg

namespace {

// metarmap2's palette (src/metarmap/web/static/style.css).
constexpr uint32_t kBg = 0x0b1117;
constexpr uint32_t kSurface = 0x121a23;
constexpr uint32_t kSurface2 = 0x18222d;
constexpr uint32_t kSunken = 0x0f161e;
constexpr uint32_t kLine = 0x223041;
constexpr uint32_t kControl = 0x2c3b4e;
constexpr uint32_t kText = 0xe6edf3;
constexpr uint32_t kText2 = 0xc4d0dd;
constexpr uint32_t kMuted = 0x93a4b8;
constexpr uint32_t kAccent = 0xf5b544;
constexpr uint32_t kAccentSoft = 0x2a2210;
constexpr uint32_t kAccentLine = 0x5a4417;
constexpr uint32_t kOffLed = 0x4a5d74;
constexpr uint32_t kVfr = 0x3ddc84;
constexpr uint32_t kMvfr = 0x4ea8ff;
constexpr uint32_t kIfr = 0xff5a5a;
constexpr uint32_t kLifr = 0xd77bff;
constexpr uint32_t kNone = 0x5b6b7e;

constexpr int kW = 320, kH = 240;
constexpr int kChipH = 44;  // the visualizer chips' height; the tabs in the bar match it
// the bar: 4 px above the tabs, which are kChipH inside their 2 px padding and 1 px border
constexpr int kBarH = 4 + kChipH + 2 * (2 + 1);
constexpr int kPad = 6;
constexpr int kCols = 4;                    // Info tiles per row
constexpr uint32_t kHoldMs = 3000;          // a touched control keeps its value while the map catches up
constexpr uint32_t kSplashHoldMs = 2000;    // the splash stays this long after connecting
constexpr uint32_t kWifiTimeoutMs = 15000;  // then the splash names the network it can't join
constexpr uint32_t kTouchHoldMs = 5000;     // touching this long shows the splash again
constexpr uint32_t kSplashShowMs = 5000;    // for this long
constexpr uint32_t kUpdateHoldMs = 10000;   // touching this long shows the update screen
constexpr uint32_t kLostMs = 5000;          // the map unreachable this long: back to the splash screen
constexpr uint32_t kStar = 0xffd400;        // the update star
constexpr const char* kRepo = "github.com/dman776/metarmap2";

MapClient* client = nullptr;
Updater* updater = nullptr;
UpdateState upd;
uint32_t updRev = 0;
MapStatus status;
MapLive live;
Connection conn;
uint32_t statusRev = 0, liveRev = 0, connRev = 0;

lv_style_t stCard, stChip, stChipOn, stTab, stTabOn, stTile, stPressed;

lv_obj_t* tabButtons[2];
lv_obj_t* panels[2];
lv_obj_t *starIcon, *hostDot, *hostLabel;
lv_obj_t *chipRow, *legendBox;
lv_obj_t *rotateSw, *clockBox, *clockArc, *clockLabel;
lv_obj_t *brtSlider, *brtLabel;
lv_obj_t *pausedBanner, *grid;
// The Info tab: what a tap on an airport does. Locate pulses its LED; AWOS reads its METAR aloud; ATIS its real
// ATIS, which then shows line by line (the transcript) in place of the airports as it's read.
enum class Mode { Locate, Awos, Atis };
Mode mode = Mode::Locate;
lv_obj_t* modeRow = nullptr;
lv_obj_t* modeChips[3];
lv_obj_t* noAtis = nullptr;      // "no airport here has an ATIS", in ATIS mode
lv_obj_t* transcript = nullptr;  // the ATIS being read: each line the FAA's text, then how it's said
std::vector<lv_obj_t*> txRows;
int txLit = -1;       // the line being said
bool txOpen = false;  // the transcript is on screen (it stays after the end, to read; a tap closes it)
AtisTranscript atis;  // the lines, from the map
uint32_t atisRev = 0;
std::string atisFor;                      // the airport whose ATIS was last tapped
lv_obj_t* radioStatus;                    // what the speaker is doing: a spoken METAR, or the Radio
lv_obj_t *volBox, *volSlider, *volLabel;  // the volume popup, while the speaker plays
StreamState radio;
uint32_t radioRev = 0;

std::vector<lv_obj_t*> chips;  // by visualizer index
struct Tile {
  lv_obj_t* btn;
  lv_obj_t* dot;
  lv_obj_t* label;
  int pixel;
  std::string category;
  std::string code;
  bool atis;  // it has an ATIS to hear (the map's leds[].atis)
};
std::vector<Tile> tiles;
std::string tileKey;  // the station codes and pixels the tiles were built for

int shownIndex = -1;           // the visualizer on screen
std::vector<bool> legendUsed;  // the legend entries on screen
uint32_t brtTouched = 0, rotateTouched = 0;
bool rotateRunning = false;
uint32_t rotateAtMs = 0;  // our millis() when the next visualizer comes
int rotateSeconds = 0;
double lastLocateAt = NAN;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

uint32_t categoryColor(const std::string& cat) {
  if (cat == "VFR") return kVfr;
  if (cat == "MVFR") return kMvfr;
  if (cat == "IFR") return kIfr;
  if (cat == "LIFR") return kLifr;
  return kNone;
}

bool recentlyTouched(uint32_t at) { return at && millis() - at < kHoldMs; }

// ---- building blocks ---------------------------------------------------------------------------------------

void initStyles() {
  lv_style_init(&stCard);
  lv_style_set_bg_color(&stCard, hex(kSunken));
  lv_style_set_bg_opa(&stCard, LV_OPA_COVER);
  lv_style_set_radius(&stCard, 6);
  lv_style_set_pad_hor(&stCard, 6);
  lv_style_set_pad_ver(&stCard, 3);

  lv_style_init(&stChip);
  lv_style_set_radius(&stChip, 8);
  lv_style_set_border_width(&stChip, 1);
  lv_style_set_border_color(&stChip, hex(kControl));
  lv_style_set_bg_opa(&stChip, LV_OPA_TRANSP);
  lv_style_set_pad_hor(&stChip, 10);
  lv_style_set_text_color(&stChip, hex(kText2));
  lv_style_set_text_font(&stChip, &lv_font_montserrat_12);

  lv_style_init(&stChipOn);
  lv_style_set_bg_color(&stChipOn, hex(kAccentSoft));
  lv_style_set_bg_opa(&stChipOn, LV_OPA_COVER);
  lv_style_set_border_color(&stChipOn, hex(kAccent));
  lv_style_set_text_color(&stChipOn, hex(kAccent));

  lv_style_init(&stTab);
  lv_style_set_radius(&stTab, 8);
  lv_style_set_bg_opa(&stTab, LV_OPA_TRANSP);
  lv_style_set_text_color(&stTab, hex(kMuted));
  lv_style_set_text_font(&stTab, &lv_font_montserrat_14);

  lv_style_init(&stTabOn);
  lv_style_set_bg_color(&stTabOn, hex(kAccentSoft));
  lv_style_set_bg_opa(&stTabOn, LV_OPA_COVER);
  lv_style_set_border_width(&stTabOn, 1);
  lv_style_set_border_color(&stTabOn, hex(kAccentLine));
  lv_style_set_text_color(&stTabOn, hex(kAccent));

  lv_style_init(&stTile);
  lv_style_set_radius(&stTile, 8);
  lv_style_set_border_width(&stTile, 1);
  lv_style_set_border_color(&stTile, hex(kControl));
  lv_style_set_bg_color(&stTile, hex(kSurface2));
  lv_style_set_bg_opa(&stTile, LV_OPA_COVER);
  lv_style_set_text_color(&stTile, hex(kText));
  lv_style_set_text_font(&stTile, &lv_font_montserrat_14);

  lv_style_init(&stPressed);
  lv_style_set_border_color(&stPressed, hex(kAccent));
}

// A plain container: no theme padding, border or background.
lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}

lv_obj_t* button(lv_obj_t* parent, lv_style_t* style) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, style, 0);
  lv_obj_add_style(b, &stPressed, LV_STATE_PRESSED);
  return b;
}

lv_obj_t* dot(lv_obj_t* parent, int size, uint32_t color) {
  lv_obj_t* d = lv_obj_create(parent);
  lv_obj_remove_style_all(d);
  lv_obj_set_size(d, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, hex(color), 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  return d;
}

std::vector<bool> legendInUse() { return legend::inUse(status, live, shownIndex); }

// ---- the visualizer ----------------------------------------------------------------------------------------

void showLegend() {
  lv_obj_clean(legendBox);
  legendUsed = legendInUse();
  int itemW = (kW - 2 * kPad - 4) / 2;
  for (size_t i = 0; i < status.legend.size(); i++) {
    if (!legendUsed[i]) continue;
    const LegendItem& item = status.legend[i];
    lv_obj_t* row = box(legendBox, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(row, &stCard, 0);
    lv_obj_set_size(row, itemW, 22);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    // The LED: a dot in its color, a bar for a gradient, a ring for off.
    const auto& c = item.colors;
    bool gradient = legend::isGradient(item);
    lv_obj_t* led = dot(row, 10, c.empty() ? kBg : c[0]);
    if (c.empty() || item.effect == "off") {
      lv_obj_set_style_bg_opa(led, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(led, 2, 0);
      lv_obj_set_style_border_color(led, hex(kOffLed), 0);
    } else if (gradient) {
      lv_obj_set_width(led, 22);
      lv_obj_set_style_radius(led, 5, 0);
      lv_obj_set_style_bg_grad_color(led, hex(c.back()), 0);
      lv_obj_set_style_bg_grad_dir(led, LV_GRAD_DIR_HOR, 0);
    } else if (item.effect == "flash" && c.size() > 1) {
      lv_obj_set_style_border_width(led, 2, 0);  // the flash color as a ring
      lv_obj_set_style_border_color(led, hex(c.back()), 0);
    }

    lv_obj_t* text = label(row, item.label.c_str(), &lv_font_montserrat_12, kText);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
  }
}

void showVisualizer(int index) {
  shownIndex = index;
  lv_obj_update_layout(chipRow);  // new chips need their positions to scroll to
  for (size_t i = 0; i < chips.size(); i++) {
    if ((int)i == index) {
      lv_obj_add_state(chips[i], LV_STATE_CHECKED);
      lv_obj_scroll_to_view(chips[i], LV_ANIM_ON);
    } else {
      lv_obj_remove_state(chips[i], LV_STATE_CHECKED);
    }
  }
  // Clear the old legend; the new one shows once it can be filtered.
  if (legendInUse() != legendUsed) showLegend();
}

void onChip(lv_event_t* e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= (int)status.visualizers.size()) return;
  showVisualizer(index);  // at once, not at the next poll
  client->command("/visualizer/" + status.visualizers[index].id);
}

void buildChips() {
  lv_obj_clean(chipRow);
  chips.clear();
  for (size_t i = 0; i < status.visualizers.size(); i++) {
    lv_obj_t* chip = button(chipRow, &stChip);
    lv_obj_add_style(chip, &stChipOn, LV_STATE_CHECKED);
    lv_obj_set_height(chip, LV_PCT(100));
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_t* l = lv_label_create(chip);
    lv_label_set_text(l, status.visualizers[i].name.c_str());
    lv_obj_center(l);
    lv_obj_add_event_cb(chip, onChip, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    chips.push_back(chip);
  }
}

bool sameVisualizers(const std::vector<Visualizer>& a) {
  if (a.size() != chips.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    lv_obj_t* l = lv_obj_get_child(chips[i], 0);
    if (a[i].name != lv_label_get_text(l)) return false;
  }
  return true;
}

// ---- auto-rotate, brightness, OLED -------------------------------------------------------------------------

void tickClock() {
  if (!rotateRunning) return;
  int32_t left = (int32_t)(rotateAtMs - millis());
  int secs = left > 0 ? (left + 999) / 1000 : 0;
  lv_label_set_text_fmt(clockLabel, "%d:%02d", secs / 60, secs % 60);
  int part = rotateSeconds > 0 ? std::min(100, 100 * secs / rotateSeconds) : 0;
  lv_arc_set_value(clockArc, part);
}

void showRotation() {
  if (recentlyTouched(rotateTouched)) return;
  if (live.autoRotate) {
    lv_obj_add_state(rotateSw, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(rotateSw, LV_STATE_CHECKED);
  }
  rotateSeconds = live.autoSeconds;
  bool running = live.autoRotate && !std::isnan(live.autoNext);
  if (running) {
    uint32_t at = live.rxMillis + (uint32_t)std::max(0.0, (live.autoNext - live.now) * 1000);
    // Not jumping with each poll's delay.
    if (!rotateRunning || abs((int32_t)(at - rotateAtMs)) > 1500) rotateAtMs = at;
  }
  rotateRunning = running;
  if (running) {
    lv_obj_remove_flag(clockBox, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(clockBox, LV_OBJ_FLAG_HIDDEN);
  }
  tickClock();
}

void onRotate(lv_event_t*) {
  rotateTouched = millis();
  bool on = lv_obj_has_state(rotateSw, LV_STATE_CHECKED);
  client->command(on ? "/visualizer/auto_rotate/on" : "/visualizer/auto_rotate/off");
}

void showBrightnessLabel(int pct) { lv_label_set_text_fmt(brtLabel, "%d%%", pct); }

void onBrightness(lv_event_t* e) {
  brtTouched = millis();
  int pct = lv_slider_get_value(brtSlider);
  showBrightnessLabel(pct);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    char path[32];
    snprintf(path, sizeof(path), "/brightness/%.2f", pct / 100.0);
    client->command(path);
  }
}

void showBrightness() {
  if (recentlyTouched(brtTouched) || lv_obj_has_state(brtSlider, LV_STATE_PRESSED)) return;
  int pct = (int)lroundf(live.brightness * 100);
  lv_slider_set_value(brtSlider, pct, LV_ANIM_OFF);
  showBrightnessLabel(pct);
}

// ---- the Info tab: the airports, and what a tap does ------------------------------------------------------------

void closeTranscript();
void showModes();
void showInfo();
void showRadio();
bool speakingNow() {
  return !radio.speaking.empty() && radio.phase != StreamState::Off && radio.phase != StreamState::Failed;
}

void onTile(lv_event_t* e) {
  int pixel = (int)(intptr_t)lv_event_get_user_data(e);
  const Tile* tile = nullptr;
  for (const Tile& t : tiles) {
    if (t.pixel == pixel) tile = &t;
  }
  if (!tile) return;
  if (mode == Mode::Locate) {
    client->command("/locate/" + std::to_string(pixel));
    return;
  }
  bool wantAtis = mode == Mode::Atis;
  if (speakingNow() && radio.speaking == tile->code && radio.atis == wantAtis) {  // the one being read: stop
    stream_set_playing(false);
    return;
  }
  audio_click();
  closeTranscript();
  stream_speak(tile->code, wantAtis);
  if (wantAtis) {
    atisFor = tile->code;
    client->requestAtis(tile->code);  // its lines, ready for when the sound starts
  }
}

void unflash(lv_timer_t* t) {
  size_t i = (size_t)(uintptr_t)lv_timer_get_user_data(t);
  if (i >= tiles.size()) return;
  lv_obj_set_style_bg_color(tiles[i].btn, hex(kSurface2), 0);
  lv_obj_set_style_text_color(tiles[i].label, hex(kText), 0);
}

// The tile the map just pulsed flashes white, like the /remote page's.
void flashTile(int pixel) {
  for (size_t i = 0; i < tiles.size(); i++) {
    if (tiles[i].pixel != pixel) continue;
    lv_obj_set_style_bg_color(tiles[i].btn, hex(0xffffff), 0);
    lv_obj_set_style_text_color(tiles[i].label, hex(kBg), 0);
    lv_obj_scroll_to_view(tiles[i].btn, LV_ANIM_ON);
    lv_timer_t* t = lv_timer_create(unflash, 700, (void*)(uintptr_t)i);
    lv_timer_set_repeat_count(t, 1);
  }
}

void buildTiles() {
  lv_obj_clean(grid);
  tiles.clear();
  int tileW = (kW - 2 * kPad - (kCols - 1) * 6 - 4) / kCols;
  for (const Station& s : live.stations) {
    lv_obj_t* b = button(grid, &stTile);
    lv_obj_set_size(b, tileW, 34);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    if (s.isVirtual) {  // no LED: the page dashes these
      lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_color(b, hex(kNone), 0);
    }
    lv_obj_t* d = dot(b, 8, categoryColor(s.category));
    lv_obj_t* l = label(b, s.code.c_str(), &lv_font_montserrat_14, kText);
    lv_obj_add_event_cb(b, onTile, LV_EVENT_CLICKED, (void*)(intptr_t)s.pixel);
    tiles.push_back({b, d, l, s.pixel, s.category, s.code, s.atis});
  }
}

void showStations() {
  std::string key;
  for (const Station& s : live.stations) key += s.code + ":" + std::to_string(s.pixel) + (s.isVirtual ? "v," : ",");
  if (key != tileKey) {
    tileKey = key;
    buildTiles();
  }
  for (size_t i = 0; i < tiles.size() && i < live.stations.size(); i++) {
    const std::string& cat = live.stations[i].category;
    if (tiles[i].category != cat) {
      tiles[i].category = cat;
      lv_obj_set_style_bg_color(tiles[i].dot, hex(categoryColor(cat)), 0);
    }
    tiles[i].atis = live.stations[i].atis;
  }
  showModes();
  showInfo();

  if (!std::isnan(live.locateAt) && live.locateAt != lastLocateAt) {
    // Not one from before the remote started.
    bool fresh = !std::isnan(lastLocateAt) || live.now - live.locateAt < 2;
    lastLocateAt = live.locateAt;
    if (fresh) flashTile(live.locatePixel);
  }
}

// ---- the bar and tabs --------------------------------------------------------------------------------------

void showTab(int which) {
  for (int i = 0; i < 2; i++) {
    if (i == which) {
      lv_obj_add_state(tabButtons[i], LV_STATE_CHECKED);
      lv_obj_remove_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_state(tabButtons[i], LV_STATE_CHECKED);
      lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void onTab(lv_event_t* e) { showTab((int)(intptr_t)lv_event_get_user_data(e)); }

void showConnection() {
  const char* text;
  uint32_t color;
  if (!conn.wifi) {
    text = "no Wi-Fi";
    color = kIfr;
  } else if (!conn.online) {
    text = "can't reach the map";
    color = kIfr;
  } else {
    text = status.ip.length() ? status.ip.c_str() : client->host();
    color = kVfr;
  }
  lv_label_set_text(hostLabel, text);
  lv_obj_set_style_text_color(hostLabel, hex(conn.online ? kMuted : 0xff8a8a), 0);
  lv_obj_set_style_bg_color(hostDot, hex(color), 0);
  for (lv_obj_t* p : panels) lv_obj_set_style_opa(p, conn.online ? LV_OPA_COVER : LV_OPA_50, 0);
}

void buildBar(lv_obj_t* scr) {
  lv_obj_t* bar = box(scr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(bar, kW, kBarH);
  lv_obj_set_style_pad_hor(bar, kPad, 0);
  lv_obj_set_style_pad_top(bar, 4, 0);
  lv_obj_set_style_pad_column(bar, 8, 0);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* tabs = box(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(tabs, 180, kChipH + 2 * (2 + 1));  // leaves 120 px: star 12 + 5 + dot 7 + 5 + address 88 = 117
  lv_obj_set_style_bg_color(tabs, hex(kSurface), 0);
  lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tabs, 1, 0);
  lv_obj_set_style_border_color(tabs, hex(kLine), 0);
  lv_obj_set_style_radius(tabs, 10, 0);
  lv_obj_set_style_pad_all(tabs, 2, 0);
  lv_obj_set_style_pad_column(tabs, 2, 0);
  const char* names[2] = {"Visualizer", "Info"};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* t = button(tabs, &stTab);
    lv_obj_add_style(t, &stTabOn, LV_STATE_CHECKED);
    lv_obj_set_height(t, LV_PCT(100));
    lv_obj_set_flex_grow(t, 1);
    lv_obj_t* l = lv_label_create(t);
    lv_label_set_text(l, names[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(t, onTab, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    tabButtons[i] = t;
  }

  lv_obj_t* host = box(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_height(host, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(host, 1);
  lv_obj_set_flex_align(host, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(host, 5, 0);
  starIcon = lv_image_create(host);  // an update is available
  lv_image_set_src(starIcon, &star_icon);
  lv_obj_add_flag(starIcon, LV_OBJ_FLAG_HIDDEN);
  hostDot = dot(host, 7, kNone);
  hostLabel = label(host, "connecting", &lv_font_montserrat_12, kMuted);
  lv_label_set_long_mode(hostLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(hostLabel, 88);
  lv_obj_set_style_text_align(hostLabel, LV_TEXT_ALIGN_RIGHT, 0);
}

lv_obj_t* panel(lv_obj_t* scr) {
  lv_obj_t* p = box(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(p, kW, kH - kBarH);
  lv_obj_set_y(p, kBarH);
  lv_obj_set_style_pad_all(p, kPad, 0);
  lv_obj_set_style_pad_row(p, 6, 0);
  return p;
}

lv_obj_t* footLabel(lv_obj_t* parent, const char* text) { return label(parent, text, &lv_font_montserrat_12, kMuted); }

lv_obj_t* toggle(lv_obj_t* parent, lv_event_cb_t cb) {
  lv_obj_t* sw = lv_switch_create(parent);
  lv_obj_set_size(sw, 38, 20);
  lv_obj_set_style_bg_color(sw, hex(kControl), LV_PART_MAIN);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return sw;
}

void buildVisualizerPanel(lv_obj_t* p) {
  // a chip for each visualizer, scrolling sideways; the active one is lit
  chipRow = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(chipRow, LV_PCT(100), kChipH);
  lv_obj_set_style_pad_column(chipRow, 6, 0);
  lv_obj_add_flag(chipRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(chipRow, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(chipRow, LV_SCROLLBAR_MODE_OFF);

  // what the LEDs mean, two columns, scrolling on its own
  legendBox = box(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(legendBox, LV_PCT(100));
  lv_obj_set_flex_grow(legendBox, 1);
  lv_obj_set_style_pad_gap(legendBox, 4, 0);
  lv_obj_set_style_border_side(legendBox, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(legendBox, 1, 0);
  lv_obj_set_style_border_color(legendBox, hex(kLine), 0);
  lv_obj_set_style_pad_top(legendBox, 6, 0);
  lv_obj_add_flag(legendBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(legendBox, LV_DIR_VER);

  // the footer, one row: auto-rotate with its countdown, LED brightness
  lv_obj_t* foot = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(foot, LV_PCT(100), 34);
  lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(foot, 1, 0);
  lv_obj_set_style_border_color(foot, hex(kLine), 0);
  lv_obj_set_style_pad_top(foot, 4, 0);
  lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(foot, 6, 0);

  footLabel(foot, "Auto");
  rotateSw = toggle(foot, onRotate);
  clockBox = box(foot, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(clockBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_align(clockBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(clockBox, 5, 0);
  clockArc = lv_arc_create(clockBox);
  lv_obj_set_size(clockArc, 18, 18);
  lv_obj_remove_style(clockArc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(clockArc, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_rotation(clockArc, 270);
  lv_arc_set_bg_angles(clockArc, 0, 360);
  lv_arc_set_range(clockArc, 0, 100);
  lv_obj_set_style_arc_width(clockArc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(clockArc, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(clockArc, hex(kLine), LV_PART_MAIN);
  lv_obj_set_style_arc_color(clockArc, hex(kAccent), LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(clockArc, 0, 0);
  clockLabel = label(clockBox, "", &lv_font_montserrat_14, kMuted);
  lv_obj_set_width(clockLabel, 34);  // the slider doesn't move as the digits change
  lv_obj_add_flag(clockBox, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* led = footLabel(foot, "LED");
  lv_obj_set_style_margin_left(led, 4, 0);
  brtSlider = lv_slider_create(foot);
  lv_obj_set_flex_grow(brtSlider, 1);
  lv_obj_set_height(brtSlider, 6);
  lv_slider_set_range(brtSlider, 0, 100);
  lv_obj_set_style_bg_color(brtSlider, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_pad_all(brtSlider, 4, LV_PART_KNOB);
  lv_obj_set_ext_click_area(brtSlider, 12);
  lv_obj_add_event_cb(brtSlider, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(brtSlider, onBrightness, LV_EVENT_RELEASED, nullptr);
  brtLabel = label(foot, "--%", &lv_font_montserrat_14, kText);
  lv_obj_set_width(brtLabel, 38);
  lv_obj_set_style_text_align(brtLabel, LV_TEXT_ALIGN_RIGHT, 0);
}

// A line above the tiles for the speaker: what a tap does in AWOS or ATIS mode, then getting/speaking or why not;
// the Radio's state while it plays. Hidden when there's nothing to say.
void buildRadioRow(lv_obj_t* p) {
  radioStatus = label(p, "", &lv_font_montserrat_12, kMuted);
  lv_obj_set_width(radioStatus, LV_PCT(100));
  lv_label_set_long_mode(radioStatus, LV_LABEL_LONG_DOT);
  lv_obj_add_flag(radioStatus, LV_OBJ_FLAG_HIDDEN);
}

void showHidden(lv_obj_t* obj, bool shown);

void showRadio() {
  if (!radioStatus) return;
  char text[80] = "";
  bool speaking = !radio.speaking.empty();
  const char* who = radio.speaking.c_str();
  const char* what = radio.atis ? "'s ATIS" : "";
  switch (speaking && radio.phase != StreamState::Failed ? StreamState::Playing : radio.phase) {
    case StreamState::Connecting:
      snprintf(text, sizeof(text), "connecting...");
      break;
    case StreamState::Buffering:
      snprintf(text, sizeof(text), "buffering %d%%", radio.bufferPct);
      break;
    case StreamState::Playing:
      if (speaking && radio.phase != StreamState::Playing) {
        snprintf(text, sizeof(text), "getting %s%s...", who, what);
      } else if (speaking && radio.atis) {
        snprintf(text, sizeof(text), "%s ATIS - tap %s to stop", who, txOpen ? "the text" : "it again");
      } else if (speaking) {
        snprintf(text, sizeof(text), "speaking %s - tap it again to stop", who);
      } else {
        snprintf(text, sizeof(text), "playing - %.1f kHz - buffer %d%%", radio.rate / 1000.0, radio.bufferPct);
      }
      break;
    case StreamState::Retrying:
      snprintf(text, sizeof(text), "%s - trying again", radio.message.c_str());
      break;
    case StreamState::Failed:
      snprintf(text, sizeof(text), "%s%s%s", speaking ? who : "", speaking ? ": " : "", radio.message.c_str());
      break;
    default:
      if (txOpen)
        snprintf(text, sizeof(text), "%s ATIS - tap the text to go back", atisFor.c_str());
      else if (mode == Mode::Awos)
        snprintf(text, sizeof(text), "tap an airport to hear its weather");
      else if (mode == Mode::Atis)
        snprintf(text, sizeof(text), "tap an airport to hear its ATIS");
      break;
  }
  lv_label_set_text(radioStatus, text);
  showHidden(radioStatus, text[0] != '\0');
}

// ---- the modes ----------------------------------------------------------------------------------------------

bool awosAvailable() { return stream_available() && live.speech; }

bool atisAvailable() {
  if (!awosAvailable()) return false;
  for (const Station& st : live.stations) {
    if (st.atis) return true;
  }
  return false;
}

void setMode(Mode next) {
  if (next == Mode::Awos && !awosAvailable()) next = Mode::Locate;
  if (next == Mode::Atis && !atisAvailable()) next = Mode::Locate;
  if (next != mode) {
    if (speakingNow()) stream_set_playing(false);
    mode = next;
    closeTranscript();
  }
  for (int i = 0; i < 3; i++) {
    if (!modeChips[i]) continue;
    if (i == (int)mode) {
      lv_obj_add_state(modeChips[i], LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(modeChips[i], LV_STATE_CHECKED);
    }
  }
  showInfo();
  showRadio();
}

void onMode(lv_event_t* e) { setMode((Mode)(intptr_t)lv_event_get_user_data(e)); }

// Which modes there are: AWOS while the map can speak, ATIS while an airport has one too. One that goes away
// (speech turned off at the map) leaves Locate.
void showModes() {
  if (!modeRow) return;
  bool awos = awosAvailable(), withAtis = atisAvailable();
  showHidden(modeRow, awos);
  showHidden(modeChips[(int)Mode::Atis], withAtis);
  if ((mode == Mode::Awos && !awos) || (mode == Mode::Atis && !withAtis)) setMode(Mode::Locate);
}

// What the panel shows for the mode: the airports (in ATIS mode only those with one), or the transcript.
void showInfo() {
  bool any = false;
  for (const Tile& t : tiles) {
    bool shown = mode != Mode::Atis || t.atis;
    showHidden(t.btn, shown);
    any = any || shown;
  }
  showHidden(grid, !txOpen);
  if (transcript) showHidden(transcript, txOpen);
  if (noAtis) showHidden(noAtis, mode == Mode::Atis && !any && !txOpen);
  bool paused = mode == Mode::Locate && live.exclusive;
  if (paused) lv_label_set_text_fmt(pausedBanner, "Locate is paused by %s", live.activeName.c_str());
  showHidden(pausedBanner, paused);
  lv_obj_set_style_opa(grid, paused ? LV_OPA_50 : LV_OPA_COVER, 0);
}

// ---- the transcript: an ATIS line by line as it's read, the line being said lit and kept in view ----------------

void lightLine(int i) {
  if (i == txLit) return;
  if (txLit >= 0 && txLit < (int)txRows.size()) {
    lv_obj_set_style_bg_opa(txRows[txLit], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(txRows[txLit], hex(kBg), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(txRows[txLit], 1), hex(kMuted), 0);
  }
  txLit = i;
  if (i >= 0 && i < (int)txRows.size()) {
    lv_obj_set_style_bg_opa(txRows[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(txRows[i], hex(kAccent), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(txRows[i], 1), hex(kText), 0);
    lv_obj_scroll_to_view(txRows[i], LV_ANIM_ON);
  }
}

void openTranscript() {
  lv_obj_clean(transcript);
  txRows.clear();
  txLit = -1;
  for (const AtisLine& line : atis.lines) {
    lv_obj_t* row = box(transcript, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_bg_color(row, hex(kAccentSoft), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 3, 0);
    lv_obj_set_style_border_color(row, hex(kBg), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);  // a tap on a line is a tap on the transcript
    lv_obj_t* raw = label(row, line.raw.c_str(), &lv_font_montserrat_12, kText);
    lv_obj_t* said = label(row, line.said.c_str(), &lv_font_montserrat_12, kMuted);
    for (lv_obj_t* l : {raw, said}) {
      lv_obj_set_width(l, LV_PCT(100));
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    if (line.raw.empty()) lv_obj_add_flag(raw, LV_OBJ_FLAG_HIDDEN);
    txRows.push_back(row);
  }
  lv_obj_scroll_to_y(transcript, 0, LV_ANIM_OFF);
  txOpen = true;
  showInfo();
}

void closeTranscript() {
  if (!txOpen) return;
  txOpen = false;
  if (transcript) lv_obj_clean(transcript);
  txRows.clear();
  txLit = -1;
  showInfo();
}

// While it plays: the lines show once the sound starts (the words wait for it, as on the web pages), and the line
// being said follows it. After the end the text stays, to read, until a tap.
void followTranscript() {
  if (!transcript || mode != Mode::Atis) return;
  bool mine = radio.atis && radio.speaking == atisFor && atis.valid && atis.code == atisFor;
  bool sounding = radio.phase == StreamState::Playing && radio.playedMs > 0;
  if (!txOpen && mine && sounding && !atis.lines.empty()) openTranscript();
  if (txOpen) lightLine(speakingNow() ? atisLineAt(atis.lines, radio.playedMs, radio.totalMs) : -1);
}

void onTranscript(lv_event_t*) {
  if (speakingNow()) {
    stream_set_playing(false);  // tap to stop; the text stays
  } else {
    closeTranscript();  // and again: back to the airports
  }
  showRadio();
}

// ---- the volume popup: a vertical slider at the right edge while the speaker plays (a spoken METAR, the Radio) ----

void onVolume(lv_event_t* e) {
  int pct = lv_slider_get_value(volSlider);
  lv_label_set_text_fmt(volLabel, "%d%%", pct);
  stream_set_volume(pct, lv_event_get_code(e) == LV_EVENT_RELEASED);  // heard at once; saved on release
}

void buildVolume(lv_obj_t* scr) {
  volBox = box(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(volBox, 52, 164);
  lv_obj_align(volBox, LV_ALIGN_RIGHT_MID, -kPad, kBarH / 2);
  lv_obj_set_flex_align(volBox, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(volBox, hex(kSurface2), 0);
  lv_obj_set_style_bg_opa(volBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(volBox, hex(kControl), 0);
  lv_obj_set_style_border_width(volBox, 1, 0);
  lv_obj_set_style_radius(volBox, 12, 0);
  lv_obj_set_style_pad_ver(volBox, 10, 0);
  lv_obj_set_style_shadow_width(volBox, 16, 0);
  lv_obj_set_style_shadow_opa(volBox, LV_OPA_50, 0);
  lv_obj_add_flag(volBox, LV_OBJ_FLAG_CLICKABLE);  // a touch beside the slider doesn't reach the tile beneath

  label(volBox, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, kText2);
  volSlider = lv_slider_create(volBox);  // taller than wide: vertical, 0 at the bottom
  lv_obj_set_size(volSlider, 8, 96);
  lv_slider_set_range(volSlider, 0, 100);
  lv_slider_set_value(volSlider, stream_volume(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(volSlider, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_pad_all(volSlider, 5, LV_PART_KNOB);
  lv_obj_set_ext_click_area(volSlider, 16);
  lv_obj_add_event_cb(volSlider, onVolume, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(volSlider, onVolume, LV_EVENT_RELEASED, nullptr);
  volLabel = label(volBox, "", &lv_font_montserrat_12, kMuted);
  lv_label_set_text_fmt(volLabel, "%d%%", stream_volume());
  lv_obj_add_flag(volBox, LV_OBJ_FLAG_HIDDEN);
}

void showVolume() {
  if (!volBox) return;
  bool playing = radio.phase == StreamState::Connecting || radio.phase == StreamState::Buffering ||
                 radio.phase == StreamState::Playing || radio.phase == StreamState::Retrying;
  showHidden(volBox, playing);
}

void buildInfoPanel(lv_obj_t* p) {
  if (stream_available()) {  // Locate | AWOS | ATIS, while the map can speak (showModes)
    modeRow = box(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(modeRow, LV_PCT(100), 30);
    lv_obj_set_style_pad_column(modeRow, 6, 0);
    const char* names[3] = {"Locate", "AWOS", "ATIS"};
    for (int i = 0; i < 3; i++) {
      lv_obj_t* c = button(modeRow, &stChip);
      lv_obj_add_style(c, &stChipOn, LV_STATE_CHECKED);
      lv_obj_set_size(c, 72, 30);
      lv_obj_t* l = lv_label_create(c);
      lv_label_set_text(l, names[i]);
      lv_obj_center(l);
      lv_obj_add_event_cb(c, onMode, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      modeChips[i] = c;
    }
    lv_obj_add_state(modeChips[0], LV_STATE_CHECKED);
    lv_obj_add_flag(modeRow, LV_OBJ_FLAG_HIDDEN);
  }

  pausedBanner = label(p, "", &lv_font_montserrat_12, kAccent);
  lv_obj_set_style_bg_color(pausedBanner, hex(kAccentSoft), 0);
  lv_obj_set_style_bg_opa(pausedBanner, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pausedBanner, 6, 0);
  lv_obj_set_style_pad_hor(pausedBanner, 8, 0);
  lv_obj_set_style_pad_ver(pausedBanner, 4, 0);
  lv_obj_add_flag(pausedBanner, LV_OBJ_FLAG_HIDDEN);

  if (stream_available()) buildRadioRow(p);

  grid = box(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);

  if (stream_available()) {
    noAtis = label(p, "No airport on this map has a digital ATIS: only about 75 large US airports publish one.",
                   &lv_font_montserrat_12, kMuted);
    lv_obj_set_width(noAtis, LV_PCT(100));
    lv_label_set_long_mode(noAtis, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(noAtis, LV_OBJ_FLAG_HIDDEN);

    transcript = box(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(transcript, LV_PCT(100));
    lv_obj_set_flex_grow(transcript, 1);
    lv_obj_set_style_pad_row(transcript, 2, 0);
    lv_obj_set_style_pad_right(transcript, 52 + 2 * kPad, 0);  // clear of the volume popup at the right edge
    lv_obj_add_flag(transcript, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(transcript, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(transcript, LV_DIR_VER);
    lv_obj_add_event_cb(transcript, onTranscript, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(transcript, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- the splash screen -------------------------------------------------------------------------------------
//
// The logo, the name, the repo and the version, with the Wi-Fi and the map
// connecting; once both are OK it stays a moment, then the remote fades in.
// A 5 s touch shows it again for 5 s (the version, the connection). If the
// map is lost, it comes back, retrying, until the map answers again.

enum class SplashMode {
  Startup,    // until Wi-Fi and the map are OK, then 2 s more
  OnDemand,   // a long touch: 5 s
  Reconnect,  // the map was lost: like Startup
};
lv_obj_t* splash = nullptr;
SplashMode splashMode = SplashMode::Startup;
lv_obj_t* remoteScreen = nullptr;
lv_obj_t *wifiStep, *wifiState, *mapState, *mapVersion;
uint32_t splashStart = 0;
uint32_t connectedAt = 0;

// The network the remote is set to join (secrets.h), from the Wi-Fi driver.
std::string wifiName() {
  wifi_config_t cfg = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) return "";
  return std::string((const char*)cfg.sta.ssid, strnlen((const char*)cfg.sta.ssid, sizeof(cfg.sta.ssid)));
}

// A "Connecting to ..." line; returns its state label (OK, retrying).
lv_obj_t* splashStep(lv_obj_t* parent, const char* text, lv_obj_t** textLabel = nullptr) {
  lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_t* l = label(row, text, &lv_font_montserrat_14, kText2);
  if (textLabel) *textLabel = l;
  return label(row, "", &lv_font_montserrat_14, kAccent);
}

void setStep(lv_obj_t* state, const char* text, uint32_t color) {
  if (strcmp(lv_label_get_text(state), text) != 0) lv_label_set_text(state, text);
  lv_obj_set_style_text_color(state, hex(color), 0);
}

void showSplash(lv_obj_t* remote, SplashMode mode) {
  remoteScreen = remote;
  splashMode = mode;
  splash = lv_obj_create(nullptr);
  lv_obj_remove_style_all(splash);
  lv_obj_set_style_bg_color(splash, hex(kBg), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(splash, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(splash, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(splash, 2, 0);

  lv_obj_t* img = lv_image_create(splash);
  lv_image_set_src(img, &logo);
  label(splash, "MetarMap2", &lv_font_montserrat_28, kText);
  label(splash, kRepo, &lv_font_montserrat_12, kMuted);
  std::string version = std::string("Remote ") + FIRMWARE_VERSION;
  label(splash, version.c_str(), &lv_font_montserrat_12, kMuted);
  mapVersion = label(splash, "Map ...", &lv_font_montserrat_12, kMuted);  // once the map answers
  lv_obj_set_style_margin_bottom(mapVersion, 10, 0);
  wifiState = splashStep(splash, "Connecting to Wi-Fi...", &wifiStep);
  mapState =
      splashStep(splash, mode == SplashMode::Reconnect ? "Reconnecting to the map..." : "Connecting to the map...");

  if (mode != SplashMode::Startup) {
    // A touch that's down began on the remote: forget it, so lifting the
    // finger doesn't tap whatever it was on.
    lv_indev_reset(nullptr, nullptr);
    lv_screen_load_anim(splash, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
  } else {
    lv_screen_load(splash);
  }
  splashStart = millis();
  connectedAt = 0;
}

void hideSplash() {
  lv_screen_load_anim(remoteScreen, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, true);  // then deletes the splash
  splash = nullptr;
}

void tickSplash() {
  if (!splash) return;
  if (status.valid && !status.versionText.empty()) {
    std::string text = "Map " + status.versionText;
    if (text != lv_label_get_text(mapVersion)) lv_label_set_text(mapVersion, text.c_str());
  }
  wl_status_t ws = WiFi.status();
  bool wifi = ws == WL_CONNECTED;
  if (wifi) {
    lv_label_set_text(wifiStep, "Connecting to Wi-Fi...");
    setStep(wifiState, "OK", kVfr);
  } else if (millis() - splashStart >= kWifiTimeoutMs) {
    // Taking too long: say which network, and whether it's even in range (the
    // S3 only sees 2.4 GHz networks). It keeps trying.
    std::string name = wifiName();
    std::string text = name.empty()             ? "No Wi-Fi network set"
                       : ws == WL_NO_SSID_AVAIL ? "Can't find " + name
                                                : "Can't join " + name;
    if (text != lv_label_get_text(wifiStep)) lv_label_set_text(wifiStep, text.c_str());
    setStep(wifiState, "retrying", kAccent);
  } else {
    setStep(wifiState, "", kVfr);
  }
  if (conn.online) {
    setStep(mapState, "OK", kVfr);
  } else if (wifi && conn.wifi && conn.httpCode != 0) {
    setStep(mapState, "retrying", kAccent);  // the map didn't answer
  } else {
    setStep(mapState, "", kAccent);
  }

  if (splashMode == SplashMode::OnDemand) {
    if (millis() - splashStart >= kSplashShowMs) hideSplash();
    return;
  }
  if (!(wifi && conn.online)) {
    connectedAt = 0;
    return;
  }
  if (!connectedAt) connectedAt = millis() ? millis() : 1;
  if (millis() - connectedAt >= kSplashHoldMs) hideSplash();
}

// ---- the update screen ---------------------------------------------------------------------------------------
//
// A 10 s touch: the installed firmware, the newest on the chosen channel
// (Releases, Latest commit, Branch: metarmap2's), and UPDATE.

lv_obj_t* updScr = nullptr;
lv_obj_t* updChips[3];
lv_obj_t *updBranchRow, *updBranch, *updAvailable, *updButton, *updProgressRow, *updBar, *updStatus, *updBack;
std::string updBranchOptions;  // what the branch menu holds

const char* const kChannelLabels[3] = {"Releases", "Latest commit", "Branch"};

void enable(lv_obj_t* obj, bool on) {
  if (on) {
    lv_obj_remove_state(obj, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(obj, LV_STATE_DISABLED);
  }
}

void showHidden(lv_obj_t* obj, bool shown) {
  if (shown) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void hideUpdate() {
  lv_screen_load_anim(remoteScreen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);  // then deletes it
  updScr = nullptr;
}

void onChannel(lv_event_t* e) {
  auto channel = (update::Channel)(intptr_t)lv_event_get_user_data(e);
  std::string branch = upd.branch;
  if (channel == update::Channel::Branch && !upd.branches.empty() &&
      std::find(upd.branches.begin(), upd.branches.end(), branch) == upd.branches.end()) {
    branch = upd.branches[0];  // the saved branch has no build: one that has
  }
  updater->setChannel(channel, branch);
}

void onBranch(lv_event_t*) {
  char name[64];
  lv_dropdown_get_selected_str(updBranch, name, sizeof(name));
  updater->setChannel(update::Channel::Branch, name);
}

void refreshUpdate() {
  if (!updScr) return;
  bool busy = upd.phase == UpdateState::Installing || upd.phase == UpdateState::Restarting;
  for (int i = 0; i < 3; i++) {
    if (i == (int)upd.channel) {
      lv_obj_add_state(updChips[i], LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(updChips[i], LV_STATE_CHECKED);
    }
    enable(updChips[i], !busy);
  }

  // The branch menu: the branches CI has built.
  bool branchChannel = upd.channel == update::Channel::Branch;
  showHidden(updBranchRow, branchChannel);
  if (branchChannel) {
    std::vector<std::string> names = upd.branches;
    if (!upd.branch.empty() && std::find(names.begin(), names.end(), upd.branch) == names.end()) {
      names.push_back(upd.branch);  // the chosen one, even before (or without) a build
    }
    std::string options;
    for (const std::string& n : names) options += (options.empty() ? "" : "\n") + n;
    if (options != updBranchOptions) {
      updBranchOptions = options;
      lv_dropdown_set_options(updBranch, options.c_str());
    }
    auto at = std::find(names.begin(), names.end(), upd.branch);
    if (at != names.end()) lv_dropdown_set_selected(updBranch, at - names.begin());
    enable(updBranch, !busy);
  }

  // What's available on that channel.
  std::string text;
  uint32_t color = kMuted;
  if (upd.phase == UpdateState::Checking) {
    text = "Checking...";
  } else if (!upd.error.empty()) {
    text = upd.error;
    color = kIfr;
  } else if (!upd.checked) {
    text = "";
  } else if (upd.candidate.empty()) {
    text = "None published";
  } else if (upd.available) {
    text = upd.candidate;
    color = kVfr;
  } else {
    text = upd.candidate + "  (up to date)";
  }
  lv_label_set_text(updAvailable, text.c_str());
  lv_obj_set_style_text_color(updAvailable, hex(color), 0);

  // UPDATE, or how the install is going.
  showHidden(updButton, !busy);
  showHidden(updProgressRow, busy);
  enable(updButton, upd.available && upd.phase == UpdateState::Idle);
  enable(updBack, !busy);
  if (busy) {
    lv_bar_set_value(updBar, upd.progress, LV_ANIM_OFF);
    if (upd.phase == UpdateState::Restarting) {
      lv_label_set_text(updStatus, "Restarting...");
    } else {
      lv_label_set_text_fmt(updStatus, "%d%%", upd.progress);
    }
  }
}

lv_obj_t* infoRow(lv_obj_t* parent, const char* name, const char* value, lv_obj_t** valueLabel = nullptr) {
  lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_t* n = label(row, name, &lv_font_montserrat_14, kMuted);
  lv_obj_set_width(n, 74);
  lv_obj_t* v = label(row, value, &lv_font_montserrat_14, kText);
  lv_obj_set_flex_grow(v, 1);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  if (valueLabel) *valueLabel = v;
  return row;
}

void showUpdate() {
  bool fromSplash = splash != nullptr;
  updScr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(updScr);
  lv_obj_set_style_bg_color(updScr, hex(kBg), 0);
  lv_obj_set_style_bg_opa(updScr, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(updScr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(updScr, 8, 0);
  lv_obj_set_style_pad_row(updScr, 8, 0);

  lv_obj_t* head = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(head, LV_PCT(100), 30);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(head, "Firmware update", &lv_font_montserrat_16, kText);
  updBack = button(head, &stChip);
  lv_obj_set_size(updBack, 64, LV_PCT(100));
  lv_obj_set_style_opa(updBack, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_center(label(updBack, "Back", &lv_font_montserrat_14, kText2));
  lv_obj_add_event_cb(updBack, [](lv_event_t*) { hideUpdate(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* chips = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(chips, LV_PCT(100), 34);
  lv_obj_set_style_pad_column(chips, 6, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_t* chip = button(chips, &stChip);
    lv_obj_add_style(chip, &stChipOn, LV_STATE_CHECKED);
    lv_obj_set_style_opa(chip, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_height(chip, LV_PCT(100));
    lv_obj_set_flex_grow(chip, 1);
    lv_obj_t* l = lv_label_create(chip);
    lv_label_set_text(l, kChannelLabels[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(chip, onChannel, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    updChips[i] = chip;
  }

  updBranchRow = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(updBranchRow, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(updBranchRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(updBranchRow, 8, 0);
  lv_obj_set_width(label(updBranchRow, "Branch", &lv_font_montserrat_14, kMuted), 74);
  updBranch = lv_dropdown_create(updBranchRow);
  lv_obj_set_flex_grow(updBranch, 1);
  lv_obj_set_height(updBranch, 32);
  lv_obj_set_style_pad_ver(updBranch, 6, 0);
  lv_obj_add_event_cb(updBranch, onBranch, LV_EVENT_VALUE_CHANGED, nullptr);
  updBranchOptions.clear();

  infoRow(updScr, "Installed", FIRMWARE_VERSION);
  infoRow(updScr, "Available", "", &updAvailable);

  lv_obj_t* spacer = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(spacer, 1);

  updButton = button(updScr, &stTile);
  lv_obj_set_size(updButton, LV_PCT(100), 42);
  lv_obj_set_style_bg_color(updButton, hex(kAccent), 0);
  lv_obj_set_style_border_width(updButton, 0, 0);
  lv_obj_set_style_opa(updButton, LV_OPA_30, LV_STATE_DISABLED);
  lv_obj_center(label(updButton, "UPDATE", &lv_font_montserrat_16, kBg));
  lv_obj_add_event_cb(updButton, [](lv_event_t*) { updater->install(); }, LV_EVENT_CLICKED, nullptr);

  updProgressRow = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(updProgressRow, LV_PCT(100), 42);
  lv_obj_set_flex_align(updProgressRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(updProgressRow, 10, 0);
  updBar = lv_bar_create(updProgressRow);
  lv_obj_set_flex_grow(updBar, 1);
  lv_obj_set_height(updBar, 10);
  lv_obj_set_style_bg_color(updBar, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_bg_color(updBar, hex(kAccent), LV_PART_INDICATOR);
  updStatus = label(updProgressRow, "", &lv_font_montserrat_14, kText);
  lv_obj_set_width(updStatus, 96);
  lv_obj_set_style_text_align(updStatus, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_add_flag(updProgressRow, LV_OBJ_FLAG_HIDDEN);

  // The touch that's still down began on another screen: forget it.
  lv_indev_reset(nullptr, nullptr);
  lv_screen_load_anim(updScr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, fromSplash);  // a splash on screen goes for good
  if (fromSplash) splash = nullptr;
  refreshUpdate();
  if (upd.phase == UpdateState::Idle) updater->check();  // fresh, while it's open
}

// A touch held for 5 s shows the splash; held on to 10 s, the update screen.
void checkLongTouch() {
  static int opened = 0;  // what this touch has opened: 1 the splash, 2 the update screen
  uint32_t held = lvgl_port_touch_held_ms();
  if (held == 0) {
    opened = 0;
  } else if (held >= kUpdateHoldMs && opened < 2 && !updScr) {
    opened = 2;
    showUpdate();
  } else if (held >= kTouchHoldMs && opened < 1 && !splash && !updScr) {
    opened = 1;
    showSplash(remoteScreen, SplashMode::OnDemand);
  }
}

// The map unreachable (or Wi-Fi gone) for 5 s: the splash screen, retrying,
// until it's back. Not over the update screen, which doesn't need the map.
void checkMapLost() {
  static bool everOnline = false;
  static uint32_t lostSince = 0;
  if (conn.online) {
    everOnline = true;
    lostSince = 0;
    return;
  }
  if (!everOnline) return;  // still starting up: the startup splash is on it
  if (!lostSince) lostSince = millis() | 1;
  if (millis() - lostSince >= kLostMs && !splash && !updScr) {
    LOGF("ui: the map is unreachable; reconnecting\n");
    showSplash(remoteScreen, SplashMode::Reconnect);
  }
}

}  // namespace

void ui_init(MapClient& c, Updater& u) {
  client = &c;
  updater = &u;
  lv_display_t* disp = lv_display_get_default();
  lv_theme_t* theme = lv_theme_default_init(disp, hex(kAccent), hex(kMvfr), true, &lv_font_montserrat_14);
  lv_display_set_theme(disp, theme);
  initStyles();

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, hex(kBg), 0);
  lv_obj_set_style_text_color(scr, hex(kText), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildBar(scr);
  panels[0] = panel(scr);
  buildVisualizerPanel(panels[0]);
  panels[1] = panel(scr);
  buildInfoPanel(panels[1]);
  if (stream_available()) buildVolume(scr);  // over both tabs; under the splash
  showTab(0);
  showSplash(scr, SplashMode::Startup);
}

void ui_show_tab(int tab) { showTab(tab == 1 ? 1 : 0); }

void ui_show_update() {
  if (!updScr) showUpdate();
}

void ui_show_splash() {
  if (!splash) showSplash(remoteScreen, SplashMode::OnDemand);
}

void ui_tick() {
  if (client->getConnection(conn, connRev)) showConnection();
  checkLongTouch();
  checkMapLost();
  tickSplash();
  if (updater->get(upd, updRev)) {
    showHidden(starIcon, upd.available);
    refreshUpdate();
  }
  if (stream_get(radio, radioRev)) {
    followTranscript();
    showRadio();
    showVolume();
  }
  if (client->getAtis(atis, atisRev)) followTranscript();

  if (client->getStatus(status, statusRev)) {
    if (!sameVisualizers(status.visualizers)) buildChips();
    showVisualizer(live.valid ? live.activeIndex : status.activeIndex);
    showLegend();
    showConnection();  // the map's IP
  }

  if (client->getLive(live, liveRev)) {
    if (live.activeIndex != shownIndex) showVisualizer(live.activeIndex);
    showRotation();
    showBrightness();
    showStations();
    if (legendInUse() != legendUsed) showLegend();
    showRadio();  // speech turned on or off at the map
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 250) {
    lastTick = millis();
    tickClock();
  }
}
