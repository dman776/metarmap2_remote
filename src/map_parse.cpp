#include "map_parse.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

double numberOrNan(JsonVariantConst v) { return v.isNull() ? NAN : v.as<double>(); }

}  // namespace

uint32_t parseColor(const char* css) {
  if (!css || css[0] != '#' || strlen(css) != 7) return 0;
  char* end = nullptr;
  unsigned long v = strtoul(css + 1, &end, 16);
  return *end == '\0' ? (uint32_t)v : 0;
}

void liveFilter(JsonDocument& filter) {
  filter.clear();
  filter["now"] = true;
  filter["brightness"] = true;
  JsonObject v = filter["visualizer"].to<JsonObject>();
  v["active"] = true;
  v["name"] = true;
  v["exclusive"] = true;
  v["autoRotate"] = true;
  JsonObject led = filter["leds"].add<JsonObject>();
  led["code"] = true;
  led["pixel"] = true;
  led["virtual"] = true;
  led["flightCategory"] = true;
  led["lightning"] = true;
  led["look"]["effect"] = true;
  led["look"]["colors"] = true;
  filter["locate"] = true;
  filter["strip"] = true;
}

void statusFilter(JsonDocument& filter) {
  filter.clear();
  filter["version"] = true;
  filter["ip"] = true;
  JsonObject v = filter["visualizer"].to<JsonObject>();
  v["active"] = true;
  JsonObject item = v["legend"].add<JsonObject>();
  item["label"] = true;
  item["detail"] = true;
  item["effect"] = true;
  item["colors"] = true;
  JsonObject vis = filter["visualizers"].add<JsonObject>();
  vis["id"] = true;
  vis["name"] = true;
  vis["exclusive"] = true;
}

MapLive parseLive(JsonVariantConst doc) {
  MapLive live;
  live.valid = true;
  live.now = doc["now"] | 0.0;
  live.brightness = doc["brightness"] | 0.0f;
  JsonVariantConst v = doc["visualizer"];
  live.activeIndex = v["active"] | -1;
  live.activeName = v["name"] | "";
  live.exclusive = v["exclusive"] | false;
  live.autoRotate = v["autoRotate"]["enabled"] | false;
  live.autoSeconds = v["autoRotate"]["seconds"] | 0;
  live.autoNext = numberOrNan(v["autoRotate"]["next"]);
  live.strip = !doc["strip"].isNull();
  for (JsonVariantConst led : doc["leds"].as<JsonArrayConst>()) {
    Station s;
    s.code = led["code"] | "";
    s.pixel = led["pixel"] | -1;
    s.isVirtual = led["virtual"] | false;
    s.category = led["flightCategory"] | "";
    s.lightning = led["lightning"] | false;
    s.effect = led["look"]["effect"] | "off";
    for (JsonVariantConst c : led["look"]["colors"].as<JsonArrayConst>()) {
      uint32_t color = parseColor(c | "");  // a cycle repeats its colors
      if (std::find(s.colors.begin(), s.colors.end(), color) == s.colors.end()) s.colors.push_back(color);
    }
    live.stations.push_back(s);
  }
  if (!doc["locate"].isNull()) {
    live.locatePixel = doc["locate"]["pixel"] | -1;
    live.locateAt = numberOrNan(doc["locate"]["at"]);
  }
  return live;
}

MapStatus parseStatus(JsonVariantConst doc) {
  MapStatus status;
  status.valid = true;
  status.version = doc["version"] | "";
  status.ip = doc["ip"] | "";
  status.activeIndex = doc["visualizer"]["active"] | -1;
  for (JsonVariantConst item : doc["visualizer"]["legend"].as<JsonArrayConst>()) {
    LegendItem li;
    li.label = item["label"] | "";
    li.detail = item["detail"] | "";
    li.effect = item["effect"] | "solid";
    for (JsonVariantConst c : item["colors"].as<JsonArrayConst>()) li.colors.push_back(parseColor(c | ""));
    status.legend.push_back(li);
  }
  for (JsonVariantConst vis : doc["visualizers"].as<JsonArrayConst>()) {
    Visualizer v;
    v.id = vis["id"] | "";
    v.name = vis["name"] | "";
    v.exclusive = vis["exclusive"] | false;
    status.visualizers.push_back(v);
  }
  return status;
}
