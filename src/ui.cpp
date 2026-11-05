#include "ui.h"

#include <lvgl.h>

#include "map_client.h"

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
constexpr int kBarH = 34;
constexpr int kPad = 6;
constexpr int kCols = 4;  // Locator tiles per row
constexpr uint32_t kHoldMs = 3000;  // a touched control keeps its value while the map catches up

MapClient* client = nullptr;
MapStatus status;
MapLive live;
Connection conn;
uint32_t statusRev = 0, liveRev = 0, connRev = 0;

lv_style_t stCard, stChip, stChipOn, stTab, stTabOn, stTile, stPressed;

lv_obj_t* tabButtons[2];
lv_obj_t* panels[2];
lv_obj_t *hostDot, *hostLabel;
lv_obj_t *visName, *chipRow, *legendBox;
lv_obj_t *rotateSw, *clockBox, *clockArc, *clockLabel;
lv_obj_t *brtSlider, *brtLabel;
lv_obj_t *pausedBanner, *grid;

std::vector<lv_obj_t*> chips;  // by visualizer index
struct Tile {
  lv_obj_t* btn;
  lv_obj_t* dot;
  lv_obj_t* label;
  int pixel;
  String category;
};
std::vector<Tile> tiles;
String tileKey;  // the station codes and pixels the tiles were built for

int shownIndex = -1;   // the visualizer on screen
int legendIndex = -2;  // the visualizer the legend on screen is for
uint32_t brtTouched = 0, rotateTouched = 0;
bool rotateRunning = false;
uint32_t rotateAtMs = 0;  // our millis() when the next visualizer comes
int rotateSeconds = 0;
double lastLocateAt = NAN;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

