// The grace before a Chart that misses /api/live answers counts as offline (offline_grace.h), on the host.
#include <unity.h>

#include "map_state.h"
#include "offline_grace.h"
#include "timing.h"

using offline_grace::Grace;
using offline_grace::Verdict;

void setUp() {}
void tearDown() {}

// The connection as MapClient keeps it: a poll's failure goes through the grace (conn.missing while in it), an
// answer is online, and Wi-Fi loss is offline at once (MapClient::run() calls setConnection(false, ...) and resets
// the grace).
struct Link {
  Grace grace{kOfflineGraceMs};
  Connection conn;
  bool& online = conn.online;
  int firstMisses = 0;  // the "poll failed, retrying" log lines

  Link() {
    conn.wifi = true;
    conn.online = true;
  }
  void fail(uint32_t now) {
    Verdict v = grace.failed(now, online);
    if (v == Verdict::kFirstMiss) {
      firstMisses++;
      conn.missing = true;  // setMissing(true)
    }
    if (v == Verdict::kOffline) online = conn.missing = false;  // setConnection(true, false, code)
  }
  uint32_t answer(uint32_t now) {
    uint32_t ran = grace.answered(now);
    if (ran) conn.missing = false;  // setMissing(false)
    online = true;
    return ran;
  }
  void wifiLost() {
    conn.wifi = online = conn.missing = false;
    grace.reset();
  }
};

void test_grace_is_15_s() { TEST_ASSERT_EQUAL_UINT32(15000, kOfflineGraceMs); }

void test_single_failure_stays_online() {
  Link l;
  l.fail(10000);
  TEST_ASSERT_TRUE(l.online);
  TEST_ASSERT_EQUAL(1, l.firstMisses);
  TEST_ASSERT_TRUE(l.grace.missing());
}

void test_failures_within_the_grace_stay_online_and_log_once() {
  Link l;
  for (uint32_t t = 10000; t < 10000 + kOfflineGraceMs - 1; t += 1500) l.fail(t);
  TEST_ASSERT_TRUE(l.online);
  TEST_ASSERT_EQUAL(1, l.firstMisses);
}

void test_15_s_of_failures_goes_offline() {
  Link l;
  // Polls that time out (5 s each) back to back, as a stalled Chart gives them
  l.fail(10000);
  l.fail(15000);
  l.fail(20000);
  l.fail(24999);
  TEST_ASSERT_TRUE(l.online);  // 14.999 s
  l.fail(25000);
  TEST_ASSERT_FALSE(l.online);  // 15 s after the first
  TEST_ASSERT_FALSE(l.grace.missing());
}

void test_once_offline_a_failure_stays_offline_without_a_new_run() {
  Link l;
  l.fail(0);
  l.fail(kOfflineGraceMs);
  TEST_ASSERT_FALSE(l.online);
  l.fail(kOfflineGraceMs + 1500);
  TEST_ASSERT_FALSE(l.online);
  TEST_ASSERT_EQUAL(1, l.firstMisses);  // no "retrying" while offline
  TEST_ASSERT_FALSE(l.grace.missing());
}

void test_a_success_resets_the_run() {
  Link l;
  l.fail(10000);
  l.fail(20000);
  TEST_ASSERT_EQUAL_UINT32(12000, l.answer(22000));  // answering again after 12 s; never offline
  TEST_ASSERT_TRUE(l.online);
  TEST_ASSERT_FALSE(l.grace.missing());
  // A new run counts from its own first failure: 26 s after the first run started is only 2 s into this one
  l.fail(24000);
  l.fail(36000);
  TEST_ASSERT_TRUE(l.online);
  TEST_ASSERT_EQUAL(2, l.firstMisses);
  l.fail(39000);
  TEST_ASSERT_FALSE(l.online);
}

void test_answer_without_a_run_reports_none() {
  Link l;
  TEST_ASSERT_EQUAL_UINT32(0, l.answer(5000));  // nothing to log
  l.fail(6000);
  TEST_ASSERT_EQUAL_UINT32(1, l.answer(6000));  // a run, however short, is reported
}

