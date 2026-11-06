// Matching the LEDs' looks to the legend entries the map is showing.
#include <unity.h>

#include <string>
#include <vector>

#include "legend.h"

namespace {

// The Flight Category legend, as /api/status gives it.
std::vector<LegendItem> flightCategoryLegend() {
  return {
      {"VFR", "Green", "solid", {0x00ff00}},
      {"MVFR", "Blue", "solid", {0x0000ff}},
      {"IFR", "Red", "solid", {0xff0000}},
      {"LIFR", "Purple", "solid", {0xff00ff}},
      {"Estimated", "Slow breathing", "pulse", {0x00ff00}},
      {"No flight category", "Slow red blink", "blink", {0xff0000}},
      {"Lightning or a thunderstorm", "Two quick yellow flashes", "flash", {0x5a5a5a, 0xffff00}},
      {"METAR not available", "Off", "off", {}},
  };
}
enum { VFR, MVFR, IFR, LIFR, ESTIMATED, MISSING, LIGHTNING, NO_METAR };

Station led(const std::string& effect, std::vector<uint32_t> colors, bool lightning = false) {
  Station s;
  s.effect = effect;
  s.colors = colors;
  s.lightning = lightning;
  return s;
}

struct Fixture {
  MapStatus status;
  MapLive live;
};

// The map running visualizer 0 with this legend and these LEDs, shown on screen.
Fixture showing(std::vector<LegendItem> legend, std::vector<Station> stations) {
  Fixture f;
  f.status.valid = true;
  f.status.activeIndex = 0;
  f.status.legend = legend;
  f.live.valid = true;
  f.live.activeIndex = 0;
  f.live.stations = stations;
  return f;
}

std::vector<bool> inUse(const Fixture& f, int shown = 0) { return legend::inUse(f.status, f.live, shown); }

// The indexes of the entries in use, for readable failures.
std::string used(const std::vector<bool>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i]) s += std::to_string(i) + " ";
  }
  return s;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---- a map that says which entries are in use (visualizer.legendInUse) ----

// flightCategoryLegend() with the keys the map gives each entry.
std::vector<LegendItem> keyedFlightCategoryLegend() {
  std::vector<LegendItem> legend = flightCategoryLegend();
  const char* keys[] = {"VFR", "MVFR", "IFR", "LIFR", "estimated", "missing", "lightning", "no_report"};
  for (size_t i = 0; i < legend.size(); i++) legend[i].key = keys[i];
  return legend;
}

Fixture saying(std::vector<std::string> inUse, std::vector<Station> stations = {}) {
  Fixture f = showing(keyedFlightCategoryLegend(), stations);
  f.live.hasLegendInUse = true;
  f.live.legendInUse = inUse;
  return f;
}

void test_the_maps_list_is_taken_at_its_word() {
  // The LED would match VFR by color; the map says otherwise.
  Fixture f = saying({"MVFR", "no_report"}, {led("solid", {0x00ff00})});
  TEST_ASSERT_EQUAL_STRING("1 7 ", used(inUse(f)).c_str());
}

void test_the_map_knows_an_estimated_category() {
  // The guess can only pick Estimated for an estimated MVFR (blue breathing);
  // the map names both entries.
  Fixture f = saying({"MVFR", "estimated"}, {led("pulse", {0x0000ff})});
  TEST_ASSERT_EQUAL_STRING("1 4 ", used(inUse(f)).c_str());
}

void test_the_map_names_lightning_and_the_color_under_it() {
  Fixture f = saying({"IFR", "lightning"}, {led("cycle", {0xffff00, 0xff0000}, true)});
  TEST_ASSERT_EQUAL_STRING("2 6 ", used(inUse(f)).c_str());
}

void test_an_empty_list_from_the_map_shows_the_whole_legend() {
  TEST_ASSERT_EQUAL_STRING("0 1 2 3 4 5 6 7 ", used(inUse(saying({}))).c_str());
}

void test_the_maps_list_still_waits_for_its_own_legend() {
  Fixture f = saying({"VFR"});
  f.live.activeIndex = 1;  // the map has moved on; the legend is still visualizer 0's
  TEST_ASSERT_EQUAL_STRING("", used(inUse(f)).c_str());
}

