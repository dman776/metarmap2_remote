// The web page's log: a ring buffer that keeps the newest bytes, read from where the reader left off.
#include <unity.h>

#include "log_ring.h"

void setUp() {}
void tearDown() {}

void test_reads_what_came_since() {
  char buf[16];
  LogRing ring(buf, sizeof(buf));
  uint64_t at = 0;
  ring.write("abc\n", 4);
  TEST_ASSERT_EQUAL_STRING("abc\n", ring.since(at).c_str());
  TEST_ASSERT_EQUAL_UINT64(4, at);
  TEST_ASSERT_EQUAL_STRING("", ring.since(at).c_str());  // nothing new
  ring.write("de\n", 3);
  TEST_ASSERT_EQUAL_STRING("de\n", ring.since(at).c_str());
}

void test_keeps_the_newest_when_full() {
  char buf[8];
  LogRing ring(buf, sizeof(buf));
  ring.write("0123456", 7);
  ring.write("789ab", 5);  // wraps: keeps 456789ab
  uint64_t at = 0;
  TEST_ASSERT_EQUAL_STRING("...\n456789ab", ring.since(at).c_str());
  TEST_ASSERT_EQUAL_UINT64(12, ring.end());
  uint64_t recent = 10;  // still kept: no "..."
  TEST_ASSERT_EQUAL_STRING("ab", ring.since(recent).c_str());
}

void test_a_write_longer_than_the_ring() {
  char buf[4];
  LogRing ring(buf, sizeof(buf));
  ring.write("abcdefgh", 8);
  uint64_t at = 4;
  TEST_ASSERT_EQUAL_STRING("efgh", ring.since(at).c_str());
  TEST_ASSERT_EQUAL_UINT64(8, at);
}

void test_a_reader_from_before_a_restart() {
  char buf[8];
  LogRing ring(buf, sizeof(buf));
  ring.write("ab", 2);
  uint64_t at = 500;  // a page still open from before the remote restarted
  TEST_ASSERT_EQUAL_STRING("", ring.since(at).c_str());
  TEST_ASSERT_EQUAL_UINT64(2, at);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_what_came_since);
  RUN_TEST(test_keeps_the_newest_when_full);
  RUN_TEST(test_a_write_longer_than_the_ring);
  RUN_TEST(test_a_reader_from_before_a_restart);
  return UNITY_END();
}
