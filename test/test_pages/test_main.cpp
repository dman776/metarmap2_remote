// The Airports panel's pages: how many, which page an airport is on, the alphabetical order and the strip.
#include <unity.h>

#include "pages.h"

void setUp() {}
void tearDown() {}

void test_count() {
  TEST_ASSERT_EQUAL_INT(5, pages::count(50, 12));
  TEST_ASSERT_EQUAL_INT(4, pages::count(48, 12));
  TEST_ASSERT_EQUAL_INT(1, pages::count(12, 12));
  TEST_ASSERT_EQUAL_INT(1, pages::count(0, 12));  // an empty map still has its (empty) page
  TEST_ASSERT_EQUAL_INT(1, pages::count(5, 0));
}

void test_of() {
  TEST_ASSERT_EQUAL_INT(0, pages::of(0, 12));
  TEST_ASSERT_EQUAL_INT(0, pages::of(11, 12));
  TEST_ASSERT_EQUAL_INT(1, pages::of(12, 12));
  TEST_ASSERT_EQUAL_INT(4, pages::of(49, 12));
  TEST_ASSERT_EQUAL_INT(0, pages::of(-1, 12));
}

void test_alphabetical() {
  std::vector<int> order = pages::alphabetical({"KIAH", "KBMT", "KT78", "KCLL", "KBMT"});
  std::vector<int> want = {1, 4, 3, 0, 2};  // the two KBMT (one airport on two LEDs) keep their order
  TEST_ASSERT_EQUAL_INT(want.size(), order.size());
  for (size_t i = 0; i < want.size(); i++) TEST_ASSERT_EQUAL_INT(want[i], order[i]);
}

void test_strip_a_button_per_page() {
  auto s = pages::strip({"KABI", "KDAL", "KGTU", "KMAF", "KUTS"}, 1, 6);
  TEST_ASSERT_EQUAL_INT(5, s.size());
  TEST_ASSERT_EQUAL_INT(pages::Button::Page, s[2].kind);
  TEST_ASSERT_EQUAL_INT(2, s[2].page);
  TEST_ASSERT_EQUAL_STRING("KGTU", s[2].label.c_str());
}

void test_strip_one_page_has_none() { TEST_ASSERT_EQUAL_INT(0, pages::strip({"KABI"}, 0, 6).size()); }

void test_strip_too_many_pages_steps() {
  std::vector<std::string> firsts = {"A", "B", "C", "D", "E", "F", "G", "H", "I"};
  auto s = pages::strip(firsts, 2, 6);
  TEST_ASSERT_EQUAL_INT(3, s.size());
  TEST_ASSERT_EQUAL_INT(pages::Button::Previous, s[0].kind);
  TEST_ASSERT_EQUAL_INT(1, s[0].page);
  TEST_ASSERT_EQUAL_STRING("3 / 9", s[1].label.c_str());
  TEST_ASSERT_EQUAL_INT(3, s[2].page);
  // at the ends, the step that goes nowhere is -1 (disabled)
  TEST_ASSERT_EQUAL_INT(-1, pages::strip(firsts, 0, 6)[0].page);
  TEST_ASSERT_EQUAL_INT(-1, pages::strip(firsts, 8, 6)[2].page);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_count);
  RUN_TEST(test_of);
  RUN_TEST(test_alphabetical);
  RUN_TEST(test_strip_a_button_per_page);
  RUN_TEST(test_strip_one_page_has_none);
  RUN_TEST(test_strip_too_many_pages_steps);
  return UNITY_END();
}
