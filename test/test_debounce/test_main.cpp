// One action per press: the touch tick's and the card buttons' debounce.
#include <unity.h>

#include "debounce.h"

using debounce::Gate;

void setUp() {}
void tearDown() {}

void test_first_press_goes_through() {
  Gate g(700);
  TEST_ASSERT_TRUE(g.accept(0));  // at 0 ms too (the clock's start)
}

void test_repeat_within_the_window_is_ignored() {
  Gate g(700);
  TEST_ASSERT_TRUE(g.accept(1000));
  TEST_ASSERT_FALSE(g.accept(1100));
  TEST_ASSERT_FALSE(g.accept(1699));
  TEST_ASSERT_TRUE(g.accept(1701));
}

void test_window_counts_from_the_one_let_through() {
  Gate g(120);
  TEST_ASSERT_TRUE(g.accept(5000));
  TEST_ASSERT_FALSE(g.accept(5060));  // ignored ones don't stretch it
  TEST_ASSERT_FALSE(g.accept(5110));
  TEST_ASSERT_TRUE(g.accept(5121));
}

void test_clock_wrapping() {
  Gate g(700);
  TEST_ASSERT_TRUE(g.accept(0xFFFFFF00u));
  TEST_ASSERT_FALSE(g.accept(0x00000100u));  // 512 ms later, across the wrap
  TEST_ASSERT_TRUE(g.accept(0x00000300u));   // 1024 ms later
}

void test_due() {
  TEST_ASSERT_TRUE(debounce::due(0, 5, 700));  // none yet
  TEST_ASSERT_FALSE(debounce::due(100, 799, 700));
  TEST_ASSERT_TRUE(debounce::due(100, 800, 700));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_press_goes_through);
  RUN_TEST(test_repeat_within_the_window_is_ignored);
  RUN_TEST(test_window_counts_from_the_one_let_through);
  RUN_TEST(test_clock_wrapping);
  RUN_TEST(test_due);
  return UNITY_END();
}
