// The volume's speaker symbol (muted at 0%) and the pop on letting go of a volume slider.
#include <unity.h>

#include "volume_ui.h"

using volume_ui::Icon;

void setUp() {}
void tearDown() {}

void test_icon_muted_at_zero() {
  TEST_ASSERT_TRUE(volume_ui::icon(0) == Icon::Mute);
  TEST_ASSERT_TRUE(volume_ui::icon(-5) == Icon::Mute);  // never below 0, but muted if so
}

void test_icon_mid_up_to_half() {
  TEST_ASSERT_TRUE(volume_ui::icon(1) == Icon::Mid);
  TEST_ASSERT_TRUE(volume_ui::icon(50) == Icon::Mid);
}

void test_icon_max_above_half() {
  TEST_ASSERT_TRUE(volume_ui::icon(51) == Icon::Max);
  TEST_ASSERT_TRUE(volume_ui::icon(100) == Icon::Max);
}

void test_no_pop_when_muted() {
  TEST_ASSERT_FALSE(volume_ui::popOnRelease(0));
  TEST_ASSERT_TRUE(volume_ui::popOnRelease(1));
  TEST_ASSERT_TRUE(volume_ui::popOnRelease(100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_icon_muted_at_zero);
  RUN_TEST(test_icon_mid_up_to_half);
  RUN_TEST(test_icon_max_above_half);
  RUN_TEST(test_no_pop_when_muted);
  return UNITY_END();
}
