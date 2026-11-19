// The backlight in the Chart's quiet hours (quiet_dim.h), on the host.
#include <unity.h>

#include "quiet_dim.h"

using quiet_dim::Backlight;
using quiet_dim::kAwakeMs;
using quiet_dim::kDim;
using quiet_dim::kNormal;

void setUp() {}
void tearDown() {}

void test_levels() {
  TEST_ASSERT_EQUAL_UINT8(200, kNormal);  // as main.cpp always set it
  TEST_ASSERT_EQUAL_UINT8(10, kDim);      // about 5% of it
  TEST_ASSERT_EQUAL_UINT32(30000, kAwakeMs);
}

void test_not_quiet_is_full() {
  Backlight b;
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(0));
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(1000000));
  TEST_ASSERT_FALSE(b.touched(5000));  // a touch outside quiet hours presses as always
}

void test_quiet_dims_at_once_when_untouched() {
  Backlight b;
  b.setQuiet(true);
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(1000));
  TEST_ASSERT_TRUE(b.dimmed(1000));
}

void test_first_touch_only_wakes() {
  Backlight b;
  b.setQuiet(true);
  TEST_ASSERT_TRUE(b.touched(1000));  // swallowed
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(1000));
  TEST_ASSERT_FALSE(b.touched(2000));  // the next one presses
}

void test_readings_of_a_held_touch_after_waking_are_not_dim() {
  Backlight b;
  b.setQuiet(true);
  TEST_ASSERT_TRUE(b.touched(1000));
  TEST_ASSERT_FALSE(b.touched(1033));  // the same finger, next reading: awake now (the caller holds the swallow)
  TEST_ASSERT_FALSE(b.touched(1066));
}

void test_redims_30_s_after_the_last_touch() {
  Backlight b;
  b.setQuiet(true);
  b.touched(1000);
  b.touched(11000);  // the last touch
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(11000 + kAwakeMs - 1));
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(11000 + kAwakeMs));
  TEST_ASSERT_TRUE(b.touched(11000 + kAwakeMs + 5));  // dim again: this touch only wakes
}

void test_quiet_ending_restores_at_once() {
  Backlight b;
  b.setQuiet(true);
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(1000));
  b.setQuiet(false);
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(1001));
  TEST_ASSERT_FALSE(b.touched(1002));
}

void test_quiet_starting_while_in_use_waits_for_30_s_untouched() {
  Backlight b;
  b.touched(5000);  // someone using it
  b.setQuiet(true);
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(6000));
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(5000 + kAwakeMs));
}

void test_quiet_starting_long_after_a_touch_dims_at_once() {
  Backlight b;
  b.touched(5000);
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(100000));
  b.setQuiet(true);
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(100001));
}

void test_millis_wrap() {
  Backlight b;
  b.setQuiet(true);
  uint32_t t = 0xFFFFFFFFu - 1000;
  TEST_ASSERT_TRUE(b.touched(t));
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(t + 10000));  // wrapped past 0
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(t + kAwakeMs));
}

void test_an_old_touch_does_not_come_back_after_a_wrap() {
  Backlight b;
  b.setQuiet(true);
  b.touched(1000);
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(1000 + kAwakeMs));  // seen ending
  // ~49.7 days later millis() is back near the old touch: still dim
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(1000 + 10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_levels);
  RUN_TEST(test_not_quiet_is_full);
  RUN_TEST(test_quiet_dims_at_once_when_untouched);
  RUN_TEST(test_first_touch_only_wakes);
  RUN_TEST(test_readings_of_a_held_touch_after_waking_are_not_dim);
  RUN_TEST(test_redims_30_s_after_the_last_touch);
  RUN_TEST(test_quiet_ending_restores_at_once);
  RUN_TEST(test_quiet_starting_while_in_use_waits_for_30_s_untouched);
  RUN_TEST(test_quiet_starting_long_after_a_touch_dims_at_once);
  RUN_TEST(test_millis_wrap);
  RUN_TEST(test_an_old_touch_does_not_come_back_after_a_wrap);
  return UNITY_END();
}