uint32_t categoryColor(const String& cat) {
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

// ---- the visualizer ----------------------------------------------------------------------------------------

void showLegend() {
  lv_obj_clean(legendBox);
  legendIndex = status.activeIndex;
  bool current = status.activeIndex == shownIndex;
  lv_obj_set_style_opa(legendBox, current ? LV_OPA_COVER : LV_OPA_50, 0);
  int itemW = (kW - 2 * kPad - 4) / 2;
  for (const LegendItem& item : status.legend) {
    lv_obj_t* row = box(legendBox, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(row, &stCard, 0);
    lv_obj_set_size(row, itemW, 22);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    // The LED: a dot in its color, a bar for a gradient, a ring for off.
    const auto& c = item.colors;
    bool gradient = c.size() > 1 && item.effect != "flash" && item.effect != "alternate";
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
  for (size_t i = 0; i < chips.size(); i++) {
    if ((int)i == index) {
      lv_obj_add_state(chips[i], LV_STATE_CHECKED);
      lv_obj_scroll_to_view(chips[i], LV_ANIM_ON);
    } else {
      lv_obj_remove_state(chips[i], LV_STATE_CHECKED);
    }
  }
  if (index >= 0 && index < (int)status.visualizers.size()) {
    lv_label_set_text(visName, status.visualizers[index].name.c_str());
  } else if (live.valid && index == live.activeIndex) {
    lv_label_set_text(visName, live.activeName.c_str());
  }
  // The legend comes with /api/status, fetched again when the visualizer
  // changes; until then the old one stays, dimmed.
  lv_obj_set_style_opa(legendBox, status.activeIndex == index ? LV_OPA_COVER : LV_OPA_50, 0);
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

// ---- the locator -------------------------------------------------------------------------------------------

void onTile(lv_event_t* e) {
  int pixel = (int)(intptr_t)lv_event_get_user_data(e);
  client->command("/locate/" + String(pixel));
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
    tiles.push_back({b, d, l, s.pixel, s.category});
  }
}

void showStations() {
  String key;
  for (const Station& s : live.stations) key += s.code + ":" + s.pixel + (s.isVirtual ? "v," : ",");
  if (key != tileKey) {
    tileKey = key;
    buildTiles();
  }
  for (size_t i = 0; i < tiles.size() && i < live.stations.size(); i++) {
    const String& cat = live.stations[i].category;
    if (tiles[i].category != cat) {
      tiles[i].category = cat;
      lv_obj_set_style_bg_color(tiles[i].dot, hex(categoryColor(cat)), 0);
    }
  }

  if (live.exclusive) {
    lv_label_set_text_fmt(pausedBanner, "Locate is paused by %s", live.activeName.c_str());
    lv_obj_remove_flag(pausedBanner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pausedBanner, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_opa(grid, live.exclusive ? LV_OPA_50 : LV_OPA_COVER, 0);

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
  lv_obj_set_size(tabs, 196, 30);
  lv_obj_set_style_bg_color(tabs, hex(kSurface), 0);
  lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tabs, 1, 0);
  lv_obj_set_style_border_color(tabs, hex(kLine), 0);
  lv_obj_set_style_radius(tabs, 10, 0);
  lv_obj_set_style_pad_all(tabs, 2, 0);
  lv_obj_set_style_pad_column(tabs, 2, 0);
  const char* names[2] = {"Visualizer", "Locator"};
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
  hostDot = dot(host, 7, kNone);
  hostLabel = label(host, "connecting", &lv_font_montserrat_12, kMuted);
  lv_label_set_long_mode(hostLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(hostLabel, 92);
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

lv_obj_t* footLabel(lv_obj_t* parent, const char* text) {
  return label(parent, text, &lv_font_montserrat_12, kMuted);
}

lv_obj_t* toggle(lv_obj_t* parent, lv_event_cb_t cb) {
  lv_obj_t* sw = lv_switch_create(parent);
  lv_obj_set_size(sw, 38, 20);
  lv_obj_set_style_bg_color(sw, hex(kControl), LV_PART_MAIN);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return sw;
}

void buildVisualizerPanel(lv_obj_t* p) {
  // the active visualizer's name
  visName = label(p, "", &lv_font_montserrat_20, kText);
  lv_label_set_long_mode(visName, LV_LABEL_LONG_DOT);
  lv_obj_set_width(visName, LV_PCT(100));
  lv_obj_set_style_text_align(visName, LV_TEXT_ALIGN_CENTER, 0);

  // a chip for each visualizer, scrolling sideways
  chipRow = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(chipRow, LV_PCT(100), 28);
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

  // the footer: auto-rotate with its countdown, LED brightness
  lv_obj_t* foot = box(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(foot, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(foot, 1, 0);
  lv_obj_set_style_border_color(foot, hex(kLine), 0);
  lv_obj_set_style_pad_top(foot, 4, 0);
  lv_obj_set_style_pad_row(foot, 2, 0);

  lv_obj_t* row1 = box(foot, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row1, LV_PCT(100), 26);
  lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row1, 8, 0);
  footLabel(row1, "Auto-rotate");
  rotateSw = toggle(row1, onRotate);
  clockBox = box(row1, LV_FLEX_FLOW_ROW);
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
  lv_obj_add_flag(clockBox, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* row2 = box(foot, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row2, LV_PCT(100), 26);
  lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row2, 10, 0);
  footLabel(row2, "LED brightness");
  brtSlider = lv_slider_create(row2);
  lv_obj_set_flex_grow(brtSlider, 1);
  lv_obj_set_height(brtSlider, 6);
  lv_slider_set_range(brtSlider, 0, 100);
  lv_obj_set_style_bg_color(brtSlider, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_pad_all(brtSlider, 4, LV_PART_KNOB);
  lv_obj_set_ext_click_area(brtSlider, 12);
  lv_obj_add_event_cb(brtSlider, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(brtSlider, onBrightness, LV_EVENT_RELEASED, nullptr);
  brtLabel = label(row2, "--%", &lv_font_montserrat_14, kText);
  lv_obj_set_width(brtLabel, 40);
  lv_obj_set_style_text_align(brtLabel, LV_TEXT_ALIGN_RIGHT, 0);
}

void buildLocatorPanel(lv_obj_t* p) {
  pausedBanner = label(p, "", &lv_font_montserrat_12, kAccent);
  lv_obj_set_style_bg_color(pausedBanner, hex(kAccentSoft), 0);
  lv_obj_set_style_bg_opa(pausedBanner, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pausedBanner, 6, 0);
  lv_obj_set_style_pad_hor(pausedBanner, 8, 0);
  lv_obj_set_style_pad_ver(pausedBanner, 4, 0);
  lv_obj_add_flag(pausedBanner, LV_OBJ_FLAG_HIDDEN);

  grid = box(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
}

}  // namespace

void ui_init(MapClient& c) {
  client = &c;
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
  buildLocatorPanel(panels[1]);
  showTab(0);
}

void ui_show_tab(int tab) { showTab(tab == 1 ? 1 : 0); }

void ui_tick() {
  if (client->getConnection(conn, connRev)) showConnection();

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
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 250) {
    lastTick = millis();
    tickClock();
  }
}
