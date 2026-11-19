// The backlight in the Chart's quiet hours (quiet_dim.h), on the host.
#include <unity.h>

#include "quiet_dim.h"

using quiet_dim::Backlight;
using quiet_dim::kAwakeMs;
using quiet_dim::kDim;
using quiet_dim::kNormal;
using quiet_dim::Touch;

constexpr uint32_t kRelease = 60;  // lvgl_port.cpp's kReleaseMs
constexpr uint32_t kRead = 33;     // LVGL reads the touch this often

void setUp() {}
void tearDown() {}

void test_levels() {
  TEST_ASSERT_EQUAL_UINT8(200, kNormal);  // as main.cpp always set it
  TEST_ASSERT_EQUAL_UINT8(24, kDim);      // about 10% of full (255)
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

// ---- Touch: what LVGL sees of the controller's readings ----

// A finger down from `from` for `ms`, read every kRead; how many readings pressed, and the ticks
struct Run {
  int pressed = 0;
  int readings = 0;
  int clicks = 0;
};
Run hold(Touch& t, uint32_t from, uint32_t ms) {
  Run r;
  for (uint32_t now = from; now < from + ms; now += kRead) {
    Touch::Reading x = t.read(true, now);
    r.readings++;
    r.pressed += x.pressed;
    r.clicks += x.click;
  }
  return r;
}

void test_normal_touch_passes_through_with_one_tick() {
  Backlight b;
  Touch t(b, kRelease);
  Run r = hold(t, 1000, 200);
  TEST_ASSERT_EQUAL(r.readings, r.pressed);
  TEST_ASSERT_EQUAL(1, r.clicks);
  TEST_ASSERT_FALSE(t.waking());
}

void test_waking_touch_is_swallowed_from_down_to_lift() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  Run r = hold(t, 1000, 300);  // a slow tap, or a drag: the readings' positions don't matter
  TEST_ASSERT_EQUAL(0, r.pressed);
  TEST_ASSERT_EQUAL(0, r.clicks);  // no tick for a touch that only wakes
  TEST_ASSERT_TRUE(t.waking());
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(1300));
  // lifted: still not pressed while the release delay runs, nor after
  TEST_ASSERT_FALSE(t.read(false, 1320).pressed);
  TEST_ASSERT_TRUE(t.down());
  TEST_ASSERT_FALSE(t.read(false, 1300 + kRelease).pressed);
  TEST_ASSERT_FALSE(t.down());
  TEST_ASSERT_FALSE(t.waking());
}

void test_a_long_drag_stays_swallowed_and_keeps_it_awake() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  Run r = hold(t, 1000, kAwakeMs + 5000);  // longer than the awake time: each reading counts as a touch
  TEST_ASSERT_EQUAL(0, r.pressed);
  TEST_ASSERT_EQUAL(0, r.clicks);
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(1000 + kAwakeMs + 5000));
}

void test_a_missed_reading_mid_touch_is_the_same_touch() {
  Backlight b;
  Touch t(b, kRelease);
  hold(t, 1000, 100);
  Touch::Reading gap = t.read(false, 1100);  // the controller loses the finger for a reading
  TEST_ASSERT_TRUE(gap.pressed);
  Run r = hold(t, 1100 + kRead, 100);
  TEST_ASSERT_EQUAL(r.readings, r.pressed);
  TEST_ASSERT_EQUAL(0, r.clicks);  // not a new press: no second tick
}

void test_a_missed_reading_mid_waking_touch_stays_swallowed() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  hold(t, 1000, 100);
  TEST_ASSERT_FALSE(t.read(false, 1100).pressed);
  Run r = hold(t, 1100 + kRead, 100);  // back within the release delay: the same, waking touch
  TEST_ASSERT_EQUAL(0, r.pressed);
  TEST_ASSERT_EQUAL(0, r.clicks);
}

void test_lifted_after_the_release_delay() {
  Backlight b;
  Touch t(b, kRelease);
  hold(t, 1000, 100);  // last seen at 1000 + 99
  uint32_t lastSeen = 1000 + 99;
  TEST_ASSERT_TRUE(t.read(false, lastSeen + kRelease - 1).pressed);
  TEST_ASSERT_FALSE(t.read(false, lastSeen + kRelease).pressed);
  TEST_ASSERT_FALSE(t.read(false, lastSeen + kRelease + kRead).pressed);
}

void test_a_second_tap_after_a_waking_tap_presses() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  hold(t, 1000, 100);  // wakes
  uint32_t lastSeen = 1000 + 99;
  t.read(false, lastSeen + kRead);
  t.read(false, lastSeen + 2 * kRead);  // lifted (66 ms >= the release delay)
  TEST_ASSERT_FALSE(t.down());
  // a quick second tap, the next reading after the lift
  Run r = hold(t, lastSeen + 3 * kRead, 100);
  TEST_ASSERT_EQUAL(r.readings, r.pressed);
  TEST_ASSERT_EQUAL(1, r.clicks);
}

void test_a_touch_after_it_redims_only_wakes_again() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  hold(t, 1000, 100);
  t.read(false, 1000 + 99 + kRelease);
  uint32_t later = 1000 + 99 + kAwakeMs;
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(later));
  Run r = hold(t, later, 100);
  TEST_ASSERT_EQUAL(0, r.pressed);
  TEST_ASSERT_EQUAL(0, r.clicks);
}

void test_quiet_ending_mid_waking_touch_keeps_it_swallowed() {
  Backlight b;
  b.setQuiet(true);
  Touch t(b, kRelease);
  hold(t, 1000, 100);
  b.setQuiet(false);  // that touch still only woke it: it doesn't turn into a press halfway
  Run r = hold(t, 1100, 100);
  TEST_ASSERT_EQUAL(0, r.pressed);
}

