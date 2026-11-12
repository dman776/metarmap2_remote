// Waiting on the network without spinning. On the board, Stream::readStringUntil() and readBytes() (and so
// deserializeJson() on a client) poll a non-blocking socket without ever yielding: a server slow to answer, or one
// that stalls mid-answer (the Chart rendering an ATIS, or busy sending one), starves core 0's idle task and the
// task watchdog restarts the board after 5 s. These wait in turns instead, and give up at a deadline, when the
// connection closes, or when the answer is no longer wanted (Stop, or another Play). What they need from the
// connection and the clock is behind NetSource (net_client.h on the board), so they run on the host too.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class NetSource {
 public:
  virtual ~NetSource() = default;
  virtual uint32_t now() = 0;             // ms (wrapping is fine)
  virtual int read() = 0;                 // the next byte, or -1 if none has come yet
  virtual bool open() = 0;                // more may still come
  virtual bool wanted() { return true; }  // the answer is still wanted
  virtual void wait() = 0;                // gives the other tasks a turn (a short delay on the board)
};

// A connection read in small blocks (fill()), each byte then handed out by read(): one call into the network stack per
// block, not one per byte (WiFiClient::available() is an lwip ioctl; on WiFiClientSecure it's two mbedtls reads).
// A block may hold bytes past what the reader wanted (the start of a body after the headers): code that goes on to
// read the connection itself must first take them with takeUnread(), or they're lost.
class BufferedSource : public NetSource {
 public:
  static constexpr size_t kBlock = 128;  // on the reading task's stack, inside this object

  int read() override;
  bool open() override { return pos_ < len_ || connectionOpen(); }
  size_t unread() const { return len_ - pos_; }
  // Moves up to `max` read-ahead bytes into `out` (they're then gone from here); how many.
  size_t takeUnread(uint8_t* out, size_t max);

 protected:
  // Reads what has come, up to `max` bytes, without waiting: how many, or <= 0 for none yet.
  virtual int fill(uint8_t* buf, size_t max) = 0;
  virtual bool connectionOpen() = 0;  // more may still come from the connection

 private:
  uint8_t buf_[kBlock] = {};
  size_t pos_ = 0, len_ = 0;
};

enum class NetResult {
  Ok,        // a byte, or a line in `out` (without its "\r\n"; or what came before the connection closed)
  TimedOut,  // nothing (more) by the deadline
  Closed,    // the connection closed with nothing (more) said
  Stopped,   // no longer wanted
};

// The next byte by `deadline` (src.now()'s clock), or -1 with why in `why`.
int readByte(NetSource& src, uint32_t deadline, NetResult* why = nullptr);

// Reads up to the next "\n" into `out` by `deadline`. A longer line than `maxLen` is cut there (the rest of it read
// and dropped). The deadline counts only when it would have to wait for more: a line already here is read whole,
// even at the deadline (except one already past `maxLen`, so one that never ends still ends).
constexpr size_t kStatusLineMax = 512;
constexpr size_t kHeaderLineMax = 2048;  // a Location can be long (a signed URL); the line is on the heap, not stack
NetResult readLine(NetSource& src, uint32_t deadline, std::string& out, size_t maxLen = kStatusLineMax);

// The code in an HTTP status line ("HTTP/1.0 200 OK", or Shoutcast's "ICY 200 OK"); 0 if it isn't one.
int httpStatusCode(const std::string& line);

// A body for deserializeJson() (ArduinoJson takes any reader with these two): each byte waited for in turns, up to
// `idleMs` after the last; then it reads as the end, and the JSON as incomplete.
class PatientReader {
 public:
  PatientReader(NetSource& src, uint32_t idleMs) : src_(src), idleMs_(idleMs) {}
  int read() { return readByte(src_, src_.now() + idleMs_, &last_); }
  size_t readBytes(char* buffer, size_t length);
  NetResult last() const { return last_; }  // why the last read() came back empty

 private:
  NetSource& src_;
  uint32_t idleMs_;
  NetResult last_ = NetResult::Ok;
};
