// Waiting on the network in turns (net_wait.h), on the host: a fake connection whose bytes come at set times.
#include <ArduinoJson.h>
#include <unity.h>

#include <string>
#include <utility>
#include <vector>

#include "net_wait.h"

void setUp() {}
void tearDown() {}

// `arrivals[i]` is when byte i comes, in ms after the start; the clock moves `turn` ms each wait(). Read in blocks
// (BufferedSource), as on the board.
class FakeSource : public BufferedSource {
 public:
  FakeSource(std::string bytes, std::vector<uint32_t> arrivals) : bytes_(std::move(bytes)), at_(std::move(arrivals)) {}
  uint32_t now() override { return clock; }
  bool wanted() override { return stopAt == 0 || clock - start < stopAt; }
  // What the connection still has, read straight from it (as stream.cpp's body loop does), not through the blocks.
  std::string rest() {
    std::string out = bytes_.substr(next_);
    next_ = bytes_.size();
    return out;
  }
  void wait() override {
    clock += turn;
    waits++;
  }

  uint32_t clock = 0, start = 0;
  uint32_t turn = 20;
  bool closes = false;  // it closes once it has sent its bytes
  uint32_t stopAt = 0;  // ms after the start that the answer is no longer wanted (0: always wanted)
  int waits = 0;
  int fills = 0;  // calls into the "network stack" (an lwip ioctl each on the board)

 protected:
  int fill(uint8_t* buf, size_t max) override {  // what has come, up to `max`: never waits
    fills++;
    size_t n = 0;
    while (n < max && next_ < bytes_.size() && at_[next_] <= clock - start) buf[n++] = bytes_[next_++];
    return static_cast<int>(n);
  }
  bool connectionOpen() override { return !closes || next_ < bytes_.size(); }

 private:
  std::string bytes_;
  std::vector<uint32_t> at_;
  size_t next_ = 0;
};

FakeSource at(const std::string& bytes, uint32_t ms) {
  return FakeSource(bytes, std::vector<uint32_t>(bytes.size(), ms));
}

// ---- readLine ----

void test_line_at_once() {
  FakeSource src = at("HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\n", 0);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 1000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("HTTP/1.0 200 OK", line.c_str());
  TEST_ASSERT_TRUE(readLine(src, 1000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Content-Type: audio/wav", line.c_str());
  TEST_ASSERT_EQUAL_INT(0, src.waits);  // a fast answer never waits
}

void test_a_slow_answer_waits_in_turns() {
  FakeSource src = at("HTTP/1.0 200 OK\r\n", 30000);  // an ATIS rendered for half a minute
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 120000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("HTTP/1.0 200 OK", line.c_str());
  TEST_ASSERT_EQUAL_INT(1500, src.waits);  // it yielded all the while (30 s in 20 ms turns)
}

void test_a_line_in_pieces() {
  FakeSource src("Server: x\n", {0, 0, 0, 500, 500, 500, 500, 900, 900, 900});
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Server: x", line.c_str());
}

void test_no_answer_by_the_deadline() {
  FakeSource src = at("HTTP/1.0 200 OK\r\n", 200000);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 120000, line) == NetResult::TimedOut);
  TEST_ASSERT_UINT32_WITHIN(20, 120000, src.clock);
}

void test_the_deadline_across_the_clock_wrap() {
  FakeSource src = at("x\n", 100000);
  src.clock = src.start = 0xFFFFFF00u;  // millis() wraps every 49 days
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, src.clock + 10000, line) == NetResult::TimedOut);
  TEST_ASSERT_UINT32_WITHIN(20, 10000, src.clock - src.start);
}

void test_stop_ends_the_wait() {
  FakeSource src = at("HTTP/1.0 200 OK\r\n", 30000);
  src.stopAt = 1000;  // Stop pressed a second in
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 120000, line) == NetResult::Stopped);
  TEST_ASSERT_UINT32_WITHIN(20, 1000, src.clock);
}

void test_closed_without_answering() {
  FakeSource src = at("", 0);
  src.closes = true;
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 120000, line) == NetResult::Closed);
  TEST_ASSERT_EQUAL_INT(0, src.waits);
}

void test_the_last_line_without_a_newline() {
  FakeSource src = at("Spoken weather is off", 0);  // the map's why, then it closes
  src.closes = true;
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("Spoken weather is off", line.c_str());
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Closed);
}

void test_a_blank_line() {
  FakeSource src = at("\r\nRIFF", 0);
  std::string line = "old";
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("", line.c_str());  // the end of the headers
}

void test_a_long_line_is_cut() {
  FakeSource src = at(std::string(600, 'a') + "\nnext\n", 0);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line, 100) == NetResult::Ok);
  TEST_ASSERT_EQUAL_UINT(100, line.size());
  TEST_ASSERT_TRUE(readLine(src, 5000, line, 100) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("next", line.c_str());  // the rest of the long one was dropped
}

