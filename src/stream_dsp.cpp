#include "stream_dsp.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

size_t ByteRing::write(const uint8_t* data, size_t len) {
  size_t n = std::min(len, space());
  size_t pos = head_.load(std::memory_order_relaxed) % size_;
  size_t first = std::min(n, size_ - pos);
  memcpy(buf_ + pos, data, first);
  memcpy(buf_, data + first, n - first);
  head_.fetch_add(n, std::memory_order_release);
  return n;
}

size_t ByteRing::read(uint8_t* out, size_t len) {
  size_t n = std::min(len, available());
  size_t pos = tail_.load(std::memory_order_relaxed) % size_;
  size_t first = std::min(n, size_ - pos);
  memcpy(out, buf_ + pos, first);
  memcpy(out + first, buf_, n - first);
  tail_.fetch_add(n, std::memory_order_release);
  return n;
}

void ByteRing::clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

void Resampler::configure(int inRate, int channels) {
  inRate_ = inRate;
  channels_ = channels == 2 ? 2 : 1;
  acc_ = 0;
  need_ = inRate;
}

size_t Resampler::maxFrames(size_t samples) const {
  if (inRate_ <= 0) return 0;
  size_t frames = samples / channels_;
  return frames * outRate_ / inRate_ + 2;
}

size_t Resampler::process(const int16_t* in, size_t samples, int16_t* out, size_t outFrames) {
  if (inRate_ <= 0) return 0;
  size_t produced = 0;
  for (size_t i = 0; i + channels_ <= samples; i += channels_) {
    int x = channels_ == 2 ? (in[i] + in[i + 1]) / 2 : in[i];  // the speaker is mono
    int weight = outRate_;  // each input sample carries outRate units; an output sample takes inRate
    while (weight > 0) {
      int take = std::min(weight, need_);
      acc_ += (int64_t)x * take;
      weight -= take;
      need_ -= take;
      if (need_ == 0) {
        int sample = (int)(acc_ / inRate_);
        sample = sample * volume_ / 256;
        if (produced < outFrames) {
          out[2 * produced] = out[2 * produced + 1] = (int16_t)std::max(-32768, std::min(32767, sample));
          produced++;
        }
        acc_ = 0;
        need_ = inRate_;
      }
    }
  }
  return produced;
}

bool parseStreamUrl(const std::string& url, StreamUrl& out) {
  StreamUrl u;
  size_t at = 0;
  if (url.compare(0, 7, "http://") == 0) {
    at = 7;
  } else if (url.compare(0, 8, "https://") == 0) {
    u.tls = true;
    u.port = 443;
    at = 8;
  } else {
    return false;
  }
  size_t slash = url.find('/', at);
  std::string authority = url.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
  if (slash != std::string::npos) u.path = url.substr(slash);
  if (authority.empty() || authority.find('@') != std::string::npos || authority[0] == '[') return false;
  size_t colon = authority.find(':');
  if (colon != std::string::npos) {
    std::string port = authority.substr(colon + 1);
    authority.resize(colon);
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return false;
    long n = strtol(port.c_str(), nullptr, 10);
    if (n < 1 || n > 65535) return false;
    u.port = (uint16_t)n;
  }
  if (authority.empty()) return false;
  u.host = authority;
  if (u.path[0] != '/') u.path = "/" + u.path;
  out = u;
  return true;
}

std::string resolveLocation(const StreamUrl& from, const std::string& location) {
  if (location.compare(0, 7, "http://") == 0 || location.compare(0, 8, "https://") == 0) return location;
  std::string base = std::string(from.tls ? "https://" : "http://") + from.host;
  bool standard = from.port == (from.tls ? 443 : 80);
  if (!standard) base += ":" + std::to_string(from.port);
  return base + (location.empty() || location[0] != '/' ? "/" : "") + location;
}

namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }

}  // namespace

int parseWavHeader(const uint8_t* data, size_t len, WavInfo& out) {
  if (len < 12) return len >= 4 && memcmp(data, "RIFF", 4) != 0 ? -1 : 0;
  if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return -1;
  WavInfo info;
  bool haveFormat = false;
  size_t at = 12;
  while (at + 8 <= len) {  // chunks: an id, a length, the body (padded to an even length)
    uint32_t size = le32(data + at + 4);
    const uint8_t* body = data + at + 8;
    if (memcmp(data + at, "fmt ", 4) == 0) {
      if (at + 8 + 16 > len) return 0;
      uint16_t format = le16(body), channels = le16(body + 2), bits = le16(body + 14);
      // 1: PCM; 0xFFFE: WAVE_FORMAT_EXTENSIBLE, which some tools write for plain PCM too
      if ((format != 1 && format != 0xFFFE) || bits != 16 || channels < 1 || channels > 2) return -1;
      info.rate = (int)le32(body + 4);
      info.channels = channels;
      if (info.rate < 4000 || info.rate > 96000) return -1;
      haveFormat = true;
    } else if (memcmp(data + at, "data", 4) == 0) {
      if (!haveFormat) return -1;
      info.dataOffset = at + 8;
      out = info;
      return 1;
    }
    at += 8 + size + (size & 1);
    if (at > 4096) return -1;  // a header this long isn't one Piper wrote
  }
  return 0;
}

uint32_t pcmMs(uint64_t bytes, int rate, int channels) {
  if (rate <= 0 || channels <= 0) return 0;
  return (uint32_t)(bytes * 1000 / ((uint64_t)rate * channels * 2));
}
