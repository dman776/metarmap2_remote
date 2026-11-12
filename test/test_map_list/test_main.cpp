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
  return UNITY_END();
}
