// The picker's skip marks: holds marked at once, the Chart's answers, two quick holds, failures, timeouts.
#include <unity.h>

#include "skips.h"

using skips::Hold;
using skips::Marks;
using skips::Said;

void setUp() {}
void tearDown() {}

namespace {

SkipAnswer ok(uint32_t seq, const char* id, bool skipped, std::vector<std::string> skip) {
  SkipAnswer a;
  a.seq = seq;
  a.id = id;
  a.httpCode = 200;
  a.ok = true;
  a.skipped = skipped;
  a.skip = std::move(skip);
  return a;
}

SkipAnswer failed(uint32_t seq, const char* id, int code, const char* reason) {
  SkipAnswer a;
  a.seq = seq;
  a.id = id;
  a.httpCode = code;
  a.reason = reason;
  return a;
}

// a Chart that says it skips `ids`
Marks chart(std::vector<std::string> ids, uint32_t now = 1000) {
  Marks m;
  m.polled(true, ids, now);
  return m;
}

void assertShown(const Marks& m, std::vector<std::string> want) {
  TEST_ASSERT_EQUAL_INT(want.size(), m.shown().size());
  for (size_t i = 0; i < want.size(); i++) TEST_ASSERT_EQUAL_STRING(want[i].c_str(), m.shown()[i].c_str());
}

}  // namespace

void test_polled_list_is_shown() {
  Marks m = chart({"wind"});
  TEST_ASSERT_TRUE(m.known());
  TEST_ASSERT_TRUE(m.isSkipped("wind"));
  TEST_ASSERT_FALSE(m.isSkipped("gusts"));
  uint32_t rev = m.revision();
  m.polled(true, {"wind"}, 2000);
  TEST_ASSERT_EQUAL_UINT32(rev, m.revision());  // the same list: nothing to redraw
  m.polled(true, {}, 3000);
  TEST_ASSERT_NOT_EQUAL(rev, m.revision());
  TEST_ASSERT_FALSE(m.isSkipped("wind"));
}

void test_hold_and_answer() {
  Marks m = chart({});
  TEST_ASSERT_EQUAL_INT((int)Hold::Ask, (int)m.hold(false));
  m.asked(1, "wind", true, 2000);
  TEST_ASSERT_TRUE(m.isSkipped("wind"));  // marked at once
  TEST_ASSERT_TRUE(m.waiting());
  m.polled(true, {}, 2500);  // a poll from before the Chart did it: the mark stays
  TEST_ASSERT_TRUE(m.isSkipped("wind"));
  TEST_ASSERT_EQUAL_INT((int)Said::Skipped, (int)m.answered(ok(1, "wind", true, {"wind"}), 3000));
  TEST_ASSERT_FALSE(m.waiting());
  assertShown(m, {"wind"});
}

void test_unskip_and_answer() {
  Marks m = chart({"wind", "gusts"});
  m.asked(1, "wind", false, 2000);
  assertShown(m, {"gusts"});
  TEST_ASSERT_EQUAL_INT((int)Said::BackInRotation, (int)m.answered(ok(1, "wind", false, {"gusts"}), 3000));
  assertShown(m, {"gusts"});
}

void test_answer_list_is_the_truth() {
  // the Cockpit skipped Temp meanwhile: the answer's whole list says so
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.answered(ok(1, "wind", true, {"temp", "wind"}), 3000);
  assertShown(m, {"temp", "wind"});
}

void test_poll_after_answer_waits_to_settle() {
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.answered(ok(1, "wind", true, {"wind"}), 3000);
  m.polled(true, {}, 3000 + skips::kSettleMs - 1);  // an older list (the status's): not taken yet
  assertShown(m, {"wind"});
  m.polled(true, {}, 3000 + skips::kSettleMs);  // then the Chart's word again (unskipped on the Cockpit)
  assertShown(m, {});
}

