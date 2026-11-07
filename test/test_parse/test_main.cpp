// Parsing the map's JSON: real (trimmed) /api/live and /api/status answers,
// through the same filters the remote uses.
#include <ArduinoJson.h>
#include <unity.h>

#include <algorithm>
#include <cmath>

#include "fixtures.h"
#include "map_parse.h"

static JsonDocument filtered(const char* json, void (*makeFilter)(JsonDocument&)) {
  JsonDocument filter;
  makeFilter(filter);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
  return doc;
}

static bool has(const std::vector<uint32_t>& v, uint32_t c) { return std::find(v.begin(), v.end(), c) != v.end(); }

void setUp() {}
void tearDown() {}

// ---- colors ----

void test_parse_color() {
  TEST_ASSERT_EQUAL_HEX32(0xff7e00, parseColor("#ff7e00"));
  TEST_ASSERT_EQUAL_HEX32(0x00ff00, parseColor("#00FF00"));
  TEST_ASSERT_EQUAL_HEX32(0, parseColor(nullptr));
  TEST_ASSERT_EQUAL_HEX32(0, parseColor(""));
  TEST_ASSERT_EQUAL_HEX32(0, parseColor("ff7e00"));     // no #
  TEST_ASSERT_EQUAL_HEX32(0, parseColor("#fff"));       // short form isn't used by the map
  TEST_ASSERT_EQUAL_HEX32(0, parseColor("#ff7e0g"));    // not hex
  TEST_ASSERT_EQUAL_HEX32(0, parseColor("#ff7e0000"));  // too long
}

// ---- /api/live ----

void test_live_visualizer_and_auto_rotate() {
  MapLive live = parseLive(filtered(kLiveJson, liveFilter).as<JsonVariantConst>());
  TEST_ASSERT_TRUE(live.valid);
  TEST_ASSERT_EQUAL_INT(10, live.activeIndex);
  TEST_ASSERT_EQUAL_STRING("Dewpoint Spread", live.activeName.c_str());
  TEST_ASSERT_FALSE(live.exclusive);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.51, live.brightness);
  TEST_ASSERT_TRUE(live.autoRotate);
  TEST_ASSERT_EQUAL_INT(30, live.autoSeconds);
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 10.06, live.autoNext - live.now);
  TEST_ASSERT_FALSE(live.strip);
}

void test_live_locate() {
  MapLive live = parseLive(filtered(kLiveJson, liveFilter).as<JsonVariantConst>());
  TEST_ASSERT_EQUAL_INT(5, live.locatePixel);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 1793913184.006, live.locateAt);
}

void test_live_stations() {
  MapLive live = parseLive(filtered(kLiveJson, liveFilter).as<JsonVariantConst>());
  TEST_ASSERT_EQUAL_UINT(4, live.stations.size());

  const Station& vct = live.stations[0];
  TEST_ASSERT_EQUAL_STRING("KVCT", vct.code.c_str());
  TEST_ASSERT_EQUAL_INT(0, vct.pixel);
  TEST_ASSERT_EQUAL_STRING("VFR", vct.category.c_str());
  TEST_ASSERT_EQUAL_STRING("solid", vct.effect.c_str());
  TEST_ASSERT_EQUAL_UINT(1, vct.colors.size());
  TEST_ASSERT_EQUAL_HEX32(0xffffff, vct.colors[0]);
  TEST_ASSERT_FALSE(vct.lightning);
  TEST_ASSERT_FALSE(vct.isVirtual);

  // Lightning: a cycle of ~80 frames, each color kept once.
  const Station& ela = live.stations[1];
  TEST_ASSERT_TRUE(ela.lightning);
  TEST_ASSERT_EQUAL_STRING("cycle", ela.effect.c_str());
  TEST_ASSERT_EQUAL_UINT(2, ela.colors.size());
  TEST_ASSERT_TRUE(has(ela.colors, 0xffff00));
  TEST_ASSERT_TRUE(has(ela.colors, 0xffffff));

  // No METAR: no category, LED off.
  const Station& dkr = live.stations[2];
  TEST_ASSERT_EQUAL_STRING("", dkr.category.c_str());
  TEST_ASSERT_EQUAL_STRING("off", dkr.effect.c_str());
  TEST_ASSERT_TRUE(dkr.colors.empty());

  TEST_ASSERT_TRUE(live.stations[3].isVirtual);
  TEST_ASSERT_EQUAL_INT(50, live.stations[3].pixel);
}

