// A client (WiFiClient, WiFiClientSecure) as a NetSource (net_wait.h): waiting on it yields in `turnMs` turns, so
// core 0's idle task runs (and the task watchdog stays quiet) however long the server takes. Read in blocks
// (BufferedSource): code that goes on to read the client itself takes the read-ahead first (takeUnread()).
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "net_wait.h"

class ClientSource : public BufferedSource {
 public:
  explicit ClientSource(Client& client, uint32_t turnMs = 5) : client_(client), turnMs_(turnMs) {}
  uint32_t now() override { return millis(); }
  void wait() override { delay(turnMs_); }

 protected:
  // Never waits: WiFiClient's read(buf, n) gives what lwip has (<= 0 for none); WiFiClientSecure's asks mbedtls
  // what's decrypted (one available() per block) and gives -1 for none, at most what's there.
  int fill(uint8_t* buf, size_t max) override { return client_.read(buf, max); }
  bool connectionOpen() override { return client_.connected() || client_.available() > 0; }

 private:
  Client& client_;
  uint32_t turnMs_;
};
