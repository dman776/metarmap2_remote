#include "log_ring.h"

#include <algorithm>

void LogRing::write(const char* text, size_t length) {
  if (!cap_) return;
  if (length > cap_) {  // only the newest part fits
    text += length - cap_;
    end_ += length - cap_;
    length = cap_;
  }
  size_t at = (size_t)(end_ % cap_);
  size_t first = std::min(length, cap_ - at);
  std::copy(text, text + first, buf_ + at);
  std::copy(text + first, text + length, buf_);
  end_ += length;
}

std::string LogRing::since(uint64_t& from) const {
  std::string out;
  uint64_t oldest = end_ > cap_ ? end_ - cap_ : 0;
  if (from < oldest) {
    out = "...\n";
    from = oldest;
  }
  if (from > end_) from = end_;  // from a reader who saw a log before a restart
  out.reserve(out.size() + (size_t)(end_ - from));
  for (uint64_t i = from; i < end_; i++) out += buf_[i % cap_];
  from = end_;
  return out;
}