void test_live_nulls() {
  // auto-rotate off: next is null; one effect on the whole strip; no locate yet.
  const char* json = R"({"now": 100.0, "visualizer": {"active": 13, "name": "Rainbow", "exclusive": true,
      "autoRotate": {"enabled": false, "seconds": 60, "next": null}}, "strip": {"effect": "rainbow"},
      "leds": [], "locate": null})";
  MapLive live = parseLive(filtered(json, liveFilter).as<JsonVariantConst>());
  TEST_ASSERT_FALSE(live.autoRotate);
  TEST_ASSERT_TRUE(std::isnan(live.autoNext));
  TEST_ASSERT_TRUE(live.strip);
  TEST_ASSERT_TRUE(live.exclusive);
  TEST_ASSERT_EQUAL_INT(-1, live.locatePixel);
  TEST_ASSERT_TRUE(std::isnan(live.locateAt));
}

void test_live_legend_in_use() {
  const char* json = R"({"now": 100.0, "visualizer": {"active": 0, "name": "Flight Category",
      "legendInUse": ["VFR", "no_report"], "legendInUseOnly": false}, "strip": null, "leds": []})";
  MapLive live = parseLive(filtered(json, liveFilter).as<JsonVariantConst>());
  TEST_ASSERT_TRUE(live.hasLegendInUse);
  TEST_ASSERT_EQUAL_UINT(2, live.legendInUse.size());
  TEST_ASSERT_EQUAL_STRING("VFR", live.legendInUse[0].c_str());
  TEST_ASSERT_EQUAL_STRING("no_report", live.legendInUse[1].c_str());
}

void test_live_without_legend_in_use() {
  // An older map doesn't send it; a whole-strip effect sends null.
  TEST_ASSERT_FALSE(parseLive(filtered(kLiveJson, liveFilter).as<JsonVariantConst>()).hasLegendInUse);
  const char* json = R"({"visualizer": {"active": 13, "legendInUse": null}, "strip": {"effect": "rainbow"}})";
  TEST_ASSERT_FALSE(parseLive(filtered(json, liveFilter).as<JsonVariantConst>()).hasLegendInUse);
}

void test_live_speech() {
  // The map can read METARs aloud only with speech on and a server set; an older map doesn't say (no hold-to-hear).
  TEST_ASSERT_FALSE(parseLive(filtered(kLiveJson, liveFilter).as<JsonVariantConst>()).speech);
  TEST_ASSERT_TRUE(parseLive(filtered(R"({"speech": true, "leds": []})", liveFilter).as<JsonVariantConst>()).speech);
  TEST_ASSERT_FALSE(parseLive(filtered(R"({"speech": false, "leds": []})", liveFilter).as<JsonVariantConst>()).speech);
}

void test_live_filter_drops_the_rest() {
  // The METAR text, positions and names are most of /api/live; the remote
  // doesn't keep them.
  JsonDocument doc = filtered(kLiveJson, liveFilter);
  JsonVariantConst led = doc["leds"][0];
  TEST_ASSERT_TRUE(led["raw"].isNull());
  TEST_ASSERT_TRUE(led["lat"].isNull());
  TEST_ASSERT_TRUE(led["name"].isNull());
  TEST_ASSERT_FALSE(led["code"].isNull());
}

// ---- /api/status ----

