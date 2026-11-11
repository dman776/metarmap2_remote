#include "ui.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <lvgl.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "audio.h"
#include "firmware_version.h"
#include "icons.h"
#include "legend.h"
#include "log.h"
#include "lvgl_port.h"
#include "map_client.h"
#include "map_parse.h"
#include "pages.h"
#include "remote_web.h"
#include "skips.h"
#include "stream.h"
#include "timing.h"
#include "updater.h"
#include "volume_ui.h"
#include "wifi_setup.h"

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
constexpr uint32_t kEffect = 0x5d4a7a;  // the border of a whole-strip visualizer (Rainbow, Chase) in the picker
constexpr uint32_t kStar = 0xffd400;    // the update star
constexpr uint32_t kStarSoft = 0x2a2500;

// The screen: the rail at the left (LEDs, Airports, the logo), the panel beside it.
constexpr int kW = 320, kH = 240;
constexpr int kRailW = 56;
constexpr int kPanelW = kW - kRailW;
constexpr int kPad = 8;
constexpr int kTileCols = 3, kTileRows = 4;  // the Airports panel's page
constexpr int kPerPage = kTileCols * kTileRows;
constexpr int kStripFit = 6;  // page buttons under the tiles, before it steps instead
constexpr int kTileGap = 5;
constexpr int kStripH = 34;
constexpr int kBrightStep = 5;  // % per press of - or +
constexpr int kPresets[4] = {10, 25, 50, 100};

constexpr uint32_t kHoldMs = 3000;        // a touched control keeps its value while the map catches up
constexpr uint32_t kSplashHoldMs = 2000;  // the splash stays this long after connecting
// kWifiTimeoutMs (then the splash names the network it can't join) and kLostMs (the map unreachable this long: back
// to the splash screen) are in timing.h: the status light on the back keeps the same time
constexpr uint32_t kSplashShowMs = 30000;    // the logo's splash closes itself after this long
constexpr uint32_t kVolumePopupMs = 4000;    // the logo's splash's volume slider closes this long after a touch
constexpr uint32_t kPulseNoteMs = 2000;      // the card says "Pixel pulsing" this long after a locate (one pulse)
constexpr uint32_t kPickedMs = 250;          // the picked visualizer shows lit this long before the picker closes
constexpr uint32_t kToastMs = 1800;          // a message (the picker's "Couldn't change Wind") shows this long
constexpr uint32_t kLegendTickMs = 40;       // the legend's auto-scroll: a step this often,
constexpr float kLegendPxPerSec = 24;        // this fast,
constexpr uint32_t kLegendPauseMs = 2000;    // resting this long at the top and the bottom,
constexpr uint32_t kLegendResumeMs = 10000;  // and starting again this long after a finger let go of it
constexpr const char* kRepo = "github.com/dman776/metarmap2";

MapClient* client = nullptr;
Updater* updater = nullptr;
UpdateState upd;
uint32_t updRev = 0;
MapStatus status;
MapLive live;
Connection conn;
uint32_t statusRev = 0, liveRev = 0, connRev = 0;

lv_style_t stCard, stBtn, stLit, stRail, stRailOn, stTile, stPressed, stDisabled;

lv_obj_t* remoteScreen = nullptr;
TaskHandle_t uiTask = nullptr;  // the task LVGL runs on (ui_init()'s; setup() and loop() share it), for remote control
lv_obj_t* railButtons[2];
lv_obj_t* panels[2];
lv_obj_t *logoDot, *starIcon;

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

  // a button: raised, with a border
  lv_style_init(&stBtn);
  lv_style_set_radius(&stBtn, 10);
  lv_style_set_border_width(&stBtn, 1);
  lv_style_set_border_color(&stBtn, hex(kControl));
  lv_style_set_bg_color(&stBtn, hex(kSurface2));
  lv_style_set_bg_opa(&stBtn, LV_OPA_COVER);
  lv_style_set_text_color(&stBtn, hex(kText2));
  lv_style_set_text_font(&stBtn, &lv_font_montserrat_14);

  // what's on (checked): the running visualizer, auto-rotate, the audio playing
  lv_style_init(&stLit);
  lv_style_set_bg_color(&stLit, hex(kAccentSoft));
  lv_style_set_border_color(&stLit, hex(kAccent));
  lv_style_set_text_color(&stLit, hex(kAccent));

  lv_style_init(&stRail);
  lv_style_set_radius(&stRail, 10);
  lv_style_set_bg_opa(&stRail, LV_OPA_TRANSP);
  lv_style_set_text_color(&stRail, hex(kMuted));
  lv_style_set_text_font(&stRail, &lv_font_montserrat_10);

  lv_style_init(&stRailOn);
  lv_style_set_bg_color(&stRailOn, hex(kAccentSoft));
  lv_style_set_bg_opa(&stRailOn, LV_OPA_COVER);
  lv_style_set_border_width(&stRailOn, 1);
  lv_style_set_border_color(&stRailOn, hex(kAccentLine));
  lv_style_set_text_color(&stRailOn, hex(kAccent));

  lv_style_init(&stTile);
  lv_style_set_radius(&stTile, 10);
  lv_style_set_border_width(&stTile, 1);
  lv_style_set_border_color(&stTile, hex(kControl));
  lv_style_set_bg_color(&stTile, hex(kSurface2));
  lv_style_set_bg_opa(&stTile, LV_OPA_COVER);
  lv_style_set_text_color(&stTile, hex(kText));
  lv_style_set_text_font(&stTile, &lv_font_montserrat_16);

  lv_style_init(&stPressed);
  lv_style_set_border_color(&stPressed, hex(kAccent));
  lv_style_set_bg_color(&stPressed, hex(kControl));

  lv_style_init(&stDisabled);
  lv_style_set_opa(&stDisabled, LV_OPA_30);
}

// A plain container: no theme padding, border or background.
lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

void center(lv_obj_t* o) { lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); }

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
  lv_obj_add_style(b, &stDisabled, LV_STATE_DISABLED);
  return b;
}

// A white icon, drawn in the text color of the button it's on (or `color`), so it lights up with it.
lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, int32_t color = -1) {
  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, src);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  lv_color_t c = color >= 0 ? hex((uint32_t)color) : lv_obj_get_style_text_color(parent, LV_PART_MAIN);
  lv_obj_set_style_image_recolor(img, c, 0);
  lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
  return img;
}

