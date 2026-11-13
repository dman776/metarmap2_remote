// The map picker's list: the Charts a scan found and the one in use, their addresses, versions and order.
#include <unity.h>

#include "map_list.h"

using map_list::Current;
using map_list::Found;
using map_list::Row;

void setUp() {}
void tearDown() {}

namespace {

Found chart(const char* name, const char* ip, uint16_t port = 80, const char* host = "", const char* version = "") {
  Found f;
  f.name = name;
  f.ip = ip;
  f.port = port;
  f.hostname = host;
  f.version = version;
  return f;
}

Current saved(const char* host, uint16_t port = 80, const char* ip = "") {
  Current c;
  c.host = host;
  c.port = port;
  c.ip = ip;
  return c;
}

}  // namespace

void test_address_leaves_out_port_80() {
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", map_list::address("192.168.1.50", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50:8080", map_list::address("192.168.1.50", 8080).c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap.local:5000", map_list::address("metarmap.local", 5000).c_str());
}

void test_version_text() {
  TEST_ASSERT_EQUAL_STRING("v26.11.10", map_list::versionText("26.11.10").c_str());
  TEST_ASSERT_EQUAL_STRING("v26.11.10", map_list::versionText("v26.11.10").c_str());
  TEST_ASSERT_EQUAL_STRING("dev", map_list::versionText("dev").c_str());
  TEST_ASSERT_EQUAL_STRING("", map_list::versionText("").c_str());
}

void test_host_to_save_prefers_the_mdns_name() {
  // ESP-IDF gives the SRV target without its domain; a fully qualified one has its trailing dot
  TEST_ASSERT_EQUAL_STRING("metarmap2-a1b2c3.local",
                           map_list::hostToSave(chart("A", "10.0.0.5", 80, "metarmap2-a1b2c3")).c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap2-a1b2c3.local",
                           map_list::hostToSave(chart("A", "10.0.0.5", 80, "Metarmap2-A1B2C3.local.")).c_str());
  // none, or one the web page wouldn't take: the IP
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", map_list::hostToSave(chart("A", "10.0.0.5")).c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", map_list::hostToSave(chart("A", "10.0.0.5", 80, "bad host")).c_str());
  std::string longName(70, 'a');
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", map_list::hostToSave(chart("A", "10.0.0.5", 80, longName.c_str())).c_str());
  TEST_ASSERT_EQUAL_STRING("", map_list::hostToSave(chart("A", "")).c_str());
}

void test_is_current_by_host_name_or_ip_and_port() {
  Found f = chart("Houston map", "10.0.0.5", 80, "metarmap2-a1b2c3");
  TEST_ASSERT_TRUE(map_list::isCurrent(f, saved("metarmap2-a1b2c3.local")));
  TEST_ASSERT_TRUE(map_list::isCurrent(f, saved("METARMAP2-A1B2C3.local.")));
  TEST_ASSERT_TRUE(map_list::isCurrent(f, saved("10.0.0.5")));
  // a typed name (metarmap.local) is matched by the IP its /api/status gave
  TEST_ASSERT_TRUE(map_list::isCurrent(f, saved("metarmap.local", 80, "10.0.0.5")));
  TEST_ASSERT_FALSE(map_list::isCurrent(f, saved("metarmap.local")));
  TEST_ASSERT_FALSE(map_list::isCurrent(f, saved("10.0.0.5", 8080)));  // another port: another server
  TEST_ASSERT_FALSE(map_list::isCurrent(f, saved("")));
}

