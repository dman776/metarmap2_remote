// The pieces of streaming audio that don't need the board, unit tested on the
// host (test/test_stream): a byte ring buffer between the network task and the
// decoder, and a resampler to the codec's fixed 16 kHz.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <string>

// http://host[:port]/path or https://...: what the stream client connects to.
struct StreamUrl {
  bool tls = false;
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
};

// False for anything else (no scheme, no host, a user:password@, a bad port).
bool parseStreamUrl(const std::string& url, StreamUrl& out);

// A redirect's Location (absolute, or a path on the same server) as a full URL.
std::string resolveLocation(const StreamUrl& from, const std::string& location);

// A WAV file's format, from its header: 16-bit PCM only (what Piper writes).
struct WavInfo {
  int rate = 0;
  int channels = 0;
  size_t dataOffset = 0;  // where the samples start
};

// 1: `data` (the first `len` bytes) is a 16-bit PCM WAV header, read into `out`; 0: more bytes are needed to tell;
// -1: not one this can play (another kind of file, or a WAV that isn't 16-bit PCM, mono or stereo).
int parseWavHeader(const uint8_t* data, size_t len, WavInfo& out);

// A single-producer, single-consumer ring of bytes over a buffer the caller
// owns (in PSRAM on the board): one task writes, another reads, no lock.
class ByteRing {
 public:
  ByteRing(uint8_t* buffer, size_t size) : buf_(buffer), size_(size) {}

  size_t size() const { return size_; }
  size_t available() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  size_t space() const { return size_ - available(); }

  // Copies in up to `len` bytes; returns how many fit.
  size_t write(const uint8_t* data, size_t len);
  // Copies out up to `len` bytes; returns how many there were.
  size_t read(uint8_t* out, size_t len);
  // Drops everything. Only when neither task is using the ring.
  void clear();

 private:
  uint8_t* buf_;
  size_t size_;
  // Counters that only grow (wrapping at 2^32 or 2^64 is fine: only their
  // difference is used), so a full ring and an empty one differ.
  std::atomic<size_t> head_{0};  // bytes ever written
  std::atomic<size_t> tail_{0};  // bytes ever read
};

// Turns decoded PCM at any rate into stereo 16-bit at outRate, with a volume.
// Integrate-and-dump: each output sample is the average of the input over its
// span (a box filter, so downsampling doesn't alias much, and a rate below
// outRate is held, which is fine for voice). Integer math, exact ratio.
class Resampler {
 public:
  explicit Resampler(int outRate) : outRate_(outRate) {}

  // Starts over for input at inRate with `channels` (1 or 2) interleaved.
  void configure(int inRate, int channels);
  // 0-256: 256 is full scale.
  void setVolume(int volume) { volume_ = volume < 0 ? 0 : (volume > 256 ? 256 : volume); }

  // Feeds `samples` interleaved input samples (frames * channels). Writes
  // stereo frames (left, right) to out, at most outFrames of them, and returns
  // how many. Size `out` with maxFrames(): input that gives more is dropped.
  size_t process(const int16_t* in, size_t samples, int16_t* out, size_t outFrames);

  // The most stereo frames processing `samples` input samples can give.
  size_t maxFrames(size_t samples) const;

  int inRate() const { return inRate_; }
  int channels() const { return channels_; }

 private:
  int outRate_;
  int inRate_ = 0;
  int channels_ = 1;
  int volume_ = 256;
  int64_t acc_ = 0;  // the weighted sum of the output sample being built
  int need_ = 0;     // weight still to add to it (inRate_ units when it's whole)
};