void test_an_older_map_is_still_matched_by_look() {
  // No legendInUse (an older map): the LEDs' looks, as before.
  Fixture f = showing(keyedFlightCategoryLegend(), {led("solid", {0x00ff00})});
  TEST_ASSERT_FALSE(legend::fromMap(f.status, f.live));
  TEST_ASSERT_EQUAL_STRING("0 ", used(inUse(f)).c_str());
}

void test_a_legend_without_keys_is_matched_by_look() {
  // legendInUse, but a legend from before keys (the status is a minute behind): nothing to match it to.
  Fixture f = showing(flightCategoryLegend(), {led("solid", {0x00ff00})});
  f.live.hasLegendInUse = true;
  f.live.legendInUse = {"MVFR"};
  TEST_ASSERT_FALSE(legend::fromMap(f.status, f.live));
  TEST_ASSERT_EQUAL_STRING("0 ", used(inUse(f)).c_str());
}

// ---- matching the LEDs' looks (a map that doesn't say) ----

void test_all_vfr_shows_only_vfr() {
  Fixture f = showing(flightCategoryLegend(), {led("solid", {0x00ff00}), led("solid", {0x00ff00})});
  TEST_ASSERT_EQUAL_STRING("0 ", used(inUse(f)).c_str());
}

void test_each_category_present() {
  Fixture f = showing(flightCategoryLegend(), {led("solid", {0x00ff00}), led("solid", {0xff0000}), led("off", {})});
  std::vector<bool> u = inUse(f);
  TEST_ASSERT_TRUE(u[VFR]);
  TEST_ASSERT_TRUE(u[IFR]);
  TEST_ASSERT_TRUE(u[NO_METAR]);
  TEST_ASSERT_FALSE(u[MVFR]);
  TEST_ASSERT_FALSE(u[LIFR]);
}

void test_blink_is_not_the_solid_color() {
  // Red blinking (no category) and solid red (IFR) are different entries.
  Fixture f = showing(flightCategoryLegend(), {led("blink", {0xff0000})});
  std::vector<bool> u = inUse(f);
  TEST_ASSERT_TRUE(u[MISSING]);
  TEST_ASSERT_FALSE(u[IFR]);
}

void test_lightning_shows_the_flash_entry_only() {
  // The look is a cycle of the flash color and the airport's; only the
  // lightning flag says what it is.
  Fixture f = showing(flightCategoryLegend(), {led("cycle", {0xffff00, 0x00ff00}, true)});
  TEST_ASSERT_EQUAL_STRING("6 ", used(inUse(f)).c_str());
}

void test_estimated_breathes_in_its_category_color() {
  // The legend shows "Estimated" in green, but an estimated MVFR breathes blue:
  // the nearest pulse entry.
  Fixture f = showing(flightCategoryLegend(), {led("pulse", {0x0000ff})});
  std::vector<bool> u = inUse(f);
  TEST_ASSERT_TRUE(u[ESTIMATED]);
  TEST_ASSERT_FALSE(u[MVFR]);
}

void test_two_color_blink_is_alternate() {
  std::vector<LegendItem> precipitation = {
      {"Rain", "Blue", "solid", {0x0000ff}},
      {"Heavy (rain)", "Fast blink, bright and dim", "alternate", {0x0000ff, 0x000040}},
  };
  Fixture f = showing(precipitation, {led("blink", {0x0000ff, 0x000040})});
  TEST_ASSERT_EQUAL_STRING("1 ", used(inUse(f)).c_str());
}

void test_gradient_entries() {
  // Temperature-like bands, each a gradient.
  std::vector<LegendItem> bands = {
      {"Cold", "Purple to blue", "solid", {0x9400d3, 0x0000ff}},
      {"Mild", "Green to yellow", "solid", {0x00ff00, 0xffff00}},
      {"Hot", "Orange to red", "solid", {0xff7e00, 0xff0000}},
  };
  // Halfway between green and yellow: only Mild.
  Fixture f = showing(bands, {led("solid", {0x80ff00})});
  TEST_ASSERT_EQUAL_STRING("1 ", used(inUse(f)).c_str());
  // An end color.
  f = showing(bands, {led("solid", {0xff0000})});
  TEST_ASSERT_EQUAL_STRING("2 ", used(inUse(f)).c_str());
}