void test_a_line_that_never_ends() {
  // a byte every 10 ms and never a newline: the deadline still ends it
  std::vector<uint32_t> times;
  for (uint32_t i = 0; i < 2000; i++) times.push_back(i * 10);
  FakeSource src(std::string(2000, 'a'), times);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::TimedOut);
  TEST_ASSERT_UINT32_WITHIN(20, 5000, src.clock);
}

void test_a_line_already_here_at_the_deadline() {
  // the status line came just as the deadline passed: it counts, it isn't a timeout
  FakeSource src = at("HTTP/1.0 200 OK\r\n", 0);
  src.clock = 5000;
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("HTTP/1.0 200 OK", line.c_str());
  TEST_ASSERT_EQUAL_INT(0, src.waits);
}

void test_a_line_half_here_at_the_deadline() {
  // the rest of it would have to be waited for: that's a timeout, without waiting
  FakeSource src("HTTP/1.0 200 OK\r\n",
                 {0, 0, 0, 0, 0, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000});
  src.clock = 5000;
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::TimedOut);
  TEST_ASSERT_EQUAL_INT(0, src.waits);
}

void test_a_cut_line_past_the_deadline_ends() {
  // past `maxLen` the deadline counts with each byte, so a line that never ends can't hold the task while it keeps
  // coming faster than it's read
  FakeSource src = at(std::string(600, 'a') + "\n", 0);
  src.clock = 5000;
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line, 100) == NetResult::TimedOut);
}

void test_a_long_header_line() {
  // a Location (a signed URL, say) is longer than a status line may be
  std::string location = "Location: https://example.com/" + std::string(1500, 'x');
  FakeSource src = at(location + "\r\n\r\n", 0);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line, kHeaderLineMax) == NetResult::Ok);
  TEST_ASSERT_EQUAL_UINT(location.size(), line.size());
  TEST_ASSERT_TRUE(readLine(src, 5000, line, kHeaderLineMax) == NetResult::Ok);
  TEST_ASSERT_EQUAL_STRING("", line.c_str());
}

// ---- reading in blocks ----

void test_reads_in_blocks_not_bytes() {
  // a 20 KB /api/live answer: one call into the network stack per block, not one (or two) per byte
  std::string body(20000, 'x');
  FakeSource src = at(body, 0);
  src.closes = true;
  size_t got = 0;
  while (src.read() >= 0) got++;
  TEST_ASSERT_EQUAL_UINT(body.size(), got);
  TEST_ASSERT_LESS_OR_EQUAL_INT(static_cast<int>(body.size() / BufferedSource::kBlock) + 2, src.fills);
}

void test_a_block_takes_only_what_has_come() {
  FakeSource src("abcdef", {0, 0, 0, 500, 500, 500});
  TEST_ASSERT_EQUAL_INT('a', src.read());
  TEST_ASSERT_EQUAL_UINT(2, src.unread());  // "bc": it didn't wait for the rest
  TEST_ASSERT_EQUAL_INT('b', src.read());
  TEST_ASSERT_EQUAL_INT('c', src.read());
  TEST_ASSERT_EQUAL_INT(-1, src.read());
  TEST_ASSERT_TRUE(src.open());
  TEST_ASSERT_EQUAL_INT(0, src.waits);
}

void test_the_body_after_the_headers_is_not_lost() {
  // the headers' last block holds the WAV's first bytes: handed back, they and the rest are the whole body
  std::string body = "RIFF";
  for (int i = 0; i < 300; i++) body.push_back(static_cast<char>(i));
  FakeSource src = at("HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: 304\r\n\r\n" + body, 0);
  std::string line;
  do {
    TEST_ASSERT_TRUE(readLine(src, 5000, line, kHeaderLineMax) == NetResult::Ok);
  } while (!line.empty());
  TEST_ASSERT_TRUE(src.unread() > 0);  // it did read ahead
  uint8_t ahead[BufferedSource::kBlock];
  size_t n = src.takeUnread(ahead, sizeof(ahead));
  TEST_ASSERT_EQUAL_UINT(0, src.unread());
  std::string whole = std::string(reinterpret_cast<char*>(ahead), n) + src.rest();
  TEST_ASSERT_EQUAL_UINT(body.size(), whole.size());
  TEST_ASSERT_TRUE(whole == body);
}

void test_take_unread_in_parts() {
  FakeSource src = at("x\nabcdef", 0);
  std::string line;
  TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  uint8_t two[2];
  TEST_ASSERT_EQUAL_UINT(2, src.takeUnread(two, sizeof(two)));
  TEST_ASSERT_EQUAL_INT('a', two[0]);
  TEST_ASSERT_EQUAL_INT('b', two[1]);
  TEST_ASSERT_EQUAL_INT('c', src.read());  // the rest is still read in order
}