void test_two_quick_holds_keep_both_marks() {
  // hold Wind, then Gusts before Wind's answer: Wind's answer (without Gusts yet) mustn't drop Gusts' badge
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.asked(2, "gusts", true, 2100);
  assertShown(m, {"wind", "gusts"});
  TEST_ASSERT_EQUAL_INT((int)Said::Skipped, (int)m.answered(ok(1, "wind", true, {"wind"}), 3000));
  assertShown(m, {"wind", "gusts"});
  TEST_ASSERT_TRUE(m.waiting());
  m.answered(ok(2, "gusts", true, {"gusts", "wind"}), 3500);
  assertShown(m, {"gusts", "wind"});
  TEST_ASSERT_FALSE(m.waiting());
}

void test_answer_overwritten_before_seen() {
  // the screen saw only the second answer: it ends both holds
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.asked(2, "gusts", true, 2100);
  m.answered(ok(2, "gusts", true, {"gusts", "wind"}), 4000);
  TEST_ASSERT_FALSE(m.waiting());
  assertShown(m, {"gusts", "wind"});
}

void test_hold_twice_same_tile() {
  // skip then unskip Wind quickly: the last hold wins on top of the first's answer
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.asked(2, "wind", false, 2200);
  assertShown(m, {});
  m.answered(ok(1, "wind", true, {"wind"}), 3000);
  assertShown(m, {});
  m.answered(ok(2, "wind", false, {}), 3500);
  assertShown(m, {});
}

void test_failed_answer_reverts() {
  Marks m = chart({"temp"});
  m.asked(1, "wind", true, 2000);
  TEST_ASSERT_TRUE(m.isSkipped("wind"));
  TEST_ASSERT_EQUAL_INT((int)Said::Failed, (int)m.answered(failed(1, "wind", -1, ""), 9000));
  assertShown(m, {"temp"});
  TEST_ASSERT_FALSE(m.waiting());
  m.polled(true, {}, 9100);  // no settling after a failure: the next poll is the Chart's word
  assertShown(m, {});
}

void test_failed_answer_keeps_other_hold() {
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  m.asked(2, "gusts", true, 2100);
  m.answered(failed(1, "wind", -1, ""), 5000);
  assertShown(m, {"gusts"});
  TEST_ASSERT_TRUE(m.waiting());
}

void test_failure_reasons() {
  Marks m = chart({"a", "b"});
  m.asked(1, "c", true, 2000);
  TEST_ASSERT_EQUAL_INT((int)Said::Last, (int)m.answered(failed(1, "c", 400, "last"), 3000));
  assertShown(m, {"a", "b"});
  m.asked(2, "rainbow", true, 3100);
  TEST_ASSERT_EQUAL_INT((int)Said::Exclusive, (int)m.answered(failed(2, "rainbow", 400, "exclusive"), 3200));
  m.asked(3, "x", true, 3300);
  TEST_ASSERT_EQUAL_INT((int)Said::Failed, (int)m.answered(failed(3, "x", 400, "unknown"), 3400));
}

void test_dropped_command_resolves_quietly_after_timeout() {
  // the Wi-Fi went: the hold times out and is undone, then MapClient drops the old command and answers it failed
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  TEST_ASSERT_TRUE(m.expire(2000 + skips::kWaitMs - 1).empty());
  TEST_ASSERT_TRUE(m.isSkipped("wind"));
  std::vector<std::string> late = m.expire(2000 + skips::kWaitMs);
  TEST_ASSERT_EQUAL_INT(1, late.size());
  TEST_ASSERT_EQUAL_STRING("wind", late[0].c_str());
  TEST_ASSERT_FALSE(m.isSkipped("wind"));
  TEST_ASSERT_FALSE(m.waiting());
  // minutes later: nothing to say, nothing marked, and a newer hold isn't ended by it
  m.asked(2, "gusts", true, 200000);
  TEST_ASSERT_EQUAL_INT((int)Said::Nothing, (int)m.answered(failed(1, "wind", 0, ""), 200100));
  TEST_ASSERT_TRUE(m.waiting());
  assertShown(m, {"gusts"});
}