void test_rows_sorted_by_name_with_the_current_marked() {
  std::vector<Found> found = {
      chart("houston map", "10.0.0.7", 80, "metarmap2-000007", "26.11.10"),
      chart("Austin", "10.0.0.5", 8080, "metarmap2-000005", "26.11.09"),
      chart("Dallas", "10.0.0.6", 80, "metarmap2-000006"),
  };
  std::vector<Row> r = map_list::rows(found, saved("metarmap2-000006.local"));
  TEST_ASSERT_EQUAL_INT(3, r.size());
  TEST_ASSERT_EQUAL_STRING("Austin", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5:8080", r[0].address.c_str());
  TEST_ASSERT_EQUAL_STRING("v26.11.09", r[0].version.c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap2-000005.local", r[0].host.c_str());
  TEST_ASSERT_EQUAL_INT(8080, r[0].port);
  TEST_ASSERT_FALSE(r[0].current);
  TEST_ASSERT_EQUAL_STRING("Dallas", r[1].name.c_str());
  TEST_ASSERT_TRUE(r[1].current);
  TEST_ASSERT_TRUE(r[1].announced);
  TEST_ASSERT_EQUAL_STRING("", r[1].version.c_str());
  TEST_ASSERT_EQUAL_STRING("houston map", r[2].name.c_str());  // any case
  TEST_ASSERT_FALSE(r[2].current);
}

void test_rows_dedupe_by_address() {
  // the same Chart heard twice (two answers, or IPv4 and IPv6 results): one row, the first
  std::vector<Found> found = {
      chart("Houston map", "10.0.0.5", 80, "metarmap2-a1b2c3"),
      chart("Houston map (again)", "10.0.0.5", 80, "metarmap2-a1b2c3"),
      chart("Second server", "10.0.0.5", 8080),  // another port on the same Pi: another map
  };
  std::vector<Row> r = map_list::rows(found, saved("10.0.0.5"));
  TEST_ASSERT_EQUAL_INT(2, r.size());
  TEST_ASSERT_EQUAL_STRING("Houston map", r[0].name.c_str());
  TEST_ASSERT_TRUE(r[0].current);
  TEST_ASSERT_EQUAL_STRING("Second server", r[1].name.c_str());
  TEST_ASSERT_FALSE(r[1].current);
}

void test_merge_answers_from_one_chart() {
  // its IPv6 answer (no IPv4 address, but the version) and its IPv4 one (the address): one Chart, with both
  std::vector<Found> found = {
      chart("Houston map", "", 80, "metarmap2-a1b2c3", "26.11.10"),
      chart("Houston map", "10.0.0.5", 80, "metarmap2-a1b2c3.local."),
      chart("Austin", "", 80, "metarmap2-0000aa"),
  };
  std::vector<Found> m = map_list::merge(found);
  TEST_ASSERT_EQUAL_INT(2, m.size());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", m[0].ip.c_str());
  TEST_ASSERT_EQUAL_STRING("26.11.10", m[0].version.c_str());
  TEST_ASSERT_EQUAL_STRING("", m[1].ip.c_str());  // still to be asked for (map_scan.cpp)
}

void test_rows_skip_a_chart_with_no_address() {
  std::vector<Found> found = {chart("Nowhere", ""), chart("Named only", "", 80, "metarmap2-0000aa")};
  std::vector<Row> r = map_list::rows(found, saved(""));
  TEST_ASSERT_EQUAL_INT(1, r.size());  // and no current row: nothing is saved
  TEST_ASSERT_EQUAL_STRING("Named only", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap2-0000aa.local", r[0].address.c_str());  // no IP: its name, to reach it by
}

void test_current_map_shown_when_it_didnt_announce() {
  Current c = saved("metarmap.local", 5000, "10.0.0.9");
  c.version = "v26.11.04";
  std::vector<Row> r = map_list::rows({chart("Austin", "10.0.0.5")}, c);
  TEST_ASSERT_EQUAL_INT(2, r.size());
  TEST_ASSERT_EQUAL_STRING("Austin", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap.local", r[1].name.c_str());  // no name from /api/status: its saved host
  TEST_ASSERT_EQUAL_STRING("metarmap.local:5000", r[1].address.c_str());
  TEST_ASSERT_EQUAL_STRING("v26.11.04", r[1].version.c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap.local", r[1].host.c_str());
  TEST_ASSERT_TRUE(r[1].current);
  TEST_ASSERT_FALSE(r[1].announced);

  c.name = "Den map";  // a Chart that has a name but doesn't announce (no zeroconf package)
  r = map_list::rows({}, c);
  TEST_ASSERT_EQUAL_INT(1, r.size());
  TEST_ASSERT_EQUAL_STRING("Den map", r[0].name.c_str());
}

void test_rows_capped_keeping_the_current() {
  std::vector<Found> found;
  for (int i = 0; i < 12; i++) {
    std::string ip = "10.0.0." + std::to_string(10 + i);
    std::string name = std::string("Map ") + (char)('A' + i);
    found.push_back(chart(name.c_str(), ip.c_str()));
  }
  std::vector<Row> r = map_list::rows(found, saved("10.0.0.21"));  // Map L: last by name
  TEST_ASSERT_EQUAL_INT(map_list::kMaxMaps, r.size());
  TEST_ASSERT_EQUAL_STRING("Map A", r[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Map G", r[map_list::kMaxMaps - 2].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Map L", r.back().name.c_str());
  TEST_ASSERT_TRUE(r.back().current);

  r = map_list::rows(found, saved("10.0.0.11"), 3);  // Map B: already within the cap
  TEST_ASSERT_EQUAL_INT(3, r.size());
  TEST_ASSERT_EQUAL_STRING("Map C", r[2].name.c_str());
  TEST_ASSERT_TRUE(r[1].current);
}

void test_rows_nothing_found_nothing_saved() { TEST_ASSERT_EQUAL_INT(0, map_list::rows({}, saved("")).size()); }

void test_merge_leaves_out_port_0() {
  // an answer without its service record (SRV) has no port: nothing to connect to
  std::vector<Found> m = map_list::merge({chart("Houston", "10.0.0.5", 0), chart("Austin", "10.0.0.6")});
  TEST_ASSERT_EQUAL_INT(1, m.size());
  TEST_ASSERT_EQUAL_STRING("Austin", m[0].name.c_str());
  TEST_ASSERT_EQUAL_INT(0, map_list::rows({chart("Houston", "10.0.0.5", 0)}, saved("")).size());
}

void test_row_keeps_the_ip_to_fall_back_to() {
  Row r = map_list::toRow(chart("Houston", "10.0.0.5", 80, "metarmap2-a1b2c3"));
  TEST_ASSERT_EQUAL_STRING("metarmap2-a1b2c3.local", r.host.c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.ip.c_str());
  std::vector<Row> rs = map_list::rows({}, saved("metarmap.local", 80, "10.0.0.9"));
  TEST_ASSERT_EQUAL_STRING("10.0.0.9", rs[0].ip.c_str());  // the current one: its /api/status IP
}

void test_plain_text_for_the_fonts() {
  TEST_ASSERT_EQUAL_STRING("Houston", map_list::plainText("Houston").c_str());
  TEST_ASSERT_EQUAL_STRING("Zurich", map_list::plainText("Z\xC3\xBCrich").c_str());        // Zürich
  TEST_ASSERT_EQUAL_STRING("Sao Paulo", map_list::plainText("S\xC3\xA3o Paulo").c_str());  // São Paulo
  TEST_ASSERT_EQUAL_STRING("ESPANA", map_list::plainText("ESPA\xC3\x91"
                                                         "A")
                                         .c_str());  // ESPAÑA
  TEST_ASSERT_EQUAL_STRING("Darryl's map", map_list::plainText("Darryl\xE2\x80\x99s map").c_str());
  TEST_ASSERT_EQUAL_STRING("A - B...", map_list::plainText("A \xE2\x80\x93 B\xE2\x80\xA6").c_str());
  TEST_ASSERT_EQUAL_STRING("Tokyo ??", map_list::plainText("Tokyo \xE6\x9D\xB1\xE4\xBA\xAC").c_str());  // 東京
  TEST_ASSERT_EQUAL_STRING("map ?", map_list::plainText("map \xF0\x9F\x97\xBA").c_str());  // an emoji: one '?'
  TEST_ASSERT_EQUAL_STRING("a b", map_list::plainText("a\tb").c_str());
  // broken UTF-8: a '?' a byte, never reading past the end
  TEST_ASSERT_EQUAL_STRING("a?b", map_list::plainText("a\xC3"
                                                      "b")
                                      .c_str());
  TEST_ASSERT_EQUAL_STRING("x??", map_list::plainText("x\xE2\x80").c_str());
  TEST_ASSERT_EQUAL_STRING("??", map_list::plainText("\x80\xFF").c_str());
}

void test_splash_name() {
  // the one just picked; else the one in use by its /api/status name, its saved name, its address
  TEST_ASSERT_EQUAL_STRING("Houston", map_list::splashName("Houston", "Austin", "Dallas", "h.local", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("Austin", map_list::splashName("", "Austin", "Dallas", "h.local", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("Dallas", map_list::splashName("", "", "Dallas", "h.local", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap.local", map_list::splashName("", "", "", "metarmap.local", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5:8080", map_list::splashName("", "", "", "10.0.0.5", 8080).c_str());
  TEST_ASSERT_EQUAL_STRING("Zurich", map_list::splashName("Z\xC3\xBCrich", "", "", "h", 80).c_str());
  std::string longName(40, 'W');  // whole: the screen shortens it, only if it's wider
  TEST_ASSERT_EQUAL_STRING(longName.c_str(), map_list::splashName(longName, "", "", "h", 80).c_str());
}

void test_map_chosen_and_the_migration() {
  TEST_ASSERT_FALSE(map_list::mapChosen(false, false, false));  // a first start: nothing saved
  TEST_ASSERT_TRUE(map_list::mapChosen(false, false, true));    // older firmware saved a map: an existing Copilot
  TEST_ASSERT_FALSE(map_list::mapChosen(true, false, true));    // this firmware's setup page, address left as shown
  TEST_ASSERT_TRUE(map_list::mapChosen(true, true, true));
  TEST_ASSERT_TRUE(map_list::mapChosen(true, true, false));
}

namespace {

map_list::AutoInput fresh(int scans = 1) {
  map_list::AutoInput in;
  in.scans = scans;
  return in;
}

}  // namespace

void test_auto_pick_one_map() {
  map_list::AutoDecision d =
      map_list::autoPick({chart("Houston", "10.0.0.5", 80, "metarmap2-a1b2c3", "26.11.12")}, fresh());
  TEST_ASSERT_TRUE(d.step == map_list::AutoStep::Pick);
  TEST_ASSERT_EQUAL_STRING("Houston", d.pick.name.c_str());
  TEST_ASSERT_EQUAL_STRING("metarmap2-a1b2c3.local", d.pick.host.c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", d.pick.ip.c_str());
  TEST_ASSERT_EQUAL_INT(80, d.pick.port);
  // one Chart heard twice (IPv4 and IPv6) is one
  d = map_list::autoPick(
      {chart("Houston", "10.0.0.5", 80, "metarmap2-a1b2c3"), chart("Houston", "", 80, "metarmap2-a1b2c3")}, fresh());
  TEST_ASSERT_TRUE(d.step == map_list::AutoStep::Pick);
}

void test_auto_pick_two_maps_open_the_picker() {
  map_list::AutoDecision d = map_list::autoPick({chart("Houston", "10.0.0.5"), chart("Austin", "10.0.0.6")}, fresh());
  TEST_ASSERT_TRUE(d.step == map_list::AutoStep::OpenPicker);
}

void test_auto_pick_none_scans_again_then_stops() {
  for (int scans = 1; scans < map_list::kAutoScans; scans++) {
    TEST_ASSERT_TRUE(map_list::autoPick({}, fresh(scans)).step == map_list::AutoStep::Rescan);
  }
  TEST_ASSERT_TRUE(map_list::autoPick({}, fresh(map_list::kAutoScans)).step == map_list::AutoStep::Nothing);
  // a failed scan (not on the network yet) is as none, its answers (if any) not trusted
  map_list::AutoInput in = fresh();
  in.scanFailed = true;
  TEST_ASSERT_TRUE(map_list::autoPick({chart("Houston", "10.0.0.5")}, in).step == map_list::AutoStep::Rescan);
  // answers with nothing to reach them by are none
  TEST_ASSERT_TRUE(map_list::autoPick({chart("Houston", ""), chart("X", "10.0.0.7", 0)}, fresh()).step ==
                   map_list::AutoStep::Rescan);
}

void test_auto_pick_never_over_a_chosen_or_answering_map() {
  std::vector<Found> one = {chart("Houston", "10.0.0.5")};
  std::vector<Found> two = {chart("Houston", "10.0.0.5"), chart("Austin", "10.0.0.6")};
  map_list::AutoInput chosen = fresh();
  chosen.chosen = true;
  TEST_ASSERT_TRUE(map_list::autoPick(one, chosen).step == map_list::AutoStep::Nothing);
  TEST_ASSERT_TRUE(map_list::autoPick(two, chosen).step == map_list::AutoStep::Nothing);
  TEST_ASSERT_TRUE(map_list::autoPick({}, chosen).step == map_list::AutoStep::Nothing);
  map_list::AutoInput answering = fresh();
  answering.answering = true;  // the default address answered before the scan was over: left as it is
  TEST_ASSERT_TRUE(map_list::autoPick(one, answering).step == map_list::AutoStep::Nothing);
  TEST_ASSERT_TRUE(map_list::autoPick(two, answering).step == map_list::AutoStep::Nothing);
  map_list::AutoInput picking = fresh();
  picking.pickerOpen = true;  // the user has the picker open: theirs to pick
  TEST_ASSERT_TRUE(map_list::autoPick(one, picking).step == map_list::AutoStep::Nothing);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_address_leaves_out_port_80);
  RUN_TEST(test_version_text);
  RUN_TEST(test_host_to_save_prefers_the_mdns_name);
  RUN_TEST(test_is_current_by_host_name_or_ip_and_port);
  RUN_TEST(test_rows_sorted_by_name_with_the_current_marked);
  RUN_TEST(test_rows_dedupe_by_address);
  RUN_TEST(test_merge_answers_from_one_chart);
  RUN_TEST(test_rows_skip_a_chart_with_no_address);
  RUN_TEST(test_current_map_shown_when_it_didnt_announce);
  RUN_TEST(test_rows_capped_keeping_the_current);
  RUN_TEST(test_rows_nothing_found_nothing_saved);
  RUN_TEST(test_merge_leaves_out_port_0);
  RUN_TEST(test_row_keeps_the_ip_to_fall_back_to);
  RUN_TEST(test_plain_text_for_the_fonts);
  RUN_TEST(test_splash_name);
  RUN_TEST(test_map_chosen_and_the_migration);
  RUN_TEST(test_auto_pick_one_map);
  RUN_TEST(test_auto_pick_two_maps_open_the_picker);
  RUN_TEST(test_auto_pick_none_scans_again_then_stops);
  RUN_TEST(test_auto_pick_never_over_a_chosen_or_answering_map);
  return UNITY_END();
}
