// The recent log, for the remote's web page: a ring buffer of text that keeps the newest bytes. Positions count
// every byte ever written, so a reader asks for what came after the last position it saw. Plain C++ (no locking:
// log.cpp locks around it), so it's unit tested on the host (test/test_log_ring).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class LogRing {
 public:
  LogRing(char* buffer, size_t capacity) : buf_(buffer), cap_(capacity) {}

  void write(const char* text, size_t length);

  // Where the next byte will go: every byte ever written
  uint64_t end() const { return end_; }

  // The text from `from` to the end, and `from` moved to the end. What's been overwritten since is skipped (it
  // starts from the oldest byte kept, after a "..." line).
  std::string since(uint64_t& from) const;

 private:
  char* buf_;
  size_t cap_;
  uint64_t end_ = 0;
};
