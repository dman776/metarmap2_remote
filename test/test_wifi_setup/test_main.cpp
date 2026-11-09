// Wi-Fi config mode's text: the access point's name, the join QR code, a tidied scan.
#include <unity.h>

#include "wifi_setup_text.h"

void setUp() {}
void tearDown() {}

void test_ap_name_has_the_mac_suffix() {
  const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0x11, 0xa1, 0x0b};
  TEST_ASSERT_EQUAL_STRING("MetarMap2-Remote-A10B", setupApName(mac).c_str());
}

void test_qr_for_an_open_network() {
  TEST_ASSERT_EQUAL_STRING("WIFI:S:MetarMap2-Remote-A1B2;T:nopass;;", wifiQr("MetarMap2-Remote-A1B2").c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI:S:a\\;b\\:c\\,d\\\\e\\\";T:nopass;;", wifiQr("a;b:c,d\\e\"").c_str());
}

void test_scan_is_tidied() {
  std::vector<ScannedNetwork> found = {
      {"Home", -70, true}, {"", -40, true}, {"Cafe", -50, false}, {"Home", -45, true}, {"Garage", -80, true}};
  std::vector<ScannedNetwork> out = tidyScan(found);
  TEST_ASSERT_EQUAL_UINT(3, out.size());
  TEST_ASSERT_EQUAL_STRING("Home", out[0].ssid.c_str());  // its strongest, -45
  TEST_ASSERT_EQUAL_INT(-45, out[0].rssi);
  TEST_ASSERT_EQUAL_STRING("Cafe", out[1].ssid.c_str());
  TEST_ASSERT_FALSE(out[1].secure);
  TEST_ASSERT_EQUAL_STRING("Garage", out[2].ssid.c_str());
  TEST_ASSERT_EQUAL_UINT(2, tidyScan(found, 2).size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ap_name_has_the_mac_suffix);
  RUN_TEST(test_qr_for_an_open_network);
  RUN_TEST(test_scan_is_tidied);
  return UNITY_END();
}