void test_gradient_distance() {
  std::vector<uint32_t> greenToYellow = {0x00ff00, 0xffff00};
  TEST_ASSERT_FLOAT_WITHIN(1, 0, legend::gradientDist2(0x00ff00, greenToYellow));
  TEST_ASSERT_FLOAT_WITHIN(1, 0, legend::gradientDist2(0x80ff00, greenToYellow));
  // Off the line: blue is 255 away in the blue channel.
  TEST_ASSERT_FLOAT_WITHIN(1, 255 * 255, legend::gradientDist2(0x80ffff, greenToYellow));
  // Past the end is measured to the end.
  TEST_ASSERT_FLOAT_WITHIN(1, 255 * 255, legend::gradientDist2(0xff0000, {0x00ff00, 0xffff00}));
}

void test_is_gradient() {
  TEST_ASSERT_TRUE(legend::isGradient({"", "", "solid", {1, 2}}));
  TEST_ASSERT_FALSE(legend::isGradient({"", "", "solid", {1}}));
  TEST_ASSERT_FALSE(legend::isGradient({"", "", "flash", {1, 2}}));
  TEST_ASSERT_FALSE(legend::isGradient({"", "", "alternate", {1, 2}}));
}

void test_nothing_until_everything_is_the_same_visualizer() {
  Fixture f = showing(flightCategoryLegend(), {led("solid", {0x00ff00})});
  // The legend is still the old visualizer's.
  f.status.activeIndex = 3;
  TEST_ASSERT_EQUAL_STRING("", used(inUse(f, 0)).c_str());
  // The screen already shows the visualizer just picked.
  f.status.activeIndex = 0;
  TEST_ASSERT_EQUAL_STRING("", used(inUse(f, 5)).c_str());
  // No /api/live yet.
  f.live.valid = false;
  TEST_ASSERT_EQUAL_STRING("", used(inUse(f, 0)).c_str());
}

void test_strip_effect_shows_the_whole_legend() {
  std::vector<LegendItem> rainbow = {{"Rainbow", "Colors moving along the strip", "chase", {0xff0000, 0x00ff00}}};
  Fixture f = showing(rainbow, {led("cycle", {0xff0000, 0x00ff00})});
  f.live.strip = true;
  TEST_ASSERT_EQUAL_STRING("0 ", used(inUse(f)).c_str());
}

void test_nothing_matched_shows_the_whole_legend() {
  // An effect the legend doesn't have: better the whole legend than none.
  Fixture f = showing(flightCategoryLegend(), {led("cycle", {0x123456})});
  TEST_ASSERT_EQUAL_STRING("0 1 2 3 4 5 6 7 ", used(inUse(f)).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_maps_list_is_taken_at_its_word);
  RUN_TEST(test_the_map_knows_an_estimated_category);
  RUN_TEST(test_the_map_names_lightning_and_the_color_under_it);
  RUN_TEST(test_an_empty_list_from_the_map_shows_the_whole_legend);
  RUN_TEST(test_the_maps_list_still_waits_for_its_own_legend);
  RUN_TEST(test_an_older_map_is_still_matched_by_look);
  RUN_TEST(test_a_legend_without_keys_is_matched_by_look);
  RUN_TEST(test_all_vfr_shows_only_vfr);
  RUN_TEST(test_each_category_present);
  RUN_TEST(test_blink_is_not_the_solid_color);
  RUN_TEST(test_lightning_shows_the_flash_entry_only);
  RUN_TEST(test_estimated_breathes_in_its_category_color);
  RUN_TEST(test_two_color_blink_is_alternate);
  RUN_TEST(test_gradient_entries);
  RUN_TEST(test_gradient_distance);
  RUN_TEST(test_is_gradient);
  RUN_TEST(test_nothing_until_everything_is_the_same_visualizer);
  RUN_TEST(test_strip_effect_shows_the_whole_legend);
  RUN_TEST(test_nothing_matched_shows_the_whole_legend);
  return UNITY_END();
}