// The sounds (the tick, the chime, the pops): muted while quiet hours have the screen dim, back while it's awake
void test_sounds_muted_while_quiet_and_dim() {
  Backlight b;
  b.setQuiet(true);
  TEST_ASSERT_TRUE(b.muted(1000));
  TEST_ASSERT_TRUE(b.muted(500000));
}

void test_sounds_back_while_awake_after_a_touch() {
  Backlight b;
  b.setQuiet(true);
  b.touched(1000);  // the waking touch
  TEST_ASSERT_FALSE(b.muted(1000));
  TEST_ASSERT_FALSE(b.muted(1000 + kAwakeMs - 1));
  b.touched(20000);  // a later touch keeps them on
  TEST_ASSERT_FALSE(b.muted(20000 + kAwakeMs - 1));
}

void test_sounds_muted_again_as_it_redims() {
  Backlight b;
  b.setQuiet(true);
  b.touched(1000);
  TEST_ASSERT_FALSE(b.muted(1000 + kAwakeMs - 1));
  TEST_ASSERT_TRUE(b.muted(1000 + kAwakeMs));
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(1000 + kAwakeMs));  // muted exactly when dim
}

void test_sounds_never_muted_outside_quiet_hours() {
  Backlight b;
  TEST_ASSERT_FALSE(b.muted(0));
  TEST_ASSERT_FALSE(b.muted(1000000));
  b.touched(5000);
  TEST_ASSERT_FALSE(b.muted(5000 + kAwakeMs + 1));
  b.setQuiet(true);
  TEST_ASSERT_TRUE(b.muted(5000 + kAwakeMs + 2));
  b.setQuiet(false);  // quiet hours end: on at once
  TEST_ASSERT_FALSE(b.muted(5000 + kAwakeMs + 3));
}

void test_sounds_mute_across_a_millis_wrap() {
  Backlight b;
  b.setQuiet(true);
  uint32_t t = 0xFFFFFFFFu - 10000;
  b.touched(t);
  TEST_ASSERT_FALSE(b.muted(t + 10000));              // still before the wrap
  TEST_ASSERT_FALSE(b.muted(t + kAwakeMs - 1));       // wrapped past 0, still awake
  TEST_ASSERT_TRUE(b.muted(t + kAwakeMs));            // re-dimmed after the wrap
  TEST_ASSERT_TRUE(b.muted(t + kAwakeMs + 0x10000));  // and stays muted
}

// A restart starts from the `quiet` the Chart last said (saved: lvgl_port_start_quiet()), till the Chart answers
void test_restart_with_saved_quiet_starts_dim_and_muted() {
  Backlight b;
  b.setQuiet(true);  // saved
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(0));
  TEST_ASSERT_TRUE(b.muted(0));
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(60000));  // the Chart not answering: stays dim
  TEST_ASSERT_TRUE(b.touched(60000));             // a touch still wakes it (and only wakes it)
  TEST_ASSERT_FALSE(b.muted(60000));
}

void test_restart_first_answer_not_quiet_is_full_at_once() {
  Backlight b;
  b.setQuiet(true);  // saved
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(500));
  b.setQuiet(false);  // the Chart's first answer (or one without `quiet`, or a map switch)
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(501));
  TEST_ASSERT_FALSE(b.muted(501));
}

void test_restart_first_answer_quiet_stays_dim() {
  Backlight b;
  b.setQuiet(true);  // saved
  b.setQuiet(true);  // the Chart's first answer
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(2000));
  TEST_ASSERT_TRUE(b.muted(2000));
}

void test_restart_with_saved_not_quiet_starts_full() {
  Backlight b;
  b.setQuiet(false);  // saved (or nothing saved yet)
  TEST_ASSERT_EQUAL_UINT8(kNormal, b.level(0));
  TEST_ASSERT_FALSE(b.muted(0));
  b.setQuiet(true);  // the Chart's first answer: dims at once (nobody has touched it)
  TEST_ASSERT_EQUAL_UINT8(kDim, b.level(3000));
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
  RUN_TEST(test_normal_touch_passes_through_with_one_tick);
  RUN_TEST(test_waking_touch_is_swallowed_from_down_to_lift);
  RUN_TEST(test_a_long_drag_stays_swallowed_and_keeps_it_awake);
  RUN_TEST(test_a_missed_reading_mid_touch_is_the_same_touch);
  RUN_TEST(test_a_missed_reading_mid_waking_touch_stays_swallowed);
  RUN_TEST(test_lifted_after_the_release_delay);
  RUN_TEST(test_a_second_tap_after_a_waking_tap_presses);
  RUN_TEST(test_a_touch_after_it_redims_only_wakes_again);
  RUN_TEST(test_quiet_ending_mid_waking_touch_keeps_it_swallowed);
  RUN_TEST(test_sounds_muted_while_quiet_and_dim);
  RUN_TEST(test_sounds_back_while_awake_after_a_touch);
  RUN_TEST(test_sounds_muted_again_as_it_redims);
  RUN_TEST(test_sounds_never_muted_outside_quiet_hours);
  RUN_TEST(test_sounds_mute_across_a_millis_wrap);
  RUN_TEST(test_restart_with_saved_quiet_starts_dim_and_muted);
  RUN_TEST(test_restart_first_answer_not_quiet_is_full_at_once);
  RUN_TEST(test_restart_first_answer_quiet_stays_dim);
  RUN_TEST(test_restart_with_saved_not_quiet_starts_full);
  return UNITY_END();
}
