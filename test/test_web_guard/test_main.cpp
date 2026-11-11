// The web server's guards against other web pages: the POSTs' X-Copilot header, the Host (DNS rebinding), and
// remote control with the default password.
#include <unity.h>

#include "settings.h"
#include "web_guard.h"

void setUp() {}
void tearDown() {}

void test_header_is_one_only() {
  TEST_ASSERT_TRUE(web_guard::headerOk("1"));
  TEST_ASSERT_FALSE(web_guard::headerOk(""));  // missing: what a forged form or a simple fetch sends
  TEST_ASSERT_FALSE(web_guard::headerOk("0"));
  TEST_ASSERT_FALSE(web_guard::headerOk("yes"));
  TEST_ASSERT_FALSE(web_guard::headerOk("11"));
}

void test_ip_addresses_are_accepted() {
  // any IPv4 address: a page can't move an IP address to itself, and it's how you reach a Copilot without mDNS
  const char* ok[] = {"192.168.1.60", "192.168.1.60:80", "192.168.4.1",     "10.0.0.7:8080",
                      "127.0.0.1",    "0.0.0.0",         "255.255.255.255", "192.168.1.60."};
  for (const char* h : ok) TEST_ASSERT_TRUE_MESSAGE(web_guard::hostOk(h), h);
  TEST_ASSERT_TRUE(web_guard::hostOk("[fe80::1]"));
  TEST_ASSERT_TRUE(web_guard::hostOk("[fe80::1]:80"));
  TEST_ASSERT_TRUE(web_guard::hostOk("[::ffff:192.168.1.60]"));
}

void test_the_copilots_names_are_accepted() {
  const char* ok[] = {"metarmap-copilot.local",  "metarmap-copilot.local:80", "METARMAP-Copilot.Local",
                      "metarmap-copilot.local.", "metarmap-copilot",          "metarmap-copilot:80"};
  for (const char* h : ok) TEST_ASSERT_TRUE_MESSAGE(web_guard::hostOk(h), h);
}

void test_other_names_are_refused() {
  // a rebinding page's own name, and look-alikes of the Copilot's or of an IP address
  const char* bad[] = {"evil.example.com",
                       "evil.example.com:80",
                       "metarmap-copilot.evil.com",
                       "metarmap-copilot.local.evil.com",
                       "xmetarmap-copilot.local",
                       "metarmap.local",  // the Chart's, not the Copilot's
                       "metarmap-copilot.lan",
                       "192.168.1.60.nip.io",
                       "192-168-1-60.nip.io",
                       "1.2.3",
                       "1.2.3.4.5",
                       "1.2.3.256",
                       "1..2.3",
                       "0x7f.0.0.1",
                       "localhost",
                       "",  // none: a browser always sends one
                       "192.168.1.60:",
                       "192.168.1.60:http",
                       "[fe80::1]x",
                       "[]",
                       "[evil.example.com]"};
  for (const char* h : bad) TEST_ASSERT_FALSE_MESSAGE(web_guard::hostOk(h), h);
}

void test_ipv4() {
  TEST_ASSERT_TRUE(web_guard::isIpv4("1.2.3.4"));
  TEST_ASSERT_FALSE(web_guard::isIpv4("1.2.3.4."));
  TEST_ASSERT_FALSE(web_guard::isIpv4("1.2.3.-4"));
  TEST_ASSERT_FALSE(web_guard::isIpv4("1.2.3.1000"));
  TEST_ASSERT_FALSE(web_guard::isIpv4(""));
}

void test_default_password() {
  TEST_ASSERT_TRUE(web_guard::isDefaultPassword(kDefaultWebPassword));
  TEST_ASSERT_TRUE(web_guard::isDefaultPassword("map"));
  TEST_ASSERT_FALSE(web_guard::isDefaultPassword("Map"));  // the login is case-sensitive: Map is one of your own
  TEST_ASSERT_FALSE(web_guard::isDefaultPassword("map "));
  TEST_ASSERT_FALSE(web_guard::isDefaultPassword("s3cret"));
  TEST_ASSERT_FALSE(web_guard::isDefaultPassword(""));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_is_one_only);
  RUN_TEST(test_ip_addresses_are_accepted);
  RUN_TEST(test_the_copilots_names_are_accepted);
  RUN_TEST(test_other_names_are_refused);
  RUN_TEST(test_ipv4);
  RUN_TEST(test_default_password);
  return UNITY_END();
}