// A button's icon follows its state: muted, lit, pressed. LVGL doesn't inherit image recolor, so copy it.
void iconFollows(lv_obj_t* img, lv_obj_t* btn) {
  lv_obj_set_style_image_recolor(img, lv_obj_get_style_text_color(btn, LV_PART_MAIN), 0);
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

void showHidden(lv_obj_t* obj, bool shown) {
  if (shown) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void setChecked(lv_obj_t* obj, bool on) {
  if (on) {
    lv_obj_add_state(obj, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(obj, LV_STATE_CHECKED);
  }
}

void enable(lv_obj_t* obj, bool on) {
  if (on) {
    lv_obj_remove_state(obj, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(obj, LV_STATE_DISABLED);
  }
}

void setText(lv_obj_t* l, const std::string& text) {
  if (text != lv_label_get_text(l)) lv_label_set_text(l, text.c_str());
}

// A sheet over the screen: a dim backdrop (a tap on it closes the sheet) and a card on it.
struct Sheet {
  lv_obj_t* backdrop = nullptr;
  lv_obj_t* card = nullptr;
};

Sheet sheet(lv_obj_t* scr, int x, int y, int w, int h, lv_event_cb_t onClose) {
  Sheet s;
  s.backdrop = lv_obj_create(scr);
  lv_obj_remove_style_all(s.backdrop);
  lv_obj_set_size(s.backdrop, kW, kH);
  lv_obj_set_style_bg_color(s.backdrop, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s.backdrop, LV_OPA_60, 0);
  lv_obj_add_flag(s.backdrop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s.backdrop, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s.backdrop, onClose, LV_EVENT_CLICKED, nullptr);

  s.card = lv_obj_create(s.backdrop);
  lv_obj_remove_style_all(s.card);
  lv_obj_set_pos(s.card, x, y);
  lv_obj_set_size(s.card, w, h);
  lv_obj_set_style_bg_color(s.card, hex(kSurface), 0);
  lv_obj_set_style_bg_opa(s.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s.card, 1, 0);
  lv_obj_set_style_border_color(s.card, hex(kControl), 0);
  lv_obj_set_style_radius(s.card, 14, 0);
  lv_obj_set_style_pad_all(s.card, 12, 0);
  lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);  // a tap on the card isn't a tap on the backdrop
  lv_obj_remove_flag(s.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s.backdrop, LV_OBJ_FLAG_HIDDEN);
  return s;
}

// The ✕ at a sheet's top right
lv_obj_t* closeButton(lv_obj_t* card, lv_event_cb_t onClose) {
  lv_obj_t* x = button(card, &stBtn);
  lv_obj_set_size(x, 44, 38);
  lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 6, -6);
  lv_obj_t* img = icon(x, &icon_close, kMuted);
  lv_obj_center(img);
  lv_obj_add_event_cb(x, onClose, LV_EVENT_CLICKED, nullptr);
  return x;
}

std::vector<bool> legendInUse();

// ---- the LEDs panel: the visualizer (◀ name ▶; the name opens the picker), its legend, Auto and LED --------------

lv_obj_t *visName, *legendBox;
constexpr int kMaxSteps = 4;
lv_obj_t *stepRow, *stepChip[kMaxSteps];  // a stepping visualizer's steps (Flight Category Forecast), the one on lit
lv_obj_t *autoBtn, *autoIcon, *autoLabel, *ledBtn, *ledIcon, *ledLabel;
int shownIndex = -1;           // the visualizer on screen
std::vector<bool> legendUsed;  // the legend entries on screen
uint32_t brtTouched = 0, rotateTouched = 0;
bool autoOn = false;
bool rotateRunning = false;
uint32_t rotateAtMs = 0;  // our millis() when the next visualizer comes
int brightPct = -1;       // the LEDs' brightness on screen

std::vector<bool> legendInUse() { return legend::inUse(status, live, shownIndex); }

// The legend's auto-scroll: when its entries don't all fit, it glides down to the last, rests, glides back up,
// rests, and so on. A finger takes over (it scrolls by hand as any list does); 10 s after it lets go, the gliding
// starts again from wherever it was left.
float legendY = 0;             // where the glide is, in px from the top
int legendDir = 1;             // down (1) or up (-1)
uint32_t legendRestUntil = 0;  // resting at an end until then
uint32_t legendTouchedAt = 0;  // when a finger last had it (0: never)

void restartLegendScroll() {
  legendY = 0;
  legendDir = 1;
  legendRestUntil = millis() + kLegendPauseMs;
  lv_obj_scroll_to_y(legendBox, 0, LV_ANIM_OFF);
}

void onLegendTouch(lv_event_t*) { legendTouchedAt = millis() | 1; }

void tickLegend(lv_timer_t*) {
  if (lv_obj_has_flag(panels[0], LV_OBJ_FLAG_HIDDEN)) return;
  int32_t y = lv_obj_get_scroll_y(legendBox);
  int32_t max = y + lv_obj_get_scroll_bottom(legendBox);
  if (max <= 0) return;  // it all fits
  uint32_t now = millis();
  if (legendTouchedAt) {
    if (lv_obj_has_state(legendBox, LV_STATE_PRESSED) || now - legendTouchedAt < kLegendResumeMs) return;
    legendTouchedAt = 0;  // carry on from where the finger left it, the way it was going
    legendY = (float)y;
    legendRestUntil = now;
  }
  if ((int32_t)(legendRestUntil - now) > 0) return;
  legendY += legendDir * kLegendPxPerSec * kLegendTickMs / 1000.0f;
  if (legendY >= max) {
    legendY = (float)max;
    legendDir = -1;
    legendRestUntil = now + kLegendPauseMs;
  } else if (legendY <= 0) {
    legendY = 0;
    legendDir = 1;
    legendRestUntil = now + kLegendPauseMs;
  }
  if ((int32_t)lroundf(legendY) != y) lv_obj_scroll_to_y(legendBox, (int32_t)lroundf(legendY), LV_ANIM_OFF);
}

void showLegend() {
  lv_obj_clean(legendBox);
  legendUsed = legendInUse();
  // an entry a row, full width; a long label wraps onto a second line rather than being cut off
  for (size_t i = 0; i < status.legend.size(); i++) {
    if (!legendUsed[i]) continue;
    const LegendItem& item = status.legend[i];
    lv_obj_t* row = box(legendBox, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(row, &stCard, 0);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 22, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);  // a touch on it is a touch on the legend (and scrolls it)

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
    lv_obj_remove_flag(led, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* text = label(row, item.label.c_str(), &lv_font_montserrat_12, kText);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  }
  restartLegendScroll();
}

void markPicked(int index);

// A stepping visualizer's steps in a row under its name, the one on now lit; the row is gone for any other
void showSteps() {
  size_t n = std::min<size_t>(live.stepLabels.size(), kMaxSteps);
  bool stepping = live.valid && n > 0 && live.activeIndex == shownIndex;
  showHidden(stepRow, stepping);
  for (int i = 0; i < kMaxSteps; i++) {
    bool used = stepping && i < (int)n;
    showHidden(stepChip[i], used);
    if (!used) continue;
    setText(lv_obj_get_child(stepChip[i], 0), live.stepLabels[i]);
    setChecked(stepChip[i], i == live.stepIndex);
  }
}

void showVisualizer(int index) {
  shownIndex = index;
  bool known = index >= 0 && index < (int)status.visualizers.size();
  std::string name = known ? status.visualizers[index].name : "";
  lv_obj_update_layout(lv_obj_get_parent(visName));  // its width is settled before it's measured
  int32_t room = lv_obj_get_content_width(lv_obj_get_parent(visName));
  auto fitsIn = [&](const std::string& text, const lv_font_t* font) {
    return lv_text_get_width(text.c_str(), text.size(), font, 0) <= room;
  };
  // 16 px, or 14 if it doesn't fit ("Dewpoint Spread"), or the short name if the name is too long even for that
  // ("Flight Category Forecast" is "FC Forecast")
  const lv_font_t* font = fitsIn(name, &lv_font_montserrat_16) ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
  if (known && !fitsIn(name, font) && !status.visualizers[index].shortName.empty()) {
    name = status.visualizers[index].shortName;
    font = fitsIn(name, &lv_font_montserrat_16) ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
  }
  lv_obj_set_style_text_font(visName, font, 0);
  setText(visName, name);
  markPicked(index);
  showSteps();
  // Clear the old legend; the new one shows once it can be filtered.
  if (legendInUse() != legendUsed) showLegend();
}

void pickVisualizer(int index) {
  if (index < 0 || index >= (int)status.visualizers.size()) return;
  showVisualizer(index);  // at once, not at the next poll
  client->command("/visualizer/" + status.visualizers[index].id);
}

void onStep(lv_event_t* e) {
  int n = (int)status.visualizers.size();
  if (n == 0) return;
  int step = (int)(intptr_t)lv_event_get_user_data(e);
  pickVisualizer(((shownIndex < 0 ? 0 : shownIndex) + step + n) % n);
}

void showAuto(int secs) {
  setChecked(autoBtn, autoOn);
  iconFollows(autoIcon, autoBtn);
  char text[16];
  if (autoOn && secs >= 0) {
    snprintf(text, sizeof(text), "Auto %d:%02d", secs / 60, secs % 60);
  } else {
    snprintf(text, sizeof(text), "Auto");
  }
  setText(autoLabel, text);
}

void tickClock() {
  if (!rotateRunning) return;
  int32_t left = (int32_t)(rotateAtMs - millis());
  showAuto(left > 0 ? (left + 999) / 1000 : 0);
}

void showRotation() {
  if (recentlyTouched(rotateTouched)) return;
  autoOn = live.autoRotate;
  bool running = live.autoRotate && !std::isnan(live.autoNext);
  if (running) {
    uint32_t at = live.rxMillis + (uint32_t)std::max(0.0, (live.autoNext - live.now) * 1000);
    // Not jumping with each poll's delay.
    if (!rotateRunning || abs((int32_t)(at - rotateAtMs)) > 1500) rotateAtMs = at;
  }
  rotateRunning = running;
  if (running) {
    tickClock();
  } else {
    showAuto(-1);
  }
}

void onAuto(lv_event_t*) {
  rotateTouched = millis();
  autoOn = !autoOn;
  rotateRunning = false;
  showAuto(-1);
  client->command(autoOn ? "/visualizer/auto_rotate/on" : "/visualizer/auto_rotate/off");
}

void openBrightness(lv_event_t* = nullptr);
void showBrightnessSheet();

void showBrightnessValue(int pct) {
  brightPct = pct;
  char text[16];
  snprintf(text, sizeof(text), "Pixels %d%%", pct);
  setText(ledLabel, text);
  showBrightnessSheet();
}

void showBrightness() {
  if (recentlyTouched(brtTouched)) return;
  showBrightnessValue((int)lroundf(live.brightness * 100));
}

lv_obj_t* footButton(lv_obj_t* parent, const lv_image_dsc_t* src, lv_obj_t** img, lv_obj_t** text, lv_event_cb_t cb) {
  lv_obj_t* b = button(parent, &stBtn);
  lv_obj_add_style(b, &stLit, LV_STATE_CHECKED);
  lv_obj_set_height(b, LV_PCT(100));
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  center(b);
  lv_obj_set_style_pad_column(b, 8, 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
  *img = icon(b, src);
  *text = lv_label_create(b);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void openPicker(lv_event_t* = nullptr);

void buildLedsPanel(lv_obj_t* p) {
  // ◀ [the visualizer] ▶; the visualizer opens the picker
  lv_obj_t* hero = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(hero, LV_PCT(100), 56);
  lv_obj_set_style_pad_column(hero, 6, 0);
  for (int step : {-1, 0, 1}) {
    lv_obj_t* b = button(hero, &stBtn);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    center(b);
    if (step == 0) {
      lv_obj_add_style(b, &stLit, 0);
      lv_obj_set_flex_grow(b, 1);
      lv_obj_set_style_pad_row(b, 2, 0);
      lv_obj_set_style_pad_hor(b, 4, 0);
      label(b, "VISUALIZER", &lv_font_montserrat_10, kMuted);
      visName = label(b, "", &lv_font_montserrat_16, kAccent);
      lv_obj_set_width(visName, LV_PCT(100));
      lv_obj_set_style_text_align(visName, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(visName, LV_LABEL_LONG_DOT);
      lv_obj_t* grid = icon(b, &icon_grid, kAccentLine);  // there's more: the picker
      lv_obj_add_flag(grid, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_image_set_scale(grid, 128);
      lv_obj_align(grid, LV_ALIGN_TOP_RIGHT, 6, -3);
      lv_obj_add_event_cb(b, openPicker, LV_EVENT_CLICKED, nullptr);
    } else {
      lv_obj_set_width(b, 44);
      icon(b, step < 0 ? &icon_left : &icon_right, kText2);
      lv_obj_add_event_cb(b, onStep, LV_EVENT_CLICKED, (void*)(intptr_t)step);
    }
  }

  // a stepping visualizer's steps (Flight Category Forecast): Now, +3h, +6h, +12h; hidden for the others
  stepRow = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(stepRow, LV_PCT(100), 24);
  lv_obj_set_style_pad_column(stepRow, 4, 0);
  lv_obj_add_flag(stepRow, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < kMaxSteps; i++) {
    stepChip[i] = button(stepRow, &stBtn);
    lv_obj_add_style(stepChip[i], &stLit, LV_STATE_CHECKED);
    lv_obj_remove_flag(stepChip[i], LV_OBJ_FLAG_CLICKABLE);  // it only says where the Chart is
    lv_obj_set_height(stepChip[i], LV_PCT(100));
    lv_obj_set_flex_grow(stepChip[i], 1);
    lv_obj_set_flex_flow(stepChip[i], LV_FLEX_FLOW_COLUMN);
    center(stepChip[i]);
    lv_obj_t* text = lv_label_create(stepChip[i]);  // takes the chip's text color, lit or not
    lv_label_set_text(text, "");
    lv_obj_set_style_text_font(text, &lv_font_montserrat_12, 0);
  }

  // what the LEDs mean, two columns (scrolls only if a visualizer has more than fit)
  // a line above it, on its own, so the entries scroll out of sight under it rather than over it
  lv_obj_t* line = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(line, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(line, hex(kLine), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  legendBox = box(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(legendBox, LV_PCT(100));
  lv_obj_set_flex_grow(legendBox, 1);
  lv_obj_set_style_pad_row(legendBox, 3, 0);
  lv_obj_add_flag(legendBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(legendBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scroll_dir(legendBox, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(legendBox, LV_SCROLLBAR_MODE_AUTO);  // a thin bar says there's more
  for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
    lv_obj_add_event_cb(legendBox, onLegendTouch, code, nullptr);
  lv_timer_create(tickLegend, kLegendTickMs, nullptr);

  // Auto (with the countdown to the next visualizer) and LED (opens the brightness sheet)
  lv_obj_t* foot = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(foot, LV_PCT(100), 44);
  lv_obj_set_style_pad_column(foot, 6, 0);
  autoBtn = footButton(foot, &icon_rotate, &autoIcon, &autoLabel, onAuto);
  ledBtn = footButton(foot, &icon_sun, &ledIcon, &ledLabel, openBrightness);
  showAuto(-1);
  iconFollows(ledIcon, ledBtn);
  setText(ledLabel, "Pixels");
}

// ---- a message at the bottom of the screen for a moment, over everything ("Couldn't change Wind") -----------------

lv_obj_t* toast = nullptr;
lv_timer_t* toastTimer = nullptr;

void hideToast() {
  if (toast) lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
}

// One line, at most about 270 px of 14 px text ("Can't skip the last one in rotation"); longer wraps.
void say(const std::string& text) {
  if (!toast) {
    toast = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(toast, hex(kSurface2), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, hex(kAccentLine), 0);
    lv_obj_set_style_radius(toast, 10, 0);
    lv_obj_set_style_pad_hor(toast, 12, 0);
    lv_obj_set_style_pad_ver(toast, 7, 0);
    lv_obj_set_style_text_font(toast, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(toast, hex(kText), 0);
    lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(toast, kW - 24, 0);
    lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  setText(toast, text);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
  if (toastTimer) lv_timer_delete(toastTimer);
  toastTimer = lv_timer_create(
      [](lv_timer_t*) {
        toastTimer = nullptr;
        hideToast();
      },
      kToastMs, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}

// ---- the visualizer picker: every visualizer at once, a tile each (its icon and short name), and Close ---------
// Press and hold a tile to leave that visualizer out of auto-rotate, or put it back (the Cockpit's hold); a skipped
// one's icon is dimmed, with a SKIP badge, and the hold plays a pop (up to skip, down to put back): that's all a hold
// that works says. A message shows only for a problem. What's skipped is the Chart's (/api/live's autoRotate.skip): the
// Copilot keeps nothing.

lv_obj_t* picker = nullptr;
std::vector<lv_obj_t*> pickTiles;   // by visualizer index
std::vector<lv_obj_t*> pickBadges;  // by visualizer index: SKIP, while auto-rotate leaves it out
std::string pickerKey;              // the visualizers the tiles were built for
lv_timer_t* pickerTimer = nullptr;

skips::Marks skipMarks;   // what's marked skipped: the Chart's list, and holds waiting for its answer (skips.h)
uint32_t skipMarked = 0;  // skipMarks.revision() the tiles show
SkipAnswer skipAnswer;
uint32_t skipRev = 0;

void closePicker(lv_event_t* = nullptr) {
  if (pickerTimer) {
    lv_timer_delete(pickerTimer);
    pickerTimer = nullptr;
  }
  lv_obj_add_flag(picker, LV_OBJ_FLAG_HIDDEN);
  hideToast();
}

bool pickerOpen() { return picker && !lv_obj_has_flag(picker, LV_OBJ_FLAG_HIDDEN); }

// The tiles' marks, as skipMarks has them. An exclusive one (Rainbow, Chase) is never marked: auto-rotate leaves it
// out anyway, whatever the list says.
void markSkipped() {
  skipMarked = skipMarks.revision();
  for (size_t i = 0; i < pickTiles.size() && i < status.visualizers.size(); i++) {
    const Visualizer& v = status.visualizers[i];
    bool skipped = skipMarks.known() && !v.exclusive && skipMarks.isSkipped(v.id);
    lv_obj_set_style_image_opa(lv_obj_get_child(pickTiles[i], 0), skipped ? LV_OPA_40 : LV_OPA_COVER, 0);
    showHidden(pickBadges[i], skipped);
  }
}

void showMarks() {
  if (skipMarks.revision() != skipMarked) markSkipped();
}

// What the Chart says is skipped: /api/live's autoRotate.skip (every 1.5 s), else /api/status's skipped; neither
// from a Chart before them, and no marks then
void showSkips() {
  bool known = live.valid && live.hasSkip;
  std::vector<std::string> ids;
  if (known) {
    ids = live.skip;
  } else if (status.hasSkip) {
    known = true;
    for (const Visualizer& v : status.visualizers) {
      if (v.skipped) ids.push_back(v.id);
    }
  }
  skipMarks.polled(known, ids, millis());
  showMarks();
}

std::string visShortName(const std::string& id) {
  for (const Visualizer& v : status.visualizers) {
    if (v.id == id) return v.shortName.empty() ? v.name : v.shortName;
  }
  return id;
}

void onHold(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= (int)status.visualizers.size()) return;
  const Visualizer& v = status.visualizers[i];
  switch (skipMarks.hold(v.exclusive)) {
    case skips::Hold::OldChart:
      say("Update the Chart to skip visualizers");
      return;
    case skips::Hold::Exclusive:
      say(visShortName(v.id) + ": always left out");
      return;
    case skips::Hold::Ask:
      break;
  }
  bool skip = !skipMarks.isSkipped(v.id);
  uint32_t seq = client->requestSkip(v.id, skip);
  if (!seq) {  // not queued: nothing to wait for, nothing marked
    say("Couldn't change auto-rotate");
    return;
  }
  audio_play(skip ? sound_hi_pop : sound_lo_pop, stream_volume());
  skipMarks.asked(seq, v.id, skip, millis());  // marked at once; the Chart's answer says what it is
  showMarks();
}

// The Chart's answer to a hold (or a dropped one's failure): the marks follow it, and while the picker is open a
// message says what went wrong. One that worked says nothing: the marks (and the hold's pop) show it
void showSkipAnswer() {
  const SkipAnswer& a = skipAnswer;
  skips::Said said = skipMarks.answered(a, millis());
  showMarks();
  if (!pickerOpen()) return;
  std::string name = visShortName(a.id);
  switch (said) {
    case skips::Said::Skipped:         // the dimmed icon and SKIP badge show it
    case skips::Said::BackInRotation:  // as does their going
      break;
    case skips::Said::Last:
      say("Can't skip the last one in rotation");
      break;
    case skips::Said::Exclusive:
      say(name + ": always left out");
      break;
    case skips::Said::Failed:
      say("Couldn't change " + name);
      break;
    case skips::Said::Nothing:
      break;
  }
}

// Holds whose answer never came (the Wi-Fi went): their marks undone, and said while the picker is open
void expireSkips() {
  if (!skipMarks.waiting()) return;
  std::vector<std::string> late = skipMarks.expire(millis());
  showMarks();
  if (!late.empty() && pickerOpen()) say("Couldn't change " + visShortName(late.back()));
}

void markPicked(int index) {
  for (size_t i = 0; i < pickTiles.size(); i++) {
    setChecked(pickTiles[i], (int)i == index);
    iconFollows(lv_obj_get_child(pickTiles[i], 0), pickTiles[i]);
  }
}

// A tap on tile `index`: the Chart switches to it, and the pick shows lit for a moment, then the legend
void tapPick(int index) {
  pickVisualizer(index);
  if (pickerTimer) lv_timer_delete(pickerTimer);
  pickerTimer = lv_timer_create(
      [](lv_timer_t*) {
        pickerTimer = nullptr;
        closePicker();
      },
      kPickedMs, nullptr);
  lv_timer_set_repeat_count(pickerTimer, 1);
}

void onPick(lv_event_t* e) { tapPick((int)(intptr_t)lv_event_get_user_data(e)); }

lv_obj_t* pickTile(const lv_image_dsc_t* src, const char* text, int tileW) {
  lv_obj_t* t = button(picker, &stBtn);
  lv_obj_add_style(t, &stLit, LV_STATE_CHECKED);
  lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
  center(t);
  lv_obj_set_style_pad_row(t, 5, 0);
  lv_obj_set_style_pad_hor(t, 2, 0);
  lv_obj_t* img = icon(t, src ? src : &icon_grid);
  if (!src) lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);  // a visualizer whose icon this firmware doesn't know
  lv_obj_t* l = lv_label_create(t);                    // in the tile's text color, so it lights up with it
  lv_label_set_text(l, text);
  // a map without short names sends only the full ones: smaller if it doesn't fit, so "Precipitation" doesn't
  // break mid-word
  bool fits = lv_text_get_width(text, strlen(text), &lv_font_montserrat_12, 0) <= tileW - 6;
  lv_obj_set_style_text_font(l, fits ? &lv_font_montserrat_12 : &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return t;
}

void buildPicker() {
  lv_obj_clean(picker);
  pickTiles.clear();
  pickBadges.clear();
  int tiles = (int)status.visualizers.size() + 1;  // and Close
  int rows = std::max(1, (tiles + 3) / 4);
  int tileW = (kW - 2 * 4 - 3 * kTileGap) / 4;
  int tileH = (kH - 2 * 4 - (rows - 1) * kTileGap) / rows;
  for (size_t i = 0; i < status.visualizers.size(); i++) {
    const Visualizer& v = status.visualizers[i];
    lv_obj_t* t = pickTile(vis_icon(v.icon.c_str()), v.shortName.c_str(), tileW);
    lv_obj_set_size(t, tileW, tileH);
    if (v.exclusive) lv_obj_set_style_border_color(t, hex(kEffect), 0);  // it takes the whole strip
    // a tap picks it; a hold skips it instead: LVGL sends SHORT_CLICKED only for a release before the hold
    // (CLICKED comes after a hold's release too)
    lv_obj_add_event_cb(t, onPick, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(t, onHold, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
    lv_obj_t* badge = label(t, "SKIP", &lv_font_montserrat_10, kText);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(badge, hex(kOffLed), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_style_pad_hor(badge, 3, 0);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 0, 3);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    pickTiles.push_back(t);
    pickBadges.push_back(badge);
  }
  lv_obj_t* close = pickTile(&icon_close, "Close", tileW);
  lv_obj_set_size(close, tileW, tileH);
  lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(close, hex(kLine), 0);
  lv_obj_add_event_cb(close, closePicker, LV_EVENT_CLICKED, nullptr);
  markPicked(shownIndex);
  markSkipped();
}

void showPickerTiles() {
  std::string key;
  for (const Visualizer& v : status.visualizers) key += v.id + "|" + v.shortName + "|" + v.icon + ",";
  if (key == pickerKey) return;
  pickerKey = key;
  buildPicker();
}

void openPicker(lv_event_t*) {
  markPicked(shownIndex);
  lv_obj_remove_flag(picker, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(picker);
}

void buildPickerScreen(lv_obj_t* scr) {
  picker = box(scr, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_size(picker, kW, kH);
  lv_obj_set_style_bg_color(picker, hex(kBg), 0);
  lv_obj_set_style_bg_opa(picker, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(picker, 4, 0);
  lv_obj_set_style_pad_gap(picker, kTileGap, 0);
  lv_obj_add_flag(picker, LV_OBJ_FLAG_CLICKABLE);  // nothing under it takes a touch
  lv_obj_add_flag(picker, LV_OBJ_FLAG_HIDDEN);
}

// ---- the brightness sheet: a thick slider, - and +, and presets ------------------------------------------------

Sheet bright;
lv_obj_t *brtSlider, *brtValue;
lv_obj_t* brtPresets[4];

void closeBrightness(lv_event_t* e = nullptr) {
  if (e && lv_event_get_target(e) != lv_event_get_current_target(e)) return;  // a tap on the card
  lv_obj_add_flag(bright.backdrop, LV_OBJ_FLAG_HIDDEN);
}

void setBrightness(int pct, bool send) {
  pct = std::max(0, std::min(100, pct));
  brtTouched = millis();
  showBrightnessValue(pct);
  if (send) {
    char path[32];
    snprintf(path, sizeof(path), "/brightness/%.2f", pct / 100.0);
    client->command(path);
  }
}

void showBrightnessSheet() {
  if (!bright.card || brightPct < 0) return;
  if (!lv_obj_has_state(brtSlider, LV_STATE_PRESSED)) lv_slider_set_value(brtSlider, brightPct, LV_ANIM_OFF);
  char text[8];
  snprintf(text, sizeof(text), "%d%%", brightPct);
  setText(brtValue, text);
  for (int i = 0; i < 4; i++) setChecked(brtPresets[i], kPresets[i] == brightPct);
}

void openBrightness(lv_event_t*) {
  showBrightnessSheet();
  lv_obj_remove_flag(bright.backdrop, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(bright.backdrop);
}

void onBrightSlider(lv_event_t* e) {
  setBrightness(lv_slider_get_value(brtSlider), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

void onBrightStep(lv_event_t* e) {
  int step = (int)(intptr_t)lv_event_get_user_data(e);
  int now = brightPct < 0 ? 0 : brightPct;
  // to the next multiple of the step, so 47 goes to 50 or 45
  int next =
      step > 0 ? (now / kBrightStep + 1) * kBrightStep : ((now + kBrightStep - 1) / kBrightStep - 1) * kBrightStep;
  setBrightness(next, true);
}

void onBrightPreset(lv_event_t* e) { setBrightness((int)(intptr_t)lv_event_get_user_data(e), true); }

void buildBrightness(lv_obj_t* scr) {
  bright = sheet(scr, 10, 30, 300, 180, closeBrightness);
  lv_obj_t* card = bright.card;
  lv_obj_t* head = box(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(head, 220, 28);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 8, 0);
  icon(head, &icon_sun, kAccent);
  label(head, "Pixel brightness", &lv_font_montserrat_14, kText);
  brtValue = label(head, "", &lv_font_montserrat_16, kAccent);
  lv_obj_set_flex_grow(brtValue, 1);
  lv_obj_set_style_text_align(brtValue, LV_TEXT_ALIGN_RIGHT, 0);
  closeButton(card, [](lv_event_t*) { closeBrightness(); });

  lv_obj_t* row = box(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), 44);
  lv_obj_set_y(row, 40);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 14, 0);
  for (int step : {-1, 1}) {
    lv_obj_t* b = button(row, &stBtn);
    lv_obj_set_size(b, 44, 44);
    lv_obj_center(label(b, step < 0 ? LV_SYMBOL_MINUS : LV_SYMBOL_PLUS, &lv_font_montserrat_16, kText2));
    lv_obj_add_event_cb(b, onBrightStep, LV_EVENT_CLICKED, (void*)(intptr_t)step);
    if (step < 0) {
      brtSlider = lv_slider_create(row);  // between - and +
      lv_obj_set_flex_grow(brtSlider, 1);
      lv_obj_set_height(brtSlider, 14);
      lv_slider_set_range(brtSlider, 0, 100);
      lv_obj_set_style_bg_color(brtSlider, hex(kControl), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(brtSlider, LV_OPA_COVER, LV_PART_MAIN);  // the theme leaves the track clear here
      lv_obj_set_style_radius(brtSlider, 7, LV_PART_MAIN);
      lv_obj_set_style_bg_color(brtSlider, hex(kAccent), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(brtSlider, LV_OPA_COVER, LV_PART_INDICATOR);
      lv_obj_set_style_radius(brtSlider, 7, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(brtSlider, lv_color_white(), LV_PART_KNOB);
      lv_obj_set_style_border_color(brtSlider, hex(kAccent), LV_PART_KNOB);
      lv_obj_set_style_border_width(brtSlider, 3, LV_PART_KNOB);
      lv_obj_set_style_pad_all(brtSlider, 8, LV_PART_KNOB);  // a 30 px knob
      lv_obj_set_ext_click_area(brtSlider, 16);
      lv_obj_add_event_cb(brtSlider, onBrightSlider, LV_EVENT_VALUE_CHANGED, nullptr);
      lv_obj_add_event_cb(brtSlider, onBrightSlider, LV_EVENT_RELEASED, nullptr);
    }
  }

  lv_obj_t* presets = box(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(presets, LV_PCT(100), 48);
  lv_obj_align(presets, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_column(presets, 6, 0);
  for (int i = 0; i < 4; i++) {
    lv_obj_t* b = button(presets, &stBtn);
    lv_obj_add_style(b, &stLit, LV_STATE_CHECKED);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text_fmt(l, "%d%%", kPresets[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, onBrightPreset, LV_EVENT_CLICKED, (void*)(intptr_t)kPresets[i]);
    brtPresets[i] = b;
  }
}

// ---- the Airports panel: the airports in alphabetical order, a page of tiles at a time --------------------------

struct Tile {
  lv_obj_t* btn;
  lv_obj_t* dot;
  int pixel;
  std::string category;
  std::string code;
  bool isVirtual;
  bool atis;  // it has an ATIS to hear (the map's leds[].atis)
};
std::vector<Tile> tiles;  // alphabetical
std::string tileKey;      // the station codes and pixels the tiles were built for
lv_obj_t *grid, *strip;
int page = 0;
std::string stripKey;  // what the strip shows
double lastLocateAt = NAN;

int pageCount() { return pages::count((int)tiles.size(), kPerPage); }

void showStrip();

void showPage(int which) {
  page = std::max(0, std::min(which, pageCount() - 1));
  for (size_t i = 0; i < tiles.size(); i++) showHidden(tiles[i].btn, pages::of((int)i, kPerPage) == page);
  showStrip();
}

void onPageButton(lv_event_t* e) {
  int to = (int)(intptr_t)lv_event_get_user_data(e);
  if (to >= 0) showPage(to);
}

void showStrip() {
  std::vector<std::string> firsts;
  for (size_t i = 0; i < tiles.size(); i += kPerPage) firsts.push_back(tiles[i].code);
  std::vector<pages::Button> buttons = pages::strip(firsts, page, kStripFit);
  std::string key = std::to_string(page) + ":";
  for (const std::string& f : firsts) key += f + ",";
  if (key == stripKey) return;
  stripKey = key;
  lv_obj_clean(strip);
  for (const pages::Button& pb : buttons) {
    lv_obj_t* b = button(strip, &stBtn);
    lv_obj_add_style(b, &stLit, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, hex(kSunken), 0);
    lv_obj_set_style_border_color(b, hex(kLine), 0);
    lv_obj_set_style_text_color(b, hex(kMuted), 0);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_center(l);
    switch (pb.kind) {
      case pages::Button::Page:
        lv_label_set_text(l, pb.label.c_str());
        setChecked(b, pb.page == page);
        break;
      case pages::Button::Position:
        lv_label_set_text(l, pb.label.c_str());
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_grow(b, 2);
        break;
      default:
        lv_label_set_text(l, pb.kind == pages::Button::Previous ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        enable(b, pb.page >= 0);
        break;
    }
    lv_obj_add_event_cb(b, onPageButton, LV_EVENT_CLICKED, (void*)(intptr_t)pb.page);
  }
  showHidden(strip, !buttons.empty());
}

// A swipe on the tiles turns the page; the touch then isn't a tap on the tile it began on.
void onSwipe(lv_event_t*) {
  lv_indev_t* indev = lv_indev_active();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT) {
    showPage(page + 1);
  } else if (dir == LV_DIR_RIGHT) {
    showPage(page - 1);
  } else {
    return;
  }
  lv_indev_wait_release(indev);
}

void openCard(int tileIndex);

void onTile(lv_event_t* e) { openCard((int)(intptr_t)lv_event_get_user_data(e)); }

void unflash(lv_timer_t* t) {
  size_t i = (size_t)(uintptr_t)lv_timer_get_user_data(t);
  if (i >= tiles.size()) return;
  lv_obj_set_style_bg_color(tiles[i].btn, hex(kSurface2), 0);
  lv_obj_set_style_text_color(tiles[i].btn, hex(kText), 0);
}

bool cardOpen();

// The tile the map just pulsed flashes white, like the /remote page's, on its page.
void flashTile(int pixel) {
  for (size_t i = 0; i < tiles.size(); i++) {
    if (tiles[i].pixel != pixel) continue;
    if (!cardOpen()) showPage(pages::of((int)i, kPerPage));
    lv_obj_set_style_bg_color(tiles[i].btn, lv_color_white(), 0);
    lv_obj_set_style_text_color(tiles[i].btn, hex(kBg), 0);
    lv_timer_t* t = lv_timer_create(unflash, 700, (void*)(uintptr_t)i);
    lv_timer_set_repeat_count(t, 1);
  }
}

void buildTiles() {
  lv_obj_clean(grid);
  tiles.clear();
  std::vector<std::string> codes;
  for (const Station& s : live.stations) codes.push_back(s.code);
  int tileW = (kPanelW - 2 * kPad - (kTileCols - 1) * kTileGap) / kTileCols;
  int tileH = (kH - 2 * kPad - kStripH - kPad - (kTileRows - 1) * kTileGap) / kTileRows;
  for (int at : pages::alphabetical(codes)) {
    const Station& s = live.stations[at];
    lv_obj_t* b = button(grid, &stTile);
    lv_obj_set_size(b, tileW, tileH);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    center(b);
    lv_obj_set_style_pad_column(b, 7, 0);
    if (s.isVirtual) {  // no LED: the page dashes these
      lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_color(b, hex(kNone), 0);
    }
    lv_obj_t* d = dot(b, 10, categoryColor(s.category));
    lv_label_set_text(lv_label_create(b), s.code.c_str());
    lv_obj_add_event_cb(b, onTile, LV_EVENT_CLICKED, (void*)(intptr_t)tiles.size());
    tiles.push_back({b, d, s.pixel, s.category, s.code, s.isVirtual, s.atis});
  }
  stripKey.clear();
  showPage(page);
}

void showCard();

void showStations() {
  std::string key;
  for (const Station& s : live.stations) key += s.code + ":" + std::to_string(s.pixel) + (s.isVirtual ? "v," : ",");
  if (key != tileKey) {
    tileKey = key;
    buildTiles();
  }
  for (const Station& s : live.stations) {
    for (Tile& t : tiles) {
      if (t.pixel != s.pixel) continue;
      if (t.category != s.category) {
        t.category = s.category;
        lv_obj_set_style_bg_color(t.dot, hex(categoryColor(s.category)), 0);
      }
      t.atis = s.atis;
    }
  }
  // Locate can't show while a whole-strip visualizer (Rainbow, Chase) runs
  lv_obj_set_style_opa(grid, live.exclusive ? LV_OPA_50 : LV_OPA_COVER, 0);
  showCard();

  if (!std::isnan(live.locateAt) && live.locateAt != lastLocateAt) {
    // Not one from before the remote started.
    bool fresh = !std::isnan(lastLocateAt) || live.now - live.locateAt < 2;
    lastLocateAt = live.locateAt;
    if (fresh) flashTile(live.locatePixel);
  }
}

void buildAirportsPanel(lv_obj_t* p) {
  grid = box(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_style_pad_gap(grid, kTileGap, 0);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_CLICKABLE);  // a swipe between the tiles turns the page too
  lv_obj_add_event_cb(grid, onSwipe, LV_EVENT_GESTURE, nullptr);

  strip = box(p, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(strip, LV_PCT(100), kStripH);
  lv_obj_set_style_pad_column(strip, 5, 0);
  lv_obj_add_flag(strip, LV_OBJ_FLAG_HIDDEN);
}

// ---- the airport card: the airport, and Locate / AWOS / ATIS; an ATIS's lines as it's read --------------------

Sheet card;
lv_obj_t *cardDot, *cardCode, *cardTag, *cardName, *cardStatus, *cardMetar;
lv_obj_t *cardActions, *locateBtn, *awosBtn, *atisBtn;
lv_obj_t *volRow, *volSlider, *volLabel;
lv_obj_t* transcript = nullptr;  // the ATIS being read, line by line, in the FAA's text
// The tile whose card is open, or -1. Not always a tile: buildTiles() (showStations(), when the Chart's airports
// change) rebuilds `tiles` with the card left open, so it can be past the end. Read it through cardAirport() (nullptr
// then), never tiles[cardTile]; the remote control checks it with ui_control::checkOpenCard() first.
int cardTile = -1;
uint32_t cardLocatedAt = 0;  // when the card last asked the Chart to pulse the pixel (0: not yet)
std::vector<lv_obj_t*> txRows;
int txLit = -1;       // the line being said
bool txOpen = false;  // the transcript is on screen (it stays after the end, to read; a tap closes it)
AtisTranscript atis;  // the lines, from the map
uint32_t atisRev = 0;
StreamState radio;
uint32_t radioRev = 0;

bool cardOpen() { return card.backdrop && !lv_obj_has_flag(card.backdrop, LV_OBJ_FLAG_HIDDEN); }

const Tile* cardAirport() { return cardTile >= 0 && cardTile < (int)tiles.size() ? &tiles[cardTile] : nullptr; }

bool speakingNow() {
  return !radio.speaking.empty() && radio.phase != StreamState::Off && radio.phase != StreamState::Failed;
}

bool awosAvailable() { return stream_available() && live.speech; }

// What the card's airport is saying now: 1 its AWOS, 2 its ATIS, 0 nothing
int cardSpeaking() {
  const Tile* t = cardAirport();
  if (!t || !speakingNow() || radio.speaking != t->code) return 0;
  return radio.atis ? 2 : 1;
}

// The line being said stands out, the others dim; with none (before or after the reading) they're all there to read.
void lightLine(int i) {
  if (i == txLit) return;
  txLit = i;
  for (int n = 0; n < (int)txRows.size(); n++) {
    bool lit = n == i;
    lv_obj_set_style_bg_opa(txRows[n], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(txRows[n], hex(lit ? kAccent : kSurface), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(txRows[n], 0), hex(lit || i < 0 ? kText : kMuted), 0);
  }
  if (i >= 0 && i < (int)txRows.size()) lv_obj_scroll_to_view(txRows[i], LV_ANIM_ON);
}

void openTranscript() {
  lv_obj_clean(transcript);
  txRows.clear();
  txLit = -1;
  for (const AtisLine& line : atis.lines) {
    lv_obj_t* row = box(transcript, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_bg_color(row, hex(kAccentSoft), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 3, 0);
    lv_obj_set_style_border_color(row, hex(kSurface), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);  // a tap on a line is a tap on the transcript
    // the FAA's text; "No ATIS is available" has only the words said
    const std::string& text = line.raw.empty() ? line.said : line.raw;
    lv_obj_t* l = label(row, text.c_str(), &lv_font_montserrat_12, kText);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    txRows.push_back(row);
  }
  lv_obj_scroll_to_y(transcript, 0, LV_ANIM_OFF);
  txOpen = true;
  showCard();
}

void closeTranscript() {
  if (!txOpen) return;
  txOpen = false;
  lv_obj_clean(transcript);
  txRows.clear();
  txLit = -1;
  showCard();
}

// While it plays: the lines show once the sound starts (the words wait for it, as on the web pages), and the line
// being said follows it. After the end the text stays, to read, until a tap.
void followTranscript() {
  const Tile* t = cardAirport();
  if (!t || !cardOpen()) return;
  bool mine = radio.atis && radio.speaking == t->code && atis.valid && atis.code == t->code;
  bool sounding = radio.phase == StreamState::Playing && radio.playedMs > 0;
  if (!txOpen && mine && sounding && !atis.lines.empty()) openTranscript();
  if (txOpen) lightLine(speakingNow() ? atisLineAt(atis.lines, radio.playedMs, radio.totalMs) : -1);
}

void onTranscript(lv_event_t*) {
  if (cardSpeaking()) {
    stream_set_playing(false);  // tap to stop; the text stays
  } else {
    closeTranscript();  // and again: back to the METAR
  }
}

// What the card says above the METAR: the speaker's progress, else what Locate did
std::string cardLine(const Tile& t) {
  char text[96] = "";
  const char* what = radio.atis ? "ATIS" : "AWOS";
  bool mine = !radio.speaking.empty() && radio.speaking == t.code;
  if (mine && radio.phase == StreamState::Failed) {
    snprintf(text, sizeof(text), "%s", radio.message.c_str());
  } else if (mine && radio.phase == StreamState::Playing) {
    snprintf(text, sizeof(text), txOpen ? "Reading the ATIS - tap the text to stop" : "Playing the %s", what);
  } else if (mine && speakingNow()) {
    snprintf(text, sizeof(text), "Getting the %s...", what);
  } else if (txOpen) {
    snprintf(text, sizeof(text), "The ATIS - tap the text to go back");
  } else if (live.exclusive) {
    snprintf(text, sizeof(text), "Locate is paused by %s", live.activeName.c_str());
  } else if (cardLocatedAt && millis() - cardLocatedAt < kPulseNoteMs) {  // only while it's pulsing
    snprintf(text, sizeof(text), t.isVirtual ? "Pulsing on the web map" : "Pixel pulsing on the Chart");
  }
  return text;
}

void actionButton(lv_obj_t* b, const lv_image_dsc_t* src, const char* text) {
  lv_obj_t* img = lv_obj_get_child(b, 0);
  if (lv_image_get_src(img) != src) lv_image_set_src(img, src);
  setText(lv_obj_get_child(b, 1), text);
  iconFollows(img, b);
}

void showCard() {
  const Tile* t = cardAirport();
  if (!t || !cardOpen()) return;
  lv_obj_set_style_bg_color(cardDot, hex(categoryColor(t->category)), 0);
  setText(cardCode, t->code);
  showHidden(cardTag, !t->category.empty());
  setText(cardTag, t->category);
  lv_obj_set_style_text_color(cardTag, hex(categoryColor(t->category)), 0);
  std::string name, raw;
  for (const AirportInfo& a : status.airports) {
    if (a.code == t->code) {
      name = a.name;
      raw = a.raw;
    }
  }
  setText(cardName, name);
  setText(cardMetar, raw.empty() ? "No METAR" : raw);
  std::string line = cardLine(*t);
  setText(cardStatus, line);
  showHidden(cardStatus, !line.empty());
  showHidden(cardMetar, !txOpen);
  showHidden(transcript, txOpen);
  showHidden(cardName, !txOpen && !name.empty());  // room for the ATIS's lines

  int speaking = cardSpeaking();
  bool audio = stream_available();
  showHidden(awosBtn, audio);
  showHidden(atisBtn, audio);
  enable(locateBtn, !live.exclusive);
  enable(awosBtn, awosAvailable());
  enable(atisBtn, awosAvailable() && t->atis);
  setChecked(awosBtn, speaking == 1);
  setChecked(atisBtn, speaking == 2);
  actionButton(locateBtn, &icon_locate, "Locate");
  actionButton(awosBtn, speaking == 1 ? &icon_stop : &icon_speaker, speaking == 1 ? "Stop" : "AWOS");
  actionButton(atisBtn, speaking == 2 ? &icon_stop : &icon_tower, speaking == 2 ? "Stop" : "ATIS");

  bool playing = radio.phase == StreamState::Connecting || radio.phase == StreamState::Buffering ||
                 radio.phase == StreamState::Playing || radio.phase == StreamState::Retrying;
  showHidden(volRow, playing);
}

void locate(const Tile& t) {
  if (live.exclusive) return;
  client->command("/locate/" + std::to_string(t.pixel));
  cardLocatedAt = millis() | 1;
  // the Chart pulses once (about a second): the note goes when it's over, and the METAR moves up
  lv_timer_t* over = lv_timer_create([](lv_timer_t*) { showCard(); }, kPulseNoteMs + 50, nullptr);
  lv_timer_set_repeat_count(over, 1);
}

void openCard(int tileIndex) {
  if (tileIndex < 0 || tileIndex >= (int)tiles.size()) return;
  if (cardSpeaking()) stream_set_playing(false);
  closeTranscript();
  cardTile = tileIndex;
  cardLocatedAt = 0;
  locate(tiles[tileIndex]);  // a tap finds it on the map at once
  lv_obj_remove_flag(card.backdrop, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(card.backdrop);
  showCard();
}

void closeCard(lv_event_t* e = nullptr) {
  if (e && lv_event_get_target(e) != lv_event_get_current_target(e)) return;  // a tap on the card
  if (cardSpeaking()) stream_set_playing(false);  // closing it stops it, as the map's popup does
  closeTranscript();
  lv_obj_add_flag(card.backdrop, LV_OBJ_FLAG_HIDDEN);
  cardTile = -1;
}

void onLocate(lv_event_t*) {
  const Tile* t = cardAirport();
  if (t) locate(*t);
  showCard();
}

// A tap on AWOS (or ATIS): reads it aloud, or stops it if it's the one playing (the button says Stop then)
void speak(bool wantAtis) {
  const Tile* t = cardAirport();
  if (!t) return;
  if (cardSpeaking() == (wantAtis ? 2 : 1)) {  // the one playing: stop
    stream_set_playing(false);
    return;
  }
  audio_click();
  closeTranscript();
  stream_speak(t->code, wantAtis);
  if (wantAtis) client->requestAtis(t->code);  // its lines, ready for when the sound starts
  showCard();
}

void onSpeak(lv_event_t* e) { speak((bool)(intptr_t)lv_event_get_user_data(e)); }

void onVolume(lv_event_t* e) {
  int pct = lv_slider_get_value(volSlider);
  lv_label_set_text_fmt(volLabel, "%d%%", pct);
  stream_set_volume(pct, lv_event_get_code(e) == LV_EVENT_RELEASED);  // heard at once; saved on release
}

// The card's slider follows a volume set elsewhere (the logo's splash), unless a finger is on it
void syncCardVolume() {
  if (!volSlider || lv_obj_has_state(volSlider, LV_STATE_PRESSED)) return;
  int pct = stream_volume();
  if (lv_slider_get_value(volSlider) == pct) return;
  lv_slider_set_value(volSlider, pct, LV_ANIM_OFF);
  lv_label_set_text_fmt(volLabel, "%d%%", pct);
}

lv_obj_t* cardAction(lv_obj_t* parent, const lv_image_dsc_t* src, const char* text, lv_event_cb_t cb, void* data) {
  lv_obj_t* b = button(parent, &stBtn);
  lv_obj_add_style(b, &stLit, LV_STATE_CHECKED);
  lv_obj_set_height(b, LV_PCT(100));
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  center(b);
  lv_obj_set_style_pad_row(b, 4, 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
  icon(b, src);
  lv_label_set_text(lv_label_create(b), text);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
  return b;
}

void buildCard(lv_obj_t* scr) {
  card = sheet(scr, 8, 8, kW - 16, kH - 16, closeCard);
  lv_obj_t* c = card.card;
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_set_style_pad_row(c, 4, 0);

  lv_obj_t* head = box(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(head, 220, 30);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 9, 0);
  cardDot = dot(head, 12, kNone);
  cardCode = label(head, "", &lv_font_montserrat_28, kText);
  cardTag = label(head, "", &lv_font_montserrat_12, kText);
  lv_obj_set_style_bg_color(cardTag, hex(kSunken), 0);
  lv_obj_set_style_bg_opa(cardTag, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(cardTag, 5, 0);
  lv_obj_set_style_pad_hor(cardTag, 7, 0);
  lv_obj_set_style_pad_ver(cardTag, 2, 0);
  closeButton(c, [](lv_event_t*) { closeCard(); });

  cardName = label(c, "", &lv_font_montserrat_12, kMuted);
  lv_obj_set_width(cardName, 230);
  lv_label_set_long_mode(cardName, LV_LABEL_LONG_DOT);

  cardStatus = label(c, "", &lv_font_montserrat_12, kAccent);
  lv_obj_set_width(cardStatus, LV_PCT(100));
  lv_label_set_long_mode(cardStatus, LV_LABEL_LONG_DOT);

  // the METAR, or the ATIS line by line while it's read
  cardMetar = label(c, "", &lv_font_montserrat_12, kText2);
  lv_obj_set_width(cardMetar, LV_PCT(100));
  lv_obj_set_flex_grow(cardMetar, 1);
  lv_label_set_long_mode(cardMetar, LV_LABEL_LONG_WRAP);
  transcript = box(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(transcript, LV_PCT(100));
  lv_obj_set_flex_grow(transcript, 1);
  lv_obj_set_style_pad_row(transcript, 2, 0);
  lv_obj_add_flag(transcript, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(transcript, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scroll_dir(transcript, LV_DIR_VER);
  lv_obj_add_event_cb(transcript, onTranscript, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(transcript, LV_OBJ_FLAG_HIDDEN);

  // the volume, while the speaker plays
  volRow = box(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(volRow, LV_PCT(100), 28);
  lv_obj_set_flex_align(volRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(volRow, 12, 0);
  lv_obj_set_style_pad_hor(volRow, 6, 0);
  label(volRow, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, kText2);
  volSlider = lv_slider_create(volRow);
  lv_obj_set_flex_grow(volSlider, 1);
  lv_obj_set_height(volSlider, 8);
  lv_slider_set_range(volSlider, 0, 100);
  lv_slider_set_value(volSlider, stream_volume(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(volSlider, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(volSlider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(volSlider, hex(kAccent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(volSlider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(volSlider, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(volSlider, 14);
  lv_obj_add_event_cb(volSlider, onVolume, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(volSlider, onVolume, LV_EVENT_RELEASED, nullptr);
  volLabel = label(volRow, "", &lv_font_montserrat_12, kMuted);
  lv_obj_set_width(volLabel, 34);
  lv_label_set_text_fmt(volLabel, "%d%%", stream_volume());
  lv_obj_add_flag(volRow, LV_OBJ_FLAG_HIDDEN);

  cardActions = box(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(cardActions, LV_PCT(100), 60);
  lv_obj_set_style_pad_column(cardActions, 8, 0);
  locateBtn = cardAction(cardActions, &icon_locate, "Locate", onLocate, nullptr);
  awosBtn = cardAction(cardActions, &icon_speaker, "AWOS", onSpeak, (void*)(intptr_t)false);
  atisBtn = cardAction(cardActions, &icon_tower, "ATIS", onSpeak, (void*)(intptr_t)true);
}

// ---- the rail: LEDs, Airports, and the logo (the splash; its dot is the map's connection) -----------------------

void showSplashOnDemand(lv_event_t* = nullptr);

void showTab(int which) {
  for (int i = 0; i < 2; i++) {
    setChecked(railButtons[i], i == which);
    iconFollows(lv_obj_get_child(railButtons[i], 0), railButtons[i]);
    showHidden(panels[i], i == which);
  }
}

void onTab(lv_event_t* e) { showTab((int)(intptr_t)lv_event_get_user_data(e)); }

void showConnection() {
  uint32_t color = !conn.wifi || !conn.online ? kIfr : kVfr;
  lv_obj_set_style_bg_color(logoDot, hex(color), 0);
  for (lv_obj_t* p : panels) lv_obj_set_style_opa(p, conn.online ? LV_OPA_COVER : LV_OPA_50, 0);
}

void buildRail(lv_obj_t* scr) {
  lv_obj_t* rail = box(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(rail, kRailW, kH);
  lv_obj_set_style_bg_color(rail, hex(kSurface), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(rail, 1, 0);
  lv_obj_set_style_border_color(rail, hex(kLine), 0);
  lv_obj_set_style_pad_ver(rail, 6, 0);
  lv_obj_set_style_pad_row(rail, 6, 0);
  lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const lv_image_dsc_t* icons[2] = {&icon_leds, &icon_pin};
  const char* names[2] = {"Pixels", "Airports"};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = button(rail, &stRail);
    lv_obj_add_style(b, &stRailOn, LV_STATE_CHECKED);
    lv_obj_set_size(b, kRailW - 8, 72);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    center(b);
    lv_obj_set_style_pad_row(b, 5, 0);
    icon(b, icons[i]);
    lv_label_set_text(lv_label_create(b), names[i]);
    lv_obj_add_event_cb(b, onTab, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    railButtons[i] = b;
  }

  lv_obj_t* spacer = box(rail, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(spacer, 1);

  // the logo: the splash screen (versions, the map's address, the firmware)
  lv_obj_t* logoBtn = button(rail, &stRail);
  lv_obj_set_size(logoBtn, kRailW - 8, 54);
  lv_obj_set_style_border_width(logoBtn, 1, 0);
  lv_obj_set_style_border_color(logoBtn, hex(kLine), 0);
  lv_obj_t* plane = icon(logoBtn, &icon_logo_plane);
  lv_obj_set_style_image_recolor_opa(plane, LV_OPA_TRANSP, 0);  // its own color
  lv_obj_center(plane);
  logoDot = dot(logoBtn, 8, kNone);  // where the logo's dot is: the map's connection
  lv_obj_align_to(logoDot, plane, LV_ALIGN_TOP_LEFT, 3, 4);
  lv_obj_add_flag(logoDot, LV_OBJ_FLAG_EVENT_BUBBLE);
  starIcon = lv_image_create(logoBtn);  // an update is available
  lv_image_set_src(starIcon, &star_icon);
  lv_obj_align(starIcon, LV_ALIGN_TOP_RIGHT, -3, 3);
  lv_obj_add_flag(starIcon, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(starIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(logoBtn, showSplashOnDemand, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t* panel(lv_obj_t* scr) {
  lv_obj_t* p = box(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(p, kPanelW, kH);
  lv_obj_set_x(p, kRailW);
  lv_obj_set_style_pad_all(p, kPad, 0);
  lv_obj_set_style_pad_row(p, kPad, 0);
  return p;
}

// ---- the splash screen -------------------------------------------------------------------------------------
//
// The logo, the name, the repo and the version, with the Wi-Fi and the map
// connecting; once both are OK it stays a moment, then the remote fades in.
// The rail's logo shows it again, with the map's address, the firmware
// screen (Update when there's one) and the speaker's volume, until Close. If the map is lost, it comes
// back, retrying, until the map answers again.

enum class SplashMode {
  Startup,    // until Wi-Fi and the map are OK, then 2 s more
  OnDemand,   // the logo: until Close (or 30 s)
  Reconnect,  // the map was lost: like Startup
};
lv_obj_t* splash = nullptr;
SplashMode splashMode = SplashMode::Startup;
lv_obj_t *wifiStep, *wifiState, *mapState, *mapVersion, *mapAddress, *updButtonSplash, *updLabelSplash;
lv_obj_t* repoLabel;  // the repo; on the logo's splash, the remote's web page once it's up
lv_obj_t* splashLogo = nullptr;
lv_obj_t* wifiSetupBtn = nullptr;  // once the remote can't join its Wi-Fi: Wi-Fi config mode
uint32_t splashStart = 0;
uint32_t connectedAt = 0;
// The logo's splash's volume: a speaker button at the top right; a tap opens a slider over the top of the splash
// (a backdrop under it closes it on a tap beside it, and it closes kVolumePopupMs after the last touch)
lv_obj_t *splashVolBtn = nullptr, *splashVolIcon = nullptr;
lv_obj_t *splashVolPopup = nullptr, *splashVolSlider = nullptr, *splashVolPct = nullptr, *splashVolPopIcon = nullptr;
lv_timer_t* splashVolTimer = nullptr;

void showUpdate();

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

// The splash is deleted (closed, or replaced by the update screen): forget its widgets, which the update
// check's answers would otherwise write to after they're gone.
void forgetSplash() {
  splash = nullptr;
  updButtonSplash = nullptr;
  updLabelSplash = nullptr;
  splashLogo = nullptr;
  wifiSetupBtn = nullptr;
  if (splashVolTimer) {  // the volume slider goes with the splash: its timer mustn't fire on it
    lv_timer_delete(splashVolTimer);
    splashVolTimer = nullptr;
  }
  splashVolBtn = splashVolIcon = nullptr;
  splashVolPopup = splashVolSlider = splashVolPct = splashVolPopIcon = nullptr;
}

const char* volumeSymbol(int pct) {
  switch (volume_ui::icon(pct)) {
    case volume_ui::Icon::Mute:
      return LV_SYMBOL_MUTE;
    case volume_ui::Icon::Mid:
      return LV_SYMBOL_VOLUME_MID;
    case volume_ui::Icon::Max:
      break;
  }
  return LV_SYMBOL_VOLUME_MAX;
}

// The splash's speaker symbols and slider follow the volume (also when the card's slider set it)
void syncSplashVolume() {
  if (!splashVolIcon) return;
  int pct = stream_volume();
  setText(splashVolIcon, volumeSymbol(pct));
  if (!splashVolSlider) return;
  setText(splashVolPopIcon, volumeSymbol(pct));
  if (!lv_obj_has_state(splashVolSlider, LV_STATE_PRESSED) && lv_slider_get_value(splashVolSlider) != pct) {
    lv_slider_set_value(splashVolSlider, pct, LV_ANIM_OFF);
  }
  setText(splashVolPct, std::to_string(lv_slider_get_value(splashVolSlider)) + "%");
}

void closeSplashVolume(lv_event_t* = nullptr) {
  if (splashVolTimer) {
    lv_timer_delete(splashVolTimer);
    splashVolTimer = nullptr;
  }
  if (splashVolPopup) lv_obj_add_flag(splashVolPopup, LV_OBJ_FLAG_HIDDEN);
}

// A touch on the slider: it stays open kVolumePopupMs more, and so does the splash (kSplashShowMs)
void keepSplashVolume() {
  splashStart = millis();
  if (splashVolTimer) {
    lv_timer_reset(splashVolTimer);
    return;
  }
  splashVolTimer = lv_timer_create(
      [](lv_timer_t*) {
        splashVolTimer = nullptr;  // a one-shot: LVGL deletes it after this
        closeSplashVolume();
      },
      kVolumePopupMs, nullptr);
  lv_timer_set_repeat_count(splashVolTimer, 1);
}

void openSplashVolume(lv_event_t*) {
  if (!splashVolPopup) return;
  lv_slider_set_value(splashVolSlider, stream_volume(), LV_ANIM_OFF);
  syncSplashVolume();
  lv_obj_remove_flag(splashVolPopup, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(splashVolPopup);
  keepSplashVolume();
}

// Heard at once (on a stream playing too); saved on release, with a quiet pop at the new level so it's heard (not at
// 0%, not with Sounds off, not over a stream: audio_play() leaves those out)
void onSplashVolume(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  keepSplashVolume();
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) return;
  int pct = lv_slider_get_value(splashVolSlider);
  bool released = code == LV_EVENT_RELEASED;
  stream_set_volume(pct, released);
  syncSplashVolume();
  if (released && volume_ui::popOnRelease(pct)) audio_play(sound_hi_pop, pct);
}

// The speaker button at the splash's top right, and its slider (hidden till a tap)
void buildSplashVolume(lv_obj_t* scr) {
  splashVolBtn = button(scr, &stBtn);
  lv_obj_set_size(splashVolBtn, 40, 40);
  lv_obj_add_flag(splashVolBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(splashVolBtn, LV_ALIGN_TOP_RIGHT, -8, 8);
  splashVolIcon = label(splashVolBtn, "", &lv_font_montserrat_16, kText2);
  lv_obj_center(splashVolIcon);
  lv_obj_add_event_cb(splashVolBtn, openSplashVolume, LV_EVENT_CLICKED, nullptr);

  splashVolPopup = lv_obj_create(scr);  // the backdrop: a tap on it closes the slider
  lv_obj_remove_style_all(splashVolPopup);
  lv_obj_add_flag(splashVolPopup, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(splashVolPopup, 0, 0);
  lv_obj_set_size(splashVolPopup, kW, kH);
  lv_obj_set_style_bg_color(splashVolPopup, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(splashVolPopup, LV_OPA_60, 0);
  lv_obj_add_flag(splashVolPopup, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(splashVolPopup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(splashVolPopup, closeSplashVolume, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* c = box(splashVolPopup, LV_FLEX_FLOW_ROW);  // over the top of the splash, the button included
  lv_obj_set_pos(c, 8, 8);
  lv_obj_set_size(c, kW - 16, 48);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(c, 14, 0);
  lv_obj_set_style_pad_column(c, 12, 0);
  lv_obj_set_style_bg_color(c, hex(kSurface), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, hex(kControl), 0);
  lv_obj_set_style_radius(c, 14, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // a tap on it isn't a tap on the backdrop
  splashVolPopIcon = label(c, "", &lv_font_montserrat_16, kText2);
  lv_obj_set_width(splashVolPopIcon, 20);
  splashVolSlider = lv_slider_create(c);  // as the card's
  lv_obj_set_flex_grow(splashVolSlider, 1);
  lv_obj_set_height(splashVolSlider, 8);
  lv_slider_set_range(splashVolSlider, 0, 100);
  lv_obj_set_style_bg_color(splashVolSlider, hex(kControl), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(splashVolSlider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(splashVolSlider, hex(kAccent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(splashVolSlider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(splashVolSlider, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(splashVolSlider, 14);
  for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED}) {
    lv_obj_add_event_cb(splashVolSlider, onSplashVolume, code, nullptr);
  }
  splashVolPct = label(c, "", &lv_font_montserrat_12, kMuted);
  lv_obj_set_width(splashVolPct, 34);
  lv_obj_add_flag(splashVolPopup, LV_OBJ_FLAG_HIDDEN);
  syncSplashVolume();
}

void hideSplash() {
  lv_screen_load_anim(remoteScreen, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, true);  // then deletes the splash
  forgetSplash();
}

void showSplashUpdate() {
  if (!splash || !updButtonSplash) return;
  bool available = upd.available;
  std::string text = available ? "Update " + upd.candidate : "Firmware";
  setText(updLabelSplash, text);
  lv_obj_set_style_bg_color(updButtonSplash, hex(available ? kStarSoft : kSurface2), 0);
  lv_obj_set_style_border_color(updButtonSplash, hex(available ? kStar : kControl), 0);
  lv_obj_set_style_text_color(updButtonSplash, hex(available ? kStar : kText2), 0);
}

void showSplash(SplashMode mode) {
  splashMode = mode;
  bool onDemand = mode == SplashMode::OnDemand;
  forgetSplash();
  splash = lv_obj_create(nullptr);
  lv_obj_remove_style_all(splash);
  lv_obj_set_style_bg_color(splash, hex(kBg), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(splash, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(splash, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(splash, 2, 0);

  lv_obj_t* img = splashLogo = lv_image_create(splash);
  lv_image_set_src(img, &logo);
  if (onDemand) {  // smaller, to make room for the buttons
    lv_image_set_scale(img, 176);
    lv_obj_set_size(img, 66, 66);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
  }
  label(splash, "MetarMap2 Copilot", &lv_font_montserrat_28, kText);
  repoLabel = label(splash, kRepo, &lv_font_montserrat_12, kMuted);
  std::string version = std::string("Copilot ") + FIRMWARE_VERSION;
  label(splash, version.c_str(), &lv_font_montserrat_12, kMuted);
  mapVersion = label(splash, "Chart ...", &lv_font_montserrat_12, kMuted);  // once the map answers
  mapAddress = label(splash, "", &lv_font_montserrat_12, kMuted);
  lv_obj_add_flag(mapAddress, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_margin_bottom(onDemand ? mapAddress : mapVersion, 10, 0);
  wifiState = splashStep(splash, "Connecting to Wi-Fi...", &wifiStep);
  mapState =
      splashStep(splash, mode == SplashMode::Reconnect ? "Reconnecting to the Chart..." : "Connecting to the Chart...");

  if (!onDemand) {  // shown once it can't join the Wi-Fi (tickSplash)
    wifiSetupBtn = button(splash, &stBtn);
    lv_obj_set_size(wifiSetupBtn, 180, 44);
    lv_obj_set_style_margin_top(wifiSetupBtn, 8, 0);
    lv_obj_set_flex_flow(wifiSetupBtn, LV_FLEX_FLOW_ROW);
    center(wifiSetupBtn);
    lv_obj_set_style_pad_column(wifiSetupBtn, 8, 0);
    label(wifiSetupBtn, LV_SYMBOL_WIFI, &lv_font_montserrat_16, kAccent);
    lv_label_set_text(lv_label_create(wifiSetupBtn), "Wi-Fi setup");
    lv_obj_add_event_cb(
        wifiSetupBtn, [](lv_event_t*) { wifi_setup_start(SetupReason::Lost); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(wifiSetupBtn, LV_OBJ_FLAG_HIDDEN);
  }

  if (onDemand) {
    // the connection is the dot on the logo and the address above: the steps aren't needed
    lv_obj_add_flag(lv_obj_get_parent(wifiState), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_get_parent(mapState), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mapAddress, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* row = box(splash, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row, kW - 20, 46);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t* close = button(row, &stBtn);
    lv_obj_set_height(close, LV_PCT(100));
    lv_obj_set_flex_grow(close, 1);
    lv_obj_set_flex_flow(close, LV_FLEX_FLOW_ROW);
    center(close);
    lv_obj_set_style_pad_column(close, 8, 0);
    icon(close, &icon_close, kText2);
    lv_label_set_text(lv_label_create(close), "Close");
    lv_obj_add_event_cb(close, [](lv_event_t*) { hideSplash(); }, LV_EVENT_CLICKED, nullptr);

    updButtonSplash = button(row, &stBtn);
    lv_obj_set_height(updButtonSplash, LV_PCT(100));
    lv_obj_set_flex_grow(updButtonSplash, 1);
    lv_obj_set_flex_flow(updButtonSplash, LV_FLEX_FLOW_ROW);
    center(updButtonSplash);
    lv_obj_set_style_pad_column(updButtonSplash, 6, 0);
    updLabelSplash = lv_label_create(updButtonSplash);
    lv_label_set_long_mode(updLabelSplash, LV_LABEL_LONG_DOT);
    lv_obj_set_width(updLabelSplash, 130);
    lv_obj_set_style_text_align(updLabelSplash, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(updButtonSplash, [](lv_event_t*) { showUpdate(); }, LV_EVENT_CLICKED, nullptr);
    showSplashUpdate();

    buildSplashVolume(splash);  // the speaker's volume, at the top right
  }

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

void showSplashOnDemand(lv_event_t*) {
  if (!splash) showSplash(SplashMode::OnDemand);
}

void tickSplash() {
  if (!splash) return;
  if (status.valid && !status.versionText.empty()) setText(mapVersion, "Chart " + status.versionText);
  if (splashMode == SplashMode::OnDemand) {
    const char* host = status.ip.length() ? status.ip.c_str() : client->host();
    setText(mapAddress, std::string("Chart at ") + host + (conn.online ? "" : " - not answering"));
    if (*remote_web_address()) setText(repoLabel, std::string("Settings: ") + remote_web_address());
    lv_obj_set_style_text_color(mapAddress, hex(conn.online ? kMuted : 0xff8a8a), 0);
    if (millis() - splashStart >= kSplashShowMs) hideSplash();
    return;
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
    setText(wifiStep, text);
    setStep(wifiState, "retrying", kAccent);
  } else {
    setStep(wifiState, "", kVfr);
  }
  // Can't join: the Wi-Fi setup button, in the logo's place
  bool stuck = !wifi && millis() - splashStart >= kWifiTimeoutMs;
  if (wifiSetupBtn) showHidden(wifiSetupBtn, stuck);
  if (splashLogo) showHidden(splashLogo, !stuck);
  if (conn.online) {
    setStep(mapState, "OK", kVfr);
  } else if (wifi && conn.wifi && conn.httpCode != 0) {
    setStep(mapState, "retrying", kAccent);  // the map didn't answer
  } else {
    setStep(mapState, "", kAccent);
  }

  if (!(wifi && conn.online)) {
    connectedAt = 0;
    return;
  }
  if (!connectedAt) connectedAt = millis() ? millis() : 1;
  if (millis() - connectedAt >= kSplashHoldMs) hideSplash();
}

// ---- Wi-Fi config mode -----------------------------------------------------------------------------------------
//
// While the remote has its own Wi-Fi network up (wifi_setup.h): how to join it from a phone (a QR code to scan) and
// what's happening. Cancel goes back to trying the saved network (not on a fresh remote: it has none).

lv_obj_t* setupScr = nullptr;
lv_obj_t* setupStatus = nullptr;

void setupStep(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = label(parent, text, font, color);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}

void showSetupScreen() {
  bool fromSplash = splash != nullptr;
  setupScr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(setupScr);
  lv_obj_set_style_bg_color(setupScr, hex(kBg), 0);
  lv_obj_set_style_bg_opa(setupScr, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(setupScr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(setupScr, 10, 0);
  lv_obj_set_style_pad_row(setupScr, 8, 0);

  lv_obj_t* head = box(setupScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 8, 0);
  label(head, LV_SYMBOL_WIFI, &lv_font_montserrat_16, kAccent);
  label(head, wifi_setup_fresh() ? "Set up the Copilot's Wi-Fi" : "Wi-Fi setup", &lv_font_montserrat_16, kText);

  lv_obj_t* row = box(setupScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(row, 10, 0);
  std::string join = wifiQr(wifi_setup_ap_name());
  lv_obj_t* qr = lv_qrcode_create(row);
  lv_qrcode_set_size(qr, 104);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, join.c_str(), join.size());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);  // the quiet zone a camera needs around it
  lv_obj_set_style_border_width(qr, 6, 0);

  lv_obj_t* steps = box(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(steps, 1);
  lv_obj_set_height(steps, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(steps, 3, 0);
  setupStep(steps, "1. On your phone, join", &lv_font_montserrat_12, kMuted);
  // the network's name on one line: 14 px, or 12 if it doesn't fit beside the code
  const std::string& ap = wifi_setup_ap_name();
  lv_obj_update_layout(setupScr);
  bool fits = lv_text_get_width(ap.c_str(), ap.size(), &lv_font_montserrat_14, 0) <= lv_obj_get_content_width(steps);
  setupStep(steps, ap.c_str(), fits ? &lv_font_montserrat_14 : &lv_font_montserrat_12, kAccent);
  setupStep(steps, "(or scan the code)", &lv_font_montserrat_12, kMuted);
  lv_obj_t* two =
      label(steps, "2. The setup page opens by itself, or go to\n192.168.4.1", &lv_font_montserrat_12, kMuted);
  lv_obj_set_width(two, LV_PCT(100));
  lv_label_set_long_mode(two, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_margin_top(two, 6, 0);

  lv_obj_t* foot = box(setupScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(foot, LV_PCT(100), 44);
  lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(foot, 8, 0);
  setupStatus = label(foot, "", &lv_font_montserrat_12, kText2);
  lv_obj_set_flex_grow(setupStatus, 1);
  lv_label_set_long_mode(setupStatus, LV_LABEL_LONG_WRAP);
  if (!wifi_setup_fresh()) {
    lv_obj_t* cancel = button(foot, &stBtn);
    lv_obj_set_size(cancel, 96, LV_PCT(100));
    lv_obj_center(label(cancel, "Cancel", &lv_font_montserrat_14, kText2));
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { wifi_setup_stop(); }, LV_EVENT_CLICKED, nullptr);
  }

  lv_indev_reset(nullptr, nullptr);  // the tap that opened it isn't a tap on it
  lv_screen_load_anim(setupScr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, fromSplash);  // a splash on screen goes for good
  if (fromSplash) forgetSplash();
}

void tickSetupScreen() {
  int phones = wifi_setup_phones();
  std::string text = phones > 0           ? "A phone has joined: finish on the setup page."
                     : wifi_setup_fresh() ? "Waiting for a phone..."
                                          : "Waiting for a phone; still trying " + wifiName() + " meanwhile.";
  setText(setupStatus, text);
}

// Config mode is over (it joined the saved network, Cancel, or no phone for 10 minutes): the splash takes over, and
// the remote fades in once the map answers.
void hideSetupScreen() {
  lv_obj_t* old = setupScr;
  setupScr = nullptr;
  setupStatus = nullptr;
  showSplash(SplashMode::Reconnect);
  lv_obj_delete_delayed(old, 400);
}

// ---- the update screen ---------------------------------------------------------------------------------------
//
// From the splash's Firmware (or Update) button: the installed firmware, the
// newest on the chosen channel (Releases, Latest commit, Branch: metarmap2's),
// and UPDATE.

lv_obj_t* updScr = nullptr;
lv_obj_t* updChips[3];
lv_obj_t *updBranchRow, *updBranch, *updAvailable, *updButton, *updProgressRow, *updBar, *updStatus, *updBack;
std::string updBranchOptions;  // what the branch menu holds

const char* const kChannelLabels[3] = {"Releases", "Latest commit", "Branch"};

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
    setChecked(updChips[i], i == (int)upd.channel);
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
  if (updScr) return;
  bool fromSplash = splash != nullptr;
  updScr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(updScr);
  lv_obj_set_style_bg_color(updScr, hex(kBg), 0);
  lv_obj_set_style_bg_opa(updScr, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(updScr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(updScr, 8, 0);
  lv_obj_set_style_pad_row(updScr, 8, 0);

  lv_obj_t* head = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(head, LV_PCT(100), 38);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(head, "Firmware update", &lv_font_montserrat_16, kText);
  updBack = button(head, &stBtn);
  lv_obj_set_size(updBack, 72, LV_PCT(100));
  lv_obj_center(label(updBack, "Back", &lv_font_montserrat_14, kText2));
  lv_obj_add_event_cb(updBack, [](lv_event_t*) { hideUpdate(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* chips = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(chips, LV_PCT(100), 40);
  lv_obj_set_style_pad_column(chips, 6, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_t* chip = button(chips, &stBtn);
    lv_obj_add_style(chip, &stLit, LV_STATE_CHECKED);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
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
  lv_obj_set_height(updBranch, 36);
  lv_obj_set_style_pad_ver(updBranch, 8, 0);
  lv_obj_add_event_cb(updBranch, onBranch, LV_EVENT_VALUE_CHANGED, nullptr);
  updBranchOptions.clear();

  infoRow(updScr, "Installed", FIRMWARE_VERSION);
  infoRow(updScr, "Available", "", &updAvailable);

  lv_obj_t* spacer = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(spacer, 1);

  updButton = button(updScr, &stTile);
  lv_obj_set_size(updButton, LV_PCT(100), 44);
  lv_obj_set_style_bg_color(updButton, hex(kAccent), 0);
  lv_obj_set_style_border_width(updButton, 0, 0);
  lv_obj_center(label(updButton, "UPDATE", &lv_font_montserrat_16, kBg));
  lv_obj_add_event_cb(updButton, [](lv_event_t*) { updater->install(); }, LV_EVENT_CLICKED, nullptr);

  updProgressRow = box(updScr, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(updProgressRow, LV_PCT(100), 44);
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
  if (fromSplash) forgetSplash();
  refreshUpdate();
  if (upd.phase == UpdateState::Idle) updater->check();  // fresh, while it's open
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
  // (The logo's splash says the map isn't answering; once it's closed, this one comes.)
  if (millis() - lostSince >= kLostMs && !splash && !updScr && !setupScr) {
    LOGF("ui: the Chart is unreachable; reconnecting\n");
    showSplash(SplashMode::Reconnect);
  }
}

}  // namespace

void ui_init(MapClient& c, Updater& u) {
  uiTask = xTaskGetCurrentTaskHandle();
  client = &c;
  updater = &u;
  lv_display_t* disp = lv_display_get_default();
  lv_theme_t* theme = lv_theme_default_init(disp, hex(kAccent), hex(kMvfr), true, &lv_font_montserrat_14);
  lv_display_set_theme(disp, theme);
  initStyles();

  lv_obj_t* scr = lv_screen_active();
  remoteScreen = scr;
  lv_obj_set_style_bg_color(scr, hex(kBg), 0);
  lv_obj_set_style_text_color(scr, hex(kText), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildRail(scr);
  panels[0] = panel(scr);
  buildLedsPanel(panels[0]);
  panels[1] = panel(scr);
  buildAirportsPanel(panels[1]);
  buildPickerScreen(scr);
  buildBrightness(scr);
  buildCard(scr);
  showTab(0);
  showSplash(SplashMode::Startup);
}

// The picker and the sheets closed, for a clean screenshot
static void closeOverlays() {
  closePicker();
  closeBrightness();
  if (cardOpen()) closeCard();
}

void ui_show_tab(int tab) {
  closeOverlays();
  showTab(tab == 1 ? 1 : 0);
}

void ui_show_update() { showUpdate(); }

void ui_show_splash() {
  if (!splash && !updScr) showSplash(SplashMode::OnDemand);
}

void ui_open(char what) {
  if (splash || updScr) return;
  closeOverlays();
  if (what == 'p') {
    showTab(0);
    openPicker();
  } else if (what == 'b') {
    showTab(0);
    openBrightness();
  } else if (what == 'c' && !tiles.empty()) {
    showTab(1);
    openCard(page * kPerPage);  // the page's first airport
  }
}

void ui_tick() {
  if (client->getConnection(conn, connRev)) showConnection();
  bool setup = wifi_setup_active();
  if (setup && !setupScr && !updScr) showSetupScreen();
  if (!setup && setupScr) hideSetupScreen();
  if (setupScr) tickSetupScreen();
  checkMapLost();
  tickSplash();
  syncSplashVolume();  // the volume as the card's slider (or the splash's) left it
  syncCardVolume();
  if (updater->get(upd, updRev)) {
    showHidden(starIcon, upd.available);
    showSplashUpdate();
    refreshUpdate();
  }
  if (stream_get(radio, radioRev)) {
    followTranscript();
    showCard();
  }
  if (client->getAtis(atis, atisRev)) followTranscript();

  if (client->getStatus(status, statusRev)) {
    showPickerTiles();
    showSkips();
    showVisualizer(live.valid ? live.activeIndex : status.activeIndex);
    showLegend();
    showCard();  // the airports' names and METARs
  }

  if (client->getLive(live, liveRev)) {
    if (live.activeIndex != shownIndex) showVisualizer(live.activeIndex);
    showRotation();
    showSkips();
    showBrightness();
    showStations();
    showSteps();
    if (legendInUse() != legendUsed) showLegend();
  }
  // after /api/live, so its answer wins over a poll from before it
  if (client->getSkip(skipAnswer, skipRev)) showSkipAnswer();
  expireSkips();

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 250) {
    lastTick = millis();
    tickClock();
  }
}

// ---- remote control, for screenshots (ui.h) ---------------------------------------------------------------------
//
// The web server answers on this same task (remote_web_tick() runs in loop(), after lv_timer_handler()), so these
// call LVGL directly, as the serial console's commands do: nothing is handed between tasks. uiTask checks that.

namespace {

using ui_control::Button;
using ui_control::done;
using ui_control::fail;
using ui_control::Outcome;
using ui_control::Screen;

constexpr uint32_t kSettleMinMs = 150;   // the stream's state (the card's Stop) and the first frame come in by then
constexpr uint32_t kSettleMaxMs = 1000;  // a screen's fade is 300-400 ms: over well before

bool onUiTask() { return uiTask && xTaskGetCurrentTaskHandle() == uiTask; }

Outcome wrongTask() { return fail(500, "Remote control has to run on the screen's task."); }

// Runs the screen (ui_tick() and LVGL's timers) for at least `minMs`, until its animations (a screen's fade) are
// over, then draws what's changed, so the next screenshot shows it
void settle(uint32_t minMs = kSettleMinMs) {
  uint32_t start = millis();
  for (;;) {
    ui_tick();
    lv_timer_handler();
    uint32_t t = millis() - start;
    if (t >= kSettleMaxMs || (t >= minMs && lv_anim_count_running() == 0)) break;
    delay(5);
  }
  lv_refr_now(nullptr);
}

bool installing() { return upd.phase == UpdateState::Installing || upd.phase == UpdateState::Restarting; }

// Wi-Fi config mode, or the startup (or reconnecting) splash: the panels aren't there to show
Outcome reachable() {
  if (setupScr) return fail(409, "The Copilot is in Wi-Fi setup.");
  if (splash && splashMode != SplashMode::OnDemand) {
    return fail(409, "The Copilot is connecting to the Chart (the startup splash is on screen).");
  }
  return done();
}

// The update screen closed, as its Back does (not while it installs)
Outcome leaveUpdate() {
  if (!updScr) return done();
  if (installing()) return fail(409, "A firmware update is installing.");
  hideUpdate();
  settle(0);
  return done();
}

// Back to the panels: the update screen and the logo's splash closed (as their Back and Close), then the picker and
// the sheets
Outcome toPanels() {
  Outcome r = reachable();
  if (r.ok()) r = leaveUpdate();
  if (!r.ok()) return r;
  if (splash) {
    hideSplash();  // its volume slider goes with it
    settle(0);
  }
  closeOverlays();
  return done();
}

// The logo's splash, as the logo shows it; one already open starts its 30 s again, its volume slider closed
Outcome openSplash() {
  Outcome r = reachable();
  if (r.ok()) r = leaveUpdate();
  if (!r.ok()) return r;
  closeOverlays();
  if (splash) {
    closeSplashVolume();
    splashStart = millis();
  } else {
    showSplash(SplashMode::OnDemand);
  }
  return done();
}

// Whether the card's button `b` would do anything on tile `t`'s card (`open`: the card open now): not a disabled (or
// hidden) one
Outcome canPress(const Tile& t, bool open, Button b) {
  switch (b) {
    case Button::Locate:
      if (live.exclusive) return fail(409, "Locate can't show while Rainbow or Chase runs.");
      break;
    case Button::Awos:
    case Button::Atis:
      if (!stream_available()) return fail(409, "The Copilot's speaker isn't working: no AWOS or ATIS.");
      if (!awosAvailable()) return fail(409, "The Chart can't speak now (its speech is off).");
      if (b == Button::Atis && !t.atis) return fail(409, t.code + " has no ATIS.");
      break;
    case Button::Stop:
      if (!open || !cardSpeaking()) return fail(409, t.code + " isn't saying anything.");
      break;
    case Button::None:
      break;
  }
  return done();
}

void press(Button b) {
  switch (b) {
    case Button::Locate:
      onLocate(nullptr);
      break;
    case Button::Awos:
      speak(false);
      break;
    case Button::Atis:
      speak(true);
      break;
    case Button::Stop:  // the playing one's button, which says Stop
      if (cardSpeaking()) speak(cardSpeaking() == 2);
      break;
    case Button::None:
      break;
  }
}

bool shown(lv_obj_t* o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

}  // namespace

ui_control::Outcome ui_control_show(const ui_control::ShowRequest& req) {
  if (!onUiTask()) return wrongTask();
  Outcome r = done();
  switch (req.screen) {
    case Screen::Chart:
    case Screen::Picker:
    case Screen::Brightness:
      r = toPanels();
      if (!r.ok()) return r;
      showTab(0);
      if (req.screen == Screen::Picker) openPicker();
      if (req.screen == Screen::Brightness) openBrightness();
      break;
    case Screen::Airports:
      if (req.page) r = ui_control::checkPage(req.page, pageCount());
      if (r.ok()) r = toPanels();
      if (!r.ok()) return r;
      showTab(1);
      if (req.page) showPage(req.page - 1);
      break;
    case Screen::Close:
      r = toPanels();
      if (!r.ok()) return r;
      break;
    case Screen::Splash:
    case Screen::Volume:
      r = openSplash();
      if (!r.ok()) return r;
      if (req.screen == Screen::Volume) openSplashVolume(nullptr);
      break;
    case Screen::Update:
      r = reachable();
      if (!r.ok()) return r;
      if (!updScr) {
        closeOverlays();
        showUpdate();  // from the splash too, as its Firmware button
      }
      break;
  }
  settle();
  return done();
}

ui_control::Outcome ui_control_card(const ui_control::CardRequest& req) {
  if (!onUiTask()) return wrongTask();
  Outcome r = reachable();
  if (!r.ok()) return r;
  int index = cardTile;
  if (req.opens()) {
    std::vector<std::string> codes(tiles.size());
    std::transform(tiles.begin(), tiles.end(), codes.begin(), [](const Tile& t) { return t.code; });
    r = ui_control::findCard(codes, kPerPage, page, req, index);
  } else {  // the open card's: cardTile can be past the end of tiles (see it)
    r = ui_control::checkOpenCard(cardOpen() && !updScr && !splash, index, (int)tiles.size());
  }
  if (r.ok() && req.opens() && req.button == Button::Stop) {
    r = fail(409, "Opening a card stops what it's saying: send button=stop alone, to the open card.");
  }
  if (r.ok() && req.button != Button::None) {
    // in range: findCard()'s index is one of tiles, and checkOpenCard() checked cardTile
    const Tile* t = req.opens() ? &tiles[index] : cardAirport();
    r = t ? canPress(*t, !req.opens(), req.button) : fail(409, "The card's airport is gone from the Chart's airports.");
  }
  if (!r.ok()) return r;
  if (req.opens()) {
    r = toPanels();
    if (!r.ok()) return r;
    showTab(1);
    showPage(pages::of(index, kPerPage));
    openCard(index);  // as a tap on its tile: the Chart pulses its pixel
    // what that stopped (the airport's own AWOS, say) reaches the screen before a button's pressed, as for a finger
    if (req.button != Button::None) settle();
  }
  press(req.button);
  settle();
  return done();
}

ui_control::Outcome ui_control_visualizer(const std::string& want) {
  if (!onUiTask()) return wrongTask();
  if (status.visualizers.empty()) return fail(409, "The Chart's visualizers aren't known yet.");
  int index = ui_control::findVisualizer(status.visualizers, want);
  if (index < 0) return fail(404, "\"" + want + "\" isn't one of the Chart's visualizers.");
  if (!conn.online) return fail(409, "The Chart isn't answering.");
  if (pickerOpen()) {
    tapPick(index);  // lit a moment, then the picker closes, as a tap
    settle(kPickedMs + 100);
  } else {
    pickVisualizer(index);
    settle();
  }
  return done();
}

ui_control::State ui_control_state() {
  ui_control::State s;
  if (!onUiTask()) {
    s.screen = "unknown";
    return s;
  }
  s.screen = setupScr ? "wifiSetup"
             : updScr ? "update"
             : splash ? (splashMode == SplashMode::OnDemand ? "splash" : "startup")
                      : "main";
  s.panel = shown(panels[1]) ? "airports" : "chart";
  if (splash && shown(splashVolPopup)) {
    s.overlay = "volume";
  } else if (cardOpen()) {
    s.overlay = "card";
  } else if (pickerOpen()) {
    s.overlay = "picker";
  } else if (shown(bright.backdrop)) {
    s.overlay = "brightness";
  }
  const Tile* t = cardOpen() ? cardAirport() : nullptr;
  if (t) s.card = t->code;
  int speaking = t ? cardSpeaking() : 0;
  s.playing = speaking == 1 ? "awos" : speaking == 2 ? "atis" : "";
  s.page = page + 1;
  s.pages = pageCount();
  size_t first = std::min(tiles.size(), (size_t)page * kPerPage);
  size_t last = std::min(tiles.size(), first + kPerPage);
  s.pageAirports.resize(last - first);
  std::transform(tiles.begin() + first, tiles.begin() + last, s.pageAirports.begin(),
                 [](const Tile& tile) { return tile.code; });
  s.airports = (int)tiles.size();
  if (shownIndex >= 0 && shownIndex < (int)status.visualizers.size()) s.visualizer = status.visualizers[shownIndex].id;
  s.visualizers.resize(status.visualizers.size());
  std::transform(status.visualizers.begin(), status.visualizers.end(), s.visualizers.begin(),
                 [](const Visualizer& v) { return v.id; });
  s.online = conn.online;
  return s;
}
