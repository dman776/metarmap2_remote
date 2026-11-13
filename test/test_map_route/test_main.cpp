// How MapClient reaches the Chart: by its name, by the IP it was last at while the name doesn't resolve, and back.
#include <unity.h>

#include "map_route.h"

using map_route::kNameFails;
using map_route::kRetryNameMs;
using map_route::Route;

void setUp() {}
void tearDown() {}

namespace {

// `n` failed lookups of the name, at `now`
void failLookups(Route& r, int n, uint32_t now = 1000) {
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_FALSE(r.useIp(now));
    r.nameLookedUp(false, "", now);
  }
}

}  // namespace

void test_by_name_while_it_resolves() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  TEST_ASSERT_FALSE(r.useIp(0));
  r.nameLookedUp(true, "10.0.0.5", 0);
  TEST_ASSERT_FALSE(r.byIp());
  TEST_ASSERT_EQUAL_STRING("metarmap2-a1b2c3.local", r.target().c_str());
}

void test_falls_back_to_the_ip_after_failed_lookups() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  uint32_t rev = r.rev();
  failLookups(r, kNameFails - 1);
  TEST_ASSERT_FALSE(r.byIp());  // not yet
  TEST_ASSERT_EQUAL_UINT32(rev, r.rev());
  failLookups(r, 1);
  TEST_ASSERT_TRUE(r.byIp());
  TEST_ASSERT_TRUE(r.useIp(2000));
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.target().c_str());
  TEST_ASSERT_NOT_EQUAL(rev, r.rev());  // published: spoken METARs follow
}

void test_a_lookup_that_works_resets_the_count() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  failLookups(r, kNameFails - 1);
  r.nameLookedUp(true, "10.0.0.5", 1000);
  failLookups(r, kNameFails - 1);
  TEST_ASSERT_FALSE(r.byIp());  // not in a row
}

void test_no_fallback_without_an_ip() {
  Route r;
  r.set("metarmap.local", "");
  failLookups(r, kNameFails * 3);
  TEST_ASSERT_FALSE(r.byIp());
  r.set("10.0.0.5", "10.0.0.5");  // a typed IP: nothing else to fall back to
  TEST_ASSERT_EQUAL_STRING("", r.ip().c_str());
  failLookups(r, kNameFails * 3);
  TEST_ASSERT_FALSE(r.byIp());
}

void test_the_name_again_when_the_ip_fails() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  failLookups(r, kNameFails);
  TEST_ASSERT_TRUE(r.byIp());
  r.ipFailed();  // the Chart may have moved: its name again
  TEST_ASSERT_FALSE(r.byIp());
  failLookups(r, kNameFails - 1);
  TEST_ASSERT_FALSE(r.byIp());  // the full count again
  failLookups(r, 1);
  TEST_ASSERT_TRUE(r.byIp());
}

void test_the_name_is_tried_again_after_a_while() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  failLookups(r, kNameFails, 1000);
  TEST_ASSERT_TRUE(r.useIp(1000 + kRetryNameMs - 1));
  TEST_ASSERT_FALSE(r.useIp(1000 + kRetryNameMs));  // one try of the name
  r.nameLookedUp(false, "", 1000 + kRetryNameMs);   // still nothing: straight back to the IP
  TEST_ASSERT_TRUE(r.byIp());
  // and once it resolves, by name it stays
  uint32_t later = 1000 + 2 * kRetryNameMs;
  TEST_ASSERT_FALSE(r.useIp(later));
  r.nameLookedUp(true, "10.0.0.5", later);
  TEST_ASSERT_FALSE(r.byIp());
  failLookups(r, kNameFails - 1, later);
  TEST_ASSERT_FALSE(r.byIp());
}

void test_learns_the_ip_the_name_resolves_to() {
  Route r;
  r.set("metarmap.local", "");  // typed on the web page: no IP yet
  uint32_t rev = r.rev();
  r.nameLookedUp(true, "10.0.0.9", 0);
  TEST_ASSERT_EQUAL_STRING("10.0.0.9", r.ip().c_str());
  TEST_ASSERT_NOT_EQUAL(rev, r.rev());  // the UI saves it
  rev = r.rev();
  r.nameLookedUp(true, "10.0.0.9", 0);
  TEST_ASSERT_EQUAL_UINT32(rev, r.rev());  // the same: nothing to save
  r.nameLookedUp(true, "10.0.0.12", 0);    // DHCP moved it
  TEST_ASSERT_EQUAL_STRING("10.0.0.12", r.ip().c_str());
  r.nameLookedUp(true, "0.0.0.0", 0);  // not an answer
  TEST_ASSERT_EQUAL_STRING("10.0.0.12", r.ip().c_str());
}

void test_set_starts_over_by_name() {
  Route r;
  r.set("metarmap2-a1b2c3.local", "10.0.0.5");
  failLookups(r, kNameFails);
  TEST_ASSERT_TRUE(r.byIp());
  r.set("metarmap2-d4e5f6.local", "10.0.0.6");
  TEST_ASSERT_FALSE(r.byIp());
  TEST_ASSERT_EQUAL_STRING("metarmap2-d4e5f6.local", r.target().c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.6", r.ip().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_by_name_while_it_resolves);
  RUN_TEST(test_falls_back_to_the_ip_after_failed_lookups);
  RUN_TEST(test_a_lookup_that_works_resets_the_count);
  RUN_TEST(test_no_fallback_without_an_ip);
  RUN_TEST(test_the_name_again_when_the_ip_fails);
  RUN_TEST(test_the_name_is_tried_again_after_a_while);
  RUN_TEST(test_learns_the_ip_the_name_resolves_to);
  RUN_TEST(test_set_starts_over_by_name);
  return UNITY_END();
}
