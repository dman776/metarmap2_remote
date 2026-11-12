#include "net_wait.h"

#include <cstring>

namespace {

bool passed(NetSource& src, uint32_t deadline) { return static_cast<int32_t>(deadline - src.now()) <= 0; }

}  // namespace

int readByte(NetSource& src, uint32_t deadline, NetResult* why) {
  NetResult result;
  for (;;) {
    if (!src.wanted()) {
      result = NetResult::Stopped;
      break;
    }
    int c = src.read();
    if (c >= 0) {
      if (why) *why = NetResult::Ok;
      return c;
    }
    if (!src.open()) {
      result = NetResult::Closed;
      break;
    }
    if (passed(src, deadline)) {
      result = NetResult::TimedOut;
      break;
    }
    src.wait();
  }
  if (why) *why = result;
  return -1;
}

int BufferedSource::read() {
  if (pos_ == len_) {
    int n = fill(buf_, sizeof(buf_));
    if (n <= 0) return -1;
    pos_ = 0;
    len_ = static_cast<size_t>(n) < sizeof(buf_) ? static_cast<size_t>(n) : sizeof(buf_);
  }
  return buf_[pos_++];
}

size_t BufferedSource::takeUnread(uint8_t* out, size_t max) {
  size_t n = unread() < max ? unread() : max;
  if (n) memcpy(out, buf_ + pos_, n);
  pos_ += n;
  return n;
}

NetResult readLine(NetSource& src, uint32_t deadline, std::string& out, size_t maxLen) {
  out.clear();
  for (;;) {
    NetResult why;
    int c = readByte(src, deadline, &why);
    if (c < 0) {
      if (why == NetResult::Closed && !out.empty()) break;  // its last line, without a newline
      return why;
    }
    if (c == '\n') break;
    if (out.size() < maxLen) {
      out.push_back(static_cast<char>(c));
    } else if (passed(src, deadline)) {
      return NetResult::TimedOut;  // a line that never ends, and keeps coming faster than it's waited for
    }
  }
  if (!out.empty() && out.back() == '\r') out.pop_back();
  return NetResult::Ok;
}

int httpStatusCode(const std::string& line) {
  size_t at;
  if (line.compare(0, 5, "HTTP/") == 0) {
    at = line.find(' ');
    if (at == std::string::npos) return 0;
    at++;
  } else if (line.compare(0, 4, "ICY ") == 0) {
    at = 4;
  } else {
    return 0;
  }
  if (line.size() < at + 3) return 0;
  int code = 0;
  for (size_t i = at; i < at + 3; i++) {
    if (line[i] < '0' || line[i] > '9') return 0;
    code = code * 10 + (line[i] - '0');
  }
  return code;
}

size_t PatientReader::readBytes(char* buffer, size_t length) {
  size_t n = 0;
  while (n < length) {
    int c = read();
    if (c < 0) break;
    buffer[n++] = static_cast<char>(c);
  }
  return n;
}