void test_status() {
  MapStatus status = parseStatus(filtered(kStatusJson, statusFilter).as<JsonVariantConst>());
  TEST_ASSERT_TRUE(status.valid);
  TEST_ASSERT_EQUAL_STRING("26.11.04", status.version.c_str());
  TEST_ASSERT_EQUAL_STRING("v26.11.04+16 f86daa4", status.versionText.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.10.76", status.ip.c_str());
  TEST_ASSERT_EQUAL_INT(10, status.activeIndex);

  TEST_ASSERT_EQUAL_UINT(15, status.visualizers.size());
  TEST_ASSERT_EQUAL_STRING("flight_category", status.visualizers[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("Flight Category", status.visualizers[0].name.c_str());
  TEST_ASSERT_FALSE(status.visualizers[0].exclusive);
  TEST_ASSERT_TRUE(status.visualizers[14].exclusive);  // Chase
}

static std::string versionOf(const char* json) {
  JsonDocument doc = filtered(json, statusFilter);
  return versionText(doc.as<JsonVariantConst>());
}

void test_version_text() {
  // at a release: just the tag
  TEST_ASSERT_EQUAL_STRING(
      "v26.11.06",
      versionOf(R"({"version": "26.11.06", "build": {"describe": "v26.11.06", "ahead": 0, "commit": "aa81431"}})")
          .c_str());
  // after one, and changed on the map
  TEST_ASSERT_EQUAL_STRING(
      "v26.11.04+2 abc1234 modified",
      versionOf(R"({"version": "26.11.04", "build": {"describe": "v26.11.04+2", "ahead": 2, "commit": "abc1234",
                    "modified": true}})")
          .c_str());
  // no release tag: describe is the commit
  TEST_ASSERT_EQUAL_STRING(
      "abc1234",
      versionOf(R"({"version": "26.11.04", "build": {"describe": "abc1234", "ahead": 0, "commit": "abc1234"}})")
          .c_str());
  // no build (not a git checkout, or an older map): the version, as a release
  TEST_ASSERT_EQUAL_STRING("v26.11.04", versionOf(R"({"version": "26.11.04", "build": null})").c_str());
  TEST_ASSERT_EQUAL_STRING("v2.9.2", versionOf(R"({"version": "2.9.2"})").c_str());
  TEST_ASSERT_EQUAL_STRING("", versionOf(R"({})").c_str());
}

void test_status_legend() {
  MapStatus status = parseStatus(filtered(kStatusJson, statusFilter).as<JsonVariantConst>());
  TEST_ASSERT_EQUAL_UINT(7, status.legend.size());

  const LegendItem& dry = status.legend[3];
  TEST_ASSERT_EQUAL_STRING("More than 4°C: dry", dry.label.c_str());
  TEST_ASSERT_EQUAL_STRING("solid", dry.effect.c_str());
  TEST_ASSERT_EQUAL_UINT(1, dry.colors.size());
  TEST_ASSERT_EQUAL_HEX32(0xffffff, dry.colors[0]);

  const LegendItem& missing = status.legend[4];
  TEST_ASSERT_EQUAL_STRING("blink", missing.effect.c_str());

  const LegendItem& lightning = status.legend[5];
  TEST_ASSERT_EQUAL_STRING("flash", lightning.effect.c_str());
  TEST_ASSERT_EQUAL_UINT(2, lightning.colors.size());
  TEST_ASSERT_EQUAL_HEX32(0xffff00, lightning.colors[1]);

  const LegendItem& off = status.legend[6];
  TEST_ASSERT_EQUAL_STRING("off", off.effect.c_str());
  TEST_ASSERT_TRUE(off.colors.empty());
}

void test_status_legend_keys() {
  const char* json = R"({"visualizer": {"active": 0, "legend": [
      {"key": "VFR", "label": "VFR", "detail": "Green", "colors": ["#00ff00"], "effect": "solid", "inUse": true},
      {"key": "no_report", "label": "METAR not available", "detail": "Off", "colors": [], "effect": "off"}]}})";
  MapStatus status = parseStatus(filtered(json, statusFilter).as<JsonVariantConst>());
  TEST_ASSERT_EQUAL_STRING("VFR", status.legend[0].key.c_str());
  TEST_ASSERT_EQUAL_STRING("no_report", status.legend[1].key.c_str());
  // an older map's legend has none
  MapStatus older = parseStatus(filtered(kStatusJson, statusFilter).as<JsonVariantConst>());
  TEST_ASSERT_TRUE(older.legend[0].key.empty());
}

void test_status_filter_drops_the_rest() {
  JsonDocument doc = filtered(kStatusJson, statusFilter);
  TEST_ASSERT_TRUE(doc["airports"].isNull());
  TEST_ASSERT_TRUE(doc["config"].isNull());
  TEST_ASSERT_TRUE(doc["visualizer"]["description"].isNull());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_color);
  RUN_TEST(test_live_visualizer_and_auto_rotate);
  RUN_TEST(test_live_locate);
  RUN_TEST(test_live_stations);
  RUN_TEST(test_live_nulls);
  RUN_TEST(test_live_speech);
  RUN_TEST(test_live_legend_in_use);
  RUN_TEST(test_live_without_legend_in_use);
  RUN_TEST(test_live_filter_drops_the_rest);
  RUN_TEST(test_status);
  RUN_TEST(test_version_text);
  RUN_TEST(test_status_legend);
  RUN_TEST(test_status_legend_keys);
  RUN_TEST(test_status_filter_drops_the_rest);
  return UNITY_END();
}