void test_json_after_the_headers() {
  // map_client: the headers and the JSON body through the same source, so nothing read ahead is lost
  FakeSource src = at("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"a\":1,\"b\":[2,3]}\n", 0);
  src.closes = true;
  std::string line;
  do {
    TEST_ASSERT_TRUE(readLine(src, 5000, line) == NetResult::Ok);
  } while (!line.empty());
  PatientReader reader(src, 5000);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, reader) == DeserializationError::Ok);
  TEST_ASSERT_EQUAL_INT(1, doc["a"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, doc["b"][1].as<int>());
}

// ---- status lines ----

void test_status_codes() {
  TEST_ASSERT_EQUAL_INT(200, httpStatusCode("HTTP/1.0 200 OK"));
  TEST_ASSERT_EQUAL_INT(404, httpStatusCode("HTTP/1.1 404 Not Found"));
  TEST_ASSERT_EQUAL_INT(302, httpStatusCode("HTTP/2 302"));
  TEST_ASSERT_EQUAL_INT(200, httpStatusCode("ICY 200 OK"));  // Shoutcast
  TEST_ASSERT_EQUAL_INT(0, httpStatusCode("RIFF...."));
  TEST_ASSERT_EQUAL_INT(0, httpStatusCode("HTTP/1.0"));
  TEST_ASSERT_EQUAL_INT(0, httpStatusCode("HTTP/1.0 2x0 OK"));
  TEST_ASSERT_EQUAL_INT(0, httpStatusCode(""));
}

// ---- a JSON body through PatientReader ----

void test_json_that_comes_slowly() {
  std::string body = "{\"a\":1,\"b\":[2,3]}";
  std::vector<uint32_t> times(body.size(), 0);
  for (size_t i = 8; i < times.size(); i++) times[i] = 3000;  // a 3 s pause mid-body
  FakeSource src(body, times);
  src.turn = 5;
  PatientReader reader(src, 5000);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, reader) == DeserializationError::Ok);
  TEST_ASSERT_EQUAL_INT(3, doc["b"][1].as<int>());
  TEST_ASSERT_EQUAL_INT(600, src.waits);  // it yielded through the pause
}

void test_json_that_stalls() {
  // the Chart stops mid-body (busy sending an ATIS): incomplete after the idle time, not a spin until the watchdog
  std::string body = "{\"a\":1,\"b\":[2,3]}";
  std::vector<uint32_t> times(body.size(), 0);
  for (size_t i = 8; i < times.size(); i++) times[i] = 60000;
  FakeSource src(body, times);
  src.turn = 5;
  PatientReader reader(src, 5000);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, reader) == DeserializationError::IncompleteInput);
  TEST_ASSERT_TRUE(reader.last() == NetResult::TimedOut);
  TEST_ASSERT_UINT32_WITHIN(10, 5000, src.clock);
}

void test_json_cut_off() {
  FakeSource src = at("{\"a\":1,", 0);
  src.closes = true;
  PatientReader reader(src, 5000);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, reader) == DeserializationError::IncompleteInput);
  TEST_ASSERT_TRUE(reader.last() == NetResult::Closed);
  TEST_ASSERT_EQUAL_INT(0, src.waits);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line_at_once);
  RUN_TEST(test_a_slow_answer_waits_in_turns);
  RUN_TEST(test_a_line_in_pieces);
  RUN_TEST(test_no_answer_by_the_deadline);
  RUN_TEST(test_the_deadline_across_the_clock_wrap);
  RUN_TEST(test_stop_ends_the_wait);
  RUN_TEST(test_closed_without_answering);
  RUN_TEST(test_the_last_line_without_a_newline);
  RUN_TEST(test_a_blank_line);
  RUN_TEST(test_a_long_line_is_cut);
  RUN_TEST(test_a_line_that_never_ends);
  RUN_TEST(test_a_line_already_here_at_the_deadline);
  RUN_TEST(test_a_line_half_here_at_the_deadline);
  RUN_TEST(test_a_cut_line_past_the_deadline_ends);
  RUN_TEST(test_a_long_header_line);
  RUN_TEST(test_reads_in_blocks_not_bytes);
  RUN_TEST(test_a_block_takes_only_what_has_come);
  RUN_TEST(test_the_body_after_the_headers_is_not_lost);
  RUN_TEST(test_take_unread_in_parts);
  RUN_TEST(test_json_after_the_headers);
  RUN_TEST(test_status_codes);
  RUN_TEST(test_json_that_comes_slowly);
  RUN_TEST(test_json_that_stalls);
  RUN_TEST(test_json_cut_off);
  return UNITY_END();
}