void test_back_online_after_going_offline_is_not_a_grace_answer() {
  Link l;
  l.fail(0);
  l.fail(kOfflineGraceMs);
  TEST_ASSERT_FALSE(l.online);
  TEST_ASSERT_EQUAL_UINT32(0, l.answer(kOfflineGraceMs + 3000));  // setConnection logs "online" instead
  TEST_ASSERT_TRUE(l.online);
}

void test_wifi_loss_is_immediate() {
  Link l;
  l.fail(10000);  // inside a run
  l.wifiLost();
  TEST_ASSERT_FALSE(l.online);
  TEST_ASSERT_FALSE(l.grace.missing());
  // Wi-Fi back, the Chart not answering yet: offline already, so no grace and the failure's code shows at once
  TEST_ASSERT_TRUE(l.grace.failed(12000, l.online) == Verdict::kOffline);
}

void test_answering_only_outside_the_grace() {
  Link l;
  TEST_ASSERT_TRUE(answering(l.conn));
  l.fail(1000);
  TEST_ASSERT_TRUE(l.online);            // the screen stays as it was
  TEST_ASSERT_FALSE(answering(l.conn));  // but nothing is sent to the Chart
  l.fail(1000 + kOfflineGraceMs - 1);
  TEST_ASSERT_FALSE(answering(l.conn));
  l.answer(1000 + kOfflineGraceMs - 1);
  TEST_ASSERT_TRUE(answering(l.conn));  // answering again
  l.fail(30000);
  l.fail(30000 + kOfflineGraceMs);
  TEST_ASSERT_FALSE(l.online);
  TEST_ASSERT_FALSE(l.conn.missing);  // offline is no grace
  TEST_ASSERT_FALSE(answering(l.conn));
  l.answer(50000);
  TEST_ASSERT_TRUE(answering(l.conn));
}

void test_answering() {
  Connection c;
  TEST_ASSERT_FALSE(answering(c));  // starting up
  c.online = true;
  TEST_ASSERT_TRUE(answering(c));
  c.missing = true;
  TEST_ASSERT_FALSE(answering(c));
}

void test_wifi_loss_ends_the_grace() {
  Link l;
  l.fail(1000);
  TEST_ASSERT_TRUE(l.conn.missing);
  l.wifiLost();
  TEST_ASSERT_FALSE(l.conn.missing);
  TEST_ASSERT_FALSE(answering(l.conn));
}

void test_offline_already_has_no_grace() {
  Grace g(kOfflineGraceMs);
  // starting up, or a map switch: the connection isn't online
  TEST_ASSERT_TRUE(g.failed(1000, false) == Verdict::kOffline);
  TEST_ASSERT_FALSE(g.missing());
}

void test_clock_wrapping() {
  Link l;
  l.fail(0xFFFFF000u);
  l.fail(0x00001000u);  // 8 s later, across the wrap
  TEST_ASSERT_TRUE(l.online);
  l.fail(0xFFFFF000u + kOfflineGraceMs - 1);
  TEST_ASSERT_TRUE(l.online);
  l.fail(0xFFFFF000u + kOfflineGraceMs);  // 15 s after the first
  TEST_ASSERT_FALSE(l.online);
}

void test_first_failure_at_zero_ms() {
  Link l;
  l.fail(0);  // the clock's start: still a run
  TEST_ASSERT_TRUE(l.grace.missing());
  l.fail(kOfflineGraceMs);
  TEST_ASSERT_FALSE(l.online);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_grace_is_15_s);
  RUN_TEST(test_single_failure_stays_online);
  RUN_TEST(test_failures_within_the_grace_stay_online_and_log_once);
  RUN_TEST(test_15_s_of_failures_goes_offline);
  RUN_TEST(test_once_offline_a_failure_stays_offline_without_a_new_run);
  RUN_TEST(test_a_success_resets_the_run);
  RUN_TEST(test_answer_without_a_run_reports_none);
  RUN_TEST(test_back_online_after_going_offline_is_not_a_grace_answer);
  RUN_TEST(test_wifi_loss_is_immediate);
  RUN_TEST(test_answering_only_outside_the_grace);
  RUN_TEST(test_answering);
  RUN_TEST(test_wifi_loss_ends_the_grace);
  RUN_TEST(test_offline_already_has_no_grace);
  RUN_TEST(test_clock_wrapping);
  RUN_TEST(test_first_failure_at_zero_ms);
  return UNITY_END();
}