void test_dropped_command_before_timeout_fails() {
  // queued behind slow requests past kQueuedMs: dropped, its failure ends the hold before the screen gives up
  Marks m = chart({});
  m.asked(1, "wind", true, 2000);
  TEST_ASSERT_EQUAL_INT((int)Said::Failed, (int)m.answered(failed(1, "wind", 0, ""), 2000 + skips::kQueuedMs));
  TEST_ASSERT_FALSE(m.waiting());
  assertShown(m, {});
}

void test_wait_covers_the_worst_answer() {
  // a command sent just before kQueuedMs, then a whole request (3 s connect, 5 s answer): still in time
  TEST_ASSERT_TRUE(skips::kWaitMs >= skips::kQueuedMs + 8000);
}

void test_old_chart() {
  Marks m;
  m.polled(false, {}, 1000);
  TEST_ASSERT_FALSE(m.known());
  TEST_ASSERT_EQUAL_INT((int)Hold::OldChart, (int)m.hold(false));
  TEST_ASSERT_EQUAL_INT((int)Hold::OldChart, (int)m.hold(true));
}

void test_exclusive_hold() {
  Marks m = chart({});
  TEST_ASSERT_EQUAL_INT((int)Hold::Exclusive, (int)m.hold(true));
}

void test_queue_full_not_counted() {
  // requestSkip() said 0: nothing to wait for and nothing marked
  Marks m = chart({});
  uint32_t rev = m.revision();
  m.asked(0, "wind", true, 1500);
  TEST_ASSERT_FALSE(m.waiting());
  TEST_ASSERT_FALSE(m.isSkipped("wind"));
  TEST_ASSERT_EQUAL_UINT32(rev, m.revision());
  m.polled(true, {"temp"}, 2000);  // polls are taken as before
  assertShown(m, {"temp"});
}

void test_millis_wrap() {
  uint32_t t = 0xFFFFF000u;  // 4 s before millis() wraps
  Marks m = chart({}, t);
  m.asked(1, "wind", true, t);
  uint32_t later = t + 10000;  // past the wrap: 10 s on, still waiting
  TEST_ASSERT_TRUE(later < t);
  TEST_ASSERT_TRUE(m.expire(later).empty());
  TEST_ASSERT_TRUE(m.isSkipped("wind"));
  TEST_ASSERT_EQUAL_INT(1, m.expire(t + skips::kWaitMs).size());
  // and the settling after an answer, across the wrap
  Marks n = chart({}, t);
  n.asked(1, "wind", true, t);
  n.answered(ok(1, "wind", true, {"wind"}), t + 2000);
  n.polled(true, {}, t + 2000 + skips::kSettleMs - 1);
  assertShown(n, {"wind"});
  n.polled(true, {}, t + 2000 + skips::kSettleMs);
  assertShown(n, {});
}

void test_seq_wrap() {
  // the command numbers wrap round too (0 is never one): the newest answer still ends the older holds
  Marks m = chart({});
  m.asked(0xFFFFFFFFu, "wind", true, 1000);
  m.asked(1, "gusts", true, 1100);
  m.answered(ok(1, "gusts", true, {"gusts", "wind"}), 2000);
  TEST_ASSERT_FALSE(m.waiting());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_polled_list_is_shown);
  RUN_TEST(test_hold_and_answer);
  RUN_TEST(test_unskip_and_answer);
  RUN_TEST(test_answer_list_is_the_truth);
  RUN_TEST(test_poll_after_answer_waits_to_settle);
  RUN_TEST(test_two_quick_holds_keep_both_marks);
  RUN_TEST(test_answer_overwritten_before_seen);
  RUN_TEST(test_hold_twice_same_tile);
  RUN_TEST(test_failed_answer_reverts);
  RUN_TEST(test_failed_answer_keeps_other_hold);
  RUN_TEST(test_failure_reasons);
  RUN_TEST(test_dropped_command_resolves_quietly_after_timeout);
  RUN_TEST(test_dropped_command_before_timeout_fails);
  RUN_TEST(test_wait_covers_the_worst_answer);
  RUN_TEST(test_old_chart);
  RUN_TEST(test_exclusive_hold);
  RUN_TEST(test_queue_full_not_counted);
  RUN_TEST(test_millis_wrap);
  RUN_TEST(test_seq_wrap);
  return UNITY_END();
}
