// The status light on the back: which color, when (status_led.h).
#include <unity.h>

#include "status_led.h"
#include "timing.h"

using namespace status_led;

void setUp() {}
void tearDown() {}

static Inputs state(bool wifi, bool online, bool setup = false, bool installing = false) {
  Inputs in;
  in.wifi = wifi;
  in.online = online;
  in.setup = setup;
  in.installing = installing;
  return in;
}

static const Rgb kOff{};

void test_full_brightness_is_the_color() {
  StatusLight l;
  Rgb c = l.tick(state(false, false), 1000, 100);
  TEST_ASSERT_TRUE(c == kBlue);
}

void test_brightness_scales_and_zero_is_off() {
  TEST_ASSERT_EQUAL_UINT8(12, scale(kBlue, 1000, 5).b);  // the 5 % default: a soft glow
  TEST_ASSERT_TRUE(scale(kBlue, 1000, 0) == kOff);
  TEST_ASSERT_EQUAL_UINT8(2, scale(kBlue, 1000, 1).b);
  TEST_ASSERT_EQUAL_UINT8(1, scale(Rgb{0, 0, 50}, 1000, 1).b);  // 0.5: never rounds a lit color down to dark
  StatusLight l;
  TEST_ASSERT_TRUE(l.tick(state(true, false), 0, 0) == kOff);
}

void test_joining_wifi_blinks_blue_then_stays_blue() {
  StatusLight l;
  TEST_ASSERT_TRUE(l.tick(state(false, false), 0, 100) == kBlue);
  TEST_ASSERT_TRUE(l.tick(state(false, false), kBlinkMs, 100) == kOff);  // the blink's dark half
  TEST_ASSERT_TRUE(l.tick(state(false, false), 2 * kBlinkMs, 100) == kBlue);
  // given up (as the splash names the network): steady
  TEST_ASSERT_TRUE(l.tick(state(false, false), kWifiTimeoutMs + kBlinkMs, 100) == kBlue);
  TEST_ASSERT_TRUE(l.tick(state(false, false), kWifiTimeoutMs + 3 * kBlinkMs, 100) == kBlue);
}

void test_green_after_reaching_the_chart_then_fades_to_dark() {
  StatusLight l;
  l.tick(state(false, false), 0, 100);
  TEST_ASSERT_TRUE(l.tick(state(true, true), 1000, 100) == kGreen);
  TEST_ASSERT_TRUE(l.tick(state(true, true), 1000 + kGreenMs - 1, 100) == kGreen);
  Rgb mid = l.tick(state(true, true), 1000 + kGreenMs + kFadeMs / 2, 100);
  TEST_ASSERT_TRUE(mid.g > 0 && mid.g < 255);
  TEST_ASSERT_TRUE(l.tick(state(true, true), 1000 + kGreenMs + kFadeMs, 100) == kOff);
  TEST_ASSERT_TRUE(l.tick(state(true, true), 600000, 100) == kOff);  // dark while all is well
}

void test_red_when_the_chart_stops_answering_after_the_splash_time() {
  StatusLight l;
  l.tick(state(true, true), 0, 100);
  l.tick(state(true, true), 60000, 100);
  TEST_ASSERT_TRUE(l.tick(state(true, false), 61000, 100) == kOff);  // a blip
  TEST_ASSERT_TRUE(l.tick(state(true, false), 61000 + kLostMs - 1, 100) == kOff);
  TEST_ASSERT_TRUE(l.tick(state(true, false), 61000 + kLostMs, 100) == kRed);  // with the splash
  TEST_ASSERT_TRUE(l.tick(state(true, true), 70000, 100) == kGreen);           // back: green again, then dark
}

void test_wifi_up_but_the_chart_never_answered() {
  StatusLight l;
  l.tick(state(false, false), 0, 100);
  TEST_ASSERT_TRUE(l.tick(state(true, false), 2000, 100) == kBlue);  // still connecting: blinking blue
  TEST_ASSERT_TRUE(l.tick(state(true, false), 2000 + kBlinkMs, 100) == kOff);
  TEST_ASSERT_TRUE(l.tick(state(true, false), 2000 + kLostMs, 100) == kRed);
}

void test_losing_wifi_blinks_blue_again() {
  StatusLight l;
  l.tick(state(true, true), 0, 100);
  TEST_ASSERT_TRUE(l.tick(state(false, false), 100000, 100) == kBlue);
  TEST_ASSERT_TRUE(l.tick(state(false, false), 100000 + kBlinkMs, 100) == kOff);
}

void test_setup_mode_breathes_purple() {
  StatusLight l;
  Rgb dim = l.tick(state(false, false, true), 0, 100);
  Rgb full = l.tick(state(false, false, true), kBreatheMs / 2, 100);
  TEST_ASSERT_TRUE(full == kPurple);
  TEST_ASSERT_TRUE(dim.r > 0 && dim.r < full.r && dim.g == 0);  // never quite dark
}

void test_installing_blinks_yellow_over_everything() {
  StatusLight l;
  TEST_ASSERT_TRUE(l.tick(state(true, true, false, true), 0, 100) == kYellow);
  TEST_ASSERT_TRUE(l.tick(state(true, true, false, true), kUpdateBlinkMs, 100) == kOff);
  TEST_ASSERT_TRUE(l.tick(state(true, false, true, true), 2 * kUpdateBlinkMs, 100) == kYellow);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_full_brightness_is_the_color);
  RUN_TEST(test_brightness_scales_and_zero_is_off);
  RUN_TEST(test_joining_wifi_blinks_blue_then_stays_blue);
  RUN_TEST(test_green_after_reaching_the_chart_then_fades_to_dark);
  RUN_TEST(test_red_when_the_chart_stops_answering_after_the_splash_time);
  RUN_TEST(test_wifi_up_but_the_chart_never_answered);
  RUN_TEST(test_losing_wifi_blinks_blue_again);
  RUN_TEST(test_setup_mode_breathes_purple);
  RUN_TEST(test_installing_blinks_yellow_over_everything);
  return UNITY_END();
}
